import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:dartssh2/dartssh2.dart';
import 'file_service.dart';
import 'known_hosts_service.dart';
import 'mail_service.dart';
import 'network_service.dart';
import 'shell.dart';
import 'socks_proxy.dart';
import 'system_service.dart';
import 'terminal_service.dart';

// Shell helpers are part of this library's public surface.
export 'shell.dart';

/// The server key was refused: the user said no, or no one could be asked, or it
/// no longer matches the remembered key.
///
/// dartssh2 reports a refused key as "connection closed before authentication",
/// which is indistinguishable from a failed login, so this says what happened.
class HostKeyRejectedError implements Exception {
  final HostKeyInfo? info;

  HostKeyRejectedError(this.info);

  bool get changed => info?.changed ?? false;

  @override
  String toString() => changed
      ? 'The server key has changed and was not trusted'
      : 'The server key was not trusted';
}

/// Host key presented by the server that the user has to approve.
class HostKeyInfo {
  final String host;
  final int port;
  final String type;
  final String fingerprint;

  /// Previously trusted `"<type> <fingerprint>"`, set when the key changed.
  final String? previous;

  HostKeyInfo({
    required this.host,
    required this.port,
    required this.type,
    required this.fingerprint,
    this.previous,
  });

  bool get changed => previous != null;
}

/// Set while a connection attempt runs: did the server key get refused?
class _Rejection {
  bool rejected = false;
  HostKeyInfo? info;

  void refuse(HostKeyInfo i) {
    rejected = true;
    info = i;
  }
}

class _KeyProblem implements Exception {
  final String message;
  const _KeyProblem(this.message);
}

enum SshConnectionState { disconnected, connecting, connected, reconnecting }

class _ConnectionParams {
  final String host;
  final int port;
  final String username;
  final String? password;
  final String? privateKey;

  const _ConnectionParams({
    required this.host,
    required this.port,
    required this.username,
    this.password,
    this.privateKey,
  });
}

class SshClientService extends ChangeNotifier implements RemoteShell {
  SSHClient? _client;

  // Linux operations, each in its own service on top of this connection.
  late final files = FileService(this);
  late final system = SystemService(this);
  late final network = NetworkService(this);
  late final mail = MailService(this);
  late final terminal = TerminalService(this);
  SshConnectionState _state = SshConnectionState.disconnected;

  /// Why the last session ended unexpectedly (null after a normal disconnect).
  String? lastDisconnectReason;

  String? currentHost;
  int? currentPort;
  String? currentUsername;

  // Kept in memory only, to reconnect without asking again.
  _ConnectionParams? _params;
  // Bumped by every explicit connect/disconnect so a reconnect attempt that was
  // already in flight can tell that it has been cancelled.
  int _epoch = 0;
  int reconnectAttempt = 0;
  DateTime? nextRetryAt;
  Timer? _retryTimer;
  Timer? _livenessTimer;
  int _missedPings = 0;
  bool _tunnelWanted = false;

  SocksProxy? _socksForward;
  Future<void>? _tunnelClosing;
  static int? _browserProxyPort;

  static const _retryDelays = [1, 2, 4, 8, 15, 30]; // seconds, last one repeats

  SshConnectionState get state => _state;
  bool get _isConnected => _state == SshConnectionState.connected;
  @override
  bool get isConnected => _isConnected;
  bool get isReconnecting => _state == SshConnectionState.reconnecting;

  /// A session exists: either live, or being restored after a connection loss.
  bool get isSessionActive => isConnected || isReconnecting;
  @override
  SSHClient? get rawClient => _client;

  /// True only while the SOCKS listener is really up on a live SSH session.
  bool get isTunnelActive =>
      _isConnected && _socksForward != null && !_socksForward!.isClosed;

  /// Fixed loopback port for the browser proxy.
  ///
  /// The WebView2 environment is created once per process with the proxy
  /// address baked into its arguments, so the port must stay the same for every
  /// SSH session until the app exits.
  static Future<int> browserProxyPort() async {
    final known = _browserProxyPort;
    if (known != null) return known;
    final probe = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
    final port = probe.port;
    await probe.close();
    return _browserProxyPort = port;
  }

