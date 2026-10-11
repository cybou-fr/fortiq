import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'ssh_service.dart';

/// A package Fortiq itself relies on on the server (Debian/Ubuntu).
class ServerRequirement {
  final String package;
  final String purpose;

  const ServerRequirement(this.package, this.purpose);
}

const kRequiredPackages = [
  ServerRequirement('tmux', 'terminals that survive a lost connection'),
  ServerRequirement('procps', 'process list and system metrics (ps, top, free)'),
  ServerRequirement('iproute2', 'network information (ss, ip)'),
];

class ServerStatus {
  final String os;
  final bool isRoot;
  final bool hasSudo;
  final List<ServerRequirement> missing;
  final List<String> upgradable; // package names
  final int securityUpdates;
  final bool rebootRequired;

  const ServerStatus({
    required this.os,
    required this.isRoot,
    required this.hasSudo,
    required this.missing,
    required this.upgradable,
    required this.securityUpdates,
    required this.rebootRequired,
  });

  /// Fortiq can run apt itself.
  bool get canElevate => isRoot || hasSudo;
  bool get isUpToDate => missing.isEmpty && upgradable.isEmpty;
}

/// Remembers per server whether system updates are installed automatically.
class ServerSetupPrefs {
  static const _key = 'fortiq_auto_updates';

  static String _id(String host, int port) => '${host.toLowerCase()}:$port';

  static Future<bool> autoUpdate(String host, int port) async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_key);
    if (raw == null) return false;
    try {
      return (jsonDecode(raw) as Map<String, dynamic>)[_id(host, port)] == true;
    } catch (_) {
      return false;
    }
  }

  static Future<void> setAutoUpdate(String host, int port, bool value) async {
    final prefs = await SharedPreferences.getInstance();
    Map<String, dynamic> all = {};
    try {
      all = jsonDecode(prefs.getString(_key) ?? '{}') as Map<String, dynamic>;
    } catch (_) {}
    all[_id(host, port)] = value;
    await prefs.setString(_key, jsonEncode(all));
  }
}

// Environment for unattended apt runs: no prompts, no surprise service restarts
// (needrestart only lists what needs one), keep the admin's edited config files.
const _aptEnv = 'DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=l';
const _aptOpts = '-o DPkg::Lock::Timeout=120'
    ' -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold';

const _probeScript = r'''
[ -r /etc/os-release ] && . /etc/os-release
echo "OS=$PRETTY_NAME"
if [ "$(id -u)" = 0 ]; then echo ROOT=1; else echo ROOT=0; fi
if sudo -n true 2>/dev/null; then echo SUDO=1; else echo SUDO=0; fi
if [ -f /var/run/reboot-required ]; then echo REBOOT=1; else echo REBOOT=0; fi
for p in "$@"; do
  if dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'install ok installed'; then
    echo "PKG_OK=$p"
  else
    echo "PKG_MISSING=$p"
  fi
done
''';

/// Parses `LC_ALL=C apt list --upgradable` into package names and the number
/// of security updates.
({List<String> names, int security}) parseUpgradable(String output) {
  final names = <String>[];
  var security = 0;
  for (final line in const LineSplitter().convert(output)) {
    if (!line.contains('[upgradable from')) continue;
    names.add(line.split('/').first);
    if (line.contains('-security')) security++;
  }
  return (names: names, security: security);
}

/// Checks that the server has what Fortiq needs, installs what is missing and
/// keeps packages up to date. Debian/Ubuntu only.
class ServerSetupService extends ChangeNotifier {
  final SshClientService ssh;

  ServerSetupService(this.ssh);

  ServerStatus? status;
  bool busy = false;
  String busyMessage = '';
  String? error;
  bool autoUpdate = false;
  final List<String> log = [];

  void _log(String line) {
    log.add(line);
    if (log.length > 400) log.removeRange(0, log.length - 400);
    notifyListeners();
  }

  void _setBusy(String message) {
    busy = true;
    busyMessage = message;
    notifyListeners();
  }

