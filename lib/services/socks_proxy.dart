import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:dartssh2/dartssh2.dart' show SSHSocket;

/// Opens a connection to [host]:[port] from the other end of the tunnel (the
/// server). The host name is resolved there, not here.
typedef SocksDial = Future<SSHSocket> Function(String host, int port);

/// A local SOCKS5 proxy (CONNECT only, no authentication) that sends every
/// connection through [SocksDial], like `ssh -D`.
///
/// It replaces the one in dartssh2, which destroys the browser's socket as soon
/// as the server closes the connection, throwing away the end of the response
/// that has not been written yet (pages arrived cut off or empty). Here data is
/// flushed before a side is closed, and each direction is closed on its own.
class SocksProxy {
  final ServerSocket _server;
  final SocksDial _dial;
  final int maxConnections;
  final Duration handshakeTimeout;
  final Duration connectTimeout;
  final Duration lingerAfterRemoteClose;
  final _connections = <_SocksConnection>{};
  bool _closed = false;

  SocksProxy._(
    this._server,
    this._dial,
    this.maxConnections,
    this.handshakeTimeout,
    this.connectTimeout,
    this.lingerAfterRemoteClose,
  );

  /// Listens on 127.0.0.1 only: nothing else on the network can use the tunnel.
  static Future<SocksProxy> bind({
    required SocksDial dial,
    int port = 0,
    int maxConnections = 512,
    Duration handshakeTimeout = const Duration(seconds: 10),
    Duration connectTimeout = const Duration(seconds: 20),
    Duration lingerAfterRemoteClose = const Duration(seconds: 10),
  }) async {
    final server = await ServerSocket.bind(InternetAddress.loopbackIPv4, port);
    final proxy = SocksProxy._(
      server,
      dial,
      maxConnections,
      handshakeTimeout,
      connectTimeout,
      lingerAfterRemoteClose,
    );
    server.listen(proxy._accept, onError: (_) {});
    return proxy;
  }

  int get port => _server.port;
  bool get isClosed => _closed;
  int get openConnections => _connections.length;

  void _accept(Socket socket) {
    if (_closed || _connections.length >= maxConnections) {
      socket.destroy();
      return;
    }
    final c = _SocksConnection(socket, this);
    _connections.add(c);
    c.start();
  }

  /// Stops listening and drops every connection.
  Future<void> close() async {
    if (_closed) return;
    _closed = true;
    await _server.close();
    for (final c in _connections.toList()) {
      c.abort();
    }
  }
}

class _SocksConnection {
  final Socket _client;
  final SocksProxy _proxy;

  final _buffer = <int>[];
  SSHSocket? _remote;
  StreamSubscription<Uint8List>? _clientSub;
  StreamSubscription<Uint8List>? _remoteSub;
  Timer? _timer;
  bool _streaming = false;
  bool _dialing = false;
  bool _gone = false;
  bool _clientEnded = false;
  bool _remoteEnded = false;
  var _greeted = false;

  _SocksConnection(this._client, this._proxy);

  void start() {
    try {
      _client.setOption(SocketOption.tcpNoDelay, true);
    } catch (_) {}
    _clientSub = _client.listen(
      _onClientData,
      onDone: _onClientDone,
      onError: (_) => abort(),
      cancelOnError: true,
    );
    _timer = Timer(_proxy.handshakeTimeout, abort);
  }

  void abort() {
    if (_gone) return;
    _gone = true;
    _timer?.cancel();
    _clientSub?.cancel();
    _remoteSub?.cancel();
    try {
      _remote?.destroy();
    } catch (_) {}
    _client.destroy();
    _proxy._connections.remove(this);
  }

  void _reply(int code) {
    // version, code, reserved, address type IPv4, address 0.0.0.0, port 0
    _write([5, code, 0, 1, 0, 0, 0, 0, 0, 0]);
  }

  void _write(List<int> data) {
    if (_gone) return;
    try {
      _client.add(data);
    } catch (_) {
      abort();
    }
  }

