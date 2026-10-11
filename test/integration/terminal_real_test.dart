import 'dart:async';
import 'dart:convert';

import 'package:dartssh2/dartssh2.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/ssh_service.dart';

import 'support.dart';

/// Terminals on a real server: a plain shell, and tmux sessions that outlive
/// the connection.
void main() {
  final h = IntegrationHarness();

  late SshClientService ssh; // fquser (sudo)
  late SshClientService plain; // fqplain (no sudo)

  setUp(() async {
    await h.server.install(['tmux']);
    ssh = await h.server.connect(user: 'fquser');
    plain = await h.server.connect(user: 'fqplain');
  });
  tearDown(() async {
    ssh.disconnect();
    plain.disconnect();
    await h.server.root('pkill -u fquser tmux; pkill -u fqplain tmux; rm -f /tmp/fq_ticks /tmp/fq-injected; exit 0');
  });

  /// Collects everything a terminal prints, so a test can wait for some text.
  _Screen watch(SSHSession s) => _Screen(s);

  group('a plain shell', () {
    test('runs what is typed and reports the exit code', () async {
      final session = await ssh.terminal.startShell();
      final screen = watch(session);
      session.stdin.add(utf8.encode('echo result=\$((20+22))\nexit 3\n'));
      await screen.waitFor('result=42');
      await session.done;
      expect(session.exitCode, 3, reason: 'exit status 3 is how the app tells "exited" from "connection lost"');
    });

    test('has the window size it was asked for, and can be resized', () async {
      final session = await ssh.terminal.startShell(ptyWidth: 100, ptyHeight: 30);
      final screen = watch(session);
      session.stdin.add(utf8.encode('stty size\n'));
      await screen.waitFor('30 100');

      session.resizeTerminal(132, 43);
      await Future.delayed(const Duration(milliseconds: 300));
      session.stdin.add(utf8.encode('stty size\n'));
      await screen.waitFor('43 132');
      session.close();
    });

    test('shows non-Latin text typed into it', () async {
      final session = await ssh.terminal.startShell();
      final screen = watch(session);
      session.stdin.add(utf8.encode('echo привет ✓\n'));
      await screen.waitFor('привет ✓');
      session.close();
    });

    test('without a connection there is no shell', () async {
      ssh.disconnect();
      await expectLater(ssh.terminal.startShell(), throwsA(anything));
    });
  });

  group('tmux availability', () {
    test('is detected, and is false when it is not installed', () async {
      expect(await ssh.terminal.hasTmux(), isTrue);
      await h.server.purge(['tmux']);
      expect(await ssh.terminal.hasTmux(), isFalse);
    });

    test('can be installed by a user who has sudo', () async {
      await h.server.purge(['tmux']);
      await ssh.terminal.installTmux();
      expect(await ssh.terminal.hasTmux(), isTrue);
    });

    test('cannot be installed without sudo, and the reason is given', () async {
      await h.server.purge(['tmux']);
      await expectLater(plain.terminal.installTmux(), throwsA(anything));
      expect(await plain.terminal.hasTmux(), isFalse);
    });
  });

  group('a tmux session', () {
    test('works like a terminal, in UTF-8, at the requested size', () async {
      final session = await ssh.terminal.attachTmux('fortiq-t1', ptyWidth: 120, ptyHeight: 40);
      final screen = watch(session);
      session.stdin.add(utf8.encode('echo ok-\$((6*7)) привет; stty size\n'));
      await screen.waitFor('ok-42 привет');
      await screen.waitFor('40 120');
      session.close();
    });

    test('its options belong to it alone', () async {
      await ssh.execute('tmux -u new-session -d -s other-session');
      final session = await ssh.terminal.attachTmux('fortiq-t2');
      watch(session); // keep the client attached
      await Future.delayed(const Duration(seconds: 1));

      expect((await ssh.execute('tmux show-options -t fortiq-t2 -v status')).trim(), 'off');
      expect((await ssh.execute('tmux show-options -t fortiq-t2 -v mouse')).trim(), 'on');
      // -A also shows inherited values: the other session still uses the default
      expect((await ssh.execute('tmux show-options -A -t other-session -v status')).trim(), 'on',
          reason: 'another session of the user keeps tmux defaults');
      expect(await ssh.execute('tmux show-options -g -v status'), 'on\n',
          reason: 'the global setting is untouched');
      session.close();
    });

    test('keeps running when the terminal is closed, and attaching again brings it back', () async {
      var session = await ssh.terminal.attachTmux('fortiq-t3');
      var screen = watch(session);
      session.stdin.add(utf8.encode('echo before-detach\n'));
      await screen.waitFor('before-detach');
      session.stdin.add(utf8.encode("sh -c 'while :; do echo x >> /tmp/fq_ticks; sleep 1; done' &\n"));
      await Future.delayed(const Duration(seconds: 2));
      session.close(); // the terminal window goes away

      final before = int.parse((await ssh.execute('wc -l < /tmp/fq_ticks')).trim());
      await Future.delayed(const Duration(seconds: 3));
      final after = int.parse((await ssh.execute('wc -l < /tmp/fq_ticks')).trim());
      expect(after, greaterThan(before), reason: 'the job kept running without any terminal');

      session = await ssh.terminal.attachTmux('fortiq-t3');
      screen = watch(session);
      await screen.waitFor('before-detach'); // the old screen is redrawn
      session.stdin.add(utf8.encode('echo back-again\n'));
      await screen.waitFor('back-again');
      session.close();
    });

    test('survives a lost connection: after the reconnect the same screen is there', () async {
      var session = await ssh.terminal.attachTmux('fortiq-t4');
      var screen = watch(session);
      session.stdin.add(utf8.encode('echo state-to-keep-42\n'));
      await screen.waitFor('state-to-keep-42');

      await h.server.dropConnections('fquser');
      await session.done;
      expect(session.exitCode, isNull, reason: 'a lost connection reports no exit status');
      for (var i = 0; i < 100 && !ssh.isConnected; i++) {
        await Future.delayed(const Duration(milliseconds: 200));
      }
      expect(ssh.isConnected, isTrue);

      session = await ssh.terminal.attachTmux('fortiq-t4');
      screen = watch(session);
      await screen.waitFor('state-to-keep-42');
      session.close();
    });

    test('ending the shell inside tmux ends the session with an exit status', () async {
      final session = await ssh.terminal.attachTmux('fortiq-t5');
      final screen = watch(session);
      session.stdin.add(utf8.encode('echo up\n'));
      await screen.waitFor('up');
      session.stdin.add(utf8.encode('exit\n'));
      await session.done;
      expect(session.exitCode, isNotNull, reason: 'a normal end carries a status');
      expect(await ssh.execute('tmux has-session -t fortiq-t5 2>&1 || echo gone'), contains('gone'));
    });

    test('closing a window kills its session and everything running in it', () async {
      final session = await ssh.terminal.attachTmux('fortiq-t6');
      final screen = watch(session);
      session.stdin.add(utf8.encode("sh -c 'while :; do echo y >> /tmp/fq_ticks; sleep 1; done' &\n"));
      await Future.delayed(const Duration(seconds: 2));
      session.close();

      await ssh.terminal.killTmuxSession('fortiq-t6');
      expect(await ssh.execute('tmux has-session -t fortiq-t6 2>&1 || echo gone'), contains('gone'));
      await Future.delayed(const Duration(seconds: 1));
      final a = int.parse((await ssh.execute('wc -l < /tmp/fq_ticks')).trim());
      await Future.delayed(const Duration(seconds: 2));
      final b = int.parse((await ssh.execute('wc -l < /tmp/fq_ticks')).trim());
      expect(b, a, reason: 'the job stopped with the session');
      screen.dispose();
    });

    test('killing a session that is not there is not an error', () async {
      await ssh.terminal.killTmuxSession('fortiq-never-existed');
    });

    test('two sessions are separate', () async {
      final a = await ssh.terminal.attachTmux('fortiq-a');
      final b = await ssh.terminal.attachTmux('fortiq-b');
      final sa = watch(a), sb = watch(b);
      a.stdin.add(utf8.encode('echo only-in-a\n'));
      b.stdin.add(utf8.encode('echo only-in-b\n'));
      await sa.waitFor('only-in-a');
      await sb.waitFor('only-in-b');
      expect(sa.text, isNot(contains('only-in-b')));
      expect(sb.text, isNot(contains('only-in-a')));
      a.close();
      b.close();
    });

    test('a hostile session name is just a name', () async {
      final session = await ssh.terminal.attachTmux(r"x'; touch /tmp/fq-injected; '");
      final screen = watch(session);
      session.stdin.add(utf8.encode('echo alive\n'));
      await screen.waitFor('alive');
      session.close();
      expect(await ssh.execute('ls /tmp/fq-injected 2>&1 || true'), contains('No such file'));
    });

    test('without tmux installed it fails instead of hanging', () async {
      await h.server.purge(['tmux']);
      final session = await ssh.terminal.attachTmux('fortiq-notmux');
      final screen = watch(session);
      await session.done.timeout(const Duration(seconds: 10));
      expect(session.exitCode, isNot(0));
      expect(screen.text, contains('not found'));
    });
  });
}

/// Everything a terminal session has printed so far.
class _Screen {
  final SSHSession session;
  final _buffer = StringBuffer();
  late final StreamSubscription<String> _out;
  late final StreamSubscription<String> _err;

  _Screen(this.session) {
    _out = session.stdout.cast<List<int>>().transform(const Utf8Decoder(allowMalformed: true)).listen(_buffer.write);
    _err = session.stderr.cast<List<int>>().transform(const Utf8Decoder(allowMalformed: true)).listen(_buffer.write);
  }

  String get text => _buffer.toString();

  Future<void> waitFor(String needle, {Duration timeout = const Duration(seconds: 15)}) async {
    final end = DateTime.now().add(timeout);
    while (!text.contains(needle)) {
      if (DateTime.now().isAfter(end)) {
        fail('"$needle" never appeared. Screen so far:\n$text');
      }
      await Future.delayed(const Duration(milliseconds: 50));
    }
  }

  void dispose() {
    _out.cancel();
    _err.cancel();
  }
}
