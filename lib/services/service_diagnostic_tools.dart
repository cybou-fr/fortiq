import 'dart:convert';

import 'shell.dart';

/// Read-only, bounded snapshots from explicitly registered Linux CLI tools.
/// No AI is required to collect or display this evidence.
class DiagnosticSection {
  final String tool;
  final String output;
  const DiagnosticSection(this.tool, this.output);
}

class DiagnosticReport {
  final String unit;
  final List<DiagnosticSection> sections;
  const DiagnosticReport(this.unit, this.sections);

  String toPlainText({int maxBytes = 7900}) {
    final buffer = StringBuffer('Service: $unit\n');
    for (final section in sections) {
      buffer.writeln('\n=== ${section.tool} ===');
      buffer.writeln(section.output);
    }
    final bytes = utf8.encode(buffer.toString());
    if (bytes.length <= maxBytes) return buffer.toString();
    // Preserve an explicit truncation marker and valid UTF-8.
    final prefix = utf8.decode(
      bytes.sublist(0, maxBytes - 32),
      allowMalformed: true,
    );
    return '$prefix\n[report truncated]\n';
  }
}

class ToolCapability {
  final String name;
  final bool available;
  const ToolCapability(this.name, this.available);
}

/// A small fixed registry, not a model-driven shell or a plugin interpreter.
class ServiceDiagnosticTools {
  final RemoteShell _ssh;
  ServiceDiagnosticTools(this._ssh);

  static final _safeUnit = RegExp(r'^[A-Za-z0-9_.@:-]+$');

  Future<List<ToolCapability>> capabilities() async {
    if (!_ssh.isConnected) throw StateError('Reconnect SSH first.');
    final output = await _ssh.execute(
      'for tool in systemctl journalctl ss; do '
      'if command -v "\$tool" >/dev/null 2>&1; then '
      'printf "%s:yes\\n" "\$tool"; else printf "%s:no\\n" "\$tool"; fi; '
      'done',
    );
    return [
      for (final name in ['systemctl', 'journalctl', 'ss'])
        ToolCapability(name, output.split('\n').contains('$name:yes')),
    ];
  }

  /// Fixed, non-mutating commands. Each snapshot is limited on the server
  /// before crossing SSH; no model can request additional shell commands.
  Future<DiagnosticReport> investigate(String unit) async {
    if (!_ssh.isConnected) throw StateError('Reconnect SSH first.');
    if (unit.isEmpty || unit.length > 200 || !_safeUnit.hasMatch(unit)) {
      throw ArgumentError('Invalid systemd unit.');
    }
    final quoted = shQuote(unit);
    final sections = <DiagnosticSection>[];
    final commands = <String, String>{
      'systemctl': 'LC_ALL=C systemctl --no-pager --full status -- $quoted 2>&1 | head -c 2800',
      'journalctl': 'LC_ALL=C journalctl --no-pager -n 25 -u $quoted 2>&1 | head -c 3000',
      'listening TCP ports': 'LC_ALL=C ss -lnt 2>&1 | head -c 1100',
    };
    for (final entry in commands.entries) {
      final result = await _ssh.execute(entry.value);
      sections.add(DiagnosticSection(entry.key, result));
    }
    return DiagnosticReport(unit, sections);
  }
}
