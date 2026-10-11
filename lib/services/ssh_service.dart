import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'package:dartssh2/dartssh2.dart';
import 'known_hosts_service.dart';

/// Quotes [value] so the remote shell treats it as one literal word.
String shQuote(String value) => "'${value.replaceAll("'", r"'''")}'";

/// A remote command finished with a non-zero exit code or was killed.
class SshCommandException implements Exception {
  final String command;
  final int? exitCode;
  final String stderr;

  SshCommandException(this.command, this.exitCode, this.stderr);

  @override
  String toString() {
    final msg = stderr.trim();
    return msg.isNotEmpty ? msg : 'Command failed (exit code $exitCode)';
  }
}

/// Host key presented by the server that the user has to approve.
class HostKeyInfo {
  final String host;
  final int port;
  final String type;
  final String fingerprint;

  /// Previously trusted `"<type> <fingerprint>"`, set when the key changed.
  final String? previous;

  HostKeyInfo({
    required this.host,
    required this.port,
    required this.type,
    required this.fingerprint,
    this.previous,
  });

  bool get changed => previous != null;
}

class LinuxFileInfo {
  final String permissions;
  final int links;
  final String owner;
  final String group;
  final int size;
  final DateTime modified;
  final String name;
  final bool isDirectory;
  final bool isLink;

  LinuxFileInfo({
    required this.permissions,
    required this.links,
    required this.owner,
    required this.group,
    required this.size,
    required this.modified,
    required this.name,
    required this.isDirectory,
    required this.isLink,
  });

  String get formattedSize {
    if (isDirectory) return '--';
    if (size < 1024) return '$size B';
    if (size < 1024 * 1024) return '${(size / 1024).toStringAsFixed(1)} KB';
    if (size < 1024 * 1024 * 1024) {
      return '${(size / (1024 * 1024)).toStringAsFixed(1)} MB';
    }
    return '${(size / (1024 * 1024 * 1024)).toStringAsFixed(1)} GB';
  }
}

class SshClientService {
  SSHClient? _client;
  bool _isConnected = false;

  String? currentHost;
  int? currentPort;
  String? currentUsername;
  String? currentKeyPath;
  Process? _socksProcess;
  bool _isTunnelActive = false;

  bool get isConnected => _isConnected;
  SSHClient? get rawClient => _client;
  bool get isTunnelActive => _isTunnelActive;

  /// Starts an OpenSSH background SOCKS5 proxy tunnel on 127.0.0.1:1080
  Future<bool> startSocksTunnel({int localPort = 1080}) async {
    if (currentHost == null || currentUsername == null) return false;
    stopSocksTunnel();

    try {
      final args = <String>[
        '-N', // Do not execute a remote command
        '-D', '127.0.0.1:$localPort', // Dynamic SOCKS5 forwarding
        '-p', '${currentPort ?? 22}',
      ];

      if (currentKeyPath != null && currentKeyPath!.isNotEmpty) {
        args.addAll(['-i', currentKeyPath!]);
      }

      // Avoid strict host check prompt interfering
      args.addAll([
        '-o', 'StrictHostKeyChecking=no',
        '-o', 'UserKnownHostsFile=/dev/null',
        '-o', 'ExitOnForwardFailure=yes',
        '$currentUsername@$currentHost',
      ]);

      _socksProcess = await Process.start('ssh', args);
      _isTunnelActive = true;
      return true;
    } catch (_) {
      _isTunnelActive = false;
      return false;
    }
  }

  void stopSocksTunnel() {
    _socksProcess?.kill();
    _socksProcess = null;
    _isTunnelActive = false;
  }

  Future<SSHSession> startShell({
    int ptyWidth = 80,
    int ptyHeight = 24,
    int pixelWidth = 0,
    int pixelHeight = 0,
  }) async {
    if (_client == null || !_isConnected) {
      throw Exception('Not connected');
    }
    return await _client!.shell(
      pty: SSHPtyConfig(
        width: ptyWidth,
        height: ptyHeight,
        pixelWidth: pixelWidth,
        pixelHeight: pixelHeight,
      ),
    );
  }

