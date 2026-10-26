"""Generate FORTIQ's static localized website (EN / FR / RU, four pages each) from copy.json.

Run: python3 docs/website/generate.py
Python standard library only, no network. Generated files under www/ are committed.
The hero shows the approved design reference (not a running Qt screenshot); copy follows
docs/RELEASE_SCOPE_1_0.md and never presents targets as implemented.
"""
from hashlib import sha256
from html import escape
from json import loads
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
COPY = loads((BASE / "copy.json").read_text(encoding="utf-8"))
PAGES = ("index", "security", "download", "faq")
MASTER_SHA256 = "852b5944c61ebb8551cc10dadeda47e7924b92baec0fd4b7758ffe2a5c6ffaef"
LANGS = ("en", "fr", "ru")

CSP = ("default-src 'none'; img-src 'self'; style-src 'self'; script-src 'self'; "
       "base-uri 'none'; form-action 'none'")

# 24x24 stroke icons, drawn for FORTIQ (no third-party icon sets).
ICONS = {
    "files": '<path d="M3 6.5A2.5 2.5 0 0 1 5.5 4H9l2 2.5h7.5A2.5 2.5 0 0 1 21 9v8.5a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 17.5z"/>',
    "terminal": '<rect x="3" y="4.5" width="18" height="15" rx="3"/><path d="m7.5 10 3 2.5-3 2.5M13 15h4"/>',
    "logs": '<path d="M4 6h16M4 11h10M4 16h13M4 21h7" /><circle cx="19" cy="17" r="2.5"/>',
    "mdok": '<rect x="3.5" y="4" width="17" height="16" rx="3"/><path d="M7 15.5v-7l2.5 3 2.5-3v7M16 8.5v7m0 0-1.8-1.8m1.8 1.8 1.8-1.8"/>',
    "mail": '<rect x="3" y="5.5" width="18" height="13" rx="3"/><path d="m4 8 8 5.5L20 8"/>',
    "browser": '<circle cx="12" cy="12" r="8.5"/><path d="M3.5 12h17M12 3.5c2.5 2.5 3.5 5.3 3.5 8.5s-1 6-3.5 8.5c-2.5-2.5-3.5-5.3-3.5-8.5s1-6 3.5-8.5z"/>',
    "system": '<rect x="6" y="6" width="12" height="12" rx="2.5"/><path d="M9.5 3v3m5-3v3m-5 12v3m5-3v3M3 9.5h3m-3 5h3m12-5h3m-3 5h3"/>',
    "databases": '<ellipse cx="12" cy="6" rx="7.5" ry="3"/><path d="M4.5 6v6c0 1.7 3.4 3 7.5 3s7.5-1.3 7.5-3V6M4.5 12v6c0 1.7 3.4 3 7.5 3s7.5-1.3 7.5-3v-6"/>',
    "lock": '<rect x="5" y="10.5" width="14" height="10" rx="2.5"/><path d="M8 10.5V8a4 4 0 0 1 8 0v2.5"/>',
    "check": '<path d="m5 12.5 4.5 4.5L19 7.5"/>',
    "dash": '<path d="M7 12h10"/>',
    "arrow": '<path d="M5 12h14m-5-5 5 5-5 5"/>',
    "moon": '<path d="M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z"/>',
    "windows": '<path d="M3.5 5.8 11 4.7v6.8H3.5zM12.5 4.5 20.5 3.3v8.2h-8zM3.5 12.5H11v6.8l-7.5-1.1zM12.5 12.5h8v8.2l-8-1.2z"/>',
    "android": '<path d="M5 17V10a7 7 0 0 1 14 0v7a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 5 17zM8.5 5.5 7 3m8.5 2.5L17 3"/><circle cx="9.5" cy="11" r=".8"/><circle cx="14.5" cy="11" r=".8"/>',
}


def icon(name: str, cls: str = "ic") -> str:
    return (f'<svg class="{cls}" viewBox="0 0 24 24" width="24" height="24" fill="none" '
            f'stroke="currentColor" stroke-width="1.6" stroke-linecap="round" '
            f'stroke-linejoin="round" aria-hidden="true">{ICONS[name]}</svg>')


def e(text: str) -> str:
    return escape(str(text), quote=True)


def lang_dir(lang: str) -> str:
    return "" if lang == "en" else lang + "/"


def page_name(page: str) -> str:
    return page + ".html"


