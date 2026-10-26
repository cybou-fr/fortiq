# FORTIQ static website — EN / FR / RU

**Status:** marketing preview, **not** a public release or download destination. Copy follows [RELEASE_SCOPE_1_0](../RELEASE_SCOPE_1_0.md) (supported / preview) and [PRODUCTION_READINESS](../PRODUCTION_READINESS.md); targets are never described as implemented.

## Pages (each in three languages)

| Page | Content |
|---|---|
| `index.html` | hero with a REAL screenshot of the running application, three principles, eight apps with status and limits, how it works (SSH diagram), honest status |
| `security.html` | SSH only, fingerprint pinning, secrets in the OS store, server-side limits, what is not claimed |
| `download.html` | **no public build yet**; Windows / Android plan and limits, server requirements, GPL source plan |
| `faq.html` | questions and the GPL-3.0 licence section (`#license`) |

English lives in `www/`, French in `www/fr/`, Russian in `www/ru/`.

## Single source and regeneration

- `docs/website/copy.json`: all localized text (per language: `ui`, `index`, `security`, `download`, `faq`).
- `docs/website/generate.py`: deterministic stdlib generator; `outputs()` yields every page, `render(lang, content, page)` builds one.
- `www/styles.css`: dark by default, light via `prefers-color-scheme` or the toggle; system fonts; responsive from 320 px; focus rings, `prefers-reduced-motion`, forced-colors.
- `www/assets/site.js`: progressive enhancement only - theme toggle with a smooth transition, scroll reveal (IntersectionObserver), pointer light on app tiles, hero tilt/sheen, firmer header on scroll. Without JavaScript, or with `prefers-reduced-motion`, everything is simply visible and static. Motion in CSS: hero entrance and drifting aurora, scroll-progress bar under the header (`animation-timeline: scroll()`), SSH diagram with traffic flowing both ways. Inline styles are not used (CSP); the script sets CSS variables via CSSOM.
- Generated HTML is committed; `tool/check_docs_consistency.py` fails on drift or on a stray page.

```sh
python3 docs/website/generate.py
python3 tool/check_docs_consistency.py
```

Brand: the header mark, favicon and the application icons are the **same artwork as the app** (the wing-F mark in `assets/branding/`, application icons from `qml/shell/AppIcon.qml`); see [branding README](../../assets/branding/README.md).

Do not hand-edit generated HTML. Pages carry a strict Content-Security-Policy (`default-src 'none'`, same-origin images/styles/scripts only): no trackers, cookies, external fonts or third-party scripts.

## Hero screenshot (real) and the design reference

- The hero is a **real screenshot** of the running Windows application: `www/assets/fortiq-app-1536.png` (full size), `fortiq-app-1536.webp` and `fortiq-app-768.webp` (plain re-encodings, no crop or retouch). It is produced by [`tool/capture_app_screenshot.py`](../../tool/capture_app_screenshot.py): the production QML shell and controllers (`build-msvc/fortiq_live_capture.exe`, source `tests/LiveCapture.cpp`) connect over SSH to a disposable Debian test server, open Files on a demo folder and Terminal, and grab the window at 1536×1024, DPR 1. The windows show data that server returned; the clock, the weather notice and the wallpaper are what the app draws. The script sets the user's real `~/.bash_history` on the test server aside during the run and restores it, and removes its throw-away key and the demo folder.
- To refresh it: build, then `python tool/capture_app_screenshot.py` and re-encode the two WebP files (Qt `QImage::save(..., "WEBP")`, width 1536 and 768, quality 88). Review the picture before committing: no host names, paths or file names of private data.
- The exact owner-approved design reference (SHA-256 `852b5944c61ebb8551cc10dadeda47e7924b92baec0fd4b7758ffe2a5c6ffaef`) stays unchanged in `docs/design/reference/` and `www/assets/fortiq-shell-master.png` (checked by the generator and the consistency check) as the **design target**; it is no longer shown on the site. Visual fidelity of the Qt shell to it is judged separately ([SHELL_FIDELITY](../SHELL_FIDELITY.md)).

## Release discipline

- No download buttons or installers until a qualified build exists; the Download page keeps saying so.
- Keep statuses in sync with `RELEASE_SCOPE_1_0.md`; no prices or monetization, no AI as available, no direct client network claims beyond SSH.
- Before external publication verify domain, asset rights (wallpaper, icons), security headers (the meta CSP should become real HTTP headers), translations and links.
- `GENERATION_PROMPTS.md` is historical and not used by the generator.

Logo/icon notes: `www/assets/fortiq-icon.svg` (favicon and header mark); native icons come from `assets/branding/` via `tool/generate_brand_assets.py` ([branding](../../assets/branding/README.md)).
