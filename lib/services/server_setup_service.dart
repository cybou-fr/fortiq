import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'shell.dart';

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

/// Everything the check finds out about the server. Read-only.
class ServerStatus {
  final String os;
  final String osId; // debian, ubuntu, ...
  final bool isRoot;
  final bool hasSudo;
  final List<ServerRequirement> missing;
  final List<String> upgradable; // package names
  final int securityUpdates;
  final bool rebootRequired;
  final bool listsStale; // apt package lists older than a day
  final bool unattendedOn;
  final bool fail2banActive;
  final bool sshDropInsSupported; // sshd_config includes sshd_config.d/*.conf
  final bool? sshPasswordAuth; // null: unknown (needs root)
  final String? sshRootLogin;
  final bool? ufwActive; // null: unknown (needs root)
  final List<int> publicTcpPorts;
  final List<int> publicUdpPorts;
  final int? sshPort;
  final String? clientIp;

  const ServerStatus({
    required this.os,
    required this.osId,
    required this.isRoot,
    required this.hasSudo,
    required this.missing,
    required this.upgradable,
    required this.securityUpdates,
    required this.rebootRequired,
    required this.listsStale,
    required this.unattendedOn,
    required this.fail2banActive,
    required this.sshDropInsSupported,
    required this.sshPasswordAuth,
    required this.sshRootLogin,
    required this.ufwActive,
    required this.publicTcpPorts,
    required this.publicUdpPorts,
    required this.sshPort,
    required this.clientIp,
  });

  /// Fortiq can run apt and edit system configuration itself.
  bool get canElevate => isRoot || hasSudo;
  bool get isSupportedOs => osId == 'debian' || osId == 'ubuntu';
}

// Environment for unattended apt runs: no prompts, no surprise service restarts
// (needrestart only lists what needs one), keep the admin's edited config files.
const _aptEnv = 'DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=l';
const _aptOpts = '-o DPkg::Lock::Timeout=120'
    ' -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold';

/// Read-only: no root needed. Arguments are the required package names.
@visibleForTesting
const probeScript = r'''
[ -r /etc/os-release ] && . /etc/os-release
echo "OS=$PRETTY_NAME"
echo "OS_ID=$ID"
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
echo "CLIENT_IP=${SSH_CONNECTION%% *}"
echo "SSH_PORT=$(echo "$SSH_CONNECTION" | awk '{print $4}')"
now=$(date +%s); m=$(stat -c %Y /var/lib/apt/lists 2>/dev/null || echo 0)
echo "LISTS_AGE=$((now - m))"
if dpkg-query -W -f='${Status}' unattended-upgrades 2>/dev/null | grep -q 'install ok installed' \
   && grep -qs 'Unattended-Upgrade "1"' /etc/apt/apt.conf.d/20auto-upgrades; then
  echo UNATTENDED=1
else
  echo UNATTENDED=0
fi
if systemctl is-active --quiet fail2ban 2>/dev/null; then echo FAIL2BAN=1; else echo FAIL2BAN=0; fi
if grep -Eqs '^[[:space:]]*Include[[:space:]]+/etc/ssh/sshd_config\.d/' /etc/ssh/sshd_config; then
  echo SSH_INCLUDE=1
else
  echo SSH_INCLUDE=0
fi
ss -H -tln 2>/dev/null | sed 's/^/TCPLISTEN /'
ss -H -uln 2>/dev/null | sed 's/^/UDPLISTEN /'
''';