  /// Starts a SOCKS5 proxy on 127.0.0.1 that sends every connection (including
  /// DNS lookups) through the authenticated SSH session, like `ssh -D`, without
  /// spawning an external `ssh` process. Returns the local port.
  ///
  /// Once requested, the tunnel is restored automatically after a reconnect.
  Future<int> startSocksTunnel() async {
    final client = _client;
    if (client == null || !_isConnected) {
      throw Exception('Not connected to SSH server');
    }
    _tunnelWanted = true;
    final existing = _socksForward;
    if (existing != null && !existing.isClosed) return existing.port;

    await _tunnelClosing; // the previous listener must release the port first
    _socksForward = await SocksProxy.bind(
      port: await browserProxyPort(),
      dial: (host, port) => client.forwardLocal(host, port),
    );
    notifyListeners();
    return _socksForward!.port;
  }

  /// Closes the proxy listener. While it is down the browser has no route at
  /// all, so nothing can leak around the tunnel.
  void stopSocksTunnel() {
    _tunnelWanted = false;
    _closeTunnel();
  }

  void _closeTunnel() {
    final forward = _socksForward;
    _socksForward = null;
    if (forward != null) _tunnelClosing = forward.close();
  }

  Future<void> connect({
    required String host,
    required int port,
    required String username,
    String? password,
    String? privateKey,
    Future<bool> Function(HostKeyInfo info)? onHostKey,
  }) async {
    disconnect();
    final epoch = _epoch;
    final params = _ConnectionParams(
      host: host,
      port: port,
      username: username,
      password: password,
      privateKey: privateKey,
    );
    _state = SshConnectionState.connecting;
    notifyListeners();
    try {
      await _open(params, epoch, onHostKey);
    } catch (_) {
      if (epoch == _epoch) {
        _state = SshConnectionState.disconnected;
        notifyListeners();
      }
      rethrow;
    }
    _params = params; // from now on a lost connection is restored automatically
  }

  /// Opens and authenticates one SSH connection and makes it the current one.
  /// The identities in [key], or an exception that says what is wrong with it.
  static List<SSHKeyPair>? _parseIdentities(String? key) {
    if (key == null || key.trim().isEmpty) return null;
    try {
      if (SSHKeyPair.isEncryptedPem(key)) {
        throw const _KeyProblem('Encrypted private keys (with passphrase) are not supported yet');
      }
      return SSHKeyPair.fromPem(key);
    } on _KeyProblem catch (e) {
      throw Exception(e.message);
    } catch (_) {
      throw Exception('Cannot parse the private key file');
    }
  }

  /// Connects the socket and builds the client for [p] (not yet authenticated).
  /// [rejection] tells afterwards whether the server key was refused.
  Future<SSHClient> _newClient(
    _ConnectionParams p,
    Future<bool> Function(HostKeyInfo info)? onHostKey,
    _Rejection rejection,
  ) async {
    // A bad key is reported before anything goes over the network.
    final identities = _parseIdentities(p.privateKey);

    final socket = await SSHSocket.connect(
      p.host,
      p.port,
      timeout: const Duration(seconds: 10),
    );

    final password = p.password;
    return SSHClient(
      socket,
      username: p.username,
      onPasswordRequest: password != null ? () => password : null,
      identities: identities,
      // Without limits a server that accepts the TCP connection but never
      // answers would keep the connect (or every reconnect) hanging forever.
      handshakeTimeout: const Duration(seconds: 15),
      authTimeout: const Duration(seconds: 30),
      onVerifyHostKey: (type, fingerprint) async {
        final ok = await _verifyHostKey(
          p.host,
          p.port,
          type,
          utf8.decode(fingerprint),
          onHostKey,
          rejection,
        );
        return ok;
      },
    );
  }

  @override
  bool get usedKeyAuth => _params?.privateKey?.trim().isNotEmpty ?? false;

  @override
  Future<bool> canLoginAgain() async {
    final p = _params;
    if (p == null) return false;
    SSHClient? probe;
    try {
      // A key that no longer matches fails here instead of being trusted.
      probe = await _newClient(p, null, _Rejection());
      await probe.authenticated.timeout(const Duration(seconds: 30));
      return true;
    } catch (_) {
      return false;
    } finally {
      probe?.close();
    }
  }