def href(lang: str, page: str, target_lang: str | None = None, anchor: str = "") -> str:
    """Relative link from a page in `lang` to `page` in `target_lang` (default: same)."""
    up = "" if lang == "en" else "../"
    return up + lang_dir(target_lang or lang) + page_name(page) + anchor


def master_check() -> None:
    reference = ROOT / "www/assets/fortiq-shell-master.png"
    if reference.exists() and sha256(reference.read_bytes()).hexdigest() != MASTER_SHA256:
        raise ValueError("Invalid exact FORTIQ master image SHA-256")


def header(lang: str, ui: dict, page: str) -> str:
    up = "" if lang == "en" else "../"
    links = [("apps", href(lang, "index", anchor="#apps"), "index"),
             ("security", href(lang, "security"), "security"),
             ("download", href(lang, "download"), "download"),
             ("faq", href(lang, "faq"), "faq")]
    nav = "".join(
        f'<a href="{l}"{" aria-current=\"page\"" if p == page and k != "apps" else ""}>{e(ui["nav"][k])}</a>'
        for k, l, p in links)
    langs = "".join(
        f'<a href="{href(lang, page, code)}" lang="{code}" hreflang="{code}"'
        f'{" aria-current=\"true\"" if code == lang else ""}>{code.upper()}</a>' for code in LANGS)
    return (f'<header class="site-header"><div class="bar wrap">'
            f'<a class="brand" href="{href(lang, "index")}" aria-label="FORTIQ">'
            f'<img class="brand-mark" src="{up}assets/fortiq-icon.svg" width="28" height="28" alt="">'
            f'<span>FORTIQ</span></a>'
            f'<nav class="nav" aria-label="{e(ui["navLabel"])}">{nav}</nav>'
            f'<div class="tools"><div class="langs" role="group" aria-label="{e(ui["langLabel"])}">{langs}</div>'
            f'<button class="theme-btn" type="button" id="theme-toggle" hidden aria-label="{e(ui["theme"])}">'
            f'{icon("moon")}</button></div></div></header>')


def footer(lang: str, ui: dict) -> str:
    links = "".join(f'<a href="{href(lang, p)}">{e(ui["nav"][k])}</a>' for p, k in
                    (("security", "security"), ("download", "download"), ("faq", "faq")))
    return (f'<footer class="site-footer"><div class="wrap foot">'
            f'<div><p class="foot-line">{e(ui["footerLine"])}</p><p class="foot-note">{e(ui["footerNote"])}</p></div>'
            f'<div class="foot-links">{links}<a href="{href(lang, "index", anchor="#status")}">{e(ui["status"])}</a>'
            f'<a href="{href(lang, "faq", anchor="#license")}">{e(ui["license"])}</a>'
            f'<a href="#top">{e(ui["backTop"])}</a></div></div></footer>')


def cards(items: list, cls: str = "") -> str:
    body = "".join(f'<li class="card"><h3>{e(i["t"])}</h3><p>{e(i["d"])}</p></li>' for i in items)
    return f'<ul class="cards {cls}">{body}</ul>'


def section(s: dict, alt: bool = False) -> str:
    return (f'<section class="section{" alt" if alt else ""}" id="{e(s["id"])}"><div class="wrap">'
            f'<p class="eyebrow">{e(s["eyebrow"])}</p><h2>{e(s["title"])}</h2>'
            f'<p class="lead">{e(s["lead"])}</p>{cards(s["items"])}</div></section>')


def page_head(c: dict) -> str:
    return (f'<section class="page-head"><div class="wrap"><p class="eyebrow">{e(c["eyebrow"])}</p>'
            f'<h1>{c["h1Html"]}</h1><p class="lead big">{e(c["lead"])}</p></div></section>')


def cta(lang: str, text: str, label: str, page: str) -> str:
    return (f'<section class="section"><div class="wrap narrow center"><p class="lead">{e(text)}</p>'
            f'<a class="btn primary" href="{href(lang, page)}">{e(label)} {icon("arrow", "ic sm")}</a>'
            f'</div></section>')


