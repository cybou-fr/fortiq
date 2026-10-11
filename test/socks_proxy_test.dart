import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:dartssh2/dartssh2.dart' show SSHSocket;
import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/socks_proxy.dart';

/// The far end of the tunnel in these tests: what the server would be.
class FakeRemote implements SSHSocket {
  final fromServer = StreamController<Uint8List>();
  final received = <int>[];
  var sinkClosed = false;
  var destroyed = false;
  late final StreamController<List<int>> _toServer = StreamController<List<int>>()
    ..stream.listen(received.addAll, onDone: () => sinkClosed = true);

  /// The server sends [data].
  void say(List<int> data) => fromServer.add(Uint8List.fromList(data));

  /// The server closes its side.
  Future<void> hangUp() => fromServer.close();

  @override
  Stream<Uint8List> get stream => fromServer.stream;

  @override
  StreamSink<List<int>> get sink => _toServer.sink;

  @override
  Future<void> get done => Completer<void>().future;

  @override
  Future<void> close() async => sink.close();

  @override
  void destroy() => destroyed = true;

  @override
  Future<void> flush() async {}
}

/// A SOCKS5 client that keeps what it has read but not yet consumed.
class Client {
  final Socket socket;
  final StreamIterator<Uint8List> _in;
  final _pending = <int>[];

  Client._(this.socket) : _in = StreamIterator(socket);

  static Future<Client> open(int port) async =>
      Client._(await Socket.connect('127.0.0.1', port, timeout: const Duration(seconds: 3)));

  Future<List<int>> read(int n) async {
    while (_pending.length < n) {
      if (!await _in.moveNext().timeout(const Duration(seconds: 5), onTimeout: () => false)) break;
      _pending.addAll(_in.current);
    }
    final take = _pending.length < n ? _pending.length : n;
    final out = _pending.sublist(0, take);
    _pending.removeRange(0, take);
    return out;
  }

  /// Everything until the other side closes.
  Future<List<int>> readToEnd() async {
    final out = <int>[..._pending];
    _pending.clear();
    while (await _in.moveNext().timeout(const Duration(seconds: 10), onTimeout: () => false)) {
      out.addAll(_in.current);
    }
    return out;
  }

  void send(List<int> bytes) => socket.add(bytes);

  /// Greeting offering "no authentication", answered by [5, 0].
  Future<void> greet() async {
    send([5, 1, 0]);
    expect(await read(2), [5, 0]);
  }

  static List<int> domainRequest(String host, int port, {int cmd = 1}) =>
      [5, cmd, 0, 3, host.length, ...host.codeUnits, port >> 8, port & 0xff];

  /// Sends a CONNECT and returns the reply code (the 10-byte reply is consumed).
  Future<int> connect(String host, int port) async {
    send(domainRequest(host, port));
    final r = await read(10);
    return r.length >= 2 ? r[1] : -1;
  }

  void close() => socket.destroy();
}

