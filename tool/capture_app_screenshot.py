#!/usr/bin/env python3
"""A REAL screenshot of the running FORTIQ application on Windows, for the website and the docs.

  python tool/capture_app_screenshot.py [--out www/assets/fortiq-app-1536.png] [--distro fortiq-test] [--user fqshell] [--ssh-port 2222] [--build build-msvc]

What happens (nothing here is a fixture or a mock-up):
  1. a tidy demo folder is created in the home directory of the user on the DISPOSABLE test server (the WSL distro the live tests use);
  2. a throw-away key pair is made, authorized for the run and removed afterwards (always, even after a failure);
  3. build-msvc/fortiq_live_capture.exe starts the production QML shell with the production controllers, connects over SSH to that
     server (host key pinned in its own settings scope, not the user's), opens Files and Terminal and grabs the window at 1536x1024, DPR 1;
  4. the demo folder and the key are removed; the user's real ~/.bash_history is set aside during the run (the Terminal window lists it) and restored.
The picture therefore shows what the application really draws from what a real server really returned. Run it on a Windows machine
with a build (fortiq_live_capture is built with the tests); never on a real server.
"""
import argparse, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MARK = "fortiq-capture-run"
DEMO = "Documents-demo"


def wsl(distro, script, check=True):
    proc = subprocess.run(["wsl", "-d", distro, "-u", "root", "--", "bash", "-s"], input=script.replace("\r\n", "\n").encode("utf-8"),
                          capture_output=True, env={**os.environ, "MSYS_NO_PATHCONV": "1"})
    out = proc.stdout.decode("utf-8", "replace")
    if check and proc.returncode != 0:
        raise RuntimeError(f"in {distro}: exit {proc.returncode}\n{out}\n{proc.stderr.decode('utf-8', 'replace')}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "www/assets/fortiq-app-1536.png"))
    ap.add_argument("--distro", default="fortiq-test")
    ap.add_argument("--user", default="fqshell")
    ap.add_argument("--ssh-port", default="2222")
    ap.add_argument("--build", default="build-msvc")
    ap.add_argument("--wallpaper", default="aurora")
    ap.add_argument("--browse", default="", help="open the Browser app on this address instead of Files + Terminal (diagnostics, docs)")
    opts = ap.parse_args()
    d, u = opts.distro, opts.user
    home = f"/home/{u}"
    demo = f"{home}/{DEMO}"
    work = Path(tempfile.mkdtemp(prefix="fortiq-capture-"))
    keep = subprocess.Popen(["wsl", "-d", d, "-u", "root", "--", "sleep", "600"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1)
        wsl(d, "mkdir -p /run/sshd; ss -ltn | grep -q ':%s ' || /usr/sbin/sshd" % opts.ssh_port)
        key = work / "capture_key"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", MARK, "-f", str(key)], check=True, stdin=subprocess.DEVNULL, timeout=60)
        pub = (work / "capture_key.pub").read_text().strip()
        wsl(d, f"""set -e
install -d -m 700 -o {u} -g {u} {home}/.ssh
touch {home}/.ssh/authorized_keys
sed -i '/{MARK}/d' {home}/.ssh/authorized_keys
echo '{pub}' >> {home}/.ssh/authorized_keys
chown {u}:{u} {home}/.ssh/authorized_keys; chmod 600 {home}/.ssh/authorized_keys
[ -f {home}/.bash_history.capture-bak ] || {{ if [ -f {home}/.bash_history ]; then mv {home}/.bash_history {home}/.bash_history.capture-bak; else touch {home}/.bash_history.capture-none; fi; }}
printf 'ls -la\ndf -h\nfree -m\nsystemctl status nginx\njournalctl -u nginx -n 50 --no-pager\ntail -f /var/log/syslog\n' > {home}/.bash_history
chown {u}:{u} {home}/.bash_history; chmod 600 {home}/.bash_history
rm -rf {demo}
install -d -o {u} -g {u} {demo}
cd {demo}
for dir in Projets Photos Musique Factures Archives Notes; do install -d -o {u} -g {u} "$dir"; done
printf '# Notes de la semaine\\n\\n- Mettre a jour le serveur\\n- Verifier les sauvegardes\\n' > notes.md
printf 'Rapport mensuel\\n' > rapport-octobre.txt
head -c 184320 /dev/urandom > sauvegarde-2026-10.tar.gz
head -c 2411724 /dev/urandom > presentation.pdf
head -c 38912 /dev/urandom > budget-2026.csv
head -c 922112 /dev/urandom > photo-lac.jpg
chown -R {u}:{u} {demo}
touch -d '2026-10-24 09:12' notes.md; touch -d '2026-10-22 17:40' rapport-octobre.txt; touch -d '2026-10-20 08:05' sauvegarde-2026-10.tar.gz
touch -d '2026-10-18 14:30' presentation.pdf budget-2026.csv; touch -d '2026-10-12 11:00' photo-lac.jpg""")
        fp = wsl(d, "ssh-keygen -l -E sha256 -f /etc/ssh/ssh_host_ecdsa_key.pub").split()[1]
        out = Path(opts.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        exe = ROOT / opts.build / "fortiq_live_capture.exe"
        env = {**os.environ, "PATH": "C:/Qt/6.12.0/msvc2022_64/bin;C:/Users/cybou/vcpkg/installed/x64-windows/bin;" + os.environ["PATH"]}
        rc = subprocess.run([str(exe), "--host", "127.0.0.1", "--port", opts.ssh_port, "--user", u, "--fingerprint", fp, "--key", str(key),
                             "--demo-dir", demo, "--out", str(out), "--wallpaper", opts.wallpaper] + (["--browse", opts.browse] if opts.browse else []), env=env, cwd=exe.parent, timeout=180).returncode
        if rc != 0:
            sys.exit(f"capture failed (exit {rc})")
        print("saved", out)
    finally:
        wsl(d, f"""sed -i '/{MARK}/d' {home}/.ssh/authorized_keys 2>/dev/null; rm -rf {demo}
if [ -f {home}/.bash_history.capture-bak ]; then mv -f {home}/.bash_history.capture-bak {home}/.bash_history; elif [ -f {home}/.bash_history.capture-none ]; then rm -f {home}/.bash_history {home}/.bash_history.capture-none; fi
true""", check=False)
        shutil.rmtree(work, ignore_errors=True)
        keep.terminate()


if __name__ == "__main__":
    main()
