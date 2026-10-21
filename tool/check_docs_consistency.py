#!/usr/bin/env python3
"""FORTIQ documentation-only integrity check. No dependencies, no network."""
from pathlib import Path
import hashlib
import re
import runpy
import sys

ROOT = Path(__file__).resolve().parents[1]
MANDATORY = (
    "README.md",
    "AGENTS.md",
    "docs/SSH_FRONTEND_CONTRACT.md",
    "docs/PRODUCT_SCOPE.md",
    "docs/ARCHITECTURE.md",
    "docs/DEVELOPMENT_MATRIX.md",
    "docs/QT_MIGRATION.md",
    "docs/FORTIQ_DESKTOP_BLUEPRINT.md",
    "docs/UI_SPEC_V1.md",
    "docs/SMART_FILES_SPEC.md",
    "docs/LOGS_INTELLIGENCE_SPEC.md",
    "docs/DOCUMENTATION_AUTHORITY.md",
)
LINK = re.compile(r'!?\[[^]\n]*\]\(([^)\n]+)\)')
OBSOLETE = (
    "Кода нет. Прежний прототип",
    "Статус на 18 октября 2026: **ничего не реализовано.**",
    "клиент на C++/Qt ещё не начат",
    "Тип интерфейса (Widgets или Qt Quick)",
    "Use it as layout/atmosphere guidance",
    "Provide explicit visual style comparable to the design reference",
)

errors = []
for item in MANDATORY:
    if not (ROOT / item).is_file():
        errors.append(f"missing normative document: {item}")

for document in ROOT.rglob("*.md"):
    if any(part in (".git", "build", "node_modules") for part in document.parts):
        continue
    raw = document.read_text(encoding="utf-8")
    relative_path = document.relative_to(ROOT)
    for item in LINK.finditer(raw):
        href = item.group(1).strip()
        if href.startswith(("http:", "https:", "mailto:", "#", "data:")):
            continue
        filename = href.split("#", 1)[0].split("?", 1)[0]
        if not filename:
            continue
        candidate = (document.parent / filename).resolve()
        if not candidate.is_relative_to(ROOT.resolve()) or not candidate.is_file():
            errors.append(f"{relative_path}: broken relative link {href}")
    if str(relative_path) in MANDATORY:
        for phrase in OBSOLETE:
            if phrase in raw:
                errors.append(f"{relative_path}: stale declaration {phrase!r}")

for target in ("README.md", "AGENTS.md", "docs/PRODUCT_SCOPE.md",
               "docs/ARCHITECTURE.md", "docs/DEVELOPMENT_MATRIX.md",
               "docs/QT_MIGRATION.md", "docs/FORTIQ_DESKTOP_BLUEPRINT.md"):
    text = (ROOT / target).read_text(encoding="utf-8")
    for name in ("UI_SPEC_V1.md", "SMART_FILES_SPEC.md", "LOGS_INTELLIGENCE_SPEC.md"):
        if name not in text:
            errors.append(f"{target}: missing contract link {name}")

# Verify that the site is reproducibly generated from the shared sources.
website = runpy.run_path(str(ROOT / "docs/website/generate.py"))
for lang, content in website["COPY"].items():
    path = ROOT / "www" / (lang if lang != "en" else "") / "index.html"
    if not path.is_file():
        errors.append(f"missing generated website: {path.relative_to(ROOT)}")
    elif path.read_text(encoding="utf-8") != website["render"](lang, content):
        errors.append(f"website drift: regenerate {path.relative_to(ROOT)}")

image = ROOT / "docs/design/reference/fortiq-shell-master.png"
site_image = ROOT / "www/assets/fortiq-shell-master.png"
expected_sha = "852b5944c61ebb8551cc10dadeda47e7924b92baec0fd4b7758ffe2a5c6ffaef"
if image.exists() != site_image.exists():
    errors.append("master PNG must be imported into both docs/reference and www/assets")
elif image.is_file():
    for f in (image, site_image):
        actual_sha = hashlib.sha256(f.read_bytes()).hexdigest()
        if actual_sha != expected_sha:
            errors.append(f"non-master/modified PNG {f.relative_to(ROOT)}: {actual_sha}")
else:
    print("WARN: exact master screenshot not yet in Git; pixel-diff gate BLOCKED")

if errors:
    print("\n".join("FAIL: " + e for e in errors), file=sys.stderr)
    sys.exit(1)
print("PASS: normative docs present, relative links resolve, core contracts agree")
