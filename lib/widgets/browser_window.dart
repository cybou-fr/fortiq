import 'package:flutter/material.dart';
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
  final WebviewController _controller = WebviewController();
  final TextEditingController _urlController = TextEditingController(text: 'https://duckduckgo.com');
  bool _ready = false;
  bool _loading = false;
  bool _tunnelActive = false;
  String _pageTitle = 'Browser';

  @override
  void initState() {
    super.initState();
    _initBrowser();
  }

  Future<void> _initBrowser() async {
    try {
      // Start SOCKS5 SSH Tunnel on 127.0.0.1:1080
      final tunnelStarted = await widget.sshService.startSocksTunnel(localPort: 1080);

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

      if (mounted) {
        setState(() {
          _ready = true;
          _tunnelActive = tunnelStarted;
        });
      }
    } catch (_) {
      // Fallback: initialize webview without tunnel if port busy
      if (!_ready) {
        try {
          await _controller.initialize();
          await _controller.loadUrl(_urlController.text);
          if (mounted) setState(() => _ready = true);
        } catch (_) {}
      }
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
                  color: _tunnelActive ? Colors.green.withValues(alpha: 0.15) : Colors.orange.withValues(alpha: 0.15),
                  borderRadius: BorderRadius.circular(10),
                  border: Border.all(
                    color: _tunnelActive ? Colors.green.withValues(alpha: 0.4) : Colors.orange.withValues(alpha: 0.4),
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
                        color: _tunnelActive ? Colors.greenAccent : Colors.orangeAccent,
                      ),
                    ),
                    const SizedBox(width: 6),
                    Text(
                      _tunnelActive ? 'VPS TUNNEL' : 'DIRECT',
                      style: TextStyle(
                        color: _tunnelActive ? Colors.greenAccent : Colors.orangeAccent,
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
          child: !_ready
              ? Center(child: CircularProgressIndicator(color: widget.domainColor))
              : Webview(_controller),
        ),
      ],
    );
  }
}
