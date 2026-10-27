#!/usr/bin/env python3
"""Licence check of what FORTIQ ships (audit backlog 10): fails when a package contains a component FORTIQ may not distribute.

FORTIQ's own code is published under the GNU GPL v3 (LICENSE) and links Qt dynamically. Qt modules are available under different terms
(docs/THIRD_PARTY_LICENSES.md); the check reads the SPDX-License-Identifier of every public header of the installed Qt kit and sorts a module into:

  LGPL    every header offers LGPL-3.0 (QtCore, QtQuick, ...)             allowed under both policies
  GPL     every header offers GPL-3.0 but some not LGPL (QtCharts, ...)   allowed under policy "gpl3" (the default), reported as a note,
                                                                           refused under policy "lgpl"
  BLOCKED some header offers neither (commercial-only or GPL-2-only)     refused under both policies

  python tool/check_third_party.py --stage build-dist/stage/FORTIQ [--qt C:/Qt/6.12.0/msvc2022_64] [--policy gpl3|lgpl]
  python tool/check_third_party.py --apk build-android-arm64/android-build/build/outputs/apk/debug/android-build-debug.apk
  python tool/check_third_party.py --self-test        (uses a synthetic Qt kit: no Qt installation needed)

Checked in the package: every Qt library (Qt6X.dll, libQt6X_<abi>.so), every Qt plugin whose file name contains the name of a refused
module, every QML module directory of a refused module, QML debugging plugins (qmltooling / qmldbg: never distributable, reported only with
--allow-debug-tooling for debug builds) and, for Windows, every non-Qt DLL against a list of known components (each of which must have its
licence text in <stage>/licenses). Qt WebEngine (the Chromium engine of the Browser, desktop only) needs its own files and notice: a package with
Qt6WebEngineCore must also carry QtWebEngineProcess.exe, the Chromium resources (icudtl.dat, v8_context_snapshot.bin, the .pak files) and
licenses/Chromium-LICENSE.txt; an Android package must NOT contain Qt WebEngine (Android uses the system WebView). Exit status 1 on any
violation or unknown component.
This is a technical check of file names against header licences; it is not legal advice and does not replace reading the notices.
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


def module_licences(qt_root: Path) -> dict[str, tuple[int, int, int]]:
    """module include dir name (QtCore, QtCharts...) -> (headers with an SPDX line, of those: offering LGPL-3.0, offering GPL-3.0 or LGPL-3.0)."""
    result: dict[str, tuple[int, int, int]] = {}
    include = qt_root / "include"
    for d in sorted(include.iterdir()):
        if not d.is_dir() or not d.name.startswith("Qt"):
            continue
        total = lgpl = gpl = 0
        for h in d.glob("*.h"):
            head = h.read_text(encoding="utf-8", errors="replace")[:4000]
            m = re.search(r"SPDX-License-Identifier:\s*(.*)", head)
            if m:
                total += 1
                ids = m.group(1)
                lgpl += "LGPL-3.0" in ids
                gpl += "GPL-3.0" in ids      # also true for "LGPL-3.0"
        if total:
            result[d.name] = (total, lgpl, gpl)
    return result


def classify(licences: dict[str, tuple[int, int, int]], policy: str) -> tuple[set[str], set[str]]:
    """-> (refused modules, GPL-only modules that are allowed). One stray header is enough to move a module: it is a different component."""
    gpl_only = {n for n, (total, lgpl, gpl) in licences.items() if gpl == total and lgpl < total}
    blocked = {n for n, (total, lgpl, gpl) in licences.items() if gpl < total}
    if policy == "lgpl":
        blocked |= gpl_only
        gpl_only = set()
    return blocked, gpl_only


def qt_module_of_library(file_name: str) -> str | None:
    m = re.match(r"^(?:lib)?Qt6(.+?)(?:_[a-z0-9_-]+)?\.(?:dll|so)$", file_name, re.I)
    return "Qt" + m.group(1) if m else None


DEBUG_TOOLING = ("qmltooling", "qmldbg")


def make_fake_kit(root: Path) -> Path:
    """A synthetic Qt kit with one module of every licence class, so the self-test needs no Qt installation."""
    kit = root / "fakeqt"
    modules = {
        "QtCore": "LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only",
        "QtQuick": "LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only",
        "QtCharts": "LicenseRef-Qt-Commercial OR GPL-3.0-only",
        "QtSecretSauce": "LicenseRef-Qt-Commercial",
        "QtOldGpl": "LicenseRef-Qt-Commercial OR GPL-2.0-only",
        "QtWebEngineCore": "LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only",
    }
    for name, spdx in modules.items():
        (kit / "include" / name).mkdir(parents=True)
        (kit / "include" / name / (name.lower() + ".h")).write_text(f"// Copyright (C) The Qt Company Ltd.\n// SPDX-License-Identifier: {spdx}\n", encoding="utf-8")
    return kit


def self_test(_qt: Path) -> int:
    import subprocess
    import tempfile
    me = Path(__file__).resolve()
    failures: list[str] = []

    def run(*args: str) -> tuple[int, str]:
        r = subprocess.run([sys.executable, str(me), *args], capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    def expect(label: str, code: int, out: str, want_ok: bool, needle: str = "") -> None:
        if (code == 0) != want_ok or (needle and needle.lower() not in out.lower()):
            failures.append(f"{label}: expected {'pass' if want_ok else 'failure'}" + (f" mentioning '{needle}'" if needle else "") + f", got exit {code}:\n{out.strip()}")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        kit = make_fake_kit(tmp_path)
        clean = tmp_path / "clean"
        (clean / "licenses").mkdir(parents=True)
        for name in ("THIRD_PARTY_NOTICES.txt", "Qt-COPYING.txt", "FORTIQ-LICENSE.txt", "libssh2.txt", "openssl.txt", "Chromium-LICENSE.txt"):
            (clean / "licenses" / name).write_text("x", encoding="utf-8")
        for name in ("fortiq.exe", "Qt6Core.dll", "Qt6Quick.dll", "libssh2.dll", "libcrypto-3-x64.dll", "vcruntime140.dll"):
            (clean / name).write_bytes(b"MZ")

        def stage_with(label: str, extra: str | None = None, drop: str | None = None) -> Path:
            d = tmp_path / ("s-" + re.sub(r"\W+", "_", label))
            for f in clean.rglob("*"):
                if f.is_file():
                    target = d / f.relative_to(clean)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(f.read_bytes())
            if drop:
                (d / drop).unlink()
            if extra:
                (d / extra).parent.mkdir(parents=True, exist_ok=True)
                (d / extra).write_bytes(b"MZ")
            return d

        base = ["--qt", str(kit)]
        code, out = run("--stage", str(clean), *base)
        expect("clean package, default policy", code, out, True)
        code, out = run("--stage", str(clean), *base, "--policy", "lgpl")
        expect("clean package, lgpl policy", code, out, True)

        # GPL-3.0-only module (Qt Charts): fine for a GPLv3 product, refused when the policy is LGPL-only
        d = stage_with("charts", "Qt6Charts.dll")
        code, out = run("--stage", str(d), *base)
        expect("GPL-only Qt module under policy gpl3", code, out, True, "GPL-3.0")
        code, out = run("--stage", str(d), *base, "--policy", "lgpl")
        expect("GPL-only Qt module under policy lgpl", code, out, False, "QtCharts")
        # commercial-only and GPL-2.0-only modules can never be shipped in a GPLv3 product
        for dll, module in (("Qt6SecretSauce.dll", "QtSecretSauce"), ("Qt6OldGpl.dll", "QtOldGpl")):
            code, out = run("--stage", str(stage_with(module, dll)), *base)
            expect(f"{module} library", code, out, False, module)
        code, out = run("--stage", str(stage_with("plugin", "plugins/platforminputcontexts/qtsecretsauceplugin.dll")), *base)
        expect("plugin of a refused module", code, out, False, "secretsauce")
        code, out = run("--stage", str(stage_with("qml", "qml/QtSecretSauce/qmldir")), *base)
        expect("QML module of a refused module", code, out, False, "secretsauce")
        # debug tooling
        d = stage_with("debug", "plugins/qmltooling/qmldbg_tcp.dll")
        code, out = run("--stage", str(d), *base)
        expect("QML debugging plugin", code, out, False, "debug")
        code, out = run("--stage", str(d), *base, "--allow-debug-tooling")
        expect("QML debugging plugin allowed for a debug build", code, out, True, "debug")
        # unknown libraries and missing notices
        code, out = run("--stage", str(stage_with("unknown", "mystery.dll")), *base)
        expect("unknown third-party DLL", code, out, False, "unknown third-party library")
        code, out = run("--stage", str(stage_with("nolic", drop="licenses/libssh2.txt")), *base)
        expect("missing licence text of a shipped component", code, out, False, "libssh2.txt")
        code, out = run("--stage", str(stage_with("nonotice", drop="licenses/THIRD_PARTY_NOTICES.txt")), *base)
        expect("missing THIRD_PARTY_NOTICES", code, out, False, "THIRD_PARTY_NOTICES")
        # Qt WebEngine (Chromium): complete package passes, each missing piece fails
        engine_files = ("Qt6WebEngineCore.dll", "QtWebEngineProcess.exe", "icudtl.dat", "v8_context_snapshot.bin", "resources/qtwebengine_resources.pak")
        full = stage_with("webengine", engine_files[0])
        for f in engine_files[1:]:
            (full / f).parent.mkdir(parents=True, exist_ok=True)
            (full / f).write_bytes(b"x")
        code, out = run("--stage", str(full), *base)
        expect("complete Qt WebEngine package", code, out, True, "Chromium")
        for f in engine_files[1:]:
            (full / f).rename(full / (f + ".off"))
            code, out = run("--stage", str(full), *base)
            expect("Qt WebEngine package without " + f, code, out, False, f.rsplit("/", 1)[-1])
            (full / (f + ".off")).rename(full / f)
        (full / "licenses" / "Chromium-LICENSE.txt").unlink()
        code, out = run("--stage", str(full), *base)
        expect("Qt WebEngine package without the Chromium licence", code, out, False, "Chromium-LICENSE")

        code, out = run("--stage", str(stage_with("noqtcopy", drop="licenses/Qt-COPYING.txt")), *base)
        expect("missing Qt licence text", code, out, False, "Qt-COPYING")
        code, out = run("--stage", str(stage_with("nogpl", drop="licenses/FORTIQ-LICENSE.txt")), *base)
        expect("missing FORTIQ GPL text", code, out, False, "FORTIQ-LICENSE")

        # Android package
        def apk(name: str, *members: str) -> Path:
            path = tmp_path / name
            with zipfile.ZipFile(path, "w") as z:
                for m in members:
                    z.writestr(m, b"\x7fELF")
            return path

        code, out = run("--apk", str(apk("ok.apk", "lib/arm64-v8a/libQt6Core_arm64-v8a.so", "lib/arm64-v8a/libfortiq_arm64-v8a.so")), *base)
        expect("clean APK", code, out, True)
        code, out = run("--apk", str(apk("bad.apk", "lib/arm64-v8a/libQt6Core_arm64-v8a.so", "lib/arm64-v8a/libQt6SecretSauce_arm64-v8a.so")), *base)
        expect("APK with a refused Qt module", code, out, False, "QtSecretSauce")
        code, out = run("--apk", str(apk("web.apk", "lib/arm64-v8a/libQt6Core_arm64-v8a.so", "lib/arm64-v8a/libQt6WebEngineCore_arm64-v8a.so")), *base)
        expect("APK with Qt WebEngine", code, out, False, "WebEngine")
        code, out = run("--apk", str(apk("dbg.apk", "lib/arm64-v8a/libQt6Core_arm64-v8a.so", "lib/arm64-v8a/libplugins_qmltooling_qmldbg_tcp_arm64-v8a.so")), *base)
        expect("debug APK", code, out, False, "debug")
        # no arguments at all must not pass silently
        code, out = run(*base)
        expect("no package given", code, out, False)

    if failures:
        print("SELF-TEST FAILED:\n" + "\n---\n".join(failures))
        return 1
    print("SELF-TEST OK: GPL-only modules pass under policy gpl3 and fail under lgpl; commercial-only and GPL-2-only modules, their plugins and QML modules, debug tooling, "
          "unknown libraries and missing licence texts are rejected in packages and APKs; clean packages pass.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage", help="staged Windows application folder")
    ap.add_argument("--apk", help="Android package (.apk)")
    ap.add_argument("--qt", default=str(DEFAULT_QT), help="Qt kit whose headers give the licences")
    ap.add_argument("--policy", choices=("gpl3", "lgpl"), default="gpl3",
                    help="gpl3 (default, FORTIQ is GPL-3.0): Qt modules offered under GPL-3.0 are allowed; lgpl: only modules offered under LGPL-3.0")
    ap.add_argument("--self-test", action="store_true", help="prove that the check itself fails on a forbidden package and passes on a clean one")
    ap.add_argument("--allow-debug-tooling", action="store_true", help="debug builds only: QML debugging plugins (qmltooling) are reported, not failed")
    opts = ap.parse_args()
    if opts.self_test:
        return self_test(Path(opts.qt))
    if bool(opts.stage) == bool(opts.apk):
        ap.error("exactly one of --stage and --apk")

    licences = module_licences(Path(opts.qt))
    bad, gpl_only = classify(licences, opts.policy)
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
                why = "GPL-3.0 only, refused by policy lgpl" if mod in {n for n, (t_, l_, g_) in licences.items() if g_ == t_} else "commercial-only or GPL-2-only"
                problems.append(f"{rel}: Qt module {mod} cannot be distributed ({why})")
        # QML debugging plugins are never distributable
        if any(tag in low for tag in DEBUG_TOOLING):
            if opts.allow_debug_tooling:
                notes.append(f"{rel}: QML debugging plugin (debug build only, must not be distributed)")
            else:
                problems.append(f"{rel}: QML debug tooling must not be distributed (build a release package, or pass --allow-debug-tooling for a debug build)")
        # plugins and QML modules are named after their module
        elif not mod:
            for key in bad_keys:
                if key in low.replace("_", "").replace("-", ""):
                    problems.append(f"{rel}: belongs to the refused Qt module '{key}'")
        if opts.stage and "/" not in rel and base.lower().endswith(".dll") and not mod and not MSVC_RUNTIME.match(base):
            known = KNOWN_COMPONENTS.get(base.lower())
            if not known:
                problems.append(f"{rel}: unknown third-party library (add it to KNOWN_COMPONENTS with its licence file)")
            elif known[0] and not (stage / "licenses" / known[0]).is_file():
                problems.append(f"{rel}: licence text licenses/{known[0]} is missing from the package")
    if "QtWebEngineCore" in qt_libs:
        if opts.apk:
            problems.append("Qt WebEngine must not be in an Android package (the Android Browser uses the system WebView)")
        else:
            for need in ("licenses/Chromium-LICENSE.txt", "QtWebEngineProcess.exe", "icudtl.dat", "v8_context_snapshot.bin"):
                if not any(rel == need or rel.endswith("/" + need.rsplit("/", 1)[-1]) for _, rel in names):
                    problems.append(f"Qt WebEngine is shipped but {need} is missing (the engine would not start, or the Chromium notice is missing)")
            if not any(rel.endswith("qtwebengine_resources.pak") for _, rel in names):
                problems.append("Qt WebEngine is shipped but qtwebengine_resources.pak is missing")
            notes.append("Qt WebEngine (Chromium): offered by Qt under LGPL-3.0/GPL-3.0; Chromium and its bundled components keep their own licences "
                         "(BSD-3-Clause and many others, some LGPL/MPL): see licenses/Chromium-LICENSE.txt and THIRD_PARTY_NOTICES.txt; legal review still required")
    for lib in sorted(qt_libs & gpl_only):
        notes.append(f"{lib}: offered by Qt under GPL-3.0 only; allowed because FORTIQ is GPL-3.0 (its source and the Qt source must be offered with the binaries)")
    if opts.stage:
        for need in ("THIRD_PARTY_NOTICES.txt", "Qt-COPYING.txt", "FORTIQ-LICENSE.txt"):
            if not (stage / "licenses" / need).is_file():
                problems.append(f"licenses/{need} is missing from the package")
        for lib in sorted(qt_libs):
            if lib not in licences:
                notes.append(f"{lib}: no public headers in the kit, licence not checked (private module?)")
    print(f"Policy: {opts.policy}; Qt libraries shipped: {len(qt_libs)}; modules of this kit refused by the policy: {', '.join(sorted(bad)) or 'none'}")
    for n in notes:
        print("note:", n)
    if problems:
        print("\nLICENCE PROBLEMS:")
        for p in problems:
            print(" -", p)
        return 1
    print("OK: no refused Qt module, no unknown third-party library, no debug tooling, licence texts present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
