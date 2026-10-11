import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/ssh_service.dart';

void main() {
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
}
