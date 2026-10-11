import 'dart:convert';
import 'dart:typed_data';

import 'package:intl/intl.dart';

import 'shell.dart';

class MailAccount {
  final String name;
  final bool isDefault;
  const MailAccount(this.name, this.isDefault);
}

class MailEnvelope {
  final String id, subject, from, to, date;
  final bool unread, hasAttachment;

  MailEnvelope(Map<String, dynamic> json)
    : id = json['id'].toString(),
      subject = (json['subject'] ?? '').toString(),
      from = _address(json['from']),
      to = _address(json['to']),
      date = (json['date'] ?? '').toString(),
      unread = !(json['flags'] as List? ?? []).any(
        (flag) => flag.toString().toLowerCase() == 'seen',
      ),
      hasAttachment = json['has_attachment'] == true;

  static String _address(dynamic value) {
    if (value is Map) {
      final name = value['name']?.toString() ?? '';
      final address = (value['addr'] ?? value['address'] ?? '').toString();
      return name.isEmpty ? address : '$name <$address>';
    }
    if (value is List) return value.map(_address).join(', ');
    return value?.toString() ?? '';
  }
}

/// Account configuration and all mail protocols belong to the server's
/// existing Himalaya CLI. This adapter only executes commands over SSH.
class HimalayaMailService {
  final RemoteShell _ssh;
  HimalayaMailService(this._ssh);

  static const _environment =
      r'export PATH="$HOME/.local/bin:$PATH"; export NO_COLOR=1; ';

  String _command(List<String> arguments, {bool json = true}) =>
      '$_environment timeout 90s himalaya '
      '${(json ? ['--output', 'json', ...arguments] : arguments).map(shQuote).join(' ')}';

  Future<String> inspect() async {
    _requireConnection();
    final version = await _ssh.execute(
      '$_environment command -v himalaya >/dev/null 2>&1 || '
      '{ echo "Himalaya is not installed on this server." >&2; exit 127; }; '
      'himalaya --version </dev/null',
    );
    // The 1.x contract differs from older releases and the unreleased CLI.
    if (!RegExp(r'himalaya\s+1\.').hasMatch(version)) {
      throw StateError(
        'This Mail adapter requires Himalaya 1.x. Found: ${version.trim()}',
      );
    }
    return version.trim();
  }

  Future<dynamic> _json(List<String> arguments) async {
    _requireConnection();
    final output = await _ssh.execute('${_command(arguments)} </dev/null');
    try {
      return jsonDecode(output);
    } on FormatException {
      throw const FormatException(
        'The server mail client returned invalid JSON.',
      );
    }
  }

  List<Map<String, dynamic>> _records(dynamic value) {
    if (value is! List || value.any((row) => row is! Map)) {
      throw const FormatException('Unsupported mail client response.');
    }
    return value.map((row) => Map<String, dynamic>.from(row as Map)).toList();
  }

  Future<List<MailAccount>> accounts() async {
    final rows = _records(await _json(['account', 'list']));
    return rows.map((row) {
      if (row['name'] is! String) {
        throw const FormatException(
          'Missing account name in mail client response.',
        );
      }
      return MailAccount(row['name'] as String, row['default'] == true);
    }).toList();
  }

  Future<List<String>> folders(String account) async {
    final rows = _records(
      await _json(['folder', 'list', '--account', account]),
    );
    return rows.map((row) {
      if (row['name'] is! String) {
        throw const FormatException(
          'Missing folder name in mail client response.',
        );
      }
      return row['name'] as String;
    }).toList();
  }

  static const pageSize = 30;
  Future<List<MailEnvelope>> envelopes(
    String account,
    String folder,
    int page,
  ) async {
    if (page < 1) throw ArgumentError.value(page, 'page');
    final rows = _records(
      await _json([
        'envelope',
        'list',
        '--account',
        account,
        '--folder',
        folder,
        '--page',
        '$page',
        '--page-size',
        '$pageSize',
        'order',
        'by',
        'date',
        'desc',
      ]),
    );
    return rows.map((row) {
      _id(row['id'].toString());
      return MailEnvelope(row);
    }).toList();
  }

  Future<String> _text(List<String> arguments) async {
    final value = await _json(arguments);
    if (value is! String) {
      throw const FormatException('Expected message text from mail client.');
    }
    return value;
  }

  Future<String> read(String account, String folder, String id) => _text([
    'message',
    'read',
    '--account',
    account,
    '--folder',
    folder,
    '--no-headers',
    _id(id),
  ]);

  Future<String> template(String account, {String? folder, String? replyTo}) =>
      _text([
        'template',
        replyTo == null ? 'write' : 'reply',
        '--account',
        account,
        if (folder != null) ...['--folder', folder],
        if (replyTo != null) _id(replyTo),
      ]);

  Future<String> downloadAttachments(
    String account,
    String folder,
    String id,
  ) => _text([
    'attachment',
    'download',
    '--account',
    account,
    '--folder',
    folder,
    _id(id),
  ]);

