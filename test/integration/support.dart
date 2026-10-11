import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/ssh_service.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// A real Debian server for the integration tests: the throwaway WSL distro
/// `fortiq-test` running sshd on 127.0.0.1:2222 (see tool/test_server/README.md).
///
/// Test files run in parallel, but they change the same server (kill sessions,
/// swap keys, install packages). A lock file makes them take turns. When the
/// server is not there, the tests skip themselves and say how to create it.
class IntegrationServer {
  static const host = '127.0.0.1';
  static const port = 2222;
  static const distro = 'fortiq-test';

  static final _dir = Directory('test/integration/.keys');
  static final _keyFile = File('${_dir.path}/id_test');
  static final _lockFile = File('${_dir.path}/server.lock');

  final String keyPem;
  final Process _keepAlive;
  final RandomAccessFile _lock;

  IntegrationServer._(this.keyPem, this._keepAlive, this._lock);

  /// Why there is no server (null when there is one). Set by [acquire].
  static String? unavailableReason;

  /// Takes the server for this test file, or returns null (and sets
  /// [unavailableReason]) when it cannot be used.
  static Future<IntegrationServer?> acquire() async {
    unavailableReason = null;
    if (!Platform.isWindows) {
      unavailableReason = 'the test server is a WSL distro (Windows only)';
      return null;
    }
    if (!_keyFile.existsSync()) {
      unavailableReason = 'no test key; run tool/test_server/setup.ps1';
      return null;
    }
    final listed = await _wsl(['-l', '-q']);
    if (!listed.replaceAll('\x00', '').split(RegExp(r'\s+')).contains(distro)) {
      unavailableReason = 'WSL distro "$distro" is missing; run tool/test_server/setup.ps1';
      return null;
    }

    final lock = await _lockFile.open(mode: FileMode.write);
    await lock.lock(FileLock.blockingExclusive); // waits for other test files

    final keepAlive = await Process.start(
      'wsl.exe',
      ['-d', distro, '-u', 'root', '--', 'sleep', 'infinity'],
      environment: {'WSL_UTF8': '1'},
    );
    final server = IntegrationServer._(await _keyFile.readAsString(), keepAlive, lock);
    try {
      await server._waitForSsh();
      await server.restoreBaseline();
    } catch (e) {
      unavailableReason = 'the test server does not answer: $e';
      await server.release();
      return null;
    }
    return server;
  }

  static Future<String> _wsl(List<String> args) async {
    final r = await Process.run('wsl.exe', args, environment: {'WSL_UTF8': '1'});
    return '${r.stdout}';
  }

  Future<void> release() async {
    _keepAlive.kill();
    await _lock.unlock();
    await _lock.close();
  }

  Future<void> _waitForSsh() async {
    final end = DateTime.now().add(const Duration(seconds: 60));
    Object? last;
    while (DateTime.now().isBefore(end)) {
      try {
        final s = await Socket.connect(host, port, timeout: const Duration(seconds: 3));
        final banner = await s.first.timeout(const Duration(seconds: 3));
        s.destroy();
        if (utf8.decode(banner, allowMalformed: true).startsWith('SSH-')) return;
      } catch (e) {
        last = e;
      }
      await Future.delayed(const Duration(seconds: 1));
    }
    throw 'no SSH banner on $host:$port ($last)';
  }

  /// Runs [script] as root inside the server (not over SSH) and returns stdout.
  Future<String> root(String script, {bool allowFailure = false}) async {
    final p = await Process.start(
      'wsl.exe',
      ['-d', distro, '-u', 'root', '--', 'sh', '-s'],
      environment: {'WSL_UTF8': '1'},
    );
    p.stdin.write(script);
    await p.stdin.close();
    final out = p.stdout.transform(utf8.decoder).join();
    final err = p.stderr.transform(utf8.decoder).join();
    final code = await p.exitCode;
    final stdout = await out;
    final stderr = await err;
    if (code != 0 && !allowFailure) {
      throw 'server command failed ($code): $stderr\n$script';
    }
    return stdout;
  }

