import 'dart:typed_data';

import 'package:dartssh2/dartssh2.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/server_setup_service.dart';
import 'package:fortiq/services/shell.dart';
import 'package:fortiq/services/ssh_service.dart';

import 'support.dart';

/// Обёртка над настоящим соединением, у которой можно подменить результат
/// «второго входа» и признак входа по ключу. Всё остальное идёт на сервер.
class _Shell implements RemoteShell {
  final SshClientService inner;
  bool? secondLogin; // null — настоящая проверка
  bool? keyAuth;
  _Shell(this.inner, {this.secondLogin, this.keyAuth});

  @override
  bool get isConnected => inner.isConnected;
  @override
  SSHClient? get rawClient => inner.rawClient;
  @override
  bool get usedKeyAuth => keyAuth ?? inner.usedKeyAuth;
  @override
  Future<bool> canLoginAgain() async => secondLogin ?? await inner.canLoginAgain();
  @override
  Future<String> execute(String command) => inner.execute(command);
  @override
  Future<int> runStreaming(String command, void Function(String line) onLine) =>
      inner.runStreaming(command, onLine);
  @override
  Future<InputResult> executeWithInput(String command, Uint8List input) =>
      inner.executeWithInput(command, input);
}

/// Подготовка сервера (проверка, обновления, защита) на настоящем Debian.
/// Самое опасное место приложения: ошибка здесь может закрыть вход на сервер.
void main() {
  final h = IntegrationHarness();

  late SshClientService sudoUser; // fquser: sudo без пароля
  late SshClientService plainUser; // fqplain: без sudo

  setUp(() async {
    // «Свежий» сервер: ничего из того, что ставит Fortiq.
    await h.server.root(
      'ufw --force disable >/dev/null 2>&1; systemctl stop fail2ban 2>/dev/null; exit 0',
    );
    await h.server.purge(['ufw', 'fail2ban', 'unattended-upgrades', 'tmux']);
    await h.server.install(['iproute2', 'procps']);
    sudoUser = await h.server.connect(user: 'fquser');
    plainUser = await h.server.connect(user: 'fqplain');
  });
  tearDown(() async {
    sudoUser.disconnect();
    plainUser.disconnect();
    await h.server.root(
      'ufw --force disable >/dev/null 2>&1; systemctl stop "fq-listen-*" 2>/dev/null; exit 0',
    );
  });

  ServerSetupService setupFor(RemoteShell shell) =>
      ServerSetupService(shell, host: IntegrationServer.host, port: IntegrationServer.port);

  Future<String> sshdSetting(String name) async =>
      (await h.server.root('sshd -T | grep -i "^$name " || true')).trim();

  group('проверка (только чтение)', () {
    test('на свежем сервере находит всё, что нужно сделать, и ничего не меняет', () async {
      final setup = setupFor(sudoUser);
      await setup.inspect();

      expect(setup.error, isNull);
      final st = setup.status!;
      expect(st.osId, 'debian');
      expect(st.hasSudo, isTrue);
      expect(st.sshPort, IntegrationServer.port);
      expect(st.publicTcpPorts, contains(IntegrationServer.port));
      expect(st.missing.map((r) => r.package), contains('tmux'));
      expect(st.fail2banActive, isFalse);
      expect(st.unattendedOn, isFalse);
      expect(st.ufwActive, isFalse);
      expect(setup.hasWork, isTrue);

      expect(await h.server.root('dpkg -s ufw 2>&1 | head -1; exit 0'), contains('not installed'));
    });

    test('обычный пользователь без sudo: все шаги недоступны с причиной', () async {
      final setup = setupFor(plainUser);
      await setup.inspect();
      expect(setup.hasWork, isFalse);
      expect(setup.steps.every((s) => s.unavailable != null), isTrue);
      expect(setup.steps.first.unavailable, contains('root or passwordless sudo'));
    });
  });

  group('полная подготовка', () {
    test('ставит пакеты, включает защиту и проверяет вход заново', () async {
      final setup = setupFor(sudoUser);
      await setup.inspect();
      await setup.run();
      for (final st in setup.steps) {
        // ignore: avoid_print
        print('STEP ${st.id.name}: ${st.state.name} ${st.note ?? ''}');
      }
      // ignore: avoid_print
      print(setup.log.join('\n'));

      expect(setup.error, isNull);
      final problems = setup.steps.where((s) => s.state == StepStatus.failed || s.state == StepStatus.unknown);
      expect(problems, isEmpty, reason: setup.log.join('\n'));

      // Сервер действительно изменён.
      expect(await h.server.root('which tmux ufw fail2ban-client'), allOf(contains('tmux'), contains('ufw')));
      expect(await h.server.root('ufw status | head -1'), contains('active'));
      expect(await h.server.root('ufw status'), contains('${IntegrationServer.port}/tcp'));
      expect(await h.server.root('systemctl is-active fail2ban'), contains('active'));
      expect(await sshdSetting('passwordauthentication'), 'passwordauthentication no');

      // Вход после изменений работает и по ключу, и заново.
      expect((await sudoUser.execute('echo still-in')).trim(), 'still-in');
      expect(await sudoUser.canLoginAgain(), isTrue);
      final fresh = await h.server.connect(user: 'fquser');
      expect((await fresh.execute('echo new-login')).trim(), 'new-login');
      fresh.disconnect();

      // После работы проверка видит, что делать больше нечего.
      expect(setup.hasWork, isFalse);
    });

    test('повторный запуск ничего не меняет', () async {
      final setup = setupFor(sudoUser);
      await setup.inspect();
      await setup.run();

      final again = setupFor(sudoUser);
      await again.inspect();
      expect(again.steps.where((s) => s.unavailable == null && s.needed).map((s) => s.id), isEmpty);
      await again.run();
      expect(again.steps.every((s) => s.state == StepStatus.upToDate), isTrue);
      expect(again.log.where((l) => l.contains('apt-get install')), isEmpty);
    });

    test('работающий сервис остаётся доступным: его порт открыт в файрволе', () async {
      await h.server.root(
        'systemd-run --quiet --unit=fq-listen-web python3 -m http.server 8088 --bind 0.0.0.0; sleep 1',
      );
      final setup = setupFor(sudoUser);
      await setup.inspect();
      expect(setup.status!.publicTcpPorts, contains(8088));
      await setup.run();

      expect(await h.server.root('ufw status'), contains('8088/tcp'));
    });

    test('без iproute2 порты работающих сервисов всё равно не теряются', () async {
      // Свежий сервер без ss: проверка не видит порты, пока iproute2 не поставлен.
      await h.server.purge(['iproute2']);
      await h.server.root(
        'systemd-run --quiet --unit=fq-listen-web python3 -m http.server 8089 --bind 0.0.0.0; sleep 1',
      );
      final setup = setupFor(sudoUser);
      await setup.inspect();
      await setup.run();

      expect(await h.server.root('ufw status'), contains('8089/tcp'),
          reason: 'файрвол не должен отрезать сервис, который слушал до подготовки');
    });
  });

  group('защита от потери доступа', () {
    test('второй вход не удался после SSH: настройка откатывается, вход остаётся', () async {
      final shell = _Shell(sudoUser, secondLogin: false);
      final setup = setupFor(shell);
      await setup.inspect();
      await setup.run();

      final ssh = setup.steps.firstWhere((s) => s.id == SetupStepId.sshHardening);
      expect(ssh.state, StepStatus.failed);
      expect(ssh.note, contains('undone'));
      expect(await h.server.root('ls /etc/ssh/sshd_config.d/'), isNot(contains('00-fortiq-hardening.conf')));
      expect(await sshdSetting('passwordauthentication'), isNot('passwordauthentication no'));
      expect((await sudoUser.execute('echo alive')).trim(), 'alive');
    });

    test('после неудачи SSH следующие шаги не запускаются', () async {
      final setup = setupFor(_Shell(sudoUser, secondLogin: false));
      await setup.inspect();
      await setup.run();
      final fw = setup.steps.firstWhere((s) => s.id == SetupStepId.firewall);
      expect(fw.state, StepStatus.skipped);
      expect(await h.server.root('ufw status | head -1; exit 0'), isNot(contains('Status: active')));
    });

    test('второй вход не удался после файрвола: файрвол выключается', () async {
      // Первая проверка (после SSH) проходит, вторая (после файрвола) нет.
      final shell = _Shell(sudoUser);
      var calls = 0;
      final flaky = _FlakyLogin(shell, () => ++calls < 2);
      final setup = setupFor(flaky);
      await setup.inspect();
      await setup.run();

      final fw = setup.steps.firstWhere((s) => s.id == SetupStepId.firewall);
      expect(fw.state, StepStatus.failed);
      expect(await h.server.root('ufw status | head -1; exit 0'), isNot(contains('Status: active')));
      expect((await sudoUser.execute('echo alive')).trim(), 'alive');
    });

    test('вход по паролю: SSH-шаг недоступен, остальное выполняется', () async {
      final setup = setupFor(_Shell(sudoUser, keyAuth: false));
      await setup.inspect();
      final ssh = setup.steps.firstWhere((s) => s.id == SetupStepId.sshHardening);
      expect(ssh.unavailable, contains('SSH key'));
      await setup.run();
      expect(ssh.state, StepStatus.skipped);
      expect(await sshdSetting('passwordauthentication'), isNot('passwordauthentication no'));
      expect(await h.server.root('ufw status | head -1'), contains('active'));
    });

    test('пользователь без sudo ничего на сервере не меняет', () async {
      final setup = setupFor(plainUser);
      await setup.inspect();
      await setup.run();
      expect(setup.steps.every((s) => s.state == StepStatus.skipped), isTrue);
      expect(await h.server.root('dpkg -s ufw 2>&1 | head -1; exit 0'), contains('not installed'));
    });
  });
}

/// Второй вход, который удаётся не всегда: [ok] решает по номеру попытки.
class _FlakyLogin extends _Shell {
  final bool Function() ok;
  _FlakyLogin(_Shell base, this.ok) : super(base.inner);
  @override
  Future<bool> canLoginAgain() async => ok();
}
