import 'dart:convert';
import 'shell.dart';

/// Processes, systemd services, metrics and logs.
class SystemService {
  final RemoteShell _ssh;

  SystemService(this._ssh);

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
      final out = (await _ssh.execute(script)).trim();
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
      // Do not invent zero metrics: callers keep the last real values.
      rethrow;
    }
  }

  /// Lists top processes using `ps`
  Future<List<Map<String, dynamic>>> fetchProcesses() async {
    final out = await _ssh.execute('ps -eo pid,user,%cpu,%mem,comm --sort=-%cpu | head -n 30');
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
    // Own processes need no privileges; fall back to sudo for other users' ones.
    await _ssh.execute('kill $flag $pid 2>/dev/null || sudo -n kill $flag $pid');
  }

  /// Lists active systemd services
  Future<List<Map<String, dynamic>>> fetchServices() async {
    final out = await _ssh.execute('systemctl list-units --type=service --no-legend --no-pager | head -n 35');
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
    await _ssh.execute(asRoot('systemctl $action ${shQuote(unit)}'));
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
      final out = await _ssh.execute(cmd);
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
}