  /// Removes packages so that a test can start from "this is not installed".
  Future<void> purge(List<String> packages) => root(
        'export DEBIAN_FRONTEND=noninteractive; '
        'apt-get purge -y -qq ${packages.join(' ')} >/dev/null 2>&1; '
        'apt-get autoremove -y -qq >/dev/null 2>&1; exit 0',
      );

  Future<void> install(List<String> packages) => root(
        'export DEBIAN_FRONTEND=noninteractive; '
        'apt-get install -y -qq ${packages.join(' ')} >/dev/null 2>&1',
      );

  /// The public half of the test key, as the server knows it.
  Future<String> get publicKey async => (await root('cat /root/fortiq-test.pub')).trim();

  /// Puts the server back the way it was provisioned: keys, SSH config,
  /// firewall, leftovers of earlier tests.
  Future<void> restoreBaseline() => root(r'''
set +e
rm -f /etc/ssh/sshd_config.d/00-fortiq-hardening.conf
rm -f /etc/systemd/system/fortiq-test.service
systemctl daemon-reload
for u in root fquser fqplain; do
  home=$(getent passwd $u | cut -d: -f6)
  mkdir -p $home/.ssh
  cp /root/fortiq-test.pub $home/.ssh/authorized_keys
  chown -R $u:$u $home/.ssh 2>/dev/null
  chmod 700 $home/.ssh; chmod 600 $home/.ssh/authorized_keys
  rm -rf $home/work
done
if [ -x /usr/sbin/ufw ]; then /usr/sbin/ufw --force disable >/dev/null 2>&1; fi
systemctl reload ssh 2>/dev/null || systemctl restart ssh
systemctl is-active --quiet ssh || systemctl start ssh
rm -f /var/mail/fquser /var/mail/root
exit 0
''');

  /// A service that is connected as [user] with the test key.
  Future<SshClientService> connect({
    String user = 'fquser',
    Future<bool> Function(HostKeyInfo info)? onHostKey,
    String? privateKey,
  }) async {
    // WSL forwards 127.0.0.1:2222 into the VM through a relay that now and then
    // resets a connection right after a heavy transfer. That is a problem of
    // the test machine, not of Fortiq, so network-level failures are retried;
    // a refused login or key never is.
    for (var attempt = 1;; attempt++) {
      final ssh = SshClientService();
      try {
        await ssh.connect(
          host: host,
          port: port,
          username: user,
          privateKey: privateKey ?? keyPem,
          onHostKey: onHostKey ?? (_) async => true,
        );
        return ssh;
      } on SocketException {
        if (attempt >= 8) rethrow;
        await Future.delayed(const Duration(seconds: 1));
      }
    }
  }

  /// Kills every SSH session of [user] on the server, as a network failure
  /// would, while sshd keeps listening.
  Future<void> dropConnections(String user) => root(
        'pkill -KILL -f "sshd-session: $user" || pkill -KILL -f "sshd: $user"; exit 0',
      );
}

/// Registers setUpAll/tearDownAll for a file that needs the real server and
/// returns an accessor. Tests call [IntegrationHarness.server] first; it skips
/// the test when there is no server.
class IntegrationHarness {
  IntegrationServer? _server;
  String? _why;

  IntegrationHarness() {
    setUpAll(() async {
      _server = await IntegrationServer.acquire();
      _why = IntegrationServer.unavailableReason;
    });
    tearDownAll(() async {
      await _server?.release();
    });
    setUp(() async {
      SharedPreferences.setMockInitialValues({});
      await _server?.restoreBaseline();
    });
  }

  /// The server, or the test is skipped.
  IntegrationServer get server {
    final s = _server;
    if (s == null) {
      markTestSkipped('integration server unavailable: $_why');
      throw const _Skipped();
    }
    return s;
  }
}

class _Skipped implements Exception {
  const _Skipped();
  @override
  String toString() => 'skipped (no integration server)';
}
