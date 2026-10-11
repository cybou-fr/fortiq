import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;
import 'package:file_selector/file_selector.dart';

import 'mono.dart';
import '../models/linux_models.dart';
import '../services/ssh_service.dart';
import '../services/desktop_task_controller.dart';
import '../services/sftp_transfer_service.dart';
import 'ui_errors.dart';

class RemoteFileExplorer extends StatefulWidget {
  final SshClientService sshService;
  final String initialPath;
  final Color domainColor;
  final void Function(String filePath)? onOpenFile;
  final ValueChanged<String>? onPathChanged;
  final DesktopTaskController? taskController;

  const RemoteFileExplorer({
    super.key,
    required this.sshService,
    this.initialPath = '/',
    required this.domainColor,
    this.onOpenFile,
    this.onPathChanged,
    this.taskController,
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
  late final DesktopTaskController _tasks =
      widget.taskController ?? DesktopTaskController();
  String? _error;
  final TextEditingController _pathController = TextEditingController();
  final List<String> _history = [];
  int _historyIndex = -1;
  int _loadSerial = 0;
  String _filter = '';
  String _sort = 'name';
  bool _showHidden = true;

  @override
  void initState() {
    super.initState();
    _currentPath = widget.initialPath;
    _pathController.text = _currentPath;
    _loadDirectory(_currentPath);
  }

  @override
  void dispose() {
    _loadSerial++;
    _pathController.dispose();
    if (widget.taskController == null) _tasks.dispose();
    super.dispose();
  }

  List<LinuxFileInfo> get _visibleFiles {
    final files = _files
        .where(
          (f) =>
              (_showHidden || !f.name.startsWith('.')) &&
              f.name.toLowerCase().contains(_filter.toLowerCase()),
        )
        .toList();
    files.sort((a, b) {
      if (a.isDirectory != b.isDirectory) return a.isDirectory ? -1 : 1;
      final comparison = switch (_sort) {
        'size' => b.size.compareTo(a.size),
        'modified' => b.modified.compareTo(a.modified),
        _ => a.name.toLowerCase().compareTo(b.name.toLowerCase()),
      };
      return comparison == 0 ? a.name.compareTo(b.name) : comparison;
    });
    return files;
  }

  Future<void> _loadDirectory(String path, {int? historyIndex}) async {
    if (path.isEmpty) return;
    path = p.posix.normalize(
      p.posix.isAbsolute(path) ? path : p.posix.join(_currentPath, path),
    );
    final serial = ++_loadSerial;
    final client = widget.sshService.rawClient;
    setState(() {
      _isLoading = true;
      _error = null;
    });

    try {
      final list = await widget.sshService.files.listDirectory(path);
      if (!mounted || serial != _loadSerial) return;
      if (widget.sshService.rawClient != client) {
        setState(() {
          _isLoading = false;
          _error = 'SSH session changed. Refresh this directory.';
        });
        return;
      }
      setState(() {
        _currentPath = path;
        _pathController.text = path;
        _files = list;
        _isLoading = false;
        if (historyIndex != null) {
          _historyIndex = historyIndex;
        } else if (_historyIndex < 0 || _history[_historyIndex] != path) {
          _history.removeRange(_historyIndex + 1, _history.length);
          _history.add(path);
          _historyIndex = _history.length - 1;
        }
      });
      widget.onPathChanged?.call(path);
    } catch (e) {
      if (!mounted || serial != _loadSerial) return;
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
                Icon(
                  Icons.insert_drive_file,
                  color: widget.domainColor,
                  size: 20,
                ),
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
                  ? Text(
                      'Error: ${snapshot.error}',
                      style: const TextStyle(color: Colors.red),
                    )
                  : SingleChildScrollView(
                      child: SelectableText(
                        snapshot.data ?? '',
                        style: const TextStyle(
                          color: Color(0xFFCBD5E1),
                          fontFamily: kMonoFont,
                          fontFamilyFallback: kMonoFontFallback,
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
    final directory = _currentPath;
    final client = widget.sshService.rawClient;
    final controller = TextEditingController();
    final name = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: const Color(0xFF1E1F29),
        title: const Text(
          'New Folder',
          style: TextStyle(color: Colors.white, fontSize: 16),
        ),
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
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          ElevatedButton(
            onPressed: () => Navigator.pop(context, controller.text),
            style: ElevatedButton.styleFrom(
              backgroundColor: widget.domainColor,
            ),
            child: const Text('Create', style: TextStyle(color: Colors.white)),
          ),
        ],
      ),
    );

    if (!mounted) return;
    if (name != null && name.isNotEmpty) {
      if (!widget.sshService.isConnected ||
          widget.sshService.rawClient != client) {
        showActionError(
          context,
          'Create folder',
          'SSH session changed. Reopen this action after reconnecting.',
        );
        return;
      }
      if (name == '.' ||
          name == '..' ||
          name.contains('/') ||
          name.contains('\x00')) {
        showActionError(
          context,
          'Create folder',
          'Enter a folder name without path separators.',
        );
        return;
      }
      final newDirPath = directory == '/' ? '/$name' : '$directory/$name';
      try {
        await widget.sshService.files.createDirectory(newDirPath);
      } catch (e) {
        if (mounted) showActionError(context, 'Create folder', e);
      }
      if (mounted) _loadDirectory(_currentPath);
    }
  }

  Future<void> _renameItem(LinuxFileInfo item) async {
    if (_isLoading || !widget.sshService.isConnected) return;
    final directory = _currentPath;
    final client = widget.sshService.rawClient;
    final controller = TextEditingController(text: item.name);
    final name = await showDialog<String>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: Text('Rename ${item.name}'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(labelText: 'New name'),
          onSubmitted: (value) => Navigator.pop(dialogContext, value),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(dialogContext, controller.text),
            child: const Text('Rename'),
          ),
        ],
      ),
    );
    // The controller is no longer needed once the dialog has closed.
    controller.dispose();
    if (!mounted || name == null || name == item.name) return;
    if (name.isEmpty || name == '.' || name == '..' ||
        name.contains('/') || name.contains('\\x00')) {
      showActionError(context, 'Rename', 'Enter a valid file name.');
      return;
    }
    if (!widget.sshService.isConnected ||
        widget.sshService.rawClient != client || _currentPath != directory) {
      showActionError(context, 'Rename', 'SSH session or directory changed.');
      return;
    }
    final source = directory == '/' ? '/${item.name}' : '$directory/${item.name}';
    final target = directory == '/' ? '/$name' : '$directory/$name';
    setState(() => _isLoading = true);
    try {
      await widget.sshService.files.rename(source, target);
      if (mounted && widget.sshService.rawClient == client &&
          _currentPath == directory) {
        await _loadDirectory(directory);
      }
    } catch (error) {
      if (mounted) showActionError(context, 'Rename', error);
    } finally {
      if (mounted) setState(() => _isLoading = false);
    }
  }

