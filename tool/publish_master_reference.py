#!/usr/bin/env python3
"""Publish the ORIGINAL owner-approved FORTIQ screenshot from PNG or import ZIP.

Run inside a clean local checkout of cybou-fr/fortiq on main:
  python tool/publish_master_reference.py /path/to/FORTIQ_Master_Reference_Import.zip

Requires only Python 3.9+, Git, GitHub push authorization and the unchanged PNG.
Verifies bytes, rebuilds EN/FR/RU website pages, runs documentation checks,
commits ONLY the two PNGs and three generated HTML pages, and pushes to main.
No branch creation and no force pushes.
"""
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
HASH = "852b5944c61ebb8551cc10dadeda47e7924b92baec0fd4b7758ffe2a5c6ffaef"
MASTER_IN_ZIP = "docs/design/reference/fortiq-shell-master.png"
SITE_IN_ZIP = "www/assets/fortiq-shell-master.png"
STAGED = [
    MASTER_IN_ZIP,
    SITE_IN_ZIP,
    "www/index.html",
    "www/fr/index.html",
    "www/ru/index.html",
]

def run(*args: str, capture: bool = False) -> str:
    value = subprocess.run(
        args, cwd=ROOT, text=True, check=True,
        stdout=subprocess.PIPE if capture else None,
    )
    return value.stdout.strip() if capture else ""

def get_original(source: Path) -> bytes:
    if source.suffix.lower() == ".zip":
        with ZipFile(source) as bundle:
            if MASTER_IN_ZIP not in bundle.namelist():
                raise ValueError(f"Import ZIP missing {MASTER_IN_ZIP}")
            data = bundle.read(MASTER_IN_ZIP)
            if SITE_IN_ZIP in bundle.namelist() and bundle.read(SITE_IN_ZIP) != data:
                raise ValueError("ZIP contains differing master and site PNGs")
    else:
        data = source.read_bytes()
    observed = hashlib.sha256(data).hexdigest()
    if observed != HASH:
        raise ValueError(f"Wrong image SHA256: {observed}; expected {HASH}")
    if data[:8] != bytes.fromhex("89504e470d0a1a0a"):
        raise ValueError("Not a PNG")
    if int.from_bytes(data[16:20], "big") != 1536 or int.from_bytes(data[20:24], "big") != 1024:
        raise ValueError("The approved design must be exactly 1536x1024")
    return data

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python tool/publish_master_reference.py /path/to/original.png-or-ZIP")
    source = Path(sys.argv[1]).expanduser().resolve()
    if not source.is_file():
        raise SystemExit(f"Input not found: {source}")
    try:
        data = get_original(source)
        top = run("git", "rev-parse", "--show-toplevel", capture=True)
        if Path(top).resolve() != ROOT:
            raise RuntimeError("Expected to run in cybou-fr/fortiq Git worktree")
        branch = run("git", "branch", "--show-current", capture=True)
        if branch != "main":
            raise RuntimeError(f"Expected branch main, got {branch!r}")
        if run("git", "status", "--porcelain", "--untracked-files=no", capture=True):
            raise RuntimeError("Tracked changes exist. Commit/stash them before importing.")
        if run("git", "diff", "--cached", "--name-only", capture=True):
            raise RuntimeError("Index has staged changes. Refusing to include unrelated files.")
        remote = run("git", "remote", "get-url", "origin", capture=True)
        if not remote:
            raise RuntimeError("Missing origin remote.")
        with tempfile.TemporaryDirectory(prefix="fortiq-approved-design-") as tmp:
            path = Path(tmp) / "fortiq-shell-master.png"
            path.write_bytes(data)
            run(sys.executable, str(ROOT / "tool/install_master_reference.py"), str(path))
        run(sys.executable, str(ROOT / "tool/check_docs_consistency.py"))
        for target in (MASTER_IN_ZIP, SITE_IN_ZIP):
            digest = hashlib.sha256((ROOT / target).read_bytes()).hexdigest()
            if digest != HASH:
                raise RuntimeError(f"Imported file checksum failed: {target}")
        run("git", "add", "--", *STAGED)
        changed = run("git", "diff", "--cached", "--name-only", capture=True).splitlines()
        if any(name not in STAGED for name in changed):
            raise RuntimeError("Unexpected staged content; not committing.")
        if not changed:
            print("Nothing to commit: approved image and localized pages already match main.")
            return
        run("git", "commit", "-m", "design: import byte-exact FORTIQ master screenshot and refresh website")
        # A normal non-force push refuses a stale upstream and preserves others' changes.
        run("git", "push", "origin", "main")
        print("SUCCESS: exact original PNG committed and pushed to main.")
        print("SHA-256:", HASH)
        print("Files:", ", ".join(STAGED))
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as e:
        raise SystemExit(f"ERROR: {e}") from e

if __name__ == "__main__":
    main()
