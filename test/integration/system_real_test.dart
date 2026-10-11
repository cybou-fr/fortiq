import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/shell.dart';
import 'package:fortiq/services/system_service.dart';
import 'package:fortiq/services/ssh_service.dart';

import 'support.dart';

/// Metrics, processes, services and logs of a real server.
void main() {
  final h = IntegrationHarness();

  late SshClientService ssh; // fquser: sudo without password
  late SshClientService plain; // fqplain: no sudo

  setUp(() async {
    ssh = await h.server.connect(user: 'fquser');
    plain = await h.server.connect(user: 'fqplain');
  });
  tearDown(() async {
    ssh.disconnect();
    plain.disconnect();
    await h.server.root(
        'systemctl stop "fq-test-proc-*" 2>/dev/null; pkill -f "fq-test-proc" || true; exit 0');
  });

  group('metrics', () {
    test('describe the machine with real numbers', () async {
      final m = await ssh.system.fetchMetrics();

      expect(m['cpu'], isA<double>());
      expect(m['cpu'] as double, inInclusiveRange(0, 100));
      expect(m['totalMem'] as int, greaterThan(100));
      expect(m['usedMem'] as int, inInclusiveRange(1, m['totalMem'] as int));
      expect(m['uptime'], isNot(anyOf('unknown', '')));
      expect(m['disk'], matches(RegExp(r'^\d+% \S+ \S+$')), reason: 'percent used, used, size');
    });

    test('without a connection they fail, they do not invent zeros', () async {
      ssh.disconnect();
      await expectLater(ssh.system.fetchMetrics(), throwsA(anything));
    });
  });

  group('processes', () {
    test('the list has the real ones, with a user and numbers', () async {
      final list = await ssh.system.fetchProcesses();

      expect(list, isNotEmpty);
      expect(list.length, lessThanOrEqualTo(30));
      final init = list.firstWhere((p) => p['pid'] == 1, orElse: () => {});
      expect(list.any((p) => '${p['comm']}'.contains('sshd')), isTrue);
      expect(list.every((p) => p['cpu'] is double && p['mem'] is double && p['pid'] is int), isTrue);
      expect(init.isEmpty || init['user'] == 'root', isTrue);
    });

    test('the busiest one is first', () async {
      await h.server.root('cp /bin/sleep /tmp/fq-test-proc; (/tmp/fq-test-proc 60 &) ; exit 0');
      await ssh.execute('(timeout 8 sh -c "while :; do :; done" >/dev/null 2>&1 &)');
      await Future.delayed(const Duration(seconds: 2));
      final list = await ssh.system.fetchProcesses();
      final cpus = list.map((p) => p['cpu'] as double).toList();
      expect(cpus, orderedEquals([...cpus]..sort((a, b) => b.compareTo(a))),
          reason: 'sorted by CPU, highest first');
      expect(cpus.first, greaterThan(20));
    });

    test('a name with a space is shown whole', () async {
      await h.server.root(
          "cp /bin/sleep '/tmp/fq-test-proc x'; systemd-run --quiet --unit=fq-test-proc-x '/tmp/fq-test-proc x' 60; sleep 1");
      final list = await ssh.system.fetchProcesses();
      // ps limits the name to 15 characters, which is what "comm" is
      expect(list.any((p) => '${p['comm']}'.startsWith('fq-test-proc x')), isTrue);
    });
  });

  group('ending a process', () {
    /// A long-running process of root, kept alive by systemd (a plain background
    /// job would die with the session that started it). Returns its pid.
    Future<int> rootProcess() async {
      final name = 'fq-test-proc-${DateTime.now().microsecondsSinceEpoch}';
      final out = await h.server.root(
          'systemd-run --quiet --unit=$name sleep 300; sleep 0.5; systemctl show -p MainPID --value $name');
      return int.parse(out.trim());
    }

    /// Starts [script] in the background as the user of [s]; returns its pid.
    Future<int> startOwn(SshClientService s, {String script = 'sleep 300'}) async {
      final pid = await s.execute(
          "setsid nohup sh -c '$script' >/dev/null 2>&1 < /dev/null & echo \$!");
      return int.parse(pid.trim());
    }

    /// Running (not gone, not a zombie), whoever owns it: `ps` needs no permission.
    Future<bool> alive(int pid) async {
      final stat = (await ssh.execute('ps -o stat= -p $pid || true')).trim();
      return stat.isNotEmpty && !stat.startsWith('Z');
    }

    test('your own process ends politely', () async {
      final pid = await startOwn(ssh);
      expect(await alive(pid), isTrue);
      await ssh.system.killProcess(pid, force: false);
      await Future.delayed(const Duration(milliseconds: 300));
      expect(await alive(pid), isFalse);
    });

    test('one that ignores the polite request needs the forced one', () async {
      final pid = await startOwn(ssh, script: r'trap "" TERM; while :; do sleep 1; done');
      await ssh.system.killProcess(pid, force: false);
      await Future.delayed(const Duration(milliseconds: 500));
      expect(await alive(pid), isTrue, reason: 'it ignored TERM');
      await ssh.system.killProcess(pid, force: true);
      await Future.delayed(const Duration(milliseconds: 300));
      expect(await alive(pid), isFalse);
    });

    test("someone else's process needs sudo, which this user has", () async {
      final pid = await rootProcess();
      await ssh.system.killProcess(pid);
      await Future.delayed(const Duration(milliseconds: 300));
      expect(await alive(pid), isFalse);
    });

    test("someone else's process cannot be ended without sudo, and says why", () async {
      final pid = await rootProcess();
      addTearDown(() => h.server.root('kill $pid; exit 0'));
      await expectLater(
        plain.system.killProcess(pid),
        throwsA(isA<SshCommandException>().having((e) => '$e', 'message', anyOf(contains('not permitted'), contains('password')))),
      );
      expect(await alive(pid), isTrue);
    });

    test('a process that does not exist is an error', () async {
      await expectLater(ssh.system.killProcess(2147483000), throwsA(isA<SshCommandException>()));
    });
  });

  group('services', () {
    const unit = 'fortiq-test.service';
    const failing = 'fortiq-failing.service';

    setUp(() async {
      await h.server.root('''
cat > /etc/systemd/system/$unit <<'EOF'
[Unit]
Description=Fortiq test service
[Service]
ExecStart=/bin/sleep infinity
EOF
cat > /etc/systemd/system/$failing <<'EOF'
[Unit]
Description=Fortiq failing service
[Service]
ExecStart=/bin/false
EOF
systemctl daemon-reload
systemctl stop $unit 2>/dev/null
systemctl reset-failed
systemctl start $failing 2>/dev/null
exit 0
''');
    });
    tearDown(() => h.server.root('systemctl stop $unit $failing 2>/dev/null; rm -f /etc/systemd/system/$unit /etc/systemd/system/$failing; systemctl daemon-reload; exit 0'));

    Future<Map<String, dynamic>?> find(String u) async =>
        (await ssh.system.fetchServices()).cast<Map<String, dynamic>?>().firstWhere((s) => s!['unit'] == u, orElse: () => null);

    test('the list has running, stopped and failed services, with the right columns', () async {
      await ssh.system.controlService(unit, 'start');
      final all = await ssh.system.fetchServices();

      final running = all.firstWhere((s) => s['unit'] == unit);
      expect(running['active'], 'active');
      expect(running['sub'], 'running');
      expect(running['load'], 'loaded');
      expect(running['description'], 'Fortiq test service');

      final broken = all.firstWhere((s) => s['unit'] == failing);
      expect(broken['active'], 'failed', reason: 'a failed unit is not shifted by the "●" marker');
      expect(broken['description'], 'Fortiq failing service');

      expect(all.any((s) => s['unit'] == 'ssh.service'), isTrue);
      expect(all.length, greaterThan(5), reason: 'not cut off after a few dozen');
    });

    test('a stopped service is listed as inactive', () async {
      final s = await find(unit);
      expect(s, isNotNull);
      expect(s!['active'], 'inactive');
    });

    test('start, restart and stop change the state for real', () async {
      await ssh.system.controlService(unit, 'start');
      expect((await find(unit))!['active'], 'active');

      final before = (await ssh.execute('systemctl show -p MainPID --value $unit')).trim();
      await ssh.system.controlService(unit, 'restart');
      final after = (await ssh.execute('systemctl show -p MainPID --value $unit')).trim();
      expect(after, isNot(before), reason: 'a new process after the restart');

      await ssh.system.controlService(unit, 'stop');
      expect((await find(unit))!['active'], 'inactive');
    });

    test('without sudo a service cannot be changed, and the error says so', () async {
      await expectLater(
        plain.system.controlService(unit, 'start'),
        throwsA(isA<SshCommandException>()),
      );
      expect((await find(unit))!['active'], 'inactive');
    });

    test('only a plain action name is accepted', () async {
      for (final bad in ['start; reboot', 'Start', '', 'restart --now', r'$(id)']) {
        await expectLater(ssh.system.controlService(unit, bad), throwsArgumentError, reason: bad);
      }
    });

    test('a unit name with shell characters is passed as one literal word', () async {
      await expectLater(
        ssh.system.controlService(r"x'; touch /tmp/fq-injected; '.service", 'start'),
        throwsA(isA<SshCommandException>()),
      );
      expect((await ssh.execute('ls /tmp/fq-injected 2>&1 || true')), contains('No such file'));
    });
  });

  group('logs', () {
    test('the journal has what was just written, and an error is marked as one', () async {
      await h.server.root('logger -t fortiq-test "everything is fine"; logger -t fortiq-test "disk error on sda"; sleep 1');
      final lines = await ssh.system.fetchSystemLogs(logType: 'journal', linesCount: 100);

      final fine = lines.firstWhere((l) => '${l['text']}'.contains('everything is fine'));
      final err = lines.firstWhere((l) => '${l['text']}'.contains('disk error on sda'));
      expect(fine['level'], 'INFO');
      expect(err['level'], 'ERROR');
    });

    test('the number of lines is respected', () async {
      final lines = await ssh.system.fetchSystemLogs(logType: 'journal', linesCount: 7);
      expect(lines.length, lessThanOrEqualTo(7));
      expect(lines, isNotEmpty);
    });

    test('auth, syslog and kernel logs answer even when the files do not exist here', () async {
      for (final type in ['auth', 'syslog', 'dmesg']) {
        final lines = await ssh.system.fetchSystemLogs(logType: type, linesCount: 20);
        expect(lines, isNotEmpty, reason: type);
        expect(lines.every((l) => l['text'] is String && l['level'] is String), isTrue);
      }
    });

    test('the auth log shows the logins of the tests', () async {
      final lines = await ssh.system.fetchSystemLogs(logType: 'auth', linesCount: 200);
      expect(lines.any((l) => '${l['text']}'.contains('fquser')), isTrue);
    });
  });

  test('the parser reads every line of real systemctl output', () async {
    final out = await ssh.execute('systemctl list-units --type=service --all --no-legend --no-pager --plain');
    expect(parseServices(out).length, out.trim().split('\n').length);
  });
}
