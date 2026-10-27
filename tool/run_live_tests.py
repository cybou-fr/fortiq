#!/usr/bin/env python3
"""Runs every FORTIQ test that needs a disposable Linux fixture and prints one evidence table.

It never touches a real server: the fixture is a throw-away Debian (for example the WSL distro `fortiq-test`
with sshd, PostgreSQL 17, notmuch, isync, msmtp, dovecot and tool/setup_mail_live_fixture.sh already run).
Second host (needed by twoHostsNeverMix, otherwise it is SKIPPED and listed as such): a copy of the fixture made independent by tool/setup_second_host_fixture.sh, passed as --port-b/--user-b/--fingerprint-b.
Fixtures to prepare once as root on the disposable Debian: tool/setup_mail_live_fixture.sh, then tool/setup_mail_tls_fixture.sh (real TLS; re-running the first one turns it off), tool/setup_browser_policy_fixture.sh, tool/setup_db_roles_fixture.sh.
The live tests delete files in the test user's home (~/Mail, ~/.msmtprc, ~/Downloads, ...): see docs/AUDIT_2026-10-23.md.

Usage (Windows, after a build in build-msvc):
  set FORTIQ_IT_PASSWORD=<test user's throw-away password>
  python tool/run_live_tests.py --host 127.0.0.1 --port 2222 --user fqshell ^
      --fingerprint SHA256:... --key C:/path/to/fixture_key --wsl-distro fortiq-test

Exit status: 0 only if every executed test passed AND every skipped test has a waiver valid in scope "live" (tool/gate_waivers.json).
A skip is NOT a pass: an unwaived skip makes the run fail (exit 1), a waived one is printed as such.
"""
import argparse, os, re, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_test_results import DEFAULT_WAIVERS, load_waivers, skip_key


