import 'package:flutter/material.dart';
import '../services/ssh_service.dart';

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
  List<Map<String, dynamic>> _services = [];
  bool _loading = false;
  String _filter = '';

  @override
  void initState() {
    super.initState();
    _loadServices();
  }

  Future<void> _loadServices() async {
    setState(() => _loading = true);
    try {
      final list = await widget.sshService.fetchServices();
      setState(() {
        _services = list;
        _loading = false;
      });
    } catch (_) {
      setState(() => _loading = false);
    }
  }

  Future<void> _action(String unit, String act) async {
    await widget.sshService.controlService(unit, act);
    _loadServices();
  }

  @override
  Widget build(BuildContext context) {
    final filtered = _services.where((s) {
      final unit = (s['unit'] as String).toLowerCase();
      final desc = (s['description'] as String).toLowerCase();
      return unit.contains(_filter.toLowerCase()) || desc.contains(_filter.toLowerCase());
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
                tooltip: 'Refresh (systemctl)',
                onPressed: _loadServices,
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
                            hintText: 'Filter systemd units...',
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

        // List
        Expanded(
          child: _loading
              ? Center(child: CircularProgressIndicator(color: widget.domainColor))
              : ListView.builder(
                  itemCount: filtered.length,
                  itemBuilder: (context, index) {
                    final s = filtered[index];
                    final unit = s['unit'] as String;
                    final active = s['active'] as String;
                    final sub = s['sub'] as String;
                    final desc = s['description'] as String;
                    final isRunning = active == 'active';

                    return Container(
                      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                      decoration: const BoxDecoration(
                        border: Border(bottom: BorderSide(color: Color(0xFF1A1B24))),
                      ),
                      child: Row(
                        children: [
                          Container(
                            width: 8,
                            height: 8,
                            decoration: BoxDecoration(
                              shape: BoxShape.circle,
                              color: isRunning ? const Color(0xFF10B981) : Colors.redAccent,
                            ),
                          ),
                          const SizedBox(width: 12),
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  unit,
                                  style: const TextStyle(
                                    color: Colors.white,
                                    fontSize: 12,
                                    fontWeight: FontWeight.w600,
                                  ),
                                ),
                                if (desc.isNotEmpty)
                                  Text(
                                    desc,
                                    style: const TextStyle(color: Colors.white38, fontSize: 11),
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                  ),
                              ],
                            ),
                          ),
                          Container(
                            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                            decoration: BoxDecoration(
                              color: isRunning ? Colors.green.withValues(alpha: 0.15) : Colors.red.withValues(alpha: 0.15),
                              borderRadius: BorderRadius.circular(4),
                              border: Border.all(
                                color: isRunning ? Colors.green.withValues(alpha: 0.5) : Colors.red.withValues(alpha: 0.5),
                              ),
                            ),
                            child: Text(
                              sub,
                              style: TextStyle(
                                color: isRunning ? Colors.greenAccent : Colors.redAccent,
                                fontSize: 10,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ),
                          const SizedBox(width: 12),
                          IconButton(
                            icon: const Icon(Icons.refresh, size: 16, color: Colors.white70),
                            tooltip: 'Restart (systemctl restart)',
                            onPressed: () => _action(unit, 'restart'),
                          ),
                          IconButton(
                            icon: Icon(
                              isRunning ? Icons.stop : Icons.play_arrow,
                              size: 16,
                              color: isRunning ? Colors.orangeAccent : Colors.greenAccent,
                            ),
                            tooltip: isRunning ? 'Stop' : 'Start',
                            onPressed: () => _action(unit, isRunning ? 'stop' : 'start'),
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
