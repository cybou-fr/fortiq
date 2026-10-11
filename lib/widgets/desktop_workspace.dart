import 'dart:async';
import 'package:flutter/material.dart';
import '../services/ssh_service.dart';
import 'file_explorer.dart';
import 'process_manager.dart';
import 'service_manager.dart';
import 'terminal_view.dart';
import 'file_editor.dart';
import 'network_inspector.dart';
import 'browser_window.dart';
import 'markdown_studio.dart';
import 'spotlight_search.dart';
import 'log_viewer.dart';
import 'mailbox_viewer.dart';
import 'media_viewer.dart';

enum FortiqAppType {
  fileExplorer,
  processManager,
  serviceManager,
  terminal,
  editor,
  networkInspector,
  browser,
  docsStudio,
  spotlight,
  logViewer,
  mailbox,
  mediaViewer,
}

class InternalWindow {
  final String id;
  final FortiqAppType appType;
  String title;
  Offset position;
  Size size;
  bool isMinimized;
  bool isMaximized;
  String? filePath;

  InternalWindow({
    required this.id,
    required this.appType,
    required this.title,
    required this.position,
    required this.size,
    this.isMinimized = false,
    this.isMaximized = false,
    this.filePath,
  });
}

class HeadlessDesktopWorkspace extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;
  final String hostLabel;

  const HeadlessDesktopWorkspace({
    super.key,
    required this.sshService,
    required this.domainColor,
    required this.hostLabel,
  });

  @override
  State<HeadlessDesktopWorkspace> createState() => _HeadlessDesktopWorkspaceState();
}

class _HeadlessDesktopWorkspaceState extends State<HeadlessDesktopWorkspace> {
  final List<InternalWindow> _windows = [];
  String? _activeWindowId;
  Timer? _metricsTimer;

  // System stats in Top Bar
  Map<String, dynamic> _metrics = {
    'cpu': 0.0,
    'totalMem': 1,
    'usedMem': 0,
    'uptime': '...',
    'disk': '--',
  };

  @override
  void initState() {
    super.initState();
    // Launch File Explorer by default
    _openWindow(FortiqAppType.fileExplorer);

    _refreshMetrics();
    _metricsTimer = Timer.periodic(const Duration(seconds: 4), (_) => _refreshMetrics());
  }

  @override
  void dispose() {
    _metricsTimer?.cancel();
    super.dispose();
  }

  Future<void> _refreshMetrics() async {
    if (!widget.sshService.isConnected) return;
    try {
      final data = await widget.sshService.fetchMetrics();
      if (mounted) {
        setState(() => _metrics = data);
      }
    } catch (_) {}
  }

  void _openWindow(FortiqAppType type, {String? filePath}) {
    if (type != FortiqAppType.editor) {
      final existing = _windows.where((w) => w.appType == type).firstOrNull;
      if (existing != null) {
        setState(() {
          existing.isMinimized = false;
          _activeWindowId = existing.id;
          _bringToFront(existing.id);
        });
        return;
      }
    }

    final id = DateTime.now().millisecondsSinceEpoch.toString();
    final count = _windows.length;
    final offset = Offset(40.0 + (count * 30), 40.0 + (count * 30));

    String title;
    Size size;

    switch (type) {
      case FortiqAppType.fileExplorer:
        title = 'File Explorer (ls / stat)';
        size = const Size(700, 480);
        break;
      case FortiqAppType.processManager:
        title = 'Activity Monitor (ps)';
        size = const Size(680, 440);
        break;
      case FortiqAppType.serviceManager:
        title = 'Services (systemctl)';
        size = const Size(620, 440);
        break;
      case FortiqAppType.terminal:
        title = 'Linux Shell (bash)';
        size = const Size(600, 380);
        break;
      case FortiqAppType.editor:
        title = 'Editor: ${filePath ?? "untitled"}';
        size = const Size(720, 500);
        break;
      case FortiqAppType.networkInspector:
        title = 'Firewall & Network (ufw / ss)';
        size = const Size(680, 460);
        break;
      case FortiqAppType.browser:
        title = 'Fortiq Secure Browser (VPS Tunnel)';
        size = const Size(820, 540);
        break;
      case FortiqAppType.docsStudio:
        title = 'Docs & Markdown Studio';
        size = const Size(840, 520);
        break;
      case FortiqAppType.spotlight:
        title = 'Spotlight & File Search Hub';
        size = const Size(660, 480);
        break;
      case FortiqAppType.logViewer:
        title = 'System Logs & Security Audit (journalctl)';
        size = const Size(780, 500);
        break;
      case FortiqAppType.mailbox:
        title = 'Mailbox & System Notifier (/var/mail)';
        size = const Size(820, 520);
        break;
      case FortiqAppType.mediaViewer:
        title = 'Media Viewer: ${filePath?.split('/').last ?? "image"}';
        size = const Size(760, 520);
        break;
    }

    final screenWidth = MediaQuery.of(context).size.width;
    final isCompact = screenWidth < 768;

    final win = InternalWindow(
      id: id,
      appType: type,
      title: title,
      position: isCompact ? Offset.zero : offset,
      size: size,
      isMaximized: isCompact, // Auto-maximize on compact/mobile screens
      filePath: filePath,
    );

    setState(() {
      _windows.add(win);
      _activeWindowId = id;
    });
  }

