"""Offline staging tests: never download a model, overwrite files or use sudo."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

STAGE = Path(__file__).resolve().parent / "stage.py"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class StageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        base = Path(self.tmp.name)
        self.home = base / "home"
        self.home.mkdir(mode=0o700)
        self.runtime = base / "verified-llama"
        self.runtime.write_bytes(b"\x7fELFcpu-only-test-executable")
        self.model = base / "verified.gguf"
        self.model.write_bytes(b"GGUFtiny-test-model")
        self.env = {**os.environ, "HOME": str(self.home)}

    def run_stage(self, *extra, binary=None, model=None, digest=None):
        runtime = binary or self.runtime
        model_file = model or self.model
        command = [
            sys.executable, str(STAGE), "--llama", str(runtime),
            "--llama-sha256", sha(runtime),
            "--model", str(model_file),
            "--model-sha256", digest or sha(model_file), *extra,
        ]
        p = subprocess.run(command, capture_output=True, text=True,
                           env=self.env, timeout=15)
        return p.returncode, json.loads(p.stdout)

    def test_preflight_does_not_touch_home(self):
        rc, result = self.run_stage()
        self.assertEqual(rc, 0)
        self.assertEqual(result["status"], "ready")
        self.assertTrue(result["no_network"])
        self.assertTrue(result["cpu_only"])
        self.assertEqual(list(self.home.iterdir()), [])

    def test_apply_installs_verified_regular_files_without_overwrite(self):
        rc, result = self.run_stage("--apply")
        self.assertEqual(rc, 0, result)
        self.assertEqual(result["status"], "installed")
        installed_bin = self.home / ".local/bin/llama-cli"
        installed_model = self.home / ".local/share/ai/model.gguf"
        self.assertEqual(installed_bin.read_bytes(), self.runtime.read_bytes())
        self.assertEqual(installed_model.read_bytes(), self.model.read_bytes())
        self.assertEqual(installed_bin.stat().st_mode & 0o777, 0o700)
        self.assertEqual(installed_model.stat().st_mode & 0o777, 0o600)
        rc, result = self.run_stage("--apply")
        self.assertEqual(rc, 2)
        self.assertIn("already exists", result["message"])

    def test_mismatching_hash_never_installs(self):
        rc, result = self.run_stage("--apply", digest="0" * 64)
        self.assertEqual(rc, 2)
        self.assertIn("SHA-256", result["message"])
        self.assertEqual(list(self.home.iterdir()), [])

    def test_bad_runtime_or_model_header_is_rejected(self):
        self.runtime.write_bytes(b"not an ELF program")
        rc, result = self.run_stage("--apply")
        self.assertEqual(rc, 2)
        self.assertIn("ELF/GGUF", result["message"])
        self.assertEqual(list(self.home.iterdir()), [])

    def test_symlink_source_is_rejected(self):
        other = Path(self.tmp.name) / "symlink.gguf"
        other.symlink_to(self.model)
        rc, result = self.run_stage("--apply", model=other)
        self.assertEqual(rc, 2)
        self.assertIn("symlink", result["message"])
        self.assertEqual(list(self.home.iterdir()), [])

    def test_symlink_destination_is_never_followed(self):
        (self.home / ".local").symlink_to(Path(self.tmp.name))
        rc, result = self.run_stage("--apply")
        self.assertEqual(rc, 2)
        self.assertIn("symlink", result["message"])
        self.assertFalse((Path(self.tmp.name) / "bin").exists())


if __name__ == "__main__":
    unittest.main()