def run(exe: Path, env: dict, args: list[str], report: Path, timeout: int):
    cmd = [str(exe), *args, "-o", f"{report},txt"]
    try:
        proc = subprocess.run(cmd, env=env, cwd=exe.parent, capture_output=True, text=True, timeout=timeout)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        return {"name": exe.stem, "rc": -1, "totals": "TIMEOUT", "failed": ["timeout"], "skipped": []}
    text = report.read_text(encoding="utf-8", errors="replace") if report.exists() else proc.stdout
    totals = next((l for l in text.splitlines() if l.startswith("Totals:")), "no totals (crashed?)")
    failed = [l for l in text.splitlines() if l.startswith("FAIL!")]
    skipped = [l for l in text.splitlines() if l.startswith("SKIP")]
    return {"name": exe.stem, "rc": rc, "totals": totals, "failed": failed, "skipped": skipped}


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # a failure message may hold a replacement character; never crash while reporting
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build-msvc")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", required=True)
    ap.add_argument("--user", required=True)
    ap.add_argument("--fingerprint", required=True)
    ap.add_argument("--key", default="", help="private key of the test user (publication tests use it)")
    ap.add_argument("--wsl-distro", default="", help="WSL distro name for the Logs reader test")
    ap.add_argument("--upload-dir", default="", help="writable directory on the fixture for the transfer tests")
    ap.add_argument("--host-b", default="", help="second fixture (tool/setup_second_host_fixture.sh): host, defaults to --host")
    ap.add_argument("--port-b", default="", help="second fixture: SSH port; with --user-b and --fingerprint-b it enables twoHostsNeverMix")
    ap.add_argument("--user-b", default="")
    ap.add_argument("--fingerprint-b", default="")
    ap.add_argument("--log-tag", default="", help="tool/setup_logs_fixture.sh: tag of the three user.err lines")
    ap.add_argument("--crash-unit", default="", help="tool/setup_logs_fixture.sh: failing user service")
    ap.add_argument("--rebind", action="store_true", help="tool/setup_dns_rebind_fixture.sh is running: enables browserSurvivesDnsRebinding")
    ap.add_argument("--weak-base", default="2431", help="base port of the weak-ssh fixture (tool/setup_weak_ssh_fixture.sh)")
    ap.add_argument("--weak-ssh", action="store_true", help="tool/setup_weak_ssh_fixture.sh is running: enables obsoleteAlgorithmsAreRefused")
    ap.add_argument("--web-base", default="", help="URL of tests/fixtures/web_fixture.py as seen from the fixture")
    ap.add_argument("--gateway-tls", action="store_true", help="tool/setup_gateway_fixture.sh is running (HTTPS on 8443): enables the TLS check of the gateway live test")
    ap.add_argument("--qt-bin", default="C:/Qt/6.12.0/msvc2022_64/bin")
    ap.add_argument("--vcpkg-bin", default="C:/Users/cybou/vcpkg/installed/x64-windows/bin")
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--only", default="", help="comma-separated suite names (for example fortiq_engine_it): run just these, for development; a release run uses none")
    opts = ap.parse_args()

    build = Path(opts.build).resolve()
    if not os.environ.get("FORTIQ_IT_PASSWORD") and not opts.key:
        sys.exit("Set FORTIQ_IT_PASSWORD (throw-away password of the test user) or pass --key.")
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([opts.qt_bin, opts.vcpkg_bin, env.get("PATH", "")])
    env.update({
        "FORTIQ_IT_HOST": opts.host, "FORTIQ_IT_PORT": opts.port, "FORTIQ_IT_USER": opts.user, "FORTIQ_IT_FINGERPRINT": opts.fingerprint,
        "FORTIQ_IT_DB": "1", "FORTIQ_IT_MAIL": "1", "FORTIQ_IT_MAILSEND": "1", "FORTIQ_IT_MAILTLS": "1", "FORTIQ_IT_POLICY": "1", "FORTIQ_IT_REBIND": "1" if opts.rebind else "", "FORTIQ_IT_GATEWAY_TLS": "1" if opts.gateway_tls else "", "FORTIQ_IT_WEAK": "1" if opts.weak_ssh else "", "FORTIQ_IT_WEAK_BASE": opts.weak_base,
        "FORTIQ_PUBLICATION_HOST": opts.host, "FORTIQ_PUBLICATION_PORT": opts.port, "FORTIQ_PUBLICATION_USER": opts.user,
        "FORTIQ_PUBLICATION_FINGERPRINT": opts.fingerprint,
    })
    if opts.key:
        env["FORTIQ_IT_KEY"] = opts.key
        env["FORTIQ_PUBLICATION_KEY"] = opts.key
    if opts.port_b and opts.user_b and opts.fingerprint_b:
        env.update({"FORTIQ_IT_HOST_B": opts.host_b or opts.host, "FORTIQ_IT_PORT_B": opts.port_b,
                    "FORTIQ_IT_USER_B": opts.user_b, "FORTIQ_IT_FINGERPRINT_B": opts.fingerprint_b})
    if opts.upload_dir:
        env["FORTIQ_IT_UPLOAD_DIR"] = opts.upload_dir
    if opts.log_tag:
        env["FORTIQ_IT_LOG_TAG"] = opts.log_tag
    if opts.crash_unit:
        env["FORTIQ_IT_CRASH_UNIT"] = opts.crash_unit
    if opts.web_base:
        env["FORTIQ_IT_WEB_BASE"] = opts.web_base
    if opts.wsl_distro:
        env["FORTIQ_TEST_WSL"] = opts.wsl_distro
    env["MSYS_NO_PATHCONV"] = "1"

    # A hostile SSH server on the loopback of THIS machine (tests/fixtures/evil_ssh_server.py): malformed transport packets, R14.
    import atexit, socket
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        evil_port = probe.getsockname()[1]
    evil = subprocess.Popen([sys.executable, str(Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "evil_ssh_server.py"), str(evil_port)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    atexit.register(evil.terminate)
    env["FORTIQ_IT_EVIL_PORT"] = str(evil_port)

    results = []
    # Offline suites first (they also run under ctest), then the live ones.
    for name in ["fortiq_task_tests", "fortiq_db_tests", "fortiq_mail_tests", "fortiq_mailsend_tests", "fortiq_browser_tests", "fortiq_network_tests",
                 "fortiq_system_tests", "fortiq_credentials_tests", "fortiq_document_tests", "fortiq_logs_tests", "fortiq_publication_tests", "fortiq_gateway_tests", "fortiq_gateway_it", "fortiq_engine_tests", "fortiq_engine_it", "fortiq_ssh_it"]:
        if opts.only and name not in opts.only.split(","):
            continue
        exe = build / f"{name}.exe"
        if not exe.exists():
            results.append({"name": name, "rc": -1, "totals": "NOT BUILT", "failed": ["missing executable"], "skipped": []})
            continue
        results.append(run(exe, env, [], build / f"audit-{name}.txt", opts.timeout))

    print("| suite | result |")
    print("|---|---|")
    bad = 0
    for r in results:
        ok = r["rc"] == 0 and not r["failed"]
        bad += 0 if ok else 1
        print(f"| {r['name']} | {'OK' if ok else 'FAILED'} - {r['totals']} |")
    for r in results:
        for l in r["failed"]:
            print("FAILED:", r["name"], l)
    print("\nSkipped (not passes):")
    waivers = load_waivers(DEFAULT_WAIVERS)
    unwaived = 0
    for r in results:
        for l in r["skipped"]:
            waiver = waivers.get(skip_key(l))
            waived = waiver is not None and "live" in waiver.get("scopes", ["offline", "live", "release"])
            unwaived += 0 if waived else 1
            print(" -", r["name"], re.sub(r"^SKIP\s*:\s*", "", l), "[waived: " + waiver["reason"] + "]" if waived else "[NO WAIVER: the run fails]")
    sys.exit(1 if bad or unwaived else 0)


if __name__ == "__main__":
    main()
