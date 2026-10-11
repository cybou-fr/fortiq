import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/models/linux_models.dart';
import 'package:fortiq/models/server_profile.dart';
import 'package:fortiq/services/profile_service.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Модели данных и сохранение профилей серверов.
void main() {
  LinuxFileInfo file({int size = 0, bool dir = false}) => LinuxFileInfo(
    permissions: '-rw-r--r--',
    links: 1,
    owner: 'u',
    group: 'g',
    size: size,
    modified: DateTime(2026),
    name: 'f',
    isDirectory: dir,
    isLink: false,
  );

  group('LinuxFileInfo.formattedSize', () {
    test('байты, КБ, МБ, ГБ', () {
      expect(file(size: 0).formattedSize, '0 B');
      expect(file(size: 1023).formattedSize, '1023 B');
      expect(file(size: 1024).formattedSize, '1.0 KB');
      expect(file(size: 1536).formattedSize, '1.5 KB');
      expect(file(size: 5 * 1024 * 1024).formattedSize, '5.0 MB');
      expect(file(size: 3 * 1024 * 1024 * 1024).formattedSize, '3.0 GB');
    });

    test('у каталога размер не показывается', () {
      expect(file(size: 4096, dir: true).formattedSize, '--');
    });
  });

  group('LinuxSystemMetrics.memPercent', () {
    LinuxSystemMetrics m(int total, int used) => LinuxSystemMetrics(
      cpuPercent: 0,
      totalMemMb: total,
      usedMemMb: used,
      uptime: '',
      diskUsage: '',
    );

    test('доля занятой памяти', () => expect(m(2000, 500).memPercent, 25));
    test('нулевой объём не даёт деления на ноль', () => expect(m(0, 0).memPercent, 0));
  });

  test('LinuxService.isRunning только для active', () {
    LinuxService s(String active) =>
        LinuxService(unit: 'a', load: 'loaded', active: active, sub: '', description: '');
    expect(s('active').isRunning, isTrue);
    expect(s('inactive').isRunning, isFalse);
    expect(s('failed').isRunning, isFalse);
  });

  group('ServerProfile', () {
    test('JSON туда и обратно сохраняет все поля', () {
      final p = ServerProfile(
        id: '1',
        name: 'Боевой',
        host: 'vps.example',
        port: 2222,
        username: 'root',
        useKeyAuth: false,
        keyPath: r'C:\k\id',
        hasStoredKey: true,
        domainColor: const Color(0xFF123456),
      );
      final back = ServerProfile.fromJson(jsonDecode(jsonEncode(p.toJson())));
      expect(back.toJson(), p.toJson());
      expect(back.domainColor.toARGB32(), 0xFF123456);
    });

    test('значения по умолчанию для старых записей', () {
      final p = ServerProfile.fromJson({'id': '1', 'name': 'n', 'host': 'h', 'username': 'u'});
      expect(p.port, 22);
      expect(p.useKeyAuth, isTrue);
      expect(p.keyPath, isNull);
      expect(p.hasStoredKey, isFalse);
    });

    test('в профиль не попадает пароль или содержимое ключа', () {
      final json = ServerProfile(id: '1', name: 'n', host: 'h', username: 'u').toJson();
      expect(json.keys, isNot(contains('password')));
      expect(json.keys, isNot(contains('privateKey')));
    });
  });

  group('ProfileService', () {
    setUp(() => SharedPreferences.setMockInitialValues({}));

    test('пустое хранилище — пустой список', () async {
      expect(await ProfileService.loadProfiles(), isEmpty);
    });

    test('сохранённые профили читаются в том же порядке', () async {
      await ProfileService.saveProfiles([
        ServerProfile(id: 'a', name: 'A', host: 'a.example', username: 'u'),
        ServerProfile(id: 'b', name: 'B', host: 'b.example', username: 'u', port: 2200),
      ]);
      final list = await ProfileService.loadProfiles();
      expect(list.map((p) => p.id), ['a', 'b']);
      expect(list[1].port, 2200);
    });

    test('повторное сохранение заменяет список', () async {
      await ProfileService.saveProfiles([ServerProfile(id: 'a', name: 'A', host: 'h', username: 'u')]);
      await ProfileService.saveProfiles([]);
      expect(await ProfileService.loadProfiles(), isEmpty);
    });

    test('повреждённые данные не роняют приложение', () async {
      SharedPreferences.setMockInitialValues({'fortiq_saved_profiles': '{не json'});
      expect(await ProfileService.loadProfiles(), isEmpty);
      SharedPreferences.setMockInitialValues({'fortiq_saved_profiles': '[{"id":1}]'});
      expect(await ProfileService.loadProfiles(), isEmpty);
    });
  });
}
