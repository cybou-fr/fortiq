import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/shell.dart';
import 'package:fortiq/services/ssh_service.dart';
import 'package:fortiq/widgets/log_viewer.dart';
import 'package:fortiq/widgets/network_inspector.dart';

/// SSH-служба, отвечающая на команды из функции; всё остальное — настоящий код
/// сервисов, как в приложении.
class _Ssh extends SshClientService {
  final String Function(String command) answer;
  final List<String> commands = [];
  _Ssh(this.answer);

  @override
  bool get isConnected => true;

  @override
  Future<String> execute(String command) async {
    commands.add(command);
    return answer(command);
  }
}

Widget _host(Widget child) => MaterialApp(
  home: Scaffold(body: SizedBox(width: 1100, height: 700, child: child)),
);

const _ssOutput =
    'TOOL=ss\n'
    'tcp LISTEN 0 128 0.0.0.0:22 0.0.0.0:* users:(("sshd",pid=1,fd=3))\n'
    'tcp LISTEN 0 511 *:8080 *:* users:(("nginx",pid=2,fd=6))\n';

const _ufwOutput =
    'Status: active\n\n'
    '     To                         Action      From\n'
    '     --                         ------      ----\n'
    '[ 1] 22/tcp                     ALLOW IN    Anywhere\n'
    '[ 2] 8080                       DENY IN     Anywhere\n';

const _ipOutput =
    '1: lo    inet 127.0.0.1/8 scope host lo\\       valid_lft forever\n'
    '2: eth0    inet 10.0.0.5/24 brd 10.0.0.255 scope global eth0\\       valid_lft forever\n';

String _network(String c) {
  if (c.contains('TOOL=ss')) return _ssOutput;
  if (c.contains('ufw status')) return _ufwOutput;
  if (c.contains('ip -o addr')) return _ipOutput;
  return '';
}

Future<void> _openNetwork(WidgetTester tester, _Ssh ssh) async {
  await tester.pumpWidget(
    _host(RemoteNetworkInspector(sshService: ssh, domainColor: Colors.teal)),
  );
  await tester.pumpAndSettle();
}

Future<void> _openLogs(WidgetTester tester, _Ssh ssh) async {
  await tester.pumpWidget(
    _host(RemoteLogViewer(sshService: ssh, domainColor: Colors.teal)),
  );
  await tester.pumpAndSettle();
}

