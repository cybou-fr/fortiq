import 'package:dartssh2/dartssh2.dart';
import 'shell.dart';

/// Shell command that attaches to the tmux session [name], creating it if
/// needed. Options are scoped to this session so the user's other tmux sessions
/// and their own configuration are left alone.
String tmuxAttachCommand(String name) {
  final n = shQuote(name);
  final sep = shQuote(';'); // tmux's own command separator, as a plain argument
  return 'tmux -u new-session -A -s $n'
      ' $sep set-option -t $n status off'
      ' $sep set-option -t $n mouse on';
}

/// Interactive shells, tmux sessions that survive a lost connection.
class TerminalService {
  final RemoteShell _ssh;

  TerminalService(this._ssh);

  Future<SSHSession> startShell({
    int ptyWidth = 80,
    int ptyHeight = 24,
    int pixelWidth = 0,
    int pixelHeight = 0,
  }) async {
    if (_ssh.rawClient == null || !_ssh.isConnected) {
      throw Exception('Not connected');
    }
    return await _ssh.rawClient!.shell(
      pty: SSHPtyConfig(
        width: ptyWidth,
        height: ptyHeight,
        pixelWidth: pixelWidth,
        pixelHeight: pixelHeight,
      ),
    );
  }

  /// Terminal sessions are named `fortiq-<n>`; tmux keeps them on the server.
  Future<bool> hasTmux() async {
    try {
      await _ssh.execute('command -v tmux >/dev/null 2>&1');
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<void> installTmux() async {
    await _ssh.execute(asRoot('apt-get update && apt-get install -y tmux'));
  }

  /// Opens (or re-attaches to) the tmux session [name] on a fresh PTY.
  Future<SSHSession> attachTmux(
    String name, {
    int ptyWidth = 100,
    int ptyHeight = 30,
  }) async {
    final client = _ssh.rawClient;
    if (client == null || !_ssh.isConnected) {
      throw Exception('Not connected');
    }
    final command = tmuxAttachCommand(name);
    return client.execute(
      command,
      pty: SSHPtyConfig(width: ptyWidth, height: ptyHeight),
    );
  }

  /// Ends a terminal session for good (used when its window is closed).
  Future<void> killTmuxSession(String name) async {
    try {
      await _ssh.execute('tmux kill-session -t ${shQuote(name)} 2>/dev/null || true');
    } catch (_) {
      // Not connected or no tmux: nothing to clean up.
    }
  }
}
