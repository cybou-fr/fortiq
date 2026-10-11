"""Contract tests: real Linux process boundaries, fake llama-cli, no GGUF download."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
CLI = ROOT / "ai"
FAKE = """#!/usr/bin/env python3
import os, sys
from pathlib import Path
args = sys.argv[1:]
assert '--file' in args, args
assert '--n-gpu-layers' in args and args[args.index('--n-gpu-layers') + 1] == '0'
prompt_file = args[args.index('--file') + 1]
assert prompt_file.startswith('/proc/self/fd/'), prompt_file
content = Path(prompt_file).read_text()
assert 'UNTRUSTED DATA BEGIN' in content
assert 'SECRET_PLACEHOLDER' not in ' '.join(args)
with open(os.environ['AI_TEST_COUNTER'], 'a') as f:
    f.write('1\\n')
print('Fake diagnosis: ' + content.split('UNTRUSTED DATA BEGIN')[1][:70].strip())
"""


class CliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.model = base / "tiny.gguf"
        self.model.write_bytes(b"GGUF fake fixture")
        self.llama = base / "fake-llama"
        self.llama.write_text(FAKE)
        self.llama.chmod(0o755)
        self.counter = base / "invocations"
        self.cache_dir = base / "cache"
        self.env = os.environ.copy()
        self.env.update({
            "AI_MODEL": str(self.model),
            "AI_LLAMA_BIN": str(self.llama),
            "AI_CACHE_DIR": str(self.cache_dir),
            "AI_MIN_TOTAL_MB": "1",
            "AI_RESERVE_MB": "0",
            "AI_TEST_COUNTER": str(self.counter),
        })

    def run_ai(self, *args, message="", env=None):
        r = subprocess.run([sys.executable, str(CLI), *args],
                           input=message, text=True, capture_output=True,
                           env=self.env if env is None else env, timeout=15)
        try:
            value = json.loads(r.stdout)
        except Exception:
            self.fail(f"Invalid JSON: {r.stdout!r}; stderr={r.stderr!r}")
        return r, value

    def count(self):
        return len(self.counter.read_text().splitlines()) if self.counter.exists() else 0

    def test_no_model_or_runtime_is_safe(self):
        env = self.env.copy()
        env.pop("AI_MODEL")
        r, value = self.run_ai("status", "--json", env=env)
        self.assertEqual(r.returncode, 0)
        self.assertFalse(value["available"])
        self.assertEqual(value["reason"], "model_missing")
        r, value = self.run_ai("ask", "--stdin", "--json", message="hello", env=env)
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(value["reason"], "model_missing")

    def test_status_does_not_load_model_or_create_cache(self):
        r, value = self.run_ai("status", "--json")
        self.assertEqual(r.returncode, 0)
        self.assertTrue(value["available"])
        self.assertEqual(self.count(), 0)
        self.assertFalse(self.cache_dir.exists())

    def test_unconfigured_cache_is_off_by_default(self):
        for _ in range(2):
            r, value = self.run_ai("ask", "--stdin", "--json", message="hello")
            self.assertEqual(r.returncode, 0)
            self.assertFalse(value["cached"])
        self.assertEqual(self.count(), 2)
        self.assertEqual(list(self.cache_dir.glob("*.json")), [])

    def test_exact_cache_hit_skips_llama_entirely(self):
        for expected in (False, True):
            r, value = self.run_ai("ask", "--stdin", "--json", "--cache",
                                   "--context-id", "server:A", message="what is systemd?")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(value["cached"], expected)
        self.assertEqual(self.count(), 1)
        self.assertEqual(self.cache_dir.stat().st_mode & 0o777, 0o700)
        self.assertEqual(next(self.cache_dir.glob("*.json")).stat().st_mode & 0o777, 0o600)

    def test_context_and_model_changes_invalidate_cache(self):
        for context in ("one", "two", "one"):
            self.run_ai("ask", "--stdin", "--cache", "--context-id", context, message="same")
        self.assertEqual(self.count(), 2)
        self.model.write_bytes(b"GGUF changed")
        _, value = self.run_ai("ask", "--stdin", "--cache", "--context-id", "one", message="same")
        self.assertFalse(value["cached"])
        self.assertEqual(self.count(), 3)

    def test_sensitive_explain_needs_explicit_consent(self):
        r, value = self.run_ai("explain", "--stdin", "--cache", message="SECRET_PLACEHOLDER")
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(value["reason"], "cache_consent_required")
        self.assertEqual(self.count(), 0)
        r, value = self.run_ai("explain", "--stdin", message="SECRET_PLACEHOLDER")
        self.assertEqual(r.returncode, 0)
        self.assertFalse(value["cached"])
        self.assertEqual(list(self.cache_dir.glob("*.json")), [])
        r, value = self.run_ai("explain", "--stdin", "--cache", "--cache-sensitive",
                               message="SECRET_PLACEHOLDER")
        self.assertEqual(r.returncode, 0)
        self.assertFalse(value["cached"])

    def test_cache_clear(self):
        self.run_ai("ask", "--stdin", "--cache", message="test")
        self.assertEqual(self.count(), 1)
        _, status = self.run_ai("cache", "status", "--json")
        self.assertEqual(status["entries"], 1)
        _, cleared = self.run_ai("cache", "clear")
        self.assertTrue(cleared["cleared"])
        _, status = self.run_ai("cache", "status", "--json")
        self.assertEqual(status["entries"], 0)

    def test_invalid_input_is_not_cached_or_executed(self):
        r, value = self.run_ai("ask", "--stdin", "--cache", message="x" * 9000)
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(value["reason"], "invalid_input")
        self.assertEqual(self.count(), 0)

    def test_setup_never_installs_automatically(self):
        r, value = self.run_ai("setup", "--check", "--json")
        self.assertEqual(r.returncode, 0)
        self.assertTrue(value["available"])
        r, value = self.run_ai("setup")
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(value["reason"], "not_implemented")

    def test_timeout_kills_process_and_does_not_cache_failure(self):
        # Deliberately use a slow mock to exercise cancellation without a real LLM.
        self.llama.write_text("#!/usr/bin/env python3\nimport time\ntime.sleep(20)\n")
        env = self.env.copy()
        env["AI_TIMEOUT"] = "1"
        r, value = self.run_ai("ask", "--stdin", "--cache", message="hello", env=env)
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(value["reason"], "timeout")
        self.assertEqual(list(self.cache_dir.glob("*.json")), [])


if __name__ == "__main__":
    unittest.main()