  Future<void> _open(
    _ConnectionParams p,
    int epoch,
    Future<bool> Function(HostKeyInfo info)? onHostKey,
  ) async {
    final rejection = _Rejection();
    final client = await _newClient(p, onHostKey, rejection);

    try {
      await client.authenticated;
    } catch (_) {
      client.close();
      if (rejection.info != null || rejection.rejected) {
        throw HostKeyRejectedError(rejection.info);
      }
      rethrow;
    }

    if (epoch != _epoch) {
      // Cancelled (Disconnect pressed) while we were connecting.
      client.close();
      throw StateError('Connection attempt cancelled');
    }

    _client = client;
    currentHost = p.host;
    currentPort = p.port;
    currentUsername = p.username;
    _state = SshConnectionState.connected;
    lastDisconnectReason = null;
    reconnectAttempt = 0;
    nextRetryAt = null;
    _startLiveness(client);
    notifyListeners();

    // Notice drops (network loss, server closing).
    unawaited(client.done.then(
      (_) => _handleClosed(client, 'Connection closed by the server'),
      onError: (Object e) => _handleClosed(client, e.toString()),
    ));
  }

  /// A dead network often leaves the TCP connection half-open: nothing tells us
  /// it is gone. Ping regularly and treat missing answers as a lost connection.
  void _startLiveness(SSHClient client) {
    _livenessTimer?.cancel();
    _missedPings = 0;
    _livenessTimer = Timer.periodic(const Duration(seconds: 10), (_) async {
      if (!identical(client, _client)) return;
      try {
        await client.ping().timeout(const Duration(seconds: 8));
        _missedPings = 0;
      } catch (_) {
        _missedPings++;
        if (_missedPings >= 2 && identical(client, _client)) {
          _handleClosed(client, 'Connection lost (server not responding)');
          client.close();
        }
      }
    });
  }

  void _handleClosed(SSHClient client, String reason) {
    // Ignore if we closed it ourselves or already moved on to another client.
    if (!identical(client, _client)) return;
    _client = null;
    _livenessTimer?.cancel();
    _closeTunnel(); // _tunnelWanted stays set: it is restored after reconnect
    lastDisconnectReason = reason;

    if (_params == null) {
      _state = SshConnectionState.disconnected;
      notifyListeners();
      return;
    }
    _state = SshConnectionState.reconnecting;
    reconnectAttempt = 0;
    _scheduleRetry();
  }

  void _scheduleRetry() {
    final idx = reconnectAttempt < _retryDelays.length
        ? reconnectAttempt
        : _retryDelays.length - 1;
    final delay = Duration(seconds: _retryDelays[idx]);
    nextRetryAt = DateTime.now().add(delay);
    _retryTimer?.cancel();
    _retryTimer = Timer(delay, _tryReconnect);
    notifyListeners();
  }

  /// Skips the waiting time and reconnects right now.
  void retryNow() {
    if (!isReconnecting || _attemptInFlight) return;
    _retryTimer?.cancel();
    unawaited(_tryReconnect());
  }

  bool _attemptInFlight = false;

  /// One reconnect attempt at a time: the timer and "Retry now" must never
  /// produce two authenticated clients, one of which would be left unmanaged.
  Future<void> _tryReconnect() async {
    if (_attemptInFlight) return;
    _attemptInFlight = true;
    try {
      await _reconnectOnce();
    } finally {
      _attemptInFlight = false;
    }
  }

  Future<void> _reconnectOnce() async {
    final params = _params;
    if (_state != SshConnectionState.reconnecting || params == null) return;
    final epoch = _epoch;
    reconnectAttempt++;
    nextRetryAt = null;
    notifyListeners();

    try {
      // No host-key prompt here: a key that no longer matches must stop the
      // reconnect instead of being trusted automatically.
      await _open(params, epoch, null);
    } catch (e) {
      if (epoch != _epoch || _state != SshConnectionState.reconnecting) return;
      if (e is SSHAuthError || e is SSHHostkeyError || e is HostKeyRejectedError) {
        lastDisconnectReason = e is HostKeyRejectedError || e is SSHHostkeyError
            ? 'Reconnect stopped: the server key changed or is not trusted'
            : 'Reconnect stopped: authentication failed';
        _params = null;
        _state = SshConnectionState.disconnected;
        notifyListeners();
        return;
      }
      _scheduleRetry();
      return;
    }

    if (_tunnelWanted) {
      try {
        await startSocksTunnel();
      } catch (_) {
        // The browser window shows "TUNNEL DOWN" until it is started again.
      }
    }
  }

