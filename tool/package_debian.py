#!/usr/bin/env python3
"""Build a native Debian 13 .deb for the FORTIQ desktop client.

Requires Debian 13, Qt >= 6.8 dev packages and CMake/CPack. Does not
cross-compile or install anything on the host. Tests run before packaging.
Production publishing additionally requires a security review of Debian's
libssh2 binary (or a verified patched build) and device acceptance.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def run(*cmd: str) -> None:
    print("+", " ".join(str(x) for x in cmd), flush=True)
    subprocess.run([str(x) for x in cmd], cwd=ROOT, check=True)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default="build-debian")
    parser.add_argument("--skip-tests", action="store_true", help="development only; package is not release-qualified")
    parser.add_argument("--qt-prefix", default="", help="optional Qt 6.12 installation; Debian system Qt >=6.8 also works")
    args = parser.parse_args()
    if platform.system() != "Linux" or not Path("/etc/debian_version").exists():
        parser.error("run inside Debian (a VM is fine)")
    for tool in ("cmake", "cpack", "dpkg-deb"):
        if not shutil.which(tool):
            parser.error(f"missing {tool}")
    build = ROOT / args.build
    config = ["cmake", "-S", ".", "-B", args.build, "-DCMAKE_BUILD_TYPE=Release",
              "-DBUILD_TESTING=ON", "-DFORTIQ_DEB=ON"]
    if args.qt_prefix:
        config.append(f"-DCMAKE_PREFIX_PATH={args.qt_prefix}")
    if shutil.which("ninja"):
        config += ["-G", "Ninja"]
    run(*config)
    run("cmake", "--build", args.build, "--parallel", str(min(os.cpu_count() or 2, 8)))
    if not args.skip_tests:
        run("ctest", "--test-dir", args.build, "--output-on-failure", "--no-tests=error")
    run("cpack", "--config", str(build / "CPackConfig.cmake"), "-G", "DEB", "-B", str(build))
    packages = sorted(build.glob("fortiq_*.deb"))
    if not packages:
        raise RuntimeError(f"CPack produced no .deb in {build}")
    for package in packages:
        run("dpkg-deb", "--info", str(package))
        run("dpkg-deb", "--contents", str(package))
        print("PACKAGE:", package, flush=True)
    if args.skip_tests:
        print("WARNING: Tests skipped; not qualified for distribution.", file=sys.stderr)
    print("SECURITY HOLD: Verify the linked libssh2 package contains relevant security fixes "
          "before distributing the .deb.", file=sys.stderr)
    return 0

if __name__ == "__main__":
    sys.exit(main())