def build_index(lang: str, ui: dict, c: dict) -> str:
    up = "" if lang == "en" else "../"
    hero_img = (f'<img class="shot" src="{up}assets/fortiq-shell-1536.webp" '
                f'srcset="{up}assets/fortiq-shell-768.webp 768w, {up}assets/fortiq-shell-1536.webp 1536w" '
                f'sizes="(min-width: 1100px) 1080px, 94vw" width="1536" height="1024" '
                f'alt="{e(c["heroAlt"])}" fetchpriority="high">')
    hero = (f'<section class="hero"><div class="wrap center">'
            f'<p class="eyebrow"><span class="dot"></span>{e(c["eyebrow"])}</p>'
            f'<h1>{c["h1Html"]}</h1><p class="lead big">{e(c["lead"])}</p>'
            f'<div class="actions"><a class="btn primary" href="{href(lang, "download")}">{e(c["ctaPrimary"])}</a>'
            f'<a class="btn ghost" href="{href(lang, "security")}">{e(c["ctaSecondary"])} {icon("arrow", "ic sm")}</a></div>'
            f'<p class="hero-status"><span class="amber"></span>{e(c["heroStatus"])}</p></div>'
            f'<figure class="stage wrap"><div class="glow" aria-hidden="true"></div>{hero_img}'
            f'<figcaption>{e(c["heroCaption"])} <a href="{up}assets/fortiq-shell-master.png">{e(c["heroOriginal"])}</a></figcaption>'
            f'</figure></section>')
    pillars = "".join(f'<li><span class="num" aria-hidden="true">{n}</span><h3>{e(p["t"])}</h3><p>{e(p["d"])}</p></li>'
                      for n, p in enumerate(c["pillars"], 1))
    pillar_sec = f'<section class="section tight"><div class="wrap"><ul class="pillars">{pillars}</ul></div></section>'
    tiles = ""
    order = ["files", "logs", "terminal", "mdok", "system", "mail", "browser", "databases"]
    for a in sorted(c["apps"], key=lambda a: order.index(a["id"])):
        tiles += (f'<li class="tile {a["id"]}"><div class="tile-top"><span class="tile-ic">{icon(a["id"])}</span>'
                  f'<span class="badge {a["status"]}">{e(ui["badge"][a["status"]])}</span></div>'
                  f'<h3>{e(a["name"])}</h3><p>{e(a["d"])}</p>'
                  f'<p class="limit"><b>{e(ui["limitLabel"])}</b> {e(a["limit"])}</p></li>')
    apps = (f'<section class="section" id="apps"><div class="wrap"><p class="eyebrow">{e(c["appsEyebrow"])}</p>'
            f'<h2>{c["appsTitleHtml"]}</h2><p class="lead">{e(c["appsLead"])}</p>'
            f'<ul class="bento">{tiles}</ul><p class="fine">{e(c["appsNote"])}</p></div></section>')
    d = c["diagram"]
    steps = "".join(f'<li><span class="step-n" aria-hidden="true">{n}</span><div><h3>{e(s["t"])}</h3><p>{e(s["d"])}</p></div></li>'
                    for n, s in enumerate(c["steps"], 1))
    how = (f'<section class="section alt" id="how"><div class="wrap split"><div>'
           f'<p class="eyebrow">{e(c["howEyebrow"])}</p><h2>{c["howTitleHtml"]}</h2><p class="lead">{e(c["howLead"])}</p>'
           f'<ol class="steps">{steps}</ol></div>'
           f'<div class="diagram" role="img" aria-label="{e(d["client"])} — {e(d["transport"])} — {e(d["server"])}">'
           f'<div class="node"><small>{e(d["clientSub"])}</small><strong>{e(d["client"])}</strong><span>{e(d["clientTech"])}</span></div>'
           f'<div class="link"><i></i><b>{icon("lock", "ic sm")} {e(d["transport"])}</b><small>{e(d["transportSub"])}</small><i></i></div>'
           f'<div class="node"><small>{e(d["serverSub"])}</small><strong>{e(d["server"])}</strong><span>{e(d["serverTech"])}</span></div>'
           f'</div></div></section>')
    done = "".join(f'<li>{icon("check")}<span>{e(t)}</span></li>' for t in c["done"])
    todo = "".join(f'<li>{icon("dash")}<span>{e(t)}</span></li>' for t in c["todo"])
    status = (f'<section class="section" id="status"><div class="wrap"><p class="eyebrow">{e(c["statusEyebrow"])}</p>'
              f'<h2>{c["statusTitleHtml"]}</h2><p class="lead">{e(c["statusLead"])}</p>'
              f'<div class="twocol"><div class="panel done"><h3>{e(c["doneTitle"])}</h3><ul>{done}</ul></div>'
              f'<div class="panel todo"><h3>{e(c["todoTitle"])}</h3><ul>{todo}</ul></div></div></div></section>')
    closing = (f'<section class="section closing"><div class="wrap narrow center"><h2>{c["ctaTitleHtml"]}</h2>'
               f'<p class="lead">{e(c["ctaText"])}</p><a class="btn primary" href="{href(lang, "download")}">{e(c["ctaButton"])}</a>'
               f'</div></section>')
    return hero + pillar_sec + apps + how + status + closing


