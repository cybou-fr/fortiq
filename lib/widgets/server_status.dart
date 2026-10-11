import 'package:flutter/material.dart';
import '../services/server_setup_service.dart';
import 'mono.dart';

/// Small indicator for the top panel: is the server set up, up to date and
/// protected? Tap to see the steps and the log.
class ServerStatusChip extends StatelessWidget {
  final ServerSetupService setup;

  const ServerStatusChip({super.key, required this.setup});

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: setup,
      builder: (context, _) {
        late final IconData icon;
        late final Color color;
        late final String text;
        var spinner = false;

        final failed = setup.steps.where((s) => s.state == StepStatus.failed).length;
        final unknown = setup.steps.where((s) => s.state == StepStatus.unknown).length;
        final todo = setup.steps.where((s) => s.willRun && s.state == StepStatus.pending).length;

        if (setup.phase == SetupPhase.inspecting) {
          spinner = true;
          icon = Icons.sync;
          color = Colors.lightBlueAccent;
          text = 'Checking the server…';
        } else if (setup.phase == SetupPhase.running) {
          spinner = true;
          icon = Icons.sync;
          color = Colors.lightBlueAccent;
          text = 'Setting up the server…';
        } else if (setup.error != null) {
          icon = Icons.error_outline;
          color = Colors.redAccent;
          text = 'Setup error';
        } else if (setup.status == null) {
          return const SizedBox.shrink();
        } else if (failed > 0 || unknown > 0) {
          icon = Icons.warning_amber;
          color = Colors.redAccent;
          text = unknown > 0 ? 'Setup: result unknown' : 'Setup: $failed failed';
        } else if (!setup.status!.isSupportedOs) {
          icon = Icons.info_outline;
          color = Colors.white54;
          text = 'Not Debian/Ubuntu';
        } else if (!setup.status!.canElevate) {
          icon = Icons.lock_outline;
          color = Colors.orangeAccent;
          text = 'No root/sudo: not set up';
        } else if (todo > 0) {
          icon = Icons.pending_actions;
          color = Colors.amberAccent;
          text = '$todo to do';
        } else if (setup.status!.rebootRequired) {
          icon = Icons.restart_alt;
          color = Colors.amberAccent;
          text = 'Reboot required';
        } else {
          icon = Icons.verified_user_outlined;
          color = Colors.greenAccent;
          text = 'Server ready';
        }

        return InkWell(
          borderRadius: BorderRadius.circular(10),
          onTap: () => showServerSetupDialog(context, setup),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                if (spinner)
                  SizedBox(
                    width: 11,
                    height: 11,
                    child: CircularProgressIndicator(strokeWidth: 2, color: color),
                  )
                else
                  Icon(icon, size: 13, color: color),
                const SizedBox(width: 5),
                Text(text, style: TextStyle(color: color, fontSize: 11)),
              ],
            ),
          ),
        );
      },
    );
  }
}

/// The setup dialog: the steps, their state and the live log. With
/// [runOnOpen] it starts doing what the check found as soon as it appears.
Future<void> showServerSetupDialog(
  BuildContext context,
  ServerSetupService setup, {
  bool runOnOpen = false,
}) {
  return showDialog<void>(
    context: context,
    // Not while it is working: leaving would hide what is being changed.
    barrierDismissible: false,
    builder: (context) => _SetupDialog(setup: setup, runOnOpen: runOnOpen),
  );
}

class _SetupDialog extends StatefulWidget {
  final ServerSetupService setup;
  final bool runOnOpen;

  const _SetupDialog({required this.setup, required this.runOnOpen});

  @override
  State<_SetupDialog> createState() => _SetupDialogState();
}