  /// Trust on first use: unknown keys and changed keys need user approval.
  Future<bool> _verifyHostKey(
    String host,
    int port,
    String type,
    String fingerprint,
    Future<bool> Function(HostKeyInfo info)? onHostKey,
    _Rejection rejection,
  ) async {
    final current = '$type $fingerprint';
    final known = await KnownHostsService.lookup(host, port);
    if (known == current) return true;

    final info = HostKeyInfo(
      host: host,
      port: port,
      type: type,
      fingerprint: fingerprint,
      previous: known,
    );
    if (onHostKey == null) {
      rejection.refuse(info);
      return false;
    }
    final accepted = await onHostKey(info);
    if (accepted) {
      await KnownHostsService.remember(host, port, current);
    } else {
      rejection.refuse(info);
    }
    return accepted;
  }

  /// Ends the session for good: cancels any reconnect and forgets credentials.
  void disconnect() {
    _epoch++;
    _retryTimer?.cancel();
    _livenessTimer?.cancel();
    _params = null;
    _tunnelWanted = false;
    _closeTunnel();
    _client?.close();
    _client = null;
    reconnectAttempt = 0;
    nextRetryAt = null;
    lastDisconnectReason = null;
    final changed = _state != SshConnectionState.disconnected;
    _state = SshConnectionState.disconnected;
    if (changed) notifyListeners();
  }

  // ---- tmux: terminals that survive a lost connection ----

  /// Runs [command] and reports every output line (stdout and stderr) as it
  /// arrives, for long jobs such as `apt-get`. Returns the exit code.
  @override
  Future<int> runStreaming(String command, void Function(String line) onLine) async {
    final client = _client;
    if (client == null || !_isConnected) {
      throw Exception('Not connected to SSH server');
    }
    final session = await client.execute(command);

    Future<void> pump(Stream<Uint8List> source) {
      final finished = Completer<void>();
      source
          .cast<List<int>>()
          .transform(const Utf8Decoder(allowMalformed: true))
          .transform(const LineSplitter())
          .listen(onLine, onDone: finished.complete, onError: (_) => finished.complete());
      return finished.future;
    }

    await Future.wait([pump(session.stdout), pump(session.stderr)]);
    await session.done;
    return session.exitCode ?? -1;
  }

  @override
  Future<InputResult> executeWithInput(String command, Uint8List input) async {
    final client = _client;
    if (client == null || !_isConnected) {
      throw Exception('Not connected to SSH server');
    }
    final session = await client.execute(command);
    // Both streams must be drained while stdin is being uploaded. Otherwise a
    // command that writes enough stdout (notably AI JSON) can block the SSH
    // channel and prevent the remote process from finishing.
    final stdoutFuture = utf8.decodeStream(session.stdout);
    final stderrFuture = utf8.decodeStream(session.stderr);
    session.stdin.add(input);
    await session.stdin.close();
    await session.done;
    final output = await stdoutFuture;
    final errors = await stderrFuture;
    return InputResult(
      exitCode: session.exitCode,
      killedBySignal: session.exitSignal != null,
      stdout: output,
      stderr: errors,
    );
  }

  @override
  Future<String> execute(String command) async {
    if (_client == null || !_isConnected) {
      throw Exception('Not connected to SSH server');
    }
    final result = await _client!.runWithResult(command);
    final code = result.exitCode;
    if ((code != null && code != 0) || result.exitSignal != null) {
      throw SshCommandException(
        command,
        code,
        utf8.decode(result.stderr, allowMalformed: true),
      );
    }
    return utf8.decode(result.stdout, allowMalformed: true);
  }

}

