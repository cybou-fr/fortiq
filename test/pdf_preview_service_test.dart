import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/pdf_preview_service.dart';

import 'fake_shell.dart';

void main() {
  test('reads PDF page count from pdfinfo', () async {
    final shell = FakeShell((_) => 'Title: demo\nPages: 12\n');
    expect(await PdfPreviewService(shell).pageCount('/docs/a b.pdf'), 12);
    expect(shell.commands.single, contains("'/docs/a b.pdf'"));
  });

  test('rejects missing page count', () async {
    final shell = FakeShell((_) => 'No pages here');
    await expectLater(
      PdfPreviewService(shell).pageCount('/docs/no.pdf'), throwsStateError);
  });

  test('renders only selected page and validates PNG signature', () async {
    final png = base64Encode([137, 80, 78, 71, 13, 10, 26, 10]);
    final shell = FakeShell((_) => png);
    final image = await PdfPreviewService(shell).renderPage('/docs/a b.pdf', 3);
    expect(image.first, 137);
    expect(shell.commands.single, contains('-f 3 -l 3'));
  });

  test('non-PNG or invalid page is rejected', () async {
    final shell = FakeShell((_) => base64Encode([1, 2, 3]));
    await expectLater(
      PdfPreviewService(shell).renderPage('/docs/a.pdf', 1), throwsStateError);
    await expectLater(
      PdfPreviewService(shell).renderPage('/docs/a.pdf', 0), throwsArgumentError);
  });
}
