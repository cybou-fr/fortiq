#!/usr/bin/env python3
"""Check distributable runtime dependencies for GPL-3.0-only FORTIQ.

Qt modules with GPLv3 or LGPLv3 grants are permitted, while unknown shared
libraries and missing required notices are rejected. This heuristic examines
Qt header SPDX identifiers and package file names; it is not legal advice.
"""
from __future__ import annotations

import argparse
import re
import sys
import zipfile
from pathlib import Path

DEFAULT_QT = Path("C:/Qt/6.12.0/msvc2022_64")

# Non-Qt components that may ship, with the licence text file expected under <stage>/licenses (None: no file needed).
KNOWN_COMPONENTS = {
    "libssh2.dll": ("libssh2.txt", "BSD-3-Clause"),
    "libcrypto-3-x64.dll": ("openssl.txt", "Apache-2.0"),
    "libssl-3-x64.dll": ("openssl.txt", "Apache-2.0"),
    "z.dll": ("zlib.txt", "Zlib"),
    "zlib1.dll": ("zlib.txt", "Zlib"),
    "d3dcompiler_47.dll": (None, "Microsoft redistributable (DirectX)"),
    "opengl32sw.dll": (None, "Mesa llvmpipe (MIT) - see Qt notices"),
}
MSVC_RUNTIME = re.compile(r"^(vcruntime140.*|msvcp140.*|concrt140|vccorlib140|vcomp140)\.dll$", re.I)


def module_licences(qt_root: Path) -> dict[str, tuple[int, int]]:
    """Qt module -> (headers with SPDX, headers offering GPLv3 or LGPLv3)."""
    result: dict[str, tuple[int, int]] = {}
    include = qt_root / "include"
    for d in sorted(include.iterdir()):
        if not d.is_dir() or not d.name.startswith("Qt"):
            continue
        total = compatible = 0
        for h in d.glob("*.h"):
            head = h.read_text(encoding="utf-8", errors="replace")[:4000]
            m = re.search(r"SPDX-License-Identifier:\s*(.*)", head)
            if m:
                total += 1
                compatible += ("LGPL-3.0" in m.group(1) or "GPL-3.0" in m.group(1))
        if total:
            result[d.name] = (total, compatible)
    return result


def not_gpl_compatible(licences: dict[str, tuple[int, int]]) -> set[str]:
    # A module is flagged when any header lacks an LGPL alternative (one stray header is enough: it is a different component).
    return {name for name, (total, compatible) in licences.items() if compatible < total}


def qt_module_of_library(file_name: str) -> str | None:
    m = re.match(r"^(?:lib)?Qt6(.+?)(?:_[a-z0-9_-]+)?\.(?:dll|so)$", file_name, re.I)
    return "Qt" + m.group(1) if m else None


