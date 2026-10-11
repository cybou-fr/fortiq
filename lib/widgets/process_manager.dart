import 'package:flutter/material.dart';
import '../services/ssh_service.dart';

class RemoteProcessManager extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;

  const RemoteProcessManager({
    super.key,
    required this.sshService,
    required this.domainColor,
  });

  @override
  State<RemoteProcessManager> createState() => _RemoteProcessManagerState();
}

class _RemoteProcessManagerState extends State<RemoteProcessManager> {
  List<Map<String, dynamic>> _processes = [];
  bool _loading = false;
  String _filter = '';

  @override
  void initState() {
    super.initState();
    _loadProcesses();
  }

  Future<void> _loadProcesses() async {
    setState(() => _loading = true);
    try {
      final list = await widget.sshService.fetchProcesses();
      setState(() {
        _processes = list;
        _loading = false;
      });
    } catch (_) {
      setState(() => _loading = false);
    }
  }

  Future<void> _killProcess(int pid, String name) async {
    final confirm = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: const Color(0xFF1E1F29),
        title: Text('Kill Process: $name (PID: $pid)?', style: const TextStyle(color: Colors.white, fontSize: 15)),
        content: const Text('Executes: kill -9 <pid>', style: TextStyle(color: Colors.white60)),
        actions: [
          TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('Cancel')),
          ElevatedButton(
            onPressed: () => Navigator.pop(context, true),
            style: ElevatedButton.styleFrom(backgroundColor: Colors.red),
            child: const Text('Kill -9', style: TextStyle(color: Colors.white)),
          ),
        ],
      ),
    );

    if (confirm == true) {
      await widget.sshService.killProcess(pid, force: true);
      _loadProcesses();
    }
  }

  @override
  Widget build(BuildContext context) {
    final filtered = _processes.where((p) {
      final comm = (p['comm'] as String).toLowerCase();
      final user = (p['user'] as String).toLowerCase();
      return comm.contains(_filter.toLowerCase()) || user.contains(_filter.toLowerCase());
    }).toList();

    return Column(
      children: [
        // Action bar
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
          decoration: const BoxDecoration(
            color: Color(0xFF181922),
            border: Border(bottom: BorderSide(color: Color(0xFF262837))),
          ),
          child: Row(
            children: [
              IconButton(
                icon: const Icon(Icons.refresh, size: 18, color: Colors.white70),
                tooltip: 'Refresh (ps)',
                onPressed: _loadProcesses,
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Container(
                  height: 32,
                  padding: const EdgeInsets.symmetric(horizontal: 8),
                  decoration: BoxDecoration(
                    color: const Color(0xFF111218),
                    borderRadius: BorderRadius.circular(6),
                    border: Border.all(color: const Color(0xFF2C2E40)),
                  ),
                  child: Row(
                    children: [
                      const Icon(Icons.search, size: 14, color: Colors.white38),
                      const SizedBox(width: 6),
                      Expanded(
                        child: TextField(
                          style: const TextStyle(color: Colors.white, fontSize: 12),
                          decoration: const InputDecoration(
                            border: InputBorder.none,
                            hintText: 'Filter by process name or user...',
                            hintStyle: TextStyle(color: Colors.white38, fontSize: 12),
                            isDense: true,
                            contentPadding: EdgeInsets.zero,
                          ),
                          onChanged: (v) => setState(() => _filter = v),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ],
          ),
        ),

        // Header
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
          color: const Color(0xFF15161E),
          child: const Row(
            children: [
              SizedBox(width: 60, child: Text('PID', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              SizedBox(width: 80, child: Text('USER', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              SizedBox(width: 65, child: Text('CPU %', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              SizedBox(width: 65, child: Text('MEM %', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              Expanded(child: Text('COMMAND', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              SizedBox(width: 40),
            ],
          ),
        ),

        // Process rows
        Expanded(
          child: _loading
              ? Center(child: CircularProgressIndicator(color: widget.domainColor))
              : ListView.builder(
                  itemCount: filtered.length,
                  itemBuilder: (context, index) {
                    final p = filtered[index];
                    final pid = p['pid'] as int;
                    final comm = p['comm'] as String;
                    final cpu = p['cpu'] as double;
                    final mem = p['mem'] as double;
                    final user = p['user'] as String;

                    return Container(
                      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
                      decoration: const BoxDecoration(
                        border: Border(bottom: BorderSide(color: Color(0xFF1A1B24))),
                      ),
                      child: Row(
                        children: [
                          SizedBox(
                            width: 60,
                            child: Text('$pid', style: const TextStyle(color: Colors.white60, fontSize: 11, fontFamily: 'Consolas, monospace')),
                          ),
                          SizedBox(
                            width: 80,
                            child: Text(user, style: const TextStyle(color: Colors.white60, fontSize: 11)),
                          ),
                          SizedBox(
                            width: 65,
                            child: Text(
                              '${cpu.toStringAsFixed(1)}%',
                              style: TextStyle(
                                color: cpu > 20 ? Colors.orangeAccent : Colors.white70,
                                fontSize: 11,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                          ),
                          SizedBox(
                            width: 65,
                            child: Text('${mem.toStringAsFixed(1)}%', style: const TextStyle(color: Colors.white60, fontSize: 11)),
                          ),
                          Expanded(
                            child: Text(
                              comm,
                              style: const TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.w500),
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                          IconButton(
                            icon: const Icon(Icons.close, size: 14, color: Colors.redAccent),
                            tooltip: 'Kill (kill -9)',
                            padding: EdgeInsets.zero,
                            constraints: const BoxConstraints(),
                            onPressed: () => _killProcess(pid, comm),
                          ),
                        ],
                      ),
                    );
                  },
                ),
        ),
      ],
    );
  }
}
