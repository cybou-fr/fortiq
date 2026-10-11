import 'package:flutter/material.dart';

import '../services/ssh_service.dart';
import 'remote_snapshot.dart';
import 'ui_errors.dart';
import 'ai_advice_dialog.dart';
import '../services/ai_service.dart';

class RemoteServiceManager extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;
  const RemoteServiceManager({
    super.key,
    required this.sshService,
    required this.domainColor,
  });
  @override
  State<RemoteServiceManager> createState() => _RemoteServiceManagerState();
}

class _RemoteServiceManagerState extends State<RemoteServiceManager> {
  late final _snapshot = RemoteSnapshotController(
    widget.sshService,
    load: widget.sshService.system.fetchServices,
  );
  String _filter = '', _stateFilter = 'all';
  final Set<String> _pending = {};

  @override
  void initState() {
    super.initState();
    _snapshot.addListener(_changed);
    _snapshot.refresh();
  }

  void _changed() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    _snapshot.removeListener(_changed);
    _snapshot.dispose();
    super.dispose();
  }

  Future<void> _action(String unit, String action) async {
    if (!_snapshot.available || _pending.contains(unit)) return;
    final client = widget.sshService.rawClient;
    _pending.add(unit);
    try {
      if (action == 'stop' || action == 'restart') {
        final confirm = await showDialog<bool>(
          context: context,
          builder: (context) => AlertDialog(
            title: Text('${action == 'stop' ? 'Stop' : 'Restart'} $unit?'),
            content: const Text(
              'Connections and tasks using this service may be interrupted.',
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(context, false),
                child: const Text('Cancel'),
              ),
              TextButton(
                onPressed: () => Navigator.pop(context, true),
                child: Text(action == 'stop' ? 'Stop' : 'Restart'),
              ),
            ],
          ),
        );
        if (!mounted || confirm != true) return;
      }
      if (!_snapshot.available || client != widget.sshService.rawClient) {
        if (mounted) {
          showActionError(
            context,
            action,
            'SSH session changed. Refresh before retrying.',
          );
        }
        return;
      }
      setState(() {});
      await widget.sshService.system.controlService(unit, action);
      if (mounted) {
        _snapshot.invalidate();
        await _snapshot.refresh();
      }
    } catch (error) {
      if (mounted) showActionError(context, '$action $unit', error);
    } finally {
      _pending.remove(unit);
      if (mounted) setState(() {});
    }
  }

  /// Только чтение: статус и последние строки журнала службы уходят
  /// команде `ai` на том же сервере по SSH.
  void _explain(String unit) {
    showAiAdviceDialog(
      context: context,
      ssh: widget.sshService,
      title: 'Explain $unit',
      loadContext: () => AiService(widget.sshService).serviceEvidence(unit),
    );
  }

  Future<void> _details(String unit) async {
    if (!_snapshot.available) return;
    final client = widget.sshService.rawClient;
    try {
      final details = await widget.sshService.system.fetchServiceDetails(unit);
      if (!mounted ||
          !_snapshot.available ||
          client != widget.sshService.rawClient) {
        return;
      }
      await showDialog<void>(
        context: context,
        builder: (context) => AlertDialog(
          title: Text(unit),
          content: SizedBox(
            width: 520,
            child: SingleChildScrollView(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  for (final entry in details.entries)
                    Padding(
                      padding: const EdgeInsets.symmetric(vertical: 4),
                      child: SelectableText(
                        '${entry.key}: ${entry.value.isEmpty ? '—' : entry.value}',
                      ),
                    ),
                ],
              ),
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Close'),
            ),
          ],
        ),
      );
    } catch (error) {
      if (mounted) showActionError(context, 'Service details', error);
    }
  }

  @override
  Widget build(BuildContext context) {
    final services = (_snapshot.data ?? <Map<String, dynamic>>[]).where((s) {
      final matches = '${s['unit']} ${s['description']}'.toLowerCase().contains(
        _filter.toLowerCase(),
      );
      final state = switch (_stateFilter) {
        'active' => s['active'] == 'active',
        'inactive' => s['active'] == 'inactive',
        'failed' => s['active'] == 'failed',
        'enabled' =>
          s['enabled'] == 'enabled' || s['enabled'] == 'enabled-runtime',
        _ => true,
      };
      return matches && state;
    }).toList();
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.all(8),
          child: Row(
            children: [
              IconButton(
                tooltip: 'Refresh services',
                onPressed: _snapshot.available && !_snapshot.loading
                    ? _snapshot.refresh
                    : null,
                icon: const Icon(
                  Icons.refresh,
                  size: 18,
                  color: Colors.white70,
                ),
              ),
              Expanded(
                child: TextField(
                  onChanged: (value) => setState(() => _filter = value),
                  style: const TextStyle(fontSize: 12, color: Colors.white),
                  decoration: const InputDecoration(
                    isDense: true,
                    hintText: 'Find a service',
                    hintStyle: TextStyle(color: Colors.white38),
                  ),
                ),
              ),
              PopupMenuButton<String>(
                tooltip: 'Filter service state',
                icon: const Icon(
                  Icons.filter_list,
                  size: 18,
                  color: Colors.white70,
                ),
                onSelected: (value) => setState(() => _stateFilter = value),
                itemBuilder: (_) => [
                  for (final entry in {
                    'all': 'All services',
                    'active': 'Active',
                    'inactive': 'Inactive',
                    'failed': 'Failed',
                    'enabled': 'Enabled at boot',
                  }.entries)
                    CheckedPopupMenuItem(
                      value: entry.key,
                      checked: _stateFilter == entry.key,
                      child: Text(entry.value),
                    ),
                ],
              ),
            ],
          ),
        ),
        RemoteSnapshotStatus(snapshot: _snapshot),
        Expanded(
          child: services.isEmpty
              ? const Center(
                  child: Text(
                    'No matching services',
                    style: TextStyle(color: Colors.white54),
                  ),
                )
              : ListView.builder(
                  itemCount: services.length,
                  itemBuilder: (context, index) {
                    final service = services[index];
                    final unit = service['unit'] as String;
                    final active = service['active'] as String;
                    final enabled = service['enabled'] as String? ?? '';
                    final color = active == 'active'
                        ? Colors.greenAccent
                        : active == 'failed'
                        ? Colors.redAccent
                        : Colors.white38;
                    return ListTile(
                      dense: true,
                      onTap: _snapshot.available ? () => _details(unit) : null,
                      leading: Icon(Icons.circle, size: 10, color: color),
                      title: Text(
                        unit,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          color: Colors.white,
                          fontSize: 12,
                        ),
                      ),
                      subtitle: Text(
                        '${service['description']}\n$active · ${service['sub']}${enabled.isEmpty ? '' : ' · $enabled'}',
                        maxLines: 3,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(color: color, fontSize: 11),
                      ),
                      trailing: _pending.contains(unit)
                          ? const SizedBox(
                              width: 16,
                              height: 16,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            )
                          : PopupMenuButton<String>(
                              tooltip: 'Service actions',
                              enabled:
                                  _snapshot.available && !_snapshot.loading,
                              icon: const Icon(
                                Icons.more_vert,
                                color: Colors.white60,
                                size: 18,
                              ),
                              onSelected: (action) => action == 'details'
                                  ? _details(unit)
                                  : action == 'explain'
                                  ? _explain(unit)
                                  : _action(unit, action),
                              itemBuilder: (_) => [
                                const PopupMenuItem(
                                  value: 'details',
                                  child: Text('Details'),
                                ),
                                const PopupMenuItem(
                                  value: 'explain',
                                  child: Text('Explain with server AI (read-only)'),
                                ),
                                PopupMenuItem(
                                  value: active == 'active' ? 'stop' : 'start',
                                  child: Text(
                                    active == 'active' ? 'Stop' : 'Start',
                                  ),
                                ),
                                const PopupMenuItem(
                                  value: 'restart',
                                  child: Text('Restart'),
                                ),
                              ],
                            ),
                    );
                  },
                ),
        ),
        Padding(
          padding: const EdgeInsets.all(8),
          child: Text(
            '${services.length} / ${_snapshot.data?.length ?? 0} services · $_stateFilter',
            style: const TextStyle(fontSize: 10, color: Colors.white38),
          ),
        ),
      ],
    );
  }
}
