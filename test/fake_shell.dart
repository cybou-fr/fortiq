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
