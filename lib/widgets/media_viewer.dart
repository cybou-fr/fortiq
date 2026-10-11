import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'mono.dart';
import '../services/ssh_service.dart';

class RemoteMediaViewer extends StatefulWidget {
  final SshClientService sshService;
  final String filePath;
  final Color domainColor;

  const RemoteMediaViewer({
    super.key,
    required this.sshService,
    required this.filePath,
    required this.domainColor,
  });

  @override
  State<RemoteMediaViewer> createState() => _RemoteMediaViewerState();
}

class _RemoteMediaViewerState extends State<RemoteMediaViewer> {

  // Async loads may finish after the window was closed.
  @override
  void setState(VoidCallback fn) {
    if (mounted) super.setState(fn);
  }
  bool _isLoading = true;
  String? _error;
  Uint8List? _imageData;
  double _scale = 1.0;
  int _rotationQuarterTurns = 0;

  @override
  void initState() {
    super.initState();
    _loadImage();
  }

  @override
  void didUpdateWidget(covariant RemoteMediaViewer oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.filePath != widget.filePath) {
      _loadImage();
    }
  }

  Future<void> _loadImage() async {
    setState(() {
      _isLoading = true;
      _error = null;
      _scale = 1.0;
      _rotationQuarterTurns = 0;
    });

    try {
      final bytes = await widget.sshService.files.readBinaryFile(widget.filePath);
      if (mounted) {
        setState(() {
          _imageData = bytes;
          _isLoading = false;
        });
      }
    } catch (e) {
      if (mounted) {
        setState(() {
          _error = 'Failed to load image: $e';
          _isLoading = false;
        });
      }
    }
  }

  void _zoomIn() {
    setState(() {
      _scale = (_scale + 0.25).clamp(0.25, 5.0);
    });
  }

  void _zoomOut() {
    setState(() {
      _scale = (_scale - 0.25).clamp(0.25, 5.0);
    });
  }

  void _resetZoom() {
    setState(() {
      _scale = 1.0;
      _rotationQuarterTurns = 0;
    });
  }

  void _rotateClockwise() {
    setState(() {
      _rotationQuarterTurns = (_rotationQuarterTurns + 1) % 4;
    });
  }

  @override
  Widget build(BuildContext context) {
    final fileName = widget.filePath.split('/').last;

    return Container(
      color: const Color(0xFF0D0E15),
      child: Column(
        children: [
          // Control Toolbar
          Container(
            height: 40,
            padding: const EdgeInsets.symmetric(horizontal: 14),
            decoration: BoxDecoration(
              color: const Color(0xFF161724),
              border: Border(
                bottom: BorderSide(
                  color: widget.domainColor.withValues(alpha: 0.15),
                ),
              ),
            ),
            child: Row(
              children: [
                Icon(Icons.image, size: 16, color: widget.domainColor),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    fileName,
                    style: const TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.bold,
                      color: Colors.white,
                    ),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                if (_imageData != null) ...[
                  Text(
                    '${(_imageData!.lengthInBytes / 1024).toStringAsFixed(1)} KB',
                    style: const TextStyle(fontSize: 11, color: Colors.white38),
                  ),
                  const SizedBox(width: 12),
                  // Zoom Out
                  IconButton(
                    icon: const Icon(Icons.zoom_out, size: 16, color: Colors.white70),
                    tooltip: 'Zoom Out',
                    splashRadius: 16,
                    onPressed: _zoomOut,
                  ),
                  // Zoom Percent Display
                  InkWell(
                    onTap: _resetZoom,
                    child: Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 4),
                      child: Text(
                        '${(_scale * 100).toInt()}%',
                        style: const TextStyle(fontSize: 11, color: Colors.white70, fontFamily: kMonoFont, fontFamilyFallback: kMonoFontFallback),
                      ),
                    ),
                  ),
                  // Zoom In
                  IconButton(
                    icon: const Icon(Icons.zoom_in, size: 16, color: Colors.white70),
                    tooltip: 'Zoom In',
                    splashRadius: 16,
                    onPressed: _zoomIn,
                  ),
                  const SizedBox(width: 4),
                  // Rotate
                  IconButton(
                    icon: const Icon(Icons.rotate_right, size: 16, color: Colors.white70),
                    tooltip: 'Rotate 90°',
                    splashRadius: 16,
                    onPressed: _rotateClockwise,
                  ),
                  const SizedBox(width: 4),
                  // Reload
                  IconButton(
                    icon: const Icon(Icons.refresh, size: 16, color: Colors.white70),
                    tooltip: 'Reload image',
                    splashRadius: 16,
                    onPressed: _loadImage,
                  ),
                ],
              ],
            ),
          ),

          // Image viewport
          Expanded(
            child: _isLoading
                ? Center(
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        CircularProgressIndicator(color: widget.domainColor, strokeWidth: 2),
                        const SizedBox(height: 12),
                        const Text('Streaming image from remote host...', style: TextStyle(color: Colors.white54, fontSize: 12)),
                      ],
                    ),
                  )
                : _error != null
                    ? Center(
                        child: Text(
                          _error!,
                          style: const TextStyle(color: Colors.redAccent, fontSize: 13),
                        ),
                      )
                    : _imageData == null || _imageData!.isEmpty
                        ? const Center(
                            child: Text('Image is empty or unreadable', style: TextStyle(color: Colors.white38)),
                          )
                        : InteractiveViewer(
                            panEnabled: true,
                            scaleEnabled: true,
                            minScale: 0.2,
                            maxScale: 6.0,
                            child: Center(
                              child: Transform.scale(
                                scale: _scale,
                                child: RotatedBox(
                                  quarterTurns: _rotationQuarterTurns,
                                  child: Image.memory(
                                    _imageData!,
                                    fit: BoxFit.contain,
                                    errorBuilder: (context, error, stackTrace) {
                                      return const Center(
                                        child: Text(
                                          'Unsupported image format',
                                          style: TextStyle(color: Colors.redAccent),
                                        ),
                                      );
                                    },
                                  ),
                                ),
                              ),
                            ),
                          ),
          ),
        ],
      ),
    );
  }
}
