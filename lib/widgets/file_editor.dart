import 'package:flutter/material.dart';
import 'mono.dart';
import 'package:flutter/services.dart';
import '../services/ssh_service.dart';

class RemoteFileEditor extends StatefulWidget {
  final SshClientService sshService;
  final String filePath;
  final Color domainColor;
  final VoidCallback? onSaved;
  final ValueChanged<bool>? onDirtyChanged;

  const RemoteFileEditor({
    super.key,
    required this.sshService,
    required this.filePath,
    required this.domainColor,
    this.onSaved,
    this.onDirtyChanged,
  });

  @override
  State<RemoteFileEditor> createState() => _RemoteFileEditorState();
}

class _RemoteFileEditorState extends State<RemoteFileEditor> {

  // Async loads may finish after the window was closed.
  @override
  void setState(VoidCallback fn) {
    if (mounted) super.setState(fn);
  }
  final TextEditingController _contentController = TextEditingController();
  bool _loading = true;
  bool _saving = false;
  bool _isDirty = false;
  String? _error;
  int _lineCount = 1;
  int _currentLine = 1;

  @override
  void initState() {
    super.initState();
    _loadFile();
    _contentController.addListener(_onTextChanged);
  }

  @override
  void dispose() {
    _contentController.removeListener(_onTextChanged);
    _contentController.dispose();
    super.dispose();
  }

  void _onTextChanged() {
    final text = _contentController.text;
    final lines = text.split('\n').length;
    if (lines != _lineCount) {
      setState(() => _lineCount = lines);
    }
    if (!_isDirty) {
      setState(() => _isDirty = true);
      widget.onDirtyChanged?.call(true);
    }
  }

