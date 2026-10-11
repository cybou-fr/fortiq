import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_markdown/flutter_markdown.dart';
import '../services/ssh_service.dart';

class RemoteMarkdownStudio extends StatefulWidget {
  final SshClientService sshService;
  final String? initialFilePath;
  final Color domainColor;
  final ValueChanged<bool>? onDirtyChanged;

  const RemoteMarkdownStudio({
    super.key,
    required this.sshService,
    this.initialFilePath,
    required this.domainColor,
    this.onDirtyChanged,
  });

  @override
  State<RemoteMarkdownStudio> createState() => _RemoteMarkdownStudioState();
}

class _RemoteMarkdownStudioState extends State<RemoteMarkdownStudio> {

  // Async loads may finish after the window was closed.
  @override
  void setState(VoidCallback fn) {
    if (mounted) super.setState(fn);
  }
  final TextEditingController _pathController = TextEditingController();
  final TextEditingController _contentController = TextEditingController();
  bool _loading = false;
  bool _saving = false;
  bool _isDirty = false;
  bool _showPreview = true;
  bool _splitMode = true; // true: side-by-side, false: tabs
  String? _error;

  @override
  void initState() {
    super.initState();
    _pathController.text = widget.initialFilePath ?? '/root/notes.md';
    if (widget.initialFilePath != null) {
      _loadFile();
    } else {
      _contentController.text = '''# Project Notes & Docs 📑

Welcome to **Fortiq Docs Studio**! Edit your documentation, notes, and specs directly on your server with a fast side-by-side Markdown preview.

## Key Features
- **Zero Server Overhead:** Everything renders locally on Windows.
- **Auto-Sync:** Hit `Ctrl+S` to instantly save changes to the Linux server.
- **Full Markdown Support:**
  - Headers, bold, italics, blockquotes
  - Lists, checkboxes, tables
  - Code blocks with syntax highlighting

```bash
# Example server command
systemctl status nginx
uptime -p
```

| Service | Status | Port |
| :--- | :--- | :--- |
| Nginx | Running | 80 / 443 |
| SSH | Active | 22 |
| Cybou Node | Active | Custom |

> *"Simplicity is prerequisite for reliability."* — Edsger W. Dijkstra
''';
    }

    _contentController.addListener(() {
      if (!_isDirty) {
        setState(() => _isDirty = true);
        widget.onDirtyChanged?.call(true);
      }
    });
  }

  Future<void> _loadFile() async {
    final path = _pathController.text.trim();
    if (path.isEmpty) return;

    setState(() {
      _loading = true;
      _error = null;
    });

    try {
      final content = await widget.sshService.files.readFile(path);
      if (!mounted) return; // window closed while loading: controller is gone
      _contentController.text = content;
      setState(() {
        _loading = false;
        _isDirty = false;
      });
      widget.onDirtyChanged?.call(false);
    } catch (e) {
      setState(() {
        _loading = false;
        _error = e.toString();
      });
    }
  }