  /// Full pass done on connect: check, install what is missing, update.
  Future<void> run() async {
    if (busy) return;
    error = null;
    try {
      autoUpdate = await ServerSetupPrefs.autoUpdate(
        ssh.currentHost ?? '',
        ssh.currentPort ?? 22,
      );
      _setBusy('Checking the server…');
      var st = await check(refresh: true);

      if (st.canElevate) {
        // 1. What Fortiq needs itself is always installed and kept current.
        if (st.missing.isNotEmpty) {
          await installPackages(st.missing.map((r) => r.package).toList());
          st = await check(refresh: false);
        }
        final own = kRequiredPackages
            .map((r) => r.package)
            .where(st.upgradable.contains)
            .toList();
        if (own.isNotEmpty) {
          await installPackages(own, onlyUpgrade: true);
          st = await check(refresh: false);
        }
        // 2. System-wide updates only when the user chose to have them applied
        //    automatically; otherwise they are offered in the status dialog.
        if (autoUpdate && st.upgradable.isNotEmpty) {
          await upgradeAll();
          st = await check(refresh: false);
        }
      }
    } catch (e) {
      error = e.toString();
    } finally {
      busy = false;
      notifyListeners();
    }
  }

  Future<ServerStatus> check({bool refresh = true}) async {
    final out = await ssh.execute(
      "sh -c ${shQuote(_probeScript)} sh ${kRequiredPackages.map((r) => r.package).join(' ')}",
    );
    var os = 'Linux';
    var root = false, sudo = false, reboot = false;
    final missing = <ServerRequirement>[];
    for (final line in LineSplitter.split(out)) {
      final i = line.indexOf('=');
      if (i < 0) continue;
      final key = line.substring(0, i);
      final value = line.substring(i + 1).trim();
      switch (key) {
        case 'OS':
          if (value.isNotEmpty) os = value;
        case 'ROOT':
          root = value == '1';
        case 'SUDO':
          sudo = value == '1';
        case 'REBOOT':
          reboot = value == '1';
        case 'PKG_MISSING':
          missing.addAll(kRequiredPackages.where((r) => r.package == value));
      }
    }

    // Refresh the package lists so the update count is current.
    if (refresh && (root || sudo)) {
      try {
        await ssh.execute(asRoot('apt-get update -qq -o DPkg::Lock::Timeout=60'));
      } catch (_) {
        // Offline mirror or a locked apt: fall back to the lists we have.
      }
    }

    var parsed = (names: <String>[], security: 0);
    try {
      parsed = parseUpgradable(await ssh.execute('LC_ALL=C apt list --upgradable 2>/dev/null'));
    } catch (_) {}

    final result = ServerStatus(
      os: os,
      isRoot: root,
      hasSudo: sudo,
      missing: missing,
      upgradable: parsed.names,
      securityUpdates: parsed.security,
      rebootRequired: reboot,
    );
    status = result;
    notifyListeners();
    return result;
  }

  Future<void> installPackages(List<String> packages, {bool onlyUpgrade = false}) async {
    final what = packages.join(', ');
    _setBusy(onlyUpgrade ? 'Updating $what…' : 'Installing $what…');
    final flags = onlyUpgrade ? '--only-upgrade ' : '';
    await _apt(
      'apt-get update -qq && $_aptEnv apt-get install -y $flags$_aptOpts ${packages.join(' ')}',
    );
  }

  Future<void> upgradeAll() async {
    _setBusy('Installing system updates…');
    await _apt('apt-get update -qq && $_aptEnv apt-get upgrade -y $_aptOpts');
  }

  Future<void> _apt(String command) async {
    _log(r'$ ' + command);
    final code = await ssh.runStreaming(asRoot('$_aptEnv $command'), _log);
    if (code != 0) {
      throw Exception('apt-get failed (exit code $code), see the log');
    }
  }

  /// Dialog actions.
  Future<void> installMissing() async {
    final st = status;
    if (st == null || st.missing.isEmpty) return;
    await _guarded(() async {
      await installPackages(st.missing.map((r) => r.package).toList());
    });
  }

  Future<void> installUpdates() => _guarded(upgradeAll);

  Future<void> recheck() => _guarded(() async {});

  Future<void> setAutoUpdate(bool value) async {
    autoUpdate = value;
    await ServerSetupPrefs.setAutoUpdate(ssh.currentHost ?? '', ssh.currentPort ?? 22, value);
    notifyListeners();
    if (value && (status?.upgradable.isNotEmpty ?? false)) await installUpdates();
  }

  Future<void> _guarded(Future<void> Function() action) async {
    if (busy) return;
    error = null;
    try {
      await action();
      _setBusy('Checking the server…');
      await check(refresh: false);
    } catch (e) {
      error = e.toString();
    } finally {
      busy = false;
      notifyListeners();
    }
  }
}
