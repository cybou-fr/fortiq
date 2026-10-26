#!/usr/bin/env python3
"""Proves that the vcpkg tree a build uses holds the SAME libssh2 and OpenSSL as FORTIQ's pinned overlay ports.

third_party/vcpkg-overlay-ports/<port>/vcpkg.json is the single source of truth (libssh2: a master commit with the packet-length fixes,
OpenSSL: 3.6.5). A build that picked the stock vcpkg ports instead (for example `vcpkg install libssh2:x64-windows` without
--overlay-ports) would silently ship older, vulnerable code; this check fails then.

  python tool/check_pins.py --vcpkg-root C:/Users/me/vcpkg [--triplet x64-windows]
  python tool/check_pins.py --installed <dir that holds vcpkg/status> [--triplet arm64-android]
  python tool/check_pins.py --self-test
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OVERLAY = ROOT / "third_party" / "vcpkg-overlay-ports"
PINNED_PORTS = ("libssh2", "openssl")


def expected(overlay: Path = OVERLAY) -> dict[str, str]:
    """port -> 'version' or 'version#port-version' as vcpkg's status file writes it."""
    result = {}
    for port in PINNED_PORTS:
        manifest = json.loads((overlay / port / "vcpkg.json").read_text(encoding="utf-8"))
        version = manifest.get("version") or manifest.get("version-string") or manifest.get("version-semver")
        if not version:
            raise SystemExit(f"{overlay / port / 'vcpkg.json'} has no version")
        port_version = manifest.get("port-version", 0)
        result[port] = version + (f"#{port_version}" if port_version else "")
    return result


def installed_versions(installed: Path, triplet: str) -> dict[str, str]:
    status = installed / "vcpkg" / "status"
    if not status.is_file():
        raise SystemExit(f"{status} not found: nothing is installed there")
    found: dict[str, str] = {}
    for block in status.read_text(encoding="utf-8", errors="replace").split("\n\n"):
        fields = dict(re.findall(r"^([A-Za-z-]+): (.*)$", block, re.M))
        if fields.get("Architecture") != triplet or "Feature" in fields or fields.get("Status") != "install ok installed":
            continue
        port_version = fields.get("Port-Version")
        found[fields.get("Package", "")] = fields.get("Version", "") + (f"#{port_version}" if port_version and port_version != "0" else "")
    return found


def compare(want: dict[str, str], have: dict[str, str]) -> list[str]:
    problems = []
    for port, version in want.items():
        if port not in have:
            problems.append(f"{port}: not installed (pinned: {version})")
        elif have[port] != version:
            problems.append(f"{port}: installed {have[port]}, pinned {version} - the build did not use the overlay port")
    return problems


def self_test() -> int:
    def status(*records: tuple[str, str, str, str]) -> str:
        return "\n".join(f"Package: {n}\nVersion: {v}\n" + (f"Port-Version: {p}\n" if p else "") + f"Architecture: {a}\nStatus: install ok installed\n" for n, v, p, a in records)

    want = {"libssh2": "1.11.1-git6e7b207", "openssl": "3.6.5#2"}
    cases = [
        ("pinned versions installed", status(("libssh2", "1.11.1-git6e7b207", "", "x64-windows"), ("openssl", "3.6.5", "2", "x64-windows")), True),
        ("stock libssh2 1.11.1", status(("libssh2", "1.11.1", "3", "x64-windows"), ("openssl", "3.6.5", "2", "x64-windows")), False),
        ("older OpenSSL", status(("libssh2", "1.11.1-git6e7b207", "", "x64-windows"), ("openssl", "3.6.3", "", "x64-windows")), False),
        ("OpenSSL port revision differs", status(("libssh2", "1.11.1-git6e7b207", "", "x64-windows"), ("openssl", "3.6.5", "1", "x64-windows")), False),
        ("pins only installed for another triplet", status(("libssh2", "1.11.1-git6e7b207", "", "arm64-android"), ("openssl", "3.6.5", "2", "arm64-android")), False),
        ("nothing installed", status(), False),
    ]
    bad = []
    for label, text, ok in cases:
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "vcpkg").mkdir()
            (Path(tmp) / "vcpkg" / "status").write_text(text, encoding="utf-8")
            problems = compare(want, installed_versions(Path(tmp), "x64-windows"))
        if (not problems) != ok:
            bad.append(f"{label}: expected {'pass' if ok else 'failure'}, problems={problems}")
    if bad:
        print("SELF-TEST FAILED:\n" + "\n".join(bad))
        return 1
    print("SELF-TEST OK: the pinned versions pass; stock ports, older OpenSSL, a different port revision, another triplet and an empty tree are refused.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vcpkg-root")
    ap.add_argument("--installed")
    ap.add_argument("--triplet", default="x64-windows")
    ap.add_argument("--self-test", action="store_true")
    opts = ap.parse_args()
    if opts.self_test:
        return self_test()
    if bool(opts.vcpkg_root) == bool(opts.installed):
        ap.error("exactly one of --vcpkg-root and --installed")
    installed = Path(opts.installed) if opts.installed else Path(opts.vcpkg_root) / "installed"
    want, have = expected(), installed_versions(installed, opts.triplet)
    problems = compare(want, have)
    print("pinned:   " + ", ".join(f"{p} {v}" for p, v in want.items()))
    print(f"installed ({opts.triplet}): " + ", ".join(f"{p} {have.get(p, '-')}" for p in want))
    if problems:
        print("\nDEPENDENCY PIN PROBLEMS:")
        for p in problems:
            print(" -", p)
        return 1
    print("OK: the installed libssh2 and OpenSSL are the pinned overlay ports.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