class _SetupDialogState extends State<_SetupDialog> {
  @override
  void initState() {
    super.initState();
    if (widget.runOnOpen) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted && !widget.setup.busy) widget.setup.run();
      });
    }
  }

  static Widget _stateIcon(SetupStep s) {
    switch (s.state) {
      case StepStatus.running:
        return const SizedBox(
          width: 14,
          height: 14,
          child: CircularProgressIndicator(strokeWidth: 2, color: Colors.lightBlueAccent),
        );
      case StepStatus.done:
        return const Icon(Icons.check_circle, size: 16, color: Colors.greenAccent);
      case StepStatus.upToDate:
        return const Icon(Icons.check_circle_outline, size: 16, color: Colors.white38);
      case StepStatus.failed:
        return const Icon(Icons.cancel, size: 16, color: Colors.redAccent);
      case StepStatus.unknown:
        return const Icon(Icons.help, size: 16, color: Colors.redAccent);
      case StepStatus.skipped:
        return const Icon(Icons.remove_circle_outline, size: 16, color: Colors.white38);
      case StepStatus.pending:
        return Icon(
          s.willRun ? Icons.radio_button_unchecked : Icons.remove_circle_outline,
          size: 16,
          color: s.willRun ? Colors.amberAccent : Colors.white38,
        );
    }
  }

  static String _stateText(SetupStep s) {
    if (s.unavailable != null) return s.unavailable!;
    if (s.note != null) return s.note!;
    switch (s.state) {
      case StepStatus.upToDate:
        return 'Already in place.';
      case StepStatus.pending:
        return s.needed ? s.what : 'Already in place.';
      default:
        return s.what;
    }
  }

  @override
  Widget build(BuildContext context) {
    final setup = widget.setup;
    return ListenableBuilder(
      listenable: setup,
      builder: (context, _) {
        final st = setup.status;
        const mono = TextStyle(
          fontFamily: kMonoFont,
          fontFamilyFallback: kMonoFontFallback,
          fontSize: 11,
          color: Colors.white70,
        );
        final done = setup.steps.where((s) => s.state == StepStatus.done).length;
        final failed = setup.steps.where((s) => s.state == StepStatus.failed).length;
        final unknown = setup.steps.where((s) => s.state == StepStatus.unknown).length;

        return PopScope(
          canPop: !setup.busy,
          child: AlertDialog(
            backgroundColor: const Color(0xFF1E1F29),
            title: Text(
              setup.phase == SetupPhase.running
                  ? 'Setting up ${setup.host}'
                  : 'Server setup: ${setup.host}',
              style: const TextStyle(color: Colors.white, fontSize: 16),
            ),
            content: SizedBox(
              width: 560,
              child: SingleChildScrollView(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    if (st != null)
                      Text(
                        '${st.os}  ·  ${st.isRoot ? 'root' : st.hasSudo ? 'sudo without password' : 'no root access'}'
                        '${st.upgradable.isNotEmpty ? '  ·  ${st.upgradable.length} updates' : ''}'
                        '${st.securityUpdates > 0 ? ' (${st.securityUpdates} security)' : ''}',
                        style: const TextStyle(color: Colors.white54, fontSize: 12),
                      ),
                    const SizedBox(height: 10),
                    for (final s in setup.steps)
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 4),
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Padding(
                              padding: const EdgeInsets.only(top: 1),
                              child: _stateIcon(s),
                            ),
                            const SizedBox(width: 10),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text(
                                    s.title,
                                    style: TextStyle(
                                      color: s.unavailable != null ? Colors.white38 : Colors.white,
                                      fontSize: 13,
                                      fontWeight: FontWeight.w600,
                                    ),
                                  ),
                                  Text(
                                    _stateText(s),
                                    style: TextStyle(
                                      color: s.state == StepStatus.failed || s.state == StepStatus.unknown
                                          ? Colors.redAccent
                                          : Colors.white54,
                                      fontSize: 11,
                                    ),
                                  ),
                                ],
                              ),
                            ),
                          ],
                        ),
                      ),
                    if (st != null && st.rebootRequired)
                      const Padding(
                        padding: EdgeInsets.only(top: 6),
                        child: Text('A reboot is required to finish earlier updates.',
                            style: TextStyle(color: Colors.amberAccent, fontSize: 12)),
                      ),
                    if (setup.phase == SetupPhase.finished)
                      Padding(
                        padding: const EdgeInsets.only(top: 8),
                        child: Text(
                          failed + unknown > 0
                              ? 'Finished with problems: $done done, ${failed + unknown} not completed. See the log.'
                              : done > 0
                                  ? 'Finished: $done steps done.'
                                  : 'Finished: nothing needed doing.',
                          style: TextStyle(
                            color: failed + unknown > 0 ? Colors.redAccent : Colors.greenAccent,
                            fontSize: 12,
                          ),
                        ),
                      ),
                    if (setup.error != null)
                      Padding(
                        padding: const EdgeInsets.only(top: 8),
                        child: Text(setup.error!,
                            style: const TextStyle(color: Colors.redAccent, fontSize: 12)),
                      ),
                    if (setup.log.isNotEmpty) ...[
                      const SizedBox(height: 10),
                      Container(
                        height: 190,
                        width: double.infinity,
                        padding: const EdgeInsets.all(8),
                        color: const Color(0xFF0C0D14),
                        child: SingleChildScrollView(
                          reverse: true,
                          child: SelectableText(setup.log.join('\n'), style: mono),
                        ),
                      ),
                    ],
                  ],
                ),
              ),
            ),
            actions: [
              if (setup.phase == SetupPhase.running)
                TextButton(
                  onPressed: setup.requestStop,
                  child: const Text('Stop after this step'),
                )
              else ...[
                TextButton(
                  onPressed: setup.busy ? null : setup.inspect,
                  child: const Text('Check again'),
                ),
                if (setup.hasWork)
                  ElevatedButton(
                    onPressed: setup.run,
                    child: const Text('Run setup now'),
                  ),
                TextButton(
                  onPressed: () => Navigator.pop(context),
                  child: const Text('Close'),
                ),
              ],
            ],
          ),
        );
      },
    );
  }
}