  Future<void> _reload() async {
    if (_isDirty) {
      final discard = await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
          backgroundColor: const Color(0xFF1E1F29),
          title: const Text('Discard changes?', style: TextStyle(color: Colors.white, fontSize: 16)),
          content: const Text(
            'Reloading replaces your unsaved edits with the file from the server.',
            style: TextStyle(color: Colors.white70, fontSize: 13),
          ),
          actions: [
            TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('Cancel')),
            ElevatedButton(
              onPressed: () => Navigator.pop(context, true),
              style: ElevatedButton.styleFrom(backgroundColor: Colors.red),
              child: const Text('Reload', style: TextStyle(color: Colors.white)),
            ),
          ],
        ),
      );
      if (discard != true) return;
    }
    await _loadFile();
  }

  Future<void> _loadFile() async {
    setState(() {
      _loading = true;
      _error = null;
    });

    try {
      final content = await widget.sshService.files.readFile(widget.filePath);
      if (!mounted) return; // window closed while loading: controller is gone
      _contentController.text = content;
      _lineCount = content.split('\n').length;
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
    if (_saving) return;
    setState(() => _saving = true);
    // The user may keep typing while the upload runs: only the text that was
    // actually sent counts as saved.
    final sent = _contentController.text;
    try {
      final atomic = await widget.sshService.files.writeFile(widget.filePath, sent);
      final unchangedSinceSend = mounted && _contentController.text == sent;
      setState(() {
        _saving = false;
        if (unchangedSinceSend) _isDirty = false;
      });
      if (unchangedSinceSend) widget.onDirtyChanged?.call(false);
      widget.onSaved?.call();
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(atomic
                ? 'Saved: ${widget.filePath}'
                : 'Saved in place (directory is read-only, not atomic): ${widget.filePath}'),
            backgroundColor: widget.domainColor,
            duration: const Duration(seconds: 2),
          ),
        );
      }
    } catch (e) {
      setState(() => _saving = false);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Save error: $e'),
            backgroundColor: Colors.redAccent,
          ),
        );
      }
    }
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
            // Editor Toolbar
            Container(
              height: 38,
              padding: const EdgeInsets.symmetric(horizontal: 10),
              decoration: const BoxDecoration(
                color: Color(0xFF181924),
                border: Border(bottom: BorderSide(color: Color(0xFF262838))),
              ),
              child: Row(
                children: [
                  Icon(Icons.description, size: 16, color: widget.domainColor),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Row(
                      children: [
                        Flexible(
                          child: Text(
                            widget.filePath,
                            style: const TextStyle(
                              color: Colors.white,
                              fontSize: 12,
                              fontWeight: FontWeight.w600,
                              fontFamily: kMonoFont, fontFamilyFallback: kMonoFontFallback,
                            ),
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                        if (_isDirty) ...[
                          const SizedBox(width: 6),
                          Container(
                            width: 6,
                            height: 6,
                            decoration: const BoxDecoration(
                              color: Colors.amberAccent,
                              shape: BoxShape.circle,
                            ),
                          ),
                        ],
                      ],
                    ),
                  ),
                  TextButton.icon(
                    onPressed: _saving || _loading ? null : _saveFile,
                    icon: _saving
                        ? const SizedBox(
                            width: 12,
                            height: 12,
                            child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white),
                          )
                        : const Icon(Icons.save, size: 14),
                    label: const Text('Save (Ctrl+S)', style: TextStyle(fontSize: 11)),
                    style: TextButton.styleFrom(
                      foregroundColor: _isDirty ? Colors.amberAccent : Colors.white70,
                      padding: const EdgeInsets.symmetric(horizontal: 10),
                    ),
                  ),
                  IconButton(
                    icon: const Icon(Icons.refresh, size: 16, color: Colors.white60),
                    tooltip: 'Reload',
                    onPressed: _reload,
                  ),
                ],
              ),
            ),

            // Editor Area
            Expanded(
              child: _loading
                  ? Center(child: CircularProgressIndicator(color: widget.domainColor))
                  : _error != null
                      ? Center(
                          child: Text('Error: $_error', style: const TextStyle(color: Colors.redAccent)),
                        )
                      : Container(
                          color: const Color(0xFF0F1018),
                          child: Row(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              // Line numbers gutter
                              Container(
                                width: 44,
                                padding: const EdgeInsets.symmetric(vertical: 8),
                                color: const Color(0xFF13141E),
                                child: ListView.builder(
                                  itemCount: _lineCount,
                                  itemBuilder: (context, idx) {
                                    return Text(
                                      '${idx + 1}',
                                      textAlign: TextAlign.right,
                                      style: TextStyle(
                                        color: Colors.white.withValues(alpha: 0.25),
                                        fontFamily: kMonoFont, fontFamilyFallback: kMonoFontFallback,
                                        fontSize: 12,
                                        height: 1.4,
                                      ),
                                    );
                                  },
                                ),
                              ),
                              const VerticalDivider(width: 1, color: Color(0xFF222435)),

                              // Code Text Field
                              Expanded(
                                child: Padding(
                                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                                  child: TextField(
                                    controller: _contentController,
                                    maxLines: null,
                                    expands: true,
                                    style: const TextStyle(
                                      color: Color(0xFFE2E8F0),
                                      fontFamily: kMonoFont, fontFamilyFallback: kMonoFontFallback,
                                      fontSize: 12.5,
                                      height: 1.4,
                                    ),
                                    decoration: const InputDecoration(
                                      border: InputBorder.none,
                                      isDense: true,
                                      contentPadding: EdgeInsets.zero,
                                    ),
                                  ),
                                ),
                              ),
                            ],
                          ),
                        ),
            ),

            // Editor status bar
            Container(
              height: 22,
              padding: const EdgeInsets.symmetric(horizontal: 12),
              decoration: const BoxDecoration(
                color: Color(0xFF141520),
                border: Border(top: BorderSide(color: Color(0xFF222435))),
              ),
              child: Row(
                children: [
                  Text(
                    '$_lineCount lines',
                    style: const TextStyle(color: Colors.white38, fontSize: 10),
                  ),
                  const Spacer(),
                  const Text(
                    'UTF-8 | LF | Linux Stream Editor',
                    style: TextStyle(color: Colors.white38, fontSize: 10),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
