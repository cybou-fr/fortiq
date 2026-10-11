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

    def test_expired_entry_is_never_reused(self):
        self.run_ai("explain", "--stdin", "--cache", "--cache-sensitive",
                    message="journal entry")
        self.assertEqual(self.count(), 1)
        entry = next(self.cache_dir.glob("*.json"))
        record = json.loads(entry.read_text())
        record["created_epoch"] = 1  # well before the ten-minute TTL
        entry.write_text(json.dumps(record))
        _, answer = self.run_ai("explain", "--stdin", "--cache",
                                "--cache-sensitive", message="journal entry")
        self.assertFalse(answer["cached"])
        self.assertEqual(self.count(), 2)

    def test_two_identical_simultaneous_requests_only_launch_one_model(self):
        from concurrent.futures import ThreadPoolExecutor
        def ask(_):
            return self.run_ai("ask", "--stdin", "--cache", message="shared question")
        with ThreadPoolExecutor(max_workers=2) as pool:
            replies = list(pool.map(ask, (1, 2)))
        self.assertEqual([r.returncode for r, _ in replies], [0, 0])
        self.assertEqual(self.count(), 1)
        self.assertEqual(sorted(answer["cached"] for _, answer in replies),
                         [False, True])

    def test_digest_metadata_is_reused_and_invalidated(self):
        self.run_ai("ask", "--stdin", "--cache", message="abc")
        manifests = list(self.cache_dir.glob(".modelhash-*"))
        self.assertEqual(len(manifests), 1)
        first = json.loads(manifests[0].read_text())
        self.run_ai("ask", "--stdin", "--cache", message="abc")
        second = json.loads(manifests[0].read_text())
        self.assertEqual(first, second)
        self.assertEqual(self.count(), 1)
        self.model.write_bytes(b"GGUF fixture changed despite cache")
        self.run_ai("ask", "--stdin", "--cache", message="abc")
        third = json.loads(manifests[0].read_text())
        self.assertNotEqual(first["sha256"], third["sha256"])
        self.assertEqual(self.count(), 2)

    def test_excessive_llama_stdout_is_stopped_without_caching(self):
        self.llama.write_text(
            "#!/usr/bin/env python3\n"
            "import sys\n"
            "sys.stdout.write('A' * 1000000)\n"
            "sys.stdout.flush()\n"
        )
        r, answer = self.run_ai("ask", "--stdin", "--cache", message="boom")
        self.assertEqual(r.returncode, 2)
        self.assertEqual(answer["reason"], "output_too_large")
        self.assertEqual(list(self.cache_dir.glob("*.json")), [])

    def test_cancelled_parent_kills_model_child(self):
        import signal
        import time
        pidfile = Path(self.temp.name) / "child.pid"
        self.llama.write_text(
            "#!/usr/bin/env python3\n"
            "import os, time\n"
            f"open({str(pidfile)!r}, 'w').write(str(os.getpid()))\n"
            "time.sleep(20)\n"
        )
        process = subprocess.Popen(
            [sys.executable, str(CLI), "ask", "--stdin", "--json"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, env=self.env, text=True,
        )
        try:
            process.stdin.write("hello")
            process.stdin.close()
            until = time.monotonic() + 5
            while not pidfile.exists() and time.monotonic() < until:
                time.sleep(0.05)
            self.assertTrue(pidfile.exists(), "mock inference was not started")
            child_pid = int(pidfile.read_text())
            os.kill(process.pid, signal.SIGKILL)
            process.wait(timeout=5)
            # PR_SET_PDEATHSIG must stop the separate inference process.
            until = time.monotonic() + 5
            while time.monotonic() < until:
                procstat = Path(f"/proc/{child_pid}/stat")
                if not procstat.exists() or procstat.read_text().split(") ")[1].startswith("Z"):
                    break  # Reaped or zombie awaiting init's wait().
                time.sleep(0.05)
            else:
                self.fail("llama CPU process survived the parent SSH CLI being killed")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)

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
