# FORTIQ — approved exact desktop reference

Original owner-provided **PNG 1536×1024 (RGBA)**, 21 October 2026.

**Expected filename:** `docs/design/reference/fortiq-shell-master.png`  
**SHA-256:** `852b5944c61ebb8551cc10dadeda47e7924b92baec0fd4b7758ffe2a5c6ffaef`

The original PNG is **not present in the repository until an authorized binary upload**. Do not substitute a compressed screenshot, AI recreation or alternate wallpaper and call it the master.

Import both documentation reference and website copy with:

```sh
python3 tool/publish_master_reference.py "/path/to/FORTIQ_Master_Reference_Import.zip"
```

The installer validates dimensions and hash before copying and regenerates the three static pages. The website will then show the exact master PNG as a **design reference**, never a falsely represented executable screenshot.

See [UI SPEC v1](../../UI_SPEC_V1.md).
