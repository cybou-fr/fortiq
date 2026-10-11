import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'mono.dart';
import '../services/ssh_service.dart';
import 'remote_snapshot.dart';

class RemoteLogViewer extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;

  const RemoteLogViewer({
    super.key,
    required this.sshService,
    required this.domainColor,
  });

  @override
  State<RemoteLogViewer> createState() => _RemoteLogViewerState();
}

class _RemoteLogViewerState extends State<RemoteLogViewer> {
  // Async loads may finish after the window was closed.
  @override
  void setState(VoidCallback fn) {
    if (mounted) super.setState(fn);
  }

  String _selectedLogType = 'journal'; // 'journal', 'auth', 'syslog', 'dmesg'
  String _selectedSeverity = 'ALL'; // 'ALL', 'ERROR', 'WARN', 'INFO'
  int _linesCount = 100;
  bool get _autoRefresh => _snapshot.live;
  late final _snapshot = RemoteSnapshotController(
    widget.sshService,
    load: () => widget.sshService.system.fetchSystemLogs(
      logType: _selectedLogType,
      linesCount: _linesCount,
    ),
  );
  final TextEditingController _filterController = TextEditingController();

  bool get _isLoading => _snapshot.loading && _snapshot.data == null;
  List<Map<String, dynamic>> get _logs => _snapshot.data ?? [];

  // Последняя ошибка загрузки: журнал не должен молча выглядеть пустым.
  Object? get _error => _snapshot.error;
  final ScrollController _scrollController = ScrollController();

  final List<Map<String, String>> _logSources = [
    {
      'id': 'journal',
      'title': 'System Journal (journalctl)',
      'icon': 'event_note',
    },
    {'id': 'auth', 'title': 'Auth & SSH Logins (auth.log)', 'icon': 'security'},
    {'id': 'syslog', 'title': 'Syslog (/var/log/syslog)', 'icon': 'article'},
    {'id': 'dmesg', 'title': 'Kernel / Hardware (dmesg)', 'icon': 'memory'},
  ];

  @override
  void initState() {
    super.initState();
    _snapshot.addListener(_snapshotChanged);
    _loadLogs();
  }

  @override
  void dispose() {
    _snapshot.removeListener(_snapshotChanged);
    _snapshot.dispose();
    _filterController.dispose();
    _scrollController.dispose();
    super.dispose();
  }

  void _toggleAutoRefresh(bool val) {
    _snapshot.setLive(val);
  }

  Future<void> _loadLogs() => _snapshot.refresh();

