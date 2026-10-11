import 'dart:convert';
import 'dart:typed_data';

import 'package:dartssh2/dartssh2.dart';
import 'package:fortiq/services/shell.dart';

/// A server that answers commands from a function and records what was run.
///
/// "Second login" checks are recorded in [commands] as [secondLogin], so a test
/// can see when they happened relative to the changes.
class FakeShell implements RemoteShell {
  static const secondLogin = '<second login>';

  final String Function(String command) answer;
  final List<String> commands = [];

  /// Results of successive "can I log in again?" checks; the last one repeats.
  List<bool> loginResults;

  FakeShell(this.answer, {this.usedKeyAuth = true, this.loginResults = const [true]});

  @override
  bool isConnected = true;

  @override
  bool usedKeyAuth;

  int _logins = 0;

  /// What [executeWithInput] received, and what it answers with.
  final List<({String command, Uint8List input})> inputs = [];
  InputResult Function(String command, Uint8List input)? inputResult;

  @override
  SSHClient? get rawClient => null;

  @override
  Future<String> execute(String command) async {
    commands.add(command);
    return answer(command);
  }

  @override
  Future<int> runStreaming(String command, void Function(String line) onLine) async {
    commands.add(command);
    answer(command).split('\n').forEach(onLine);
    return 0;
  }

  @override
  Future<InputResult> executeWithInput(String command, Uint8List input) async {
    commands.add(command);
    inputs.add((command: command, input: input));
    return inputResult?.call(command, input) ?? const InputResult(exitCode: 0);
  }

  /// The text of the most recent input sent to the server.
  String get lastInputText => utf8.decode(inputs.last.input);

  @override
  Future<bool> canLoginAgain() async {
    commands.add(secondLogin);
    final i = _logins < loginResults.length ? _logins : loginResults.length - 1;
    _logins++;
    return loginResults[i];
  }

  /// Index of the first recorded command containing [text], or -1.
  int indexOf(String text) => commands.indexWhere((c) => c.contains(text));

  bool ran(String text) => indexOf(text) >= 0;
}
