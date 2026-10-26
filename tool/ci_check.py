#!/usr/bin/env python3
"""The gate every commit to main must pass, runnable by hand and by CI (.github/workflows/ci.yml).

Steps (each prints its own result; the script stops at the first failure):
  1. documentation consistency   tool/check_docs_consistency.py
  2. the gate's own tools prove themselves (licence check, dependency pins, test-result reader, SBOM) and every tool script compiles
  3. configure + build            cmake (Ninja when available) with the given Qt prefix and, if given, the vcpkg toolchain
  4. the offline test suites      ctest --output-on-failure   (unit tests + layout test; live tests are NOT part of it:
                                  they need the disposable Linux fixture, see tool/run_live_tests.py), then tool/check_test_results.py:
                                  a skipped test without a waiver (tool/gate_waivers.json), a failure or a crashed program fails the gate
                                  although ctest calls a skipping test "Passed"
  5. optional --stress N          the layout test N times under CPU load (tool/stress_layout.py)

A skipped or missing step is never reported as passed. Usage:
  python tool/ci_check.py --qt-prefix C:/Qt/6.12.0/msvc2022_64 --toolchain C:/vcpkg/scripts/buildsystems/vcpkg.cmake [--build build-ci] [--stress 1]
"""
import argparse, glob, os, shutil, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(title, cmd, cwd=ROOT, env=None):
    started = time.time()
    print(f"\n=== {title}\n$ {' '.join(str(c) for c in cmd)}", flush=True)
    proc = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env)
    ok = proc.returncode == 0
    print(f"--- {title}: {'OK' if ok else 'FAILED (exit %d)' % proc.returncode} in {time.time() - started:.0f}s", flush=True)
    if not ok:
        sys.exit(proc.returncode or 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build-ci")
    ap.add_argument("--qt-prefix", default=os.environ.get("QT_ROOT_DIR", ""), help="Qt kit (CMAKE_PREFIX_PATH)")
    ap.add_argument("--toolchain", default="", help="vcpkg toolchain file (libssh2)")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--triplet", default="x64-windows", help="vcpkg triplet whose pins are checked")
    ap.add_argument("--stress", type=int, default=0, help="also run the layout test N times under CPU load")
    ap.add_argument("--skip-build", action="store_true", help="reuse an existing build directory (tests only)")
    opts = ap.parse_args()

    run("documentation consistency", [sys.executable, "tool/check_docs_consistency.py"])
    run("licence check of the packages proves itself (tool/check_third_party.py)", [sys.executable, "tool/check_third_party.py", "--self-test"])
    run("dependency pin check proves itself (tool/check_pins.py)", [sys.executable, "tool/check_pins.py", "--self-test"])
    run("test-result reader proves itself (tool/check_test_results.py)", [sys.executable, "tool/check_test_results.py", "--self-test"])
    run("SBOM tool proves itself (tool/make_sbom.py)", [sys.executable, "tool/make_sbom.py", "--self-test"])
    scripts = sorted(glob.glob(str(ROOT / "tool" / "*.py")) + glob.glob(str(ROOT / "tests" / "fixtures" / "**" / "*.py"), recursive=True))
    run("tool and fixture scripts compile", [sys.executable, "-m", "py_compile", *scripts])

    if opts.toolchain:   # the build must use FORTIQ's pinned libssh2/OpenSSL (overlay ports), never the stock vcpkg ones
        vcpkg_root = Path(opts.toolchain).resolve().parents[2]
        run("libssh2 and OpenSSL are the pinned overlay ports", [sys.executable, "tool/check_pins.py", "--vcpkg-root", vcpkg_root, "--triplet", opts.triplet])

    build = ROOT / opts.build
    if not opts.skip_build:
        if not opts.qt_prefix:
            sys.exit("--qt-prefix (or QT_ROOT_DIR) is required to configure the build")
        configure = ["cmake", "-S", ".", "-B", opts.build, f"-DCMAKE_PREFIX_PATH={opts.qt_prefix}", "-DBUILD_TESTING=ON", f"-DCMAKE_BUILD_TYPE={opts.config}"]
        if opts.toolchain:
            configure.append(f"-DCMAKE_TOOLCHAIN_FILE={opts.toolchain}")
        if shutil.which("ninja"):
            configure[1:1] = ["-G", "Ninja"]
        run("configure", configure)
        run("build", ["cmake", "--build", opts.build, "--config", opts.config, "--parallel"])
    elif not build.exists():
        sys.exit(f"{build} does not exist")

    for stale in build.rglob("*results*.txt"):   # only the result files of THIS run may be judged
        stale.unlink()
    run("offline test suites", ["ctest", "--test-dir", opts.build, "-C", opts.config, "--output-on-failure", "--no-tests=error"])
    run("no skipped, crashed or failed test hides behind ctest's verdict", [sys.executable, "tool/check_test_results.py", "--results-dir", build, "--scope", "offline", "--min-files", "10"])
    if opts.stress > 0:
        run(f"layout test x{opts.stress} under CPU load", [sys.executable, "tool/stress_layout.py", "--build", opts.build, "--runs", str(opts.stress)])
    print("\nALL STEPS PASSED (live tests against a Linux fixture were not run here: this is the OFFLINE gate, not the release gate)")


if __name__ == "__main__":
    main()
