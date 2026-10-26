# FORTIQ — branding and native application icons

**Visual anchor:** the FORTIQ cyan/blue/violet "wing F" monogram (concept board, 21 October 2026). Since 26.10.2026 it is the **only** mark: the application (window and taskbar icon, Windows `.ico`, Android launcher, top bar of the shell) and the website use the same artwork. The earlier violet/teal folded-F raster is retired.

## Single source

`assets/branding/fortiq-icon.svg` (mark on the midnight tile) and `assets/branding/fortiq-mark.svg` (the same mark alone, tight viewBox) are the vector sources. `assets/branding/fortiq_mark.png` is the canonical raster **mark** (transparent, 1024 wide) rendered from `fortiq-mark.svg` with Qt's `QSvgRenderer` (anti-aliased, no background); `fortiq-icon-source.png` is the tile rendered at 1254². If the vector changes, re-render both rasters and rebuild.

During CMake configure, `tool/generate_brand_assets.py` (Python standard library only) builds from `fortiq_mark.png` into the **build directory**, not source control:

- Windows `fortiq.ico` with 16/24/32/48/64/128/256 PNG-backed layers (the mark on a midnight tile) and `fortiq.rc`, included by `fortiq.exe` and the preview executable.
- `fortiq_mark.png` (transparent, 256²) compiled into `:/branding/fortiq_mark.png`; `src/main.cpp` sets `QGuiApplication::setWindowIcon` and `QQuickWindow::setIcon`; the shell's top bar shows the same file.
- Android density mipmaps, `mipmap-anydpi-v26` adaptive icon (foreground = the mark, background colour `#07132B`) and a monochrome layer, packaged through `QT_ANDROID_PACKAGE_SOURCE_DIR` / `QT_ANDROID_APP_ICON`.

**Web:** `fortiq-icon.svg` (favicon, site header), `fortiq-logo.svg` for dark and `fortiq-logo-dark.svg` for light backgrounds (mark + wordmark). `www/assets/` holds copies, and `www/assets/fortiq-icon.png` is the 512² tile PNG. **Application icons** on the site repeat the artwork of `qml/shell/AppIcon.qml` / `FolderGlyph.qml` (same rounded tile, gradient colours and glyph); when one changes, change the other (`APP_ICON` in `docs/website/generate.py`).

Android adaptive layer uses the system launcher mask; do not bake a fixed rounded square into the adaptive foreground. Legacy mipmaps contain a bounded midnight tile for Android 7.x and older launchers. `fr.cybou.fortiq` remains unchanged.

## Rebuild / checks

```sh
python3 tool/generate_brand_assets.py --source assets/branding/fortiq_mark.png \
  --output build/generated/branding --android-output build/generated/android
python3 tool/check_docs_consistency.py
```

Then configure and build Windows with Qt/MSVC or Qt/MinGW. Verify icon extraction from actual .exe in Explorer; verify QML titlebar/taskbar. For Android configure with Qt 6.9+, SDK/NDK/libssh2 arm64, inspect `aapt dump badging app.apk` and `unzip -l app.apk` (mipmap assets), then **install APK on device** and inspect home-screen mask. Source integration alone is not runtime validation.

**Important:** the shell's top-bar mark is visible in the owner's reference scene; its replacement by the wing-F mark was requested by the owner on 26.10.2026. The master screenshot's layout is not changed.
