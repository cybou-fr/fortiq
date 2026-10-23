#!/bin/sh
# Builds the fuzz targets with clang + libFuzzer + ASan/UBSan and runs every target for a while.
# Run on a DISPOSABLE Linux (it installs nothing itself): needs clang, cmake, ninja, qt6-base-dev, libclang-rt-dev.
#   sh tool/fuzz_linux.sh [seconds per target, default 60] [build dir, default /tmp/fortiq-fuzz-build]
# Findings: crash-*/oom-*/timeout-* files and the log are left in <build dir>/artifacts; docs/FUZZING.md says what to do next.
set -eu
SECS=${1:-60}
BUILD=${2:-/tmp/fortiq-fuzz-build}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export QT_QPA_PLATFORM=offscreen
CC=clang CXX=clang++ cmake -S "$ROOT/fuzz" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build "$BUILD"
rm -rf "$BUILD/artifacts"; mkdir -p "$BUILD/artifacts"
# 1. deterministic driver under sanitizers (all seeds, many mutations)
"$BUILD/fortiq_fuzz_smoke" "$ROOT/tests/fixtures/mail" "$ROOT/tests/fixtures/network" "$ROOT/tests/fixtures/system" "$ROOT/fuzz/seeds" --iterations 100000
# 2. libFuzzer: one run per target; its first input byte selects the target, so seeds are copied with that prefix
N=$("$BUILD/fortiq_fuzz_smoke" --list 2>/dev/null | wc -l)
status=0
i=0
while [ "$i" -lt "$N" ]; do
    corpus="$BUILD/corpus-$i"; rm -rf "$corpus"; mkdir -p "$corpus"
    for f in "$ROOT"/fuzz/seeds/* "$ROOT"/tests/fixtures/mail/*.json "$ROOT"/tests/fixtures/mail/*.txt "$ROOT"/tests/fixtures/network/* "$ROOT"/tests/fixtures/system/*; do
        [ -f "$f" ] || continue
        { printf "$(printf '\%03o' "$i")"; cat "$f"; } > "$corpus/$(basename "$f")"
    done
    echo "== target $i for ${SECS}s"
    "$BUILD/fortiq_fuzz" -max_total_time="$SECS" -max_len=20000 -timeout=20 -rss_limit_mb=2048 -artifact_prefix="$BUILD/artifacts/t$i-" "$corpus" 2>&1 | tail -4 || status=1
    i=$((i + 1))
done
ls "$BUILD/artifacts"
[ -z "$(ls "$BUILD/artifacts")" ] || status=1
exit $status
