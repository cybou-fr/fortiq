import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/himalaya_mail_service.dart';
import 'package:fortiq/services/shell.dart';

import 'fake_shell.dart';

/// Почта через Himalaya CLI на сервере: разбор JSON, экранирование аргументов,
/// проверка версии и честные ошибки отправки.
void main() {
  group('inspect', () {
    test('принимает Himalaya 1.x и возвращает версию', () async {
      final shell = FakeShell((_) => 'himalaya 1.1.0 +imap +smtp\n');
      expect(await HimalayaMailService(shell).inspect(), 'himalaya 1.1.0 +imap +smtp');
    });

    test('отвергает другую мажорную версию', () async {
      final shell = FakeShell((_) => 'himalaya 0.9.0\n');
      await expectLater(HimalayaMailService(shell).inspect(), throwsStateError);
    });

    test('без соединения сразу говорит об этом, не выполняя команд', () async {
      final shell = FakeShell((_) => '')..isConnected = false;
      await expectLater(HimalayaMailService(shell).inspect(), throwsStateError);
      expect(shell.commands, isEmpty);
    });
  });

  group('разбор ответов', () {
    test('аккаунты: имя и признак по умолчанию', () async {
      final shell = FakeShell((_) => '[{"name":"work","default":true},{"name":"home"}]');
      final list = await HimalayaMailService(shell).accounts();
      expect(list.map((a) => a.name), ['work', 'home']);
      expect(list.map((a) => a.isDefault), [true, false]);
    });

    test('аккаунт без имени — ошибка формата', () async {
      final shell = FakeShell((_) => '[{"default":true}]');
      await expectLater(HimalayaMailService(shell).accounts(), throwsFormatException);
    });

    test('папки', () async {
      final shell = FakeShell((_) => '[{"name":"INBOX"},{"name":"Sent"}]');
      expect(await HimalayaMailService(shell).folders('work'), ['INBOX', 'Sent']);
    });

    test('не-JSON от клиента даёт понятную ошибку, а не падение разбора', () async {
      final shell = FakeShell((_) => 'Error: boom');
      await expectLater(HimalayaMailService(shell).accounts(), throwsFormatException);
    });

    test('ответ не списком — неподдерживаемый формат', () async {
      final shell = FakeShell((_) => '{"a":1}');
      await expectLater(HimalayaMailService(shell).folders('x'), throwsFormatException);
    });

    test('письма: отправитель, флаги, вложение', () async {
      final shell = FakeShell((_) => '''[
        {"id":"7","subject":"Привет","from":{"name":"Аня","addr":"a@x.ru"},
         "to":[{"addr":"me@x.ru"}],"date":"2026-10-11","flags":["Seen"],"has_attachment":true},
        {"id":"8","subject":null,"from":"b@x.ru","flags":[]}
      ]''');
      final list = await HimalayaMailService(shell).envelopes('work', 'INBOX', 1);
      expect(list[0].from, 'Аня <a@x.ru>');
      expect(list[0].to, 'me@x.ru');
      expect(list[0].unread, isFalse);
      expect(list[0].hasAttachment, isTrue);
      expect(list[1].subject, '');
      expect(list[1].unread, isTrue);
    });

    test('идентификатор письма с посторонними символами отвергается', () async {
      final shell = FakeShell((_) => '[{"id":"1; rm -rf ~"}]');
      await expectLater(
        HimalayaMailService(shell).envelopes('a', 'INBOX', 1),
        throwsFormatException,
      );
    });

    test('страница меньше 1 — ошибка аргумента, команда не уходит на сервер', () async {
      final shell = FakeShell((_) => '[]');
      await expectLater(
        HimalayaMailService(shell).envelopes('a', 'INBOX', 0),
        throwsArgumentError,
      );
      expect(shell.commands, isEmpty);
    });
  });

  group('команды', () {
    test('аргументы экранируются как отдельные слова', () async {
      final shell = FakeShell((_) => '[]');
      await HimalayaMailService(shell).envelopes("a'; reboot; '", 'Входящие', 2);
      final cmd = shell.commands.single;
      expect(cmd, contains(shQuote("a'; reboot; '")));
      expect(cmd, contains(shQuote('Входящие')));
      expect(cmd, contains("'--page' '2'"));
      expect(cmd, contains('--output'));
      expect(cmd, contains('timeout 90s'));
    });

    test('чтение письма: только числовой id', () async {
      final shell = FakeShell((_) => '"текст письма"');
      final service = HimalayaMailService(shell);
      expect(await service.read('a', 'INBOX', '5'), 'текст письма');
      // id проверяется синхронно, до обращения к серверу
      expect(() => service.read('a', 'INBOX', '5; id'), throwsFormatException);
    });

    test('ответ-не-строка при чтении — ошибка формата', () async {
      final shell = FakeShell((_) => '[1]');
      await expectLater(HimalayaMailService(shell).read('a', 'INBOX', '1'), throwsFormatException);
    });

    test('шаблон ответа использует reply, нового письма — write', () async {
      final shell = FakeShell((_) => '"From: a\\n\\nbody"');
      final service = HimalayaMailService(shell);
      await service.template('a');
      await service.template('a', folder: 'INBOX', replyTo: '3');
      expect(shell.commands[0], contains("'write'"));
      expect(shell.commands[1], contains("'reply'"));
    });

    test('перемещение и снятие флага «прочитано»', () async {
      final shell = FakeShell((_) => 'null');
      final service = HimalayaMailService(shell);
      await service.move('a', 'INBOX', '4', 'Archive');
      await service.markUnread('a', 'INBOX', '4');
      expect(shell.commands[0], contains("'move'"));
      expect(shell.commands[1], contains("'remove'"));
      expect(shell.commands[1], contains("'seen'"));
    });
  });

  group('отправка', () {
    MailDraft draft() => MailDraft.fromTemplate(
      'From: me@x.ru\nTo: you@x.ru\nSubject: Тема\n\nТекст',
    );

    test('успех: письмо передаётся на stdin, длина проверяется на сервере', () async {
      final shell = FakeShell((_) => '')..inputResult = (_, _) => const InputResult(exitCode: 0);
      await HimalayaMailService(shell).send('work', draft());
      expect(shell.commands.single, contains('wc -c'));
      expect(shell.commands.single, contains('message'));
      expect(shell.lastInputText, contains('To: you@x.ru'));
    });

    test('ненулевой код — ошибка с текстом stderr, а не молчаливый успех', () async {
      final shell = FakeShell((_) => '')
        ..inputResult = (_, _) => const InputResult(exitCode: 1, stderr: 'smtp down');
      await expectLater(
        HimalayaMailService(shell).send('work', draft()),
        throwsA(isA<StateError>().having((e) => e.message, 'message', contains('smtp down'))),
      );
    });

    test('обрыв сигналом (потеря связи) тоже считается неподтверждённой отправкой', () async {
      final shell = FakeShell((_) => '')
        ..inputResult = (_, _) => const InputResult(killedBySignal: true);
      await expectLater(HimalayaMailService(shell).send('work', draft()), throwsStateError);
    });

    test('без соединения ничего не отправляется', () async {
      final shell = FakeShell((_) => '')..isConnected = false;
      await expectLater(HimalayaMailService(shell).send('work', draft()), throwsStateError);
      expect(shell.inputs, isEmpty);
    });
  });

  group('MailDraft', () {
    test('разбор шаблона: заголовки, склейка продолжений, тело', () {
      final d = MailDraft.fromTemplate(
        'From: me@x.ru\r\nTo: a@x.ru,\r\n b@x.ru\r\nSubject: Hi\r\n\r\nbody\r\nline2',
      );
      expect(d.to, 'a@x.ru, b@x.ru');
      expect(d.subject, 'Hi');
      expect(d.body, 'body\nline2');
    });

    test('шаблон без пустой строки — ошибка', () {
      expect(() => MailDraft.fromTemplate('From: a'), throwsFormatException);
    });

    test('перевод строки в теме запрещён (инъекция заголовков)', () {
      final d = MailDraft.fromTemplate('From: me@x.ru\nTo: a@x.ru\n\nx')
        ..subject = 'a\r\nBcc: evil@x.ru';
      expect(d.toMessage, throwsFormatException);
    });

    test('нужны отправитель и получатель', () {
      expect(MailDraft({'from': 'me@x.ru'}).toMessage, throwsFormatException);
      expect(MailDraft({}, to: 'a@x.ru').toMessage, throwsFormatException);
    });

    test('сообщение: base64-тело, кодированная тема, CRLF', () {
      final d = MailDraft.fromTemplate('From: me@x.ru\nTo: a@x.ru\nCc: c@x.ru\n\nx')
        ..subject = 'Длинная тема на русском языке, которую придётся разбить на слова'
        ..body = 'Привет';
      final text = d.toMessage();
      expect(text, contains('\r\n'));
      expect(text, contains('Content-Transfer-Encoding: base64'));
      expect(text, contains('cc: c@x.ru'));
      expect(text, contains('=?UTF-8?B?'));
      expect(text, isNot(contains('Привет')));
      expect(text, contains('0J/RgNC40LLQtdGC')); // base64 от «Привет»
    });
  });
}
