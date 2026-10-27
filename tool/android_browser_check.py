#!/usr/bin/env python3
"""Android Browser engine check on a RUNNING EMULATOR (docs/BROWSER.md, "Android"): real WebView, real SSH gateway, disposable fixture.

It installs the DEVELOPER APK (built with FORTIQ_ANDROID_BROWSER_SELFTEST=ON; `tool/build_android.py ... --define FORTIQ_ANDROID_BROWSER_SELFTEST=ON
--package`), starts the app with `--browser-selftest` (src/browser/engine/android/BrowserSelfTest.cpp: EngineBrowser + the real WebView page,
no taps) and, from OUTSIDE the app, samples the socket tables of the app's uid on the device (/proc/net/tcp, tcp6, udp, udp6) the whole time:
the only connections it tolerates are loopback -> the gateway port, the SSH connection to the fixture (adb reverse -> 127.0.0.1:<ssh port>)
and listeners. Then it checks: JavaScript through the gateway, the canary page (every route out of a browser, incl. 127.0.0.1 of the DEVICE),
the foreground service and the gateway while the app is minimised, a lost SSH server (page unavailable, still nothing direct) and a stopped
gateway (the WebView hits a closed port, never the open network).

It never starts or touches a physical device or a virtual machine: it refuses to run unless exactly one device is attached and its serial
starts with "emulator-". The key is a THROW-AWAY key of the disposable fixture (tool/prepare_live_fixture.py --no-run prints it); it is copied
into the app's private files directory and removed at the end.

  python tool/android_browser_check.py --sdk C:/Users/<you>/AppData/Local/Android/Sdk --key <throwaway_key> --fingerprint SHA256:... \
      [--user fqshell] [--ssh-port 2222] [--web-base http://127.0.0.1:8099] [--wsl-distro fortiq-test] [--background-seconds 120] [--out build-dist]
"""
import argparse, json, re, subprocess, sys, threading, time
from pathlib import Path

PACKAGE = "fr.cybou.fortiq"
ACTIVITY = "org.qtproject.qt.android.bindings.QtActivity"
APK = "build-android-arm64/android-build/build/outputs/apk/debug/android-build-debug.apk"
FILES = f"/data/user/0/{PACKAGE}/files"
STATES = {"01": "ESTABLISHED", "02": "SYN_SENT", "03": "SYN_RECV", "04": "FIN_WAIT1", "05": "FIN_WAIT2", "06": "TIME_WAIT", "07": "CLOSE", "08": "CLOSE_WAIT",
          "09": "LAST_ACK", "0A": "LISTEN", "0B": "CLOSING"}


def decode(addr_port, v6):
    addr, port = addr_port.split(":")
    if not v6:
        b = bytes.fromhex(addr)[::-1]
        return ".".join(str(x) for x in b), int(port, 16)
    joined = "".join(bytes.fromhex(addr[i:i + 8])[::-1].hex() for i in range(0, 32, 8))
    return ":".join(joined[i:i + 4] for i in range(0, 32, 4)), int(port, 16)


def is_loopback(ip):
    return ip.startswith("127.") or ip in ("0.0.0.0", "0000:0000:0000:0000:0000:0000:0000:0001", "0000:0000:0000:0000:0000:0000:0000:0000") \
        or ip.endswith(":0000:ffff:7f00:0001") or ip.lower().startswith("0000:0000:0000:0000:0000:ffff:7f")


