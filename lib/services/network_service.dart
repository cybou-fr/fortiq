import 'dart:convert';
import 'shell.dart';

/// Ports, firewall (ufw) and network interfaces.
class NetworkService {
  final RemoteShell _ssh;

  NetworkService(this._ssh);

  /// Fetches listening ports using `ss -tulpn` or fallback to `netstat -tulpn`
  Future<List<Map<String, dynamic>>> fetchOpenPorts() async {
    final out = await _ssh.execute(r'''
if command -v ss >/dev/null 2>&1; then echo TOOL=ss; ss -H -tulnp 2>/dev/null
elif command -v netstat >/dev/null 2>&1; then echo TOOL=netstat; netstat -tulnp 2>/dev/null
else echo TOOL=none; fi
''');
    final lines = const LineSplitter().convert(out);
    final tool = lines.isNotEmpty ? lines.first.trim() : '';
    final body = lines.skip(1).join('\n');
    switch (tool) {
      case 'TOOL=ss':
        return parseSsPorts(body);
      case 'TOOL=netstat':
        return parseNetstatPorts(body);
      default:
        // An error, not an empty list: "no ports" would look like a closed server.
        throw Exception('Neither ss nor netstat is installed (package iproute2 or net-tools)');
    }
  }

  /// Fetches UFW status and rules
  Future<Map<String, dynamic>> fetchUfwStatus() async {
    try {
      // ufw lives in /usr/sbin, which is not in PATH for non-root SSH commands.
      final out = await _ssh.execute(
        'if [ ! -x /usr/sbin/ufw ]; then echo NOT_INSTALLED; '
        'elif [ "\$(id -u)" = 0 ]; then ufw status numbered; '
        'else sudo -n ufw status numbered 2>&1 || echo NEEDS_SUDO; fi',
      );
      if (out.contains('NOT_INSTALLED')) {
        return {'installed': false, 'active': false, 'rules': []};
      }
      if (out.contains('NEEDS_SUDO')) {
        return {
          'installed': true,
          'active': false,
          'rules': [],
          'error': 'UFW needs root. Passwordless sudo is not available for this user '
              '(`sudo -n ufw status` failed). Connect as root or allow sudo without a password.',
        };
      }
      final isActive = out.contains('Status: active');
      final lines = LineSplitter.split(out);
      final rules = <Map<String, dynamic>>[];

      for (final line in lines) {
        final match = RegExp(r'\[\s*(\d+)\]\s+(.*?)\s+(ALLOW|DENY|REJECT|LIMIT)(?:\s+(?:IN|OUT|FWD))?\s+(.*)')
            .firstMatch(line);
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
    } catch (e) {
      return {'installed': true, 'active': false, 'rules': [], 'error': e.toString()};
    }
  }

  /// Adds a UFW rule
  Future<void> addUfwRule(String port, String action) async {
    if (!RegExp(r'^[a-z]+$').hasMatch(action)) {
      throw ArgumentError('Invalid ufw action: $action');
    }
    await _ssh.execute(asRoot('/usr/sbin/ufw $action ${shQuote(port)}'));
  }

  /// Deletes a UFW rule by number
  Future<void> deleteUfwRule(int ruleNum) async {
    await _ssh.execute(asRoot('/usr/sbin/ufw --force delete $ruleNum'));
  }

  /// Fetches network interfaces using `ip -o addr`
  Future<List<Map<String, dynamic>>> fetchNetworkInterfaces() async {
    final String out;
    try {
      out = await _ssh.execute('ip -o addr show');
    } on SshCommandException catch (e) {
      if (e.exitCode == 127) {
        throw Exception('The ip command is not installed (package iproute2)');
      }
      rethrow;
    }
    final list = <Map<String, dynamic>>[];
    for (final line in LineSplitter.split(out)) {
      final trimmed = line.trim();
      if (trimmed.isEmpty) continue;
      final parts = trimmed.split(RegExp(r'\s+'));
      if (parts.length >= 4) {
        list.add({
          'interface': parts[1],
          'type': parts[2],
          'ip': parts[3],
        });
      }
    }
    return list;
  }
}

/// Splits "0.0.0.0:22", "[::]:22", "*:80" or "127.0.0.53%lo:53" into host and port.
({String host, int? port}) _splitAddress(String local) {
  final i = local.lastIndexOf(':');
  if (i < 0) return (host: local, port: null);
  return (host: local.substring(0, i), port: int.tryParse(local.substring(i + 1)));
}

/// "users:(("sshd",pid=842,fd=3),("sshd",pid=900,fd=4))" -> name and first pid.
({String name, int? pid}) _ssProcess(String field) {
  final m = RegExp(r'\(\("([^"]*)",pid=(\d+)').firstMatch(field);
  if (m == null) return (name: '--', pid: null);
  return (name: m.group(1)!, pid: int.tryParse(m.group(2)!));
}

Map<String, dynamic> _portEntry(String proto, String state, String local, String process, int? pid) {
  final a = _splitAddress(local);
  return {
    'proto': proto,
    'state': state,
    'address': local,
    'host': a.host,
    'port': a.port,
    'process': pid != null ? '$process ($pid)' : process,
    'pid': pid,
  };
}

/// Parses `ss -H -tulnp`: Netid State Recv-Q Send-Q Local Peer [Process].
/// Listening TCP sockets and bound (unconnected) UDP sockets are kept.
List<Map<String, dynamic>> parseSsPorts(String output) {
  final list = <Map<String, dynamic>>[];
  for (final line in const LineSplitter().convert(output)) {
    final p = line.trim().split(RegExp(r'\s+'));
    if (p.length < 6) continue;
    final netid = p[0];
    final state = p[1];
    final listening = netid.startsWith('tcp') ? state == 'LISTEN' : state == 'UNCONN';
    if (!listening) continue;
    final proc = p.length > 6 ? _ssProcess(p.sublist(6).join(' ')) : (name: '--', pid: null);
    list.add(_portEntry(netid, state, p[4], proc.name, proc.pid));
  }
  return list;
}

/// Parses `netstat -tulnp`: Proto Recv-Q Send-Q Local Foreign [State] PID/Program.
/// UDP lines have no State column.
List<Map<String, dynamic>> parseNetstatPorts(String output) {
  final list = <Map<String, dynamic>>[];
  for (final line in const LineSplitter().convert(output)) {
    final p = line.trim().split(RegExp(r'\s+'));
    if (p.length < 5) continue;
    final proto = p[0];
    if (!proto.startsWith('tcp') && !proto.startsWith('udp')) continue; // headers
    final isTcp = proto.startsWith('tcp');
    String state;
    int next;
    if (isTcp) {
      if (p.length < 6 || p[5] != 'LISTEN') continue;
      state = 'LISTEN';
      next = 6;
    } else {
      state = 'UNCONN';
      next = 5;
    }
    var name = '--';
    int? pid;
    if (p.length > next && p[next] != '-') {
      final slash = p[next].indexOf('/');
      pid = int.tryParse(slash < 0 ? p[next] : p[next].substring(0, slash));
      name = slash < 0 ? '--' : p[next].substring(slash + 1);
    }
    list.add(_portEntry(proto, state, p[3], name, pid));
  }
  return list;
}
