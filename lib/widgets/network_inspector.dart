import 'package:flutter/material.dart';
import 'mono.dart';
import '../services/ssh_service.dart';
import 'ui_errors.dart';

class RemoteNetworkInspector extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;

  const RemoteNetworkInspector({
    super.key,
    required this.sshService,
    required this.domainColor,
  });

  @override
  State<RemoteNetworkInspector> createState() => _RemoteNetworkInspectorState();
}

class _RemoteNetworkInspectorState extends State<RemoteNetworkInspector> {

  // Async loads may finish after the window was closed.
  @override
  void setState(VoidCallback fn) {
    if (mounted) super.setState(fn);
  }
  int _currentTab = 0; // 0: Ports, 1: UFW, 2: Interfaces
  bool _loading = false;

  List<Map<String, dynamic>> _openPorts = [];
  Map<String, dynamic> _ufwData = {'installed': false, 'active': false, 'rules': []};
  List<Map<String, dynamic>> _interfaces = [];

  // Ошибки по вкладкам: сбой одной вкладки не должен выглядеть как «пусто»
  // и не должен прятать остальные.
  Object? _portsError;
  Object? _interfacesError;

  final TextEditingController _portController = TextEditingController();
  String _ruleAction = 'allow';

  @override
  void initState() {
    super.initState();
    _refreshAll();
  }

  @override
  void dispose() {
    _portController.dispose();
    super.dispose();
  }

  Future<void> _refreshAll() async {
    setState(() => _loading = true);
    final net = widget.sshService.network;
    var ports = <Map<String, dynamic>>[];
    var ufw = <String, dynamic>{'installed': false, 'active': false, 'rules': []};
    var ifaces = <Map<String, dynamic>>[];
    Object? portsError, interfacesError;
    try {
      ports = await net.fetchOpenPorts();
    } catch (e) {
      portsError = e;
    }
    try {
      ufw = await net.fetchUfwStatus();
    } catch (e) {
      ufw = {'installed': true, 'active': false, 'rules': [], 'error': '$e'};
    }
    try {
      ifaces = await net.fetchNetworkInterfaces();
    } catch (e) {
      interfacesError = e;
    }
    setState(() {
      _openPorts = ports;
      _ufwData = ufw;
      _interfaces = ifaces;
      _portsError = portsError;
      _interfacesError = interfacesError;
      _loading = false;
    });
  }

