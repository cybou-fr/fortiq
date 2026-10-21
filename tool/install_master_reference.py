#!/usr/bin/env python3
"""Import the owner's byte-exact design PNG into FORTIQ docs + marketing preview."""
import hashlib
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHA256 = "852b5944c61ebb8551cc10dadeda47e7924b92baec0fd4b7758ffe2a5c6ffaef"
DOC = ROOT / "docs/design/reference/fortiq-shell-master.png"
SITE = ROOT / "www/assets/fortiq-shell-master.png"

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python3 tool/install_master_reference.py /path/to/original/fortiq-shell-master.png")
    source = Path(sys.argv[1])
    image = source.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    if digest != SHA256:
        raise SystemExit("Refusing non-master file; SHA-256 mismatch: " + digest)
    if image[:8] != bytes.fromhex("89504e470d0a1a0a"):
        raise SystemExit("Not a PNG")
    width, height = struct.unpack(">II", image[16:24])
    if (width, height) != (1536, 1024):
        raise SystemExit(f"Unexpected master PNG dimensions: {width}x{height}")
    for target in (DOC, SITE):
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(image)  # Copy unchanged, preserving SHA-256.
        print("Verified", target.relative_to(ROOT), digest)
    subprocess.run([sys.executable, str(ROOT / "docs/website/generate.py")], check=True)
    print("Image imported and site regenerated. Review diff before committing to main.")

if __name__ == "__main__":
    main()