  Future<void> connect({
    required String host,
    required int port,
    required String username,
    String? password,
    String? privateKey,
    Future<bool> Function(HostKeyInfo info)? onHostKey,
  }) async {
    disconnect();
    final socket = await SSHSocket.connect(host, port, timeout: const Duration(seconds: 10));

    List<SSHKeyPair>? identities;
    if (privateKey != null && privateKey.trim().isNotEmpty) {
      if (SSHKeyPair.isEncryptedPem(privateKey)) {
        throw Exception('Encrypted private keys (with passphrase) are not supported yet');
      }
      try {
        identities = SSHKeyPair.fromPem(privateKey);
      } catch (_) {
        throw Exception('Cannot parse the private key file');
      }
    }

    _client = SSHClient(
      socket,
      username: username,
      onPasswordRequest: password != null ? () => password : null,
      identities: identities,
      onVerifyHostKey: (type, fingerprint) =>
          _verifyHostKey(host, port, type, utf8.decode(fingerprint), onHostKey),
    );

    currentHost = host;
    currentPort = port;
    currentUsername = username;
    currentKeyPath = privateKey != null && privateKey.trim().isNotEmpty ? null : null; // key content vs path

    try {
      await _client!.authenticated;
    } catch (_) {
      disconnect();
      rethrow;
    }
    _isConnected = true;
  }

  /// Trust on first use: unknown keys and changed keys need user approval.
  Future<bool> _verifyHostKey(
    String host,
    int port,
    String type,
    String fingerprint,
    Future<bool> Function(HostKeyInfo info)? onHostKey,
  ) async {
    final current = '$type $fingerprint';
    final known = await KnownHostsService.lookup(host, port);
    if (known == current) return true;
    if (onHostKey == null) return false;

    final accepted = await onHostKey(HostKeyInfo(
      host: host,
      port: port,
      type: type,
      fingerprint: fingerprint,
      previous: known,
    ));
    if (accepted) await KnownHostsService.remember(host, port, current);
    return accepted;
  }

  void disconnect() {
    stopSocksTunnel();
    _client?.close();
    _client = null;
    _isConnected = false;
  }

  Future<String> execute(String command) async {
    if (_client == null || !_isConnected) {
      throw Exception('Not connected to SSH server');
    }
    final result = await _client!.runWithResult(command);
    final code = result.exitCode;
    if ((code != null && code != 0) || result.exitSignal != null) {
      throw SshCommandException(
        command,
        code,
        utf8.decode(result.stderr, allowMalformed: true),
      );
    }
    return utf8.decode(result.stdout, allowMalformed: true);
  }

  /// Lists files using `ls -la --time-style=+%s`
  Future<List<LinuxFileInfo>> listDirectory(String path) async {
    // Using standard POSIX ls with timestamp in unix epoch for precision
    final output = await execute('ls -la --time-style=+%s ${shQuote(path)}');
    final lines = LineSplitter.split(output);
    final results = <LinuxFileInfo>[];

    for (final line in lines) {
      final trimmed = line.trim();
      if (trimmed.isEmpty || trimmed.startsWith('total ')) continue;

      // ls -la format:
      // drwxr-xr-x  2 root root 4096 1728599420 filename
      final parts = trimmed.split(RegExp(r'\s+'));
      if (parts.length < 7) continue;

      final permissions = parts[0];
      final links = int.tryParse(parts[1]) ?? 1;
      final owner = parts[2];
      final group = parts[3];
      final size = int.tryParse(parts[4]) ?? 0;
      final timestampSec = int.tryParse(parts[5]) ?? 0;
      final modified = DateTime.fromMillisecondsSinceEpoch(timestampSec * 1000);

      // File name may contain spaces
      final name = parts.sublist(6).join(' ');

      if (name == '.' || name == '..') continue;

      final isDir = permissions.startsWith('d');
      final isLink = permissions.startsWith('l');

      results.add(LinuxFileInfo(
        permissions: permissions,
        links: links,
        owner: owner,
        group: group,
        size: size,
        modified: modified,
        name: name,
        isDirectory: isDir,
        isLink: isLink,
      ));
    }

    // Sort: directories first, then alphabetically
    results.sort((a, b) {
      if (a.isDirectory && !b.isDirectory) return -1;
      if (!a.isDirectory && b.isDirectory) return 1;
      return a.name.toLowerCase().compareTo(b.name.toLowerCase());
    });

    return results;
  }

  /// Creates a directory using `mkdir -p`
  Future<void> createDirectory(String path) async {
    await execute('mkdir -p ${shQuote(path)}');
  }

  /// Removes file or directory
  Future<void> remove(String path, {bool recursive = false}) async {
    final flag = recursive ? '-rf' : '';
    await execute('rm $flag -- ${shQuote(path)}');
  }

  /// Reads file content using `cat`
  Future<String> readFile(String path) async {
    return await execute('cat -- ${shQuote(path)}');
  }

