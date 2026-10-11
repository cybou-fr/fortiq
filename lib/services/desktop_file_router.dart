/// Routing of Linux files to FORTIQ desktop applications.
/// PDF and other unsupported binary formats must never enter the text editor.
enum DesktopFileKind { image, markdown, text, pdf, unsupported }

DesktopFileKind classifyDesktopFile(String path) {
  final name = path.split('/').last.toLowerCase();
  if (name.endsWith('.pdf')) return DesktopFileKind.pdf;
  if (const ['.png', '.jpg', '.jpeg', '.gif', '.webp', '.bmp']
      .any(name.endsWith)) return DesktopFileKind.image;
  if (const ['.md', '.markdown', '.mdown'].any(name.endsWith)) {
    return DesktopFileKind.markdown;
  }
  if (const [
    '.txt', '.log', '.json', '.yaml', '.yml', '.xml', '.csv', '.ini',
    '.conf', '.cfg', '.sh', '.py', '.js', '.ts', '.dart', '.html',
    '.css', '.toml', '.sql', '.c', '.h', '.cpp', '.hpp', '.java',
    '.rs', '.go', '.php', '.rb', '.env'
  ].any(name.endsWith)) return DesktopFileKind.text;
  return DesktopFileKind.unsupported;
}