def self_test(qt: Path) -> int:
    import subprocess
    import tempfile
    me = Path(__file__).resolve()

    def run(stage: Path) -> tuple[int, str]:
        r = subprocess.run([sys.executable, str(me), "--stage", str(stage), "--qt", str(qt)], capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    with tempfile.TemporaryDirectory() as tmp:
        clean = Path(tmp) / "clean"
        (clean / "licenses").mkdir(parents=True)
        for name in ("THIRD_PARTY_NOTICES.txt", "Qt-COPYING.txt", "libssh2.txt"):
            (clean / "licenses" / name).write_text("x", encoding="utf-8")
        for name in ("fortiq.exe", "Qt6Core.dll", "Qt6Quick.dll", "libssh2.dll", "vcruntime140.dll"):
            (clean / name).write_bytes(b"MZ")
        code, out = run(clean)
        if code != 0:
            print("SELF-TEST FAILED: a clean package was rejected:\n" + out)
            return 1
        # GPLv3-only Qt modules are permitted by the GPLv3 FORTIQ license.
        allowed = ("Qt6VirtualKeyboard.dll", "Qt6QuickTimeline.dll",
                   "platforminputcontexts/qtvirtualkeyboardplugin.dll",
                   "qml/QtCharts/qtchartsqml2plugin.dll")
        for rel in allowed:
            case = Path(tmp) / ("allowed-" + str(abs(hash(rel))))
            for f in clean.rglob("*"):
                if f.is_file():
                    t = case / f.relative_to(clean)
                    t.parent.mkdir(parents=True, exist_ok=True)
                    t.write_bytes(f.read_bytes())
            (case / rel).parent.mkdir(parents=True, exist_ok=True)
            (case / rel).write_bytes(b"MZ")
            code, out = run(case)
            if code != 0:
                print(f"SELF-TEST FAILED: GPL-compatible Qt module {rel} was rejected:\\n" + out)
                return 1
        unknown = Path(tmp) / "unknown"
        for f in clean.rglob("*"):
            if f.is_file():
                t = unknown / f.relative_to(clean)
                t.parent.mkdir(parents=True, exist_ok=True)
                t.write_bytes(f.read_bytes())
        (unknown / "mystery.dll").write_bytes(b"MZ")
        code, out = run(unknown)
        if code == 0 or "unknown third-party library" not in out:
            print("SELF-TEST FAILED: unknown DLL was accepted:\\n" + out)
            return 1
        missing = Path(tmp) / "nolicence"
        (missing / "licenses").mkdir(parents=True)
        (missing / "libssh2.dll").write_bytes(b"MZ")
        code, out = run(missing)
        if code == 0 or "missing" not in out.lower():
            print("SELF-TEST FAILED: missing licence texts were not reported:\n" + out)
            return 1
    print("SELF-TEST OK: GPL Qt modules accepted; unknown libraries and missing licence texts rejected; clean package passes.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage", help="staged Windows application folder")
    ap.add_argument("--apk", help="Android package (.apk)")
    ap.add_argument("--qt", default=str(DEFAULT_QT), help="Qt kit whose headers give the licences")
    ap.add_argument("--self-test", action="store_true", help="prove that the check itself fails on a forbidden package and passes on a clean one")
    ap.add_argument("--allow-debug-tooling", action="store_true", help="debug builds only: QML debugging plugins (qmltooling) are reported, not failed")
    opts = ap.parse_args()
    if opts.self_test:
        return self_test(Path(opts.qt))
    if bool(opts.stage) == bool(opts.apk):
        ap.error("exactly one of --stage and --apk")

    licences = module_licences(Path(opts.qt))
    bad = not_gpl_compatible(licences)
    bad_keys = {b[2:].lower() for b in bad}               # "virtualkeyboard", "quicktimeline", ...
    problems: list[str] = []
    notes: list[str] = []

    if opts.stage:
        stage = Path(opts.stage)
        files = [p for p in stage.rglob("*") if p.is_file()]
        names = [(p, p.relative_to(stage).as_posix()) for p in files]
    else:
        z = zipfile.ZipFile(opts.apk)
        names = [(None, n) for n in z.namelist() if not n.endswith("/")]
        stage = None

    qt_libs: set[str] = set()
    for _, rel in names:
        base = rel.rsplit("/", 1)[-1]
        low = rel.lower()
        mod = qt_module_of_library(base)
        if mod:
            qt_libs.add(mod)
            if mod in bad:
                problems.append(f"{rel}: Qt module {mod} has public headers without a GPLv3/LGPLv3 grant")
        # plugins and QML modules are named after their module
        for key in bad_keys:
            if key in low.replace("_", "").replace("-", "") and not mod:
                if opts.allow_debug_tooling and ("qmltooling" in low or "qmldbg" in low):
                    notes.append(f"{rel}: debugging plugin of the non-LGPL module '{key}' (debug build only, must not be distributed)")
                else:
                    problems.append(f"{rel}: belongs to the non-LGPL Qt module '{key}'")
        if opts.stage and "/" not in rel and base.lower().endswith(".dll") and not mod and not MSVC_RUNTIME.match(base):
            known = KNOWN_COMPONENTS.get(base.lower())
            if not known:
                problems.append(f"{rel}: unknown third-party library (add it to KNOWN_COMPONENTS with its licence file)")
            elif known[0] and not (stage / "licenses" / known[0]).is_file():
                problems.append(f"{rel}: licence text licenses/{known[0]} is missing from the package")
    if opts.stage:
        for need in ("THIRD_PARTY_NOTICES.txt", "Qt-COPYING.txt"):
            if not (stage / "licenses" / need).is_file():
                problems.append(f"licenses/{need} is missing from the package")
        for lib in sorted(qt_libs):
            if lib not in licences:
                notes.append(f"{lib}: no public headers in the kit, licence not checked (private module?)")
    print(f"Qt libraries shipped: {len(qt_libs)}; modules of this kit flagged as not GPLv3-compatible: {', '.join(sorted(bad)) or 'none'}")
    for n in notes:
        print("note:", n)
    if problems:
        print("\nLICENCE PROBLEMS:")
        for p in problems:
            print(" -", p)
        return 1
    print("OK: no flagged incompatible Qt module, no unknown third-party library, licence texts present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
