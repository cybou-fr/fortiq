#!/usr/bin/env python3
"""Explicit, offline, per-user staging of verified llama.cpp and GGUF on Linux.

This command does not download, build, execute, update, or expose the model.
It only installs user-provided files after exact SHA-256 verification.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile

SCHEMA = "fortiq-ai-stage/v1"
HASH = re.compile(r"^[0-9a-fA-F]{64}$")
CHUNK = 1024 * 1024


class StageError(Exception):
    pass


def sha256_file(file: Path, expected: str, magic: bytes) -> int:
    if not HASH.fullmatch(expected):
        raise StageError("Expected a full SHA-256 checksum for every artifact")
    if file.is_symlink() or not file.is_file():
        raise StageError("Source must be a regular file, not a symlink")
    source = file.stat()
    if not stat.S_ISREG(source.st_mode):
        raise StageError("Invalid source type")
    digest = hashlib.sha256()
    count = 0
    with file.open("rb") as inp:
        if inp.read(len(magic)) != magic:
            raise StageError("Source does not have the expected ELF/GGUF header")
        inp.seek(0)
        for piece in iter(lambda: inp.read(CHUNK), b""):
            count += len(piece)
            digest.update(piece)
    if count != source.st_size or digest.hexdigest() != expected.lower():
        raise StageError("SHA-256 mismatch or source changed while reading")
    current = file.stat()
    if (current.st_dev, current.st_ino, current.st_size,
            current.st_mtime_ns, current.st_ctime_ns) != (
            source.st_dev, source.st_ino, source.st_size,
            source.st_mtime_ns, source.st_ctime_ns):
        raise StageError("Source changed during verification")
    return count


def destination(home: Path, relative: str) -> Path:
    if home.is_symlink() or home.stat().st_uid != os.getuid():
        raise StageError("HOME must be an owned, non-symlink directory")
    path = home
    for part in Path(relative).parts[:-1]:
        path = path / part
        if path.is_symlink():
            raise StageError("Refusing a symlink in the destination path")
        if path.exists() and (not path.is_dir() or path.stat().st_uid != os.getuid()):
            raise StageError("Destination directory is not owned by this Linux user")
    target = home / relative
    if target.exists() or target.is_symlink():
        raise StageError("Destination already exists; no file is overwritten")
    return target


def install_file(source: Path, target: Path, expected: str,
                 magic: bytes, mode: int) -> None:
    parent = target.parent
    parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    # Check every path again after mkdir rather than trusting preflight.
    dest = destination(Path.home(), str(target.relative_to(Path.home())))
    if dest != target:
        raise StageError("Destination changed")
    fd, temporary = tempfile.mkstemp(prefix=".ai-stage-", dir=parent)
    try:
        os.fchmod(fd, mode)
        digest = hashlib.sha256()
        with os.fdopen(fd, "wb") as out, source.open("rb") as inp:
            fd = -1
            if inp.read(len(magic)) != magic:
                raise StageError("Artifact header changed during install")
            inp.seek(0)
            for chunk in iter(lambda: inp.read(CHUNK), b""):
                out.write(chunk)
                digest.update(chunk)
            out.flush()
            os.fsync(out.fileno())
        if digest.hexdigest() != expected.lower():
            raise StageError("Source SHA-256 changed during install")
        # link is atomic and fails if a target is already present.
        os.link(temporary, target, follow_symlinks=False)
    finally:
        if fd != -1:
            os.close(fd)
        if os.path.exists(temporary):
            os.unlink(temporary)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Offline, explicit staging of checked llama.cpp CPU and GGUF artifacts")
    parser.add_argument("--llama", type=Path, required=True)
    parser.add_argument("--llama-sha256", required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--model-sha256", required=True)
    parser.add_argument("--apply", action="store_true",
                        help="Actually copy files; default is read-only preflight")
    args = parser.parse_args()
    try:
        if sys.platform != "linux":
            raise StageError("Only Linux is supported")
        home = Path.home()
        runtime_target = destination(home, ".local/bin/llama-cli")
        model_target = destination(home, ".local/share/ai/model.gguf")
        if args.model.suffix.lower() != ".gguf":
            raise StageError("Model must be a GGUF file")
        runtime_bytes = sha256_file(args.llama, args.llama_sha256, b"\x7fELF")
        model_bytes = sha256_file(args.model, args.model_sha256, b"GGUF")
        available = shutil.disk_usage(home).free
        if runtime_bytes + model_bytes + 16 * CHUNK > available:
            raise StageError("Insufficient free space on the selected Linux server")
        if args.apply:
            created = []
            try:
                install_file(args.llama, runtime_target,
                             args.llama_sha256, b"\x7fELF", 0o700)
                created.append(runtime_target)
                install_file(args.model, model_target,
                             args.model_sha256, b"GGUF", 0o600)
                created.append(model_target)
            except Exception:
                for target in reversed(created):
                    target.unlink(missing_ok=True)
                raise
        print(json.dumps({
            "schema": SCHEMA, "status": "installed" if args.apply else "ready",
            "runtime_bytes": runtime_bytes, "model_bytes": model_bytes,
            "requires_separate_ai_cli": True,
            "no_network": True, "no_sudo": True, "cpu_only": True,
        }))
        return 0
    except (StageError, OSError, ValueError) as error:
        print(json.dumps({"schema": SCHEMA, "status": "error",
                          "message": str(error)}))
        return 2


if __name__ == "__main__":
    sys.exit(main())
