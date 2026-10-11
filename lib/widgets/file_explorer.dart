import 'package:flutter/material.dart';
import '../models/linux_models.dart';
import '../services/ssh_service.dart';
import 'ui_errors.dart';

class RemoteFileExplorer extends StatefulWidget {
  final SshClientService sshService;
  final String initialPath;
  final Color domainColor;
  final void Function(String filePath)? onOpenFile;

  const RemoteFileExplorer({
    super.key,
    required this.sshService,
    this.initialPath = '/',
    required this.domainColor,
    this.onOpenFile,
  });

  @override
  State<RemoteFileExplorer> createState() => _RemoteFileExplorerState();
}

class _RemoteFileExplorerState extends State<RemoteFileExplorer> {

  // Async loads may finish after the window was closed.
  @override
  void setState(VoidCallback fn) {
    if (mounted) super.setState(fn);
  }
  late String _currentPath;
  List<LinuxFileInfo> _files = [];
  bool _isLoading = false;
  String? _error;
  final TextEditingController _pathController = TextEditingController();

  @override
  void initState() {
    super.initState();
    _currentPath = widget.initialPath;
    _pathController.text = _currentPath;
    _loadDirectory(_currentPath);
  }

  Future<void> _loadDirectory(String path) async {
    setState(() {
      _isLoading = true;
      _error = null;
    });

    try {
      final list = await widget.sshService.files.listDirectory(path);
      setState(() {
        _currentPath = path;
        _pathController.text = path;
        _files = list;
        _isLoading = false;
      });
    } catch (e) {
      setState(() {
        _error = e.toString();
        _isLoading = false;
      });
    }
  }

  void _navigateUp() {
    if (_currentPath == '/') return;
    final parts = _currentPath.split('/').where((p) => p.isNotEmpty).toList();
    if (parts.isEmpty) {
      _loadDirectory('/');
    } else {
      parts.removeLast();
      final newPath = parts.isEmpty ? '/' : '/${parts.join('/')}';
      _loadDirectory(newPath);
    }
  }

  void _openItem(LinuxFileInfo item) {
    final filePath = _currentPath == '/' 
        ? '/${item.name}' 
        : '$_currentPath/${item.name}';

    if (item.isDirectory) {
      _loadDirectory(filePath);
    } else {
      if (widget.onOpenFile != null) {
        widget.onOpenFile!(filePath);
      } else {
        _viewFileContent(item);
      }
    }
  }

