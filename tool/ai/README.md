# Linux `ai` CLI — implementation slice 0.1.0

This is the **implemented**, optional, CPU-only Linux executable in
[`tool/ai/ai`](ai), with fake-runtime contract tests in
[`test_ai.py`](test_ai.py). GUI actions in **Logs** and **Services**
call it over SSH via `AiService` and show advisory output.

**Not yet provided:** automatic llama.cpp binary/GGUF installation,
selection of a production GGUF, proof of speed on a real VPS, streaming
generation, external AI, or production CPU benchmarking. `ai setup`
currently returns a JSON `not_implemented` error deliberately; its
`--check` mode is read-only.

## One-time manual installation on the Linux host

Run from a checked-out FORTIQ repository *on the Linux server*, or securely
transfer the vetted `tool/ai/ai` file using the existing SSH connection:

```bash
install -d -m 700 ~/.local/bin ~/.local/share/ai
install -m 755 tool/ai/ai ~/.local/bin/ai
export PATH="$HOME/.local/bin:$PATH"
ai status --json
```

Prepare the existing, verified **CPU-capable `llama-cli` binary** on the
Linux host and provide a verified, appropriately licensed GGUF file at
`~/.local/share/ai/model.gguf`. Use a model chosen from actual CPU-only
measurements (not a default download). The binary must be discoverable as
`llama-cli` in the server's PATH; if using a custom path, set
`AI_LLAMA_BIN=/absolute/path/to/llama-cli` in the server process environment.

There is **no `curl | sh` bootstrap, sudo, auto-pull, or network service**.
To verify the actual llama.cpp CLI options and run in a disposable VM:

```bash
llama-cli --version
llama-cli --help
ai setup --check --json
printf 'Explain why nginx exited with code 1' | ai explain --stdin --json
```

The GUI's SSH exec prepends `$HOME/.local/bin` to PATH, and the CLI looks
for its model at `~/.local/share/ai/model.gguf` unless `AI_MODEL` is set.
No `AI_MODEL` or `AI_LLAMA_BIN` setting is required if both use those
default locations. Rebooting or reconnecting SSH does not launch AI.

If resource gates fail, `ai status --json` returns
`available:false` and the feature is unavailable; all ordinary FORTIQ
tools are unaffected. By default, 1–2GB VPS do not run inference.
These are conservative screening rules, **not a proof** that a particular
GGUF is usable on a 4GB VPS.

## Cache is off until explicitly requested

An exact-cache hit is returned **before starting llama.cpp**. A changed
prompt/context/model hash invalidates the entry. Cache lives exclusively
in the invoking Linux user's protected `~/.cache/ai`, with permissions
0700/0600, TTL 10 minutes for `explain`, 24 hours for `ask`,
and a 16 MiB size budget.

```bash
printf 'What does journalctl show?' | ai ask --stdin --json --cache
printf 'Selected journal lines' | ai explain --stdin --json --cache --cache-sensitive
ai cache status --json
ai cache clear
```

The second form has an extra **explicit sensitive-data consent** flag.
The FORTIQ GUI exposes an unchecked checkbox explaining that logs may
contain secrets. Never cache private credentials or key contents.
Local file permissions **do not constitute encryption at rest**.
Cached advice is not live system verification.

## Tests

```bash
python3 -m unittest discover -s tool/ai -p 'test_*.py' -v
```

The tests run against a fake llama binary and temporary GGUF; they verify
the memfd prompt handoff, exact-cache hit without launching the fake model,
permission isolation, invalidation, timeout and non-installing setup.
GitHub Actions workflow `ai-cli.yml` runs these tests. A separate Windows
workflow checks `AiService` and its GUI integration.

## Operational restrictions

- Only Linux, Python 3 stdlib, and a **verified llama.cpp CLI**.
- No daemon, no AI HTTP port, no auto-download, no API billing.
- Prompts are passed by SSH stdin → Linux anonymous `memfd` → llama.cpp
  `--file /proc/self/fd/N`. They are not passed as command-line arguments.
- CLI can only explain. It does not execute proposed commands, repair
  services, install packages or modify system configuration.
- Concurrent inference for one Linux user is serialized with a local lock;
  **CPU quota, multi-user host isolation, model quality and version
  compatibility remain to be tested on a disposable VPS before production**.

Contract: [`docs/AI_CLI.md`](../../docs/AI_CLI.md).
