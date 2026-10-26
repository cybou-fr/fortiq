# FORTIQ static website — EN / FR / RU

**Status:** marketing preview, **not** a public release or download destination. Copy follows [RELEASE_SCOPE_1_0](../RELEASE_SCOPE_1_0.md) (supported / preview) and [PRODUCTION_READINESS](../PRODUCTION_READINESS.md); targets are never described as implemented.

## Pages (each in three languages)

| Page | Content |
|---|---|
| `index.html` | hero on the approved design reference, three principles, eight apps with status and limits, how it works (SSH diagram), honest status |
| `security.html` | SSH only, fingerprint pinning, secrets in the OS store, server-side limits, what is not claimed |
| `download.html` | **no public build yet**; Windows / Android plan and limits, server requirements, GPL source plan |
| `faq.html` | questions and the GPL-3.0 licence section (`#license`) |

English lives in `www/`, French in `www/fr/`, Russian in `www/ru/`.

## Single source and regeneration

- `docs/website/copy.json`: all localized text (per language: `ui`, `index`, `security`, `download`, `faq`).
- `docs/website/generate.py`: deterministic stdlib generator; `outputs()` yields every page, `render(lang, content, page)` builds one.
- `www/styles.css`: dark by default, light via `prefers-color-scheme` or the toggle; system fonts; responsive from 320 px; focus rings, `prefers-reduced-motion`, forced-colors.
- `www/assets/site.js`: only the optional theme toggle (the site works without JavaScript).
- Generated HTML is committed; `tool/check_docs_consistency.py` fails on drift or on a stray page.

```sh
python3 docs/website/generate.py
python3 tool/check_docs_consistency.py
```

Do not hand-edit generated HTML. Pages carry a strict Content-Security-Policy (`default-src 'none'`, same-origin images/styles/scripts only): no trackers, cookies, external fonts or third-party scripts.

## Design reference image

The exact owner-approved 1536×1024 PNG (SHA-256 `852b5944c61ebb8551cc10dadeda47e7924b92baec0fd4b7758ffe2a5c6ffaef`) is kept unchanged in `docs/design/reference/` and `www/assets/` (checked by the generator and the consistency check). The hero uses `fortiq-shell-1536.webp` / `fortiq-shell-768.webp`, plain re-encodings of the same image at the same 3:2 aspect (no crop, no retouch), with a link to the original PNG. The caption always states it is a design target, not a running-app screenshot. Visual fidelity of the Qt shell is judged separately ([SHELL_FIDELITY](../SHELL_FIDELITY.md)).

## Release discipline

- No download buttons or installers until a qualified build exists; the Download page keeps saying so.
- Keep statuses in sync with `RELEASE_SCOPE_1_0.md`; no prices or monetization, no AI as available, no direct client network claims beyond SSH.
- Before external publication verify domain, asset rights (wallpaper, icons), security headers (the meta CSP should become real HTTP headers), translations and links.
- `GENERATION_PROMPTS.md` is historical and not used by the generator.

Logo/icon notes: `www/assets/fortiq-icon.svg` (favicon and header mark); native icons come from `assets/branding/` via `tool/generate_brand_assets.py` ([branding](../../assets/branding/README.md)).
