import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:crypto/crypto.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/ssh_service.dart';

import 'support.dart';

/// The browser tunnel (SOCKS5 over the SSH session), against a real server.
///
/// The target is a web server on an address that exists only on the server:
/// `fortiq-only.test` -> 127.0.0.2 in the server's /etc/hosts. If a page comes
/// back, the name was resolved by the server and the connection was made from
/// there. This machine could not have fetched it directly.
void main() {
  final h = IntegrationHarness();

  const host = 'fortiq-only.test';
  const webPort = 8099;

  setUp(() async {
    // setUp of the harness already ran (restoreBaseline); now the web server.
    await h.server.root('''
grep -q "$host" /etc/hosts || echo "127.0.0.2 $host" >> /etc/hosts
mkdir -p /tmp/fqweb
echo "hello from the server" > /tmp/fqweb/index.txt
head -c 3000000 /dev/urandom > /tmp/fqweb/big.bin
pkill -f "http.server $webPort" || true
cd /tmp/fqweb
nohup python3 -m http.server $webPort --bind 127.0.0.2 >/tmp/fqweb/log 2>&1 &
# wait until it really answers, instead of hoping that a pause is long enough
for i in \$(seq 1 100); do
  curl -s -o /dev/null http://$host:$webPort/index.txt && exit 0
  sleep 0.1
done
echo "the test web server did not start" >&2
exit 1
''');
  });
  tearDown(() => h.server.root('pkill -f "http.server $webPort"; exit 0'));

  /// A minimal SOCKS5 client: CONNECT to [target] given by name, then send [request].
  Future<({int reply, List<int> body})> viaSocks(
    int proxyPort,
    String target,
    int port, {
    String request = '',
    int atyp = 3,
  }) async {
    final socket = await Socket.connect('127.0.0.1', proxyPort, timeout: const Duration(seconds: 5));
    final input = StreamIterator<Uint8List>(socket);
    final pending = <int>[]; // bytes received but not consumed yet
    Future<List<int>> read(int n) async {
      while (pending.length < n) {
        if (!await input.moveNext()) break;
        pending.addAll(input.current);
      }
      final take = pending.length < n ? pending.length : n;
      final out = pending.sublist(0, take);
      pending.removeRange(0, take);
      return out;
    }

    try {
      socket.add([5, 1, 0]);
      final greeting = await read(2);
      expect(greeting, [5, 0], reason: 'no authentication');
      final name = utf8.encode(target);
      socket.add([5, 1, 0, atyp, name.length, ...name, port >> 8, port & 0xff]);
      final head = await read(4);
      final reply = head.length >= 2 ? head[1] : -1;
      if (reply != 0) return (reply: reply, body: <int>[]);
      // bound address: IPv4 = 4+2 bytes more
      await read(head[3] == 1 ? 6 : head[3] == 4 ? 18 : 0);
      socket.add(utf8.encode(request));
      final body = <int>[...pending];
      pending.clear();
      while (await input.moveNext()) {
        body.addAll(input.current);
      }
      return (reply: 0, body: body);
    } finally {
      socket.destroy();
    }
  }

  String text(List<int> body) => utf8.decode(body, allowMalformed: true);

  group('the tunnel', () {
    test('reaches a name that only the server can resolve', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      final port = await ssh.startSocksTunnel();

      final r = await viaSocks(port, host, webPort, request: 'GET /index.txt HTTP/1.0\r\n\r\n');

      expect(r.reply, 0);
      expect(text(r.body), contains('200 OK'));
      expect(text(r.body), contains('hello from the server'));
      // and it really could not be reached from here
      await expectLater(
        Socket.connect('127.0.0.2', webPort, timeout: const Duration(seconds: 2)),
        throwsA(isA<SocketException>()),
        reason: 'the page is on the server, not on this computer',
      );
    });

    test('a name the server cannot resolve is reported, not sent anywhere else', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      final port = await ssh.startSocksTunnel();

      final r = await viaSocks(port, 'no-such-host.invalid', 80);

      expect(r.reply, isNot(0));
    });

    test('a closed port on the server gives a refused connection', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      final port = await ssh.startSocksTunnel();

      final r = await viaSocks(port, host, 9); // nothing listens on 9

      expect(r.reply, isNot(0));
    });

    test('a download of three megabytes arrives intact', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      final port = await ssh.startSocksTunnel();

      final r = await viaSocks(port, host, webPort, request: 'GET /big.bin HTTP/1.0\r\n\r\n');
      final body = Uint8List.fromList(r.body);
      final split = _indexOfHeaderEnd(body);
      final received = body.sublist(split);

      final onServer = (await ssh.execute('sha256sum /tmp/fqweb/big.bin | cut -d" " -f1')).trim();
      expect(received.length, 3000000);
      expect(_sha256Hex(received), onServer);
    });

    test('many connections at once all get through', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      final port = await ssh.startSocksTunnel();

      final outcomes = await Future.wait([
        for (var i = 0; i < 40; i++)
          viaSocks(port, host, webPort, request: 'GET /index.txt HTTP/1.0\r\n\r\n')
              .then((r) => r.reply == 0 && text(r.body).contains('hello from the server')
                  ? 'ok'
                  : 'reply=${r.reply} body=${r.body.length}')
              .catchError((Object e) => 'error: $e'),
      ]);

      final bad = outcomes.where((o) => o != 'ok').toList();
      expect(bad, isEmpty, reason: '${bad.length} of 40 failed: ${bad.toSet()}');
    });
  });

  group('the tunnel on and off', () {
    test('it cannot start without a session', () async {
      final ssh = SshClientService();
      await expectLater(ssh.startSocksTunnel(), throwsA(predicate((e) => '$e'.contains('Not connected'))));
      expect(ssh.isTunnelActive, isFalse);
    });

    test('starting twice gives the same port and one listener', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      final a = await ssh.startSocksTunnel();
      final b = await ssh.startSocksTunnel();
      expect(b, a);
      expect(ssh.isTunnelActive, isTrue);
      expect(a, await SshClientService.browserProxyPort());
    });

    test('stopped, the proxy port refuses connections: nothing can go around it', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      final port = await ssh.startSocksTunnel();

      ssh.stopSocksTunnel();

      expect(ssh.isTunnelActive, isFalse);
      await Future.delayed(const Duration(milliseconds: 300));
      await expectLater(
        Socket.connect('127.0.0.1', port, timeout: const Duration(seconds: 2)),
        throwsA(isA<SocketException>()),
      );
    });

    test('it can be started again on the same port after a stop', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      final first = await ssh.startSocksTunnel();
      ssh.stopSocksTunnel();
      final second = await ssh.startSocksTunnel();
      expect(second, first);
      final r = await viaSocks(second, host, webPort, request: 'GET /index.txt HTTP/1.0\r\n\r\n');
      expect(text(r.body), contains('hello from the server'));
    });

    test('disconnecting closes the proxy', () async {
      final ssh = await h.server.connect();
      final port = await ssh.startSocksTunnel();
      ssh.disconnect();
      expect(ssh.isTunnelActive, isFalse);
      await Future.delayed(const Duration(milliseconds: 300));
      await expectLater(
        Socket.connect('127.0.0.1', port, timeout: const Duration(seconds: 2)),
        throwsA(isA<SocketException>()),
      );
    });

    test('the proxy only listens on this computer', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      await ssh.startSocksTunnel();
      // no other interface address of this machine accepts connections
      final port = await SshClientService.browserProxyPort();
      for (final iface in await NetworkInterface.list()) {
        for (final a in iface.addresses.where((a) => a.type == InternetAddressType.IPv4 && !a.isLoopback)) {
          await expectLater(
            Socket.connect(a, port, timeout: const Duration(seconds: 1)),
            throwsA(anything),
            reason: '${a.address} must not accept proxy connections',
          );
        }
      }
    });
  });
}

int _indexOfHeaderEnd(Uint8List b) {
  for (var i = 0; i + 3 < b.length; i++) {
    if (b[i] == 13 && b[i + 1] == 10 && b[i + 2] == 13 && b[i + 3] == 10) return i + 4;
  }
  return 0;
}

String _sha256Hex(List<int> data) => sha256.convert(data).toString();
