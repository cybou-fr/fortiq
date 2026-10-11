import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/ssh_service.dart';

import 'support.dart';

/// Losing the connection and getting it back, against a real sshd. The loss is
/// real: the session process is killed on the server.
void main() {
  final h = IntegrationHarness();

  Future<void> waitFor(bool Function() condition, String what,
      {Duration timeout = const Duration(seconds: 40)}) async {
    final end = DateTime.now().add(timeout);
    while (!condition()) {
      if (DateTime.now().isAfter(end)) fail('timed out waiting for $what');
      await Future.delayed(const Duration(milliseconds: 100));
    }
  }

  /// Open sessions of fquser on the server: sshd starts one "[priv]" process
  /// per session (the name of the other one varies).
  Future<int> sessions() async => int.parse(
      (await h.server.root(r"pgrep -fc 'sshd-session: fquser \[priv\]' || true")).trim());

  group('after a lost connection', () {
    test('Fortiq reconnects by itself and everything works again', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      final states = <SshConnectionState>[];
      ssh.addListener(() {
        if (states.isEmpty || states.last != ssh.state) states.add(ssh.state);
      });

      await h.server.dropConnections('fquser');
      await waitFor(() => ssh.isReconnecting, 'the loss to be noticed');
      expect(ssh.isSessionActive, isTrue, reason: 'the session is being restored, not gone');
      expect(ssh.isConnected, isFalse);
      expect(ssh.lastDisconnectReason, isNotNull);
      await expectLater(ssh.execute('true'), throwsA(predicate((e) => '$e'.contains('Not connected'))));

      await waitFor(() => ssh.isConnected, 'the reconnect');
      expect(states, [SshConnectionState.reconnecting, SshConnectionState.connected]);
      expect(ssh.lastDisconnectReason, isNull);
      expect(ssh.reconnectAttempt, 0);
      expect((await ssh.execute('id -un')).trim(), 'fquser');
    });

    test('it comes back as often as needed', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      for (var i = 0; i < 3; i++) {
        await h.server.dropConnections('fquser');
        await waitFor(() => ssh.isReconnecting, 'loss #$i');
        await waitFor(() => ssh.isConnected, 'reconnect #$i');
      }
      expect(await ssh.execute('echo ok'), 'ok\n');
      expect(await sessions(), 1, reason: 'no session is left over from the failed ones');
    });

    test('while the server is away it keeps trying, with growing pauses', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      await h.server.root('systemctl stop ssh.socket ssh 2>/dev/null; systemctl stop ssh; exit 0');
      await h.server.dropConnections('fquser');
      await waitFor(() => ssh.isReconnecting, 'the loss');

      final pauses = <int>[];
      var lastAttempt = -1;
      final end = DateTime.now().add(const Duration(seconds: 25));
      while (pauses.length < 3 && DateTime.now().isBefore(end)) {
        final next = ssh.nextRetryAt;
        if (next != null && ssh.reconnectAttempt != lastAttempt) {
          lastAttempt = ssh.reconnectAttempt;
          pauses.add(next.difference(DateTime.now()).inMilliseconds);
        }
        await Future.delayed(const Duration(milliseconds: 50));
      }
      expect(pauses.length, greaterThanOrEqualTo(3));
      // 1 s, 2 s, 4 s (the first one was already scheduled when we looked)
      expect(pauses[1], inInclusiveRange(1000, 2100));
      expect(pauses[2], inInclusiveRange(2000, 4100));
      expect(ssh.isReconnecting, isTrue);

      await h.server.root('systemctl start ssh');
      ssh.retryNow();
      await waitFor(() => ssh.isConnected, 'the reconnect after the server is back');
    });

    test('"Retry now" skips the wait and never creates two sessions', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      await h.server.root('systemctl stop ssh');
      await h.server.dropConnections('fquser');
      await waitFor(() => ssh.isReconnecting, 'the loss');
      await waitFor(() => ssh.reconnectAttempt >= 2, 'a few failed attempts');
      await h.server.root('systemctl start ssh; sleep 1');

      // hammer the button, and let the timer fire as well
      for (var i = 0; i < 5; i++) {
        ssh.retryNow();
      }
      await waitFor(() => ssh.isConnected, 'the reconnect');
      await Future.delayed(const Duration(seconds: 4)); // a late timer must do nothing
      final procs = await h.server.root("ps -eo pid,args | grep sshd-session | grep -v grep || true");
      expect(ssh.isConnected, isTrue,
          reason: 'state=${ssh.state} reason=${ssh.lastDisconnectReason} $procs');
      expect(await sessions(), 1, reason: procs);
    });

    test('Disconnect during the wait cancels every further attempt', () async {
      final ssh = await h.server.connect();
      await h.server.root('systemctl stop ssh');
      await h.server.dropConnections('fquser');
      await waitFor(() => ssh.isReconnecting, 'the loss');

      ssh.disconnect();
      expect(ssh.state, SshConnectionState.disconnected);
      expect(ssh.isSessionActive, isFalse);
      ssh.retryNow(); // nothing to retry any more

      await h.server.root('systemctl start ssh; sleep 1');
      await Future.delayed(const Duration(seconds: 5));
      expect(ssh.state, SshConnectionState.disconnected);
      expect(await sessions(), 0, reason: 'no attempt connected behind the user\'s back');
    });

    test('Disconnect while an attempt is connecting does not leave a late session', () async {
      final ssh = await h.server.connect();
      await h.server.dropConnections('fquser');
      await waitFor(() => ssh.isReconnecting, 'the loss');
      // the attempt starts after ~1 s: disconnect right while it is in flight
      await waitFor(() => ssh.reconnectAttempt >= 1, 'an attempt to start');
      ssh.disconnect();
      await Future.delayed(const Duration(seconds: 4));
      expect(ssh.state, SshConnectionState.disconnected);
      expect(await sessions(), 0);
    });

    test('lost access stops the retries and says authentication failed', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      await h.server.root('> /home/fquser/.ssh/authorized_keys');
      await h.server.dropConnections('fquser');

      await waitFor(() => ssh.state == SshConnectionState.disconnected, 'the retries to stop');
      expect(ssh.lastDisconnectReason, contains('authentication failed'));
      expect(ssh.isSessionActive, isFalse);
    });

    test('a changed server key stops the retries and says so', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      await h.server.root(
        'rm -f /etc/ssh/ssh_host_*; ssh-keygen -A >/dev/null; systemctl restart ssh; sleep 2',
      );
      await h.server.dropConnections('fquser');

      await waitFor(() => ssh.state == SshConnectionState.disconnected, 'the retries to stop');
      expect(ssh.lastDisconnectReason, contains('server key changed'));
    });

    test('a server that stops answering (no close) is noticed by the ping', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      // Freeze the session process: the TCP connection stays open, nothing answers.
      await h.server.root("pkill -STOP -f 'sshd-session: fquser' || true");
      addTearDown(() => h.server.root("pkill -CONT -f 'sshd-session: fquser'; pkill -KILL -f 'sshd-session: fquser'; exit 0"));

      await waitFor(() => ssh.isReconnecting, 'the ping to give up',
          timeout: const Duration(seconds: 60));
      expect(ssh.lastDisconnectReason, contains('not responding'));
      await waitFor(() => ssh.isConnected, 'the new session', timeout: const Duration(seconds: 30));
    }, timeout: const Timeout(Duration(seconds: 150)));
  });

  group('the browser tunnel across a lost connection', () {
    test('the tunnel is back on the same port after the reconnect', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      final port = await ssh.startSocksTunnel();
      expect(ssh.isTunnelActive, isTrue);

      await h.server.dropConnections('fquser');
      await waitFor(() => ssh.isReconnecting, 'the loss');
      expect(ssh.isTunnelActive, isFalse, reason: 'with the session gone there is no tunnel');
      await waitFor(() => ssh.isConnected, 'the reconnect');
      await waitFor(() => ssh.isTunnelActive, 'the tunnel to return');

      expect(await SshClientService.browserProxyPort(), port,
          reason: 'WebView was started with this port and cannot be told another');
    });

    test('an explicit stop is not undone by a reconnect', () async {
      final ssh = await h.server.connect();
      addTearDown(ssh.disconnect);
      await ssh.startSocksTunnel();
      ssh.stopSocksTunnel();
      await h.server.dropConnections('fquser');
      await waitFor(() => ssh.isReconnecting, 'the loss');
      await waitFor(() => ssh.isConnected, 'the reconnect');
      await Future.delayed(const Duration(milliseconds: 500));
      expect(ssh.isTunnelActive, isFalse);
    });
  });
}