void main() {
  late SocksProxy proxy;
  late Map<String, FakeRemote> remotes;
  late List<(String, int)> dialed;
  Future<SSHSocket> Function(String, int)? onDial;

  Future<SocksProxy> start({
    int maxConnections = 512,
    Duration handshakeTimeout = const Duration(seconds: 10),
    Duration connectTimeout = const Duration(seconds: 20),
    Duration linger = const Duration(seconds: 10),
  }) =>
      SocksProxy.bind(
        maxConnections: maxConnections,
        handshakeTimeout: handshakeTimeout,
        connectTimeout: connectTimeout,
        lingerAfterRemoteClose: linger,
        dial: (host, port) async {
          dialed.add((host, port));
          if (onDial != null) return onDial!(host, port);
          return remotes.putIfAbsent('$host:$port', FakeRemote.new);
        },
      );

  setUp(() async {
    remotes = {};
    dialed = [];
    onDial = null;
    proxy = await start();
  });
  tearDown(() => proxy.close());

  group('the handshake', () {
    test('accepts "no authentication" and nothing else', () async {
      final c = await Client.open(proxy.port);
      c.send([5, 1, 0]);
      expect(await c.read(2), [5, 0]);
      c.close();
    });

    test('refuses a client that offers only authentication methods it does not have', () async {
      final c = await Client.open(proxy.port);
      c.send([5, 1, 2]); // username/password only
      expect(await c.read(2), [5, 0xFF]);
      expect(await c.readToEnd(), isEmpty, reason: 'then it hangs up');
    });

    test('refuses another SOCKS version', () async {
      final c = await Client.open(proxy.port);
      c.send([4, 1, 0]);
      expect(await c.read(2), [5, 0xFF]);
    });

    test('works when the bytes arrive one at a time', () async {
      final c = await Client.open(proxy.port);
      for (final b in [5, 1, 0]) {
        c.send([b]);
        await Future.delayed(const Duration(milliseconds: 10));
      }
      expect(await c.read(2), [5, 0]);
      final request = Client.domainRequest('slow.example', 80);
      for (final b in request) {
        c.send([b]);
        await Future.delayed(const Duration(milliseconds: 5));
      }
      expect((await c.read(10))[1], 0);
      expect(dialed, [('slow.example', 80)]);
    });

    test('connects by name: the name is handed on unresolved', () async {
      final c = await Client.open(proxy.port);
      await c.greet();
      expect(await c.connect('internal.service.lan', 8443), 0);
      expect(dialed, [('internal.service.lan', 8443)]);
    });

    test('connects by IPv4 address', () async {
      final c = await Client.open(proxy.port);
      await c.greet();
      c.send([5, 1, 0, 1, 10, 1, 2, 3, 0x1F, 0x90]);
      expect((await c.read(10))[1], 0);
      expect(dialed, [('10.1.2.3', 8080)]);
    });

    test('connects by IPv6 address', () async {
      final c = await Client.open(proxy.port);
      await c.greet();
      c.send([5, 1, 0, 4, ...List.filled(15, 0), 1, 0x01, 0xBB]);
      expect((await c.read(10))[1], 0);
      expect(dialed, [('::1', 443)]);
    });

    test('only CONNECT is supported: BIND and UDP are refused with "command not supported"', () async {
      for (final cmd in [2, 3]) {
        final c = await Client.open(proxy.port);
        await c.greet();
        c.send(Client.domainRequest('x.example', 80, cmd: cmd));
        expect((await c.read(10))[1], 7);
        expect(dialed, isEmpty);
      }
    });

    test('an unknown address type is refused', () async {
      final c = await Client.open(proxy.port);
      await c.greet();
      c.send([5, 1, 0, 9, 0, 0, 0, 0]);
      expect((await c.read(10))[1], 8);
    });

    test('a client that never says anything is dropped after the handshake timeout', () async {
      await proxy.close();
      proxy = await start(handshakeTimeout: const Duration(milliseconds: 300));
      final c = await Client.open(proxy.port);
      expect(await c.readToEnd(), isEmpty);
      expect(proxy.openConnections, 0);
    });

    test('a "handshake" that never ends is cut off at 32 KB', () async {
      final c = await Client.open(proxy.port);
      c.send([5, 255, ...List.filled(40000, 0)]);
      expect(await c.readToEnd(), isEmpty);
    });
  });

  group('when the server side fails', () {
    test('a refused connection is reported as such', () async {
      onDial = (h, p) async => throw const SocketException('Connection refused');
      final c = await Client.open(proxy.port);
      await c.greet();
      expect(await c.connect('x.example', 9), 5);
    });

    test('any other failure is "host unreachable"', () async {
      onDial = (h, p) async => throw Exception('no route');
      final c = await Client.open(proxy.port);
      await c.greet();
      expect(await c.connect('x.example', 80), 4);
    });

    test('a server that never answers the dial times out as "host unreachable"', () async {
      await proxy.close();
      proxy = await start(connectTimeout: const Duration(milliseconds: 300));
      onDial = (h, p) => Completer<SSHSocket>().future;
      final c = await Client.open(proxy.port);
      await c.greet();
      expect(await c.connect('x.example', 80), 4);
    });
  });

  group('moving data', () {
    test('data goes both ways', () async {
      final c = await Client.open(proxy.port);
      await c.greet();
      await c.connect('echo.example', 7);
      final remote = remotes['echo.example:7']!;

      c.send('ping'.codeUnits);
      await Future.delayed(const Duration(milliseconds: 100));
      expect(String.fromCharCodes(remote.received), 'ping');

      remote.say('pong'.codeUnits);
      expect(String.fromCharCodes(await c.read(4)), 'pong');
    });

    test('bytes sent right behind the request are not lost', () async {
      final c = await Client.open(proxy.port);
      await c.greet();
      // request and first payload in one write
      c.send([...Client.domainRequest('fast.example', 80), ...'HELLO'.codeUnits]);
      await c.read(10);
      await Future.delayed(const Duration(milliseconds: 100));
      expect(String.fromCharCodes(remotes['fast.example:80']!.received), 'HELLO');
    });

    test('when the server closes first, everything it sent still arrives (a megabyte)', () async {
      final c = await Client.open(proxy.port);
      await c.greet();
      await c.connect('big.example', 80);
      final remote = remotes['big.example:80']!;
      final payload = List.generate(1024 * 1024, (i) => i % 251);

      remote.say(payload);
      await remote.hangUp(); // the server closes right behind its last byte

      final got = await c.readToEnd();
      expect(got.length, payload.length);
      expect(got, payload);
    });

    test('data in small pieces followed by an immediate close arrives complete', () async {
      for (var round = 0; round < 20; round++) {
        final c = await Client.open(proxy.port);
        await c.greet();
        await c.connect('piece$round.example', 80);
        final remote = remotes['piece$round.example:80']!;
        final expected = <int>[];
        for (var i = 0; i < 50; i++) {
          final piece = List.filled(1000, i);
          expected.addAll(piece);
          remote.say(piece);
        }
        await remote.hangUp();
        expect((await c.readToEnd()).length, expected.length, reason: 'round $round');
      }
    });

    test('when the client finishes sending, the server is told, and can still answer', () async {
      final c = await Client.open(proxy.port);
      await c.greet();
      await c.connect('http10.example', 80);
      final remote = remotes['http10.example:80']!;

      c.send('GET / HTTP/1.0\r\n\r\n'.codeUnits);
      await c.socket.close(); // the client is done sending, not receiving
      await Future.delayed(const Duration(milliseconds: 200));
      expect(remote.sinkClosed, isTrue, reason: 'the server sees the end of the request');

      remote.say('reply after the request ended'.codeUnits);
      await remote.hangUp();
      expect(String.fromCharCodes(await c.readToEnd()), 'reply after the request ended');
    });

    test('a client that never closes does not hold the connection after the server is gone', () async {
      await proxy.close();
      proxy = await start(linger: const Duration(milliseconds: 300));
      final c = await Client.open(proxy.port);
      await c.greet();
      await c.connect('linger.example', 80);
      final remote = remotes['linger.example:80']!;
      await remote.hangUp();

      await Future.delayed(const Duration(milliseconds: 800));
      expect(proxy.openConnections, 0);
      expect(remote.destroyed, isTrue);
    });

    test('many connections at once do not mix their data up', () async {
      final clients = await Future.wait([
        for (var i = 0; i < 60; i++)
          () async {
            final c = await Client.open(proxy.port);
            await c.greet();
            expect(await c.connect('host$i.example', 80), 0);
            return c;
          }(),
      ]);
      for (var i = 0; i < clients.length; i++) {
        remotes['host$i.example:80']!.say('answer-$i'.codeUnits);
      }
      for (var i = 0; i < clients.length; i++) {
        final want = 'answer-$i';
        expect(String.fromCharCodes(await clients[i].read(want.length)), want);
      }
    });
  });

  group('the proxy itself', () {
    test('listens on this computer only', () async {
      final s = await Socket.connect('127.0.0.1', proxy.port);
      expect(s.remoteAddress.isLoopback, isTrue);
      s.destroy();
      for (final iface in await NetworkInterface.list()) {
        for (final a in iface.addresses.where((a) => a.type == InternetAddressType.IPv4 && !a.isLoopback)) {
          await expectLater(
            Socket.connect(a, proxy.port, timeout: const Duration(seconds: 1)),
            throwsA(anything),
            reason: '${a.address} must not reach the proxy',
          );
        }
      }
    });

    test('it stops accepting at the connection limit and accepts again when one ends', () async {
      await proxy.close();
      proxy = await start(maxConnections: 2);
      final a = await Client.open(proxy.port);
      final b = await Client.open(proxy.port);
      await a.greet();
      await b.greet();

      final third = await Client.open(proxy.port); // accepted by the OS, then dropped
      third.send([5, 1, 0]);
      expect(await third.readToEnd(), isEmpty);

      a.close();
      await Future.delayed(const Duration(milliseconds: 200));
      final fourth = await Client.open(proxy.port);
      await fourth.greet();
    });

    test('closing it refuses new connections and ends the open ones', () async {
      final c = await Client.open(proxy.port);
      await c.greet();
      await c.connect('open.example', 80);
      final remote = remotes['open.example:80']!;
      final port = proxy.port;

      await proxy.close();

      expect(proxy.isClosed, isTrue);
      expect(await c.readToEnd(), isEmpty);
      expect(remote.destroyed, isTrue);
      await expectLater(
        Socket.connect('127.0.0.1', port, timeout: const Duration(seconds: 1)),
        throwsA(isA<SocketException>()),
      );
    });

    test('closing twice is harmless', () async {
      await proxy.close();
      await proxy.close();
    });

    test('the same port can be used again right after closing', () async {
      final port = proxy.port;
      await proxy.close();
      proxy = await SocksProxy.bind(port: port, dial: (h, p) async => FakeRemote());
      expect(proxy.port, port);
    });
  });
}
