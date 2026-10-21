#!/usr/bin/env python3
"""Compare a FORTIQ Desktop Shell capture with the owner's master PNG.

Optionally runs the capture first:

    python3 tool/compare_shell_screenshot.py --run build-preview/fortiq_shell_preview.exe

or compares an existing capture:

    python3 tool/compare_shell_screenshot.py --capture out/shell.png

Writes into --out (default build-preview/compare): side-by-side.png,
diff.png (amplified absolute difference), overlay.png (50% blend) and
report.json with per-region metrics for the UI_SPEC_V1 §1 regions.

Metrics are evidence for tuning, not an acceptance verdict: the spec
requires a human side-by-side review before fidelity can be claimed.
Requires Pillow.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parent.parent
MASTER = ROOT / "docs/design/reference/fortiq-shell-master.png"
MASTER_SHA256 = "852b5944c61ebb8551cc10dadeda47e7924b92baec0fd4b7758ffe2a5c6ffaef"
SIZE = (1536, 1024)

# UI_SPEC_V1 §1 reference bounding regions (left, top, right, bottom).
REGIONS = {
    "TOP": (0, 0, 1536, 47),
    "RAIL": (4, 59, 88, 940),
    "CLOCK": (119, 70, 361, 275),
    "FILES": (471, 72, 1189, 475),
    "FILES_SIDEBAR": (472, 73, 628, 475),
    "LAUNCHER": (97, 498, 536, 941),
    "BROWSER": (554, 512, 1204, 912),
    "CONTROL": (1215, 64, 1526, 744),
    "WORKSPACES": (1215, 765, 1526, 894),
    "DOCK": (384, 948, 1077, 1017),
    "TRASH": (1081, 948, 1153, 1019),
}
# Pixels whose channel-max delta exceeds this count as "different".
THRESHOLD = 32


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def region_metrics(master: Image.Image, capture: Image.Image, box) -> dict:
    a = master.crop(box)
    b = capture.crop(box)
    diff = ImageChops.difference(a, b)
    mean = ImageStat.Stat(diff).mean
    peak = diff.convert("L").point(lambda v: 255 if v > THRESHOLD else 0)
    # Channel max approximated through the luminance of the difference.
    changed = ImageStat.Stat(peak).mean[0] / 255.0
    ma = ImageStat.Stat(a).mean
    mb = ImageStat.Stat(b).mean
    return {
        "box": list(box),
        "mean_abs_diff": round(sum(mean) / 3, 2),
        "changed_ratio": round(changed, 4),
        "master_mean_rgb": [round(v) for v in ma],
        "capture_mean_rgb": [round(v) for v in mb],
    }


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", help="fortiq_shell_preview (or fortiq) executable to capture with")
    parser.add_argument("--capture", help="existing 1536x1024 capture PNG")
    parser.add_argument("--wallpaper", help="wallpaper passed to the capture run")
    parser.add_argument("--out", default=str(ROOT / "build-preview/compare"))
    args = parser.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    if MASTER.exists() is False:
        print(f"master reference missing: {MASTER}", file=sys.stderr)
        return 2
    master_hash = sha256(MASTER)
    if master_hash != MASTER_SHA256:
        print(f"master hash mismatch: {master_hash}", file=sys.stderr)
        return 2

    if args.run:
        capture_path = out / "capture.png"
        cmd = [str(Path(args.run).resolve()), "--reference-fixture", "--screenshot", str(capture_path)]
        if args.wallpaper:
            cmd += ["--wallpaper", args.wallpaper]
        result = subprocess.run(cmd, timeout=60)
        if result.returncode != 0:
            print(f"capture failed with exit code {result.returncode}", file=sys.stderr)
            return 3
    elif args.capture:
        capture_path = Path(args.capture)
    else:
        parser.error("pass --run or --capture")

    master = Image.open(MASTER).convert("RGB")
    capture = Image.open(capture_path).convert("RGB")
    if capture.size != SIZE:
        print(f"capture is {capture.size[0]}x{capture.size[1]}, expected 1536x1024", file=sys.stderr)
        return 4

    diff = ImageChops.difference(master, capture)
    diff.point(lambda v: min(255, v * 3)).save(out / "diff.png")
    Image.blend(master, capture, 0.5).save(out / "overlay.png")
    side = Image.new("RGB", (SIZE[0] * 2, SIZE[1]))
    side.paste(master, (0, 0))
    side.paste(capture, (SIZE[0], 0))
    side.save(out / "side-by-side.png")

    report = {
        "master": str(MASTER.relative_to(ROOT)),
        "master_sha256": master_hash,
        "capture": str(capture_path),
        "threshold": THRESHOLD,
        "whole": region_metrics(master, capture, (0, 0, *SIZE)),
        "regions": {name: region_metrics(master, capture, box) for name, box in REGIONS.items()},
        "verdict": "not accepted: metrics only; owner side-by-side review required (UI_SPEC_V1 §6)",
    }
    (out / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")

    print(f"{'region':<15}{'meanΔ':>8}{'changed':>9}   master→capture mean RGB")
    for name, m in [("WHOLE", report["whole"]), *report["regions"].items()]:
        print(f"{name:<15}{m['mean_abs_diff']:>8}{m['changed_ratio']:>9.1%}   {m['master_mean_rgb']} → {m['capture_mean_rgb']}")
    print(f"artifacts: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
