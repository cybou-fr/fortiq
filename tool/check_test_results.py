#!/usr/bin/env python3
"""Reads the QtTest result files (`-o file,txt`) of a test run and fails the gate on anything that is not a plain pass.

A skipped test is NOT a pass. A result file without a "Totals:" line means the test executable crashed or was killed. This script exists
because `ctest` reports a test that only skipped as "Passed".

  python tool/check_test_results.py --results-dir build-msvc [--waivers tool/gate_waivers.json] [--scope offline|live|release] [--min-files N]
  python tool/check_test_results.py --self-test

Rules:
  * any failed test, any result file without Totals, no result file at all (or fewer than --min-files): FAIL
  * a skipped test passes only when tool/gate_waivers.json holds a waiver with a reason, written as "<Class>::<test>" (the "fortiq::"
    namespace and the "()" are dropped); the waiver is printed so it is visible, never silent
  * every waiver names the scopes in which it holds ("scopes": default all): "offline" = the ctest run of tool/ci_check.py (no Linux fixture),
    "live" = tool/run_live_tests.py against the disposable fixture, "release" = the production gate, which accepts only waivers valid in
    "release" - so a test that needs the fixture or a second host must really have run before a release
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WAIVERS = ROOT / "tool" / "gate_waivers.json"
SCOPES = ["offline", "live", "release"]
TOTALS = re.compile(r"^Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped")


def skip_key(line: str) -> str:
    """'SKIP   : fortiq::CredentialsTest::rememberWithoutStoreIsRefused() reason' -> 'CredentialsTest::rememberWithoutStoreIsRefused'."""
    body = re.sub(r"^SKIP\s*:\s*", "", line).strip()
    name = body.split("(")[0].split()[0] if body else ""
    return name.removeprefix("fortiq::")


def load_waivers(path: Path) -> dict[str, dict]:
    if not path.is_file():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    for key, waiver in data.items():
        if not isinstance(waiver, dict) or not str(waiver.get("reason", "")).strip():
            raise SystemExit(f"{path}: waiver '{key}' has no reason")
    return data


def evaluate(files: dict[str, str], waivers: dict[str, dict], scope: str) -> tuple[list[str], list[str], dict[str, int]]:
    """files: name -> text of the result file. Returns (problems, waived notes, counts)."""
    problems: list[str] = []
    notes: list[str] = []
    counts = {"passed": 0, "failed": 0, "skipped": 0, "files": len(files)}
    for name in sorted(files):
        text = files[name]
        totals = next((m for m in (TOTALS.match(l) for l in text.splitlines()) if m), None)
        if not totals:
            problems.append(f"{name}: no 'Totals:' line - the test program crashed or was killed")
            continue
        passed, failed, skipped = (int(x) for x in totals.groups())
        counts["passed"] += passed
        counts["failed"] += failed
        counts["skipped"] += skipped
        if failed:
            problems.append(f"{name}: {failed} test(s) failed")
        for line in text.splitlines():
            if not line.startswith("SKIP"):
                continue
            key = skip_key(line)
            waiver = waivers.get(key)
            reason = re.sub(r"^SKIP\s*:\s*", "", line).strip()
            if waiver is None:
                problems.append(f"{name}: SKIPPED without a waiver: {reason}")
            elif scope not in waiver.get("scopes", SCOPES):
                problems.append(f"{name}: SKIPPED, and its waiver does not hold in scope '{scope}' ({waiver['reason']}): {reason}")
            else:
                notes.append(f"{name}: skipped, waived - {waiver['reason']}")
    return problems, notes, counts


def self_test() -> int:
    ok_text = "Config: x\nPASS   : A::b()\nTotals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 5ms\n"
    skip_text = ("SKIP   : fortiq::CredentialsTest::rememberWithoutStoreIsRefused() A protected store exists\n"
                 "Totals: 2 passed, 0 failed, 1 skipped, 0 blacklisted, 5ms\n")
    fail_text = "FAIL!  : A::b() boom\nTotals: 1 passed, 1 failed, 0 skipped, 0 blacklisted, 5ms\n"
    crash_text = "PASS   : A::b()\n"
    waive = {"CredentialsTest::rememberWithoutStoreIsRefused": {"reason": "platform-conditional"}}
    dev_only = {"CredentialsTest::rememberWithoutStoreIsRefused": {"reason": "needs a second host", "scopes": ["offline", "live"]}}
    cases = [
        ("plain pass", {"a": ok_text}, {}, "offline", True),
        ("skip without waiver", {"a": skip_text}, {}, "offline", False),
        ("skip with waiver", {"a": skip_text}, waive, "release", True),
        ("narrow waiver inside its scope", {"a": skip_text}, dev_only, "live", True),
        ("narrow waiver outside its scope (the release gate)", {"a": skip_text}, dev_only, "release", False),
        ("a failing test", {"a": fail_text}, waive, "offline", False),
        ("a crashed program (no Totals)", {"a": crash_text}, waive, "offline", False),
        ("a pass hiding behind another crash", {"a": ok_text, "b": crash_text}, {}, "offline", False),
    ]
    bad = []
    for label, files, waivers, scope, want_ok in cases:
        problems, _, _ = evaluate(files, waivers, scope)
        if (not problems) != want_ok:
            bad.append(f"{label}: expected {'pass' if want_ok else 'failure'}, problems={problems}")
    if skip_key("SKIP   : fortiq::SshIntegrationTest::twoHostsNeverMix() second fixture not configured") != "SshIntegrationTest::twoHostsNeverMix":
        bad.append("skip_key does not extract the test name")
    if bad:
        print("SELF-TEST FAILED:\n" + "\n".join(bad))
        return 1
    print("SELF-TEST OK: skips without a waiver, waivers outside their scope, failures and crashed programs are all refused; waived skips and plain passes are accepted.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--results-dir", help="folder searched (recursively) for *results*.txt written by the test programs")
    ap.add_argument("--waivers", default=str(DEFAULT_WAIVERS))
    ap.add_argument("--scope", choices=SCOPES, default="offline", help="which waivers hold (see above)")
    ap.add_argument("--min-files", type=int, default=1, help="fewer result files than this is a failure (a gate that ran nothing proves nothing)")
    ap.add_argument("--self-test", action="store_true")
    opts = ap.parse_args()
    if opts.self_test:
        return self_test()
    if not opts.results_dir:
        ap.error("--results-dir is required")
    paths = sorted(Path(opts.results_dir).rglob("*results*.txt"))
    files = {p.name: p.read_text(encoding="utf-8", errors="replace") for p in paths}
    problems, notes, counts = evaluate(files, load_waivers(Path(opts.waivers)), opts.scope)
    if len(files) < opts.min_files:
        problems.append(f"only {len(files)} result file(s) found in {opts.results_dir}, expected at least {opts.min_files}")
    print(f"{counts['files']} result files: {counts['passed']} passed, {counts['failed']} failed, {counts['skipped']} skipped")
    for n in notes:
        print("waived:", n)
    if problems:
        print("\nGATE FAILED:")
        for p in problems:
            print(" -", p)
        return 1
    print("OK: every test program ran to the end, nothing failed, every skip has a waiver.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
