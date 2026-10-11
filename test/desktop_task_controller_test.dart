import 'dart:async';

import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/desktop_task_controller.dart';

void main() {
  test('completed task is retained until dismissed', () async {
    final tasks = DesktopTaskController();
    final task = await tasks.run('Copy', () async {});
    expect(task.state, DesktopTaskState.completed);
    expect(tasks.runningCount, 0);
    expect(tasks.tasks.length, 1);
    tasks.clearFinished();
    expect(tasks.tasks, isEmpty);
    tasks.dispose();
  });

  test('running task remains visible and is not cleared', () async {
    final tasks = DesktopTaskController();
    final pending = Completer<void>();
    final future = tasks.run('Copy', () => pending.future);
    expect(tasks.runningCount, 1);
    tasks.clearFinished();
    expect(tasks.tasks.length, 1);
    pending.complete();
    await future;
    expect(tasks.runningCount, 0);
    tasks.dispose();
  });

  test('SSH session change gives unknown rather than success', () async {
    final tasks = DesktopTaskController();
    var current = true;
    final task = await tasks.run('Copy', () async {
      current = false;
    }, isSessionCurrent: () => current);
    expect(task.state, DesktopTaskState.unknown);
    expect(task.message, contains('Verify'));
    tasks.dispose();
  });

  test('operation error is recorded without losing task', () async {
    final tasks = DesktopTaskController();
    final task = await tasks.run('Copy', () async {
      throw StateError('permission denied');
    });
    expect(task.state, DesktopTaskState.failed);
    expect(task.message, contains('permission denied'));
    expect(tasks.tasks.single, same(task));
    tasks.dispose();
  });
}
