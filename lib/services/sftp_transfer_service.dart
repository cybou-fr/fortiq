import 'dart:io';

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
      if (!attrs.isRegularFile) throw StateError('Select a regular file.');
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
}
