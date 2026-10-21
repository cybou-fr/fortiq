# FORTIQ — branding and native application icons

**Visual anchor:** approved FORTIQ cyan/blue/violet folded-F monogram (concept board, 21 October 2026). This directory contains the raster source already present in the project and scalable SVG interpretation for web branding.

## Single source for native binaries

`assets/branding/fortiq_mark.png` is the canonical raster **mark**. During CMake configure, `tool/generate_brand_assets.py` (Python standard library only) creates assets in the **build directory**, not in source control:

- Windows `fortiq.ico` with 16/24/32/48/64/128/256 PNG-backed icon layers, and accompanying `fortiq.rc`. Both `fortiq.exe` and the preview executable include the Win32 resource.
- `fortiq_mark.png` (transparent, 256²) compiled into the Qt resource path `:/branding/fortiq_mark.png`. `src/main.cpp` sets `QGuiApplication::setWindowIcon` and `QQuickWindow::setIcon`.
- Android density mipmap PNGs, `mipmap-anydpi-v26` adaptive icon, foreground bitmap and background color, packaged by Qt via `QT_ANDROID_PACKAGE_SOURCE_DIR` and `QT_ANDROID_APP_ICON` (Qt 6.9+ for Android).

**Web:** transparent scalable `fortiq-logo.svg` for dark nav/footer, `fortiq-logo-dark.svg` for white backgrounds, and `fortiq-icon.svg` for favicon. Website remains generated from `docs/website/site-template.html` across EN/FR/RU.

Android adaptive layer uses the system launcher mask; do not bake a fixed rounded square into adaptive foreground. The fallback legacy mipmaps contain a bounded midnight tile for Android 7.x and prior launcher behavior. `fr.cybou.fortiq` remains unchanged.

## Rebuild / checks

```sh
python3 tool/generate_brand_assets.py --source assets/branding/fortiq_mark.png \
  --output build/generated/branding --android-output build/generated/android
python3 tool/check_docs_consistency.py
```

Then configure and build Windows with Qt/MSVC or Qt/MinGW. Verify icon extraction from actual .exe in Explorer; verify QML titlebar/taskbar. For Android configure with Qt 6.9+, SDK/NDK/libssh2 arm64, inspect `aapt dump badging app.apk` and `unzip -l app.apk` (mipmap assets), then **install APK on device** and inspect home-screen mask. Source integration alone is not runtime validation.

**Important:** the SVG is a clean production redraw of the approved visual theme; a generated brand-board image is **not** by itself a lossless vector asset. Do not change the master screenshot's layout merely to adopt the logo. Changes to the reference-visible shell brand mark need separate visual approval.
