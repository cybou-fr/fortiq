import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/shell.dart';
import 'package:fortiq/services/ssh_service.dart';

import 'support.dart';

/// Ports, interfaces and the firewall of a real server.
void main() {
  final h = IntegrationHarness();

  late SshClientService ssh; // fquser: sudo without password
  late SshClientService plain; // fqplain: no sudo
  late SshClientService root;

  setUp(() async {
    ssh = await h.server.connect(user: 'fquser');
    plain = await h.server.connect(user: 'fqplain');
    root = await h.server.connect(user: 'root');
  });
  tearDown(() async {
    ssh.disconnect();
    plain.disconnect();
    root.disconnect();
    await h.server.root('systemctl stop "fq-test-udp" 2>/dev/null; exit 0');
  });

  group('when the tools are missing', () {
    setUp(() => h.server.purge(['iproute2']));
    tearDown(() => h.server.install(['iproute2']));

    test('the ports say that ss/netstat are missing, not "no ports"', () async {
      await expectLater(
        ssh.network.fetchOpenPorts(),
        throwsA(predicate((e) => '$e'.contains('Neither ss nor netstat'))),
      );
    });

    test('the interfaces say that ip is missing', () async {
      await expectLater(
        ssh.network.fetchNetworkInterfaces(),
        throwsA(predicate((e) => '$e'.contains('ip command is not installed'))),
      );
    });
  });

  group('ports', () {
    setUp(() => h.server.install(['iproute2']));

    test('the SSH port is there, with the server program for root', () async {
      final ports = await root.network.fetchOpenPorts();
      final ssh2222 = ports.firstWhere((p) => p['port'] == 2222 && p['proto'] == 'tcp');
      expect(ssh2222['state'], 'LISTEN');
      expect(ssh2222['process'], contains('sshd'));
      expect(ssh2222['pid'], isA<int>());
      expect(ssh2222['address'], endsWith(':2222'));
    });

    test('an ordinary user sees the ports but not the programs of others', () async {
      final ports = await plain.network.fetchOpenPorts();
      final p = ports.firstWhere((p) => p['port'] == 2222 && p['proto'] == 'tcp');
      expect(p['process'], '--');
      expect(p['pid'], isNull);
    });

    test('a UDP socket is listed as well', () async {
      await h.server.root(
        'systemd-run --quiet --unit=fq-test-udp python3 -c "import socket,time; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind((\'0.0.0.0\', 55555)); time.sleep(300)"; sleep 1',
      );
      final ports = await root.network.fetchOpenPorts();
      final udp = ports.firstWhere((p) => p['port'] == 55555);
      expect(udp['proto'], 'udp');
      expect(udp['state'], 'UNCONN');
      expect(udp['process'], contains('python3'));
    });

    test('IPv6 and wildcard addresses are split into host and port', () async {
      final ports = await root.network.fetchOpenPorts();
      final v6 = ports.where((p) => '${p['address']}'.startsWith('[::]:') && p['port'] == 2222);
      expect(v6, isNotEmpty);
      expect(v6.first['host'], '[::]');
    });
  });

  group('interfaces', () {
    setUp(() => h.server.install(['iproute2']));

    test('the loopback and the real network are listed with their addresses', () async {
      final list = await ssh.network.fetchNetworkInterfaces();
      final lo = list.firstWhere((i) => i['interface'] == 'lo' && i['type'] == 'inet');
      expect(lo['ip'], '127.0.0.1/8');
      expect(list.any((i) => i['interface'] != 'lo' && i['type'] == 'inet'), isTrue);
      expect(list.any((i) => i['type'] == 'inet6'), isTrue);
    });
  });

  group('the firewall', () {
    group('before it exists', () {
      setUp(() => h.server.purge(['ufw']));
      tearDown(() => h.server.install(['ufw']));

      test('is reported as not installed', () async {
        final st = await ssh.network.fetchUfwStatus();
        expect(st['installed'], isFalse);
      });
    });

    group('installed but off', () {
      setUp(() async {
        await h.server.install(['ufw']);
        await h.server.root('/usr/sbin/ufw --force disable; /usr/sbin/ufw --force reset >/dev/null');
      });

      test('is installed and inactive, with no rules to show', () async {
        final st = await ssh.network.fetchUfwStatus();
        expect(st['installed'], isTrue);
        expect(st['active'], isFalse);
        expect(st['rules'], isEmpty);
        expect(st['error'], isNull);
      });

      test('without sudo it says that root is needed, not "not installed"', () async {
        final st = await plain.network.fetchUfwStatus();
        expect(st['installed'], isTrue);
        expect(st['error'], contains('needs root'));
      });
    });

    group('running', () {
      setUp(() async {
        await h.server.install(['ufw']);
        await h.server.root('''
/usr/sbin/ufw --force reset >/dev/null
/usr/sbin/ufw default deny incoming >/dev/null
/usr/sbin/ufw allow 2222/tcp >/dev/null
/usr/sbin/ufw --force enable >/dev/null
''');
      });
      tearDown(() => h.server.root('/usr/sbin/ufw --force disable >/dev/null; exit 0'));

      test('shows the status and the numbered rules', () async {
        final st = await ssh.network.fetchUfwStatus();
        expect(st['active'], isTrue);
        final rules = (st['rules'] as List).cast<Map<String, dynamic>>();
        expect(rules.map((r) => r['to']), containsAll(['2222/tcp', '2222/tcp (v6)']));
        expect(rules.first['action'], 'ALLOW');
        expect(rules.first['from'], 'Anywhere');
        expect(rules.first['num'], 1);
      });

      test('a rule can be added and then appears in the list', () async {
        await ssh.network.addUfwRule('8080', 'allow');
        await ssh.network.addUfwRule('9090/tcp', 'deny');
        final rules = ((await ssh.network.fetchUfwStatus())['rules'] as List).cast<Map<String, dynamic>>();
        expect(rules.any((r) => r['to'] == '8080' && r['action'] == 'ALLOW'), isTrue);
        expect(rules.any((r) => r['to'] == '9090/tcp' && r['action'] == 'DENY'), isTrue);
      });

      test('deleting by number removes exactly that rule', () async {
        await ssh.network.addUfwRule('8080', 'allow');
        var rules = ((await ssh.network.fetchUfwStatus())['rules'] as List).cast<Map<String, dynamic>>();
        final num8080 = rules.firstWhere((r) => r['to'] == '8080')['num'] as int;

        await ssh.network.deleteUfwRule(num8080);

        rules = ((await ssh.network.fetchUfwStatus())['rules'] as List).cast<Map<String, dynamic>>();
        expect(rules.any((r) => r['to'] == '8080'), isFalse);
        expect(rules.any((r) => r['to'] == '2222/tcp'), isTrue, reason: 'the others stay');
      });

      test('an action other than a plain word is refused before anything is sent', () async {
        for (final bad in ['allow; reboot', 'Allow', '', 'deny --force']) {
          await expectLater(ssh.network.addUfwRule('80', bad), throwsArgumentError, reason: bad);
        }
      });

      test('a port that is not a port is refused by ufw, as one literal word', () async {
        await expectLater(
          ssh.network.addUfwRule(r"80; touch /tmp/fq-injected #", 'allow'),
          throwsA(isA<SshCommandException>()),
        );
        expect(await ssh.execute('ls /tmp/fq-injected 2>&1 || true'), contains('No such file'));
      });

      test('without sudo a rule cannot be added', () async {
        await expectLater(plain.network.addUfwRule('8080', 'allow'), throwsA(isA<SshCommandException>()));
      });

      test('the SSH connection of the tests stays open with the firewall on', () async {
        expect((await ssh.execute('echo still-here')).trim(), 'still-here');
        expect(await ssh.canLoginAgain(), isTrue);
      });
    });
  });
}
