import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/ssh_service.dart';
import 'package:fortiq/widgets/ai_advice_dialog.dart';

/// Без настоящего SSH-клиента (rawClient == null): окно обязано отказать и
/// ничего не отправлять, а не молча «работать».
class _Offline extends SshClientService {
  @override
  bool get isConnected => false;
}

Widget _app(SshClientService ssh, {required void Function() onLoad}) => MaterialApp(
  home: Builder(
    builder: (context) => Scaffold(
      body: Center(
        child: ElevatedButton(
          onPressed: () => showAiAdviceDialog(
            context: context,
            ssh: ssh,
            title: 'Explain logs',
            loadContext: () async {
              onLoad();
              return 'контекст';
            },
          ),
          child: const Text('open'),
        ),
      ),
    ),
  ),
);

void main() {
  testWidgets('кэш на сервере выключен по умолчанию и объяснён пользователю', (tester) async {
    await tester.pumpWidget(_app(_Offline(), onLoad: () {}));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    final box = tester.widget<CheckboxListTile>(find.byType(CheckboxListTile));
    expect(box.value, isFalse);
    expect(find.textContaining('May store sensitive information'), findsOneWidget);
    expect(find.textContaining('cannot change'), findsOneWidget);
  });

  testWidgets('без SSH запрос не отправляется и контекст не собирается', (tester) async {
    var loaded = false;
    await tester.pumpWidget(_app(_Offline(), onLoad: () => loaded = true));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    await tester.tap(find.text('Explain'));
    await tester.pumpAndSettle();

    expect(find.text('Reconnect SSH to use server AI.'), findsOneWidget);
    expect(loaded, isFalse);
  });

  testWidgets('окно закрывается кнопкой Close', (tester) async {
    await tester.pumpWidget(_app(_Offline(), onLoad: () {}));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Close'));
    await tester.pumpAndSettle();
    expect(find.byType(AlertDialog), findsNothing);
  });
}
