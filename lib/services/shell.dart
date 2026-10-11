import 'dart:typed_data';
import 'package:dartssh2/dartssh2.dart';

/// What the Linux services need from a connection. [SshClientService]
/// implements it; tests can supply a fake.
abstract interface class RemoteShell {
  bool get isConnected;
  SSHClient? get rawClient;

  /// The session was opened with a private key (not a password).
  bool get usedKeyAuth;

  /// Opens a second, independent connection with the same credentials and
  /// closes it again. Used to prove that a change to SSH or the firewall has
  /// not locked us out, while the first session is still there to undo it.
  Future<bool> canLoginAgain();

  /// Runs [command]; throws [SshCommandException] on a non-zero exit code.
  Future<String> execute(String command);

  /// Runs [command] and reports output lines as they arrive; returns the exit code.
  Future<int> runStreaming(String command, void Function(String line) onLine);

  /// Runs [command] with [input] on its stdin (file contents are far bigger than
  /// a command line may be) and waits for it to finish.
  Future<InputResult> executeWithInput(String command, Uint8List input);
}

/// How a command that was fed through stdin ended.
class InputResult {
  final int? exitCode;
  final bool killedBySignal;
  final String stderr;

  const InputResult({this.exitCode, this.killedBySignal = false, this.stderr = ''});

  bool get ok => !killedBySignal && (exitCode == null || exitCode == 0);
}

/// Quotes [value] so the remote shell treats it as one literal word.
String shQuote(String value) => "'${value.replaceAll("'", r"'\''")}'";

/// Wraps [command] so it runs as root: directly for root, else via passwordless sudo.
///
/// The whole [command] (including `&&` chains) runs inside one `sh -c`, so
/// every part of it gets the privileges, not just the first word.
String asRoot(String command) {
  final q = shQuote(command);
  return 'if [ "\$(id -u)" = 0 ]; then sh -c $q; else sudo -n sh -c $q; fi';
}

/// A remote command finished with a non-zero exit code or was killed.
class SshCommandException implements Exception {
  final String command;
  final int? exitCode;
  final String stderr;

  SshCommandException(this.command, this.exitCode, this.stderr);

  @override
  String toString() {
    final msg = stderr.trim();
    return msg.isNotEmpty ? msg : 'Command failed (exit code $exitCode)';
  }
}