void main() {
  group('окно сети', () {
    testWidgets('порты: показывает протокол, адрес и процесс', (tester) async {
      await _openNetwork(tester, _Ssh(_network));
      expect(find.text('0.0.0.0:22'), findsOneWidget);
      expect(find.textContaining('sshd'), findsWidgets);
      expect(find.textContaining('nginx'), findsWidgets);
      expect(find.text('No listening ports detected'), findsNothing);
    });

    testWidgets('нет ss и netstat — ошибка с повтором, а не «портов нет»', (tester) async {
      await _openNetwork(
        tester,
        _Ssh((c) => c.contains('TOOL=ss') ? 'TOOL=none\n' : _network(c)),
      );
      expect(find.textContaining('Neither ss nor netstat'), findsOneWidget);
      expect(find.text('Retry'), findsOneWidget);
      expect(find.text('No listening ports detected'), findsNothing);
    });

    testWidgets('повтор после ошибки подхватывает исправленный сервер', (tester) async {
      var broken = true;
      await _openNetwork(
        tester,
        _Ssh((c) => broken && c.contains('TOOL=ss') ? 'TOOL=none\n' : _network(c)),
      );
      broken = false;
      await tester.tap(find.text('Retry'));
      await tester.pumpAndSettle();
      expect(find.text('0.0.0.0:22'), findsOneWidget);
    });

    testWidgets('сбой портов не прячет межсетевой экран и интерфейсы', (tester) async {
      await _openNetwork(
        tester,
        _Ssh((c) => c.contains('TOOL=ss') ? 'TOOL=none\n' : _network(c)),
      );
      await tester.tap(find.text('UFW Firewall'));
      await tester.pumpAndSettle();
      expect(find.text('Status: ACTIVE'), findsOneWidget);

      await tester.tap(find.text('Interfaces & IPs'));
      await tester.pumpAndSettle();
      expect(find.text('10.0.0.5/24'), findsOneWidget);
    });

    testWidgets('нет утилиты ip — ошибка на вкладке интерфейсов', (tester) async {
      await _openNetwork(
        tester,
        _Ssh((c) {
          if (c.contains('ip -o addr')) throw SshCommandException(c, 127, 'not found');
          return _network(c);
        }),
      );
      await tester.tap(find.text('Interfaces & IPs'));
      await tester.pumpAndSettle();
      expect(find.textContaining('ip command is not installed'), findsOneWidget);
      expect(find.text('No interfaces found'), findsNothing);
    });

    testWidgets('UFW: правила видны, добавление отправляет команду', (tester) async {
      final ssh = _Ssh(_network);
      await _openNetwork(tester, ssh);
      await tester.tap(find.text('UFW Firewall'));
      await tester.pumpAndSettle();

      expect(find.text('22/tcp'), findsOneWidget);
      expect(find.text('8080'), findsOneWidget);

      await tester.enterText(find.byType(TextField), '443');
      await tester.tap(find.text('Add Rule'));
      await tester.pumpAndSettle();
      expect(ssh.commands.any((c) => c.contains('443') && c.contains('allow')), isTrue);
    });

    testWidgets('UFW: удаление правила по номеру', (tester) async {
      final ssh = _Ssh(_network);
      await _openNetwork(tester, ssh);
      await tester.tap(find.text('UFW Firewall'));
      await tester.pumpAndSettle();

      await tester.tap(find.byTooltip('Delete Rule').last);
      await tester.pumpAndSettle();
      expect(ssh.commands.any((c) => c.contains('delete') && c.contains('2')), isTrue);
    });

    testWidgets('UFW: отказ сервера при добавлении показан пользователю', (tester) async {
      await _openNetwork(
        tester,
        _Ssh((c) {
          if (c.contains('allow') && c.contains('99999')) {
            throw SshCommandException(c, 1, 'ERROR: Bad port');
          }
          return _network(c);
        }),
      );
      await tester.tap(find.text('UFW Firewall'));
      await tester.pumpAndSettle();
      await tester.enterText(find.byType(TextField), '99999');
      await tester.tap(find.text('Add Rule'));
      await tester.pumpAndSettle();
      expect(find.textContaining('Add firewall rule failed'), findsOneWidget);
    });

    testWidgets('UFW не установлен — предложение установки', (tester) async {
      await _openNetwork(
        tester,
        _Ssh((c) => c.contains('ufw status') ? 'NOT_INSTALLED\n' : _network(c)),
      );
      await tester.tap(find.text('UFW Firewall'));
      await tester.pumpAndSettle();
      expect(find.textContaining('not installed'), findsOneWidget);
      expect(find.textContaining('Install UFW'), findsOneWidget);
    });

    testWidgets('UFW без sudo — сообщение о правах, а не «не установлен»', (tester) async {
      await _openNetwork(
        tester,
        _Ssh((c) => c.contains('ufw status') ? 'NEEDS_SUDO\n' : _network(c)),
      );
      await tester.tap(find.text('UFW Firewall'));
      await tester.pumpAndSettle();
      expect(find.textContaining('needs root'), findsOneWidget);
    });
  });

  group('просмотр журналов', () {
    const journal =
        'Oct 11 10:00:00 host sshd[1]: Accepted publickey for root\n'
        'Oct 11 10:00:01 host kernel: ERROR disk failure\n'
        'Oct 11 10:00:02 host app: warning low memory\n';

    testWidgets('показывает строки и счётчик', (tester) async {
      await _openLogs(tester, _Ssh((_) => journal));
      expect(find.textContaining('Accepted publickey'), findsOneWidget);
      expect(find.textContaining('Showing 3 of 3 lines'), findsOneWidget);
    });

    testWidgets('фильтр по тексту сужает список', (tester) async {
      await _openLogs(tester, _Ssh((_) => journal));
      await tester.enterText(find.byType(TextField), 'publickey');
      await tester.pump();
      expect(find.textContaining('Showing 1 of 3 lines'), findsOneWidget);
      expect(find.textContaining('disk failure'), findsNothing);
    });

    testWidgets('фильтр по уровню ERROR', (tester) async {
      await _openLogs(tester, _Ssh((_) => journal));
      // первая «ERROR» — кнопка фильтра в панели, остальные — метки строк
      await tester.tap(find.text('ERROR').first);
      await tester.pump();
      expect(find.textContaining('disk failure'), findsOneWidget);
      expect(find.textContaining('Accepted publickey'), findsNothing);
    });

    testWidgets('ничего не подходит — понятная пустая страница', (tester) async {
      await _openLogs(tester, _Ssh((_) => journal));
      await tester.enterText(find.byType(TextField), 'такого нет');
      await tester.pump();
      expect(find.text('No log entries matching criteria'), findsOneWidget);
    });

    testWidgets('ошибка чтения показана с повтором, загрузка не зависает', (tester) async {
      var broken = true;
      await _openLogs(
        tester,
        _Ssh((_) {
          if (broken) throw Exception('Not connected to SSH server');
          return journal;
        }),
      );
      expect(find.textContaining('Not connected to SSH server'), findsOneWidget);
      expect(find.byType(CircularProgressIndicator), findsNothing);

      broken = false;
      await tester.tap(find.text('Retry'));
      await tester.pumpAndSettle();
      expect(find.textContaining('Accepted publickey'), findsOneWidget);
    });

    testWidgets('смена источника перезагружает журнал нужной командой', (tester) async {
      final ssh = _Ssh((_) => journal);
      await _openLogs(tester, ssh);
      await tester.tap(find.byType(DropdownButton<String>));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Kernel / Hardware (dmesg)').last);
      await tester.pumpAndSettle();
      expect(ssh.commands.last, contains('dmesg'));
    });

    testWidgets('автообновление опрашивает сервер каждые 4 с и выключается', (tester) async {
      final ssh = _Ssh((_) => journal);
      await _openLogs(tester, ssh);
      final before = ssh.commands.length;

      await tester.tap(find.text('Live (4s)'));
      await tester.pump();
      expect(find.text('STREAMING LIVE'), findsOneWidget);
      await tester.pump(const Duration(seconds: 9));
      expect(ssh.commands.length, greaterThan(before));

      await tester.tap(find.text('Live (4s)'));
      await tester.pump();
      final after = ssh.commands.length;
      await tester.pump(const Duration(seconds: 9));
      expect(ssh.commands.length, after);
    });

    testWidgets('закрытие окна во время загрузки не падает', (tester) async {
      await tester.pumpWidget(
        _host(RemoteLogViewer(sshService: _Ssh((_) => journal), domainColor: Colors.teal)),
      );
      await tester.pumpWidget(_host(const SizedBox()));
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
    });
  });
}
