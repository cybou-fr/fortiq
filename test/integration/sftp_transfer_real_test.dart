import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/desktop_task_controller.dart';
import 'package:fortiq/services/sftp_transfer_service.dart';
import 'package:fortiq/services/ssh_service.dart';

import 'support.dart';

/// Скачивание файла с настоящего сервера по SFTP внутри той же SSH-сессии:
/// содержимое, прогресс, отказ перезаписывать и сохранность частичного файла.
void main() {
  final h = IntegrationHarness();

  late SshClientService ssh;
  late Directory local;
  const remoteDir = '/home/fquser/work';
  const service = SftpTransferService();

  setUp(() async {
    ssh = await h.server.connect(user: 'fquser');
    await ssh.execute('mkdir -p $remoteDir');
    local = await Directory.systemTemp.createTemp('fortiq_sftp_test');
  });
  tearDown(() async {
    ssh.disconnect();
    await local.delete(recursive: true);
  });

  Future<void> download(
    String remote,
    String name, {
    List<(int, int?)>? progress,
    bool Function()? current,
  }) => service.download(
    client: ssh.rawClient!,
    remotePath: remote,
    localPath: '${local.path}${Platform.pathSeparator}$name',
    onProgress: (b, t) => progress?.add((b, t)),
    sessionCurrent: current ?? () => true,
  );

  File dest(String name) => File('${local.path}${Platform.pathSeparator}$name');

  test('файл с любыми байтами приходит без изменений, прогресс доходит до размера', () async {
    // 3 МБ: все значения байта, чтобы ловить порчу двоичных данных
    await ssh.execute(
      "python3 -c \"import sys; sys.stdout.buffer.write(bytes(range(256))*12288)\" > $remoteDir/blob.bin",
    );
    final progress = <(int, int?)>[];
    await download('$remoteDir/blob.bin', 'blob.bin', progress: progress);

    final got = await dest('blob.bin').readAsBytes();
    expect(got.length, 256 * 12288);
    expect(got.sublist(0, 256), List.generate(256, (i) => i));
    expect(progress, isNotEmpty);
    expect(progress.last.$1, got.length);
    expect(progress.last.$2, got.length);
    for (var i = 1; i < progress.length; i++) {
      expect(progress[i].$1, greaterThanOrEqualTo(progress[i - 1].$1), reason: 'прогресс не идёт назад');
    }
    expect(File('${dest('blob.bin').path}.fortiq-part').existsSync(), isFalse,
        reason: 'временный файл переименован в итоговый');
  });

  test('пустой файл и имя с пробелами и кириллицей', () async {
    await ssh.execute("touch '$remoteDir/пустой файл.txt'");
    await download('$remoteDir/пустой файл.txt', 'пустой файл.txt');
    expect(await dest('пустой файл.txt').length(), 0);
  });

  test('существующий локальный файл не перезаписывается', () async {
    await ssh.execute("echo remote > $remoteDir/a.txt");
    await dest('a.txt').writeAsString('мой файл');
    await expectLater(download('$remoteDir/a.txt', 'a.txt'), throwsStateError);
    expect(await dest('a.txt').readAsString(), 'мой файл');
  });

  test('остаток прошлой неудачной выгрузки не затирается', () async {
    await ssh.execute("echo remote > $remoteDir/b.txt");
    await File('${dest('b.txt').path}.fortiq-part').writeAsString('остаток');
    await expectLater(download('$remoteDir/b.txt', 'b.txt'), throwsStateError);
    expect(await File('${dest('b.txt').path}.fortiq-part').readAsString(), 'остаток');
    expect(dest('b.txt').existsSync(), isFalse);
  });

  test('каталог и несуществующий файл — ошибка, итогового файла нет', () async {
    await expectLater(download(remoteDir, 'dir'), throwsStateError);
    await expectLater(download('$remoteDir/нет-такого', 'x'), throwsA(anything));
    expect(dest('dir').existsSync(), isFalse);
    expect(dest('x').existsSync(), isFalse);
  });

  test('недопустимые пути отвергаются до обращения к серверу', () async {
    await expectLater(download('', 'x'), throwsArgumentError);
    await expectLater(download('$remoteDir/a\x00b', 'x'), throwsArgumentError);
  });

  test('сессия сменилась во время скачивания: итогового файла нет', () async {
    await ssh.execute(
      "python3 -c \"import sys; sys.stdout.buffer.write(b'x'*2000000)\" > $remoteDir/c.bin",
    );
    var calls = 0;
    await expectLater(
      download('$remoteDir/c.bin', 'c.bin', current: () => ++calls < 2),
      throwsStateError,
    );
    expect(dest('c.bin').existsSync(), isFalse, reason: 'чужую сессию за успех не принимаем');
  });

  test('отмена скачивания: итогового файла нет', () async {
    await ssh.execute(
      "python3 -c \"import sys; sys.stdout.buffer.write(b'x'*4000000)\" > $remoteDir/d.bin",
    );
    final cancel = TransferCancellation();
    await expectLater(
      service.download(
        client: ssh.rawClient!,
        remotePath: '$remoteDir/d.bin',
        localPath: dest('d.bin').path,
        onProgress: (b, t) => cancel.request(),
        sessionCurrent: () => true,
        cancellation: cancel,
      ),
      throwsA(isA<TransferCancelled>()),
    );
    expect(dest('d.bin').existsSync(), isFalse);
  });

  group('загрузка на сервер', () {
    Future<void> upload(
      File source,
      String remote, {
      TransferCancellation? cancel,
      bool Function()? current,
    }) => service.upload(
      client: ssh.rawClient!,
      localPath: source.path,
      remotePath: remote,
      onProgress: (b, t) {},
      sessionCurrent: current ?? () => true,
      cancellation: cancel,
    );

    Future<File> localFile(String name, List<int> bytes) async =>
        (await dest(name).writeAsBytes(bytes));

    test('содержимое доходит без изменений, временных файлов не остаётся', () async {
      final bytes = List.generate(1500000, (i) => i % 251);
      final src = await localFile('up.bin', bytes);
      await upload(src, '$remoteDir/up.bin');

      final sum = (await ssh.execute('cksum < $remoteDir/up.bin')).trim().split(' ');
      expect(int.parse(sum[1]), bytes.length);
      expect(await ssh.execute('ls -A $remoteDir | grep fortiq-upload || true'), isEmpty);
    });

    test('имя с пробелами, кавычкой и кириллицей — одно обычное имя', () async {
      final src = await localFile('s.txt', 'привет'.codeUnits);
      const name = "$remoteDir/мой файл 'x'; touch pwn.txt";
      await upload(src, name);
      expect(await ssh.execute('test -e $remoteDir/pwn.txt && echo есть || echo нет'), contains('нет'));
      expect(await ssh.execute("ls $remoteDir | grep -c 'мой файл'"), contains('1'));
    });

    test('существующий файл на сервере не перезаписывается', () async {
      await ssh.execute('echo original > $remoteDir/keep.txt');
      final src = await localFile('k.txt', 'новое'.codeUnits);
      await expectLater(upload(src, '$remoteDir/keep.txt'), throwsStateError);
      expect((await ssh.execute('cat $remoteDir/keep.txt')).trim(), 'original');
    });

    test('отмена: файла на сервере нет', () async {
      final src = await localFile('c.bin', List.filled(3000000, 7));
      final cancel = TransferCancellation()..request();
      await expectLater(
        upload(src, '$remoteDir/cancelled.bin', cancel: cancel),
        throwsA(isA<TransferCancelled>()),
      );
      expect(await ssh.execute('ls $remoteDir | grep -c "^cancelled.bin\$" || true'), contains('0'));
    });

    test('в каталог без прав записи — ошибка, ничего не создано', () async {
      final src = await localFile('r.txt', 'x'.codeUnits);
      await expectLater(upload(src, '/root/nope.txt'), throwsA(anything));
    });

    test('сессия сменилась: загрузка не публикуется', () async {
      final src = await localFile('m.bin', List.filled(2000000, 1));
      var calls = 0;
      await expectLater(
        upload(src, '$remoteDir/moved.bin', current: () => ++calls < 3),
        throwsStateError,
      );
      expect(await ssh.execute('ls $remoteDir | grep -c "^moved.bin\$" || true'), contains('0'));
    });
  });
}
