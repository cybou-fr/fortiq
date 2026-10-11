import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/server_setup_service.dart';
import 'fake_shell.dart';

const _freshProbe = '''
OS=Debian GNU/Linux 12 (bookworm)
OS_ID=debian
ROOT=0
SUDO=1
REBOOT=0
PKG_OK=procps
PKG_OK=iproute2
PKG_MISSING=tmux
CLIENT_IP=203.0.113.7
SSH_PORT=22
LISTS_AGE=999999
UNATTENDED=0
FAIL2BAN=0
SSH_INCLUDE=1
TCPLISTEN LISTEN 0 128 0.0.0.0:22 0.0.0.0:*
TCPLISTEN LISTEN 0 511 *:80 *:*
TCPLISTEN LISTEN 0 511 [::]:443 [::]:*
TCPLISTEN LISTEN 0 128 127.0.0.1:5432 0.0.0.0:*
UDPLISTEN UNCONN 0 0 0.0.0.0:51820 0.0.0.0:*
UDPLISTEN UNCONN 0 0 127.0.0.53%lo:53 0.0.0.0:*
''';

const _freshRootProbe = '''
SSHD passwordauthentication yes
SSHD permitrootlogin yes
UFW=inactive
''';

const _doneProbe = '''
OS=Ubuntu 24.04 LTS
OS_ID=ubuntu
ROOT=1
SUDO=0
REBOOT=0
PKG_OK=tmux
PKG_OK=procps
PKG_OK=iproute2
CLIENT_IP=203.0.113.7
SSH_PORT=22
LISTS_AGE=100
UNATTENDED=1
FAIL2BAN=1
SSH_INCLUDE=1
TCPLISTEN LISTEN 0 128 0.0.0.0:22 0.0.0.0:*
''';

const _doneRootProbe = '''
SSHD passwordauthentication no
SSHD permitrootlogin prohibit-password
UFW=active
''';

const _updates =
    'Listing...\nopenssl/stable-security 3.0.15 amd64 [upgradable from: 3.0.14]';

/// A server answering the read-only checks with the given probe outputs.
String Function(String) server({
  String probe = _freshProbe,
  String rootProbe = _freshRootProbe,
  String updates = _updates,
}) =>
    (cmd) {
      if (cmd.contains('PKG_MISSING')) return probe;
      if (cmd.contains('/usr/sbin/sshd -T')) return rootProbe;
      if (cmd.contains('apt list --upgradable')) return updates;
      return '';
    };

ServerSetupService setupFor(FakeShell shell) =>
    ServerSetupService(shell, host: 'vps.example', port: 22);

StepStatus state(ServerSetupService s, SetupStepId id) =>
    s.steps.firstWhere((x) => x.id == id).state;

/// Did anything that changes the server run? (The read-only checks mention ufw
/// and sshd_config.d too, so only the writing commands count.)
bool changedServer(FakeShell shell) => shell.commands.any(
      (c) =>
          c.contains('apt-get') ||
          c.contains('ufw allow') ||
          c.contains('ufw default') ||
          c.contains('ufw --force') ||
          c.contains('00-fortiq-hardening') ||
          c.contains('systemctl enable'),
    );

