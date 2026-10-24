#!/usr/bin/env python3
"""Installs the debug APK on a RUNNING Android emulator, forwards the fixture's SSH port and launches the app.

Repeatable part of the platform check in docs/PLATFORM_QUALIFICATION.md. It never starts or touches a physical device or
a virtual machine: it refuses to run unless exactly one device is attached and its serial starts with "emulator-".
Taps and typing stay manual (coordinates depend on the AVD); the script saves screenshots to <out>.

Usage:
  python tool/android_emulator_smoke.py --sdk C:/Users/<you>/AppData/Local/Android/Sdk [--key path/to/throwaway_key] [--ssh-port 2222] [--out build-dist]
--key  a THROWAWAY private key for the disposable fixture (authorized_keys on the fixture, removed afterwards by you);
       it is copied into the app's private files directory and appears in the form as /data/user/0/fr.cybou.fortiq/files/<name>.
"""
import argparse, subprocess, sys, time
from pathlib import Path

PACKAGE = "fr.cybou.fortiq"
ACTIVITY = "org.qtproject.qt.android.bindings.QtActivity"
APK = "build-android-arm64/android-build/build/outputs/apk/debug/android-build-debug.apk"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--apk", default=APK)
    ap.add_argument("--key", default="")
    ap.add_argument("--ssh-port", type=int, default=2222)
    ap.add_argument("--out", default="build-dist")
    ap.add_argument("--wait", type=int, default=50, help="seconds to wait for the first frame (arm64 translation is slow)")
    opts = ap.parse_args()

    adb = str(Path(opts.sdk) / "platform-tools" / "adb.exe")
    if not Path(adb).exists():
        adb = str(Path(opts.sdk) / "platform-tools" / "adb")

    def run(*args, check=True, binary=False):
        proc = subprocess.run([adb, *args], capture_output=True, text=not binary)
        if check and proc.returncode != 0:
            sys.exit(f"adb {' '.join(args)} failed: {proc.stderr if not binary else proc.stderr.decode(errors='replace')}")
        return proc.stdout

    devices = [l.split()[0] for l in run("devices").splitlines()[1:] if l.strip().endswith("device")]
    if len(devices) != 1 or not devices[0].startswith("emulator-"):
        sys.exit(f"exactly one running emulator is required (found: {devices or 'none'}); physical devices are never used here")
    apk = Path(opts.apk)
    if not apk.exists():
        sys.exit(f"{apk} not built (tool/build_android.py --package)")

    print("installing", apk, "->", devices[0])
    print(run("install", "-r", str(apk)).strip().splitlines()[-1])
    run("reverse", f"tcp:{opts.ssh_port}", f"tcp:{opts.ssh_port}")
    if opts.key:
        key = Path(opts.key)
        run("push", str(key), f"/data/local/tmp/{key.name}")
        run("shell", f"run-as {PACKAGE} sh -c 'mkdir -p files && cp /data/local/tmp/{key.name} files/{key.name} && chmod 600 files/{key.name}'")
        run("shell", f"rm /data/local/tmp/{key.name}")
        print(f"key available to the app as /data/user/0/{PACKAGE}/files/{key.name}")
    run("shell", "am", "force-stop", PACKAGE)
    run("shell", "am", "start", "-n", f"{PACKAGE}/{ACTIVITY}")
    time.sleep(opts.wait)
    out = Path(opts.out); out.mkdir(parents=True, exist_ok=True)
    shot = out / "android-smoke.png"
    shot.write_bytes(run("exec-out", "screencap", "-p", binary=True))
    pid = run("shell", "pidof", PACKAGE, check=False).strip()
    print(f"app process: {pid or 'NOT RUNNING'}; screenshot: {shot}")
    sys.exit(0 if pid else 1)


if __name__ == "__main__":
    main()