  /// Reads binary file content (e.g. image) as Uint8List using base64 encoding over SSH
  Future<Uint8List> readBinaryFile(String path) async {
    final q = shQuote(path);
    final out = await execute('base64 -w 0 -- $q 2>/dev/null || base64 -- $q');
    final cleanBase64 = out.replaceAll(RegExp(r'\s+'), '');
    return base64Decode(cleanBase64);
  }

  /// Writes file content
  Future<void> writeFile(String path, String content) async {
    final client = _client;
    if (client == null || !_isConnected) {
      throw Exception('Not connected to SSH server');
    }
    // Content goes through stdin: command arguments are limited to ~128 KB.
    final command = 'cat > ${shQuote(path)}';
    final session = await client.execute(command);
    final stderrFuture = utf8.decodeStream(session.stderr);
    session.stdin.add(Uint8List.fromList(utf8.encode(content)));
    await session.stdin.close();
    await session.done;
    final err = await stderrFuture;
    final code = session.exitCode;
    if ((code != null && code != 0) || session.exitSignal != null) {
      throw SshCommandException(command, code, err);
    }
  }

  /// Fetches system metrics (CPU, RAM, Uptime, Disk)
  Future<Map<String, dynamic>> fetchMetrics() async {
    // Single consolidated command to minimize SSH roundtrips
    const script = '''
cpu=\$(top -bn1 | grep "Cpu(s)" | sed "s/.*, *\\([0-9.]*\\)%* id.*/\\1/" | awk '{print 100 - \$1}')
mem=\$(free -m | awk '/Mem:/ {print \$2, \$3}')
up=\$(uptime -p | sed 's/up //')
disk=\$(df -h / | awk 'NR==2 {print \$5, \$3, \$2}')
echo "CPU:\$cpu|MEM:\$mem|UP:\$up|DISK:\$disk"
''';
    try {
      final out = (await execute(script)).trim();
      final parts = out.split('|');
      double cpu = 0.0;
      int totalMem = 0;
      int usedMem = 0;
      String uptime = 'up';
      String disk = '';

      for (final p in parts) {
        if (p.startsWith('CPU:')) {
          cpu = double.tryParse(p.substring(4).trim()) ?? 0.0;
        } else if (p.startsWith('MEM:')) {
          final memParts = p.substring(4).trim().split(' ');
          if (memParts.length >= 2) {
            totalMem = int.tryParse(memParts[0]) ?? 0;
            usedMem = int.tryParse(memParts[1]) ?? 0;
          }
        } else if (p.startsWith('UP:')) {
          uptime = p.substring(3).trim();
        } else if (p.startsWith('DISK:')) {
          disk = p.substring(5).trim();
        }
      }
      return {
        'cpu': cpu,
        'totalMem': totalMem,
        'usedMem': usedMem,
        'uptime': uptime,
        'disk': disk,
      };
    } catch (_) {
      return {
        'cpu': 0.0,
        'totalMem': 1,
        'usedMem': 0,
        'uptime': 'unknown',
        'disk': '--',
      };
    }
  }

  /// Lists top processes using `ps`
  Future<List<Map<String, dynamic>>> fetchProcesses() async {
    final out = await execute('ps -eo pid,user,%cpu,%mem,comm --sort=-%cpu | head -n 30');
    final lines = LineSplitter.split(out);
    final list = <Map<String, dynamic>>[];

    for (final line in lines.skip(1)) {
      final trimmed = line.trim();
      if (trimmed.isEmpty) continue;
      final parts = trimmed.split(RegExp(r'\s+'));
      if (parts.length >= 5) {
        list.add({
          'pid': int.tryParse(parts[0]) ?? 0,
          'user': parts[1],
          'cpu': double.tryParse(parts[2]) ?? 0.0,
          'mem': double.tryParse(parts[3]) ?? 0.0,
          'comm': parts.sublist(4).join(' '),
        });
      }
    }
    return list;
  }

  /// Kills a process by PID
  Future<void> killProcess(int pid, {bool force = true}) async {
    final flag = force ? '-9' : '-15';
    await execute('kill $flag $pid');
  }

  /// Lists active systemd services
  Future<List<Map<String, dynamic>>> fetchServices() async {
    final out = await execute('systemctl list-units --type=service --no-legend --no-pager | head -n 35');
    final lines = LineSplitter.split(out);
    final list = <Map<String, dynamic>>[];

    for (final line in lines) {
      final trimmed = line.trim();
      if (trimmed.isEmpty) continue;
      final parts = trimmed.split(RegExp(r'\s+'));
      if (parts.length >= 5) {
        list.add({
          'unit': parts[0],
          'load': parts[1],
          'active': parts[2],
          'sub': parts[3],
          'description': parts.sublist(4).join(' '),
        });
      }
    }
    return list;
  }

