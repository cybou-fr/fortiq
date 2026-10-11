import 'dart:convert';
import 'dart:typed_data';

import 'shell.dart';

/// Server-side Poppler rendering. Only the selected raster page crosses SSH.
class PdfPreviewService {
  final RemoteShell shell;
  PdfPreviewService(this.shell);

  Future<int> pageCount(String path) async {
    final result = await shell.execute(
      'LC_ALL=C pdfinfo -- ${shQuote(path)}',
    );
    final match = RegExp(r'^Pages:\s*(\d+)\s*$', multiLine: true)
        .firstMatch(result);
    final pages = int.tryParse(match?.group(1) ?? '');
    if (pages == null || pages < 1 || pages > 100000) {
      throw StateError('Cannot determine PDF page count.');
    }
    return pages;
  }

  Future<Uint8List> renderPage(String path, int page) async {
    if (page < 1 || page > 100000) throw ArgumentError('Invalid PDF page');
    final q = shQuote(path);
    // Binary PNG encoded as base64 for RemoteShell.execute's string transport.
    // Only one bounded-resolution page is rendered, never a full PDF.
    final data = await shell.execute(
      'pdftoppm -f $page -l $page -singlefile -scale-to 1400 '
      '-png -- $q | base64 -w 0',
    );
    final bytes = base64Decode(data.replaceAll(RegExp(r'\s+'), ''));
    if (bytes.length < 8 || bytes[0] != 0x89 || bytes[1] != 0x50 ||
        bytes[2] != 0x4e || bytes[3] != 0x47) {
      throw StateError('PDF renderer did not return a PNG.');
    }
    return bytes;
  }
}
