# FORTIQ static product website — EN / FR / RU

**Status:** updated conceptual marketing preview, not a public release or download destination. The site is aligned with the new architecture: **local Qt/C++ QML Desktop Shell for remote Debian/Ubuntu**, Windows and Android first; flagship **Smart Files** and **Logs**.

## Single source and regeneration

- `docs/website/site-template.html`: shared accessible HTML skeleton.
- `docs/website/copy.json`: localized EN / FR / RU copy and honest capability/status labels.
- `docs/website/generate.py`: deterministic Python stdlib generator.
- `www/index.html`, `www/fr/index.html`, `www/ru/index.html`: generated outputs.
- `www/styles.css`: responsive navy/blue/glass design.

```sh
python3 docs/website/generate.py
python3 tool/check_docs_consistency.py
```

Do not hand-edit generated HTML without updating the generator source and regenerating all locales. No JS, external fonts, trackers, cookies or mandatory network dependencies are required.

## Design master screenshot (still pending binary import)

The exact owner-approved 1536×1024 PNG has SHA-256 `852b5944c61ebb8551cc10dadeda47e7924b92baec0fd4b7758ffe2a5c6ffaef`.

Run `python3 tool/install_master_reference.py /path/to/fortiq-shell-master.png` with the actual original file. It copies unchanged PNG bytes to `docs/design/reference/fortiq-shell-master.png` and `www/assets/fortiq-shell-master.png` and regenerates the pages.

Before the binary is imported, the website uses a **clearly labeled CSS conceptual illustration** which MUST NOT be treated as the target screenshot or runtime app. After import, the website shows the **exact PNG** labeled as the approved design reference, still **not** as a running Qt screenshot.

The binding contract for QML remains [UI SPEC](../UI_SPEC_V1.md), not this marketing page.

## Release discipline

- Do not publish download buttons or imply Qt Windows/Android builds exist without actual evidence.
- Do not present Smart Files advanced operations, Logs, Browser SSH proxy or AI as implemented.
- Existing Qt R0.1 source has read-only SFTP path/listing and preliminary SSH only.
- Before external publication, verify domain, asset attribution, security headers, translations, actual links, mobile responsiveness and brand rights.

See [Documentation Authority](../DOCUMENTATION_AUTHORITY.md).

## Logo and native application icons

Website navigation and footer now use `www/assets/fortiq-logo.svg` (light wordmark on dark background), with `fortiq-icon.svg` as scalable favicon and PNG fallback. For white backgrounds use `fortiq-logo-dark.svg`. SVGs are production vector interpretations of the approved logo, not crops from the brand-board collage.

Native executable resources are generated from `assets/branding/fortiq_mark.png` by `tool/generate_brand_assets.py` during CMake configuration. See [branding documentation](../../assets/branding/README.md).
