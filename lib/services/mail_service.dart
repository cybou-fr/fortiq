import 'dart:convert';
import 'dart:typed_data';
import 'shell.dart';

/// The server's local system mailbox.
class MailService {
  final RemoteShell _ssh;

  MailService(this._ssh);

  /// Fetches local system mailbox emails (from /var/mail/$USER, /var/mail/root, or fallback spool)
  Future<List<Map<String, dynamic>>> fetchSystemMail({String mailboxUser = 'debian'}) async {
    // Shell command to search and cat mailbox files
    final script = '''
user=${shQuote(mailboxUser)}
paths=(
  "/var/mail/\$user"
  "/var/spool/mail/\$user"
  "/var/mail/root"
  "/var/spool/mail/root"
  "/tmp/sample_mbox"
)

found=""
for p in "\${paths[@]}"; do
  if [ -s "\$p" ]; then
    echo "===MAILBOX_PATH: \$p==="
    cat "\$p"
    found="1"
    break
  fi
done

if [ -z "\$found" ]; then
  echo "===MAILBOX_EMPTY==="
fi
''';

    try {
      final out = await _ssh.execute(script);
      if (out.contains('===MAILBOX_EMPTY===')) {
        return [];
      }

      final lines = LineSplitter.split(out).toList();
      final messages = <Map<String, dynamic>>[];

      Map<String, dynamic>? currentMsg;
      final bodyBuffer = StringBuffer();
      bool inBody = false;

      for (final line in lines) {
        if (line.startsWith('===MAILBOX_PATH:')) {
          continue;
        }

        // mbox messages begin with 'From ' (note the space, e.g. 'From MAILER-DAEMON...')
        if (line.startsWith('From ') && (line.contains(' 20') || line.contains(' 19') || line.contains('@') || line.contains(':'))) {
          if (currentMsg != null) {
            currentMsg['body'] = bodyBuffer.toString().trim();
            messages.add(currentMsg);
            bodyBuffer.clear();
          }
          currentMsg = {
            'from': 'Unknown',
            'to': '',
            'subject': '(No Subject)',
            'date': '',
            'id': DateTime.now().millisecondsSinceEpoch.toString() + '_' + messages.length.toString(),
          };
          inBody = false;
          continue;
        }

        if (currentMsg != null) {
          if (!inBody) {
            if (line.trim().isEmpty) {
              inBody = true;
              continue;
            }

            final lower = line.toLowerCase();
            if (lower.startsWith('from:')) {
              currentMsg['from'] = line.substring(5).trim();
            } else if (lower.startsWith('to:')) {
              currentMsg['to'] = line.substring(3).trim();
            } else if (lower.startsWith('subject:')) {
              currentMsg['subject'] = line.substring(8).trim();
            } else if (lower.startsWith('date:')) {
              currentMsg['date'] = line.substring(5).trim();
            }
          } else {
            bodyBuffer.writeln(line);
          }
        }
      }

      if (currentMsg != null) {
        currentMsg['body'] = bodyBuffer.toString().trim();
        messages.add(currentMsg);
      }

      // Return newest first
      return messages.reversed.toList();
    } catch (_) {
      return [];
    }
  }

  /// Asks the Linux MTA to accept a system message over SSH stdin.
  /// A zero exit code confirms submission to the command, NOT delivery.
  /// Missing tools never fall back to writing arbitrary mbox content.
  Future<bool> sendSystemMail({
    required String to,
    required String subject,
    required String body,
  }) async {
    final recipient = to.trim();
    if (recipient.isEmpty ||
        recipient.length > 254 ||
        !RegExp(r'^[A-Za-z0-9_+.-]+(?:@[A-Za-z0-9.-]+)?$').hasMatch(recipient) ||
        subject.isEmpty || subject.length > 998 ||
        subject.contains('\r') || subject.contains('\n')) {
      return false;
    }
    final command = '''
to=${shQuote(recipient)}
subject=${shQuote(subject)}
if command -v sendmail >/dev/null 2>&1; then
  { printf 'To: %s\\nSubject: %s\\n\\n' "\$to" "\$subject"; cat; } | sendmail -i -t
elif command -v mail >/dev/null 2>&1; then
  mail -s "\$subject" "\$to"
else
  printf 'No mail or sendmail command available on Linux.\\n' >&2
  exit 127
fi
''';
    final result = await _ssh.executeWithInput(
      command,
      Uint8List.fromList(utf8.encode(body)),
    );
    if (result.exitCode == null || result.killedBySignal) {
      throw StateError(
        'System mail result unknown: verify the mail queue before retrying.',
      );
    }
    return result.exitCode == 0;
  }
}
