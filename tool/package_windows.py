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
uninstalling. With --sign-thumbprint <sha1> the application and the installer are
signed with a certificate from the current user's store (see docs/WINDOWS_SIGNING.md);
without it they are not, and Windows SmartScreen will warn. The maintenance tool that the
installer copies next to the application is not signed by this script.
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
    r"windowscodecs|cryptbase|cryptnet|wintrust|ntdsapi|esent|davclnt|virtdisk|d3dcompiler_47|imagehlp|uiautomationcore|icuuc|icuin|icu|dhcpcsvc|fontsub|pdh|urlmon|mmdevapi|winusb)\.dll$",
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


def find_signtool() -> Path:
    found = sorted(glob.glob("C:/Program Files (x86)/Windows Kits/10/bin/*/x64/signtool.exe"))
    if not found:
        sys.exit("signtool.exe not found (install the Windows SDK)")
    return Path(found[-1])


def authenticode(path: Path) -> tuple[str, str, str]:
    """(status, signer thumbprint, message) of the Authenticode signature of a file, from PowerShell."""
    quoted = str(path).replace("'", "''")
    script = (f"$s = Get-AuthenticodeSignature -LiteralPath '{quoted}'; "
              "$t = if ($s.SignerCertificate) { $s.SignerCertificate.Thumbprint } else { '' }; "
              "Write-Output ($s.Status.ToString() + '|' + $t + '|' + $s.StatusMessage)")
    out = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True, errors="replace").stdout.strip()
    status, _, rest = out.partition("|")
    thumb, _, message = rest.partition("|")
    return status, thumb, message


def sign_files(files: list[Path], thumbprint: str, timestamp_url: str) -> None:
    """Signs with a certificate that is in the current user's store (no password or key ever passes through this script)."""
    tool = find_signtool()
    for f in files:
        cmd = [str(tool), "sign", "/fd", "SHA256", "/sha1", thumbprint, "/s", "My"]
        if timestamp_url:
            cmd += ["/tr", timestamp_url, "/td", "SHA256"]
        run(cmd + [str(f)])


