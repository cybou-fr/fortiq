#!/usr/bin/env python3
"""Build the Windows x64 distribution of FORTIQ from a Release build.

    python tool/package_windows.py --build build-msvc

Produces in build-dist/ (never committed, never published by this script):
  stage/FORTIQ/                      self-contained application folder
  FORTIQ-<ver>-win64.zip             portable archive of that folder
  FORTIQ-<ver>-win64-setup.exe       Qt Installer Framework installer
  package-report.json                files, sizes, SHA-256, dependency check

Steps: copy fortiq.exe, run windeployqt (Release, QML imports from qml/),
add the libssh2 / OpenSSL / zlib DLLs actually imported, add the MSVC
runtime app-locally (no vc_redist run on the user's machine), collect
third-party license texts, then verify with dumpbin that every DLL import of
every shipped binary is either shipped or a Windows system library.

The installer installs per user into %LOCALAPPDATA%\\Programs\\FORTIQ (no
administrator rights), adds a Start menu entry and a maintenance tool for
uninstalling. It is not code-signed: Windows SmartScreen will warn.
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QT = Path(os.environ.get("FORTIQ_QT", "C:/Qt/6.12.0/msvc2022_64"))
IFW = Path(os.environ.get("FORTIQ_IFW", "C:/Qt/Tools/QtInstallerFramework/4.11/bin"))
VCPKG = Path(os.environ.get("FORTIQ_VCPKG", "C:/Users/cybou/vcpkg/installed/x64-windows"))
VS = Path(os.environ.get("FORTIQ_VS", "C:/Program Files/Microsoft Visual Studio/2022/Community"))

# Imports that Windows itself provides (System32, API sets).
SYSTEM_DLL = re.compile(
    r"^(api-ms-win-.*|ext-ms-win-.*|kernel32|kernelbase|user32|gdi32|gdi32full|advapi32|shell32|ole32|oleaut32|"
    r"combase|ws2_32|crypt32|bcrypt|ncrypt|secur32|sspicli|iphlpapi|dnsapi|netapi32|userenv|winmm|imm32|version|"
    r"shlwapi|setupapi|cfgmgr32|comdlg32|comctl32|uxtheme|dwmapi|dwrite|d2d1|d3d11|d3d12|dxgi|d3d9|dcomp|"
    r"opengl32|glu32|mpr|wtsapi32|rpcrt4|ntdll|authz|powrprof|propsys|shcore|winspool\.drv|wininet|winhttp|"
    r"msimg32|hid|dbghelp|msvcrt|ucrtbase|normaliz|mscms|wevtapi|dxcore|dsound|mf|mfplat|mfreadwrite|"
    r"windowscodecs|cryptbase|cryptnet|wintrust|ntdsapi|esent|davclnt|virtdisk|d3dcompiler_47|imagehlp|uiautomationcore|icuuc|icuin|icu)\.dll$",
    re.IGNORECASE)


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd))
    return subprocess.run(cmd, check=True, **kw)


def dumpbin() -> Path:
    found = sorted(glob.glob(str(VS / "VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe")))
    if not found:
        sys.exit("dumpbin.exe not found (set FORTIQ_VS)")
    return Path(found[-1])


def imports(binary: Path, tool: Path) -> list[str]:
    out = subprocess.run([str(tool), "/nologo", "/dependents", str(binary)],
                         capture_output=True, text=True, errors="replace").stdout
    deps, inside = [], False
    for line in out.splitlines():
        s = line.strip()
        if s.startswith("Image has the following"):
            inside = True
            continue
        if inside:
            if not s:
                if deps:
                    break
                continue
            if s.lower().endswith(".dll"):
                deps.append(s)
            elif s.startswith("Summary"):
                break
    return deps


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default=str(ROOT / "build-msvc"), help="CMake Release build directory with fortiq.exe")
    parser.add_argument("--out", default=str(ROOT / "build-dist"))
    parser.add_argument("--no-installer", action="store_true")
    args = parser.parse_args()

    build, out = Path(args.build), Path(args.out)
    exe = build / "fortiq.exe"
    if not exe.is_file():
        sys.exit(f"missing {exe}: build the Release configuration first")
    version = re.search(r"project\(Fortiq VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)
    stage = out / "stage" / "FORTIQ"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    shutil.copy2(exe, stage / "fortiq.exe")
    run([str(QT / "bin/windeployqt.exe"), "--release", "--qmldir", str(ROOT / "qml"), "--no-compiler-runtime",
         "--skip-plugin-types", "qmltooling", "--no-system-dxc-compiler", str(stage / "fortiq.exe")])

    tool = dumpbin()
    # Third-party DLLs: follow imports from fortiq.exe into vcpkg's bin.
    pending, seen = ["fortiq.exe"], set()
    while pending:
        name = pending.pop()
        if name.lower() in seen:
            continue
        seen.add(name.lower())
        target = stage / name
        if not target.exists():
            candidate = VCPKG / "bin" / name
            if not candidate.exists():
                continue
            shutil.copy2(candidate, target)
        pending.extend(imports(target, tool))

    # MSVC runtime, app-local.
    crt = sorted(glob.glob(str(VS / "VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT")))
    if not crt:
        sys.exit("MSVC redistributable folder not found")
    for dll in Path(crt[-1]).glob("*.dll"):
        shutil.copy2(dll, stage / dll.name)

    # Licenses and notices.
    lic = stage / "licenses"
    lic.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "third_party/libvterm/LICENSE", lic / "libvterm.txt")
    for pkg in ("libssh2", "openssl", "zlib"):
        src = VCPKG / "share" / pkg / "copyright"
        if src.exists():
            shutil.copy2(src, lic / f"{pkg}.txt")
    for name in ("LICENSE", "COPYING.txt", "Copyright.txt"):
        src = Path("C:/Qt/Licenses") / name
        if src.exists():
            shutil.copy2(src, lic / f"Qt-{name}")
    redist = Path(crt[-1]).parents[2] / "redist.txt"
    (lic / "THIRD_PARTY_NOTICES.txt").write_text(
        "FORTIQ includes the following third-party libraries:\n"
        "- libvterm 0.3.3 (MIT), statically linked, unmodified sources: see libvterm.txt.\n"
        f"- Qt {QT.parent.name} (LGPLv3 / GPLv3; see Qt-*.txt). The Qt libraries are separate DLLs in this folder and can be replaced.\n"
        "- libssh2 (BSD-3-Clause), OpenSSL 3 (Apache-2.0), zlib (zlib license): see the matching files.\n"
        "- Microsoft Visual C++ runtime DLLs, redistributed app-locally under the Visual Studio redistribution terms.\n"
        "Commercial Qt licensing for the product remains an open decision (docs/ARCHITECTURE.md).\n",
        encoding="utf-8")

    # Dependency check of everything shipped.
    shipped = {p.name.lower() for p in stage.rglob("*") if p.is_file()}
    problems = []
    binaries = [p for p in stage.rglob("*") if p.suffix.lower() in (".exe", ".dll")]
    for b in binaries:
        for dep in imports(b, tool):
            if dep.lower() not in shipped and not SYSTEM_DLL.match(dep):
                problems.append(f"{b.relative_to(stage)} -> {dep}")
    files = sorted(p for p in stage.rglob("*") if p.is_file())
    report = {
        "version": version,
        "files": len(files),
        "bytes": sum(p.stat().st_size for p in files),
        "unresolved_imports": problems,
        "fortiq_exe_sha256": sha256(stage / "fortiq.exe"),
    }

    zip_path = out / f"FORTIQ-{version}-win64.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for p in files:
            z.write(p, Path("FORTIQ") / p.relative_to(stage))
    report["zip"] = {"file": zip_path.name, "bytes": zip_path.stat().st_size, "sha256": sha256(zip_path)}

    if not args.no_installer:
        ifw = out / "ifw"
        if ifw.exists():
            shutil.rmtree(ifw)
        meta = ifw / "packages/fr.cybou.fortiq/meta"
        data = ifw / "packages/fr.cybou.fortiq/data"
        meta.mkdir(parents=True)
        (ifw / "config").mkdir(parents=True)
        shutil.copytree(stage, data)
        icon = build / "generated/branding/fortiq.ico"
        if icon.exists():
            shutil.copy2(icon, ifw / "config/fortiq.ico")
        (ifw / "config/config.xml").write_text(f"""<?xml version="1.0" encoding="UTF-8"?>
