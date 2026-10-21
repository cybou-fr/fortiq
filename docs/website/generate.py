"""Generate FORTIQ's three static localized pages from one shared template and copy.

Run: python3 docs/website/generate.py
No external dependencies or network. Generated files under www/ are committed.
Visuals on the site are illustrative: UI fidelity is tested separately in Qt.
"""
from html import escape
from json import loads
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
COPY = loads((BASE / "copy.json").read_text(encoding="utf-8"))
TEMPLATE = (BASE / "site-template.html").read_text(encoding="utf-8")

def render(language: str, content: dict) -> str:
    prefix = "" if language == "en" else "../"
    values = {**content, "lang": language, "prefix": prefix,
              "enCurrent": 'aria-current="page"' if language == "en" else "",
              "frCurrent": 'aria-current="page"' if language == "fr" else "",
              "ruCurrent": 'aria-current="page"' if language == "ru" else ""}
    for tag, arr in (("filesTag", content["filesTags"]), ("logsTag", content["logsTags"])):
        values.update({f"{tag}{i}": v for i, v in enumerate(arr)})
    result = TEMPLATE
    for key, value in values.items():
        result = result.replace("{{{" + key + "}}}", str(value))
        result = result.replace("{{" + key + "}}", escape(str(value), quote=True))
    if "{{" in result:
        raise ValueError("Unexpanded placeholder in " + language)
    return result

for language, content in COPY.items():
    target = ROOT / "www" / (language if language != "en" else "") / "index.html"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(render(language, content), encoding="utf-8")
    print("Wrote", target.relative_to(ROOT))
