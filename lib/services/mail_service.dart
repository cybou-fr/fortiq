import 'dart:convert';
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

  /// Sends a local system mail using mail/sendmail/append to mbox
  Future<bool> sendSystemMail({
    required String to,
    required String subject,
    required String body,
  }) async {
    final base64Body = base64Encode(utf8.encode(body));

    // Try sendmail or mail, fallback to direct append to user mbox
    final script = '''
to=${shQuote(to)}
subject=${shQuote(subject)}
body=\$(echo "$base64Body" | base64 -d)
if command -v mail >/dev/null 2>&1; then
  echo "\$body" | mail -s "\$subject" "\$to"
elif command -v sendmail >/dev/null 2>&1; then
  printf "To: %s\\nSubject: %s\\n\\n%s\\n" "\$to" "\$subject" "\$body" | sendmail -t
else
  # Fallback: append formatted mbox email to /tmp/sample_mbox or /var/mail/\$to
  date_str=\$(date -R)
  target="/var/mail/\$to"
  if [ ! -w "\$target" ] && [ -w /tmp/sample_mbox ]; then
    target="/tmp/sample_mbox"
  fi
  printf "\\nFrom MAILER-DAEMON %s\\nDate: %s\\nFrom: fortis-desktop@localhost\\nTo: %s\\nSubject: %s\\n\\n%s\\n" "\$(date)" "\$date_str" "\$to" "\$subject" "\$body" >> "\$target" 2>/dev/null || echo "failed"
fi
''';

    try {
      final res = await _ssh.execute(script);
      return !res.contains('failed');
    } catch (_) {
      return false;
    }
  }
}
