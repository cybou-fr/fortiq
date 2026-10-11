import 'dart:convert';
import 'dart:typed_data';

import 'shell.dart';

/// Server-owned optional llama.cpp adviser. FORTIQ only passes selected text
/// through SSH to the Linux `ai` CLI and renders its response.
class AiAdvice {
  final String text;
  final bool cached;
  final int ageSeconds;
  const AiAdvice(this.text, {required this.cached, required this.ageSeconds});
}

class AiStatus {
  final bool available;
  final String? reason;
  final int totalMemoryMb;
  final int availableMemoryMb;

  const AiStatus({
    required this.available,
    required this.reason,
    required this.totalMemoryMb,
    required this.availableMemoryMb,
  });
}

class AiService {
  final RemoteShell _ssh;

  AiService(this._ssh);

  /// Read-only capability detection. Never downloads a GGUF or starts
  /// inference; weak VPS hosts remain fully functional without server AI.
  Future<AiStatus> status() async {
    if (!_ssh.isConnected) throw StateError('Reconnect SSH first.');
    final result = await _ssh.execute(
      r'export PATH="$HOME/.local/bin:$PATH"; ai status --json',
    );
    final dynamic decoded;
    try {
      decoded = jsonDecode(result);
    } on FormatException {
      throw const FormatException('Linux AI returned invalid status JSON.');
    }
    if (decoded is! Map ||
        decoded['schema'] != 'fortiq-ai-status/v1' ||
        decoded['available'] is! bool ||
        decoded['memory_total_mb'] is! int ||
        decoded['memory_available_mb'] is! int) {
      throw const FormatException('Unexpected Linux AI status response.');
    }
    return AiStatus(
      available: decoded['available'] as bool,
      reason: decoded['reason'] as String?,
      totalMemoryMb: decoded['memory_total_mb'] as int,
      availableMemoryMb: decoded['memory_available_mb'] as int,
    );
  }

  /// No fallback to an HTTP API or a local model when SSH is absent.
  Future<AiAdvice> explain(
    String selectedContext, {
    bool allowServerCache = false,
  }) async {
    if (!_ssh.isConnected) {
      throw StateError('AI unavailable: reconnect SSH.');
    }
    final input = Uint8List.fromList(utf8.encode(selectedContext));
    if (input.isEmpty || input.length > 8192) {
      throw ArgumentError('AI context must be between 1 and 8192 bytes.');
    }
    // The cache is opt-in because logs can contain credentials. The CLI
    // enforces its own consent and TTL checks before any persistent write.
    final command = r'export PATH="$HOME/.local/bin:$PATH"; ai explain --stdin --json'
        '${allowServerCache ? ' --cache --cache-sensitive' : ''}';
    final result = await _ssh.executeWithInput(command, input);
    // Never assume that a missing exit status means success.
    if (result.exitCode != 0 || result.killedBySignal) {
      String message = 'The Linux AI command failed or did not finish.';
      try {
        final data = jsonDecode(result.stdout);
        if (data is Map && data['message'] is String) {
          message = data['message'] as String;
        }
      } on FormatException {
        // Do not show stderr: a third-party runtime may include prompt data.
      }
      throw StateError(message);
    }
    final dynamic decoded;
    try {
      decoded = jsonDecode(result.stdout);
    } on FormatException {
      throw const FormatException('Linux AI returned invalid JSON.');
    }
    if (decoded is! Map ||
        decoded['schema'] != 'fortiq-ai-advice/v1' ||
        decoded['status'] != 'ok' ||
        decoded['advice'] is! String ||
        (decoded['advice'] as String).trim().isEmpty ||
        decoded['cached'] is! bool ||
        decoded['age_seconds'] is! int) {
      throw const FormatException('Unexpected Linux AI response.');
    }
    return AiAdvice(
      decoded['advice'] as String,
      cached: decoded['cached'] as bool,
      ageSeconds: decoded['age_seconds'] as int,
    );
  }

  /// Read-only Linux evidence for a selected systemd unit. This is data, not
  /// an instruction for the model; all operations remain ordinary SSH CLI.
  Future<String> serviceEvidence(String unit) async {
    if (!_ssh.isConnected) throw StateError('Reconnect SSH first.');
    if (unit.isEmpty || unit.length > 200 ||
        !RegExp(r'^[A-Za-z0-9_.@:-]+$').hasMatch(unit)) {
      throw ArgumentError('Invalid systemd unit.');
    }
    // systemctl status returns non-zero for failed units; capture the status
    // as evidence without treating an inactive service as a transport failure.
    final command =
        'systemctl --no-pager --full status ${shQuote(unit)} 2>&1; '
        'echo "=== JOURNAL ==="; '
        'journalctl --no-pager -n 25 -u ${shQuote(unit)} 2>&1';
    final result = await _ssh.execute(command);
    // Keep enough space for the task heading and at most 8192 UTF-8 bytes
    // when passed to the CLI; truncate on valid Unicode boundaries.
    final text = 'Service: $unit\n$result';
    final bytes = utf8.encode(text);
    if (bytes.length <= 7900) return text;
    return utf8.decode(bytes.sublist(0, 7900), allowMalformed: true);
  }
}
