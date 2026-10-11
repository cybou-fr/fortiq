import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/mail_service.dart';
import 'package:fortiq/services/shell.dart';

import 'fake_shell.dart';

void main() {
  test('system mail uses Linux command and passes body only via SSH stdin', () async {
    final ssh = FakeShell((_) => '')
      ..inputResult = (command, input) => const InputResult(exitCode: 0);
    final accepted = await MailService(ssh).sendSystemMail(
      to: 'root',
      subject: 'Backup status',
      body: 'Important text with quotes and Unicode ✓',
    );
    expect(accepted, isTrue);
    expect(ssh.lastInputText, contains('Unicode ✓'));
    expect(ssh.commands.single, contains('sendmail -i -t'));
    expect(ssh.commands.single, contains('mail -s'));
    expect(ssh.commands.single, isNot(contains('sample_mbox')));
    expect(ssh.commands.single, isNot(contains('Important text')));
  });

  test('missing Linux MTA never silently reports delivery', () async {
    final ssh = FakeShell((_) => '')
      ..inputResult = (_, _) => const InputResult(exitCode: 127);
    expect(await MailService(ssh).sendSystemMail(
      to: 'root', subject: 'Test', body: 'hello',
    ), isFalse);
    expect(ssh.commands.single, contains('exit 127'));
  });

  test('SSH lost before confirmed mail submission is unknown, not success', () async {
    final ssh = FakeShell((_) => '')
      ..inputResult = (_, _) => const InputResult(exitCode: null);
    await expectLater(
      MailService(ssh).sendSystemMail(
        to: 'root', subject: 'Test', body: 'hello',
      ),
      throwsStateError,
    );
  });

  test('rejects newline header injection and recipients starting with dash', () async {
    final ssh = FakeShell((_) => '');
    for (final recipient in ['-Cdebug', 'root\\nCc:evil', 'root;touch /tmp/x']) {
      expect(await MailService(ssh).sendSystemMail(
        to: recipient, subject: 'safe', body: 'hello',
      ), isFalse);
    }
    expect(await MailService(ssh).sendSystemMail(
      to: 'root', subject: 'Test\\nBcc: other', body: 'hello',
    ), isFalse);
    expect(ssh.commands, isEmpty);
  });
}
