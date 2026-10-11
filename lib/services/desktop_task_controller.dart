import 'package:flutter/foundation.dart';

enum DesktopTaskState { running, completed, failed, unknown }

class DesktopTask {
  final int id;
  final String label;
  final DateTime startedAt;
  DesktopTaskState state;
  String? message;
  int bytesTransferred = 0;
  int? totalBytes;
  double? get progress => totalBytes != null && totalBytes! > 0
      ? (bytesTransferred / totalBytes!).clamp(0.0, 1.0)
      : null;

  DesktopTask(this.id, this.label)
      : startedAt = DateTime.now(),
        state = DesktopTaskState.running;
}

/// In-memory task history for one desktop session.
/// A lost SSH acknowledgement is NOT proof that a remote operation failed.
/// Do not auto-retry unknown writes.
class DesktopTaskController extends ChangeNotifier {
  final List<DesktopTask> _tasks = [];
  int _nextId = 0;
  bool _disposed = false;

  List<DesktopTask> get tasks => List.unmodifiable(_tasks);
  int get runningCount =>
      _tasks.where((task) => task.state == DesktopTaskState.running).length;

  Future<DesktopTask> run(
    String label,
    Future<void> Function() operation, {
    bool Function()? isSessionCurrent,
  }) async {
    final task = DesktopTask(++_nextId, label);
    _tasks.insert(0, task);
    _notify();
    try {
      await operation();
      if (isSessionCurrent != null && !isSessionCurrent()) {
        task.state = DesktopTaskState.unknown;
        task.message = 'SSH session changed. Verify the remote result manually.';
      } else {
        task.state = DesktopTaskState.completed;
      }
    } catch (error) {
      task.state = (isSessionCurrent != null && !isSessionCurrent())
          ? DesktopTaskState.unknown
          : DesktopTaskState.failed;
      task.message = task.state == DesktopTaskState.unknown
          ? 'SSH session changed. Verify the remote result manually.'
          : error.toString();
    }
    _notify();
    return task;
  }

  Future<DesktopTask> runTransfer(
    String label,
    Future<void> Function(void Function(int bytes, int? total) progress) operation, {
    bool Function()? isSessionCurrent,
  }) async {
    final task = DesktopTask(++_nextId, label);
    _tasks.insert(0, task);
    _notify();
    try {
      await operation((bytes, total) {
        if (task.state != DesktopTaskState.running) return;
        // Reject stale/out-of-order progress and invalid totals.
        if (bytes < 0 || bytes < task.bytesTransferred ||
            (total != null && (total < 0 || bytes > total)) ||
            (task.totalBytes != null && total != task.totalBytes)) {
          return;
        }
        task.bytesTransferred = bytes;
        task.totalBytes = total;
        _notify();
      });
      task.state = isSessionCurrent != null && !isSessionCurrent()
          ? DesktopTaskState.unknown
          : DesktopTaskState.completed;
      if (task.state == DesktopTaskState.unknown) {
        task.message = 'SSH session changed. Verify the transfer manually.';
      }
    } catch (error) {
      task.state = isSessionCurrent != null && !isSessionCurrent()
          ? DesktopTaskState.unknown
          : DesktopTaskState.failed;
      task.message = task.state == DesktopTaskState.unknown
          ? 'SSH session changed. Verify the transfer manually.'
          : error.toString();
    }
    _notify();
    return task;
  }

  void clearFinished() {
    _tasks.removeWhere((task) => task.state != DesktopTaskState.running);
    _notify();
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    super.dispose();
  }
}