void main() {
  group('the check is read-only', () {
    test('a fresh server: everything is found, nothing is changed', () async {
      final shell = FakeShell(server());
      final setup = setupFor(shell);

      await setup.inspect();

      expect(changedServer(shell), isFalse, reason: shell.commands.join('\n'));
      expect(setup.steps.every((s) => s.willRun), isTrue);
      expect(setup.hasWork, isTrue);
      final st = setup.status!;
      expect(st.missing.map((r) => r.package), ['tmux']);
      expect(st.upgradable, ['openssl']);
      expect(st.securityUpdates, 1);
      expect(st.publicTcpPorts, [22, 80, 443], reason: 'loopback-only 5432 is not public');
      expect(st.publicUdpPorts, [51820], reason: 'the loopback resolver is not public');
      expect(st.clientIp, '203.0.113.7');
      expect(st.sshPort, 22);
      expect(st.sshPasswordAuth, isTrue);
      expect(st.ufwActive, isFalse);
    });

    test('an already set up server has nothing to do', () async {
      final shell = FakeShell(
        server(probe: _doneProbe, rootProbe: _doneRootProbe, updates: 'Listing...\n'),
      );
      final setup = setupFor(shell);

      await setup.inspect();

      expect(setup.hasWork, isFalse);
      expect(changedServer(shell), isFalse);
    });

    test('without root or sudo nothing can run', () async {
      final shell = FakeShell(server(probe: _freshProbe.replaceFirst('SUDO=1', 'SUDO=0')));
      final setup = setupFor(shell);

      await setup.inspect();

      expect(setup.hasWork, isFalse);
      expect(setup.steps.every((s) => s.unavailable != null), isTrue);
      expect(changedServer(shell), isFalse);
    });

    test('an unsupported system is left alone', () async {
      final shell = FakeShell(server(probe: _freshProbe.replaceFirst('OS_ID=debian', 'OS_ID=centos')));
      final setup = setupFor(shell);

      await setup.inspect();

      expect(setup.hasWork, isFalse);
      expect(setup.steps.first.unavailable, contains('Debian and Ubuntu'));
    });

    test('SSH hardening needs a key login, so it cannot lock a password user out', () async {
      final shell = FakeShell(server(), usedKeyAuth: false);
      final setup = setupFor(shell);

      await setup.inspect();

      final ssh = setup.steps.firstWhere((s) => s.id == SetupStepId.sshHardening);
      expect(ssh.willRun, isFalse);
      expect(ssh.unavailable, contains('SSH key'));
      expect(setup.steps.where((s) => s.willRun), hasLength(5));
    });
  });

  group('the setup', () {
    test('does everything on a fresh server, in order, and tells where it is', () async {
      final shell = FakeShell(server());
      final setup = setupFor(shell);
      await setup.inspect();

      await setup.run();

      for (final id in SetupStepId.values) {
        expect(state(setup, id), StepStatus.done, reason: '$id\n${setup.log.join('\n')}');
      }
      // packages and updates
      expect(shell.ran('apt-get install'), isTrue);
      expect(shell.commands.where((c) => c.contains('apt-get update')), hasLength(1),
          reason: 'the package lists are refreshed once per run');
      expect(shell.ran('apt-get upgrade'), isTrue);
      expect(shell.ran(unattendedConfigPath), isTrue);
      // fail2ban never bans the address Fortiq connects from
      final jail = shell.commands.firstWhere((c) => c.contains(fail2banJailPath));
      expect(jail, contains('127.0.0.1/8 ::1 203.0.113.7'));
      // SSH is hardened, then proven, and only then the firewall comes
      expect(shell.ran('PasswordAuthentication no'), isTrue);
      expect(shell.ran('sshd -t'), isTrue);
      final ufwEnable = shell.indexOf('ufw --force enable');
      expect(ufwEnable, greaterThan(shell.indexOf('sshd -t')));
      // the firewall keeps SSH and what listens now open
      final fw = shell.commands.firstWhere((c) => c.contains('ufw --force enable'));
      for (final p in [22, 80, 443]) {
        expect(fw, contains('ufw allow $p/tcp'));
      }
      expect(fw, contains('ufw allow 51820/udp'),
          reason: 'a running UDP service (WireGuard-like) stays reachable');
      expect(fw, isNot(contains('5432')));
      expect(shell.commands.where((c) => c == FakeShell.secondLogin), hasLength(2));
      // everything runs as root, through one shell
      expect(shell.commands.where((c) => c.contains('apt-get install')).every((c) => c.contains('sudo -n sh -c')), isTrue);
      expect(setup.phase, SetupPhase.finished);
    });

    test('an SSH change that breaks the login is undone and nothing else runs', () async {
      final shell = FakeShell(server(), loginResults: [false]);
      final setup = setupFor(shell);
      await setup.inspect();

      await setup.run();

      expect(state(setup, SetupStepId.sshHardening), StepStatus.failed);
      final login = shell.indexOf(FakeShell.secondLogin);
      final rollback = shell.commands.indexWhere((c) =>
          c.contains('00-fortiq-hardening') && c.contains('systemctl reload'), login);
      expect(rollback, greaterThan(login),
          reason: 'after the failed login the drop-in is removed and sshd reloaded');
      expect(state(setup, SetupStepId.firewall), StepStatus.skipped);
      expect(shell.ran('ufw allow'), isFalse);
      expect(shell.ran('ufw --force enable'), isFalse);
    });

    test('a firewall that breaks the login is switched off again', () async {
      final shell = FakeShell(server(), loginResults: [true, false]);
      final setup = setupFor(shell);
      await setup.inspect();

      await setup.run();

      expect(state(setup, SetupStepId.sshHardening), StepStatus.done);
      expect(state(setup, SetupStepId.firewall), StepStatus.failed);
      expect(shell.indexOf('ufw --force disable'), greaterThan(shell.indexOf('ufw --force enable')));
    });

    test('a failing step stops the rest instead of piling up errors', () async {
      final setup = setupFor(_ExitShell(server(), 'apt-get upgrade'));
      await setup.inspect();

      await setup.run();

      expect(state(setup, SetupStepId.deps), StepStatus.done);
      expect(state(setup, SetupStepId.upgrade), StepStatus.failed);
      for (final id in [
        SetupStepId.unattended,
        SetupStepId.fail2ban,
        SetupStepId.sshHardening,
        SetupStepId.firewall,
      ]) {
        expect(state(setup, id), StepStatus.skipped);
      }
    });

    test('a connection lost mid-step is unknown, not retried', () async {
      late FakeShell shell;
      shell = FakeShell((cmd) {
        if (cmd.contains('apt-get install') && cmd.contains('tmux')) {
          shell.isConnected = false;
          throw Exception('connection closed');
        }
        return server()(cmd);
      });
      final setup = setupFor(shell);
      await setup.inspect();

      await setup.run();

      expect(state(setup, SetupStepId.deps), StepStatus.unknown);
      expect(shell.commands.where((c) => c.contains('apt-get install')), hasLength(1));
      expect(state(setup, SetupStepId.upgrade), StepStatus.skipped);
    });

    test('"stop" lets the running step finish and skips the rest', () async {
      final shell = FakeShell(server());
      final setup = setupFor(shell);
      await setup.inspect();
      var asked = false;
      setup.addListener(() {
        if (!asked && state(setup, SetupStepId.deps) == StepStatus.running) {
          asked = true;
          setup.requestStop();
        }
      });

      await setup.run();

      expect(state(setup, SetupStepId.deps), StepStatus.done);
      expect(state(setup, SetupStepId.upgrade), StepStatus.skipped);
    });

    test('closing the window during a run does not touch the disposed service', () async {
      final setup = setupFor(FakeShell(server()));
      await setup.inspect();
      final running = setup.run();
      setup.dispose();
      await running; // would throw "used after being disposed" without the guard
    });
  });

  group('parsers', () {
    test('public TCP ports come from ss and skip loopback', () {
      const ss = '''
LISTEN 0 4096 0.0.0.0:22 0.0.0.0:*
LISTEN 0 4096 [::]:22 [::]:*
LISTEN 0 511 *:443 *:*
LISTEN 0 128 127.0.0.1:631 0.0.0.0:*
LISTEN 0 128 [::1]:631 [::]:*
LISTEN 0 4096 127.0.0.53%lo:53 0.0.0.0:*
LISTEN 0 4096 10.0.0.5:8080 0.0.0.0:*
''';
      expect(parsePublicTcpPorts(ss), [22, 443, 8080]);
      expect(
        parsePublicUdpPorts('UNCONN 0 0 0.0.0.0:51820 0.0.0.0:*\nUNCONN 0 0 127.0.0.1:323 0.0.0.0:*'),
        [51820],
      );
    });
  });

  group('the scripts', () {
    test('a hostile client address never reaches the fail2ban config', () {
      final script = fail2banScript('1.2.3.4; rm -rf /');
      expect(script, isNot(contains('rm -rf')));
      expect(script, contains('ignoreip = 127.0.0.1/8 ::1'));
    });

    test('the SSH change is validated by sshd before it is reloaded', () {
      final s = sshHardeningScript();
      expect(s.indexOf('sshd -t'), lessThan(s.indexOf('systemctl reload')));
      expect(s, contains('rm -f'), reason: 'a rejected config removes the file again');
    });

    test('every script is valid shell', () async {
      final sh = await _findSh();
      if (sh == null) {
        markTestSkipped('no sh on this machine');
        return;
      }
      final scripts = {
        'probe': probeScript,
        'root probe': rootProbeScript,
        'unattended': unattendedScript(),
        'fail2ban': fail2banScript('203.0.113.7'),
        'ssh': sshHardeningScript(),
        'ssh rollback': sshRollbackScript(),
        'firewall': firewallScript([22, 80, 443], [51820]),
        'firewall rollback': firewallRollbackScript,
      };
      for (final e in scripts.entries) {
        final r = await Process.run(sh, ['-n', '-c', e.value]);
        expect(r.exitCode, 0, reason: '${e.key}: ${r.stderr}\n${e.value}');
      }
    });
  });
}

Future<String?> _findSh() async {
  try {
    final r = await Process.run('sh', ['-c', 'exit 0']);
    return r.exitCode == 0 ? 'sh' : null;
  } catch (_) {
    return null;
  }
}

class _ExitShell extends FakeShell {
  final String marker;

  _ExitShell(super.answer, this.marker);

  @override
  Future<int> runStreaming(String command, void Function(String line) onLine) async {
    await super.runStreaming(command, onLine);
    return command.contains(marker) ? 100 : 0;
  }
}