  Future<void> move(
    String account,
    String folder,
    String id,
    String target,
  ) async {
    await _json([
      'message',
      'move',
      '--account',
      account,
      '--folder',
      folder,
      target,
      _id(id),
    ]);
  }

  Future<void> markUnread(String account, String folder, String id) async {
    await _json([
      'flag',
      'remove',
      '--account',
      account,
      '--folder',
      folder,
      _id(id),
      'seen',
    ]);
  }

  Future<void> send(String account, MailDraft draft) async {
    _requireConnection();
    final bytes = Uint8List.fromList(utf8.encode(draft.toMessage()));
    // Wait for the whole upload before asking the CLI to send. EOF caused by a
    // dropped SSH connection must never turn a partial upload into an email.
    final result = await _ssh.executeWithInput(
      '$_environment umask 077; '
      r'''tmp=$(mktemp) || exit 1; trap 'rm -f -- "$tmp"' EXIT HUP INT TERM; '''
      r'cat > "$tmp" || exit 1; '
      '[ "\$(wc -c < "\$tmp")" -eq ${bytes.length} ] || '
      '{ echo "Incomplete mail upload; message not sent." >&2; exit 1; }; '
      '${_command(['message', 'send', '--account', account], json: false)} < "\$tmp" >/dev/null',
      bytes,
    );
    if (result.killedBySignal || result.exitCode != 0) {
      throw StateError(
        'Sending was not confirmed. Check Sent before retrying. '
        '${result.stderr.trim()}',
      );
    }
  }

  String _id(String value) {
    if (!RegExp(r'^\d+$').hasMatch(value)) {
      throw const FormatException('Invalid message identifier.');
    }
    return value;
  }

  void _requireConnection() {
    if (!_ssh.isConnected) {
      throw StateError('Mail server unavailable: reconnect SSH.');
    }
  }
}

/// Headers for a new message or reply come from Himalaya, including threading
/// headers. The edited body is sent as plain text, never interpreted as MML.
class MailDraft {
  final Map<String, String> headers;
  String to, subject, body;
  MailDraft(this.headers, {this.to = '', this.subject = '', this.body = ''});

  factory MailDraft.fromTemplate(String template) {
    final normalized = template.replaceAll('\r\n', '\n');
    final boundary = normalized.indexOf('\n\n');
    if (boundary < 0) {
      throw const FormatException('Invalid compose template from server.');
    }
    final headers = <String, String>{};
    String? previous;
    for (final line in normalized.substring(0, boundary).split('\n')) {
      if (line.startsWith(' ') || line.startsWith('\t')) {
        if (previous != null) {
          headers[previous] = '${headers[previous]} ${line.trim()}';
        }
        continue;
      }
      final colon = line.indexOf(':');
      if (colon < 1) continue;
      previous = line.substring(0, colon).toLowerCase();
      headers[previous] = line.substring(colon + 1).trim();
    }
    return MailDraft(
      headers,
      to: headers['to'] ?? '',
      subject: headers['subject'] ?? '',
      body: normalized.substring(boundary + 2),
    );
  }

  String toMessage() {
    final from = headers['from'] ?? '';
    for (final header in [from, to, subject, ...headers.values]) {
      if (header.contains(RegExp(r'[\r\n\x00]'))) {
        throw const FormatException('Mail headers cannot contain line breaks.');
      }
    }
    if (from.isEmpty || to.trim().isEmpty) {
      throw const FormatException('Sender and recipient are required.');
    }
    final encodedBody = base64Encode(utf8.encode(body));
    final lines = <String>[];
    for (var offset = 0; offset < encodedBody.length; offset += 76) {
      lines.add(
        encodedBody.substring(
          offset,
          (offset + 76).clamp(0, encodedBody.length),
        ),
      );
    }
    // Encode the subject in short UTF-8 encoded words, without splitting runes.
    final words = <String>[];
    var chunk = '';
    for (final rune in subject.runes) {
      final next = String.fromCharCode(rune);
      if (utf8.encode(chunk + next).length > 42) {
        words.add('=?UTF-8?B?${base64Encode(utf8.encode(chunk))}?=');
        chunk = '';
      }
      chunk += next;
    }
    if (chunk.isNotEmpty) {
      words.add('=?UTF-8?B?${base64Encode(utf8.encode(chunk))}?=');
    }
    return [
      'From: $from',
      'To: $to',
      'Date: ${DateFormat('EEE, dd MMM yyyy HH:mm:ss', 'en_US').format(DateTime.now().toUtc())} +0000',
      'Subject: ${words.join('\r\n ')}',
      for (final name in ['cc', 'in-reply-to', 'references'])
        if ((headers[name] ?? '').isNotEmpty) '$name: ${headers[name]}',
      'MIME-Version: 1.0',
      'Content-Type: text/plain; charset=UTF-8',
      'Content-Transfer-Encoding: base64',
      '',
      ...lines,
      '',
    ].join('\r\n');
  }
}
