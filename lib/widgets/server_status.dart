import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../services/server_setup_service.dart';

/// Small status indicator for the top panel: shows whether the server has
/// everything Fortiq needs and is up to date. Tap for details and actions.
class ServerStatusChip extends StatelessWidget {
  final ServerSetupService setup;

  const ServerStatusChip({super.key, required this.setup});

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: setup,
      builder: (context, _) {
        final st = setup.status;
        late final IconData icon;
        late final Color color;
        late final String text;

        if (setup.busy) {
          icon = Icons.sync;
          color = Colors.lightBlueAccent;
          text = setup.busyMessage;
        } else if (setup.error != null) {
          icon = Icons.error_outline;
          color = Colors.redAccent;
          text = 'Setup error';
        } else if (st == null) {
          return const SizedBox.shrink();
        } else if (st.missing.isNotEmpty) {
          icon = Icons.warning_amber;
          color = Colors.redAccent;
          text = 'Missing: ${st.missing.map((r) => r.package).join(', ')}';
        } else if (st.upgradable.isNotEmpty) {
          icon = Icons.system_update_alt;
          color = Colors.amberAccent;
          text = st.securityUpdates > 0
              ? '${st.upgradable.length} updates (${st.securityUpdates} security)'
              : '${st.upgradable.length} updates';
        } else if (st.rebootRequired) {
          icon = Icons.restart_alt;
          color = Colors.amberAccent;
          text = 'Reboot required';
        } else {
          icon = Icons.check_circle_outline;
          color = Colors.greenAccent;
          text = 'Server up to date';
        }

        return InkWell(
          borderRadius: BorderRadius.circular(10),
          onTap: () => showServerStatusDialog(context, setup),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                if (setup.busy)
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

Future<void> showServerStatusDialog(BuildContext context, ServerSetupService setup) {
  return showDialog<void>(
    context: context,
    builder: (context) => _ServerStatusDialog(setup: setup),
  );
}

class _ServerStatusDialog extends StatelessWidget {
  final ServerSetupService setup;

  const _ServerStatusDialog({required this.setup});

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: setup,
      builder: (context, _) {
        final st = setup.status;
        final missingNames = st?.missing.map((r) => r.package).toSet() ?? <String>{};
        const mono = TextStyle(fontFamily: 'Consolas', fontSize: 11, color: Colors.white70);

        return AlertDialog(
          backgroundColor: const Color(0xFF1E1F29),
          title: const Text('Server setup', style: TextStyle(color: Colors.white, fontSize: 16)),
          content: SizedBox(
            width: 520,
            child: SingleChildScrollView(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  if (st == null)
                    const Text('Not checked yet.', style: TextStyle(color: Colors.white70))
                  else ...[
                    Text(
                      '${st.os}  ·  ${st.isRoot ? 'root' : st.hasSudo ? 'sudo without password' : 'no root access'}',
                      style: const TextStyle(color: Colors.white54, fontSize: 12),
                    ),
                    const SizedBox(height: 12),
                    const Text('Required packages',
                        style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13)),
                    const SizedBox(height: 4),
                    for (final r in kRequiredPackages)
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 1),
                        child: Row(
                          children: [
                            Icon(
                              missingNames.contains(r.package) ? Icons.cancel : Icons.check_circle,
                              size: 14,
                              color: missingNames.contains(r.package) ? Colors.redAccent : Colors.greenAccent,
                            ),
                            const SizedBox(width: 8),
                            Text(r.package, style: const TextStyle(color: Colors.white, fontSize: 12)),
                            const SizedBox(width: 8),
                            Expanded(
                              child: Text(r.purpose,
                                  style: const TextStyle(color: Colors.white38, fontSize: 11)),
                            ),
                          ],
                        ),
                      ),
                    const SizedBox(height: 12),
                    Text(
                      st.upgradable.isEmpty
                          ? 'System updates: none available'
                          : 'System updates: ${st.upgradable.length} available'
                              '${st.securityUpdates > 0 ? ' (${st.securityUpdates} security)' : ''}',
                      style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
                    ),
                    if (st.upgradable.isNotEmpty)
                      Padding(
                        padding: const EdgeInsets.only(top: 2),
                        child: Text(
                          st.upgradable.take(20).join(', ') + (st.upgradable.length > 20 ? ', …' : ''),
                          style: const TextStyle(color: Colors.white54, fontSize: 11),
                        ),
                      ),
                    if (st.rebootRequired)
                      const Padding(
                        padding: EdgeInsets.only(top: 6),
                        child: Text('A reboot is required to finish earlier updates.',
                            style: TextStyle(color: Colors.amberAccent, fontSize: 12)),
                      ),
                    if (!st.canElevate) ...[
                      const SizedBox(height: 12),
                      const Text(
                        'This user cannot install packages (no root, no passwordless sudo). '
                        'Ask an administrator to run:',
                        style: TextStyle(color: Colors.orangeAccent, fontSize: 12),
                      ),
                      const SizedBox(height: 4),
                      Row(
                        children: [
                          Expanded(
                            child: SelectableText(
                              'sudo apt-get install -y ${kRequiredPackages.map((r) => r.package).join(' ')}',
                              style: mono,
                            ),
                          ),
                          IconButton(
                            tooltip: 'Copy',
                            icon: const Icon(Icons.copy, size: 14, color: Colors.white54),
                            onPressed: () => Clipboard.setData(ClipboardData(
                              text:
                                  'sudo apt-get install -y ${kRequiredPackages.map((r) => r.package).join(' ')}',
                            )),
                          ),
                        ],
                      ),
                    ],
                    const SizedBox(height: 8),
                    CheckboxListTile(
                      contentPadding: EdgeInsets.zero,
                      dense: true,
                      controlAffinity: ListTileControlAffinity.leading,
                      value: setup.autoUpdate,
                      onChanged: setup.busy || !st.canElevate
                          ? null
                          : (v) => setup.setAutoUpdate(v ?? false),
                      title: const Text('Install system updates automatically on connect',
                          style: TextStyle(color: Colors.white70, fontSize: 12)),
                      subtitle: const Text(
                        'Runs apt-get upgrade: no prompts, services are not restarted, edited config files are kept. '
                        'Packages Fortiq needs are always installed and updated.',
                        style: TextStyle(color: Colors.white38, fontSize: 11),
                      ),
                    ),
                  ],
                  if (setup.busy) ...[
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        const SizedBox(width: 12, height: 12, child: CircularProgressIndicator(strokeWidth: 2)),
                        const SizedBox(width: 8),
                        Text(setup.busyMessage, style: const TextStyle(color: Colors.lightBlueAccent, fontSize: 12)),
                      ],
                    ),
                  ],
                  if (setup.error != null) ...[
                    const SizedBox(height: 8),
                    Text(setup.error!, style: const TextStyle(color: Colors.redAccent, fontSize: 12)),
                  ],
                  if (setup.log.isNotEmpty) ...[
                    const SizedBox(height: 10),
                    Container(
                      height: 150,
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
            if (st != null && st.missing.isNotEmpty && st.canElevate)
              ElevatedButton(
                onPressed: setup.busy ? null : setup.installMissing,
                child: const Text('Install missing'),
              ),
            if (st != null && st.upgradable.isNotEmpty && st.canElevate)
              ElevatedButton(
                onPressed: setup.busy ? null : setup.installUpdates,
                child: const Text('Install updates'),
              ),
            TextButton(
              onPressed: setup.busy ? null : setup.run,
              child: const Text('Check again'),
            ),
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Close'),
            ),
          ],
        );
      },
    );
  }
}
