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
    // user:32: plain "user" is cut to 8 characters ("systemd+"). Every process,
    // busiest first: the window filters, it does not see only a top-30 slice.
    final out = await _ssh.execute('ps -eo pid,user:32,%cpu,%mem,comm --sort=-%cpu');
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
    // --plain: no "●" marker in front of failed units, which shifted the columns;
    // --all: stopped units are services too. A stopped service that nothing
    // needs is not loaded at all, so it only appears in the unit-file list.
    final out = await _ssh.execute(
      'systemctl list-units --type=service --all --no-legend --no-pager --plain; '
      'echo $_unitFilesMarker; '
      'systemctl list-unit-files --type=service --no-legend --no-pager --plain',
    );
    final split = out.split(_unitFilesMarker);
    return mergeServices(
      parseServices(split.first),
      parseUnitFiles(split.length > 1 ? split[1] : ''),
    );
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
    // An ordinary user sees only their own messages in the journal (and cannot
    // read auth.log or dmesg), so use sudo when it works without a password.
    const sudo = r'S=; [ "$(id -u)" = 0 ] || { sudo -n true 2>/dev/null && S="sudo -n"; }; ';
    final String cmd;
    switch (logType) {
      case 'auth':
        cmd = '$sudo\$S tail -n $linesCount /var/log/auth.log 2>/dev/null || '
            '\$S journalctl -u ssh -n $linesCount --no-pager 2>/dev/null || '
            'echo "No auth logs available"';
      case 'syslog':
        cmd = '$sudo\$S tail -n $linesCount /var/log/syslog 2>/dev/null || '
            '\$S journalctl -n $linesCount --no-pager 2>/dev/null || '
            'echo "No syslog available"';
      case 'dmesg':
        cmd = '$sudo\$S dmesg -T 2>/dev/null | tail -n $linesCount || '
            'echo "dmesg restricted or unavailable"';
      default:
        cmd = '$sudo\$S journalctl -n $linesCount --no-pager 2>/dev/null || '
            'echo "journalctl unavailable"';
    }

    // A failed connection is an error, not an empty log.
    final out = await _ssh.execute(cmd);
    return parseLogLines(out);
  }
}

/// Parses `systemctl list-units --type=service --all --no-legend --plain`.
List<Map<String, dynamic>> parseServices(String output) {
  final list = <Map<String, dynamic>>[];
  for (final line in const LineSplitter().convert(output)) {
    final trimmed = line.trim();
    if (trimmed.isEmpty) continue;
    final parts = trimmed.split(RegExp(r'\s+'));
    if (parts.length < 4) continue;
    list.add({
      'unit': parts[0],
      'load': parts[1],
      'active': parts[2],
      'sub': parts[3],
      'description': parts.length > 4 ? parts.sublist(4).join(' ') : '',
    });
  }
  list.sort((a, b) => (a['unit'] as String).compareTo(b['unit'] as String));
  return list;
}

const _unitFilesMarker = '===FORTIQ-UNIT-FILES===';

/// Splits `systemctl list-unit-files --type=service --plain` into unit -> state
/// (enabled, disabled, static, masked, ...).
Map<String, String> parseUnitFiles(String output) {
  final map = <String, String>{};
  for (final line in const LineSplitter().convert(output)) {
    final p = line.trim().split(RegExp(r'\s+'));
    if (p.length >= 2) map[p[0]] = p[1];
  }
  return map;
}

/// Loaded units plus the installed units that are not loaded (stopped and
/// unneeded services). Template units (`name@.service`) cannot be started and
/// are left out. Every entry gets `enabled`: the unit-file state, if known.
List<Map<String, dynamic>> mergeServices(
  List<Map<String, dynamic>> loaded,
  Map<String, String> unitFiles,
) {
  final result = [
    for (final s in loaded) {...s, 'enabled': unitFiles[s['unit']] ?? ''},
  ];
  final known = {for (final s in loaded) s['unit'] as String};
  unitFiles.forEach((unit, state) {
    if (known.contains(unit) || unit.endsWith('@.service')) return;
    result.add({
      'unit': unit,
      'load': 'not-loaded',
      'active': 'inactive',
      'sub': 'dead',
      'description': '',
      'enabled': state,
    });
  });
  result.sort((a, b) => (a['unit'] as String).compareTo(b['unit'] as String));
  return result;
}

/// Turns log text into lines with an approximate level.
List<Map<String, dynamic>> parseLogLines(String output) {
  final list = <Map<String, dynamic>>[];
  for (final line in const LineSplitter().convert(output)) {
    final trimmed = line.trim();
    if (trimmed.isEmpty) continue;
    final lower = trimmed.toLowerCase();
    var level = 'INFO';
    if (lower.contains('error') || lower.contains('fail') || lower.contains('fatal') || lower.contains('crit')) {
      level = 'ERROR';
    } else if (lower.contains('warn')) {
      level = 'WARN';
    }
    list.add({'text': trimmed, 'level': level});
  }
  return list;
}