def build_security(lang: str, ui: dict, c: dict) -> str:
    secs = "".join(section(s, i % 2 == 1) for i, s in enumerate(c["sections"]))
    note = (f'<section class="section"><div class="wrap narrow"><div class="panel note"><h3>{e(c["noteTitle"])}</h3>'
            f'<p>{e(c["note"])}</p></div></div></section>')
    return page_head(c) + secs + note + cta(lang, c["ctaText"], c["ctaButton"], "faq")


def build_download(lang: str, ui: dict, c: dict) -> str:
    n = c["notice"]
    notice = (f'<section class="section tight"><div class="wrap narrow"><div class="panel notice"><h3>{e(n["t"])}</h3>'
              f'<p>{e(n["d"])}</p></div></div></section>')
    plats = ""
    for icon_name, p in zip(("windows", "android"), c["platforms"]):
        items = "".join(f"<li>{icon('dash')}<span>{e(t)}</span></li>" for t in p["items"])
        plats += (f'<div class="platform"><div class="tile-top"><span class="tile-ic">{icon(icon_name)}</span>'
                  f'<span class="badge preview">{e(p["badge"])}</span></div><h3>{e(p["name"])}</h3>'
                  f'<p class="sub">{e(p["sub"])}</p><ul class="plain">{items}</ul></div>')
    platforms = f'<section class="section tight"><div class="wrap"><div class="twocol">{plats}</div></div></section>'
    secs = "".join(section(s, i % 2 == 0) for i, s in enumerate(c["sections"]))
    return page_head(c) + notice + platforms + secs


def build_faq(lang: str, ui: dict, c: dict) -> str:
    qa = "".join(f'<details class="qa"><summary>{e(i["q"])}</summary><p>{e(i["a"])}</p></details>' for i in c["items"])
    faq = f'<section class="section tight"><div class="wrap narrow"><div class="qa-list">{qa}</div></div></section>'
    return page_head(c) + faq + section(c["license"], True)


BUILDERS = {"index": build_index, "security": build_security, "download": build_download, "faq": build_faq}


def render(language: str, content: dict, page: str = "index") -> str:
    master_check()
    ui, c = content["ui"], content[page if page != "index" else "index"]
    up = "" if language == "en" else "../"
    alternates = "".join(
        f'<link rel="alternate" hreflang="{code}" href="{href(language, page, code)}">' for code in LANGS)
    alternates += f'<link rel="alternate" hreflang="x-default" href="{href(language, page, "en")}">'
    html = (f'<!doctype html>\n<html lang="{language}">\n<head>\n<meta charset="utf-8">\n'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<meta http-equiv="Content-Security-Policy" content="{CSP}">\n'
            f'<meta name="referrer" content="no-referrer">\n'
            f'<meta name="color-scheme" content="dark light">\n'
            f'<meta name="theme-color" content="#070b14" media="(prefers-color-scheme: dark)">\n'
            f'<meta name="theme-color" content="#f5f7fb" media="(prefers-color-scheme: light)">\n'
            f'<meta name="description" content="{e(c["description"])}">\n<title>{e(c["title"])}</title>\n'
            f'<link rel="icon" type="image/svg+xml" href="{up}assets/fortiq-icon.svg">\n'
            f'<link rel="icon" type="image/png" href="{up}assets/fortiq-icon.png">\n'
            f'<link rel="stylesheet" href="{up}styles.css">\n{alternates}\n'
            f'<script src="{up}assets/site.js"></script>\n</head>\n<body id="top">\n'
            f'<a class="skip" href="#main">{e(ui["skip"])}</a>\n{header(language, ui, page)}\n'
            f'<main id="main">\n{BUILDERS[page](language, ui, c)}\n</main>\n{footer(language, ui)}\n</body>\n</html>\n')
    return html


def outputs():
    """Yield (path relative to ROOT, html) for every generated page."""
    for language, content in COPY.items():
        for page in PAGES:
            yield Path("www") / lang_dir(language) / page_name(page), render(language, content, page)


def main() -> None:
    for rel, html in outputs():
        target = ROOT / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(html.encode("utf-8"))
        print("Wrote", rel.as_posix())


if __name__ == "__main__":
    main()