  Future<void> _copyItem(LinuxFileInfo item) async {
    if (_isLoading || !widget.sshService.isConnected) return;
    final directory = _currentPath;
    final client = widget.sshService.rawClient;
    final source = directory == '/' ? '/${item.name}' : '$directory/${item.name}';
    final controller = TextEditingController(text: '$source.copy');
    final destination = await showDialog<String>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: Text('Copy ${item.name}'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(labelText: 'Destination path on Linux'),
          onSubmitted: (value) => Navigator.pop(dialogContext, value),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(dialogContext, controller.text),
            child: const Text('Copy'),
          ),
        ],
      ),
    );
    controller.dispose();
    if (!mounted || destination == null) return;
    final target = p.posix.normalize(destination);
    if (!p.posix.isAbsolute(destination) || destination.isEmpty ||
        target == source || destination.contains('\x00')) {
      showActionError(context, 'Copy', 'Enter a different absolute Linux path.');
      return;
    }
    if (!widget.sshService.isConnected ||
        widget.sshService.rawClient != client || _currentPath != directory) {
      showActionError(context, 'Copy', 'SSH session or directory changed.');
      return;
    }
    // Operation can continue while the user browses other directories.
    // It is not cancelled when its window is closed: SSH may still complete.
    final task = await _tasks.run(
      'Copy ${item.name}',
      () => widget.sshService.files.copy(source, target),
      isSessionCurrent: () =>
          widget.sshService.isConnected &&
          widget.sshService.rawClient == client,
    );
    if (!mounted) return;
    if (task.state == DesktopTaskState.completed &&
        _currentPath == directory) {
      await _loadDirectory(directory);
    } else if (task.state == DesktopTaskState.failed) {
      showActionError(context, 'Copy', task.message ?? 'Copy failed');
    }
  }

  Future<void> _downloadItem(LinuxFileInfo item) async {
    if (item.isDirectory || _isLoading || !widget.sshService.isConnected) return;
    final source = _currentPath == '/'
        ? '/${item.name}' : '$_currentPath/${item.name}';
    final client = widget.sshService.rawClient;
    if (client == null) return;
    final destination = await getSaveLocation(suggestedName: item.name);
    if (destination == null || !mounted) return;
    if (!widget.sshService.isConnected ||
        widget.sshService.rawClient != client) {
      showActionError(context, 'Download', 'SSH session changed.');
      return;
    }
    final cancellation = TransferCancellation();
    final task = await _tasks.runTransfer(
      'Download ${item.name}',
      (progress) => const SftpTransferService().download(
        client: client,
        remotePath: source,
        localPath: destination.path,
        onProgress: progress,
        cancellation: cancellation,
        sessionCurrent: () => widget.sshService.isConnected &&
            widget.sshService.rawClient == client,
      ),
      cancellation: cancellation,
      isSessionCurrent: () => widget.sshService.isConnected &&
          widget.sshService.rawClient == client,
    );
    if (mounted && task.state == DesktopTaskState.failed) {
      showActionError(context, 'Download', task.message ?? 'Transfer failed');
    }
  }

  Future<void> _uploadFile() async {
    if (_isLoading || !widget.sshService.isConnected) return;
    final directory = _currentPath;
    final client = widget.sshService.rawClient;
    if (client == null) return;
    final local = await openFile();
    if (local == null || !mounted) return;
    final name = p.basename(local.path);
    if (name.isEmpty || name == '.' || name == '..') return;
    if (!widget.sshService.isConnected ||
        widget.sshService.rawClient != client || _currentPath != directory) {
      showActionError(context, 'Upload', 'SSH session or directory changed.');
      return;
    }
    final remote = p.posix.join(directory, name);
    final cancellation = TransferCancellation();
    final task = await _tasks.runTransfer(
      'Upload $name',
      (progress) => const SftpTransferService().upload(
        client: client,
        localPath: local.path,
        remotePath: remote,
        onProgress: progress,
        cancellation: cancellation,
        sessionCurrent: () => widget.sshService.isConnected &&
            widget.sshService.rawClient == client,
      ),
      cancellation: cancellation,
      isSessionCurrent: () => widget.sshService.isConnected &&
          widget.sshService.rawClient == client,
    );
    if (!mounted) return;
    if (task.state == DesktopTaskState.completed &&
        _currentPath == directory) {
      await _loadDirectory(directory);
    } else if (task.state == DesktopTaskState.failed) {
      showActionError(context, 'Upload', task.message ?? 'Upload failed');
    }
  }

  Future<void> _deleteItem(LinuxFileInfo item) async {
    final directory = _currentPath;
    final client = widget.sshService.rawClient;
    final confirm = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: const Color(0xFF1E1F29),
        title: Text(
          'Delete ${item.name}?',
          style: const TextStyle(color: Colors.white, fontSize: 16),
        ),
        content: Text(
          'Command to execute: rm ${item.isDirectory ? "-rf" : ""} "${item.name}"',
          style: const TextStyle(color: Colors.white70, fontSize: 13),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancel'),
          ),
          ElevatedButton(
            onPressed: () => Navigator.pop(context, true),
            style: ElevatedButton.styleFrom(backgroundColor: Colors.red),
            child: const Text('Delete', style: TextStyle(color: Colors.white)),
          ),
        ],
      ),
    );

    if (confirm == true && mounted) {
      if (!widget.sshService.isConnected ||
          widget.sshService.rawClient != client) {
        showActionError(
          context,
          'Delete ${item.name}',
          'SSH session changed. Reopen this action after reconnecting.',
        );
        return;
      }
      final fullPath = directory == '/'
          ? '/${item.name}'
          : '$directory/${item.name}';
      try {
        await widget.sshService.files.remove(
          fullPath,
          recursive: item.isDirectory,
        );
      } catch (e) {
        if (mounted) showActionError(context, 'Delete ${item.name}', e);
      }
      if (mounted) _loadDirectory(_currentPath);
    }
  }

  @override
  Widget build(BuildContext context) {
    final visibleFiles = _visibleFiles;
    return LayoutBuilder(
      builder: (context, constraints) {
        final wide = constraints.maxWidth >= 650;
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
                    icon: const Icon(
                      Icons.chevron_left,
                      size: 18,
                      color: Colors.white70,
                    ),
                    tooltip: 'Back',
                    onPressed: !_isLoading && _historyIndex > 0
                        ? () => _loadDirectory(
                            _history[_historyIndex - 1],
                            historyIndex: _historyIndex - 1,
                          )
                        : null,
                  ),
                  IconButton(
                    icon: const Icon(
                      Icons.chevron_right,
                      size: 18,
                      color: Colors.white70,
                    ),
                    tooltip: 'Forward',
                    onPressed:
                        !_isLoading && _historyIndex + 1 < _history.length
                        ? () => _loadDirectory(
                            _history[_historyIndex + 1],
                            historyIndex: _historyIndex + 1,
                          )
                        : null,
                  ),
                  IconButton(
                    icon: const Icon(
                      Icons.arrow_upward,
                      size: 18,
                      color: Colors.white70,
                    ),
                    tooltip: 'Go to parent directory',
                    onPressed: !_isLoading && _currentPath != '/'
                        ? _navigateUp
                        : null,
                  ),
                  IconButton(
                    icon: const Icon(
                      Icons.refresh,
                      size: 18,
                      color: Colors.white70,
                    ),
                    tooltip: 'Refresh',
                    onPressed: _isLoading
                        ? null
                        : () => _loadDirectory(_currentPath),
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
                          const Icon(
                            Icons.terminal,
                            size: 16,
                            color: Colors.white38,
                          ),
                          const SizedBox(width: 8),
                          Expanded(
                            child: TextField(
                              controller: _pathController,
                              style: const TextStyle(
                                color: Colors.white,
                                fontSize: 13,
                              ),
                              decoration: const InputDecoration(
                                border: InputBorder.none,
                                isDense: true,
                                contentPadding: EdgeInsets.zero,
                              ),
                              onSubmitted: (val) => _loadDirectory(val),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  IconButton(
                    icon: const Icon(Icons.upload_file,
                        size: 18, color: Colors.white70),
                    tooltip: 'Upload file via SFTP',
                    onPressed: _isLoading ? null : _uploadFile,
                  ),
                  IconButton(
                    icon: const Icon(
                      Icons.create_new_folder,
                      size: 18,
                      color: Colors.white70,
                    ),
                    tooltip: 'New Folder (mkdir)',
                    onPressed: _isLoading ? null : _showNewFolderDialog,
                  ),
                ],
              ),
            ),

            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
              child: Row(
                children: [
                  Expanded(
                    child: TextField(
                      style: const TextStyle(color: Colors.white, fontSize: 12),
                      decoration: const InputDecoration(
                        isDense: true,
                        hintText: 'Filter files',
                        hintStyle: TextStyle(color: Colors.white38),
                        prefixIcon: Icon(Icons.search, size: 16),
                      ),
                      onChanged: (value) => setState(() => _filter = value),
                    ),
                  ),
                  PopupMenuButton<String>(
                    tooltip: 'Sort files',
                    icon: const Icon(
                      Icons.sort,
                      size: 18,
                      color: Colors.white70,
                    ),
                    onSelected: (value) => setState(() => _sort = value),
                    itemBuilder: (_) => [
                      for (final entry in {
                        'name': 'Name',
                        'size': 'Size (largest first)',
                        'modified': 'Modified (newest first)',
                      }.entries)
                        CheckedPopupMenuItem(
                          value: entry.key,
                          checked: _sort == entry.key,
                          child: Text(entry.value),
                        ),
                    ],
                  ),
                  IconButton(
                    tooltip: _showHidden
                        ? 'Hide hidden files'
                        : 'Show hidden files',
                    icon: Icon(
                      _showHidden ? Icons.visibility : Icons.visibility_off,
                      size: 18,
                      color: Colors.white70,
                    ),
                    onPressed: () => setState(() => _showHidden = !_showHidden),
                  ),
                ],
              ),
            ),

            // File List Content
            Expanded(
              child: _isLoading
                  ? Center(
                      child: CircularProgressIndicator(
                        color: widget.domainColor,
                      ),
                    )
                  : _error != null
                  ? Center(
                      child: Text(
                        _error!,
                        style: const TextStyle(
                          color: Colors.redAccent,
                          fontSize: 13,
                        ),
                      ),
                    )
                  : visibleFiles.isEmpty
                  ? Center(
                      child: Text(
                        _files.isEmpty
                            ? 'This directory is empty'
                            : 'No matching files',
                        style: const TextStyle(color: Colors.white54),
                      ),
                    )
                  : ListView.builder(
                      itemCount: visibleFiles.length,
                      itemBuilder: (context, index) {
                        final file = visibleFiles[index];
                        return InkWell(
                          onDoubleTap: () => _openItem(file),
                          hoverColor: Colors.white.withValues(alpha: 0.04),
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 14,
                              vertical: 8,
                            ),
                            decoration: const BoxDecoration(
                              border: Border(
                                bottom: BorderSide(color: Color(0xFF1C1D26)),
                              ),
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
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                    style: TextStyle(
                                      color: Colors.white,
                                      fontSize: 13,
                                      fontWeight: file.isDirectory
                                          ? FontWeight.w600
                                          : FontWeight.normal,
                                    ),
                                  ),
                                ),
                                SizedBox(
                                  width: 80,
                                  child: Text(
                                    file.formattedSize,
                                    textAlign: TextAlign.right,
                                    style: const TextStyle(
                                      color: Colors.white38,
                                      fontSize: 11,
                                    ),
                                  ),
                                ),
                                if (wide) ...[
                                  const SizedBox(width: 20),
                                  SizedBox(
                                    width: 90,
                                    child: Text(
                                      file.permissions,
                                      style: const TextStyle(
                                        color: Colors.white38,
                                        fontFamily: kMonoFont,
                                        fontFamilyFallback: kMonoFontFallback,
                                        fontSize: 11,
                                      ),
                                    ),
                                  ),
                                  const SizedBox(width: 20),
                                  SizedBox(
                                    width: 80,
                                    child: Text(
                                      '${file.owner}:${file.group}',
                                      maxLines: 1,
                                      overflow: TextOverflow.ellipsis,
                                      style: const TextStyle(
                                        color: Colors.white38,
                                        fontSize: 11,
                                      ),
                                    ),
                                  ),
                                ],
                                if (!file.isDirectory)
                                  IconButton(
                                    icon: const Icon(Icons.download_outlined,
                                        size: 16, color: Colors.white60),
                                    tooltip: 'Download via SFTP',
                                    onPressed: _isLoading ? null : () => _downloadItem(file),
                                  ),
                                IconButton(
                                  icon: const Icon(Icons.copy_outlined,
                                      size: 16, color: Colors.white60),
                                  tooltip: 'Copy on Linux',
                                  onPressed: _isLoading ? null : () => _copyItem(file),
                                ),
                                IconButton(
                                  icon: const Icon(Icons.drive_file_rename_outline,
                                      size: 16, color: Colors.white60),
                                  tooltip: 'Rename',
                                  onPressed: _isLoading ? null : () => _renameItem(file),
                                ),
                                IconButton(
                                  icon: const Icon(
                                    Icons.delete_outline,
                                    size: 16,
                                    color: Colors.white38,
                                  ),
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

            AnimatedBuilder(
              animation: _tasks,
              builder: (context, _) {
                if (_tasks.tasks.isEmpty) return const SizedBox.shrink();
                return ExpansionTile(
                  initiallyExpanded: true,
                  title: Text('File tasks · ${_tasks.runningCount} running',
                      style: const TextStyle(fontSize: 12)),
                  trailing: _tasks.runningCount > 0
                      ? const SizedBox(width: 15, height: 15,
                          child: CircularProgressIndicator(strokeWidth: 2))
                      : IconButton(
                          tooltip: 'Clear finished tasks',
                          icon: const Icon(Icons.clear_all, size: 18),
                          onPressed: _tasks.clearFinished,
                        ),
                  children: [
                    for (final task in _tasks.tasks.take(5))
                      ListTile(
                        dense: true,
                        title: Text(task.label,
                            style: const TextStyle(fontSize: 12)),
                        subtitle: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(task.message ?? task.state.name,
                                maxLines: 2,
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(fontSize: 11)),
                            if (task.totalBytes != null) ...[
                              const SizedBox(height: 4),
                              LinearProgressIndicator(
                                  value: task.progress, minHeight: 3),
                              Text('${task.bytesTransferred} / ${task.totalBytes} bytes',
                                  style: const TextStyle(fontSize: 10)),
                            ] else if (task.bytesTransferred > 0)
                              Text('${task.bytesTransferred} bytes',
                                  style: const TextStyle(fontSize: 10)),
                          ],
                        ),
                        trailing: task.state == DesktopTaskState.running &&
                                task.cancellation != null
                            ? IconButton(
                                tooltip: 'Request upload cancellation',
                                icon: const Icon(Icons.cancel_outlined, size: 18),
                                onPressed: task.cancellation!.isRequested
                                    ? null
                                    : () => _tasks.cancelTransfer(task.id),
                              )
                            : null,
                        leading: Icon(
                          task.state == DesktopTaskState.completed
                              ? Icons.check_circle_outline
                              : task.state == DesktopTaskState.failed
                                  ? Icons.error_outline
                                  : task.state == DesktopTaskState.unknown
                                      ? Icons.help_outline
                                      : Icons.hourglass_top,
                          size: 17,
                        ),
                      ),
                  ],
                );
              },
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
                    '${visibleFiles.length} / ${_files.length} items',
                    style: const TextStyle(color: Colors.white38, fontSize: 11),
                  ),
                  const Spacer(),
                  if (wide)
                    const Text(
                      'Server files · SSH',
                      style: TextStyle(color: Colors.white24, fontSize: 11),
                    ),
                ],
              ),
            ),
          ],
        );
      },
    );
  }
}
