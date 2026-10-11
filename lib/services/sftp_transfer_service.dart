import 'dart:io';
import 'dart:typed_data';

import 'package:dartssh2/dartssh2.dart';
import 'shell.dart';
import 'desktop_task_controller.dart';

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
    TransferCancellation? cancellation,
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
      cancellation?.throwIfRequested();
      if (!sessionCurrent()) throw StateError('SSH session changed.');
      final total = attrs.size;
      final sink = partial.openWrite(mode: FileMode.writeOnly);
      Object? failure;
      var receivedBytes = -1;
      try {
        receivedBytes = await sftp.download(
          remotePath,
          sink,
          onProgress: (received) {
            cancellation?.throwIfRequested();
            onProgress(received, total);
          },
          closeDestination: false,
        );
        await sink.flush();
      } catch (error) {
        failure = error;
      }
      try {
        await sink.close();
      } catch (error) {
        // После отмены закрытие файла может сообщить «File closed» и скрыть
        // настоящую причину; первая ошибка важнее.
        failure ??= error;
      }
      if (failure != null) {
        if (cancellation?.isRequested ?? false) throw const TransferCancelled();
        throw failure;
      }
      cancellation?.throwIfRequested();
      if (!sessionCurrent()) throw StateError('SSH session changed.');
      if (await partial.length() != receivedBytes ||
          (total != null && receivedBytes != total)) {
        throw StateError('Incomplete transfer. Partial file retained.');
      }
      if (await target.exists()) {
        throw StateError('Destination appeared during download. Partial file retained.');
      }
      cancellation?.throwIfRequested();
      await partial.rename(localPath);
    } finally {
      await sftp.close();
    }
  }
  /// Explicit upload into a selected Linux directory. Stage under a temporary
  /// name, verify the byte count, then publish after checking the destination.
  /// A Linux hard-link publish avoids the SFTP rename overwrite race.
  /// Requires the temporary file and target in the same filesystem.
  Future<void> upload({
    required SSHClient client,
    required String localPath,
    required String remotePath,
    required void Function(int bytes, int? totalBytes) onProgress,
    required bool Function() sessionCurrent,
    TransferCancellation? cancellation,
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
    var createdPartial = false;
    try {
      try {
        await sftp.stat(remotePath);
        throw StateError('Destination already exists on Linux.');
      } on SftpStatusError catch (error) {
        if (error.code != SftpStatusCode.noSuchFile) rethrow;
      }
      final handle = await sftp.open(
        partial,
        mode: SftpFileOpenMode.create |
            SftpFileOpenMode.write |
            SftpFileOpenMode.exclusive,
      );
      createdPartial = true;
      try {
        Stream<Uint8List> chunks() async* {
          await for (final data in source.openRead()) {
            cancellation?.throwIfRequested();
            if (!sessionCurrent()) throw StateError('SSH session changed.');
            yield Uint8List.fromList(data);
          }
        }
        cancellation?.throwIfRequested();
        final writer = handle.write(
          chunks(),
          onProgress: (sent) => onProgress(sent, length),
        );
        await writer.done;
      } finally {
        await handle.close();
      }
      cancellation?.throwIfRequested();
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
      cancellation?.throwIfRequested();
      // Atomic no-clobber publish on the same Linux filesystem: hard-link
      // creation fails if the target appeared after our last check.
      final publish = await client.runWithResult(
        'ln -- ${shQuote(partial)} ${shQuote(remotePath)} && '
        'rm -- ${shQuote(partial)}',
      );
      if (publish.exitCode != 0) {
        throw StateError('Could not publish upload without overwriting; '
            'verify the temporary remote file.');
      }
    } on TransferCancelled {
      // Only delete this invocation's staging file, after the writer handle
      // has closed and only while the original authenticated session is live.
      // Cleanup failure must not hide the cancellation.
      if (createdPartial && sessionCurrent()) {
        try {
          await sftp.remove(partial);
        } catch (_) {
          // A staging file may remain; report this through task metadata later.
        }
      }
      rethrow;
    } finally {
      await sftp.close();
    }
  }

}
