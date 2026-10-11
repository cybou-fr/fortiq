import 'dart:async';
import 'package:flutter/material.dart';
import '../services/ssh_service.dart';

/// A snapshot belongs to one server and one completed SSH request.
class RemoteSnapshotController<T> extends ChangeNotifier {
  final SshClientService ssh;
  final Future<T> Function() load;
  late final String source;
  String get _source => '${ssh.currentUsername}@${ssh.currentHost}:${ssh.currentPort}';
  bool get available => ssh.isConnected && source == _source;
  T? data;
  DateTime? updatedAt;
  String? error;
  bool loading = false, live = false, _disposed = false;
  int _generation = 0;
  Object? _client;
  bool _connected = false;
  Timer? _timer;

  RemoteSnapshotController(this.ssh, {required this.load}) {
    source = _source;
    _client = ssh.rawClient;
    _connected = available;
    ssh.addListener(_connectionChanged);
  }

  void _connectionChanged() {
    if (_client == ssh.rawClient && _connected == available) return;
    _client = ssh.rawClient;
    _connected = available;
    _generation++;
    loading = false;
    notifyListeners();
    if (available) unawaited(refresh());
  }

  void setLive(bool value) {
    live = value;
    _timer?.cancel();
    if (value) {
      _timer = Timer.periodic(const Duration(seconds: 5), (_) {
        if (available) unawaited(refresh());
      });
    }
    notifyListeners();
  }

  void invalidate({bool clear = false}) {
    _generation++;
    loading = false;
    error = null;
    if (clear) { data = null; updatedAt = null; }
    notifyListeners();
  }

  Future<void> refresh() async {
    if (_disposed || loading || !available) return;
    final generation = ++_generation;
    final client = ssh.rawClient;
    loading = true;
    error = null;
    notifyListeners();
    try {
      final result = await load();
      if (!_disposed && generation == _generation && available && client == ssh.rawClient) {
        data = result;
        updatedAt = DateTime.now();
      }
    } catch (failure) {
      if (!_disposed && generation == _generation) error = failure.toString();
    } finally {
      if (!_disposed && generation == _generation) {
        loading = false;
        notifyListeners();
      }
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _generation++;
    _timer?.cancel();
    ssh.removeListener(_connectionChanged);
    super.dispose();
  }
}

class RemoteSnapshotStatus extends StatelessWidget {
  final RemoteSnapshotController<dynamic> snapshot;
  const RemoteSnapshotStatus({super.key, required this.snapshot});
  @override
  Widget build(BuildContext context) {
    final time = snapshot.updatedAt;
    final label = time == null ? 'Not loaded' :
        'Updated ${time.hour.toString().padLeft(2, '0')}:${time.minute.toString().padLeft(2, '0')}:${time.second.toString().padLeft(2, '0')}';
    final notice = !snapshot.available ? 'SSH unavailable. Showing the last loaded data.' : snapshot.error;
    return Column(mainAxisSize: MainAxisSize.min, children: [
      if (notice != null) Container(width: double.infinity, color: const Color(0xFF432E25),
        padding: const EdgeInsets.all(8), child: SelectableText(notice,
          style: const TextStyle(fontSize: 11, color: Colors.amberAccent))),
      if (snapshot.loading) const LinearProgressIndicator(minHeight: 2),
      Padding(padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4), child: Row(children: [
        Expanded(child: Text(label, style: const TextStyle(fontSize: 10, color: Colors.white54))),
        TextButton.icon(onPressed: () => snapshot.setLive(!snapshot.live),
          icon: Icon(snapshot.live ? Icons.pause : Icons.sync, size: 14),
          label: Text(snapshot.live ? 'Pause updates' : 'Live · 5s', style: const TextStyle(fontSize: 11))),
      ])),
    ]);
  }
}
