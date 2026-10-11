import 'dart:convert';
import 'dart:typed_data';

import 'package:dartssh2/dartssh2.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/known_hosts_service.dart';
import 'package:fortiq/services/ssh_service.dart';

import 'support.dart';

/// The SSH connection against a real sshd: who can log in, how the server key
/// is trusted, and how commands behave.
void main() {
  final h = IntegrationHarness();

  group('logging in', () {
    for (final user in ['root', 'fquser', 'fqplain']) {
      test('as $user with the key', () async {
        final ssh = await h.server.connect(user: user);
        addTearDown(ssh.disconnect);

        expect(ssh.isConnected, isTrue);
        expect(ssh.state, SshConnectionState.connected);
        expect(ssh.usedKeyAuth, isTrue);
        expect(ssh.currentUsername, user);
        expect((await ssh.execute('id -un')).trim(), user);
      });
    }

    test('a wrong key is refused and nothing stays open', () async {
      final other = await h.server.root(
        'rm -f /tmp/other_key*; ssh-keygen -q -t ed25519 -N "" -f /tmp/other_key; cat /tmp/other_key',
      );
      final ssh = SshClientService();
      await expectLater(
        ssh.connect(
          host: IntegrationServer.host,
          port: IntegrationServer.port,
          username: 'fquser',
          privateKey: other,
          onHostKey: (_) async => true,
        ),
        throwsA(isA<SSHAuthFailError>()),
      );
      expect(ssh.isConnected, isFalse);
      expect(ssh.state, SshConnectionState.disconnected);
      expect(ssh.isSessionActive, isFalse);
    });

    test('an unknown user is refused', () async {
      final ssh = SshClientService();
      await expectLater(
        ssh.connect(
          host: IntegrationServer.host,
          port: IntegrationServer.port,
          username: 'nobody-here',
          privateKey: h.server.keyPem,
          onHostKey: (_) async => true,
        ),
        throwsA(isA<SSHAuthFailError>()),
      );
      expect(ssh.state, SshConnectionState.disconnected);
    });

    test('a password login is refused when the user has no password', () async {
      final ssh = SshClientService();
      await expectLater(
        ssh.connect(
          host: IntegrationServer.host,
          port: IntegrationServer.port,
          username: 'fquser',
          password: 'wrong',
          onHostKey: (_) async => true,
        ),
        throwsA(isA<SSHAuthFailError>()),
      );
    });

    test('a private key that is not a key says so', () async {
      final ssh = SshClientService();
      await expectLater(
        ssh.connect(
          host: IntegrationServer.host,
          port: IntegrationServer.port,
          username: 'fquser',
          privateKey: 'this is not a key',
          onHostKey: (_) async => true,
        ),
        throwsA(predicate((e) => '$e'.contains('Cannot parse the private key'))),
      );
      expect(ssh.state, SshConnectionState.disconnected);
    });

    test('nothing listening on the port fails fast and cleanly', () async {
      final ssh = SshClientService();
      await expectLater(
        ssh.connect(
          host: IntegrationServer.host,
          port: 1, // closed
          username: 'fquser',
          privateKey: h.server.keyPem,
          onHostKey: (_) async => true,
        ),
        throwsA(anything),
      );
      expect(ssh.state, SshConnectionState.disconnected);
    });

    test('connecting again replaces the old session', () async {
      final ssh = await h.server.connect(user: 'fquser');
      addTearDown(ssh.disconnect);
      await ssh.connect(
        host: IntegrationServer.host,
        port: IntegrationServer.port,
        username: 'fqplain',
        privateKey: h.server.keyPem,
        onHostKey: (_) async => true,
      );
      expect((await ssh.execute('id -un')).trim(), 'fqplain');
    });
  });

  group('trusting the server key', () {
    test('the first time the user is asked, and the answer decides', () async {
      HostKeyInfo? asked;
      final ssh = SshClientService();
      await expectLater(
        ssh.connect(
          host: IntegrationServer.host,
          port: IntegrationServer.port,
          username: 'fquser',
          privateKey: h.server.keyPem,
          onHostKey: (info) async {
            asked = info;
            return false; // not trusted
          },
        ),
        throwsA(isA<HostKeyRejectedError>()),
      );
      expect(asked, isNotNull);
      expect(asked!.changed, isFalse);
      expect(asked!.host, IntegrationServer.host);
      expect(asked!.fingerprint, startsWith('SHA256:'));
      expect(await KnownHostsService.lookup(IntegrationServer.host, IntegrationServer.port), isNull,
          reason: 'a refused key is not remembered');
    });

    test('an accepted key is remembered and not asked again', () async {
      var asked = 0;
      Future<bool> accept(HostKeyInfo _) async {
        asked++;
        return true;
      }

      final first = await h.server.connect(onHostKey: accept);
      first.disconnect();
      final second = await h.server.connect(onHostKey: accept);
      addTearDown(second.disconnect);

      expect(asked, 1);
      expect(await KnownHostsService.lookup(IntegrationServer.host, IntegrationServer.port),
          startsWith('ssh-'));
    });

    test('a changed key is reported as changed, with the old one', () async {
      (await h.server.connect()).disconnect(); // remember the current key
      final remembered =
          await KnownHostsService.lookup(IntegrationServer.host, IntegrationServer.port);

      await h.server.root(
        'rm -f /etc/ssh/ssh_host_*; ssh-keygen -A >/dev/null; systemctl restart ssh; sleep 2',
      );
      HostKeyInfo? asked;
      final ssh = SshClientService();
      await expectLater(
        ssh.connect(
          host: IntegrationServer.host,
          port: IntegrationServer.port,
          username: 'fquser',
          privateKey: h.server.keyPem,
          onHostKey: (info) async {
            asked = info;
            return false;
          },
        ),
        throwsA(isA<HostKeyRejectedError>()),
      );
      expect(asked!.changed, isTrue);
      expect(asked!.previous, remembered);
      expect('${asked!.type} ${asked!.fingerprint}', isNot(remembered));
    });

    test('without a way to ask, an unknown key is not trusted', () async {
      final ssh = SshClientService();
      await expectLater(
        ssh.connect(
          host: IntegrationServer.host,
          port: IntegrationServer.port,
          username: 'fquser',
          privateKey: h.server.keyPem,
        ),
        throwsA(isA<HostKeyRejectedError>()),
      );
    });
  });

  group('commands', () {
    late SshClientService ssh;

    setUp(() async {
      ssh = await h.server.connect(user: 'fquser');
    });
    tearDown(() => ssh.disconnect());

    test('output comes back as it is, including unicode', () async {
      expect(await ssh.execute(r"printf 'привет мир ✓\n'"), 'привет мир ✓\n');
    });

    test('a failing command throws with the exit code and what it printed on stderr', () async {
      try {
        await ssh.execute('echo oops >&2; exit 7');
        fail('should have thrown');
      } on SshCommandException catch (e) {
        expect(e.exitCode, 7);
        expect(e.stderr.trim(), 'oops');
        expect('$e', 'oops');
      }
    });

    test('a command killed by a signal is an error too', () async {
      await expectLater(
        ssh.execute(r'kill -KILL $$'),
        throwsA(isA<SshCommandException>()),
      );
    });

    test('a megabyte of output arrives complete', () async {
      final out = await ssh.execute('head -c 1048576 /dev/zero | tr "\\0" "x"');
      expect(out.length, 1048576);
    });

    test('streamed output arrives line by line with the exit code', () async {
      final lines = <String>[];
      final code = await ssh.runStreaming('echo one; echo two >&2; echo three; exit 3', lines.add);
      expect(code, 3);
      expect(lines.toSet(), {'one', 'two', 'three'});
      expect(lines.indexOf('one'), lessThan(lines.indexOf('three')));
    });

    test('a streamed multi-byte character is not torn apart', () async {
      final lines = <String>[];
      await ssh.runStreaming(r"printf '✓✓✓✓✓✓✓✓✓✓\n'", lines.add);
      expect(lines.single, '✓' * 10);
    });

    test('input is delivered to the command byte for byte', () async {
      final bytes = Uint8List.fromList(List.generate(70000, (i) => (i * 7) % 256));
      final r = await ssh.executeWithInput('cat > /tmp/fq_input && wc -c < /tmp/fq_input >&2', bytes);
      expect(r.ok, isTrue);
      expect(r.stderr.trim(), '70000');
      final back = await ssh.execute('base64 -w0 /tmp/fq_input');
      expect(base64Decode(back), bytes);
    });

    test('a command fed through stdin reports its failure', () async {
      final r = await ssh.executeWithInput('cat >/dev/null; echo bad >&2; exit 4', Uint8List(0));
      expect(r.ok, isFalse);
      expect(r.exitCode, 4);
      expect(r.stderr.trim(), 'bad');
    });

    test('a second login works while the key is there, and not once it is gone', () async {
      expect(await ssh.canLoginAgain(), isTrue);
      await h.server.root('> /home/fquser/.ssh/authorized_keys');
      expect(await ssh.canLoginAgain(), isFalse);
      expect(ssh.isConnected, isTrue, reason: 'the open session is not affected');
    });
  });

  group('disconnecting', () {
    test('a disconnected service refuses commands with a clear message', () async {
      final ssh = await h.server.connect();
      ssh.disconnect();

      expect(ssh.isConnected, isFalse);
      expect(ssh.state, SshConnectionState.disconnected);
      await expectLater(ssh.execute('true'), throwsA(predicate((e) => '$e'.contains('Not connected'))));
      await expectLater(
        ssh.runStreaming('true', (_) {}),
        throwsA(predicate((e) => '$e'.contains('Not connected'))),
      );
    });

    test('listeners hear about every state change', () async {
      final states = <SshConnectionState>[];
      final ssh = SshClientService();
      ssh.addListener(() => states.add(ssh.state));

      await ssh.connect(
        host: IntegrationServer.host,
        port: IntegrationServer.port,
        username: 'fquser',
        privateKey: h.server.keyPem,
        onHostKey: (_) async => true,
      );
      ssh.disconnect();

      expect(states.first, SshConnectionState.connecting);
      expect(states, contains(SshConnectionState.connected));
      expect(states.last, SshConnectionState.disconnected);
    });
  });
}
