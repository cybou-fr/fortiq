import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/file_service.dart';

import 'fake_shell.dart';

void main() {
  test('rename uses quoted Linux paths and prevents overwrite', () async {
    final shell = FakeShell((_) => '');
    await FileService(shell).rename('/home/u/old file.txt', '/home/u/new file.txt');
    final command = shell.commands.single;
    expect(command, contains("'/home/u/old file.txt'"));
    expect(command, contains("'/home/u/new file.txt'"));
    expect(command, contains('mv -nT --'));
    expect(command, contains('Destination already exists'));
  });

  test('invalid rename paths never reach the Linux server', () async {
    final shell = FakeShell((_) => '');
    await expectLater(FileService(shell).rename('/a', '/a'), throwsArgumentError);
    await expectLater(FileService(shell).rename('/a', ''), throwsArgumentError);
    expect(shell.commands, isEmpty);
  });
}
