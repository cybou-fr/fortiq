#!/usr/bin/env python3
"""Performance baseline (docs/PERFORMANCE_BASELINE.md): prepares the DISPOSABLE WSL server and runs build-msvc/fortiq_perf.

  python tool/run_perf_baseline.py [--soak-minutes 10] [--sections terminal ssh files parsers] [--distro fortiq-test] [--user fqshell]

It creates a directory with 50 000 files on the throw-away distro, a throw-away SSH key for this run, runs the measurements and
ALWAYS removes the key and the directory afterwards. Output: markdown table rows (paste them into the baseline document with the date).
"""
import argparse, os, subprocess, sys, tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_live_fixture as fx   # wsl(), MARK: the same helpers and key handling as the live fixture

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--distro", default="fortiq-test")
    ap.add_argument("--user", default="fqshell")
    ap.add_argument("--ssh-port", default="2222")
    ap.add_argument("--build", default="build-msvc")
    ap.add_argument("--soak-minutes", type=int, default=0)
    ap.add_argument("--entries", type=int, default=50000)
    ap.add_argument("--sections", nargs="*", default=["terminal", "parsers", "ssh", "files"])
    opts = ap.parse_args()
    d, u = opts.distro, opts.user
    exe = ROOT / opts.build / "fortiq_perf.exe"
    if not exe.exists():
        sys.exit(f"{exe} is not built (cmake --build {opts.build} --target fortiq_perf)")

    keep = subprocess.Popen(["wsl", "-d", d, "-u", "root", "--", "sleep", "86400"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    work = Path(tempfile.mkdtemp(prefix="fortiq-perf-"))
    perf_dir = f"/home/{u}/perf{opts.entries}"
    try:
        fx.wsl(d, "mkdir -p /run/sshd; ss -ltn | grep -q ':%s ' || /usr/sbin/sshd" % opts.ssh_port)
        fx.wsl(d, "for i in 1 2 3 4 5; do ss -ltn | grep -q ':%s ' && exit 0; sleep 1; done; exit 1" % opts.ssh_port)
        key = work / "perf_key"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", fx.MARK, "-f", str(key)], check=True, stdin=subprocess.DEVNULL, timeout=60)
        pub = (work / "perf_key.pub").read_text().strip()
        fx.wsl(d, f"""set -e
install -d -m 700 -o {u} -g {u} /home/{u}/.ssh
touch /home/{u}/.ssh/authorized_keys
echo '{pub}' >> /home/{u}/.ssh/authorized_keys
chown {u}:{u} /home/{u}/.ssh/authorized_keys; chmod 600 /home/{u}/.ssh/authorized_keys
rm -rf {perf_dir}; mkdir -p {perf_dir}
cd {perf_dir}
python3 - <<'PY'
import os
for i in range({opts.entries}):
    open(f"file-{{i:06d}}-" + "x" * (i % 40) + ".txt", "w").close()
PY
ls {perf_dir} | wc -l""")
        fp = fx.wsl(d, "ssh-keygen -l -E sha256 -f /etc/ssh/ssh_host_ecdsa_key.pub").split()[1]
        env = {**os.environ, "FORTIQ_IT_HOST": "127.0.0.1", "FORTIQ_IT_PORT": opts.ssh_port, "FORTIQ_IT_USER": u, "FORTIQ_IT_KEY": str(key),
               "FORTIQ_IT_FINGERPRINT": fp, "FORTIQ_PERF_DIR": perf_dir,
               "PATH": os.pathsep.join(["C:/Qt/6.12.0/msvc2022_64/bin", "C:/Users/cybou/vcpkg/installed/x64-windows/bin", os.environ.get("PATH", "")])}
        args = list(opts.sections) + ([f"soak={opts.soak_minutes}"] if opts.soak_minutes else [])
        proc = subprocess.run([str(exe), *args], env=env, cwd=exe.parent)
        sys.exit(proc.returncode)
    finally:
        fx.wsl(d, f"sed -i '/{fx.MARK}/d' /home/{u}/.ssh/authorized_keys; rm -rf {perf_dir}", check=False)
        keep.terminate()


if __name__ == "__main__":
    main()
