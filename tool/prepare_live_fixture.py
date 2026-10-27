#!/usr/bin/env python3
"""One command that prepares the DISPOSABLE WSL fixture for every live test and runs them (stabilization step 2).

It replaces the manual order of tool/setup_*.sh scripts and the hand-copied values (fingerprint, key, password, log tag, ...):

  python tool/prepare_live_fixture.py [--distro fortiq-test] [--user fqshell] [--ssh-port 2222] [--no-run] [--teardown-only]

What it does (everything as root INSIDE the throw-away distro, never on a real server and never on a virtual machine of the owner):
  1. keeps the distro alive (WSL stops idle distros together with sshd and the background services) and checks sshd;
  2. creates a THROW-AWAY key pair and password for the test user for this run only (the key lives in a temporary folder, the
     password only in this process' environment) and authorizes the key;
  3. runs tool/setup_mail_live_fixture.sh, setup_mail_tls_fixture.sh, setup_browser_policy_fixture.sh, setup_gateway_fixture.sh (HTTPS for the gateway), setup_db_roles_fixture.sh,
     setup_logs_fixture.sh, setup_weak_ssh_fixture.sh start, setup_dns_rebind_fixture.sh start, in the order their comments demand;
  4. reads the host key fingerprint of the server (ECDSA first: FORTIQ pins that type first) and the values the scripts print;
  5. checks that every service the tests need is really listening and fails loudly if not (a dead TLS sink is exactly what made a
     test red before);
  6. runs tool/run_live_tests.py with all arguments (unless --no-run);
  7. ALWAYS removes the throw-away key from authorized_keys and stops the weak-ssh and DNS-flip helpers, even after a failure.

The second-host test (twoHostsNeverMix) needs a COPY of the distro made independent by tool/setup_second_host_fixture.sh (default name
`fortiq-test-b`, user `fqother`). When that distro exists the script also: keeps it alive, moves its sshd to a port Windows can bind,
authorizes the same throw-away key for `fqother`, reads its host key and passes --port-b/--user-b/--fingerprint-b to the run; the key is
removed from it afterwards too. Without the copy (or with --second-host "") the test stays SKIPPED and is listed as such.
"""
import argparse, os, secrets, shutil, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tool"
MARK = "fortiq-live-run"          # comment on the authorized_keys line, used to remove exactly our line


def wsl(distro, script, check=True, user="root", capture=True):
    """Runs a shell script (stdin) in the distro and returns stdout. Scripts are passed through stdin to avoid quoting problems."""
    proc = subprocess.run(["wsl", "-d", distro, "-u", user, "--", "bash", "-s"], input=script.replace("\r\n", "\n").encode("utf-8"),
                          capture_output=capture, env={**os.environ, "MSYS_NO_PATHCONV": "1"})   # bytes: text mode would turn LF into CRLF
    out = proc.stdout.decode("utf-8", "replace") if capture else ""
    if check and proc.returncode != 0:
        raise RuntimeError(f"in {distro}: exit {proc.returncode}\n{out}\n{proc.stderr.decode('utf-8', 'replace')}")
    return out


def free_weak_base() -> int:
    """A base port B such that B..B+4 can be bound on Windows: ports inside the Hyper-V/WinNAT excluded ranges (they MOVE between boots)
    cannot be forwarded from WSL to Windows, which made the weak-ssh fixture silently unreachable."""
    import socket
    for base in range(2431, 3000, 5):
        socks = []
        try:
            for port in range(base, base + 5):
                s = socket.socket()
                s.bind(("127.0.0.1", port))
                socks.append(s)
            return base
        except OSError:
            continue
        finally:
            for s in socks:
                s.close()
    raise RuntimeError("no block of five free ports found for the weak-ssh fixture")


def free_port(start: int, avoid: set[int]) -> int:
    """One port >= start that can be bound on Windows (see free_weak_base) and is not in `avoid`."""
    import socket
    for port in range(start, start + 400):
        if port in avoid:
            continue
        s = socket.socket()
        try:
            s.bind(("127.0.0.1", port))
            return port
        except OSError:
            continue
        finally:
            s.close()
    raise RuntimeError("no free port found")


def distro_exists(name: str) -> bool:
    out = subprocess.run(["wsl", "-l", "-q"], capture_output=True).stdout.decode("utf-16-le", "ignore").replace("\x00", "")
    return name in [l.strip() for l in out.splitlines()]