  void _bringToFront(String id) {
    setState(() {
      final index = _windows.indexWhere((w) => w.id == id);
      if (index != -1) {
        final win = _windows.removeAt(index);
        _windows.add(win);
        _activeWindowId = id;
      }
    });
  }

  void _closeWindow(String id) {
    setState(() {
      _windows.removeWhere((w) => w.id == id);
      if (_activeWindowId == id) {
        _activeWindowId = _windows.isNotEmpty ? _windows.last.id : null;
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: const Color(0xFF0F1017),
      child: Stack(
        children: [
          // Desktop Wallpaper / Background Grid
          Positioned.fill(
            child: CustomPaint(
              painter: _GridBackgroundPainter(color: widget.domainColor.withValues(alpha: 0.03)),
            ),
          ),

          // Top Status Bar (macOS / Linux Panel Style)
          Positioned(
            top: 0,
            left: 0,
            right: 0,
            height: 30,
            child: _buildTopPanel(),
          ),

          // Internal Windows Layer
          Positioned.fill(
            top: 30,
            bottom: 64, // Margin for Dock
            child: Stack(
              children: _windows.map((win) {
                if (win.isMinimized) return const SizedBox.shrink();
                return _buildInternalWindow(win);
              }).toList(),
            ),
          ),

          // Desktop Dock (bottom centered)
          Positioned(
            bottom: 12,
            left: 0,
            right: 0,
            child: Center(
              child: _buildDock(),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildTopPanel() {
    final cpu = (_metrics['cpu'] as double?) ?? 0.0;
    final totalMem = (_metrics['totalMem'] as int?) ?? 1;
    final usedMem = (_metrics['usedMem'] as int?) ?? 0;
    final memPercent = totalMem > 0 ? (usedMem / totalMem * 100).toInt() : 0;
    final uptime = (_metrics['uptime'] as String?) ?? '';
    final disk = (_metrics['disk'] as String?) ?? '';

    return Container(
      decoration: const BoxDecoration(
        color: Color(0xFF161722),
        border: Border(bottom: BorderSide(color: Color(0xFF232536))),
      ),
      padding: const EdgeInsets.symmetric(horizontal: 10),
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        physics: const BouncingScrollPhysics(),
        child: Row(
          children: [
            Icon(Icons.desktop_windows, size: 14, color: widget.domainColor),
            const SizedBox(width: 8),
            Text(
              widget.hostLabel,
              style: TextStyle(
                color: widget.domainColor,
                fontWeight: FontWeight.bold,
                fontSize: 11,
                letterSpacing: 0.5,
              ),
            ),
            const SizedBox(width: 14),
            _metricPill(Icons.speed, 'CPU ${cpu.toStringAsFixed(1)}%'),
            const SizedBox(width: 12),
            _metricPill(Icons.memory, 'RAM $memPercent% ($usedMem/${totalMem}MB)'),
            if (disk.isNotEmpty) ...[
              const SizedBox(width: 12),
              _metricPill(Icons.storage, 'Disk $disk'),
            ],
            if (uptime.isNotEmpty) ...[
              const SizedBox(width: 12),
              Text(
                'Uptime: $uptime',
                style: const TextStyle(color: Colors.white38, fontSize: 11),
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _metricPill(IconData icon, String text) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 12, color: Colors.white60),
        const SizedBox(width: 4),
        Text(text, style: const TextStyle(color: Colors.white70, fontSize: 11)),
      ],
    );
  }

  Widget _buildInternalWindow(InternalWindow win) {
    final isActive = _activeWindowId == win.id;

    return Positioned(
      left: win.isMaximized ? 0 : win.position.dx,
      top: win.isMaximized ? 0 : win.position.dy,
      width: win.isMaximized ? MediaQuery.of(context).size.width : win.size.width,
      height: win.isMaximized ? (MediaQuery.of(context).size.height - 94) : win.size.height,
      child: Listener(
        onPointerDown: (_) {
          if (_activeWindowId != win.id) {
            _bringToFront(win.id);
          }
        },
        child: Container(
          decoration: BoxDecoration(
            color: const Color(0xFF14151F),
            borderRadius: BorderRadius.circular(8),
            border: Border.all(
              color: isActive ? widget.domainColor.withValues(alpha: 0.8) : const Color(0xFF2C2E42),
              width: isActive ? 1.5 : 1.0,
            ),
            boxShadow: [
              BoxShadow(
                color: Colors.black.withValues(alpha: 0.5),
                blurRadius: 18,
                offset: const Offset(0, 8),
              ),
            ],
          ),
          child: ClipRRect(
            borderRadius: BorderRadius.circular(7),
            child: Column(
              children: [
                // Inner Window Titlebar
                GestureDetector(
                  onPanUpdate: win.isMaximized
                      ? null
                      : (details) {
                          setState(() {
                            win.position += details.delta;
                          });
                        },
                  onDoubleTap: () {
                    setState(() {
                      win.isMaximized = !win.isMaximized;
                    });
                  },
                  child: Container(
                    height: 32,
                    padding: const EdgeInsets.symmetric(horizontal: 10),
                    decoration: BoxDecoration(
                      color: isActive ? const Color(0xFF1E202E) : const Color(0xFF171822),
                      border: Border(bottom: BorderSide(color: const Color(0xFF26283B))),
                    ),
                    child: Row(
                      children: [
                        Icon(
                          _getIconForType(win.appType),
                          size: 14,
                          color: isActive ? widget.domainColor : Colors.white38,
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            win.title,
                            style: TextStyle(
                              color: isActive ? Colors.white : Colors.white60,
                              fontSize: 12,
                              fontWeight: FontWeight.w600,
                            ),
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                        InkWell(
                          onTap: () => setState(() => win.isMinimized = true),
                          child: const Padding(
                            padding: EdgeInsets.all(4),
                            child: Icon(Icons.remove, size: 13, color: Colors.white60),
                          ),
                        ),
                        InkWell(
                          onTap: () => setState(() => win.isMaximized = !win.isMaximized),
                          child: const Padding(
                            padding: EdgeInsets.all(4),
                            child: Icon(Icons.crop_square, size: 13, color: Colors.white60),
                          ),
                        ),
                        InkWell(
                          onTap: () => _closeWindow(win.id),
                          child: const Padding(
                            padding: EdgeInsets.all(4),
                            child: Icon(Icons.close, size: 13, color: Colors.redAccent),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),

                // Window Inner Body
                Expanded(
                  child: _buildAppContent(win),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildAppContent(InternalWindow win) {
    switch (win.appType) {
      case FortiqAppType.fileExplorer:
        return RemoteFileExplorer(
          sshService: widget.sshService,
          initialPath: '/',
          domainColor: widget.domainColor,
          onOpenFile: (path) {
            final ext = path.toLowerCase();
            if (ext.endsWith('.png') ||
                ext.endsWith('.jpg') ||
                ext.endsWith('.jpeg') ||
                ext.endsWith('.gif') ||
                ext.endsWith('.webp') ||
                ext.endsWith('.bmp')) {
              _openWindow(FortiqAppType.mediaViewer, filePath: path);
            } else if (ext.endsWith('.md')) {
              _openWindow(FortiqAppType.docsStudio, filePath: path);
            } else {
              _openWindow(FortiqAppType.editor, filePath: path);
            }
          },
        );
      case FortiqAppType.processManager:
        return RemoteProcessManager(
          sshService: widget.sshService,
          domainColor: widget.domainColor,
        );
      case FortiqAppType.serviceManager:
        return RemoteServiceManager(
          sshService: widget.sshService,
          domainColor: widget.domainColor,
        );
      case FortiqAppType.terminal:
        return RemoteTerminalView(
          sshService: widget.sshService,
          domainColor: widget.domainColor,
        );
      case FortiqAppType.editor:
        return RemoteFileEditor(
          sshService: widget.sshService,
          filePath: win.filePath ?? '/etc/hosts',
          domainColor: widget.domainColor,
        );
      case FortiqAppType.networkInspector:
        return RemoteNetworkInspector(
          sshService: widget.sshService,
          domainColor: widget.domainColor,
        );
      case FortiqAppType.browser:
        return RemoteBrowserWindow(
          sshService: widget.sshService,
          domainColor: widget.domainColor,
        );
      case FortiqAppType.docsStudio:
        return RemoteMarkdownStudio(
          sshService: widget.sshService,
          initialFilePath: win.filePath,
          domainColor: widget.domainColor,
        );
      case FortiqAppType.spotlight:
        return RemoteSpotlightSearch(
          sshService: widget.sshService,
          domainColor: widget.domainColor,
          onOpenFile: (path) => _openWindow(FortiqAppType.editor, filePath: path),
        );
      case FortiqAppType.logViewer:
        return RemoteLogViewer(
          sshService: widget.sshService,
          domainColor: widget.domainColor,
        );
      case FortiqAppType.mailbox:
        return RemoteMailboxViewer(
          sshService: widget.sshService,
          domainColor: widget.domainColor,
        );
      case FortiqAppType.mediaViewer:
        return RemoteMediaViewer(
          sshService: widget.sshService,
          filePath: win.filePath ?? '',
          domainColor: widget.domainColor,
        );
    }
  }

  Widget _buildDock() {
    return Container(
      height: 52,
      margin: const EdgeInsets.symmetric(horizontal: 8),
      padding: const EdgeInsets.symmetric(horizontal: 10),
      decoration: BoxDecoration(
        color: const Color(0xFF1B1C28).withValues(alpha: 0.96),
        borderRadius: BorderRadius.circular(26),
        border: Border.all(color: widget.domainColor.withValues(alpha: 0.35), width: 1.5),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: 0.45),
            blurRadius: 18,
            offset: const Offset(0, 4),
          ),
        ],
      ),
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        physics: const BouncingScrollPhysics(),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            _dockItem(Icons.folder_open, 'Files', FortiqAppType.fileExplorer),
            _dockItem(Icons.search, 'Search Hub', FortiqAppType.spotlight),
            _dockItem(Icons.public, 'Browser', FortiqAppType.browser),
            _dockItem(Icons.mail_outline, 'Mailbox', FortiqAppType.mailbox),
            _dockItem(Icons.menu_book, 'Docs & Notes', FortiqAppType.docsStudio),
            _dockItem(Icons.receipt_long, 'System Logs', FortiqAppType.logViewer),
            _dockItem(Icons.bar_chart, 'Processes', FortiqAppType.processManager),
            _dockItem(Icons.settings, 'Services', FortiqAppType.serviceManager),
            _dockItem(Icons.computer, 'Terminal', FortiqAppType.terminal),
            _dockItem(Icons.edit_note, 'Editor', FortiqAppType.editor),
            _dockItem(Icons.shield, 'Firewall & Ports', FortiqAppType.networkInspector),
          ],
        ),
      ),
    );
  }

  Widget _dockItem(IconData icon, String tooltip, FortiqAppType type) {
    final isOpen = _windows.any((w) => w.appType == type && !w.isMinimized);

    return Tooltip(
      message: tooltip,
      child: InkWell(
        borderRadius: BorderRadius.circular(22),
        onTap: () => _openWindow(type),
        child: Container(
          width: 48,
          height: 44,
          margin: const EdgeInsets.symmetric(horizontal: 2),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon, size: 21, color: isOpen ? widget.domainColor : Colors.white70),
              const SizedBox(height: 3),
              Container(
                width: 4,
                height: 4,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: isOpen ? widget.domainColor : Colors.transparent,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  IconData _getIconForType(FortiqAppType type) {
    switch (type) {
      case FortiqAppType.fileExplorer:
        return Icons.folder_open;
      case FortiqAppType.spotlight:
        return Icons.search;
      case FortiqAppType.browser:
        return Icons.public;
      case FortiqAppType.docsStudio:
        return Icons.menu_book;
      case FortiqAppType.processManager:
        return Icons.bar_chart;
      case FortiqAppType.serviceManager:
        return Icons.settings;
      case FortiqAppType.terminal:
        return Icons.computer;
      case FortiqAppType.editor:
        return Icons.edit_note;
      case FortiqAppType.networkInspector:
        return Icons.shield;
      case FortiqAppType.logViewer:
        return Icons.receipt_long;
      case FortiqAppType.mailbox:
        return Icons.mail_outline;
      case FortiqAppType.mediaViewer:
        return Icons.image;
    }
  }
}

class _GridBackgroundPainter extends CustomPainter {
  final Color color;
  _GridBackgroundPainter({required this.color});

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = color
      ..strokeWidth = 1;

    const step = 32.0;
    for (double x = 0; x < size.width; x += step) {
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), paint);
    }
    for (double y = 0; y < size.height; y += step) {
      canvas.drawLine(Offset(0, y), Offset(size.width, y), paint);
    }
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}
