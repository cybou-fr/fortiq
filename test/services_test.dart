import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/file_service.dart';
import 'package:fortiq/services/network_service.dart';
import 'package:fortiq/services/shell.dart';
import 'fake_shell.dart';
import 'package:fortiq/services/system_service.dart';

void main() {
  group('FileService', () {
    test('lists a directory: preserves names, sorts folders first, quotes the path', () async {
      final shell = FakeShell((_) => [
            'f\tf\t-rw-r--r--\t1\tdebian\tdebian\t120\t1728599420\x00notes  draft\n.txt\x00\x00',
            'd\td\tdrwxr-xr-x\t2\tdebian\tdebian\t4096\t1728599421\x00projects\x00\x00',
            'f\tf\t-rw-r--r--\t1\tdebian\tdebian\t5\t1728599423\x00a.md\x00\x00',
          ].join());

      final files = await FileService(shell).listDirectory('/home/my dir');

      expect(shell.commands.single, startsWith("find -H '/home/my dir' -mindepth 1 -maxdepth 1 "));
      expect(files.map((f) => f.name), ['projects', 'a.md', 'notes  draft\n.txt']);
      expect(files.first.isDirectory, isTrue);
      expect(files.last.size, 120);
    });

    test('file save refuses an unknown SSH completion status', () async {
      final shell = FakeShell((_) => '')
        ..inputResult = (_, _) => const InputResult(exitCode: null);
      await expectLater(
        FileService(shell).writeFile('/tmp/config', 'changed'),
        throwsA(isA<SshCommandException>()),
      );
      expect(shell.inputs.single.input, isNotEmpty);
    });

    test('quotes paths with shell metacharacters on every operation', () async {
      final shell = FakeShell((_) => '');
      final files = FileService(shell);
      const evil = r"/tmp/$(id) `x` 'q'";

      await files.createDirectory(evil);
      await files.remove(evil, recursive: true);
      await files.readFile(evil);

      for (final cmd in shell.commands) {
        expect(cmd, contains(shQuote(evil)));
        expect(cmd, isNot(contains('"')));
      }
    });
  });

  group('SystemService', () {
    test('parses the process table', () async {
      final shell = FakeShell((_) => [
            '    PID USER     %CPU %MEM COMMAND',
            '      1 root      0.1  0.3 systemd',
            '    842 debian   12.5  1.2 node server',
          ].join('\n'));

      final list = await SystemService(shell).fetchProcesses();

      expect(list.length, 2);
      expect(list[1]['pid'], 842);
      expect(list[1]['cpu'], 12.5);
      expect(list[1]['comm'], 'node server');
    });

    test('service actions run through one privileged shell and are validated', () async {
      final shell = FakeShell((_) => '');
      final system = SystemService(shell);

      await system.controlService('nginx.service', 'restart');
      expect(shell.commands.single, contains("sudo -n sh -c 'systemctl restart"));

      expect(() => system.controlService('nginx.service', 'restart; reboot'), throwsArgumentError);
    });

    test('kill falls back to sudo for processes of other users', () async {
      final shell = FakeShell((_) => '');
      await SystemService(shell).killProcess(4242, force: false);
      expect(shell.commands.single, 'kill -15 4242 2>/dev/null || sudo -n kill -15 4242');
    });

    test('metrics errors are not turned into fake zero values', () async {
      final shell = FakeShell((_) => throw SshCommandException('x', 1, 'boom'));
      expect(SystemService(shell).fetchMetrics(), throwsA(isA<SshCommandException>()));
    });
  });

  group('NetworkService.fetchUfwStatus', () {
    test('reads active status and numbered rules', () async {
      final shell = FakeShell((_) => [
            'Status: active',
            '',
            '     To                         Action      From',
            '     --                         ------      ----',
            '[ 1] 22/tcp                     ALLOW IN    Anywhere',
            '[ 2] 80/tcp                     DENY IN     10.0.0.0/8',
          ].join('\n'));

      final st = await NetworkService(shell).fetchUfwStatus();

      expect(st['installed'], isTrue);
      expect(st['active'], isTrue);
      final rules = st['rules'] as List;
      expect(rules.length, 2);
      expect(rules[0], {'num': 1, 'to': '22/tcp', 'action': 'ALLOW', 'from': 'Anywhere'});
      expect(rules[1]['action'], 'DENY');
      expect(rules[1]['from'], '10.0.0.0/8');
    });

    test('distinguishes "not installed" from "needs sudo"', () async {
      final missing = await NetworkService(FakeShell((_) => 'NOT_INSTALLED')).fetchUfwStatus();
      expect(missing['installed'], isFalse);

      final noSudo = await NetworkService(FakeShell((_) => 'sudo: a password is required\nNEEDS_SUDO')).fetchUfwStatus();
      expect(noSudo['installed'], isTrue);
      expect(noSudo['error'], isNotNull);
    });
  });
}