def verify_signatures(files: list[Path], thumbprint: str, allow_untrusted: bool) -> list[str]:
    problems = []
    for f in files:
        status, thumb, message = authenticode(f)
        trusted = status == "Valid"
        # A throw-away self-signed certificate (the self-test) is "UnknownError / not trusted" but must still carry OUR signature and an intact hash.
        accepted_untrusted = allow_untrusted and status in ("UnknownError", "NotTrusted")      # a HashMismatch is never accepted
        if thumb.upper() != thumbprint.upper() or not (trusted or accepted_untrusted):
            problems.append(f"{f.name}: signature status {status} ({message.strip()}), signer {thumb or 'none'}")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default=str(ROOT / "build-msvc"), help="CMake Release build directory with fortiq.exe")
    parser.add_argument("--out", default=str(ROOT / "build-dist"))
    parser.add_argument("--no-installer", action="store_true")
    parser.add_argument("--sign-thumbprint", default="", help="SHA-1 thumbprint of a code-signing certificate in the current user's store: signs fortiq.exe and the installer")
    parser.add_argument("--timestamp-url", default="", help="RFC 3161 timestamp server for the signatures (build-time use only; without one a signature ends with the certificate)")
    parser.add_argument("--allow-untrusted-signature", action="store_true", help="accept a signature whose root is not trusted (a throw-away test certificate only)")
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
         "--skip-plugin-types", "qmltooling,platforminputcontexts,vectorimageformats", "--no-virtualkeyboard", "--no-system-dxc-compiler", str(stage / "fortiq.exe")])

    for d in sorted((p for p in stage.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
        if not any(d.iterdir()):
            d.rmdir()          # module folders that windeployqt created but left empty

    to_verify: list[Path] = []
    if args.sign_thumbprint:
        sign_files([stage / "fortiq.exe"], args.sign_thumbprint, args.timestamp_url)    # before the archive and the installer are made
        to_verify.append(stage / "fortiq.exe")

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
    shutil.copy2(ROOT / "LICENSE", lic / "FORTIQ-LICENSE.txt")   # FORTIQ itself is GPL-3.0: the text travels with every binary
    for pkg in ("libssh2", "openssl", "zlib"):
        src = VCPKG / "share" / pkg / "copyright"
        if src.exists():
            shutil.copy2(src, lic / f"{pkg}.txt")
    for name in ("LICENSE", "COPYING.txt", "Copyright.txt"):
        src = Path("C:/Qt/Licenses") / name
        if src.exists():
            shutil.copy2(src, lic / f"Qt-{name}")
    has_engine = (stage / "Qt6WebEngineCore.dll").is_file()
    if has_engine:
        # Chromium: Qt's own copy of the Chromium licence (BSD-3-Clause) from the Qt sources of the same version, when installed.
        chromium = QT.parent / "Src" / "qtwebengine" / "LICENSE.Chromium"
        if chromium.exists():
            shutil.copy2(chromium, lic / "Chromium-LICENSE.txt")
        else:
            print(f"WARNING: {chromium} not found: install the Qt sources (Qt Maintenance Tool) so that the Chromium licence can be shipped")
    redist = Path(crt[-1]).parents[2] / "redist.txt"
    contact = os.environ.get("FORTIQ_NOTICE_CONTACT", "l'éditeur (CYBOU), par le site du produit")
    qt_version = QT.parent.name
    (lic / "THIRD_PARTY_NOTICES.txt").write_text(
        "FORTIQ is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License version 3\n"
        "(see FORTIQ-LICENSE.txt). The complete corresponding source code of this exact version is available from the FORTIQ source repository\n"
        f"and, on request, from {contact}.\n\n"
        "FORTIQ includes the following third-party software:\n\n"
        f"- Qt {qt_version}, used under the GNU Lesser General Public License version 3 (LGPLv3); see Qt-COPYING.txt, Qt-LICENSE and Qt-Copyright.txt.\n"
        "  The Qt libraries (Qt6*.dll, the plugins and the qml folder) are separate, unmodified files in this folder that are loaded dynamically:\n"
        "  you may replace them with your own build of the same Qt version. The corresponding Qt source code is available from\n"
        f"  https://download.qt.io/official_releases/qt/{qt_version.rsplit('.', 1)[0]}/{qt_version}/ and, on request, from {contact}.\n"
        "  FORTIQ ships no Qt module that is only available under a commercial licence or GPL-2.0-only (checked by tool/check_third_party.py).\n"
        + (
        "- Qt WebEngine (QtWebEngine*.dll, QtWebEngineProcess.exe, the resources folder and translations/qtwebengine_locales) = the Chromium web engine, version\n"
        f"  {qt_version} (Chromium 140 base with security patches taken from later Chromium versions, see CHROMIUM_VERSION in the Qt WebEngine sources). It is used under the LGPLv3\n"
        "  (see Qt-COPYING.txt); Chromium itself is BSD-3-Clause (Chromium-LICENSE.txt) and bundles third-party components under their own licences\n"
        "  (including BSD, MIT, Apache-2.0, MPL-2.0, LGPL-2.1 FFmpeg, ICU, V8, Skia, ANGLE, WebRTC, Brotli, zlib, and others). The authoritative list is the\n"
        "  Qt WebEngine licensing page and the third_party folders of the source: https://doc.qt.io/qt-6/qtwebengine-licensing.html .\n"
        "  The complete corresponding source code of Qt WebEngine and of the Chromium it contains is available from\n"
        f"  https://download.qt.io/official_releases/qt/{qt_version.rsplit('.', 1)[0]}/{qt_version}/submodules/ and, on request, from {contact}.\n"
        "  The engine files are separate, unmodified files in this folder: you may replace them with your own build of the same Qt version.\n"
        "  FORTIQ starts Chromium with its network stack bound to the SSH gateway only (docs/BROWSER.md). Legal review of this notice is still pending.\n"
        if has_engine else "") +
        "- libvterm 0.3.3 (MIT), statically linked, unmodified sources: see libvterm.txt.\n"
        "- libssh2 (BSD-3-Clause), OpenSSL 3 (Apache-2.0), zlib (zlib license): see the matching files.\n"
        "- Mesa llvmpipe software renderer (opengl32sw.dll, MIT and others) and Microsoft d3dcompiler_47.dll, both shipped with Qt for Windows.\n"
        "- Microsoft Visual C++ runtime DLLs, redistributed app-locally under the Visual Studio redistribution terms.\n"
        "- The installer is built with the Qt Installer Framework (GPLv3 with the Qt exception for the installer's content).\n",
        encoding="utf-8")
    check = subprocess.run([sys.executable, str(ROOT / "tool/check_third_party.py"), "--stage", str(stage), "--qt", str(QT)])
    if check.returncode != 0:
        sys.exit("licence check failed: the package contains a component that may not be distributed (see above)")

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
        if args.sign_thumbprint:
            sign_files([setup], args.sign_thumbprint, args.timestamp_url)
            to_verify.append(setup)
        report["installer"] = {"file": setup.name, "bytes": setup.stat().st_size, "sha256": sha256(setup)}

    if args.sign_thumbprint:
        sig_problems = verify_signatures(to_verify, args.sign_thumbprint, args.allow_untrusted_signature)
        report["signatures"] = {"thumbprint": args.sign_thumbprint, "timestamp": bool(args.timestamp_url), "problems": sig_problems}
        if sig_problems:
            problems.extend(sig_problems)
    else:
        report["signatures"] = "not signed (no --sign-thumbprint): Windows SmartScreen will warn"
    (out / "package-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "fortiq_exe_sha256"}, indent=2, ensure_ascii=False))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