def wslpath(distro, path: Path) -> str:
    out = subprocess.run(["wsl", "-d", distro, "--", "wslpath", "-a", str(path).replace("\\", "/")], capture_output=True, text=True, encoding="utf-8")
    return out.stdout.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--distro", default="fortiq-test")
    ap.add_argument("--user", default="fqshell")
    ap.add_argument("--ssh-port", default="2222")
    ap.add_argument("--build", default="build-msvc")
    ap.add_argument("--second-host", default="fortiq-test-b", help="WSL distro of the independent second server (empty = none: twoHostsNeverMix stays skipped)")
    ap.add_argument("--user-b", default="fqother")
    ap.add_argument("--only", default="", help="pass --only to tool/run_live_tests.py (comma-separated suite names, development only)")
    ap.add_argument("--no-run", action="store_true", help="only prepare and print the run_live_tests.py command line")
    ap.add_argument("--teardown-only", action="store_true", help="only remove the key and stop the helpers (cleanup after a crash)")
    opts = ap.parse_args()
    d, u = opts.distro, opts.user

    second = opts.second_host if opts.second_host and distro_exists(opts.second_host) else ""

    def teardown():
        if second:
            wsl(second, f"sed -i '/{MARK}/d' /home/{opts.user_b}/.ssh/authorized_keys 2>/dev/null; true", check=False)
        wsl(d, f"""sed -i '/{MARK}/d' /home/{u}/.ssh/authorized_keys 2>/dev/null
bash {wslpath(d, TOOL / 'setup_weak_ssh_fixture.sh')} stop >/dev/null 2>&1
bash {wslpath(d, TOOL / 'setup_dns_rebind_fixture.sh')} stop >/dev/null 2>&1
bash {wslpath(d, TOOL / 'setup_gateway_fixture.sh')} stop >/dev/null 2>&1
true""", check=False)

    if opts.teardown_only:
        teardown(); print("teardown done"); return

    keep = subprocess.Popen(["wsl", "-d", d, "-u", "root", "--", "sleep", "86400"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    keep_b = None
    keep_fixture = False
    work = Path(tempfile.mkdtemp(prefix="fortiq-live-"))
    try:
        time.sleep(1)
        wsl(d, "mkdir -p /run/sshd; ss -ltn | grep -q ':%s ' || /usr/sbin/sshd" % opts.ssh_port)
        wsl(d, "for i in 1 2 3 4 5; do ss -ltn | grep -q ':%s ' && exit 0; sleep 1; done; echo 'sshd is not listening' >&2; exit 1" % opts.ssh_port)

        # 2. throw-away key + password for this run only
        key = work / "fixture_key"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", MARK, "-f", str(key)], check=True, stdin=subprocess.DEVNULL, timeout=60)
        pub = (work / "fixture_key.pub").read_text().strip()
        password = secrets.token_urlsafe(18)
        wsl(d, f"""set -e
install -d -m 700 -o {u} -g {u} /home/{u}/.ssh
touch /home/{u}/.ssh/authorized_keys
sed -i '/{MARK}/d' /home/{u}/.ssh/authorized_keys
echo '{pub}' >> /home/{u}/.ssh/authorized_keys
chown {u}:{u} /home/{u}/.ssh/authorized_keys; chmod 600 /home/{u}/.ssh/authorized_keys
echo '{u}:{password}' | chpasswd
sshd -T | grep -qi '^passwordauthentication yes' || {{ echo 'sshd must allow password login on the fixture' >&2; exit 1; }}""")

        # 2b. the independent second server (own host keys, own user, own files): same throw-away key, a port Windows can bind
        port_b = ""
        fp_b = ""
        if second:
            keep_b = subprocess.Popen(["wsl", "-d", second, "-u", "root", "--", "sleep", "86400"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1)
            port_b = str(free_port(2440, {int(opts.ssh_port)}))
            wsl(second, f"""set -e
mkdir -p /run/sshd /etc/ssh/sshd_config.d
sed -i '/^[[:space:]]*Port[[:space:]]/d' /etc/ssh/sshd_config
printf 'Port %s\\n' {port_b} > /etc/ssh/sshd_config.d/99-fortiq-b.conf
id {opts.user_b} >/dev/null
install -d -m 700 -o {opts.user_b} -g {opts.user_b} /home/{opts.user_b}/.ssh
touch /home/{opts.user_b}/.ssh/authorized_keys
sed -i '/{MARK}/d' /home/{opts.user_b}/.ssh/authorized_keys
echo '{pub}' >> /home/{opts.user_b}/.ssh/authorized_keys
chown {opts.user_b}:{opts.user_b} /home/{opts.user_b}/.ssh/authorized_keys; chmod 600 /home/{opts.user_b}/.ssh/authorized_keys
(systemctl restart ssh 2>/dev/null || systemctl restart sshd 2>/dev/null || {{ pkill -f 'sshd.*{port_b}' ; /usr/sbin/sshd; }}) >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do ss -ltn | grep -q ':{port_b} ' && exit 0; sleep 1; done
echo 'second sshd is not listening on {port_b}' >&2; exit 1""")
            fp_b = wsl(second, "ssh-keygen -l -E sha256 -f /etc/ssh/ssh_host_ecdsa_key.pub").split()[1]

        # 3. the fixture scripts, in the order their comments demand
        sink = wslpath(d, ROOT / "tests" / "fixtures" / "mail" / "smtp_sink.py")
        web = wslpath(d, ROOT / "tests" / "fixtures" / "web_fixture.py")
        t = lambda name: wslpath(d, TOOL / name)
        wsl(d, f"bash {t('setup_mail_live_fixture.sh')} {sink} {u}")
        wsl(d, f"bash {t('setup_mail_tls_fixture.sh')} {sink} {u}")
        wsl(d, f"bash {t('setup_browser_policy_fixture.sh')} {web} {u}")
        tls_web = wslpath(d, ROOT / "tests" / "fixtures" / "tls_web_fixture.py")
        wsl(d, f"bash {t('setup_gateway_fixture.sh')} {tls_web} {u}")        # HTTPS endpoint for the Browser gateway tests
        wsl(d, f"bash {t('setup_db_roles_fixture.sh')}", check=False)       # needs the research database; the DB tests report their own state
        logs = wsl(d, f"sh {t('setup_logs_fixture.sh')} {u}").strip().splitlines()[-1].split()
        weak_base = free_weak_base()
        wsl(d, f"bash {t('setup_weak_ssh_fixture.sh')} start {weak_base}")
        wsl(d, f"bash {t('setup_dns_rebind_fixture.sh')} start")

        # 4. values the tests need
        fp = wsl(d, "ssh-keygen -l -E sha256 -f /etc/ssh/ssh_host_ecdsa_key.pub").split()[1]
        args = {"--log-tag": logs[logs.index("--log-tag") + 1], "--crash-unit": logs[logs.index("--crash-unit") + 1],
                "--upload-dir": logs[logs.index("--upload-dir") + 1]}

        # 5. every service the tests rely on must really listen (a silently dead helper = a red test that proves nothing)
        listening = wsl(d, "ss -ltn")
        missing = [p for p in ("1143", "1993", "2525", "2465", "2587", "8099", "8443", *[str(weak_base + i) for i in range(5)], "5432") if f":{p} " not in listening]
        if missing:
            raise RuntimeError("fixture services are not listening: " + ", ".join(missing))

        cmd = [sys.executable, str(TOOL / "run_live_tests.py"), "--build", opts.build, "--host", "127.0.0.1", "--port", opts.ssh_port,
               "--user", u, "--fingerprint", fp, "--key", str(key), "--wsl-distro", d, "--weak-ssh", "--weak-base", str(weak_base), "--rebind",
               "--web-base", "http://127.0.0.1:8099", "--gateway-tls"]
        for k, v in args.items():
            cmd += [k, v]
        if opts.only:
            cmd += ["--only", opts.only]
        # every port the tests rely on must be reachable FROM WINDOWS too (a WSL-side listener is not enough: see free_weak_base)
        import socket
        for port in [int(opts.ssh_port), 1143, 1993, 8099, 8443] + [weak_base + i for i in range(5)]:
            try:
                socket.create_connection(("127.0.0.1", port), timeout=3).close()
            except OSError:
                raise RuntimeError(f"port {port} listens in WSL but is not reachable from Windows (excluded port range? `netsh int ipv4 show excludedportrange protocol=tcp`)")
        if second:
            cmd += ["--port-b", port_b, "--user-b", opts.user_b, "--fingerprint-b", fp_b]
            try:
                socket.create_connection(("127.0.0.1", int(port_b)), timeout=3).close()
            except OSError:
                raise RuntimeError(f"second server port {port_b} listens in WSL but is not reachable from Windows")
        print("fixture ready; fingerprint", fp + (f"; second host {second}:{port_b} fingerprint {fp_b}" if second else "; no second host (twoHostsNeverMix will be skipped)"))
        if opts.no_run:
            keep_fixture = True
            print("the key and the helpers stay until `--teardown-only`; the password of this run is not printed (set your own with chpasswd)")
            print(" ".join(cmd))
            return
        env = {**os.environ, "FORTIQ_IT_PASSWORD": password}
        rc = subprocess.run(cmd, env=env, cwd=ROOT).returncode
        sys.exit(rc)
    finally:
        if not keep_fixture:
            teardown()
            shutil.rmtree(work, ignore_errors=True)
            keep.terminate()
            if keep_b is not None:
                keep_b.terminate()


if __name__ == "__main__":
    main()
