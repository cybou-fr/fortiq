import 'dart:typed_data';
import 'package:flutter/material.dart';

import '../services/pdf_preview_service.dart';
import '../services/ssh_service.dart';

class RemotePdfViewer extends StatefulWidget {
  final SshClientService sshService;
  final String filePath;

  const RemotePdfViewer({
    super.key,
    required this.sshService,
    required this.filePath,
  });

  @override
  State<RemotePdfViewer> createState() => _RemotePdfViewerState();
}

class _RemotePdfViewerState extends State<RemotePdfViewer> {
  int _page = 1;
  int? _count;
  double _zoom = 1;
  Uint8List? _image;
  String? _error;
  bool _loading = false;
  int _request = 0;

  @override
  void initState() {
    super.initState();
    _open();
  }

  @override
  void didUpdateWidget(covariant RemotePdfViewer oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.filePath != widget.filePath ||
        oldWidget.sshService != widget.sshService) {
      _open();
    }
  }

  Future<void> _open() async {
    final request = ++_request;
    final client = widget.sshService.rawClient;
    setState(() {
      _count = null;
      _page = 1;
      _image = null;
      _error = null;
      _loading = true;
    });
    try {
      final count = await PdfPreviewService(widget.sshService).pageCount(widget.filePath);
      if (!mounted || request != _request ||
          widget.sshService.rawClient != client) return;
      setState(() => _count = count);
      await _load(1, request: request);
    } catch (error) {
      if (!mounted || request != _request) return;
      setState(() {
        _error = error.toString();
        _loading = false;
      });
    }
  }

  Future<void> _load(int page, {int? request}) async {
    if (_count != null && (page < 1 || page > _count!)) return;
    final serial = request ?? ++_request;
    final client = widget.sshService.rawClient;
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final image = await PdfPreviewService(widget.sshService).renderPage(widget.filePath, page);
      if (!mounted || serial != _request ||
          widget.sshService.rawClient != client) return;
      setState(() {
        _page = page;
        _image = image;
        _loading = false;
      });
    } catch (error) {
      if (!mounted || serial != _request) return;
      setState(() {
        _error = error.toString();
        _loading = false;
      });
    }
  }

  @override
  void dispose() {
    _request++;
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
          child: Row(
            children: [
              IconButton(
                tooltip: 'Previous page',
                onPressed: _loading || _page <= 1 ? null : () => _load(_page - 1),
                icon: const Icon(Icons.chevron_left),
              ),
              Text('Page $_page / ${_count ?? "…"}'),
              IconButton(
                tooltip: 'Next page',
                onPressed: _loading || _count == null || _page >= _count!
                    ? null : () => _load(_page + 1),
                icon: const Icon(Icons.chevron_right),
              ),
              const Spacer(),
              IconButton(
                tooltip: 'Zoom out',
                onPressed: () => setState(() => _zoom = (_zoom - 0.25).clamp(0.5, 3.0)),
                icon: const Icon(Icons.zoom_out),
              ),
              Text('${(_zoom * 100).round()}%'),
              IconButton(
                tooltip: 'Zoom in',
                onPressed: () => setState(() => _zoom = (_zoom + 0.25).clamp(0.5, 3.0)),
                icon: const Icon(Icons.zoom_in),
              ),
            ],
          ),
        ),
        if (_loading) const LinearProgressIndicator(),
        if (_error != null)
          Padding(
            padding: const EdgeInsets.all(16),
            child: SelectableText(_error!),
          ),
        Expanded(
          child: _image == null
              ? const Center(child: Text('No page rendered'))
              : InteractiveViewer(
                  minScale: 0.5,
                  maxScale: 5,
                  child: Center(
                    child: Image.memory(
                      _image!,
                      fit: BoxFit.contain,
                      scale: 1 / _zoom,
                    ),
                  ),
                ),
        ),
      ],
    );
  }
}
