import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/file_service.dart';

import 'fake_shell.dart';

void main() {
  test('copy keeps source and destination quoted and prevents overwrite', () async {
    final shell = FakeShell((_) => '');
    await FileService(shell).copy('/home/u/my file', '/home/u/my file.copy');
    final command = shell.commands.single;
    expect(command, contains("'/home/u/my file'"));
    expect(command, contains("'/home/u/my file.copy'"));
    expect(command, contains('cp -a -n -T --'));
    expect(command, contains('Destination already exists'));
  });

  test('invalid paths never send a copy command', () async {
    final shell = FakeShell((_) => '');
    await expectLater(FileService(shell).copy('/tmp/a', '/tmp/a'), throwsArgumentError);
    await expectLater(FileService(shell).copy('', '/tmp/b'), throwsArgumentError);
    await expectLater(FileService(shell).copy('/tmp/a', ''), throwsArgumentError);
    expect(shell.commands, isEmpty);
  });

  test('failed SSH command is surfaced to caller', () async {
    final shell = FakeShell((_) => throw StateError('Remote copy failed'));
    await expectLater(
      FileService(shell).copy('/tmp/a', '/tmp/b'),
      throwsStateError,
    );
  });
}
