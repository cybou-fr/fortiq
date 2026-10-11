import 'dart:convert';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/shell.dart';
import 'package:fortiq/services/ssh_service.dart';

import 'support.dart';

/// Files on a real server: listing, reading, writing, searching, deleting.
void main() {
  final h = IntegrationHarness();

  late SshClientService ssh; // fquser: normal user with sudo
  late SshClientService plain; // fqplain: normal user without sudo
  const work = '/home/fquser/work';
  const plainWork = '/home/fqplain/work';

  setUp(() async {
    ssh = await h.server.connect(user: 'fquser');
    plain = await h.server.connect(user: 'fqplain');
    await ssh.execute('mkdir -p $work');
    await plain.execute('mkdir -p $plainWork');
  });
  tearDown(() {
    ssh.disconnect();
    plain.disconnect();
  });

  Future<String> stat(String path, {SshClientService? as}) async =>
      (await (as ?? ssh).execute("stat -c '%a %U %i' ${shQuote(path)}")).trim();

  group('listing a folder', () {
    test('every kind of name survives', () async {
      await ssh.execute(r'''
cd /home/fquser/work
touch 'plain.txt' 'two  spaces.txt' "it's quoted.txt" 'dollar $HOME.txt' 'back`tick`.txt' 'tab	in name.txt' '.hidden' 'файл ✓.txt'
touch "$(printf 'new\nline.txt')"
mkdir 'a folder' emptydir
''');
      final files = await ssh.files.listDirectory(work);
      final names = files.map((f) => f.name).toSet();

      expect(names, {
        'plain.txt',
        'two  spaces.txt',
        "it's quoted.txt",
        r'dollar $HOME.txt',
        'back`tick`.txt',
        'tab\tin name.txt',
        '.hidden',
        'файл ✓.txt',
        'new\nline.txt',
        'a folder',
        'emptydir',
      });
      expect(files.length, 11, reason: 'no entry lost, none doubled, no "." or ".."');
    });

    test('folders come first, then files, each sorted without regard to case', () async {
      await ssh.execute('cd $work; touch b.txt A.txt c.txt; mkdir Zdir adir');
      final names = (await ssh.files.listDirectory(work)).map((f) => f.name).toList();
      expect(names, ['adir', 'Zdir', 'A.txt', 'b.txt', 'c.txt']);
    });

    test('sizes, owner, group, permissions, link count and time are right', () async {
      await ssh.execute('cd $work; printf 12345 > five.bin; chmod 640 five.bin; touch -d @1700000000 five.bin');
      final f = (await ssh.files.listDirectory(work)).single;

      expect(f.size, 5);
      expect(f.owner, 'fquser');
      expect(f.group, 'fquser');
      expect(f.permissions, '-rw-r-----');
      expect(f.links, 1);
      expect(f.isDirectory, isFalse);
      expect(f.isLink, isFalse);
      expect(f.linkTarget, isNull);
      expect(f.modified.toUtc().millisecondsSinceEpoch, 1700000000000);
      expect(f.formattedSize, '5 B');
    });

    test('links: to a folder (opens like a folder), to a file, and a broken one', () async {
      await ssh.execute('''
cd $work
mkdir realdir; touch realfile
ln -s realdir dirlink
ln -s realfile filelink
ln -s nothing-here broken
''');
      final byName = {for (final f in await ssh.files.listDirectory(work)) f.name: f};

      expect(byName['dirlink']!.isLink, isTrue);
      expect(byName['dirlink']!.isDirectory, isTrue, reason: 'a link to a folder is navigated like one');
      expect(byName['dirlink']!.linkTarget, 'realdir');
      expect(byName['filelink']!.isLink, isTrue);
      expect(byName['filelink']!.isDirectory, isFalse);
      expect(byName['filelink']!.linkTarget, 'realfile');
      expect(byName['broken']!.isLink, isTrue);
      expect(byName['broken']!.isDirectory, isFalse);
      expect(byName['broken']!.linkTarget, 'nothing-here');
      expect(byName['realdir']!.isLink, isFalse);
    });

    test('a link to a folder can itself be listed', () async {
      await ssh.execute('cd $work; mkdir real; touch real/inside.txt; ln -s real viaLink');
      final inside = await ssh.files.listDirectory('$work/viaLink');
      expect(inside.map((f) => f.name), ['inside.txt']);
    });

    test('an empty folder is an empty list', () async {
      await ssh.execute('mkdir -p $work/empty');
      expect(await ssh.files.listDirectory('$work/empty'), isEmpty);
    });

    test('a folder that does not exist is an error with a reason, not an empty list', () async {
      await expectLater(
        ssh.files.listDirectory('$work/missing'),
        throwsA(isA<SshCommandException>().having((e) => '$e', 'message', contains('No such file'))),
      );
    });

    test('a folder the user may not read is an error', () async {
      await plain.execute('mkdir -p $plainWork/secret && chmod 000 $plainWork/secret');
      await expectLater(
        plain.files.listDirectory('$plainWork/secret'),
        throwsA(isA<SshCommandException>().having((e) => '$e', 'message', contains('Permission denied'))),
      );
      await plain.execute('chmod 755 $plainWork/secret');
    });

    test('the root folder lists', () async {
      final names = (await ssh.files.listDirectory('/')).map((f) => f.name);
      expect(names, containsAll(['etc', 'home', 'usr', 'var']));
    });

    test('a large folder is listed completely', () async {
      await ssh.execute('cd $work; for i in \$(seq 1 3000); do : > "file_\$i.txt"; done');
      expect((await ssh.files.listDirectory(work)).length, 3000);
    });
  });

  group('creating and deleting', () {
    test('a nested folder is created in one go, and creating it again is fine', () async {
      await ssh.files.createDirectory('$work/a/b/c');
      await ssh.files.createDirectory('$work/a/b/c');
      expect((await ssh.files.listDirectory('$work/a/b')).single.name, 'c');
    });

    test('a folder with a hostile name is created as that exact name', () async {
      const name = r'''$(touch pwned); `touch pwned2`; 'x' "; rm -rf ~ #''';
      await ssh.files.createDirectory('$work/$name');
      final names = (await ssh.files.listDirectory(work)).map((f) => f.name);
      expect(names, [name], reason: 'only the folder exists: nothing was executed');
    });

    test('a file is deleted', () async {
      await ssh.execute('touch $work/x.txt');
      await ssh.files.remove('$work/x.txt');
      expect(await ssh.files.listDirectory(work), isEmpty);
    });

    test('a folder is deleted with everything in it', () async {
      await ssh.execute('mkdir -p $work/d/e; touch $work/d/e/f $work/d/g');
      await ssh.files.remove('$work/d', recursive: true);
      expect(await ssh.files.listDirectory(work), isEmpty);
    });

    test('a non-empty folder is not deleted without recursion', () async {
      await ssh.execute('mkdir -p $work/d; touch $work/d/g');
      await expectLater(ssh.files.remove('$work/d'), throwsA(isA<SshCommandException>()));
      expect((await ssh.files.listDirectory(work)).single.name, 'd');
    });

    test('deleting a name that starts with a dash does not read it as an option', () async {
      await ssh.execute("cd $work; touch -- '-rf' keep.txt");
      await ssh.files.remove('$work/-rf');
      expect((await ssh.files.listDirectory(work)).map((f) => f.name), ['keep.txt']);
    });

    test('deleting a file the user may not delete is an error', () async {
      await plain.execute('mkdir -p $plainWork/locked && touch $plainWork/locked/f && chmod 555 $plainWork/locked');
      await expectLater(plain.files.remove('$plainWork/locked/f'), throwsA(isA<SshCommandException>()));
      await plain.execute('chmod 755 $plainWork/locked');
    });
  });

  group('reading', () {
    test('text comes back exactly, including unicode, CRLF and no final newline', () async {
      const text = 'привет ✓\r\nвторая строка\r\nбез конца';
      await ssh.files.writeFile('$work/t.txt', text);
      expect(await ssh.files.readFile('$work/t.txt'), text);
    });

    test('a missing file is an error with the reason', () async {
      await expectLater(
        ssh.files.readFile('$work/nope.txt'),
        throwsA(isA<SshCommandException>().having((e) => '$e', 'message', contains('No such file'))),
      );
    });

    test('a name that starts with a dash is read as a file', () async {
      await ssh.execute("cd $work; printf hello > ./-n");
      expect(await ssh.files.readFile('$work/-n'), 'hello');
    });

    test('binary data survives the round trip', () async {
      final rnd = Random(7);
      final bytes = Uint8List.fromList(List.generate(200000, (_) => rnd.nextInt(256)));
      final r = await ssh.executeWithInput('cat > ${shQuote('$work/b.bin')}', bytes);
      expect(r.ok, isTrue);
      expect(await ssh.files.readBinaryFile('$work/b.bin'), bytes);
    });

    test('a five megabyte text file is read completely', () async {
      await ssh.execute('head -c 5000000 /dev/zero | tr "\\0" "a" > $work/big.txt');
      expect((await ssh.files.readFile('$work/big.txt')).length, 5000000);
    });
  });

  group('saving', () {
    test('a new file is created with the text', () async {
      expect(await ssh.files.writeFile('$work/new.txt', 'hello'), isTrue);
      expect(await ssh.files.readFile('$work/new.txt'), 'hello');
    });

    test('an empty text empties the file', () async {
      await ssh.files.writeFile('$work/e.txt', 'something');
      await ssh.files.writeFile('$work/e.txt', '');
      expect(await ssh.files.readFile('$work/e.txt'), '');
    });

    test('an existing file is replaced atomically and keeps its mode and owner', () async {
      await ssh.execute('cd $work; printf old > conf; chmod 640 conf');
      final before = await stat('$work/conf');

      final atomic = await ssh.files.writeFile('$work/conf', 'new text');

      expect(atomic, isTrue);
      final after = await stat('$work/conf');
      expect(after.split(' ')[0], '640', reason: 'mode kept');
      expect(after.split(' ')[1], 'fquser', reason: 'owner kept');
      expect(after.split(' ')[2], isNot(before.split(' ')[2]), reason: 'a new file was moved into place');
      expect(await ssh.files.readFile('$work/conf'), 'new text');
      expect(await ssh.execute('ls -A $work'), 'conf\n', reason: 'no temporary file is left behind');
    });

    test('saving through a link changes the target and keeps the link', () async {
      await ssh.execute('cd $work; printf old > target.txt; ln -s target.txt link.txt');
      await ssh.files.writeFile('$work/link.txt', 'via link');
      expect(await ssh.execute('readlink $work/link.txt'), 'target.txt\n');
      expect(await ssh.files.readFile('$work/target.txt'), 'via link');
    });

    test('a path with spaces, quotes and unicode is saved as that exact path', () async {
      const path = "$work/it's a файл ✓ \$x.txt";
      await ssh.files.writeFile(path, 'ok');
      expect(await ssh.files.readFile(path), 'ok');
      expect((await ssh.files.listDirectory(work)).single.name, "it's a файл ✓ \$x.txt");
    });

    test('a file in a read-only folder is written in place, and the user is told it was not atomic', () async {
      await plain.execute('cd $plainWork; printf old > ro.txt; chmod 666 ro.txt; chmod 555 .');
      addTearDown(() => plain.execute('chmod 755 $plainWork'));

      final atomic = await plain.files.writeFile('$plainWork/ro.txt', 'changed anyway');

      expect(atomic, isFalse, reason: 'no atomic replace was possible');
      expect(await plain.files.readFile('$plainWork/ro.txt'), 'changed anyway');
    });

    test('a file the user may not write is refused and stays as it was', () async {
      await plain.execute('cd $plainWork; printf keep > locked.txt; chmod 444 locked.txt');
      await expectLater(
        plain.files.writeFile('$plainWork/locked.txt', 'x'),
        throwsA(isA<SshCommandException>()),
      );
      expect(await plain.files.readFile('$plainWork/locked.txt'), 'keep');
    });

    test('a new file in a folder the user may not write to is refused', () async {
      await expectLater(
        plain.files.writeFile('/etc/fortiq-test-forbidden', 'x'),
        throwsA(isA<SshCommandException>().having((e) => '$e', 'message', contains('Permission denied'))),
      );
    });

    test('a missing folder is an error', () async {
      await expectLater(
        ssh.files.writeFile('$work/no/such/dir/f.txt', 'x'),
        throwsA(isA<SshCommandException>()),
      );
    });

    test('a megabyte of text is saved completely (more than a command line holds)', () async {
      final text = List.generate(100000, (i) => 'line $i with some text').join('\n');
      await ssh.files.writeFile('$work/long.txt', text);
      expect(await ssh.files.readFile('$work/long.txt'), text);
    });

    test('saving twice at the same time ends with one of the two texts, never a mix', () async {
      await ssh.files.writeFile('$work/race.txt', 'start');
      final a = 'A' * 500000;
      final b = 'B' * 500000;
      await Future.wait([ssh.files.writeFile('$work/race.txt', a), ssh.files.writeFile('$work/race.txt', b)]);
      final result = await ssh.files.readFile('$work/race.txt');
      expect(result == a || result == b, isTrue);
    });
  });

  group('search', () {
    setUp(() async {
      await ssh.execute(r'''
cd /home/fquser/work
mkdir -p docs "dir with space"
printf 'the quick brown fox\n' > docs/Fox.txt
printf 'nothing here\n' > docs/other.txt
printf 'fox in a spaced dir\n' > "dir with space/notes (1).txt"
printf 'tricky $(id) `x` "q" content\n' > docs/tricky.txt
''');
    });

    test('by name, without regard to case', () async {
      final r = await ssh.files.searchFiles('fox', searchRoot: work);
      expect(r.map((e) => e['path']), ['$work/docs/Fox.txt']);
      expect(r.single['type'], 'file');
    });

    test('by content', () async {
      final r = await ssh.files.searchFiles('fox', searchRoot: work, searchContent: true);
      final paths = r.map((e) => e['path']).toSet();
      expect(paths, {'$work/docs/Fox.txt', '$work/dir with space/notes (1).txt'});
      expect(r.first['line'], '1');
      expect(r.first['type'], 'content');
    });

    test('text with shell characters is searched for literally', () async {
      final r = await ssh.files.searchFiles(r'$(id) `x` "q"', searchRoot: work, searchContent: true);
      expect(r.map((e) => e['path']), ['$work/docs/tricky.txt']);
    });

    test('no match is an empty list', () async {
      expect(await ssh.files.searchFiles('zzz-no-such-thing', searchRoot: work), isEmpty);
      expect(await ssh.files.searchFiles('zzz-no-such-thing', searchRoot: work, searchContent: true), isEmpty);
    });
  });

  test('a file name with every kind of character is shown as it is in the listing', () async {
    await ssh.files.writeFile('$work/${utf8.decode([0xF0, 0x9F, 0x93, 0x81])} emoji.txt', 'x');
    final f = (await ssh.files.listDirectory(work)).single;
    expect(f.name, '\u{1F4C1} emoji.txt');
  });
}
