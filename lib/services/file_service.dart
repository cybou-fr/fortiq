import 'dart:convert';
import 'dart:typed_data';
import '../models/linux_models.dart';
import 'shell.dart';

/// Remote script for [FileService.writeFile]: `$1` = target, `$2` = byte count.
///
/// An existing writable file is replaced atomically (temp file in the same
/// directory, same mode/owner, renamed only after the full content arrived),
/// so a dropped connection never leaves a truncated file. A cut-off upload ends
/// the same way as a finished one (EOF on stdin), hence the size check.
/// New files and files in unwritable directories are written directly; when an
/// existing file had to be written that way the script reports
/// [_nonAtomicMarker] on stderr so the UI can warn about it.
const _nonAtomicMarker = 'FORTIQ_NONATOMIC';
const _atomicWriteScript = r'''
t=$(readlink -f -- "$1") || t=$1
d=$(dirname -- "$t")
existed=0
[ -f "$t" ] && existed=1
if [ -f "$t" ] && [ -w "$t" ] && [ -w "$d" ]; then
  tmp=$(mktemp "$d/.fortiq-save.XXXXXX") || exit 1
  if cat > "$tmp" \
     && [ "$(wc -c < "$tmp")" -eq "$2" ] \
     && chmod --reference="$t" "$tmp" \
     && { chown --reference="$t" "$tmp" 2>/dev/null || true; } \
     && mv -f -- "$tmp" "$t"; then
    exit 0
  fi
  rm -f -- "$tmp"
  echo "Upload incomplete, file left unchanged" >&2
  exit 1
fi
cat > "$t" || exit 1
[ "$(wc -c < "$t")" -eq "$2" ] || { echo "Upload incomplete" >&2; exit 1; }
[ "$existed" = 1 ] && echo FORTIQ_NONATOMIC >&2
exit 0
''';

/// Files and directories on the server.
class FileService {
  final RemoteShell _ssh;

  FileService(this._ssh);

  /// Lists files using `ls -la --time-style=+%s`
  Future<List<LinuxFileInfo>> listDirectory(String path) async {
    // Using standard POSIX ls with timestamp in unix epoch for precision
    final output = await _ssh.execute('ls -la --time-style=+%s ${shQuote(path)}');
    final lines = LineSplitter.split(output);
    final results = <LinuxFileInfo>[];

    for (final line in lines) {
      final trimmed = line.trim();
      if (trimmed.isEmpty || trimmed.startsWith('total ')) continue;

      // ls -la format:
      // drwxr-xr-x  2 root root 4096 1728599420 filename
      final parts = trimmed.split(RegExp(r'\s+'));
      if (parts.length < 7) continue;

      final permissions = parts[0];
      final links = int.tryParse(parts[1]) ?? 1;
      final owner = parts[2];
      final group = parts[3];
      final size = int.tryParse(parts[4]) ?? 0;
      final timestampSec = int.tryParse(parts[5]) ?? 0;
      final modified = DateTime.fromMillisecondsSinceEpoch(timestampSec * 1000);

      // File name may contain spaces
      final name = parts.sublist(6).join(' ');

      if (name == '.' || name == '..') continue;

      final isDir = permissions.startsWith('d');
      final isLink = permissions.startsWith('l');

      results.add(LinuxFileInfo(
        permissions: permissions,
        links: links,
        owner: owner,
        group: group,
        size: size,
        modified: modified,
        name: name,
        isDirectory: isDir,
        isLink: isLink,
      ));
    }

    // Sort: directories first, then alphabetically
    results.sort((a, b) {
      if (a.isDirectory && !b.isDirectory) return -1;
      if (!a.isDirectory && b.isDirectory) return 1;
      return a.name.toLowerCase().compareTo(b.name.toLowerCase());
    });

    return results;
  }

  /// Creates a directory using `mkdir -p`
  Future<void> createDirectory(String path) async {
    await _ssh.execute('mkdir -p ${shQuote(path)}');
  }

  /// Removes file or directory
  Future<void> remove(String path, {bool recursive = false}) async {
    final flag = recursive ? '-rf' : '';
    await _ssh.execute('rm $flag -- ${shQuote(path)}');
  }

  /// Reads file content using `cat`
  Future<String> readFile(String path) async {
    return await _ssh.execute('cat -- ${shQuote(path)}');
  }

  /// Reads binary file content (e.g. image) as Uint8List using base64 encoding over SSH
  Future<Uint8List> readBinaryFile(String path) async {
    final q = shQuote(path);
    final out = await _ssh.execute('base64 -w 0 -- $q 2>/dev/null || base64 -- $q');
    final cleanBase64 = out.replaceAll(RegExp(r'\s+'), '');
    return base64Decode(cleanBase64);
  }

  /// Writes file content
  ///
  /// Returns false when the file could only be overwritten in place (its
  /// directory is not writable), which a dropped connection could truncate.
  Future<bool> writeFile(String path, String content) async {
    final client = _ssh.rawClient;
    if (client == null || !_ssh.isConnected) {
      throw Exception('Not connected to SSH server');
    }
    // Content goes through stdin: command arguments are limited to ~128 KB.
    final bytes = Uint8List.fromList(utf8.encode(content));
    final command = 'sh -c ${shQuote(_atomicWriteScript)} sh ${shQuote(path)} ${bytes.length}';
    final session = await client.execute(command);
    final stderrFuture = utf8.decodeStream(session.stderr);
    session.stdin.add(bytes);
    await session.stdin.close();
    await session.done;
    final err = await stderrFuture;
    final code = session.exitCode;
    if ((code != null && code != 0) || session.exitSignal != null) {
      throw SshCommandException(command, code, err);
    }
    return !err.contains(_nonAtomicMarker);
  }

  /// Searches files by name or content on the remote host
  Future<List<Map<String, dynamic>>> searchFiles(
    String query, {
    String searchRoot = '/',
    bool searchContent = false,
  }) async {
    final q = shQuote(query);
    final root = shQuote(searchRoot);
    final nameGlob = shQuote('*$query*');

    String cmd;
    if (searchContent) {
      // Search file content using grep (limiting to text files and max 50 results)
      cmd = 'grep -rIn --exclude-dir={proc,sys,dev,run,mnt,media} -m 1 -e $q -- $root 2>/dev/null | head -n 40';
    } else {
      // Search file name using find
      cmd = 'find $root -maxdepth 6 \\( -path "/proc" -o -path "/sys" -o -path "/dev" \\) -prune -o -iname $nameGlob -print 2>/dev/null | head -n 40';
    }

    try {
      final out = await _ssh.execute(cmd);
      final lines = LineSplitter.split(out);
      final list = <Map<String, dynamic>>[];

      for (final line in lines) {
        final trimmed = line.trim();
        if (trimmed.isEmpty) continue;

        if (searchContent) {
          // grep output: /path/to/file:linenumber:matched line
          final parts = trimmed.split(':');
          if (parts.length >= 3) {
            final path = parts[0];
            final lineNum = parts[1];
            final match = parts.sublist(2).join(':').trim();
            list.add({
              'path': path,
              'line': lineNum,
              'preview': match,
              'type': 'content',
            });
          }
        } else {
          list.add({
            'path': trimmed,
            'line': null,
            'preview': null,
            'type': trimmed.endsWith('/') ? 'dir' : 'file',
          });
        }
      }
      return list;
    } catch (_) {
      return [];
    }
  }
}