def dns_questions(path):
    """DNS questions (name, qtype) of a classic pcap written by `emulator -tcpdump` (Ethernet, IPv4/IPv6, UDP port 53)."""
    import struct
    data = Path(path).read_bytes()
    if len(data) < 24:
        return []
    endian = "<" if struct.unpack("<I", data[:4])[0] in (0xa1b2c3d4, 0xa1b23c4d) else ">"
    off, found = 24, []
    while off + 16 <= len(data):
        _, _, incl, _ = struct.unpack(endian + "IIII", data[off:off + 16])
        pkt = data[off + 16:off + 16 + incl]
        off += 16 + incl
        if len(pkt) < 14:
            continue
        et, p = struct.unpack(">H", pkt[12:14])[0], 14
        if et == 0x8100:
            et, p = struct.unpack(">H", pkt[16:18])[0], 18
        if et == 0x0800 and len(pkt) > p + 20 and pkt[p + 9] == 17:
            u = p + (pkt[p] & 15) * 4
        elif et == 0x86dd and len(pkt) > p + 40 and pkt[p + 6] == 17:
            u = p + 40
        else:
            continue
        if len(pkt) < u + 12 or struct.unpack(">H", pkt[u + 2:u + 4])[0] != 53:
            continue
        dns = pkt[u + 8:]
        if len(dns) < 13 or dns[2] & 0x80:
            continue
        i, labels = 12, []
        while i < len(dns) and dns[i]:
            labels.append(dns[i + 1:i + 1 + dns[i]].decode("latin1"))
            i += dns[i] + 1
        found.append((".".join(labels), struct.unpack(">H", dns[i + 1:i + 3])[0] if i + 3 <= len(dns) else 0))
    return found


