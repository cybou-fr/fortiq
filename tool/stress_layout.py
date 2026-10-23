#!/usr/bin/env python3
"""Runs the layout test repeatedly while every CPU core is kept busy, and reports which checks failed.

The layout test drives a real window (offscreen) and used to fail on a loaded machine because it waited fixed
times instead of for conditions (audit R6). This script is the reproducible check that it no longer does.

Usage (after a build in build-msvc):  python tool/stress_layout.py --build build-msvc --runs 3 [--load 1.0]
  --load  busy processes per core (1.0 = one busy loop per core, as a running game or build; 2.0 was too slow to finish)
Exit status 0 only if every run passed.
"""
import argparse, os, re, subprocess, sys, time
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build-msvc")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--load", type=float, default=1.0)
    ap.add_argument("--qt-bin", default="C:/Qt/6.12.0/msvc2022_64/bin")
    ap.add_argument("--vcpkg-bin", default="C:/Users/cybou/vcpkg/installed/x64-windows/bin")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--timeout", type=int, default=1200)
    opts = ap.parse_args()

    build = Path(opts.build).resolve()
    if not (build / "CTestTestfile.cmake").exists():
        sys.exit(f"{build} is not a configured build directory")
    # ctest supplies the exact environment of the normal check (offscreen plugin path, software backend, style)
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([opts.qt_bin, opts.vcpkg_bin, env.get("PATH", "")])
    env.setdefault("QT_QPA_FONTDIR", os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts"))

    busy = max(1, int((os.cpu_count() or 4) * opts.load))
    hogs = [subprocess.Popen([sys.executable, "-c", "while True: pass"]) for _ in range(busy)]
    print(f"{busy} busy processes on {os.cpu_count()} cores", flush=True)
    failed_runs = 0
    try:
        for n in range(1, opts.runs + 1):
            started = time.time()
            try:
                proc = subprocess.run(["ctest", "--test-dir", str(build), "-C", opts.config, "-R", "fortiq_layout", "--output-on-failure"],
                                      env=env, capture_output=True, text=True, errors="replace", timeout=opts.timeout)
                rc, text = proc.returncode, proc.stdout + proc.stderr
            except subprocess.TimeoutExpired:
                rc, text = -1, "timeout"
            totals = next((l.strip() for l in text.splitlines() if l.startswith("Totals:")), "no totals")
            fails = [l for l in text.splitlines() if l.startswith("FAIL!")]
            print(f"run {n}: {'PASS' if rc == 0 and not fails else 'FAIL'} in {time.time() - started:.0f}s - {totals}", flush=True)
            for f in fails:
                print("   ", re.sub(r"\s+", " ", f)[:220])
            failed_runs += 0 if rc == 0 and not fails else 1
    finally:
        for h in hogs:
            h.kill()
    sys.exit(1 if failed_runs else 0)


if __name__ == "__main__":
    main()
