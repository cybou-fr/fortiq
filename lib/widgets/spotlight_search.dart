import 'package:flutter/material.dart';
import '../services/ssh_service.dart';

class RemoteSpotlightSearch extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;
  final void Function(String filePath)? onOpenFile;

  const RemoteSpotlightSearch({
    super.key,
    required this.sshService,
    required this.domainColor,
    this.onOpenFile,
  });

  @override
  State<RemoteSpotlightSearch> createState() => _RemoteSpotlightSearchState();
}

class _RemoteSpotlightSearchState extends State<RemoteSpotlightSearch> {
  final TextEditingController _queryController = TextEditingController();
  final FocusNode _inputFocus = FocusNode();
  List<Map<String, dynamic>> _results = [];
  bool _loading = false;
  bool _searchContent = false;
  String _searchRoot = '/etc'; // Default fast root, changeable to / or /var

  final List<String> _quickRoots = ['/etc', '/var/log', '/home', '/root', '/'];

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _inputFocus.requestFocus();
    });
  }

  @override
  void dispose() {
    _queryController.dispose();
    _inputFocus.dispose();
    super.dispose();
  }

  Future<void> _performSearch() async {
    final q = _queryController.text.trim();
    if (q.isEmpty) return;

    setState(() => _loading = true);
    try {
      final res = await widget.sshService.searchFiles(
        q,
        searchRoot: _searchRoot,
        searchContent: _searchContent,
      );
      if (mounted) {
        setState(() {
          _results = res;
          _loading = false;
        });
      }
    } catch (_) {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        // Spotlight Search Bar
        Container(
          padding: const EdgeInsets.all(12),
          decoration: const BoxDecoration(
            color: Color(0xFF161722),
            border: Border(bottom: BorderSide(color: Color(0xFF242636))),
          ),
          child: Column(
            children: [
              Row(
                children: [
                  Icon(Icons.search, size: 22, color: widget.domainColor),
                  const SizedBox(width: 10),
                  Expanded(
                    child: TextField(
                      controller: _queryController,
                      focusNode: _inputFocus,
                      style: const TextStyle(color: Colors.white, fontSize: 14),
                      decoration: InputDecoration(
                        border: InputBorder.none,
                        hintText: _searchContent
                            ? 'Search text inside files (grep)...'
                            : 'Search file or directory name (find)...',
                        hintStyle: const TextStyle(color: Colors.white30, fontSize: 13),
                        isDense: true,
                      ),
                      onSubmitted: (_) => _performSearch(),
                    ),
                  ),
                  if (_loading)
                    const SizedBox(
                      width: 18,
                      height: 18,
                      child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white70),
                    )
                  else
                    ElevatedButton(
                      onPressed: _performSearch,
                      style: ElevatedButton.styleFrom(
                        backgroundColor: widget.domainColor,
                        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 0),
                        minimumSize: const Size(60, 32),
                      ),
                      child: const Text('Search', style: TextStyle(fontSize: 12)),
                    ),
                ],
              ),
              const SizedBox(height: 10),

              // Filter & Scope row
              Row(
                children: [
                  const Text('Scope: ', style: TextStyle(color: Colors.white38, fontSize: 11)),
                  ..._quickRoots.map((r) => Padding(
                        padding: const EdgeInsets.only(right: 4.0),
                        child: ChoiceChip(
                          label: Text(r, style: const TextStyle(fontSize: 10, fontFamily: 'Consolas, monospace')),
                          selected: _searchRoot == r,
                          selectedColor: widget.domainColor.withValues(alpha: 0.25),
                          visualDensity: VisualDensity.compact,
                          onSelected: (_) {
                            setState(() => _searchRoot = r);
                            if (_queryController.text.trim().isNotEmpty) _performSearch();
                          },
                        ),
                      )),
                  const Spacer(),
                  FilterChip(
                    label: const Text('Search Content (grep)', style: TextStyle(fontSize: 10)),
                    selected: _searchContent,
                    selectedColor: widget.domainColor.withValues(alpha: 0.25),
                    visualDensity: VisualDensity.compact,
                    onSelected: (val) {
                      setState(() => _searchContent = val);
                      if (_queryController.text.trim().isNotEmpty) _performSearch();
                    },
                  ),
                ],
              ),
            ],
          ),
        ),

        // Search Results List
        Expanded(
          child: _results.isEmpty && !_loading
              ? Center(
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(Icons.manage_search, size: 48, color: Colors.white.withValues(alpha: 0.15)),
                      const SizedBox(height: 10),
                      Text(
                        _queryController.text.isEmpty
                            ? 'Type a keyword to search across Linux filesystem'
                            : 'No matching files found in $_searchRoot',
                        style: const TextStyle(color: Colors.white38, fontSize: 12),
                      ),
                    ],
                  ),
                )
              : ListView.builder(
                  itemCount: _results.length,
                  itemBuilder: (context, index) {
                    final item = _results[index];
                    final path = item['path'] as String;
                    final isContent = item['type'] == 'content';
                    final line = item['line'] as String?;
                    final preview = item['preview'] as String?;

                    final filename = path.split('/').last;
                    final parentDir = path.substring(0, path.length - filename.length);

                    return InkWell(
                      onTap: () {
                        widget.onOpenFile?.call(path);
                      },
                      hoverColor: Colors.white.withValues(alpha: 0.04),
                      child: Container(
                        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                        decoration: const BoxDecoration(
                          border: Border(bottom: BorderSide(color: Color(0xFF1B1C28))),
                        ),
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Padding(
                              padding: const EdgeInsets.only(top: 2.0),
                              child: Icon(
                                isContent
                                    ? Icons.find_in_page_outlined
                                    : (path.endsWith('/') ? Icons.folder : Icons.insert_drive_file),
                                size: 18,
                                color: widget.domainColor,
                              ),
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Row(
                                    children: [
                                      Text(
                                        filename.isEmpty ? '/' : filename,
                                        style: const TextStyle(
                                          color: Colors.white,
                                          fontSize: 13,
                                          fontWeight: FontWeight.w600,
                                        ),
                                      ),
                                      if (line != null) ...[
                                        const SizedBox(width: 6),
                                        Container(
                                          padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
                                          decoration: BoxDecoration(
                                            color: Colors.amber.withValues(alpha: 0.15),
                                            borderRadius: BorderRadius.circular(3),
                                          ),
                                          child: Text(
                                            'line $line',
                                            style: const TextStyle(color: Colors.amberAccent, fontSize: 10),
                                          ),
                                        ),
                                      ],
                                      const Spacer(),
                                      Text(
                                        parentDir,
                                        style: const TextStyle(color: Colors.white24, fontSize: 11, fontFamily: 'Consolas, monospace'),
                                        overflow: TextOverflow.ellipsis,
                                      ),
                                    ],
                                  ),
                                  if (preview != null && preview.isNotEmpty) ...[
                                    const SizedBox(height: 4),
                                    Text(
                                      preview,
                                      style: const TextStyle(
                                        color: Color(0xFF94A3B8),
                                        fontSize: 11,
                                        fontFamily: 'Consolas, monospace',
                                      ),
                                      maxLines: 2,
                                      overflow: TextOverflow.ellipsis,
                                    ),
                                  ],
                                ],
                              ),
                            ),
                            const SizedBox(width: 8),
                            const Icon(Icons.arrow_forward_ios, size: 11, color: Colors.white24),
                          ],
                        ),
                      ),
                    );
                  },
                ),
        ),

        // Footer info bar
        Container(
          height: 24,
          padding: const EdgeInsets.symmetric(horizontal: 12),
          decoration: const BoxDecoration(
            color: Color(0xFF12131A),
            border: Border(top: BorderSide(color: Color(0xFF202230))),
          ),
          child: Row(
            children: [
              Text(
                '${_results.length} results',
                style: const TextStyle(color: Colors.white38, fontSize: 10),
              ),
              const Spacer(),
              const Text(
                'Click item to open in Editor | Linux native find & grep',
                style: TextStyle(color: Colors.white24, fontSize: 10),
              ),
            ],
          ),
        ),
      ],
    );
  }
}
