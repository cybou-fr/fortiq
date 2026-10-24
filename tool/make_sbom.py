#!/usr/bin/env python3
"""Software bill of materials (CycloneDX 1.5, JSON) of what FORTIQ ships (docs/STABILIZATION_PLAN.md, S-11).

Sources of truth, read from the build machine (nothing is downloaded):
  * vcpkg `installed/vcpkg/status` of the Windows (x64-windows) and Android (arm64-android) dependency trees: libssh2, OpenSSL, zlib
    with their exact versions and port revisions; SHA-256 of the Windows DLLs that exist on disk;
  * the Qt kit version (Qt6ConfigVersion.cmake) and the Qt Installer Framework version;
  * the vendored libvterm (`third_party/libvterm/README.fortiq.md`: upstream commit and local patches);
  * the project version from CMakeLists.txt.
Components that cannot be versioned from the machine (Mesa llvmpipe and d3dcompiler_47 inside the Windows package, the Gradle
dependencies of Qt for Android, the MSVC runtime) are listed with `fortiq:version-known = false` so that nobody reads their absence of
a version as "no vulnerabilities". The file is deterministic (no timestamp unless --timestamp), so a changed dependency shows up as a diff.

  python tool/make_sbom.py --output docs/sbom/fortiq.cdx.json      # write
  python tool/make_sbom.py --check docs/sbom/fortiq.cdx.json       # fail if the machine no longer matches the committed file
  python tool/make_sbom.py --self-test                             # structure checks on a synthetic inventory (no machine needed)

This is an inventory, not a vulnerability scan: docs/STABILIZATION_PLAN.md (S-11) records the manual CVE review of these exact versions.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_QT = Path("C:/Qt/6.12.0/msvc2022_64")
DEFAULT_IFW = Path("C:/Qt/Tools/QtInstallerFramework")
DEFAULT_VCPKG_WINDOWS = Path("C:/Users/cybou/vcpkg/installed")
DEFAULT_VCPKG_ANDROID = ROOT / "build-android-deps" / "installed"
WINDOWS_DLLS = {"libssh2": "libssh2.dll", "openssl": ("libcrypto-3-x64.dll", "libssl-3-x64.dll"), "zlib": "z.dll"}
PORTS = {
    "libssh2": dict(cpe="cpe:2.3:a:libssh2:libssh2:{v}:*:*:*:*:*:*:*", license="BSD-3-Clause", url="https://libssh2.org/"),
    "openssl": dict(cpe="cpe:2.3:a:openssl:openssl:{v}:*:*:*:*:*:*:*", license="Apache-2.0", url="https://www.openssl.org/"),
    "zlib": dict(cpe="cpe:2.3:a:zlib:zlib:{v}:*:*:*:*:*:*:*", license="Zlib", url="https://zlib.net/"),
}


def read_vcpkg_status(installed: Path, triplet: str) -> dict[str, str]:
    """name -> 'version[#port-revision]' for the real ports (feature records and other triplets are skipped)."""
    status = installed / "vcpkg" / "status"
    if not status.is_file():
        return {}
    found: dict[str, str] = {}
    for block in status.read_text(encoding="utf-8", errors="replace").split("\n\n"):
        fields = dict(re.findall(r"^([A-Za-z-]+): (.*)$", block, re.M))
        if fields.get("Architecture") != triplet or "Feature" in fields or fields.get("Status", "install ok installed") != "install ok installed":
            continue
        name, version = fields.get("Package"), fields.get("Version")
        if name and version:
            port = fields.get("Port-Version")
            found[name] = version + (f"#{port}" if port and port != "0" else "")
    return found


def sha256_of(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def qt_version(qt: Path) -> str | None:
    config = qt / "lib" / "cmake" / "Qt6" / "Qt6ConfigVersion.cmake"
    if config.is_file():
        match = re.search(r'set\(PACKAGE_VERSION "([0-9.]+)"\)', config.read_text(encoding="utf-8", errors="replace"))
        if match:
            return match.group(1)
    match = re.search(r"Qt/(\d+\.\d+\.\d+)", qt.as_posix())
    return match.group(1) if match else None


def ifw_version(ifw: Path) -> str | None:
    for sub in sorted(ifw.glob("*")) if ifw.is_dir() else []:
        if re.fullmatch(r"\d+\.\d+(\.\d+)?", sub.name):
            return sub.name
    return None


def project_version() -> str:
    match = re.search(r"project\(Fortiq VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    return match.group(1) if match else "0"


def libvterm_info() -> dict:
    text = (ROOT / "third_party" / "libvterm" / "README.fortiq.md").read_text(encoding="utf-8")
    commit = re.search(r"commit `([0-9a-f]{40})`", text)
    header = (ROOT / "third_party" / "libvterm" / "include" / "vterm.h").read_text(encoding="utf-8", errors="replace")
    ver = ".".join(re.search(rf"VTERM_VERSION_{part} (\d+)", header).group(1) for part in ("MAJOR", "MINOR", "PATCH"))
    patches = len(re.findall(r"^\d\. `src/", text, re.M))
    return dict(commit=commit.group(1) if commit else "", version=ver, patches=patches)


def component(name: str, version: str | None, kind: str = "library", *, license: str | None = None, purl: str | None = None,
              cpe: str | None = None, url: str | None = None, hashes: dict[str, str] | None = None, props: dict[str, str] | None = None,
              description: str | None = None) -> dict:
    comp: dict = {"type": kind, "bom-ref": f"{name}@{version or 'unknown'}:{(props or {}).get('fortiq:platform', '')}", "name": name}
    if version:
        comp["version"] = version
    if description:
        comp["description"] = description
    if license:
        comp["licenses"] = [{"license": {"id": license}}] if re.fullmatch(r"[A-Za-z0-9.+-]+", license) and " " not in license else [{"license": {"name": license}}]
    if purl:
        comp["purl"] = purl
    if cpe:
        comp["cpe"] = cpe
    if url:
        comp["externalReferences"] = [{"type": "website", "url": url}]
    if hashes:
        comp["hashes"] = [{"alg": "SHA-256", "content": value} for _, value in sorted(hashes.items())]
    properties = dict(props or {})
    properties["fortiq:version-known"] = "true" if version else "false"
    comp["properties"] = [{"name": key, "value": value} for key, value in sorted(properties.items())]
    return comp


def build_inventory(qt: Path, ifw: Path, vcpkg_windows: Path, vcpkg_android: Path) -> list[dict]:
    components: list[dict] = []
    qtv = qt_version(qt)
    components.append(component("Qt", qtv, license="LGPL-3.0-only", purl=f"pkg:generic/qt@{qtv}" if qtv else None,
                                cpe=f"cpe:2.3:a:qt:qt:{qtv}:*:*:*:*:*:*:*" if qtv else None, url="https://www.qt.io/",
                                props={"fortiq:platform": "windows-x64,android-arm64", "fortiq:linking": "dynamic (LGPLv3)"},
                                description="Qt Base, Declarative, Quick Controls 2 (LGPL modules only; see tool/check_third_party.py)"))
    for platform, installed, triplet in (("windows-x64", vcpkg_windows, "x64-windows"), ("android-arm64", vcpkg_android, "arm64-android")):
        ports = read_vcpkg_status(installed, triplet)
        for name, meta in PORTS.items():
            full = ports.get(name)
            version = full.split("#")[0] if full else None
            hashes = {}
            if platform == "windows-x64":
                names = WINDOWS_DLLS[name]
                for dll in ((names,) if isinstance(names, str) else names):
                    digest = sha256_of(installed / triplet / "bin" / dll)
                    if digest:
                        hashes[dll] = digest
            props = {"fortiq:platform": platform, "fortiq:source": f"vcpkg {triplet}" + (f" (port revision {full.split('#')[1]})" if full and '#' in full else "")}
            components.append(component(name, version, license=meta["license"], purl=f"pkg:vcpkg/{name}@{version}" if version else None,
                                        cpe=meta["cpe"].format(v=version) if version else None, url=meta["url"], hashes=hashes, props=props))
    vt = libvterm_info()
    components.append(component("libvterm", vt["version"], license="MIT", purl=f"pkg:github/neovim/libvterm@{vt['commit']}" if vt["commit"] else None,
                                url="https://github.com/neovim/libvterm",
                                props={"fortiq:platform": "windows-x64,android-arm64", "fortiq:source": "vendored in third_party/libvterm",
                                       "fortiq:upstream-commit": vt["commit"], "fortiq:local-patches": str(vt["patches"]),
                                       "fortiq:linking": "static"},
                                description="Terminal emulation; carries local security patches (third_party/libvterm/README.fortiq.md)"))
    ifwv = ifw_version(ifw)
    components.append(component("Qt Installer Framework", ifwv, kind="application", license="GPL-3.0-only WITH exception (to be confirmed, see THIRD_PARTY_LICENSES.md)",
                                props={"fortiq:platform": "windows-x64", "fortiq:source": "Qt online installer, Tools/QtInstallerFramework", "fortiq:role": "installer builder (installerbase.exe is copied into the installer)"}))
    for name, note in (("Mesa llvmpipe (opengl32sw.dll)", "software OpenGL shipped by windeployqt"), ("Direct3D compiler (d3dcompiler_47.dll)", "Microsoft redistributable"),
                       ("Microsoft Visual C++ runtime", "vcruntime140*/msvcp140*"), ("Qt for Android Gradle dependencies", "not inventoried (androiddeployqt)")):
        components.append(component(name, None, props={"fortiq:platform": "android-arm64" if "Android" in name else "windows-x64", "fortiq:note": note}))
    return components


def build_bom(components: list[dict], timestamp: str | None) -> dict:
    seen: set[str] = set()
    for comp in components:
        if comp["bom-ref"] in seen:
            raise SystemExit(f"duplicate bom-ref {comp['bom-ref']}")
        seen.add(comp["bom-ref"])
    body = {
        "bomFormat": "CycloneDX", "specVersion": "1.5", "version": 1,
        "metadata": {
            "tools": {"components": [{"type": "application", "name": "tool/make_sbom.py"}]},
            "component": {"type": "application", "name": "FORTIQ", "version": project_version(), "bom-ref": "fortiq"},
            "properties": [{"name": "fortiq:note", "value": "inventory only; CVE review is recorded in docs/STABILIZATION_PLAN.md (S-11)"}],
        },
        "components": sorted(components, key=lambda c: (c["name"], c["bom-ref"])),
        "dependencies": [{"ref": "fortiq", "dependsOn": sorted(c["bom-ref"] for c in components)}],
    }
    if timestamp:
        body["metadata"]["timestamp"] = timestamp
    digest = hashlib.sha256(json.dumps(body, sort_keys=True).encode()).hexdigest()
    body["serialNumber"] = "urn:uuid:" + str(uuid.UUID(digest[:32]))
    return body


def validate(bom: dict) -> list[str]:
    problems: list[str] = []
    if bom.get("bomFormat") != "CycloneDX" or bom.get("specVersion") != "1.5":
        problems.append("not CycloneDX 1.5")
    refs = [c.get("bom-ref") for c in bom.get("components", [])]
    if len(refs) != len(set(refs)):
        problems.append("duplicate bom-ref")
    for comp in bom.get("components", []):
        props = {p["name"]: p["value"] for p in comp.get("properties", [])}
        if comp.get("version") is None and props.get("fortiq:version-known") != "false":
            problems.append(f"{comp['name']}: no version and not marked unknown")
        if comp.get("version") and not comp.get("purl") and props.get("fortiq:source") is None:
            problems.append(f"{comp['name']}: versioned component without purl or source")
        if comp.get("cpe") and comp.get("version") and comp["version"] not in comp["cpe"]:
            problems.append(f"{comp['name']}: cpe does not carry the version")
    known = {c["name"] for c in bom.get("components", [])}
    for required in ("Qt", "libssh2", "openssl", "zlib", "libvterm"):
        if required not in known:
            problems.append(f"missing component {required}")
    return problems


def self_test() -> None:
    inv = [component("Qt", "6.12.0", license="LGPL-3.0-only", purl="pkg:generic/qt@6.12.0", cpe="cpe:2.3:a:qt:qt:6.12.0:*:*:*:*:*:*:*", props={"fortiq:platform": "x"})]
    inv += [component(n, "1.0", license="MIT", purl=f"pkg:vcpkg/{n}@1.0", cpe=f"cpe:2.3:a:{n}:{n}:1.0:*:*:*:*:*:*:*", props={"fortiq:platform": "x"}) for n in ("libssh2", "openssl", "zlib", "libvterm")]
    assert not validate(build_bom(inv, None)), validate(build_bom(inv, None))
    assert build_bom(inv, None) == build_bom(list(reversed(inv)), None), "output must not depend on discovery order"
    broken = [dict(c) for c in inv]
    broken[0].pop("version")
    assert any("no version" in p for p in validate(build_bom(broken, None)))
    assert any("missing component" in p for p in validate(build_bom(inv[:2], None)))
    unknown = inv + [component("Mesa", None, props={"fortiq:platform": "x"})]
    assert not validate(build_bom(unknown, None))
    wrong_cpe = [dict(c) for c in inv]
    wrong_cpe[1]["cpe"] = "cpe:2.3:a:libssh2:libssh2:9.9:*:*:*:*:*:*:*"
    assert any("cpe does not carry" in p for p in validate(build_bom(wrong_cpe, None)))
    print("make_sbom self-test passed")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--check", type=Path, help="compare the committed SBOM with the current machine; exit 1 on drift")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--timestamp", help="ISO-8601 time to record (omitted by default so the file stays reproducible)")
    ap.add_argument("--qt", type=Path, default=DEFAULT_QT)
    ap.add_argument("--ifw", type=Path, default=DEFAULT_IFW)
    ap.add_argument("--vcpkg-windows", type=Path, default=DEFAULT_VCPKG_WINDOWS)
    ap.add_argument("--vcpkg-android", type=Path, default=DEFAULT_VCPKG_ANDROID)
    opts = ap.parse_args()
    if opts.self_test:
        self_test()
        return 0
    bom = build_bom(build_inventory(opts.qt, opts.ifw, opts.vcpkg_windows, opts.vcpkg_android), opts.timestamp)
    problems = validate(bom)
    for problem in problems:
        print("SBOM problem:", problem, file=sys.stderr)
    if problems:
        return 1
    text = json.dumps(bom, indent=2, ensure_ascii=False) + "\n"
    if opts.check:
        have = json.loads(opts.check.read_text(encoding="utf-8"))
        have.pop("serialNumber", None)
        now = json.loads(text)
        now.pop("serialNumber", None)
        if have != now:
            print(f"SBOM drift: {opts.check} no longer matches this machine; regenerate with --output and review the diff", file=sys.stderr)
            return 1
        print("SBOM matches the machine:", len(bom["components"]), "components")
        return 0
    if opts.output:
        opts.output.parent.mkdir(parents=True, exist_ok=True)
        opts.output.write_text(text, encoding="utf-8", newline="\n")
        print("wrote", opts.output, "-", len(bom["components"]), "components")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
