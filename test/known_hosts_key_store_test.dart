import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/known_hosts_service.dart';
import 'package:fortiq/services/private_key_store.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Запоминание отпечатков хостов (TOFU) и хранилище импортированных ключей.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('KnownHostsService', () {
    setUp(() => SharedPreferences.setMockInitialValues({}));

    test('неизвестный хост — null', () async {
      expect(await KnownHostsService.lookup('a.example', 22), isNull);
    });

    test('запомненный отпечаток находится', () async {
      await KnownHostsService.remember('a.example', 22, 'ssh-ed25519 AA:BB');
      expect(await KnownHostsService.lookup('a.example', 22), 'ssh-ed25519 AA:BB');
    });

    test('имя хоста без учёта регистра', () async {
      await KnownHostsService.remember('A.Example', 22, 'k1');
      expect(await KnownHostsService.lookup('a.EXAMPLE', 22), 'k1');
    });

    test('порт — часть идентификатора: один хост на разных портах', () async {
      await KnownHostsService.remember('h', 22, 'k22');
      await KnownHostsService.remember('h', 2222, 'k2222');
      expect(await KnownHostsService.lookup('h', 22), 'k22');
      expect(await KnownHostsService.lookup('h', 2222), 'k2222');
      expect(await KnownHostsService.lookup('h', 23), isNull);
    });

    test('повторная запись заменяет отпечаток и не трогает остальные хосты', () async {
      await KnownHostsService.remember('a', 22, 'old');
      await KnownHostsService.remember('b', 22, 'other');
      await KnownHostsService.remember('a', 22, 'new');
      expect(await KnownHostsService.lookup('a', 22), 'new');
      expect(await KnownHostsService.lookup('b', 22), 'other');
    });

    test('повреждённые данные читаются как пустой список, запись их исправляет', () async {
      SharedPreferences.setMockInitialValues({'fortiq_known_hosts': 'не json'});
      expect(await KnownHostsService.lookup('a', 22), isNull);
      await KnownHostsService.remember('a', 22, 'k');
      expect(await KnownHostsService.lookup('a', 22), 'k');
    });
  });

  group('PrivateKeyStore', () {
    late Directory dir;

    setUp(() async {
      dir = await Directory.systemTemp.createTemp('fortiq_keys_test');
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger.setMockMethodCallHandler(
        const MethodChannel('plugins.flutter.io/path_provider'),
        (call) async => dir.path,
      );
    });
    tearDown(() async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger.setMockMethodCallHandler(
        const MethodChannel('plugins.flutter.io/path_provider'),
        null,
      );
      await dir.delete(recursive: true);
    });

    test('нет ключа — null', () async {
      expect(await PrivateKeyStore.load('p1'), isNull);
    });

    test('сохранённый ключ читается без изменений', () async {
      const pem = '-----BEGIN OPENSSH PRIVATE KEY-----\nabc\n-----END OPENSSH PRIVATE KEY-----\n';
      await PrivateKeyStore.save('p1', pem);
      expect(await PrivateKeyStore.load('p1'), pem);
    });

    test('ключи разных профилей не смешиваются, повторная запись заменяет', () async {
      await PrivateKeyStore.save('a', 'ka');
      await PrivateKeyStore.save('b', 'kb');
      await PrivateKeyStore.save('a', 'ka2');
      expect(await PrivateKeyStore.load('a'), 'ka2');
      expect(await PrivateKeyStore.load('b'), 'kb');
    });

    test('удаление убирает ключ; удаление отсутствующего не ошибка', () async {
      await PrivateKeyStore.save('a', 'ka');
      await PrivateKeyStore.delete('a');
      expect(await PrivateKeyStore.load('a'), isNull);
      await PrivateKeyStore.delete('a');
    });

    test('идентификатор не может вывести файл за пределы папки ключей', () async {
      await PrivateKeyStore.save('../../evil', 'k');
      final outside = dir.parent.listSync().whereType<File>().where((f) => f.path.contains('evil'));
      expect(outside, isEmpty);
      final keys = Directory('${dir.path}${Platform.pathSeparator}keys').listSync();
      expect(keys.single.path, endsWith('.key'));
      expect(await PrivateKeyStore.load('../../evil'), 'k');
    });
  });
}
