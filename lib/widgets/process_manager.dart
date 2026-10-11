import 'package:flutter/material.dart';
import '../services/ssh_service.dart';
import 'remote_snapshot.dart';
import 'ui_errors.dart';

class RemoteProcessManager extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;
  const RemoteProcessManager({super.key, required this.sshService, required this.domainColor});
  @override
  State<RemoteProcessManager> createState() => _RemoteProcessManagerState();
}

class _RemoteProcessManagerState extends State<RemoteProcessManager> {
  late final _snapshot = RemoteSnapshotController(widget.sshService,
    load: widget.sshService.system.fetchProcesses);
  String _filter = '', _sort = 'cpu';
  final Set<int> _pending = {};

  @override
  void initState() {
    super.initState();
    _snapshot.addListener(_changed);
    _snapshot.refresh();
  }
  void _changed() { if (mounted) setState(() {}); }
  @override
  void dispose() { _snapshot.removeListener(_changed); _snapshot.dispose(); super.dispose(); }

  Future<void> _terminate(Map<String, dynamic> process, {required bool force}) async {
    final pid = process['pid'] as int;
    if (_pending.contains(pid) || !_snapshot.available) return;
    final client = widget.sshService.rawClient;
    _pending.add(pid);
    try {
      final confirm = await showDialog<bool>(context: context, builder: (context) => AlertDialog(
        title: Text('${force ? 'Force quit' : 'End'} ${process['comm']}?'),
        content: Text('PID $pid · ${process['user']}\n${force ? 'Force quit stops the process immediately. Unsaved work may be lost.' : 'The process will be asked to terminate normally.'}'),
        actions: [TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('Cancel')),
          TextButton(onPressed: () => Navigator.pop(context, true), child: Text(force ? 'Force quit' : 'End process'))],
      ));
      if (!mounted || confirm != true) return;
      if (!_snapshot.available || client != widget.sshService.rawClient) {
        showActionError(context, 'End process', 'SSH session changed. Refresh before retrying.'); return;
      }
      setState(() {});
      await widget.sshService.system.killProcess(pid, force: force);
      if (mounted) await _snapshot.refresh();
    } catch (error) {
      if (mounted) showActionError(context, 'End process $pid', error);
    } finally {
      _pending.remove(pid);
      if (mounted) setState(() {});
    }
  }

  @override
  Widget build(BuildContext context) {
    final filter = _filter.toLowerCase();
    final processes = (_snapshot.data ?? <Map<String, dynamic>>[]).where((p) =>
      '${p['comm']} ${p['user']} ${p['pid']}'.toLowerCase().contains(filter)).toList();
    processes.sort((a, b) {
      final result = switch (_sort) {
        'pid' => (a['pid'] as int).compareTo(b['pid'] as int),
        'name' => '${a['comm']}'.compareTo('${b['comm']}'),
        'mem' => (b['mem'] as num).compareTo(a['mem'] as num),
        _ => (b['cpu'] as num).compareTo(a['cpu'] as num),
      };
      return result == 0 ? (a['pid'] as int).compareTo(b['pid'] as int) : result;
    });
    return Column(children: [
      Padding(padding: const EdgeInsets.all(8), child: Row(children: [
        IconButton(tooltip: 'Refresh processes', onPressed: _snapshot.available && !_snapshot.loading ? _snapshot.refresh : null,
          icon: const Icon(Icons.refresh, color: Colors.white70, size: 18)),
        Expanded(child: TextField(onChanged: (value) => setState(() => _filter = value),
          style: const TextStyle(color: Colors.white, fontSize: 12),
          decoration: const InputDecoration(isDense: true, hintText: 'Name, user or PID', hintStyle: TextStyle(color: Colors.white38)))),
        PopupMenuButton<String>(tooltip: 'Sort processes', icon: const Icon(Icons.sort, color: Colors.white70, size: 18),
          onSelected: (value) => setState(() => _sort = value),
          itemBuilder: (_) => [for (final entry in {'cpu': 'CPU (highest first)', 'mem': 'Memory (highest first)', 'name': 'Name', 'pid': 'PID'}.entries)
            CheckedPopupMenuItem(value: entry.key, checked: _sort == entry.key, child: Text(entry.value))]),
      ])),
      RemoteSnapshotStatus(snapshot: _snapshot),
      Expanded(child: processes.isEmpty ? Center(child: Text(_snapshot.loading ? 'Loading processes…' : 'No matching processes', style: const TextStyle(color: Colors.white54)))
        : ListView.builder(itemCount: processes.length, itemBuilder: (context, index) {
          final process = processes[index];
          final pid = process['pid'] as int;
          final cpu = (process['cpu'] as num).toDouble(), memory = (process['mem'] as num).toDouble();
          return ListTile(dense: true,
            title: Text('${process['comm']}', maxLines: 1, overflow: TextOverflow.ellipsis,
              style: const TextStyle(color: Colors.white, fontSize: 13)),
            subtitle: Text('PID $pid · ${process['user']} · CPU ${cpu.toStringAsFixed(1)}% · RAM ${memory.toStringAsFixed(1)}%',
              style: TextStyle(color: cpu > 20 ? Colors.orangeAccent : Colors.white54, fontSize: 11)),
            trailing: _pending.contains(pid) ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
              : PopupMenuButton<bool>(tooltip: 'Process actions', enabled: _snapshot.available && !_snapshot.loading,
                icon: const Icon(Icons.more_vert, size: 18, color: Colors.white60),
                onSelected: (force) => _terminate(process, force: force), itemBuilder: (_) => const [
                  PopupMenuItem(value: false, child: Text('End process')),
                  PopupMenuItem(value: true, child: Text('Force quit')),
                ]),
          );
        })),
      Padding(padding: const EdgeInsets.all(8), child: Text('${processes.length} / ${_snapshot.data?.length ?? 0} processes · CPU is ps lifetime average',
        style: const TextStyle(color: Colors.white38, fontSize: 10))),
    ]);
  }
}
