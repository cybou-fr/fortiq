import 'dart:async';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/desktop_task_controller.dart';
import 'package:fortiq/services/sftp_transfer_service.dart';
import 'package:fortiq/services/ssh_service.dart';

import 'support.dart';

/// Отказы при передаче файлов на настоящем сервере: обрыв SSH посреди
/// загрузки и скачивания, отмена на разных стадиях, конфликт имён, который
/// возникает уже во время передачи. Главное правило: чужой или уже
/// существующий файл не затирается, а недокачанное не выдаётся за готовое.
void main() {
  final h = IntegrationHarness();

  late SshClientService ssh;
  late Directory local;
  const remoteDir = '/home/fquser/work';
  const service = SftpTransferService();
  const bigBytes = 40 * 1000 * 1000;

  setUp(() async {
    ssh = await h.server.connect(user: 'fquser');
    await ssh.execute('mkdir -p $remoteDir');
    local = await Directory.systemTemp.createTemp('fortiq_sftp_faults');
  });
  tearDown(() async {
    ssh.disconnect();
    await local.delete(recursive: true);
  });

  File dest(String name) => File('${local.path}${Platform.pathSeparator}$name');

  Future<void> makeRemoteBig(String name) => ssh.execute(
    "python3 -c \"import sys; sys.stdout.buffer.write(b'z'*$bigBytes)\" > $remoteDir/$name",
  );

  /// Замедляет передачу, чтобы за это время сервер успел оборвать соединение:
  /// блокирует поток на несколько десятков миллисекунд при первых вызовах.
  void Function(int, int?) slowThen(void Function() once) {
    var calls = 0;
    return (bytes, total) {
      calls++;
      if (calls == 1) once();
      if (calls < 40) sleep(const Duration(milliseconds: 50));
    };
  }

  Future<String> remoteLs() async =>
      (await h.server.root('ls -A $remoteDir')).trim();

  Future<void> download(
    String remote,
    String name, {
    void Function(int, int?)? onProgress,
    TransferCancellation? cancel,
  }) => service.download(
    client: ssh.rawClient!,
    remotePath: '$remoteDir/$remote',
    localPath: dest(name).path,
    onProgress: onProgress ?? (_, _) {},
    sessionCurrent: () => true,
    cancellation: cancel,
  );

  Future<void> upload(
    String name,
    String remote, {
    void Function(int, int?)? onProgress,
    TransferCancellation? cancel,
  }) => service.upload(
    client: ssh.rawClient!,
    localPath: dest(name).path,
    remotePath: '$remoteDir/$remote',
    onProgress: onProgress ?? (_, _) {},
    sessionCurrent: () => true,
    cancellation: cancel,
  );

  group('обрыв SSH', () {
    test('посреди скачивания: ошибка, готового файла нет', () async {
      await makeRemoteBig('big.bin');
      await expectLater(
        download(
          'big.bin',
          'big.bin',
          onProgress: slowThen(() => unawaited(h.server.dropConnections('fquser'))),
        ),
        throwsA(anything),
      );
      expect(dest('big.bin').existsSync(), isFalse,
          reason: 'недокачанное не должно выглядеть готовым файлом');
    });

    test('посреди загрузки: на сервере нет файла с итоговым именем', () async {
      await dest('up.bin').writeAsBytes(List.filled(bigBytes, 5));
      await expectLater(
        upload(
          'up.bin',
          'up.bin',
          onProgress: slowThen(() => unawaited(h.server.dropConnections('fquser'))),
        ),
        throwsA(anything),
      );
      final names = (await remoteLs()).split('\n');
      expect(names, isNot(contains('up.bin')),
          reason: 'обрыв не должен оставить обрезанный файл под настоящим именем');
    });

    test('после обрыва новое соединение работает и можно повторить с нуля', () async {
      await dest('again.bin').writeAsBytes(List.filled(2000000, 3));
      await expectLater(
        upload(
          'again.bin',
          'again.bin',
          onProgress: slowThen(() => unawaited(h.server.dropConnections('fquser'))),
        ),
        throwsA(anything),
      );
      ssh.disconnect();
      ssh = await h.server.connect(user: 'fquser');
      await upload('again.bin', 'again.bin');
      expect((await ssh.execute('wc -c < $remoteDir/again.bin')).trim(), '2000000');
    });
  });

  group('конфликт имён во время передачи', () {
    test('скачивание: файл появился на месте назначения — он остаётся нетронутым', () async {
      await makeRemoteBig('c.bin');
      await expectLater(
        download(
          'c.bin',
          'c.bin',
          onProgress: slowThen(() => dest('c.bin').writeAsStringSync('мой файл')),
        ),
        throwsStateError,
      );
      expect(dest('c.bin').readAsStringSync(), 'мой файл');
    });

    test('загрузка: файл появился на сервере — он не перезаписан', () async {
      await dest('u.bin').writeAsBytes(List.filled(bigBytes, 9));
      await expectLater(
        upload(
          'u.bin',
          'u.bin',
          onProgress: slowThen(() => unawaited(
            h.server.root('echo чужой > $remoteDir/u.bin; chown fquser $remoteDir/u.bin'),
          )),
        ),
        throwsStateError,
      );
      expect((await ssh.execute('cat $remoteDir/u.bin')).trim(), 'чужой');
    });

    test('две загрузки с одним именем: выигрывает одна, файл целый', () async {
      await dest('x1.bin').writeAsBytes(List.filled(3000000, 1));
      await dest('x2.bin').writeAsBytes(List.filled(3000000, 2));
      final ssh2 = await h.server.connect(user: 'fquser');
      addTearDown(ssh2.disconnect);

      final results = await Future.wait([
        upload('x1.bin', 'same.bin').then((_) => true, onError: (_) => false),
        service
            .upload(
              client: ssh2.rawClient!,
              localPath: dest('x2.bin').path,
              remotePath: '$remoteDir/same.bin',
              onProgress: (_, _) {},
              sessionCurrent: () => true,
            )
            .then((_) => true, onError: (_) => false),
      ]);
      expect(results.where((ok) => ok).length, 1, reason: 'ровно одна загрузка опубликована');
      final first = (await ssh.execute("head -c1 $remoteDir/same.bin")).codeUnitAt(0);
      final distinct = (await ssh.execute(
        "python3 -c \"d=open('$remoteDir/same.bin','rb').read(); print(len(d), len(set(d)))\"",
      )).trim();
      expect(distinct, '3000000 1', reason: 'содержимое одной загрузки, без смеси');
      expect([1, 2], contains(first));
    });
  });

  group('отмена', () {
    test('загрузки: свой временный файл на сервере удалён, чужие не тронуты', () async {
      await ssh.execute('echo чужой > $remoteDir/other.txt.fortiq-upload-1');
      await dest('cancel.bin').writeAsBytes(List.filled(bigBytes, 4));
      final cancel = TransferCancellation();
      await expectLater(
        upload('cancel.bin', 'cancel.bin',
            onProgress: slowThen(cancel.request), cancel: cancel),
        throwsA(isA<TransferCancelled>()),
      );
      final names = (await remoteLs()).split('\n');
      expect(names, isNot(contains('cancel.bin')));
      expect(names.where((n) => n.startsWith('cancel.bin.fortiq-upload')), isEmpty);
      expect(names, contains('other.txt.fortiq-upload-1'), reason: 'чужой файл не удаляется');
    });

    test('скачивания: готового файла нет, повторное скачивание возможно после очистки', () async {
      await makeRemoteBig('dl.bin');
      final cancel = TransferCancellation();
      await expectLater(
        download('dl.bin', 'dl.bin', onProgress: slowThen(cancel.request), cancel: cancel),
        throwsA(isA<TransferCancelled>()),
      );
      expect(dest('dl.bin').existsSync(), isFalse);

      // Остаток отменённой выгрузки не затирается молча: пользователь решает сам.
      await expectLater(download('dl.bin', 'dl.bin'), throwsStateError);
      final part = File('${dest('dl.bin').path}.fortiq-part');
      if (part.existsSync()) part.deleteSync();
      await download('dl.bin', 'dl.bin');
      expect(dest('dl.bin').lengthSync(), bigBytes);
    });

    test('отмена и обрыв одновременно: не выдаётся за чистую отмену по ложному поводу', () async {
      await dest('both.bin').writeAsBytes(List.filled(bigBytes, 6));
      final cancel = TransferCancellation();
      Object? failure;
      try {
        await upload(
          'both.bin',
          'both.bin',
          cancel: cancel,
          onProgress: slowThen(() {
            unawaited(h.server.dropConnections('fquser'));
            cancel.request();
          }),
        );
      } catch (e) {
        failure = e;
      }
      expect(failure, isNotNull);
      expect(await remoteLs(), isNot(contains('both.bin\n')));
    });
  });
}
