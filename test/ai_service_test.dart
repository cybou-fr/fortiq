import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/ai_service.dart';
import 'package:fortiq/services/shell.dart';

import 'fake_shell.dart';

const goodAnswer =
    '{"schema":"fortiq-ai-advice/v1","status":"ok","advice":"Check nginx -t",'
    '"cached":false,"age_seconds":0}';

void main() {
  test('explain uses SSH stdin, never a direct AI API or model on Windows', () async {
    final shell = FakeShell((_) => '')
      ..inputResult = (_, _) => const InputResult(
        exitCode: 0, stdout: goodAnswer,
      );
    final result = await AiService(shell).explain('nginx: failed');
    expect(result.text, 'Check nginx -t');
    expect(result.cached, isFalse);
    expect(shell.lastInputText, 'nginx: failed');
    expect(shell.commands.single, contains('ai explain --stdin --json'));
    expect(shell.commands.single, isNot(contains('nginx: failed')));
    expect(shell.commands.single, isNot(contains('--cache')));
  });

  test('cache opt-in is explicit for potentially sensitive server logs', () async {
    final shell = FakeShell((_) => '')
      ..inputResult = (_, _) => const InputResult(
        exitCode: 0,
        stdout: '{"schema":"fortiq-ai-advice/v1","status":"ok",'
            '"advice":"Cached guidance","cached":true,"age_seconds":42}',
      );
    final answer = await AiService(shell).explain('journal', allowServerCache: true);
    expect(answer.cached, isTrue);
    expect(answer.ageSeconds, 42);
    expect(shell.commands.single, contains('--cache --cache-sensitive'));
  });

  test('unknown exit is never considered success even if JSON looks valid', () async {
    final shell = FakeShell((_) => '')
      ..inputResult = (_, _) => const InputResult(
        exitCode: null, stdout: goodAnswer,
      );
    await expectLater(AiService(shell).explain('example'), throwsStateError);
  });

  test('nonzero exit with structured error becomes understandable UI text', () async {
    final shell = FakeShell((_) => '')
      ..inputResult = (_, _) => const InputResult(
        exitCode: 2,
        stdout: '{"status":"error","message":"model_missing"}',
      );
    await expectLater(
      AiService(shell).explain('example'),
      throwsA(isA<StateError>().having((e) => e.message, 'message', 'model_missing')),
    );
  });

  test('unexpected schema is an error, not a guessed answer', () async {
    final shell = FakeShell((_) => '')
      ..inputResult = (_, _) => const InputResult(
        exitCode: 0, stdout: '{"answer":"maybe"}',
      );
    await expectLater(AiService(shell).explain('test'), throwsFormatException);
  });

  test('disconnected SSH does not invoke AI', () async {
    final shell = FakeShell((_) => '')..isConnected = false;
    await expectLater(AiService(shell).explain('test'), throwsStateError);
    expect(shell.commands, isEmpty);
  });

  test('oversized input never travels to the server', () async {
    final shell = FakeShell((_) => '');
    await expectLater(AiService(shell).explain('x' * 8193), throwsArgumentError);
    expect(shell.commands, isEmpty);
  });

  test('service evidence is read-only and the unit name is checked', () async {
    final shell = FakeShell((_) => 'unit failed');
    final result = await AiService(shell).serviceEvidence('nginx.service');
    expect(result, contains('nginx.service'));
    expect(shell.commands.single, contains('journalctl'));
    expect(shell.commands.single, contains("'nginx.service'"));
    await expectLater(
      AiService(shell).serviceEvidence('nginx; reboot'),
      throwsArgumentError,
    );
    expect(shell.commands.length, 1);
  });
}
