import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter/foundation.dart' show visibleForTesting;
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
/// [nonAtomicMarker] on stderr so the UI can warn about it.
const nonAtomicMarker = 'FORTIQ_NONATOMIC';
@visibleForTesting
const atomicWriteScript = r'''
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
    // One record per entry, NUL-separated, so that any file name survives
    // (spaces, quotes, newlines): header fields, then name, then link target.
    // -H follows the starting directory if it is itself a symbolic link.
    final output = await _ssh.execute(
      'find -H ${shQuote(path)} -mindepth 1 -maxdepth 1 '
      r"-printf '%y\t%Y\t%M\t%n\t%u\t%g\t%s\t%T@\0%f\0%l\0'",
    );
    return parseDirectoryListing(output);
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

  /// Renames a remote entry without overwriting an existing destination.
  /// A failed/unknown SSH result must be surfaced rather than retried.
  Future<void> rename(String source, String destination) async {
    if (source.isEmpty || destination.isEmpty || source == destination ||
        source.contains('\\x00') || destination.contains('\\x00')) {
      throw ArgumentError('Invalid rename paths');
    }
    await _ssh.execute(
      'if [ -e ${shQuote(destination)} ] || [ -L ${shQuote(destination)} ]; then '
      'echo "Destination already exists" >&2; exit 1; fi; '
      'mv -nT -- ${shQuote(source)} ${shQuote(destination)} && '
      'test ! -e ${shQuote(source)} && test ! -L ${shQuote(source)}',
    );
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
    // Content goes through stdin: command arguments are limited to ~128 KB.
    final bytes = Uint8List.fromList(utf8.encode(content));
    final command = 'sh -c ${shQuote(atomicWriteScript)} sh ${shQuote(path)} ${bytes.length}';
    final result = await _ssh.executeWithInput(command, bytes);
    if (!result.ok) {
      throw SshCommandException(command, result.exitCode, result.stderr);
    }
    return !result.stderr.contains(nonAtomicMarker);
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

/// Parses the output of the `find -printf` command used by
/// [FileService.listDirectory]. Folders (and links to folders) come first.
List<LinuxFileInfo> parseDirectoryListing(String output) {
  final parts = output.split('\x00');
  final results = <LinuxFileInfo>[];
  for (var i = 0; i + 2 < parts.length; i += 3) {
    final fields = parts[i].split('\t');
    if (fields.length < 8) continue;
    final type = fields[0]; // d f l ...
    final targetType = fields[1]; // type after following a link: d f N L ?
    final mtime = double.tryParse(fields[7]) ?? 0;
    final target = parts[i + 2];
    results.add(LinuxFileInfo(
      permissions: fields[2],
      links: int.tryParse(fields[3]) ?? 1,
      owner: fields[4],
      group: fields[5],
      size: int.tryParse(fields[6]) ?? 0,
      modified: DateTime.fromMillisecondsSinceEpoch((mtime * 1000).round()),
      name: parts[i + 1],
      isDirectory: type == 'd' || (type == 'l' && targetType == 'd'),
      isLink: type == 'l',
      linkTarget: type == 'l' ? target : null,
    ));
  }
  results.sort((a, b) {
    if (a.isDirectory != b.isDirectory) return a.isDirectory ? -1 : 1;
    return a.name.toLowerCase().compareTo(b.name.toLowerCase());
  });
  return results;
}
