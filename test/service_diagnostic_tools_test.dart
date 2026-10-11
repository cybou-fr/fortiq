import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/service_diagnostic_tools.dart';

import 'fake_shell.dart';

void main() {
  test('capability discovery only executes a read-only CLI check', () async {
    final shell = FakeShell((_) => 'systemctl:yes\njournalctl:yes\nss:no\n');
    final capabilities = await ServiceDiagnosticTools(shell).capabilities();
    expect(capabilities.map((c) => c.available).toList(), [true, true, false]);
    expect(shell.commands.single, contains('command -v'));
  });

  test('investigation combines three bounded read-only tools', () async {
    final shell = FakeShell((command) => 'sample evidence');
    final report = await ServiceDiagnosticTools(shell).investigate('nginx.service');
    expect(report.sections.length, 3);
    expect(report.toPlainText(), contains('=== listening TCP ports ==='));
    expect(shell.commands.length, 3);
    for (final command in shell.commands) {
      expect(command, contains('head -c'));
      expect(command, isNot(contains('restart')));
      expect(command, isNot(contains('sudo')));
    }
    expect(shell.commands.first, contains("'nginx.service'"));
  });

  test('invalid unit is rejected before issuing remote commands', () async {
    final shell = FakeShell((_) => '');
    await expectLater(
      ServiceDiagnosticTools(shell).investigate('nginx; rm -rf /'),
      throwsArgumentError,
    );
    expect(shell.commands, isEmpty);
  });

  test('investigation rejects disconnected SSH', () async {
    final shell = FakeShell((_) => '')..isConnected = false;
    await expectLater(
      ServiceDiagnosticTools(shell).investigate('nginx.service'),
      throwsStateError,
    );
    expect(shell.commands, isEmpty);
  });

  test('report truncates large evidence before passing it to AI', () {
    final report = DiagnosticReport('nginx.service', [
      DiagnosticSection('journalctl', 'x' * 20000),
    ]);
    final text = report.toPlainText();
    expect(text, contains('[report truncated]'));
    expect(text.length, lessThan(8192));
  });
}