/// Read-only, needs root: what sshd and the firewall are set to right now.
@visibleForTesting
const rootProbeScript = r'''
/usr/sbin/sshd -T 2>/dev/null | grep -Ei '^(passwordauthentication|permitrootlogin) ' | sed 's/^/SSHD /'
if [ -x /usr/sbin/ufw ]; then
  if /usr/sbin/ufw status 2>/dev/null | grep -q '^Status: active'; then echo UFW=active; else echo UFW=inactive; fi
else
  echo UFW=missing
fi
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

/// TCP ports that listen on a non-loopback address, from `ss -H -tln`.
List<int> parsePublicTcpPorts(String ssOutput) => _publicPorts(ssOutput, 'TCPLISTEN ');

/// UDP ports bound on a non-loopback address, from `ss -H -uln`.
List<int> parsePublicUdpPorts(String ssOutput) => _publicPorts(ssOutput, 'UDPLISTEN ');

List<int> _publicPorts(String ssOutput, String prefix) {
  final ports = <int>{};
  for (var line in const LineSplitter().convert(ssOutput)) {
    line = line.trim();
    if (line.startsWith(prefix)) line = line.substring(prefix.length).trim();
    final f = line.split(RegExp(r'\s+'));
    if (f.length < 5) continue; // State Recv-Q Send-Q Local Peer
    final local = f[3];
    final colon = local.lastIndexOf(':');
    if (colon < 0) continue;
    final host = local.substring(0, colon);
    final port = int.tryParse(local.substring(colon + 1));
    if (port == null) continue;
    final loopback = host.startsWith('127.') || host == '[::1]' || host.contains('%lo');
    if (!loopback) ports.add(port);
  }
  return ports.toList()..sort();
}

// ---- the scripts the steps run (pure functions: easy to read and to test) ----

/// `printf` that writes [lines] to [path] (one per line).
String _writeFile(String path, List<String> lines) =>
    "printf '%s\\n' ${lines.map(shQuote).join(' ')} > ${shQuote(path)}";

const unattendedConfigPath = '/etc/apt/apt.conf.d/20auto-upgrades';

String unattendedScript() => _writeFile(unattendedConfigPath, [
      'APT::Periodic::Update-Package-Lists "1";',
      'APT::Periodic::Unattended-Upgrade "1";',
    ]);

const fail2banJailPath = '/etc/fail2ban/jail.d/fortiq.local';

/// SSH brute-force protection. The address Fortiq connects from is never banned.
String fail2banScript(String? clientIp) {
  final safeIp =
      clientIp != null && RegExp(r'^[0-9a-fA-F:.]+$').hasMatch(clientIp) ? clientIp : null;
  return [
    'set -e',
    'mkdir -p /etc/fail2ban/jail.d',
    _writeFile(fail2banJailPath, [
      '[sshd]',
      'enabled = true',
      'backend = systemd',
      'maxretry = 5',
      'findtime = 10m',
      'bantime = 1h',
      'ignoreip = 127.0.0.1/8 ::1${safeIp != null ? ' $safeIp' : ''}',
    ]),
    'systemctl enable --now fail2ban',
    'fail2ban-client reload',
  ].join('\n');
}

const sshDropInPath = '/etc/ssh/sshd_config.d/00-fortiq-hardening.conf';

/// Key-only SSH login, no password login for root. sshd validates the result
/// before it is reloaded; a rejected configuration is removed again.
String sshHardeningScript() => [
      'set -e',
      _writeFile(sshDropInPath, [
        '# Managed by Fortiq',
        'PasswordAuthentication no',
        'ChallengeResponseAuthentication no',
        'PermitRootLogin prohibit-password',
      ]),
      'if ! /usr/sbin/sshd -t; then',
      '  rm -f ${shQuote(sshDropInPath)}',
      '  echo "sshd rejected the new configuration; change reverted" >&2',
      '  exit 1',
      'fi',
      '(systemctl reload ssh || systemctl reload sshd)',
    ].join('\n');

String sshRollbackScript() =>
    'rm -f ${shQuote(sshDropInPath)}; (systemctl reload ssh || systemctl reload sshd)';

/// Deny incoming by default, but keep SSH and everything that listens now open
/// (TCP and UDP), so no running service is cut off.
String firewallScript(List<int> tcpPorts, [List<int> udpPorts = const []]) => [
      'set -e',
      '/usr/sbin/ufw default deny incoming',
      '/usr/sbin/ufw default allow outgoing',
      for (final p in tcpPorts) '/usr/sbin/ufw allow $p/tcp',
      for (final p in udpPorts) '/usr/sbin/ufw allow $p/udp',
      '/usr/sbin/ufw --force enable',
    ].join('\n');

const firewallRollbackScript = '/usr/sbin/ufw --force disable';

enum StepStatus { pending, running, done, upToDate, skipped, failed, unknown }

enum SetupStepId { deps, upgrade, unattended, fail2ban, sshHardening, firewall }

class SetupStep {
  final SetupStepId id;
  final String title;
  final String what;

  /// Cannot run on this server (with the reason).
  String? unavailable;

  /// The check found something to do.
  bool needed = false;
  StepStatus state = StepStatus.pending;
  String? note;

  SetupStep(this.id, this.title, this.what);

  bool get willRun => unavailable == null && needed;
}

List<SetupStep> _newSteps() => [
      SetupStep(SetupStepId.deps, 'Required packages',
          'Installs tmux, procps and iproute2 (terminal sessions, process list, network info).'),
      SetupStep(SetupStepId.upgrade, 'System updates',
          'Refreshes the package lists and installs all available updates (apt-get upgrade). '
              'No prompts, services are not restarted, edited config files are kept.'),
      SetupStep(SetupStepId.unattended, 'Automatic security updates',
          'Turns on unattended-upgrades so the server keeps patching itself.'),
      SetupStep(SetupStepId.fail2ban, 'Brute-force protection',
          'Installs fail2ban with an SSH jail; the address you connect from is never banned.'),
      SetupStep(SetupStepId.sshHardening, 'SSH hardening',
          'Key-only login (no passwords) and no password login for root. '
              'Checked with a second login; undone automatically if it fails.'),
      SetupStep(SetupStepId.firewall, 'Firewall',
          'Installs ufw and blocks incoming connections, except SSH and the ports that '
              'listen right now. Checked with a second login; undone automatically if it fails.'),
    ];

enum SetupPhase { idle, inspecting, running, finished }

/// Checks the server, brings it up to date and hardens it. Debian/Ubuntu only.
///
/// This is what happens when Fortiq connects: the check is read-only, and
/// everything it finds to do is then done, step by step, in front of the user
/// (the setup dialog shows the steps and the live log). Steps that could cut the
/// server off (SSH, firewall) are proven with a second login and undone if that
/// fails.
class ServerSetupService extends ChangeNotifier {
  final RemoteShell ssh;
  final String host;
  final int port;

  ServerSetupService(this.ssh, {required this.host, required this.port});

  bool _disposed = false;
  bool _stopRequested = false;

  // The workspace can be closed while a check or an apt run is still going.
  // A running job is left to finish on the server (interrupting a package
  // installation is worse), but nothing touches the UI after dispose and no
  // new step is started.
  @override
  void dispose() {
    _disposed = true;
    super.dispose();
  }

  @override
  void notifyListeners() {
    if (!_disposed) super.notifyListeners();
  }

  ServerStatus? status;
  SetupPhase phase = SetupPhase.idle;
  String? error;
  List<SetupStep> steps = _newSteps();
  final List<String> log = [];

  bool get busy => phase == SetupPhase.inspecting || phase == SetupPhase.running;

  /// The last check found something that [run] would do.
  bool get hasWork => steps.any((s) => s.willRun);

  void _log(String line) {
    log.add(line);
    if (log.length > 600) log.removeRange(0, log.length - 600);
    notifyListeners();
  }

  /// Stop after the step that is running now.
  void requestStop() {
    _stopRequested = true;
    notifyListeners();
  }

  // ---------------------------------------------------------------- check

  /// Read-only look at the server.
  Future<void> inspect() async {
    if (busy) return;
    error = null;
    phase = SetupPhase.inspecting;
    notifyListeners();
    try {
      await _inspect();
    } catch (e) {
      error = e.toString();
    } finally {
      phase = SetupPhase.idle;
      notifyListeners();
    }
  }

  Future<void> _inspect() async {
    final out = await ssh.execute(
      "sh -c ${shQuote(probeScript)} sh ${kRequiredPackages.map((r) => r.package).join(' ')}",
    );
    var os = 'Linux', osId = '';
    var root = false, sudo = false, reboot = false;
    var unattended = false, fail2ban = false, sshInclude = false;
    var listsAge = 1 << 30;
    int? sshPort;
    String? clientIp;
    final missing = <ServerRequirement>[];
    final listenLines = <String>[];
    final udpLines = <String>[];
    for (final line in LineSplitter.split(out)) {
      if (line.startsWith('TCPLISTEN ')) {
        listenLines.add(line);
        continue;
      }
      if (line.startsWith('UDPLISTEN ')) {
        udpLines.add(line);
        continue;
      }
      final i = line.indexOf('=');
      if (i < 0) continue;
      final key = line.substring(0, i);
      final value = line.substring(i + 1).trim();
      switch (key) {
        case 'OS':
          if (value.isNotEmpty) os = value;
        case 'OS_ID':
          osId = value.toLowerCase();
        case 'ROOT':
          root = value == '1';
        case 'SUDO':
          sudo = value == '1';
        case 'REBOOT':
          reboot = value == '1';
        case 'PKG_MISSING':
          missing.addAll(kRequiredPackages.where((r) => r.package == value));
        case 'CLIENT_IP':
          if (value.isNotEmpty) clientIp = value;
        case 'SSH_PORT':
          sshPort = int.tryParse(value);
        case 'LISTS_AGE':
          listsAge = int.tryParse(value) ?? listsAge;
        case 'UNATTENDED':
          unattended = value == '1';
        case 'FAIL2BAN':
          fail2ban = value == '1';
        case 'SSH_INCLUDE':
          sshInclude = value == '1';
      }
    }

    bool? sshPassword;
    String? sshRoot;
    bool? ufwActive;
    if (root || sudo) {
      try {
        final rootOut = await ssh.execute(asRoot(rootProbeScript));
        for (final line in LineSplitter.split(rootOut)) {
          final parts = line.trim().split(RegExp(r'\s+'));
          if (parts.length >= 3 && parts[0] == 'SSHD') {
            if (parts[1].toLowerCase() == 'passwordauthentication') {
              sshPassword = parts[2].toLowerCase() == 'yes';
            } else if (parts[1].toLowerCase() == 'permitrootlogin') {
              sshRoot = parts[2].toLowerCase();
            }
          } else if (line.startsWith('UFW=')) {
            ufwActive = line.substring(4).trim() == 'active';
          }
        }
      } catch (_) {
        // Unknown stays null: the dependent steps are then treated as needed.
      }
    }

    var parsed = (names: <String>[], security: 0);
    try {
      parsed = parseUpgradable(await ssh.execute('LC_ALL=C apt list --upgradable 2>/dev/null'));
    } catch (_) {}

    final st = ServerStatus(
      os: os,
      osId: osId,
      isRoot: root,
      hasSudo: sudo,
      missing: missing,
      upgradable: parsed.names,
      securityUpdates: parsed.security,
      rebootRequired: reboot,
      listsStale: listsAge > 24 * 3600,
      unattendedOn: unattended,
      fail2banActive: fail2ban,
      sshDropInsSupported: sshInclude,
      sshPasswordAuth: sshPassword,
      sshRootLogin: sshRoot,
      ufwActive: ufwActive,
      publicTcpPorts: parsePublicTcpPorts(listenLines.join('\n')),
      publicUdpPorts: parsePublicUdpPorts(udpLines.join('\n')),
      sshPort: sshPort,
      clientIp: clientIp,
    );
    status = st;
    steps = _planFor(st);
  }

  List<SetupStep> _planFor(ServerStatus st) {
    final plan = _newSteps();
    SetupStep step(SetupStepId id) => plan.firstWhere((s) => s.id == id);

    String? reason;
    if (!st.isSupportedOs) {
      reason = 'Only Debian and Ubuntu are supported (this is ${st.os}).';
    } else if (!st.canElevate) {
      reason = 'Needs root or passwordless sudo.';
    }
    for (final s in plan) {
      s.unavailable = reason;
    }
    if (reason != null) return plan;

    step(SetupStepId.deps).needed = st.missing.isNotEmpty;
    // With stale package lists the real number of updates is unknown, so the
    // step runs: it refreshes the lists first.
    step(SetupStepId.upgrade).needed = st.upgradable.isNotEmpty || st.listsStale;
    step(SetupStepId.unattended).needed = !st.unattendedOn;
    step(SetupStepId.fail2ban).needed = !st.fail2banActive;

    final sshStep = step(SetupStepId.sshHardening);
    if (!ssh.usedKeyAuth) {
      sshStep.unavailable = 'You connected with a password. Connect with an SSH key first, '
          'or this step could lock you out.';
    } else if (!st.sshDropInsSupported) {
      sshStep.unavailable =
          'sshd_config does not include /etc/ssh/sshd_config.d (old OpenSSH setup).';
    } else {
      sshStep.needed = st.sshPasswordAuth != false || st.sshRootLogin == 'yes';
    }

    step(SetupStepId.firewall).needed = st.ufwActive != true;
    return plan;
  }

  // ------------------------------------------------------------------ run

  /// Does everything the check found, in order. Stops at the first failure.
  Future<void> run() async {
    if (busy) return;
    error = null;
    _stopRequested = false;
    log.clear();
    phase = SetupPhase.running;
    notifyListeners();
    var listsRefreshed = false;

    Future<void> refreshLists() async {
      if (listsRefreshed) return;
      await _sh('apt-get update -qq');
      listsRefreshed = true;
    }

    try {
      if (status == null) await _inspect();
      var failed = false;
      for (final step in steps) {
        if (_disposed) return;
        if (step.unavailable != null) {
          step.state = StepStatus.skipped;
          step.note = step.unavailable;
          continue;
        }
        if (!step.needed) {
          step.state = StepStatus.upToDate;
          continue;
        }
        if (failed || _stopRequested) {
          step.state = StepStatus.skipped;
          step.note = failed ? 'Not run: an earlier step failed.' : 'Stopped.';
          continue;
        }
        step.state = StepStatus.running;
        _log('');
        _log('== ${step.title} ==');
        notifyListeners();
        try {
          await _apply(step, refreshLists);
          if (step.state == StepStatus.running) step.state = StepStatus.done;
        } catch (e) {
          if (!ssh.isConnected) {
            // The connection dropped while the step was running: it may or may
            // not have finished on the server. It is never repeated blindly.
            step.state = StepStatus.unknown;
            step.note = 'Connection lost during this step, so the result is unknown. '
                'Reconnect: Fortiq checks the server again.';
          } else {
            step.state = StepStatus.failed;
            step.note = e.toString().replaceFirst('Exception: ', '');
          }
          _log('!! ${step.note}');
          failed = true;
        }
        notifyListeners();
      }

      if (!_disposed && ssh.isConnected) {
        // What is true now, after the changes. The result of each step stays.
        final results = {for (final s in steps) s.id: (s.state, s.note)};
        await _inspect();
        for (final s in steps) {
          final r = results[s.id];
          if (r != null) {
            s.state = r.$1;
            s.note = r.$2;
          }
        }
      }
    } catch (e) {
      error = e.toString();
    } finally {
      phase = SetupPhase.finished;
      notifyListeners();
    }
  }

  Future<void> _apply(SetupStep step, Future<void> Function() refreshLists) async {
    final st = status!;
    switch (step.id) {
      case SetupStepId.deps:
        await refreshLists();
        await _aptInstall(st.missing.map((r) => r.package).toList());
      case SetupStepId.upgrade:
        await refreshLists();
        final list =
            parseUpgradable(await ssh.execute('LC_ALL=C apt list --upgradable 2>/dev/null'));
        if (list.names.isEmpty) {
          step.note = 'Already up to date.';
          step.state = StepStatus.upToDate;
        } else {
          _log('${list.names.length} updates (${list.security} security)');
          await _sh('$_aptEnv apt-get upgrade -y $_aptOpts');
          step.note = '${list.names.length} packages updated.';
        }
      case SetupStepId.unattended:
        await refreshLists();
        await _aptInstall(['unattended-upgrades']);
        await _sh(unattendedScript());
      case SetupStepId.fail2ban:
        await refreshLists();
        await _aptInstall(['fail2ban', 'python3-systemd']);
        await _sh(fail2banScript(st.clientIp));
      case SetupStepId.sshHardening:
        await _sh(sshHardeningScript());
        _log('Checking that a new login still works…');
        if (!await ssh.canLoginAgain()) {
          _log('A new login failed: undoing the SSH change.');
          await _sh(sshRollbackScript());
          throw Exception('A second login did not work after the change, so it was undone. '
              'SSH is as it was.');
        }
        step.note = 'Key-only login. A second login was checked.';
      case SetupStepId.firewall:
        await refreshLists();
        await _aptInstall(['ufw']);
        final ports = <int>{...st.publicTcpPorts, st.sshPort ?? port}.toList()..sort();
        final udp = st.publicUdpPorts;
        _log('Open: TCP ${ports.join(', ')}${udp.isEmpty ? '' : '; UDP ${udp.join(', ')}'}');
        await _sh(firewallScript(ports, udp));
        _log('Checking that a new login still works…');
        if (!await ssh.canLoginAgain()) {
          _log('A new login failed: switching the firewall off again.');
          await _sh(firewallRollbackScript);
          throw Exception('A second login did not work after the change, so the firewall '
              'was switched off again.');
        }
        step.note = 'Open: TCP ${ports.join(', ')}${udp.isEmpty ? '' : '; UDP ${udp.join(', ')}'}. '
            'A second login was checked.';
    }
  }

  Future<void> _aptInstall(List<String> packages) =>
      _sh('$_aptEnv apt-get install -y $_aptOpts ${packages.join(' ')}');

  /// Runs [script] as root and streams its output into the log.
  Future<void> _sh(String script) async {
    for (final line in const LineSplitter().convert(script)) {
      _log('\$ $line');
    }
    final code = await ssh.runStreaming(asRoot(script), _log);
    if (code != 0) {
      throw Exception('Command failed (exit code $code), see the log');
    }
  }
}
