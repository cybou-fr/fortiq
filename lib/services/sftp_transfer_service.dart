import 'dart:io';
import 'dart:typed_data';

import 'package:dartssh2/dartssh2.dart';

/// Explicit export from a remote Linux file to a user-chosen local path.
/// Uses the authenticated SSH transport; no extra service, port or API.
/// The caller must provide the same live SSH session for the entire transfer.
class SftpTransferService {
  const SftpTransferService();

  Future<void> download({
    required SSHClient client,
    required String remotePath,
    required String localPath,
    required void Function(int bytes, int? totalBytes) onProgress,
    required bool Function() sessionCurrent,
  }) async {
    if (remotePath.isEmpty || localPath.isEmpty || remotePath.contains('\x00')) {
      throw ArgumentError('Invalid transfer paths');
    }
    final target = File(localPath);
    if (await target.exists()) {
      throw StateError('Destination exists. Choose another filename.');
    }
    final partial = File('$localPath.fortiq-part');
    if (await partial.exists()) {
      throw StateError('An unfinished export already exists at this path.');
    }
    final sftp = await client.sftp();
    try {
      final attrs = await sftp.stat(remotePath);
      if (!attrs.isFile) throw StateError('Select a regular file.');
      if (!sessionCurrent()) throw StateError('SSH session changed.');
      final total = attrs.size;
      final sink = partial.openWrite(mode: FileMode.writeOnly);
      try {
        await sftp.download(
          remotePath,
          sink,
          onProgress: (received) => onProgress(received, total),
          closeDestination: false,
        );
        await sink.flush();
      } finally {
        await sink.close();
      }
      if (!sessionCurrent()) throw StateError('SSH session changed.');
      if (total != null && await partial.length() != total) {
        throw StateError('Incomplete transfer. Partial file retained.');
      }
      if (await target.exists()) {
        throw StateError('Destination appeared during download. Partial file retained.');
      }
      await partial.rename(localPath);
    } finally {
      await sftp.close();
    }
  }
  /// Explicit upload into a selected Linux directory. Stage under a temporary
  /// name, verify the byte count, then publish after checking the destination.
  /// The SFTP v3 rename/check pair is not atomic no-clobber: concurrent writers
  /// on the server can still race. Do not use for overwrite-sensitive workflows.
  Future<void> upload({
    required SSHClient client,
    required String localPath,
    required String remotePath,
    required void Function(int bytes, int? totalBytes) onProgress,
    required bool Function() sessionCurrent,
  }) async {
    if (localPath.isEmpty || remotePath.isEmpty ||
        remotePath.contains('\x00')) {
      throw ArgumentError('Invalid transfer paths');
    }
    final source = File(localPath);
    final length = await source.length();
    if (!sessionCurrent()) throw StateError('SSH session changed.');
    final sftp = await client.sftp();
    final partial = '$remotePath.fortiq-upload-${DateTime.now().microsecondsSinceEpoch}';
    try {
      try {
        await sftp.stat(remotePath);
        throw StateError('Destination already exists on Linux.');
      } on SftpStatusError catch (error) {
        if (error.code != SftpStatusCode.noSuchFile) rethrow;
      }
      final handle = await sftp.open(
        partial,
        mode: SftpFileOpenMode.create | SftpFileOpenMode.write,
      );
      try {
        final writer = handle.write(
          source.openRead().map((data) => Uint8List.fromList(data)),
          onProgress: (sent) => onProgress(sent, length),
        );
        await writer.done;
      } finally {
        await handle.close();
      }
      if (!sessionCurrent()) throw StateError('SSH session changed.');
      final attrs = await sftp.stat(partial);
      if (attrs.size != length) {
        throw StateError('Incomplete upload; temporary remote file retained.');
      }
      try {
        await sftp.stat(remotePath);
        throw StateError('Destination appeared during upload.');
      } on SftpStatusError catch (error) {
        if (error.code != SftpStatusCode.noSuchFile) rethrow;
      }
      await sftp.rename(partial, remotePath);
    } finally {
      await sftp.close();
    }
  }

}