<Installer>
    <Name>FORTIQ</Name>
    <Version>{version}</Version>
    <Title>FORTIQ {version}</Title>
    <Publisher>CYBOU</Publisher>
    <StartMenuDir>FORTIQ</StartMenuDir>
    <TargetDir>@HomeDir@/AppData/Local/Programs/FORTIQ</TargetDir>
    <AdminTargetDir>@ApplicationsDirX64@/FORTIQ</AdminTargetDir>
    <MaintenanceToolName>FORTIQ-maintenance</MaintenanceToolName>
    <InstallerApplicationIcon>fortiq</InstallerApplicationIcon>
    <InstallerWindowIcon>fortiq.ico</InstallerWindowIcon>
    <WizardStyle>Modern</WizardStyle>
    <AllowNonAsciiCharacters>true</AllowNonAsciiCharacters>
    <AllowSpaceInPath>true</AllowSpaceInPath>
</Installer>
""", encoding="utf-8")
        (meta / "package.xml").write_text(f"""<?xml version="1.0" encoding="UTF-8"?>
<Package>
    <DisplayName>FORTIQ</DisplayName>
    <Description>Bureau Linux distant par SSH.</Description>
    <Version>{version}</Version>
    <ReleaseDate>{__import__('datetime').date.today().isoformat()}</ReleaseDate>
    <Default>true</Default>
    <ForcedInstallation>true</ForcedInstallation>
    <Script>installscript.qs</Script>
    <Licenses>
        <License name="Avis de tiers" file="notices.txt" />
    </Licenses>
</Package>
""", encoding="utf-8")
        shutil.copy2(lic / "THIRD_PARTY_NOTICES.txt", meta / "notices.txt")
        (meta / "installscript.qs").write_text("""function Component() {}
Component.prototype.createOperations = function() {
    component.createOperations();
    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut", "@TargetDir@/fortiq.exe",
            "@StartMenuDir@/FORTIQ.lnk", "workingDirectory=@TargetDir@",
            "iconPath=@TargetDir@/fortiq.exe", "iconId=0", "description=FORTIQ");
        component.addOperation("CreateShortcut", "@TargetDir@/FORTIQ-maintenance.exe",
            "@StartMenuDir@/Désinstaller FORTIQ.lnk", "workingDirectory=@TargetDir@");
    }
}
""", encoding="utf-8")
        setup = out / f"FORTIQ-{version}-win64-setup.exe"
        run([str(IFW / "binarycreator.exe"), "--offline-only", "-c", str(ifw / "config/config.xml"),
             "-p", str(ifw / "packages"), str(setup)])
        report["installer"] = {"file": setup.name, "bytes": setup.stat().st_size, "sha256": sha256(setup)}

    (out / "package-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "fortiq_exe_sha256"}, indent=2, ensure_ascii=False))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