class Sampler:
    """One adb session that loops on the device and prints only the lines of the app's uid; deduplicated here."""

    def __init__(self, adb, uid):
        self.seen = {}
        self.lines = 0
        script = f"while true; do for t in tcp tcp6 udp udp6; do grep -H -E ' {uid} +[0-9]+ +[0-9]+ ' /proc/net/$t; done; sleep 0.02; done"
        self.proc = subprocess.Popen([adb, "shell", script], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self.thread = threading.Thread(target=self.read, daemon=True)
        self.thread.start()

    def read(self):
        for raw in self.proc.stdout:
            self.lines += 1
            m = re.match(r"/proc/net/(tcp6?|udp6?):\s*\d+:\s+(\S+)\s+(\S+)\s+(\S+)", raw)
            if not m:
                continue
            proto, local, remote, st = m.groups()
            v6 = proto.endswith("6")
            lip, lport = decode(local, v6)
            rip, rport = decode(remote, v6)
            key = (proto, lip, lport, rip, rport, STATES.get(st, st))
            self.seen[key] = self.seen.get(key, 0) + 1

    def snapshot(self):
        return dict(self.seen)

    def stop(self):
        self.proc.kill()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--apk", default=APK)
    ap.add_argument("--key", required=True, help="throw-away private key of the disposable fixture")
    ap.add_argument("--fingerprint", required=True)
    ap.add_argument("--user", default="fqshell")
    ap.add_argument("--ssh-port", type=int, default=2222)
    ap.add_argument("--web-base", default="http://127.0.0.1:8099", help="the fixture web server AS THE SERVER SEES IT")
    ap.add_argument("--wsl-distro", default="", help="enables the lost-SSH-server step (kills the user's sshd sessions and the listener, restarts it afterwards)")
    ap.add_argument("--background-seconds", type=int, default=120)
    ap.add_argument("--out", default="build-dist")
    ap.add_argument("--pcap", default="", help="packet capture of the EMULATOR (emulator -tcpdump <file>), started before this run: every DNS question in it is listed and none may name a host the pages used")
    ap.add_argument("--quick", action="store_true", help="skip the minimised wait and the lost-SSH step (diagnosis)")
    opts = ap.parse_args()

    adb = str(Path(opts.sdk) / "platform-tools" / "adb.exe")
    if not Path(adb).exists():
        adb = str(Path(opts.sdk) / "platform-tools" / "adb")

    def run(*args, check=True, timeout=60):
        proc = subprocess.run([adb, *args], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
        if check and proc.returncode != 0:
            sys.exit(f"adb {' '.join(args)} failed: {proc.stderr}")
        return proc.stdout

    devices = [l.split()[0] for l in run("devices").splitlines()[1:] if l.strip().endswith("device")]
    if len(devices) != 1 or not devices[0].startswith("emulator-"):
        sys.exit(f"exactly one running emulator is required (found: {devices or 'none'}); physical devices are never used here")
    apk = Path(opts.apk)
    if not apk.exists():
        sys.exit(f"{apk} not built")
    out = Path(opts.out)
    out.mkdir(parents=True, exist_ok=True)
    key = Path(opts.key)
    verdict = []

    def check(name, ok, detail=""):
        verdict.append((name, bool(ok), detail))
        print(("PASS " if ok else "FAIL ") + name + (f"  [{detail}]" if detail else ""), flush=True)

    def runas(cmd):
        return run("shell", f"run-as {PACKAGE} sh -c '{cmd}'", check=False)

    def report():
        return run("exec-out", "run-as", PACKAGE, "cat", "files/browser-selftest-report.txt", check=False)

    def command(text):
        run("shell", f"run-as {PACKAGE} sh -c \"echo '{text}' >> files/browser-selftest-cmd.txt\"")

    def wait_for(pattern, since="", timeout=120):
        end = time.time() + timeout
        while time.time() < end:
            text = report()
            tail = text[text.rfind(since):] if since and since in text else text
            m = re.search(pattern, tail, re.M)
            if m:
                return m
            time.sleep(1.5)
        return None

    print("installing", apk, "->", devices[0])
    run("install", "-r", str(apk), timeout=300)
    print(run("shell", "dumpsys webviewupdate | grep -m1 'Current WebView package'").strip())
    uid = re.search(r"uid:(\d+)", run("shell", "pm", "list", "packages", "-U", PACKAGE)).group(1)
    run("reverse", f"tcp:{opts.ssh_port}", f"tcp:{opts.ssh_port}")
    run("shell", "am", "force-stop", PACKAGE)
    run("push", str(key), f"/data/local/tmp/{key.name}")
    cfg = json.dumps({"host": "127.0.0.1", "port": opts.ssh_port, "user": opts.user, "keyFile": "selftest_key", "fingerprint": opts.fingerprint, "base": opts.web_base})
    cfg_local = out / "browser-selftest.json"
    cfg_local.write_text(cfg)
    run("push", str(cfg_local), "/data/local/tmp/browser-selftest.json")
    cfg_local.unlink()
    runas(f"mkdir -p files && cp /data/local/tmp/{key.name} files/selftest_key && chmod 600 files/selftest_key && cp /data/local/tmp/browser-selftest.json files/browser-selftest.json")
    run("shell", f"rm /data/local/tmp/{key.name} /data/local/tmp/browser-selftest.json")
    runas("rm -f files/browser-selftest-report.txt files/browser-selftest-cmd.txt")   # nothing of an earlier run may be mistaken for this one
    sampler = Sampler(adb, uid)
    known_udp = set()

    def new_udp(step):
        """UDP sockets of the uid that appeared since the previous call: tells which step opened them."""
        fresh = sorted(k for k in sampler.snapshot() if k[0].startswith("udp") and k not in known_udp and not (k[3] in ("0.0.0.0", "0000:0000:0000:0000:0000:0000:0000:0000") and k[4] == 0))
        known_udp.update(k for k in sampler.snapshot() if k[0].startswith("udp"))
        if fresh:
            print(f"INFO new UDP sockets after '{step}': " + "; ".join(f"{k[0]} {k[1]}:{k[2]}->{k[3]}:{k[4]}" for k in fresh), flush=True)
    try:
        run("logcat", "-c")
        run("shell", "am", "start", "-n", f"{PACKAGE}/{ACTIVITY}", "--es", "applicationArguments", "--browser-selftest")
        # ── start-up: gateway, then the WebView (the proxy override must be confirmed before any view exists)
        m = wait_for(r"gateway=ready port:(\d+)", timeout=120)
        check("the SSH gateway starts", m)
        gw_port = int(m.group(1)) if m else 0
        new_udp("gateway start")
        started = wait_for(r"webview=(started|refused: .*)", timeout=90)
        check("the WebView starts only after the proxy override is confirmed", started and started.group(1) == "started", started.group(0) if started else "no answer")
        new_udp("webview start")
        js = wait_for(r"js=(OK|TIMEOUT).*", timeout=120)
        time.sleep(4)
        new_udp("javascript page")
        check("a page whose text exists only after JavaScript ran loads through the gateway", js and js.group(1) == "OK", js.group(0) if js else "no answer")
        canary = wait_for(r"^canary=(.*)$", timeout=240)
        time.sleep(4)
        new_udp("canary")
        text = report()
        check("the canary page finished", canary, (canary.group(1)[:300] if canary else "no answer"))
        stats = {k: v for k, v in re.findall(r"^stats\.(\w+)=(.*)$", text, re.M)}
        if "js" in stats:
            check("the gateway counted the JavaScript page (accepted > 0, channels > 0)", re.search(r"accepted:[1-9]", stats["js"]) and re.search(r"channels:[1-9]", stats["js"]), stats["js"])
        if "canary" in stats:
            check("the gateway saw the canary's requests (blocked/internal refused > 0 or accepted grew)", re.search(r"accepted:(\d+)", stats["canary"]) and
                  int(re.search(r"accepted:(\d+)", stats["canary"]).group(1)) > int(re.search(r"accepted:(\d+)", stats.get("js", "accepted:0")).group(1)), stats["canary"])
        check("the listener of the DEVICE (127.0.0.1:<port> asked by the canary) saw no connection", re.search(r"canary\.connections\.canary=0", text) is not None)
        shot = out / "android-browser-canary.png"
        shot.write_bytes(subprocess.run([adb, "exec-out", "screencap", "-p"], capture_output=True).stdout)

        # ── tabs: the real tab pages on the single native WebView (liveLimit 1): the page that goes is released before the next view exists,
        #    showing a tab again loads it again through the gateway, closing frees it; all of it under the same socket sampling
        command("tabs")
        t = wait_for(r"^tabs=(OK|FAIL.*|TIMEOUT.*)$", since="cmd=tabs", timeout=240)
        text = report()
        check("tabs: open two tabs, switch back (reload through the gateway), close one - on the single WebView", t and t.group(1) == "OK", t.group(0)[:200] if t else "no answer")
        check("tabs: one live page at a time (the old WebView page is released first) and the hidden tab keeps its address",
              re.search(r"^tabs\.limits=max:\d+ live:1$", text, re.M) is not None and re.search(r"^tabs\.page2=ok count=2 live=1 limit=1 tabA\.live=0$", text, re.M) is not None
              and re.search(r"^tabs\.reload=ok live=1 tabB\.live=0 tabB\.url=\S+/tab\?n=2$", text, re.M) is not None, "")
        if re.search(r"^tabs\.restored=view$", text, re.M) is None:
            wait_for(r"^tabs\.restored=view$", since="tabs=", timeout=60)
        new_udp("tabs")

        # ── WebRTC: no forced policy exists in the WebView, so UDP would leave around the proxy. The script closes iframe.contentWindow;
        #    window.frames[n] cannot be hooked (known gap, measured LAST so that its sockets do not mask the verdict above).
        command("webrtc")
        w = wait_for(r"^webrtc=(done|TIMEOUT) .*datagrams=(\d+)", since="cmd=webrtc", timeout=120)
        check("WebRTC through iframe.contentWindow of a script-made blank iframe sends no datagram to this device", w and w.group(1) == "done" and w.group(2) == "0", w.group(0)[:200] if w else "no answer")
        before_gap = sampler.snapshot()
        # The residual hole, measured and REPORTED (not a pass criterion): window.frames[n] of the same kind of iframe cannot be hooked.
        command("webrtc-frames")
        g = wait_for(r"^webrtc=(done|TIMEOUT) .*datagrams=(\d+)", since="cmd=webrtc-frames", timeout=120)
        gap_datagrams = int(g.group(2)) if g and g.group(1) == "done" else -1
        print(f"INFO known gap: WebRTC through window.frames[n] of a script-made blank iframe sent {gap_datagrams} datagram(s) to this device", flush=True)

        # ── minimised:        # ── minimised: the foreground service keeps the gateway (and the SSH tunnel) alive
        if gw_port and not opts.quick:
            run("shell", "input", "keyevent", "KEYCODE_HOME")
            time.sleep(3)
            svc = run("shell", f"dumpsys activity services {PACKAGE}", check=False)
            check("GatewayService is a foreground service while the app is minimised", "isForeground=true" in svc or "foregroundServiceType=0x40000000" in svc or "isForeground" in svc and "true" in svc[svc.find("isForeground"):svc.find("isForeground") + 20], "")
            print(f"waiting {opts.background_seconds} s with the app minimised ...", flush=True)
            import socket
            run("forward", f"tcp:{gw_port}", f"tcp:{gw_port}")

            def probe(limit=30):
                started = time.time()
                try:
                    c = socket.create_connection(("127.0.0.1", gw_port), timeout=limit)
                    c.sendall(b"GET http://example.org/ HTTP/1.1\r\nHost: example.org\r\n\r\n")
                    c.settimeout(limit)
                    answer = c.recv(200)
                    c.close()
                except OSError as e:
                    answer = repr(e).encode()
                return answer, time.time() - started

            answers = []
            marks = [5, opts.background_seconds // 2, opts.background_seconds]
            t0 = time.time()
            for mark in marks:
                time.sleep(max(0, mark - (time.time() - t0)))
                a, took = probe()
                answers.append((round(time.time() - t0), a[:24].decode(errors="replace"), round(took, 2)))
            check("the gateway answers (407, no secret) at the start, in the middle and at the end of the minimised wait", all(a.startswith("HTTP/1.1 407") for _, a, _ in answers), str(answers))
            run("forward", "--remove", f"tcp:{gw_port}", check=False)
            run("shell", "am", "start", "-n", f"{PACKAGE}/{ACTIVITY}")
            time.sleep(3)
            command("load /js")
            r = wait_for(r"result for=\S+/js url=\S+ title=(\S*) failure=(.*)", since="cmd=load /js", timeout=90)
            check("after returning, a page loads through the same gateway", r and r.group(1) == "js-done", r.group(0) if r else "no answer")

        # ── lost SSH server
        if opts.wsl_distro and not opts.quick:
            wsl = lambda script: subprocess.run(["wsl", "-d", opts.wsl_distro, "-u", "root", "--", "bash", "-c", script], capture_output=True, text=True, env={**__import__("os").environ, "MSYS_NO_PATHCONV": "1"})
            wsl(f"pkill -f '[s]shd(-session)?: {opts.user}' ; pkill -x sshd-session ; pkill -x sshd; sleep 1; ss -ltn | grep -c ':{opts.ssh_port} ' ; true")   # the user's sessions and the listener ('[s]' keeps pkill from matching this very command)
            time.sleep(2)
            command("load /page2")
            r = wait_for(r"result for=\S+/page2 url=\S+ title=(.*) failure=(.*)", since="cmd=load /page2", timeout=120)
            body = r.group(0) if r else "no answer"
            check("with the SSH server lost the page is NOT shown (unavailable, not direct)", r and "Second" not in r.group(1), body[:200])
            wsl("mkdir -p /run/sshd; /usr/sbin/sshd; true")
            time.sleep(2)

        # ── gateway stopped: the WebView is pointed at a closed port, never at the open network
        command("gateway-stop")
        wait_for(r"gateway=stopped", since="cmd=gateway-stop", timeout=30)
        command("load /page2")
        r = wait_for(r"result for=\S+/page2 url=\S+ title=(.*) failure=(.*)", since="cmd=gateway-stop", timeout=90)
        check("with the gateway stopped the WebView cannot load (proxy failure, fail closed)", r and "Second" not in r.group(1) and r.group(2).strip() != "", r.group(0)[:200] if r else "no answer")
        time.sleep(3)
    finally:
        sampler.stop()
        command("quit")
        time.sleep(2)
        final = report()
        (out / "android-browser-report.txt").write_text(final, encoding="utf-8")
        runas("rm -f files/selftest_key files/browser-selftest.json files/browser-selftest-cmd.txt")
        run("reverse", "--remove", f"tcp:{opts.ssh_port}", check=False)
        log = run("logcat", "-d", "-s", "chromium:E", "FORTIQ-SELFTEST:I", check=False)
        (out / "android-browser-logcat.txt").write_text(log, encoding="utf-8")

    # ── the verdict of the socket tables (everything up to the known-gap measurement, which has its own line)
    tolerated, violations, udp, udp_gap = [], [], [], []
    for (proto, lip, lport, rip, rport, st), n in sorted(sampler.seen.items()):
        line = f"{proto} {lip}:{lport} -> {rip}:{rport} {st} (x{n})"
        if proto.startswith("udp"):
            if (proto, lip, lport, rip, rport, st) in before_gap:
                udp.append(line)
            else:
                udp_gap.append(line)
            continue
        if st == "LISTEN" or st == "CLOSE":
            tolerated.append(line)
        elif is_loopback(lip) and is_loopback(rip) and (rport in (gw_port, opts.ssh_port) or lport in (gw_port, opts.ssh_port)):
            tolerated.append(line)
        else:
            violations.append(line)
    check("no TCP connection of the app (all processes of its uid) other than loopback -> gateway / SSH", not violations, "; ".join(violations[:6]))
    # UDP: unconnected listeners (the self-test's own canary socket) are fine. A CONNECTED UDP socket to the system resolver (port 53) or to
    # loopback is a connect()-only probe of Chromium's network stack (reachability / resolver selection): it sends nothing, which the packet
    # capture confirms when --pcap is given. Anything else is a violation.
    def listener(l):
        return " 0.0.0.0:0 CLOSE" in l or "0000:0000:0000:0000:0000:0000:0000:0000:0 CLOSE" in l
    def probe(l):
        return bool(re.search(r"-> (10\.0\.2\.3|127\.0\.0\.1):(53|443) ESTABLISHED", l)) or bool(re.search(r"-> fec0:0000:0000:0000:0000:0000:0000:0003:53 ESTABLISHED", l))
    udp_probes = [l for l in udp if probe(l)]
    udp_unexpected = [l for l in udp if not listener(l) and not probe(l)]
    check("no UDP socket of the app's uid to anywhere but the system resolver / loopback (connect-only probes), before the known-gap step", not udp_unexpected, "; ".join(udp_unexpected[:6]))
    dns_names = []
    if opts.pcap:
        dns_names = dns_questions(opts.pcap)
        used = [n for n, _ in dns_names if n.endswith(".invalid") or "canary" in n or "stun" in n or n.endswith("example.org") or "fixture" in n]
        print("INFO DNS questions on the emulator network during the run (system and Google services included): " + ", ".join(sorted({n for n, _ in dns_names})), flush=True)
        check("no DNS question for any name the test pages used (canary, STUN, fixture) appeared on the network", not used, ", ".join(used[:6]))
    elif udp_probes:
        print(f"WARN {len(udp_probes)} connect-only UDP probe socket(s) of the app were seen; without --pcap it is not proven that they sent nothing", flush=True)
    check("the sampler really saw the app's sockets (positive control)", any(":%d" % gw_port in l for l in tolerated) and sampler.lines > 0, f"{len(sampler.seen)} distinct sockets, {sampler.lines} lines")
    summary = ["# Android Browser check", "", f"sockets tolerated ({len(tolerated)}):", *("  " + l for l in tolerated), "", f"violations ({len(violations)}):", *("  " + l for l in violations),
               "", f"udp before the known-gap step ({len(udp)}; {len(udp_probes)} connect-only probes):", *("  " + l for l in udp), "", f"udp appearing during/after the known-gap step ({len(udp_gap)}):", *("  " + l for l in udp_gap), "", f"dns questions in the capture: {sorted({n for n, _ in dns_names})}", "", "verdict:", *(f"  {'PASS' if ok else 'FAIL'} {n} [{d}]" for n, ok, d in verdict)]
    (out / "android-browser-sockets.txt").write_text("\n".join(summary), encoding="utf-8")
    print("report:", out / "android-browser-report.txt", "| sockets:", out / "android-browser-sockets.txt", "| screenshot:", out / "android-browser-canary.png")
    sys.exit(0 if all(ok for _, ok, _ in verdict) else 1)


if __name__ == "__main__":
    main()
