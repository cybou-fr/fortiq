import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:webview_windows/webview_windows.dart';
import '../services/ssh_service.dart';

class RemoteBrowserWindow extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;

  const RemoteBrowserWindow({
    super.key,
    required this.sshService,
    required this.domainColor,
  });

  @override
  State<RemoteBrowserWindow> createState() => _RemoteBrowserWindowState();
}

class _RemoteBrowserWindowState extends State<RemoteBrowserWindow> {

  // Async loads may finish after the window was closed.
  @override
  void setState(VoidCallback fn) {
    if (mounted) super.setState(fn);
  }
  final WebviewController _controller = WebviewController();
  final TextEditingController _urlController = TextEditingController(text: 'https://duckduckgo.com');
  bool _ready = false;
  bool _loading = false;
  String _pageTitle = 'Browser';
  String? _error;

  @override
  void initState() {
    super.initState();
    widget.sshService.addListener(_onTunnelChanged);
    _initBrowser();
  }

  void _onTunnelChanged() {
    if (mounted) setState(() {});
  }

  /// All web traffic, including DNS lookups and localhost, goes through the
  /// SSH tunnel. The proxy is the only route: if it is down, pages fail to
  /// load instead of silently connecting directly.
  static String _proxyArguments(int port) => [
        '--proxy-server=socks5://127.0.0.1:$port',
        '--proxy-bypass-list=<-loopback>', // localhost means the server's localhost
        '--force-webrtc-ip-handling-policy=disable_non_proxied_udp',
      ].join(' ');

  Future<void> _initBrowser() async {
    // webview_windows exists only on Windows. Until an Android/iOS engine with
    // verified proxy support is added, the browser stays off there: showing a
    // page without the tunnel would defeat its purpose.
    if (!Platform.isWindows) {
      setState(() => _error =
          'The tunnel browser is not available on this platform yet. '
          'It needs a WebView whose traffic can be forced through the SSH tunnel.');
      return;
    }
    try {
      final port = await widget.sshService.startSocksTunnel();

      // The WebView2 environment exists once per process; the proxy port is
      // fixed for the whole run, so a repeated call is harmless.
      try {
        await WebviewController.initializeEnvironment(
          additionalArguments: _proxyArguments(port),
        );
      } on PlatformException catch (e) {
        if (e.code != 'environment_already_initialized') rethrow;
      }

      await _controller.initialize();

      _controller.title.listen((t) {
        if (mounted) setState(() => _pageTitle = t);
      });

      _controller.url.listen((u) {
        if (mounted) _urlController.text = u;
      });

      _controller.loadingState.listen((state) {
        if (mounted) {
          setState(() => _loading = state == LoadingState.loading);
        }
      });

      await _controller.loadUrl(_urlController.text);

      if (mounted) setState(() => _ready = true);
    } catch (e) {
      // No fallback to a direct connection.
      if (mounted) setState(() => _error = 'Cannot start the tunnel browser: $e');
    }
  }

  void _loadEnteredUrl() {
    var raw = _urlController.text.trim();
    if (raw.isEmpty) return;

    if (!raw.startsWith('http://') && !raw.startsWith('https://')) {
      if (raw.contains('.') && !raw.contains(' ')) {
        raw = 'https://$raw';
      } else {
        raw = 'https://duckduckgo.com/?q=${Uri.encodeComponent(raw)}';
      }
    }
    _controller.loadUrl(raw);
  }

  @override
  void dispose() {
    widget.sshService.removeListener(_onTunnelChanged);
    _urlController.dispose();
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        // Browser Navigation Bar
        Container(
          height: 42,
          padding: const EdgeInsets.symmetric(horizontal: 10),
          decoration: const BoxDecoration(
            color: Color(0xFF161722),
            border: Border(bottom: BorderSide(color: Color(0xFF242637))),
          ),
          child: Row(
            children: [
              IconButton(
                icon: const Icon(Icons.arrow_back, size: 16, color: Colors.white70),
                tooltip: 'Back',
                onPressed: () => _controller.goBack(),
              ),
              IconButton(
                icon: const Icon(Icons.arrow_forward, size: 16, color: Colors.white70),
                tooltip: 'Forward',
                onPressed: () => _controller.goForward(),
              ),
              IconButton(
                icon: _loading
                    ? const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white70))
                    : const Icon(Icons.refresh, size: 16, color: Colors.white70),
                tooltip: 'Reload',
                onPressed: () => _controller.reload(),
              ),
              const SizedBox(width: 8),

              // URL / Search Input Bar
              Expanded(
                child: Container(
                  height: 30,
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  decoration: BoxDecoration(
                    color: const Color(0xFF0F1018),
                    borderRadius: BorderRadius.circular(15),
                    border: Border.all(color: const Color(0xFF2A2D40)),
                  ),
                  child: Row(
                    children: [
                      Icon(
                        _urlController.text.startsWith('https') ? Icons.lock : Icons.public,
                        size: 13,
                        color: _urlController.text.startsWith('https') ? Colors.greenAccent : Colors.white38,
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: TextField(
                          controller: _urlController,
                          style: const TextStyle(color: Colors.white, fontSize: 12),
                          decoration: const InputDecoration(
                            border: InputBorder.none,
                            isDense: true,
                            contentPadding: EdgeInsets.zero,
                          ),
                          onSubmitted: (_) => _loadEnteredUrl(),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(width: 10),

              // VPS Tunnel SOCKS5 Status Badge
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                decoration: BoxDecoration(
                  color: widget.sshService.isTunnelActive ? Colors.green.withValues(alpha: 0.15) : Colors.red.withValues(alpha: 0.15),
                  borderRadius: BorderRadius.circular(10),
                  border: Border.all(
                    color: widget.sshService.isTunnelActive ? Colors.green.withValues(alpha: 0.4) : Colors.red.withValues(alpha: 0.4),
                  ),
                ),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Container(
                      width: 6,
                      height: 6,
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        color: widget.sshService.isTunnelActive ? Colors.greenAccent : Colors.redAccent,
                      ),
                    ),
                    const SizedBox(width: 6),
                    Text(
                      widget.sshService.isTunnelActive ? 'VPS TUNNEL' : 'TUNNEL DOWN',
                      style: TextStyle(
                        color: widget.sshService.isTunnelActive ? Colors.greenAccent : Colors.redAccent,
                        fontSize: 10,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),

        // Webview Content
        Expanded(
          child: _error != null
              ? Center(
                  child: Padding(
                    padding: const EdgeInsets.all(20),
                    child: Text(_error!, textAlign: TextAlign.center, style: const TextStyle(color: Colors.redAccent)),
                  ),
                )
              : !_ready
                  ? Center(child: CircularProgressIndicator(color: widget.domainColor))
                  : Webview(_controller),
        ),
      ],
    );
  }
}
