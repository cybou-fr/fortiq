import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/desktop_file_router.dart';

void main() {
  test('image formats select media viewer', () {
    for (final ext in ['png', 'jpg', 'jpeg', 'gif', 'webp', 'bmp']) {
      expect(classifyDesktopFile('/home/user/photo.$ext'),
          DesktopFileKind.image);
    }
  });

  test('markdown formats select Docs Studio', () {
    for (final ext in ['md', 'markdown', 'mdown']) {
      expect(classifyDesktopFile('/docs/readme.$ext'),
          DesktopFileKind.markdown);
    }
  });

  test('known text formats select text editor', () {
    for (final ext in ['txt', 'json', 'yaml', 'dart', 'cpp', 'csv']) {
      expect(classifyDesktopFile('/docs/file.$ext'),
          DesktopFileKind.text);
    }
  });

  test('PDF is not sent to editor', () {
    expect(classifyDesktopFile('/home/user/Report.PDF'), DesktopFileKind.pdf);
  });

  test('unknown binary types have no implicit text viewer', () {
    expect(classifyDesktopFile('/home/user/archive.zip'),
        DesktopFileKind.unsupported);
    expect(classifyDesktopFile('/home/user/program'),
        DesktopFileKind.unsupported);
  });
}
