import 'dart:io';
import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';
import 'services/ssh_service.dart';
import 'services/profile_service.dart';
import 'models/server_profile.dart';
import 'widgets/qubes_window_frame.dart';
import 'widgets/desktop_workspace.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  if (Platform.isWindows || Platform.isLinux || Platform.isMacOS) {
    await windowManager.ensureInitialized();

    const windowOptions = WindowOptions(
      size: Size(1100, 700),
      minimumSize: Size(800, 500),
      center: true,
      backgroundColor: Colors.transparent,
      skipTaskbar: false,
      titleBarStyle: TitleBarStyle.hidden,
    );

    await windowManager.waitUntilReadyToShow(windowOptions);
    await windowManager.show();
    await windowManager.focus();
  }

  runApp(const FortiqApp());
}

class FortiqApp extends StatelessWidget {
  const FortiqApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Fortiq',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: const Color(0xFF13141B),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF8B5CF6),
        ),
      ),
      home: const FortiqHomeScreen(),
    );
  }
}

class FortiqHomeScreen extends StatefulWidget {
  const FortiqHomeScreen({super.key});

  @override
  State<FortiqHomeScreen> createState() => _FortiqHomeScreenState();
}

class _FortiqHomeScreenState extends State<FortiqHomeScreen> {
  final SshClientService _sshService = SshClientService();
  bool _connecting = false;
  String? _connectError;

  // Connection settings
  final _hostController = TextEditingController(text: 'vps-d0669a91.vps.ovh.net');
  final _portController = TextEditingController(text: '22');
  final _userController = TextEditingController(text: 'debian');
  final _passwordController = TextEditingController();
  final _labelController = TextEditingController(text: 'DEBIAN-VPS');
  final _keyPathController = TextEditingController();
  bool _useKeyAuth = true;

  // Selected Qubes Domain Color
  Color _domainColor = const Color(0xFF8B5CF6); // Purple

  final List<Color> _qubesPalette = const [
    Color(0xFF8B5CF6), // Purple
    Color(0xFF10B981), // Green (Trusted)
    Color(0xFFEF4444), // Red (Untrusted / External VPS)
    Color(0xFF3B82F6), // Blue (Dev / WSL)
    Color(0xFFF59E0B), // Orange / Amber
  ];

  List<ServerProfile> _profiles = [];
  ServerProfile? _selectedProfile;

  @override
  void initState() {
    super.initState();
    _loadSavedProfiles();
  }

  Future<void> _loadSavedProfiles() async {
    final list = await ProfileService.loadProfiles();
    setState(() {
      _profiles = list;
    });

    final userProfile = Platform.environment['USERPROFILE'] ?? '';
    String? defaultKey;
    if (userProfile.isNotEmpty) {
      final edKey = File('$userProfile\\.ssh\\id_ed25519');
      final rsaKey = File('$userProfile\\.ssh\\id_rsa');
      if (edKey.existsSync()) {
        defaultKey = edKey.path;
      } else if (rsaKey.existsSync()) {
        defaultKey = rsaKey.path;
      }
    }

    if (_profiles.isNotEmpty) {
      _selectProfile(_profiles.first, fallbackKey: defaultKey);
    }
  }

  void _selectProfile(ServerProfile p, {String? fallbackKey}) {
    setState(() {
      _selectedProfile = p;
      _labelController.text = p.name;
      _hostController.text = p.host;
      _portController.text = p.port.toString();
      _userController.text = p.username;
      _useKeyAuth = p.useKeyAuth;
      _domainColor = p.domainColor;
      if (p.keyPath != null && p.keyPath!.isNotEmpty) {
        _keyPathController.text = p.keyPath!;
      } else if (fallbackKey != null) {
        _keyPathController.text = fallbackKey;
      }
    });
  }

  Future<void> _saveCurrentProfile() async {
    final current = ServerProfile(
      id: _selectedProfile?.id ?? DateTime.now().millisecondsSinceEpoch.toString(),
      name: _labelController.text.trim().isEmpty ? 'MY-SERVER' : _labelController.text.trim(),
      host: _hostController.text.trim(),
      port: int.tryParse(_portController.text.trim()) ?? 22,
      username: _userController.text.trim(),
      useKeyAuth: _useKeyAuth,
      keyPath: _keyPathController.text.trim(),
      domainColor: _domainColor,
    );

    final idx = _profiles.indexWhere((p) => p.id == current.id);
    setState(() {
      if (idx != -1) {
        _profiles[idx] = current;
      } else {
        _profiles.add(current);
      }
      _selectedProfile = current;
    });
    await ProfileService.saveProfiles(_profiles);
  }

