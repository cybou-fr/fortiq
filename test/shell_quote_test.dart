import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/server_setup_service.dart';
import 'package:fortiq/services/shell.dart';
import 'package:fortiq/services/terminal_service.dart';

void main() {
  group('SSH input exit status', () {
    test('only a confirmed zero exit code is successful', () {
      expect(const InputResult(exitCode: 0).ok, isTrue);
      expect(const InputResult(exitCode: 1).ok, isFalse);
      expect(const InputResult(exitCode: null).ok, isFalse,
          reason: 'A missing SSH exit status is an unknown outcome');
      expect(const InputResult(exitCode: 0, killedBySignal: true).ok, isFalse);
    });
  });

  group('shQuote', () {
    test('wraps plain values in single quotes', () {
      expect(shQuote('/home/user'), "'/home/user'");
    });

    test('keeps command substitution literal', () {
      expect(shQuote(r'$(rm -rf /)'), r"'$(rm -rf /)'");
      expect(shQuote('`id`'), "'`id`'");
    });

    test('escapes embedded single quotes', () {
      expect(shQuote("it's"), r"'it'\''s'");
    });

    test('keeps double quotes and spaces literal', () {
      expect(shQuote('a "b" c'), "'a \"b\" c'");
    });
  });

  group('asRoot', () {
    test('runs the whole && chain inside one privileged shell', () {
      final cmd = asRoot('apt-get update && apt-get install -y ufw');
      final quoted = shQuote('apt-get update && apt-get install -y ufw');
      expect(cmd, contains('sudo -n sh -c $quoted'));
      expect(cmd, contains('else sudo -n sh -c'));
      // The chain must never appear unquoted after sudo.
      expect(cmd, isNot(contains('sudo -n apt-get')));
    });
  });

  group('tmuxAttachCommand', () {
    test('attaches or creates the named session with scoped options', () {
      final cmd = tmuxAttachCommand('fortiq-2');
      expect(cmd, startsWith("tmux -u new-session -A -s 'fortiq-2'"));
      expect(cmd, contains("';' set-option -t 'fortiq-2' status off"));
      expect(cmd, contains("';' set-option -t 'fortiq-2' mouse on"));
    });

    test('quotes hostile session names', () {
      final cmd = tmuxAttachCommand(r"x'; rm -rf ~ #");
      expect(cmd, isNot(contains("-s x'")));
      expect(cmd, contains(r"'x'\''; rm -rf ~ #'"));
    });
  });

  group('parseUpgradable', () {
    test('reads Debian and Ubuntu apt output', () {
      const out = '''Listing...
openssl/stable-security 3.0.15-1~deb12u1 amd64 [upgradable from: 3.0.14-1~deb12u2]
tmux/stable 3.3a-3+deb12u1 amd64 [upgradable from: 3.3a-3]
libc6/noble-updates,noble-security 2.39-0ubuntu8.3 amd64 [upgradable from: 2.39-0ubuntu8.2]
''';
      final r = parseUpgradable(out);
      expect(r.names, ['openssl', 'tmux', 'libc6']);
      expect(r.security, 2);
    });

    test('empty list', () {
      final r = parseUpgradable('Listing...');
      expect(r.names, isEmpty);
      expect(r.security, 0);
    });
  });
}