  /// Ошибка загрузки вкладки с кнопкой повтора (вместо пустого списка).
  Widget _errorView(Object error) {
    final text = error.toString().replaceFirst('Exception: ', '');
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.error_outline, size: 40, color: Colors.redAccent),
            const SizedBox(height: 10),
            Text(text, textAlign: TextAlign.center, style: const TextStyle(color: Colors.white70)),
            const SizedBox(height: 12),
            OutlinedButton(onPressed: _refreshAll, child: const Text('Retry')),
          ],
        ),
      ),
    );
  }

  Future<void> _addRule() async {
    final port = _portController.text.trim();
    if (port.isEmpty) return;

    _portController.clear();
    try {
      await widget.sshService.network.addUfwRule(port, _ruleAction);
    } catch (e) {
      showActionError(context, 'Add firewall rule', e);
    }
    _refreshAll();
  }

  Future<void> _deleteRule(int ruleNum) async {
    try {
      await widget.sshService.network.deleteUfwRule(ruleNum);
    } catch (e) {
      showActionError(context, 'Delete firewall rule', e);
    }
    _refreshAll();
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: const Color(0xFF13141E),
      child: Column(
        children: [
          // Navigation Tab Bar with Direct Buttons
          Container(
            height: 40,
            padding: const EdgeInsets.symmetric(horizontal: 10),
            decoration: const BoxDecoration(
              color: Color(0xFF161722),
              border: Border(bottom: BorderSide(color: Color(0xFF242637))),
            ),
            child: Row(
              children: [
                _tabButton(0, Icons.radar, 'Listening Ports'),
                const SizedBox(width: 6),
                _tabButton(1, Icons.security, 'UFW Firewall'),
                const SizedBox(width: 6),
                _tabButton(2, Icons.settings_ethernet, 'Interfaces & IPs'),
                const Spacer(),
                IconButton(
                  icon: const Icon(Icons.refresh, size: 16, color: Colors.white70),
                  tooltip: 'Refresh',
                  onPressed: _refreshAll,
                ),
              ],
            ),
          ),

          // Tab Content
          Expanded(
            child: _loading
                ? Center(child: CircularProgressIndicator(color: widget.domainColor))
                : _buildTabBody(),
          ),
        ],
      ),
    );
  }

  Widget _tabButton(int index, IconData icon, String title) {
    final isSelected = _currentTab == index;
    return InkWell(
      onTap: () => setState(() => _currentTab = index),
      borderRadius: BorderRadius.circular(6),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
        decoration: BoxDecoration(
          color: isSelected ? widget.domainColor.withValues(alpha: 0.2) : Colors.transparent,
          borderRadius: BorderRadius.circular(6),
          border: Border.all(
            color: isSelected ? widget.domainColor : Colors.transparent,
            width: 1,
          ),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 14, color: isSelected ? widget.domainColor : Colors.white60),
            const SizedBox(width: 6),
            Text(
              title,
              style: TextStyle(
                color: isSelected ? Colors.white : Colors.white60,
                fontSize: 12,
                fontWeight: isSelected ? FontWeight.bold : FontWeight.normal,
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildTabBody() {
    switch (_currentTab) {
      case 0:
        return _buildPortsTab();
      case 1:
        return _buildUfwTab();
      case 2:
        return _buildInterfacesTab();
      default:
        return _buildPortsTab();
    }
  }

  Widget _buildPortsTab() {
    if (_portsError != null) return _errorView(_portsError!);
    return Column(
      children: [
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
          color: const Color(0xFF13141E),
          child: const Row(
            children: [
              SizedBox(width: 70, child: Text('PROTO', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              SizedBox(width: 170, child: Text('LOCAL ADDRESS', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              Expanded(child: Text('PROCESS / SERVICE', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
            ],
          ),
        ),
        Expanded(
          child: _openPorts.isEmpty
              ? const Center(child: Text('No listening ports detected', style: TextStyle(color: Colors.white38)))
              : ListView.builder(
                  itemCount: _openPorts.length,
                  itemBuilder: (context, index) {
                    final item = _openPorts[index];
                    final proto = item['proto'] as String;
                    final addr = item['address'] as String;
                    final proc = item['process'] as String;

                    return Container(
                      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                      decoration: const BoxDecoration(
                        border: Border(bottom: BorderSide(color: Color(0xFF1C1D28))),
                      ),
                      child: Row(
                        children: [
                          SizedBox(
                            width: 70,
                            child: Container(
                              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                              decoration: BoxDecoration(
                                color: widget.domainColor.withValues(alpha: 0.15),
                                borderRadius: BorderRadius.circular(4),
                              ),
                              child: Text(
                                proto.toUpperCase(),
                                style: TextStyle(color: widget.domainColor, fontSize: 10, fontWeight: FontWeight.bold),
                                textAlign: TextAlign.center,
                              ),
                            ),
                          ),
                          SizedBox(
                            width: 170,
                            child: Text(
                              addr,
                              style: const TextStyle(color: Colors.white, fontSize: 12, fontFamily: kMonoFont, fontFamilyFallback: kMonoFontFallback),
                            ),
                          ),
                          Expanded(
                            child: Text(
                              proc,
                              style: const TextStyle(color: Colors.white60, fontSize: 11, fontFamily: kMonoFont, fontFamilyFallback: kMonoFontFallback),
                              overflow: TextOverflow.ellipsis,
                            ),
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

  Widget _buildUfwTab() {
    final installed = _ufwData['installed'] as bool;
    final active = _ufwData['active'] as bool;
    final rules = (_ufwData['rules'] as List?)?.cast<Map<String, dynamic>>() ?? [];
    final error = _ufwData['error'] as String?;

    if (error != null) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.lock_outline, size: 40, color: Colors.orangeAccent),
              const SizedBox(height: 10),
              Text(error, textAlign: TextAlign.center, style: const TextStyle(color: Colors.white70)),
              const SizedBox(height: 12),
              OutlinedButton(onPressed: _refreshAll, child: const Text('Retry')),
            ],
          ),
        ),
      );
    }

    if (!installed) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.shield_outlined, size: 40, color: Colors.white24),
            const SizedBox(height: 10),
            const Text('UFW Firewall is not installed on this host', style: TextStyle(color: Colors.white60)),
            const SizedBox(height: 12),
            ElevatedButton(
              onPressed: () async {
                try {
                  await widget.sshService.execute(asRoot('apt-get update && apt-get install -y ufw'));
                } catch (e) {
                  showActionError(context, 'Install ufw', e);
                }
                _refreshAll();
              },
              style: ElevatedButton.styleFrom(backgroundColor: widget.domainColor),
              child: const Text('Install UFW (apt install ufw)'),
            ),
          ],
        ),
      );
    }

    return Column(
      children: [
        // Status header & Add rule bar
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
          decoration: const BoxDecoration(
            color: Color(0xFF151620),
            border: Border(bottom: BorderSide(color: Color(0xFF222434))),
          ),
          child: Row(
            children: [
              Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: active ? const Color(0xFF10B981) : Colors.orangeAccent,
                ),
              ),
              const SizedBox(width: 8),
              Text(
                'Status: ${active ? "ACTIVE" : "INACTIVE"}',
                style: TextStyle(
                  color: active ? const Color(0xFF10B981) : Colors.orangeAccent,
                  fontWeight: FontWeight.bold,
                  fontSize: 12,
                ),
              ),
              const Spacer(),
              // Quick rule input
              SizedBox(
                width: 100,
                height: 30,
                child: TextField(
                  controller: _portController,
                  style: const TextStyle(color: Colors.white, fontSize: 12),
                  decoration: const InputDecoration(
                    hintText: 'Port (80, 443)',
                    hintStyle: TextStyle(color: Colors.white24, fontSize: 11),
                    filled: true,
                    fillColor: Color(0xFF0F1018),
                    border: OutlineInputBorder(borderRadius: BorderRadius.all(Radius.circular(4))),
                    contentPadding: EdgeInsets.symmetric(horizontal: 8),
                  ),
                ),
              ),
              const SizedBox(width: 6),
              DropdownButton<String>(
                value: _ruleAction,
                dropdownColor: const Color(0xFF1E202E),
                underline: const SizedBox(),
                items: const [
                  DropdownMenuItem(value: 'allow', child: Text('ALLOW', style: TextStyle(color: Colors.greenAccent, fontSize: 12))),
                  DropdownMenuItem(value: 'deny', child: Text('DENY', style: TextStyle(color: Colors.redAccent, fontSize: 12))),
                ],
                onChanged: (v) => setState(() => _ruleAction = v ?? 'allow'),
              ),
              const SizedBox(width: 6),
              ElevatedButton(
                onPressed: _addRule,
                style: ElevatedButton.styleFrom(
                  backgroundColor: widget.domainColor,
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 0),
                  minimumSize: const Size(60, 30),
                ),
                child: const Text('Add Rule', style: TextStyle(fontSize: 11)),
              ),
            ],
          ),
        ),

        // Rules table header
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
          color: const Color(0xFF12131C),
          child: const Row(
            children: [
              SizedBox(width: 40, child: Text('#', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              Expanded(flex: 3, child: Text('TO (DESTINATION / PORT)', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              Expanded(flex: 2, child: Text('ACTION', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              Expanded(flex: 3, child: Text('FROM (SOURCE)', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              SizedBox(width: 40),
            ],
          ),
        ),

        // Rules list
        Expanded(
          child: rules.isEmpty
              ? const Center(child: Text('No rules found in UFW', style: TextStyle(color: Colors.white38)))
              : ListView.builder(
                  itemCount: rules.length,
                  itemBuilder: (context, index) {
                    final r = rules[index];
                    final num = r['num'] as int;
                    final to = r['to'] as String;
                    final action = r['action'] as String;
                    final from = r['from'] as String;
                    final isAllow = action.toUpperCase() == 'ALLOW';

                    return Container(
                      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
                      decoration: const BoxDecoration(
                        border: Border(bottom: BorderSide(color: Color(0xFF1B1C26))),
                      ),
                      child: Row(
                        children: [
                          SizedBox(
                            width: 40,
                            child: Text('$num', style: const TextStyle(color: Colors.white38, fontSize: 11)),
                          ),
                          Expanded(
                            flex: 3,
                            child: Text(to, style: const TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.w500)),
                          ),
                          Expanded(
                            flex: 2,
                            child: Text(
                              action,
                              style: TextStyle(
                                color: isAllow ? Colors.greenAccent : Colors.redAccent,
                                fontSize: 11,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ),
                          Expanded(
                            flex: 3,
                            child: Text(from, style: const TextStyle(color: Colors.white60, fontSize: 11)),
                          ),
                          IconButton(
                            icon: const Icon(Icons.delete_outline, size: 16, color: Colors.white38),
                            tooltip: 'Delete Rule',
                            onPressed: () => _deleteRule(num),
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

  Widget _buildInterfacesTab() {
    if (_interfacesError != null) return _errorView(_interfacesError!);
    return Column(
      children: [
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
          color: const Color(0xFF13141E),
          child: const Row(
            children: [
              SizedBox(width: 100, child: Text('INTERFACE', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              SizedBox(width: 80, child: Text('TYPE', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
              Expanded(child: Text('IP / CIDR', style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold))),
            ],
          ),
        ),
        Expanded(
          child: _interfaces.isEmpty
              ? const Center(child: Text('No interfaces found', style: TextStyle(color: Colors.white38)))
              : ListView.builder(
                  itemCount: _interfaces.length,
                  itemBuilder: (context, index) {
                    final item = _interfaces[index];
                    final iface = item['interface'] as String;
                    final type = item['type'] as String;
                    final ip = item['ip'] as String;

                    return Container(
                      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                      decoration: const BoxDecoration(
                        border: Border(bottom: BorderSide(color: Color(0xFF1C1D28))),
                      ),
                      child: Row(
                        children: [
                          SizedBox(
                            width: 100,
                            child: Text(
                              iface,
                              style: const TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.w600),
                            ),
                          ),
                          SizedBox(
                            width: 80,
                            child: Text(
                              type.toUpperCase(),
                              style: TextStyle(
                                color: type == 'inet' ? Colors.greenAccent : Colors.cyanAccent,
                                fontSize: 11,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ),
                          Expanded(
                            child: SelectableText(
                              ip,
                              style: const TextStyle(color: Colors.white70, fontSize: 12, fontFamily: kMonoFont, fontFamilyFallback: kMonoFontFallback),
                            ),
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