  void _snapshotChanged() {
    if (!mounted) return;
    final follow =
        _autoRefresh &&
        !_snapshot.loading &&
        (!_scrollController.hasClients ||
            _scrollController.position.extentAfter < 24);
    setState(() {});
    if (follow) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) _scrollToBottom();
      });
    }
  }

  void _scrollToBottom() {
    if (_scrollController.hasClients) {
      _scrollController.animateTo(
        _scrollController.position.maxScrollExtent,
        duration: const Duration(milliseconds: 300),
        curve: Curves.easeOut,
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final filterText = _filterController.text.trim().toLowerCase();
    final filteredLogs = _logs.where((l) {
      final level = (l['level'] as String? ?? 'INFO').toUpperCase();
      final text = (l['text'] as String? ?? '').toLowerCase();

      // Severity check
      if (_selectedSeverity != 'ALL' && level != _selectedSeverity) {
        return false;
      }

      // Keyword text filter
      if (filterText.isNotEmpty && !text.contains(filterText)) {
        return false;
      }

      return true;
    }).toList();

    return Container(
      color: const Color(0xFF13141F),
      child: Column(
        children: [
          // Top Control Toolbar
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
            decoration: BoxDecoration(
              color: const Color(0xFF1B1C28),
              border: Border(
                bottom: BorderSide(
                  color: widget.domainColor.withValues(alpha: 0.15),
                ),
              ),
            ),
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: Wrap(
                spacing: 12,
                runSpacing: 8,
                crossAxisAlignment: WrapCrossAlignment.center,
                children: [
                  // Log Source Dropdown
                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 10,
                      vertical: 2,
                    ),
                    decoration: BoxDecoration(
                      color: const Color(0xFF222334),
                      borderRadius: BorderRadius.circular(6),
                      border: Border.all(color: Colors.white10),
                    ),
                    child: DropdownButtonHideUnderline(
                      child: DropdownButton<String>(
                        value: _selectedLogType,
                        dropdownColor: const Color(0xFF222334),
                        style: const TextStyle(
                          fontSize: 12,
                          color: Colors.white,
                          fontWeight: FontWeight.bold,
                        ),
                        items: _logSources.map((s) {
                          return DropdownMenuItem<String>(
                            value: s['id'],
                            child: Row(
                              children: [
                                Icon(
                                  s['id'] == 'auth'
                                      ? Icons.security
                                      : s['id'] == 'dmesg'
                                      ? Icons.memory
                                      : s['id'] == 'syslog'
                                      ? Icons.article
                                      : Icons.event_note,
                                  size: 14,
                                  color: widget.domainColor,
                                ),
                                const SizedBox(width: 8),
                                Text(s['title']!),
                              ],
                            ),
                          );
                        }).toList(),
                        onChanged: (val) {
                          if (val != null) {
                            setState(() => _selectedLogType = val);
                            _snapshot.invalidate(clear: true);
                            _loadLogs();
                          }
                        },
                      ),
                    ),
                  ),

                  // Severity Filter Chips
                  Wrap(
                    spacing: 4,
                    children: [
                      _buildSeverityFilter('ALL', Colors.grey),
                      _buildSeverityFilter('ERROR', Colors.redAccent),
                      _buildSeverityFilter('WARN', Colors.orangeAccent),
                      _buildSeverityFilter('INFO', Colors.cyanAccent),
                    ],
                  ),

                  // Filter Text Field
                  SizedBox(
                    width: 140,
                    height: 30,
                    child: TextField(
                      controller: _filterController,
                      onChanged: (_) => setState(() {}),
                      style: const TextStyle(fontSize: 11, color: Colors.white),
                      decoration: InputDecoration(
                        hintText: 'Filter log text...',
                        hintStyle: const TextStyle(
                          fontSize: 11,
                          color: Colors.white30,
                        ),
                        prefixIcon: const Icon(
                          Icons.search,
                          size: 14,
                          color: Colors.white54,
                        ),
                        isDense: true,
                        contentPadding: const EdgeInsets.symmetric(
                          vertical: 6,
                          horizontal: 8,
                        ),
                        filled: true,
                        fillColor: const Color(0xFF222334),
                        border: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(6),
                          borderSide: BorderSide.none,
                        ),
                      ),
                    ),
                  ),

                  // Lines Count Selector
                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 8,
                      vertical: 2,
                    ),
                    decoration: BoxDecoration(
                      color: const Color(0xFF222334),
                      borderRadius: BorderRadius.circular(6),
                    ),
                    child: DropdownButtonHideUnderline(
                      child: DropdownButton<int>(
                        value: _linesCount,
                        dropdownColor: const Color(0xFF222334),
                        style: const TextStyle(
                          fontSize: 11,
                          color: Colors.white70,
                        ),
                        items: const [
                          DropdownMenuItem(value: 50, child: Text('50 lines')),
                          DropdownMenuItem(
                            value: 100,
                            child: Text('100 lines'),
                          ),
                          DropdownMenuItem(
                            value: 200,
                            child: Text('200 lines'),
                          ),
                          DropdownMenuItem(
                            value: 500,
                            child: Text('500 lines'),
                          ),
                        ],
                        onChanged: (val) {
                          if (val != null) {
                            setState(() => _linesCount = val);
                            _snapshot.invalidate(clear: true);
                            _loadLogs();
                          }
                        },
                      ),
                    ),
                  ),

                  // Auto Refresh Switch
                  InkWell(
                    onTap: () => _toggleAutoRefresh(!_autoRefresh),
                    borderRadius: BorderRadius.circular(6),
                    child: Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 8,
                        vertical: 6,
                      ),
                      decoration: BoxDecoration(
                        color: _autoRefresh
                            ? widget.domainColor.withValues(alpha: 0.15)
                            : const Color(0xFF222334),
                        borderRadius: BorderRadius.circular(6),
                        border: Border.all(
                          color: _autoRefresh
                              ? widget.domainColor
                              : Colors.transparent,
                          width: 1,
                        ),
                      ),
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(
                            Icons.sync,
                            size: 14,
                            color: _autoRefresh
                                ? widget.domainColor
                                : Colors.white54,
                          ),
                          const SizedBox(width: 6),
                          Text(
                            'Live (5s)',
                            style: TextStyle(
                              fontSize: 11,
                              fontWeight: FontWeight.bold,
                              color: _autoRefresh
                                  ? widget.domainColor
                                  : Colors.white70,
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),

                  // Reload button
                  IconButton(
                    icon: const Icon(
                      Icons.refresh,
                      size: 16,
                      color: Colors.white70,
                    ),
                    tooltip: 'Manual Refresh',
                    splashRadius: 18,
                    onPressed: _snapshot.available && !_snapshot.loading
                        ? _loadLogs
                        : null,
                  ),

                  // Scroll to Bottom button
                  IconButton(
                    icon: const Icon(
                      Icons.vertical_align_bottom,
                      size: 16,
                      color: Colors.white70,
                    ),
                    tooltip: 'Scroll to newest (bottom)',
                    splashRadius: 18,
                    onPressed: _scrollToBottom,
                  ),
                  IconButton(
                    tooltip: 'Copy filtered log lines',
                    icon: const Icon(
                      Icons.copy,
                      size: 16,
                      color: Colors.white70,
                    ),
                    onPressed: filteredLogs.isEmpty
                        ? null
                        : () => Clipboard.setData(
                            ClipboardData(
                              text: filteredLogs
                                  .map((line) => line['text'])
                                  .join('\n'),
                            ),
                          ),
                  ),
                ],
              ),
            ),
          ),

          RemoteSnapshotStatus(snapshot: _snapshot, showLiveToggle: false),
          // Log Content View
          Expanded(
            child: _isLoading
                ? Center(
                    child: CircularProgressIndicator(
                      color: widget.domainColor,
                      strokeWidth: 2,
                    ),
                  )
                : _error != null && _logs.isEmpty
                ? SingleChildScrollView(
                    child: Padding(
                      padding: const EdgeInsets.all(20),
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          const Icon(
                            Icons.error_outline,
                            size: 40,
                            color: Colors.redAccent,
                          ),
                          const SizedBox(height: 10),
                          Text(
                            _error.toString().replaceFirst('Exception: ', ''),
                            textAlign: TextAlign.center,
                            style: const TextStyle(color: Colors.white70),
                          ),
                          const SizedBox(height: 12),
                          OutlinedButton(
                            onPressed: _snapshot.available && !_snapshot.loading
                                ? _loadLogs
                                : null,
                            child: const Text('Retry'),
                          ),
                        ],
                      ),
                    ),
                  )
                : filteredLogs.isEmpty
                ? Center(
                    child: SingleChildScrollView(
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Icon(
                            Icons.notes,
                            size: 48,
                            color: Colors.white.withValues(alpha: 0.15),
                          ),
                          const SizedBox(height: 12),
                          const Text(
                            'No log entries matching criteria',
                            style: TextStyle(
                              color: Colors.white38,
                              fontSize: 13,
                            ),
                          ),
                        ],
                      ),
                    ),
                  )
                : SelectionArea(
                    child: ListView.builder(
                      controller: _scrollController,
                      itemCount: filteredLogs.length,
                      itemBuilder: (context, index) {
                        final item = filteredLogs[index];
                        final text = item['text'] as String? ?? '';
                        final level = item['level'] as String? ?? 'INFO';

                        Color tagColor = Colors.cyanAccent;
                        Color bgAccent = Colors.transparent;

                        if (level == 'ERROR') {
                          tagColor = Colors.redAccent;
                          bgAccent = Colors.redAccent.withValues(alpha: 0.05);
                        } else if (level == 'WARN') {
                          tagColor = Colors.orangeAccent;
                          bgAccent = Colors.orangeAccent.withValues(
                            alpha: 0.04,
                          );
                        }

                        return Container(
                          color: bgAccent != Colors.transparent
                              ? bgAccent
                              : (index % 2 == 0
                                    ? Colors.transparent
                                    : Colors.white.withValues(alpha: 0.015)),
                          padding: const EdgeInsets.symmetric(
                            horizontal: 14,
                            vertical: 3,
                          ),
                          child: Row(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              // Line number / level pill
                              Container(
                                width: 48,
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 4,
                                  vertical: 2,
                                ),
                                decoration: BoxDecoration(
                                  color: tagColor.withValues(alpha: 0.12),
                                  borderRadius: BorderRadius.circular(3),
                                ),
                                alignment: Alignment.center,
                                child: Text(
                                  level,
                                  style: TextStyle(
                                    fontSize: 9,
                                    fontWeight: FontWeight.w900,
                                    color: tagColor,
                                    letterSpacing: 0.5,
                                  ),
                                ),
                              ),
                              const SizedBox(width: 10),
                              // Log message text
                              Expanded(
                                child: Text(
                                  text,
                                  style: TextStyle(
                                    fontFamily: kMonoFont,
                                    fontFamilyFallback: kMonoFontFallback,
                                    fontSize: 11,
                                    height: 1.4,
                                    color: level == 'ERROR'
                                        ? Colors.red.shade200
                                        : level == 'WARN'
                                        ? Colors.orange.shade200
                                        : Colors.white70,
                                  ),
                                ),
                              ),
                            ],
                          ),
                        );
                      },
                    ),
                  ),
          ),

          // Bottom Bar Status
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 5),
            decoration: BoxDecoration(
              color: const Color(0xFF1B1C28),
              border: Border(
                top: BorderSide(
                  color: widget.domainColor.withValues(alpha: 0.1),
                ),
              ),
            ),
            child: Row(
              children: [
                Icon(
                  Icons.terminal,
                  size: 13,
                  color: widget.domainColor.withValues(alpha: 0.8),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    '${_selectedLogType.toUpperCase()} • Showing ${filteredLogs.length} of ${_logs.length} lines',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      fontSize: 11,
                      color: Colors.white54,
                      fontFamily: kMonoFont,
                      fontFamilyFallback: kMonoFontFallback,
                    ),
                  ),
                ),
                if (_autoRefresh)
                  Row(
                    children: [
                      Container(
                        width: 6,
                        height: 6,
                        decoration: BoxDecoration(
                          shape: BoxShape.circle,
                          color: widget.domainColor,
                        ),
                      ),
                      const SizedBox(width: 6),
                      Text(
                        _snapshot.available ? 'AUTO REFRESH' : 'PAUSED · SSH',
                        style: TextStyle(
                          fontSize: 9,
                          fontWeight: FontWeight.bold,
                          color: widget.domainColor,
                          letterSpacing: 0.8,
                        ),
                      ),
                    ],
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSeverityFilter(String name, Color color) {
    final isSelected = _selectedSeverity == name;
    return InkWell(
      onTap: () => setState(() => _selectedSeverity = name),
      borderRadius: BorderRadius.circular(4),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
        decoration: BoxDecoration(
          color: isSelected
              ? color.withValues(alpha: 0.2)
              : const Color(0xFF222334),
          borderRadius: BorderRadius.circular(4),
          border: Border.all(
            color: isSelected ? color : Colors.transparent,
            width: 1,
          ),
        ),
        child: Text(
          name,
          style: TextStyle(
            fontSize: 10,
            fontWeight: isSelected ? FontWeight.bold : FontWeight.normal,
            color: isSelected ? color : Colors.white60,
          ),
        ),
      ),
    );
  }
}