  Future<void> _saveFile() async {
    final path = _pathController.text.trim();
    if (path.isEmpty) return;

    if (_saving) return;
    setState(() => _saving = true);
    // Only the text that was actually sent counts as saved (typing continues
    // while the upload runs).
    final sent = _contentController.text;
    try {
      final atomic = await widget.sshService.files.writeFile(path, sent);
      final unchangedSinceSend = mounted && _contentController.text == sent;
      setState(() {
        _saving = false;
        if (unchangedSinceSend) _isDirty = false;
      });
      if (unchangedSinceSend) widget.onDirtyChanged?.call(false);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(atomic
                ? 'Saved to $path'
                : 'Saved in place (directory is read-only, not atomic): $path'),
            backgroundColor: widget.domainColor,
            duration: const Duration(seconds: 2),
          ),
        );
      }
    } catch (e) {
      setState(() => _saving = false);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Save error: $e'), backgroundColor: Colors.redAccent),
        );
      }
    }
  }

  @override
  void dispose() {
    _pathController.dispose();
    _contentController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return CallbackShortcuts(
      bindings: {
        const SingleActivator(LogicalKeyboardKey.keyS, control: true): _saveFile,
      },
      child: Focus(
        autofocus: true,
        child: Column(
          children: [
            // Top Toolbar
            Container(
              height: 42,
              padding: const EdgeInsets.symmetric(horizontal: 10),
              decoration: const BoxDecoration(
                color: Color(0xFF161722),
                border: Border(bottom: BorderSide(color: Color(0xFF242636))),
              ),
              child: Row(
                children: [
                  Icon(Icons.menu_book, size: 16, color: widget.domainColor),
                  const SizedBox(width: 8),
                  // File path bar
                  Expanded(
                    child: Container(
                      height: 30,
                      padding: const EdgeInsets.symmetric(horizontal: 10),
                      decoration: BoxDecoration(
                        color: const Color(0xFF0F1018),
                        borderRadius: BorderRadius.circular(6),
                        border: Border.all(color: const Color(0xFF26283A)),
                      ),
                      child: Row(
                        children: [
                          const Icon(Icons.description_outlined, size: 13, color: Colors.white38),
                          const SizedBox(width: 6),
                          Expanded(
                            child: TextField(
                              controller: _pathController,
                              style: const TextStyle(color: Colors.white, fontSize: 12, fontFamily: 'Consolas, monospace'),
                              decoration: const InputDecoration(
                                border: InputBorder.none,
                                isDense: true,
                                contentPadding: EdgeInsets.zero,
                              ),
                              onSubmitted: (_) => _loadFile(),
                            ),
                          ),
                          if (_isDirty) ...[
                            Container(
                              width: 6,
                              height: 6,
                              decoration: const BoxDecoration(color: Colors.amberAccent, shape: BoxShape.circle),
                            ),
                            const SizedBox(width: 4),
                          ],
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  // Save Button
                  ElevatedButton.icon(
                    onPressed: _saving || _loading ? null : _saveFile,
                    icon: _saving
                        ? const SizedBox(width: 12, height: 12, child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white))
                        : const Icon(Icons.save, size: 14),
                    label: const Text('Save (Ctrl+S)', style: TextStyle(fontSize: 11)),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: widget.domainColor,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 0),
                      minimumSize: const Size(60, 30),
                    ),
                  ),
                  const SizedBox(width: 6),
                  // Toggle Preview Button
                  IconButton(
                    icon: Icon(
                      _showPreview ? Icons.vertical_split : Icons.code,
                      size: 16,
                      color: _showPreview ? widget.domainColor : Colors.white60,
                    ),
                    tooltip: _showPreview ? 'Hide Preview' : 'Show Preview',
                    onPressed: () => setState(() => _showPreview = !_showPreview),
                  ),
                ],
              ),
            ),

            // Content Area (Split Editor + Markdown Preview)
            Expanded(
              child: _loading
                  ? Center(child: CircularProgressIndicator(color: widget.domainColor))
                  : Row(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        // Left: Source Editor
                        Expanded(
                          flex: 1,
                          child: Container(
                            color: const Color(0xFF0F1018),
                            padding: const EdgeInsets.all(12),
                            child: TextField(
                              controller: _contentController,
                              maxLines: null,
                              expands: true,
                              style: const TextStyle(
                                color: Color(0xFFE2E8F0),
                                fontFamily: 'Consolas, monospace',
                                fontSize: 13,
                                height: 1.5,
                              ),
                              decoration: const InputDecoration(
                                border: InputBorder.none,
                                hintText: 'Write Markdown here...',
                                hintStyle: TextStyle(color: Colors.white24),
                                isDense: true,
                              ),
                            ),
                          ),
                        ),

                        // Split divider
                        if (_showPreview) ...[
                          const VerticalDivider(width: 1, color: Color(0xFF222435)),

                          // Right: Rendered Markdown Preview
                          Expanded(
                            flex: 1,
                            child: Container(
                              color: const Color(0xFF13141E),
                              padding: const EdgeInsets.all(16),
                              child: Markdown(
                                data: _contentController.text,
                                selectable: true,
                                styleSheet: MarkdownStyleSheet.fromTheme(Theme.of(context)).copyWith(
                                  p: const TextStyle(color: Color(0xFFCBD5E1), fontSize: 13, height: 1.6),
                                  h1: const TextStyle(color: Colors.white, fontSize: 20, fontWeight: FontWeight.bold),
                                  h2: const TextStyle(color: Colors.white, fontSize: 16, fontWeight: FontWeight.bold),
                                  h3: const TextStyle(color: Colors.white, fontSize: 14, fontWeight: FontWeight.bold),
                                  code: TextStyle(
                                    backgroundColor: const Color(0xFF1E202E),
                                    color: widget.domainColor,
                                    fontFamily: 'Consolas, monospace',
                                    fontSize: 12,
                                  ),
                                  codeblockDecoration: BoxDecoration(
                                    color: const Color(0xFF0B0C12),
                                    borderRadius: BorderRadius.circular(6),
                                    border: Border.all(color: const Color(0xFF222435)),
                                  ),
                                  blockquoteDecoration: BoxDecoration(
                                    border: Border(left: BorderSide(color: widget.domainColor, width: 3)),
                                  ),
                                  tableBorder: TableBorder.all(color: const Color(0xFF2A2D40), width: 1),
                                  tableHead: const TextStyle(fontWeight: FontWeight.bold, color: Colors.white),
                                  tableBody: const TextStyle(color: Color(0xFFCBD5E1)),
                                ),
                              ),
                            ),
                          ),
                        ],
                      ],
                    ),
            ),
          ],
        ),
      ),
    );
  }
}