  /// Controls a service: start, stop, restart
  Future<void> controlService(String unit, String action) async {
    if (!RegExp(r'^[a-z-]+$').hasMatch(action)) {
      throw ArgumentError('Invalid service action: $action');
    }
    await execute('systemctl $action ${shQuote(unit)}');
  }

  /// Fetches listening ports using `ss -tulpn` or fallback to `netstat -tulpn`
  Future<List<Map<String, dynamic>>> fetchOpenPorts() async {
    try {
      final out = await execute('ss -tulpn 2>/dev/null || netstat -tulpn 2>/dev/null || true');
      final lines = LineSplitter.split(out);
      final list = <Map<String, dynamic>>[];

      for (final line in lines) {
        final trimmed = line.trim();
        if (trimmed.isEmpty || (!trimmed.contains('LISTEN') && !trimmed.contains('State'))) continue;
        final parts = trimmed.split(RegExp(r'\s+'));
        if (parts.length >= 5) {
          final proto = parts[0];
          // Handle both ss and netstat outputs
          final localAddress = parts.length > 4 ? parts[4] : '';
          String process = '--';
          if (parts.length >= 7) {
            process = parts.sublist(6).join(' ');
          } else if (parts.length == 6) {
            process = parts[5];
          }
          if (localAddress.isNotEmpty && localAddress != 'Local') {
            list.add({
              'proto': proto,
              'address': localAddress,
              'process': process,
            });
          }
        }
      }
      return list;
    } catch (_) {
      return [];
    }
  }

  /// Fetches UFW status and rules
  Future<Map<String, dynamic>> fetchUfwStatus() async {
    try {
      final out = await execute('which ufw >/dev/null && ufw status numbered || echo "NOT_INSTALLED"');
      if (out.contains('NOT_INSTALLED')) {
        return {'installed': false, 'active': false, 'rules': []};
      }
      final isActive = out.contains('Status: active');
      final lines = LineSplitter.split(out);
      final rules = <Map<String, dynamic>>[];

      for (final line in lines) {
        final match = RegExp(r'\[\s*(\d+)\]\s+(.*?)\s+(ALLOW|DENY|REJECT|LIMIT)\s+(.*)').firstMatch(line);
        if (match != null) {
          rules.add({
            'num': int.tryParse(match.group(1) ?? '0') ?? 0,
            'to': match.group(2)?.trim() ?? '',
            'action': match.group(3)?.trim() ?? '',
            'from': match.group(4)?.trim() ?? '',
          });
        }
      }
      return {'installed': true, 'active': isActive, 'rules': rules};
    } catch (_) {
      return {'installed': false, 'active': false, 'rules': []};
    }
  }

  /// Adds a UFW rule
  Future<void> addUfwRule(String port, String action) async {
    if (!RegExp(r'^[a-z]+$').hasMatch(action)) {
      throw ArgumentError('Invalid ufw action: $action');
    }
    await execute('ufw $action ${shQuote(port)}');
  }

  /// Deletes a UFW rule by number
  Future<void> deleteUfwRule(int ruleNum) async {
    await execute('echo "y" | ufw delete $ruleNum');
  }

  /// Fetches network interfaces using `ip -o addr`
  Future<List<Map<String, dynamic>>> fetchNetworkInterfaces() async {
    try {
      final out = await execute('ip -o addr show');
      final lines = LineSplitter.split(out);
      final list = <Map<String, dynamic>>[];

      for (final line in lines) {
        final trimmed = line.trim();
        if (trimmed.isEmpty) continue;
        final parts = trimmed.split(RegExp(r'\s+'));
        if (parts.length >= 4) {
          final iface = parts[1];
          final inet = parts[2];
          final ipMask = parts[3];
          list.add({
            'interface': iface,
            'type': inet,
            'ip': ipMask,
          });
        }
      }
      return list;
    } catch (_) {
      return [];
    }
  }

