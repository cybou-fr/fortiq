import 'package:flutter/material.dart';

import '../services/ai_service.dart';
import '../services/ssh_service.dart';

/// A read-only, explicitly user-triggered explanation of server data.
/// The selected context is sent only to the `ai` command on this SSH host.
Future<void> showAiAdviceDialog({
  required BuildContext context,
  required SshClientService ssh,
  required String title,
  required Future<String> Function() loadContext,
}) {
  return showDialog<void>(
    context: context,
    builder: (_) => _AiAdviceDialog(
      ssh: ssh,
      title: title,
      loadContext: loadContext,
    ),
  );
}

class _AiAdviceDialog extends StatefulWidget {
  final SshClientService ssh;
  final String title;
  final Future<String> Function() loadContext;

  const _AiAdviceDialog({
    required this.ssh,
    required this.title,
    required this.loadContext,
  });

  @override
  State<_AiAdviceDialog> createState() => _AiAdviceDialogState();
}

class _AiAdviceDialogState extends State<_AiAdviceDialog> {
  bool _busy = false;
  bool _cache = false;
  bool _unavailable = false;
  AiAdvice? _advice;
  String? _error;

  Future<void> _run() async {
    if (_busy) return;
    // Do not show the result of an operation started under a different SSH
    // connection after a reconnect or a server switch.
    final source = widget.ssh.rawClient;
    if (source == null || !widget.ssh.isConnected) {
      setState(() => _error = 'Reconnect SSH to use server AI.');
      return;
    }
    setState(() {
      _busy = true;
      _error = null;
      _advice = null;
    });
    try {
      // Check server resources before fetching potentially sensitive logs.
      // Neither status() nor this dialog starts a CPU model or installs it.
      final status = await AiService(widget.ssh).status();
      if (!identical(source, widget.ssh.rawClient)) {
        throw StateError('SSH session changed. AI request was not sent.');
      }
      if (!status.available) {
        _unavailable = true;
        throw StateError(
          'AI disabled on this Linux server: ${status.reason ?? 'unknown'} '
          '(${status.availableMemoryMb} MB available). '
          'Other FORTIQ tools are unaffected.',
        );
      }
      final contextText = await widget.loadContext();
      if (!identical(source, widget.ssh.rawClient)) {
        throw StateError('SSH session changed. AI request was not sent.');
      }
      final answer = await AiService(widget.ssh).explain(
        contextText,
        allowServerCache: _cache,
      );
      if (!identical(source, widget.ssh.rawClient)) {
        throw StateError('SSH session changed. AI result was discarded.');
      }
      if (mounted) setState(() => _advice = answer);
    } catch (error) {
      if (mounted) setState(() => _error = error.toString());
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
    backgroundColor: const Color(0xFF1B1C27),
    title: Text(widget.title, style: const TextStyle(color: Colors.white)),
    content: SizedBox(
      width: 560,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            'A compact llama.cpp model on the selected Linux server '
            'will explain these logs. AI only advises; it cannot change '
            'services or execute proposed commands.',
            style: TextStyle(color: Colors.white70),
          ),
          const SizedBox(height: 12),
          CheckboxListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text(
              'Cache this advice on the Linux server (10 minutes)',
              style: TextStyle(color: Colors.white, fontSize: 13),
            ),
            subtitle: const Text(
              'May store sensitive information extracted from logs. '
              'Off by default.',
              style: TextStyle(color: Colors.white54, fontSize: 11),
            ),
            value: _cache,
            onChanged: _busy ? null : (v) => setState(() => _cache = v ?? false),
            controlAffinity: ListTileControlAffinity.leading,
          ),
          if (_busy) const LinearProgressIndicator(minHeight: 3),
          if (_error != null)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 10),
              child: Text(_error!, style: const TextStyle(color: Colors.redAccent)),
            ),
          if (_advice != null) ...[
            if (_advice!.cached)
              Text('Cached answer · ${_advice!.ageSeconds}s old',
                  style: const TextStyle(color: Colors.amberAccent, fontSize: 12))
            else
              const Text('Generated on Linux', style: TextStyle(color: Colors.greenAccent, fontSize: 12)),
            const SizedBox(height: 8),
            ConstrainedBox(
              constraints: const BoxConstraints(maxHeight: 310),
              child: SingleChildScrollView(
                child: SelectableText(_advice!.text,
                    style: const TextStyle(color: Colors.white, fontSize: 13)),
              ),
            ),
            const SizedBox(height: 8),
            const Text(
              'Verify the system with Linux tools before making changes.',
              style: TextStyle(color: Colors.white54, fontSize: 11),
            ),
          ],
        ],
      ),
    ),
    actions: [
      TextButton(
        onPressed: _busy ? null : () => Navigator.pop(context),
        child: const Text('Close'),
      ),
      FilledButton.icon(
        onPressed: _busy || _unavailable ? null : _run,
        icon: const Icon(Icons.auto_awesome, size: 16),
        label: Text(_advice == null ? 'Explain' : 'Explain again'),
      ),
    ],
  );
}