  Future<void> _viewFileContent(LinuxFileInfo file) async {
    final filePath = _currentPath == '/' 
        ? '/${file.name}' 
        : '$_currentPath/${file.name}';

    showDialog(
      context: context,
      builder: (context) => FutureBuilder<String>(
        future: widget.sshService.files.readFile(filePath),
        builder: (context, snapshot) {
          return AlertDialog(
            backgroundColor: const Color(0xFF1E1F29),
            title: Row(
              children: [
                Icon(Icons.insert_drive_file, color: widget.domainColor, size: 20),
                const SizedBox(width: 8),
                Text(
                  file.name,
                  style: const TextStyle(color: Colors.white, fontSize: 14),
                ),
              ],
            ),
            content: SizedBox(
              width: 600,
              height: 400,
              child: snapshot.connectionState == ConnectionState.waiting
                  ? const Center(child: CircularProgressIndicator())
                  : snapshot.hasError
                      ? Text('Error: ${snapshot.error}', style: const TextStyle(color: Colors.red))
                      : SingleChildScrollView(
                          child: SelectableText(
                            snapshot.data ?? '',
                            style: const TextStyle(
                              color: Color(0xFFCBD5E1),
                              fontFamily: 'Consolas, monospace',
                              fontSize: 12,
                            ),
                          ),
                        ),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(context),
                child: const Text('Close'),
              ),
            ],
          );
        },
      ),
    );
  }

  Future<void> _showNewFolderDialog() async {
    final controller = TextEditingController();
    final name = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: const Color(0xFF1E1F29),
        title: const Text('New Folder', style: TextStyle(color: Colors.white, fontSize: 16)),
        content: TextField(
          controller: controller,
          autofocus: true,
          style: const TextStyle(color: Colors.white),
          decoration: const InputDecoration(
            hintText: 'folder_name',
            hintStyle: TextStyle(color: Colors.white38),
          ),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(context), child: const Text('Cancel')),
          ElevatedButton(
            onPressed: () => Navigator.pop(context, controller.text.trim()),
            style: ElevatedButton.styleFrom(backgroundColor: widget.domainColor),
            child: const Text('Create', style: TextStyle(color: Colors.white)),
          ),
        ],
      ),
    );

    if (name != null && name.isNotEmpty) {
      final newDirPath = _currentPath == '/' ? '/$name' : '$_currentPath/$name';
      try {
        await widget.sshService.files.createDirectory(newDirPath);
      } catch (e) {
        showActionError(context, 'Create folder', e);
      }
      _loadDirectory(_currentPath);
    }
  }

  Future<void> _deleteItem(LinuxFileInfo item) async {
    final confirm = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: const Color(0xFF1E1F29),
        title: Text('Delete ${item.name}?', style: const TextStyle(color: Colors.white, fontSize: 16)),
        content: Text(
          'Command to execute: rm ${item.isDirectory ? "-rf" : ""} "${item.name}"',
          style: const TextStyle(color: Colors.white70, fontSize: 13),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('Cancel')),
          ElevatedButton(
            onPressed: () => Navigator.pop(context, true),
            style: ElevatedButton.styleFrom(backgroundColor: Colors.red),
            child: const Text('Delete', style: TextStyle(color: Colors.white)),
          ),
        ],
      ),
    );

    if (confirm == true) {
      final fullPath = _currentPath == '/' ? '/${item.name}' : '$_currentPath/${item.name}';
      try {
        await widget.sshService.files.remove(fullPath, recursive: item.isDirectory);
      } catch (e) {
        showActionError(context, 'Delete ${item.name}', e);
      }
      _loadDirectory(_currentPath);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        // Toolbar (Path & Actions)
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
          decoration: const BoxDecoration(
            color: Color(0xFF181922),
            border: Border(bottom: BorderSide(color: Color(0xFF262837))),
          ),
          child: Row(
            children: [
              IconButton(
                icon: const Icon(Icons.arrow_upward, size: 18, color: Colors.white70),
                tooltip: 'Go to parent directory',
                onPressed: _navigateUp,
              ),
              IconButton(
                icon: const Icon(Icons.refresh, size: 18, color: Colors.white70),
                tooltip: 'Refresh',
                onPressed: () => _loadDirectory(_currentPath),
              ),
              const SizedBox(width: 8),
              // Path input field
              Expanded(
                child: Container(
                  height: 34,
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  decoration: BoxDecoration(
                    color: const Color(0xFF111218),
                    borderRadius: BorderRadius.circular(6),
                    border: Border.all(color: const Color(0xFF2C2E40)),
                  ),
                  child: Row(
                    children: [
                      const Icon(Icons.terminal, size: 16, color: Colors.white38),
                      const SizedBox(width: 8),
                      Expanded(
                        child: TextField(
                          controller: _pathController,
                          style: const TextStyle(color: Colors.white, fontSize: 13),
                          decoration: const InputDecoration(
                            border: InputBorder.none,
                            isDense: true,
                            contentPadding: EdgeInsets.zero,
                          ),
                          onSubmitted: (val) => _loadDirectory(val.trim()),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(width: 8),
              IconButton(
                icon: const Icon(Icons.create_new_folder, size: 18, color: Colors.white70),
                tooltip: 'New Folder (mkdir)',
                onPressed: _showNewFolderDialog,
              ),
            ],
          ),
        ),

        // File List Content
        Expanded(
          child: _isLoading
              ? Center(
                  child: CircularProgressIndicator(color: widget.domainColor),
                )
              : _error != null
                  ? Center(
                      child: Text(
                        _error!,
                        style: const TextStyle(color: Colors.redAccent, fontSize: 13),
                      ),
                    )
                  : ListView.builder(
                      itemCount: _files.length,
                      itemBuilder: (context, index) {
                        final file = _files[index];
                        return InkWell(
                          onDoubleTap: () => _openItem(file),
                          hoverColor: Colors.white.withOpacity(0.04),
                          child: Container(
                            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                            decoration: const BoxDecoration(
                              border: Border(bottom: BorderSide(color: Color(0xFF1C1D26))),
                            ),
                            child: Row(
                              children: [
                                Icon(
                                  file.isDirectory
                                      ? Icons.folder
                                      : file.isLink
                                          ? Icons.link
                                          : Icons.insert_drive_file,
                                  color: file.isDirectory
                                      ? widget.domainColor
                                      : const Color(0xFF94A3B8),
                                  size: 18,
                                ),
                                const SizedBox(width: 12),
                                Expanded(
                                  child: Text(
                                    file.name,
                                    style: TextStyle(
                                      color: Colors.white,
                                      fontSize: 13,
                                      fontWeight: file.isDirectory ? FontWeight.w600 : FontWeight.normal,
                                    ),
                                  ),
                                ),
                                SizedBox(
                                  width: 80,
                                  child: Text(
                                    file.formattedSize,
                                    textAlign: TextAlign.right,
                                    style: const TextStyle(color: Colors.white38, fontSize: 11),
                                  ),
                                ),
                                const SizedBox(width: 20),
                                SizedBox(
                                  width: 90,
                                  child: Text(
                                    file.permissions,
                                    style: const TextStyle(
                                      color: Colors.white38,
                                      fontFamily: 'Consolas, monospace',
                                      fontSize: 11,
                                    ),
                                  ),
                                ),
                                const SizedBox(width: 20),
                                SizedBox(
                                  width: 80,
                                  child: Text(
                                    '${file.owner}:${file.group}',
                                    style: const TextStyle(color: Colors.white38, fontSize: 11),
                                  ),
                                ),
                                IconButton(
                                  icon: const Icon(Icons.delete_outline, size: 16, color: Colors.white38),
                                  tooltip: 'Delete (rm)',
                                  onPressed: () => _deleteItem(file),
                                ),
                              ],
                            ),
                          ),
                        );
                      },
                    ),
        ),

        // Status bar
        Container(
          height: 26,
          padding: const EdgeInsets.symmetric(horizontal: 12),
          decoration: const BoxDecoration(
            color: Color(0xFF15161E),
            border: Border(top: BorderSide(color: Color(0xFF222432))),
          ),
          child: Row(
            children: [
              Text(
                '${_files.length} items',
                style: const TextStyle(color: Colors.white38, fontSize: 11),
              ),
              const Spacer(),
              const Text(
                'Powered by Linux ls & stat over SSH',
                style: TextStyle(color: Colors.white24, fontSize: 11),
              ),
            ],
          ),
        ),
      ],
    );
  }
}
