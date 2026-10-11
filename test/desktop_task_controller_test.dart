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
  test('transfer progress tracks cumulative bytes without guessing speed', () async {
    final tasks = DesktopTaskController();
    final pending = Completer<void>();
    final result = tasks.runTransfer('Download', (progress) async {
      progress(256, 1024);
      await pending.future;
      progress(1024, 1024);
    });
    expect(tasks.tasks.single.bytesTransferred, 256);
    expect(tasks.tasks.single.progress, 0.25);
    pending.complete();
    final task = await result;
    expect(task.state, DesktopTaskState.completed);
    expect(task.progress, 1.0);
    tasks.dispose();
  });

  test('stale or impossible progress updates are ignored', () async {
    final tasks = DesktopTaskController();
    final task = await tasks.runTransfer('Upload', (progress) async {
      progress(512, 1024);
      progress(128, 1024);
      progress(2048, 1024);
      progress(-1, 1024);
      progress(700, 2048);
    });
    expect(task.state, DesktopTaskState.completed);
    expect(task.bytesTransferred, 512);
    expect(task.totalBytes, 1024);
    expect(task.progress, 0.5);
    tasks.dispose();
  });

  test('cooperative cancellation retains a cancelled transfer task', () async {
    final tasks = DesktopTaskController();
    final token = TransferCancellation();
    final gate = Completer<void>();
    final pending = tasks.runTransfer('Upload', (progress) async {
      progress(10, 100);
      await gate.future;
      token.throwIfRequested();
    }, cancellation: token);
    expect(tasks.runningCount, 1);
    expect(tasks.cancelTransfer(tasks.tasks.single.id), isTrue);
    expect(token.isRequested, isTrue);
    gate.complete();
    final task = await pending;
    expect(task.state, DesktopTaskState.cancelled);
    expect(task.bytesTransferred, 10);
    expect(tasks.cancelTransfer(task.id), isFalse);
    tasks.dispose();
  });

}