  Future<void> _handleConnect() async {
    setState(() {
      _connecting = true;
      _connectError = null;
    });

    try {
      String? privateKeyContent;
      if (_useKeyAuth && _keyPathController.text.trim().isNotEmpty) {
        final keyFile = File(_keyPathController.text.trim());
        if (keyFile.existsSync()) {
          privateKeyContent = await keyFile.readAsString();
        } else {
          throw Exception('Key file not found: ${_keyPathController.text}');
        }
      }

      await _sshService.connect(
        host: _hostController.text.trim(),
        port: int.tryParse(_portController.text.trim()) ?? 22,
        username: _userController.text.trim(),
        password: _passwordController.text.isNotEmpty ? _passwordController.text : null,
        privateKey: privateKeyContent,
      );
      _sshService.currentKeyPath = _useKeyAuth ? _keyPathController.text.trim() : null;
      setState(() {
        _connecting = false;
      });
    } catch (e) {
      setState(() {
        _connecting = false;
        _connectError = e.toString();
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return QubesWindowFrame(
      title: _sshService.isConnected
          ? 'Linux Desktop — ${_hostController.text}'
          : 'Fortiq Desktop — Connect to Linux',
      hostLabel: _labelController.text.isEmpty ? 'LINUX' : _labelController.text,
      domainColor: _domainColor,
      actions: _sshService.isConnected
          ? [
              IconButton(
                icon: const Icon(Icons.power_settings_new, size: 16, color: Colors.redAccent),
                tooltip: 'Disconnect',
                onPressed: () {
                  _sshService.disconnect();
                  setState(() {});
                },
              ),
            ]
          : null,
      child: _sshService.isConnected
          ? HeadlessDesktopWorkspace(
              sshService: _sshService,
              domainColor: _domainColor,
              hostLabel: _labelController.text.isEmpty ? 'LINUX' : _labelController.text,
            )
          : _buildConnectionForm(),
    );
  }

  Widget _buildConnectionForm() {
    return Center(
      child: SingleChildScrollView(
        child: Container(
          width: 480,
          padding: const EdgeInsets.all(28),
          decoration: BoxDecoration(
            color: const Color(0xFF1B1C26),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: _domainColor.withOpacity(0.4), width: 1.5),
            boxShadow: [
              BoxShadow(
                color: _domainColor.withOpacity(0.1),
                blurRadius: 20,
                spreadRadius: 2,
              ),
            ],
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: [
              Row(
                children: [
                  Icon(Icons.terminal, color: _domainColor, size: 28),
                  const SizedBox(width: 10),
                  const Text(
                    'Fortiq — Headless Linux Desktop',
                    style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold, color: Colors.white),
                  ),
                ],
              ),
              const SizedBox(height: 6),
              const Text(
                'Connect to remote Debian/Ubuntu or local WSL via SSH. All GUI apps run locally on Windows using native Linux CLI under the hood.',
                style: TextStyle(fontSize: 12, color: Colors.white60),
              ),
              const SizedBox(height: 20),

              // Saved Profiles Row
              Row(
                children: [
                  const Text('Saved Servers:', style: TextStyle(color: Colors.white70, fontSize: 12, fontWeight: FontWeight.bold)),
                  const Spacer(),
                  TextButton.icon(
                    onPressed: () {
                      _selectProfile(ServerProfile(
                        id: DateTime.now().millisecondsSinceEpoch.toString(),
                        name: 'NEW-SERVER',
                        host: '',
                        username: 'root',
                        domainColor: const Color(0xFF3B82F6),
                      ));
                    },
                    icon: const Icon(Icons.add, size: 14),
                    label: const Text('Add New', style: TextStyle(fontSize: 11)),
                    style: TextButton.styleFrom(padding: EdgeInsets.zero),
                  ),
                ],
              ),
              const SizedBox(height: 6),
              SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                child: Row(
                  children: _profiles.map((p) {
                    final isSelected = _selectedProfile?.id == p.id;
                    return Padding(
                      padding: const EdgeInsets.only(right: 8.0),
                      child: ChoiceChip(
                        avatar: Container(
                          width: 8,
                          height: 8,
                          decoration: BoxDecoration(shape: BoxShape.circle, color: p.domainColor),
                        ),
                        label: Text(p.name, style: const TextStyle(fontSize: 12)),
                        selected: isSelected,
                        selectedColor: p.domainColor.withValues(alpha: 0.25),
                        side: BorderSide(
                          color: isSelected ? p.domainColor : const Color(0xFF2C2E40),
                          width: isSelected ? 1.5 : 1.0,
                        ),
                        onSelected: (_) => _selectProfile(p),
                      ),
                    );
                  }).toList(),
                ),
              ),
              const SizedBox(height: 16),

              // Domain Label and Color picker (Qubes OS Style)
              const Text('Qubes Domain Tag & Window Accent Color:', style: TextStyle(color: Colors.white70, fontSize: 12)),
              const SizedBox(height: 8),
              Row(
                children: [
                  Expanded(
                    child: TextField(
                      controller: _labelController,
                      style: const TextStyle(color: Colors.white, fontSize: 13),
                      decoration: InputDecoration(
                        filled: true,
                        fillColor: const Color(0xFF12131A),
                        isDense: true,
                        hintText: 'e.g. DEBIAN-PROD, WSL-DEV',
                        hintStyle: const TextStyle(color: Colors.white38),
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                      ),
                      onChanged: (_) => setState(() {}),
                    ),
                  ),
                  const SizedBox(width: 12),
                  ..._qubesPalette.map((color) => GestureDetector(
                        onTap: () => setState(() => _domainColor = color),
                        child: Container(
                          margin: const EdgeInsets.only(left: 6),
                          width: 24,
                          height: 24,
                          decoration: BoxDecoration(
                            color: color,
                            shape: BoxShape.circle,
                            border: Border.all(
                              color: _domainColor == color ? Colors.white : Colors.transparent,
                              width: 2,
                            ),
                          ),
                        ),
                      )),
                ],
              ),
              const SizedBox(height: 16),

              // Host & Port
              Row(
                children: [
                  Expanded(
                    flex: 3,
                    child: TextField(
                      controller: _hostController,
                      style: const TextStyle(color: Colors.white, fontSize: 13),
                      decoration: InputDecoration(
                        labelText: 'Host / IP',
                        labelStyle: const TextStyle(color: Colors.white60),
                        filled: true,
                        fillColor: const Color(0xFF12131A),
                        isDense: true,
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                      ),
                    ),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    flex: 1,
                    child: TextField(
                      controller: _portController,
                      style: const TextStyle(color: Colors.white, fontSize: 13),
                      decoration: InputDecoration(
                        labelText: 'Port',
                        labelStyle: const TextStyle(color: Colors.white60),
                        filled: true,
                        fillColor: const Color(0xFF12131A),
                        isDense: true,
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),

              // Username & Password
              TextField(
                controller: _userController,
                style: const TextStyle(color: Colors.white, fontSize: 13),
                decoration: InputDecoration(
                  labelText: 'Username',
                  labelStyle: const TextStyle(color: Colors.white60),
                  filled: true,
                  fillColor: const Color(0xFF12131A),
                  isDense: true,
                  border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                ),
              ),
              // Auth Method Selector
              Row(
                children: [
                  ChoiceChip(
                    label: const Text('SSH Key', style: TextStyle(fontSize: 12)),
                    selected: _useKeyAuth,
                    selectedColor: _domainColor.withValues(alpha: 0.3),
                    onSelected: (val) => setState(() => _useKeyAuth = true),
                  ),
                  const SizedBox(width: 8),
                  ChoiceChip(
                    label: const Text('Password', style: TextStyle(fontSize: 12)),
                    selected: !_useKeyAuth,
                    selectedColor: _domainColor.withValues(alpha: 0.3),
                    onSelected: (val) => setState(() => _useKeyAuth = false),
                  ),
                ],
              ),
              const SizedBox(height: 12),

              if (_useKeyAuth)
                TextField(
                  controller: _keyPathController,
                  style: const TextStyle(color: Colors.white, fontSize: 13),
                  decoration: InputDecoration(
                    labelText: 'Private Key Path',
                    labelStyle: const TextStyle(color: Colors.white60),
                    hintText: 'C:\\Users\\...\\.ssh\\id_ed25519',
                    prefixIcon: const Icon(Icons.key, size: 16, color: Colors.white60),
                    filled: true,
                    fillColor: const Color(0xFF12131A),
                    isDense: true,
                    border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                  ),
                )
              else
                TextField(
                  controller: _passwordController,
                  obscureText: true,
                  style: const TextStyle(color: Colors.white, fontSize: 13),
                  decoration: InputDecoration(
                    labelText: 'Password',
                    labelStyle: const TextStyle(color: Colors.white60),
                    prefixIcon: const Icon(Icons.lock, size: 16, color: Colors.white60),
                    filled: true,
                    fillColor: const Color(0xFF12131A),
                    isDense: true,
                    border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                  ),
                ),
              const SizedBox(height: 16),

              if (_connectError != null) ...[
                Text(
                  'Connection failed: $_connectError',
                  style: const TextStyle(color: Colors.redAccent, fontSize: 12),
                ),
                const SizedBox(height: 12),
              ],

              Row(
                children: [
                  OutlinedButton.icon(
                    onPressed: _saveCurrentProfile,
                    icon: const Icon(Icons.bookmark_border, size: 16),
                    label: const Text('Save Profile'),
                    style: OutlinedButton.styleFrom(
                      foregroundColor: Colors.white70,
                      side: const BorderSide(color: Color(0xFF2C2E40)),
                      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
                    ),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: SizedBox(
                      height: 42,
                      child: ElevatedButton.icon(
                        onPressed: _connecting ? null : _handleConnect,
                        icon: _connecting
                            ? const SizedBox(
                                width: 16,
                                height: 16,
                                child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2),
                              )
                            : const Icon(Icons.login, size: 18),
                        label: Text(_connecting ? 'Connecting...' : 'Connect (SSH)'),
                        style: ElevatedButton.styleFrom(
                          backgroundColor: _domainColor,
                          foregroundColor: Colors.white,
                          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}
