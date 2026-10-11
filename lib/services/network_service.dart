import 'dart:convert';
import 'shell.dart';

/// Ports, firewall (ufw) and network interfaces.
class NetworkService {
  final RemoteShell _ssh;

  NetworkService(this._ssh);

  /// Fetches listening ports using `ss -tulpn` or fallback to `netstat -tulpn`
  Future<List<Map<String, dynamic>>> fetchOpenPorts() async {
    try {
      final out = await _ssh.execute('ss -tulpn 2>/dev/null || netstat -tulpn 2>/dev/null || true');
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
    try {
      final out = await _ssh.execute('ip -o addr show');
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
}