  Future<void> _failWith(int code) async {
    _reply(code);
    try {
      await _client.flush();
    } catch (_) {}
    abort();
  }

  void _onClientData(Uint8List chunk) {
    if (_gone) return;
    if (_streaming) {
      try {
        _remote!.sink.add(chunk);
      } catch (_) {
        abort();
      }
      return;
    }
    _buffer.addAll(chunk);
    if (_buffer.length > 32768) {
      abort(); // a handshake is a few hundred bytes at most
      return;
    }
    _handshake();
  }

  void _handshake() {
    if (_dialing) return;
    if (!_greeted) {
      if (_buffer.length < 2) return;
      final methods = _buffer[1];
      if (_buffer.length < 2 + methods) return;
      final version = _buffer[0];
      final offered = _buffer.sublist(2, 2 + methods);
      _buffer.removeRange(0, 2 + methods);
      if (version != 5 || !offered.contains(0)) {
        _write([5, 0xFF]); // no acceptable method
        _client.flush().whenComplete(abort);
        return;
      }
      _write([5, 0]);
      _greeted = true;
    }
    _request();
  }

  void _request() {
    if (_buffer.length < 4) return;
    if (_buffer[0] != 5) {
      abort();
      return;
    }
    if (_buffer[1] != 1) {
      _failWith(7); // only CONNECT
      return;
    }
    final atyp = _buffer[3];
    int need;
    if (atyp == 1) {
      need = 10;
    } else if (atyp == 3) {
      if (_buffer.length < 5) return;
      need = 7 + _buffer[4];
    } else if (atyp == 4) {
      need = 22;
    } else {
      _failWith(8);
      return;
    }
    if (_buffer.length < need) return;

    final request = Uint8List.fromList(_buffer.sublist(0, need));
    _buffer.removeRange(0, need);
    final String host;
    if (atyp == 1) {
      host = request.sublist(4, 8).join('.');
    } else if (atyp == 3) {
      host = utf8.decode(request.sublist(5, 5 + request[4]), allowMalformed: true);
    } else {
      host = InternetAddress.fromRawAddress(Uint8List.fromList(request.sublist(4, 20))).address;
    }
    final port = (request[need - 2] << 8) | request[need - 1];
    _connect(host, port);
  }

  Future<void> _connect(String host, int port) async {
    _dialing = true;
    _timer?.cancel();
    SSHSocket remote;
    try {
      remote = await _proxy._dial(host, port).timeout(_proxy.connectTimeout);
    } catch (e) {
      final refused = '$e'.toLowerCase().contains('refused');
      await _failWith(refused ? 5 : 4); // connection refused / host unreachable
      return;
    }
    _dialing = false;
    if (_gone) {
      remote.destroy();
      return;
    }

    _remote = remote;
    _remoteSub = remote.stream.listen(
      _write,
      onDone: _onRemoteDone,
      onError: (_) => abort(),
      cancelOnError: true,
    );
    _reply(0);
    _streaming = true;
    // whatever the browser sent right behind the request
    if (_buffer.isNotEmpty) {
      remote.sink.add(Uint8List.fromList(_buffer));
      _buffer.clear();
    }
  }

  /// The server closed its side: finish writing what it sent, then close ours.
  Future<void> _onRemoteDone() async {
    if (_gone) return;
    _remoteEnded = true;
    try {
      await _client.flush();
      await _client.close(); // sends FIN after the data, does not drop it
    } catch (_) {}
    _maybeFinish();
  }

  /// The browser closed its side: pass that on, keep reading the answer.
  void _onClientDone() {
    _clientEnded = true;
    if (!_streaming) {
      abort();
      return;
    }
    try {
      _remote?.sink.close();
    } catch (_) {}
    _maybeFinish();
  }

  void _maybeFinish() {
    if (_clientEnded && _remoteEnded) {
      abort();
    } else if (_remoteEnded) {
      // The browser has not closed yet; do not keep the connection forever.
      _timer?.cancel();
      _timer = Timer(_proxy.lingerAfterRemoteClose, abort);
    }
  }
}