  /// Searches files by name or content on the remote host
  Future<List<Map<String, dynamic>>> searchFiles(
    String query, {
    String searchRoot = '/',
    bool searchContent = false,
  }) async {
    final q = shQuote(query);
    final root = shQuote(searchRoot);
    final nameGlob = shQuote('*$query*');

    String cmd;
    if (searchContent) {
      // Search file content using grep (limiting to text files and max 50 results)
      cmd = 'grep -rIn --exclude-dir={proc,sys,dev,run,mnt,media} -m 1 -e $q -- $root 2>/dev/null | head -n 40';
    } else {
      // Search file name using find
      cmd = 'find $root -maxdepth 6 \\( -path "/proc" -o -path "/sys" -o -path "/dev" \\) -prune -o -iname $nameGlob -print 2>/dev/null | head -n 40';
    }

    try {
      final out = await execute(cmd);
      final lines = LineSplitter.split(out);
      final list = <Map<String, dynamic>>[];

      for (final line in lines) {
        final trimmed = line.trim();
        if (trimmed.isEmpty) continue;

        if (searchContent) {
          // grep output: /path/to/file:linenumber:matched line
          final parts = trimmed.split(':');
          if (parts.length >= 3) {
            final path = parts[0];
            final lineNum = parts[1];
            final match = parts.sublist(2).join(':').trim();
            list.add({
              'path': path,
              'line': lineNum,
              'preview': match,
              'type': 'content',
            });
          }
        } else {
          list.add({
            'path': trimmed,
            'line': null,
            'preview': null,
            'type': trimmed.endsWith('/') ? 'dir' : 'file',
          });
        }
      }
      return list;
    } catch (_) {
      return [];
    }
  }

  /// Fetches system logs by type (journal, auth, syslog, dmesg)
  Future<List<Map<String, dynamic>>> fetchSystemLogs({
    String logType = 'journal',
    int linesCount = 80,
  }) async {
    String cmd;
    switch (logType) {
      case 'auth':
        cmd = 'tail -n $linesCount /var/log/auth.log 2>/dev/null || journalctl -u ssh -n $linesCount --no-pager 2>/dev/null || echo "No auth logs available"';
        break;
      case 'syslog':
        cmd = 'tail -n $linesCount /var/log/syslog 2>/dev/null || journalctl -n $linesCount --no-pager 2>/dev/null || echo "No syslog available"';
        break;
      case 'dmesg':
        cmd = 'dmesg -T 2>/dev/null | tail -n $linesCount || echo "dmesg restricted or unavailable"';
        break;
      case 'journal':
      default:
        cmd = 'journalctl -n $linesCount --no-pager 2>/dev/null || echo "journalctl unavailable"';
        break;
    }

    try {
      final out = await execute(cmd);
      final lines = LineSplitter.split(out);
      final list = <Map<String, dynamic>>[];

      for (final line in lines) {
        final trimmed = line.trim();
        if (trimmed.isEmpty) continue;

        String level = 'INFO';
        final lower = trimmed.toLowerCase();
        if (lower.contains('error') || lower.contains('fail') || lower.contains('fatal') || lower.contains('crit')) {
          level = 'ERROR';
        } else if (lower.contains('warn') || lower.contains('warning')) {
          level = 'WARN';
        }

        list.add({
          'text': trimmed,
          'level': level,
        });
      }
      return list;
    } catch (_) {
      return [];
    }
  }

  /// Fetches local system mailbox emails (from /var/mail/$USER, /var/mail/root, or fallback spool)
  Future<List<Map<String, dynamic>>> fetchSystemMail({String mailboxUser = 'debian'}) async {
    // Shell command to search and cat mailbox files
    final script = '''
user="$mailboxUser"
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
      final out = await execute(script);
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
    final cleanTo = to.replaceAll('"', '\\"');
    final cleanSubject = subject.replaceAll('"', '\\"');
    final base64Body = base64Encode(utf8.encode(body));

    // Try sendmail or mail, fallback to direct append to user mbox
    final script = '''
body=\$(echo "$base64Body" | base64 -d)
if command -v mail >/dev/null 2>&1; then
  echo "\$body" | mail -s "$cleanSubject" "$cleanTo"
elif command -v sendmail >/dev/null 2>&1; then
  printf "To: %s\\nSubject: %s\\n\\n%s\\n" "$cleanTo" "$cleanSubject" "\$body" | sendmail -t
else
  # Fallback: append formatted mbox email to /tmp/sample_mbox or /var/mail/\$to
  date_str=\$(date -R)
  target="/var/mail/$cleanTo"
  if [ ! -w "\$target" ] && [ -w /tmp/sample_mbox ]; then
    target="/tmp/sample_mbox"
  fi
  printf "\\nFrom MAILER-DAEMON %s\\nDate: %s\\nFrom: fortis-desktop@localhost\\nTo: %s\\nSubject: %s\\n\\n%s\\n" "\$(date)" "\$date_str" "$cleanTo" "$cleanSubject" "\$body" >> "\$target" 2>/dev/null || echo "failed"
fi
''';

    try {
      final res = await execute(script);
      return !res.contains('failed');
    } catch (_) {
      return false;
    }
  }
}

