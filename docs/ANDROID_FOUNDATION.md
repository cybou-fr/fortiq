# Android foundation — 22 October 2026

This is an implemented prototype slice, **not device qualification or visual acceptance**.
The binding visual contract remains [UI SPEC v1](UI_SPEC_V1.md); product/network rules remain
[Product Scope](PRODUCT_SCOPE.md) and [SSH frontend](SSH_FRONTEND_CONTRACT.md).

## Implemented

- At 1536×1024 landscape the existing desktop composition and Files geometry remain. Below
  1280 logical pixels, or in portrait, the shell uses actual viewport units, without shrinking
  the complete 1536×1024 desktop. One foreground Files/Logs surface maximizes within the shell;
  the single bottom app panel scrolls horizontally, with unavailable apps explicitly dimmed.
- Files has a touch toolbar, path/search fields, an action menu, single-tap opening and long-press
  context actions. Its sidebar becomes a drawer below 900 pixels. Logs uses the same drawer
  pattern and scrollable filters; its incident inspector switches synopsis/events on narrow screens.
  Existing window instances survive resizing and app switching, preserving unsaved editor text.
- Back hides the IME, closes overlays/drawers or requests viewer closure with the existing unsaved
  confirmation before leaving the editor. ApplicationWindow supplies Qt's safe-area padding;
  keyboard overlap is accounted for, the app panel hides during text input, and server-form fields
  can scroll into view. These native interactions still require real-device tests.
- The manifest uses SAF document grants and declares INTERNET, without legacy READ/WRITE_EXTERNAL_STORAGE
  or MANAGE_EXTERNAL_STORAGE permissions. It allows rotation, requests adjustResize, and packages
  the approved native icon. AndroidX adds its app-private non-exported-receiver permission during merge.

## Document URLs and credentials

`LocalDocument` preserves `content://` URLs as opaque encoded identifiers. It does not decode their
document IDs into local paths or introduce an HTTP fallback. QFile/QFileInfo use Qt's Android content
engine (including provider display names); the native picker handles URI grants. A provider may
revoke or decline persistent access, so an unreadable saved key must be reselected.

Uploads stream through QFile; unknown/zero provider length is treated as unknown, and final remote
size is checked against bytes actually read. Private-key authentication now loads a bounded key
(up to 1 MiB) via QFile and calls libssh2 from memory, also on Windows. The key is never copied to
a temporary file or persisted in a profile; its byte buffer is cleared after use. Only its path/URI
is remembered. This is not a claim of hardened/locked secret memory.

Native downloads retain QSaveFile atomic publication. A SAF download first finishes its SFTP read
into an auto-removing temporary file (Android app-private temporary storage), verifies byte count
when the server reports size, then opens the chosen provider destination. Provider publication is
**not atomic**: an open/write/flush/close failure or cancellation after opening reports `unknown`
with an explicit partial-document warning. Disconnecting during a running provider download
also conservatively marks it `unknown`, even when a new server generation would discard its late result. It never silently enables QSaveFile direct-write fallback.
Cancellation before publication does not open the destination; the picker itself may already have
created an empty document. Abrupt process death can leave temporary storage requiring OS/app cleanup.

Implementation references: [Qt QFile content-URI limitations](https://doc.qt.io/qt-6/qfile.html),
[QSaveFile](https://doc.qt.io/qt-6/qsavefile.html),
[ApplicationWindow safe areas](https://doc.qt.io/qt-6/qml-qtquick-controls-applicationwindow.html),
[libssh2 memory-key authentication](https://libssh2.org/libssh2_userauth_publickey_frommemory.html).
The installed Qt 6.12 source was also inspected for content-engine display names, URI grants and
QSaveFile's explicit content-URI direct-write limitation.

## Build and evidence

Built locally with Qt 6.12.0, NDK 28.2.13676358, JDK 21 and SDK 36, arm64-v8a, min API 28.
libssh2 1.11.1, OpenSSL 3.6.3 and zlib 1.3.2 were cross-compiled from cached vcpkg sources into
the ignored `build-android-deps/` directory. The client compiled for Android and a **debug-signed APK**
was packaged. `aapt` confirmed package `fr.cybou.fortiq`, version 0.1.0, min SDK 28, target/compile SDK 36,
arm64-v8a and absence of broad storage permissions. `apksigner verify` passed. This is not a store/release build.

Developer-only repeatable build (all toolchains/dependencies must already be installed):

```powershell
python tool/build_android.py --qt-android C:/Qt/6.12.0/android_arm64_v8a `
  --qt-host C:/Qt/6.12.0/msvc2022_64 --sdk C:/Users/cybou/AppData/Local/Android/Sdk `
  --ndk C:/Users/cybou/AppData/Local/Android/Sdk/ndk/28.2.13676358 `
  --jdk C:/Users/cybou/tools/jdk-21 --deps ./build-android-deps/installed/arm64-android `
  --ninja C:/Qt/Tools/Ninja/ninja.exe --package
```

The helper defaults to Debug, accepts explicit installed paths and SDK version, and never installs
to a device. `--build-type Release` produces an unsigned release candidate unless separately signed.
Expected debug output: `build-android-arm64/android-build/build/outputs/apk/debug/android-build-debug.apk`.
Customers receive a finished application; these instructions are for repository development only.

Windows regression evidence: CTest **7/7 targets passed**; layout QtTest **26 passed**,
document/export QtTest **9 passed**, Logs **11 passed**, live publication **27 passed**. The general
SSH integration suite explicitly skipped without `FORTIQ_IT_*`; the separate publication fixture
used real SSH with a test key through the new memory-authentication path.
Layout cases cover 1536×1024, 1024×768, 768×1024 and 390×844 for Files, Logs, editor, incident,
service and server manager, rotation, app switching, and Back with dirty editor text.
Screenshots are regenerated under `build-msvc/layout-captures/`; offscreen software rendering with
installed system fonts is geometry/interaction evidence, **not Android GPU/material fidelity proof**.
The tests explicitly configure Qt's offscreen plugin/font paths; this fixes the earlier failed
preview invocation whose environment exposed only the Windows platform plugin.

Document tests use fake providers to exercise opaque URLs, deferred destination opening, partial
writes, denied opening and cancellation before/during export. Live publication tests on the isolated
WSL SSH/SFTP fixture also verify the new memory-key path. They do not simulate Android grant/provider APIs.

## Still required on Lenovo / Android

No device was visible to `adb devices -l`; no APK was installed or run. Verify installation/launch,
rotation and real DPR, safe areas/predictive Back, IME/selection, tap/long press, readable touch controls,
native key/upload/save pickers, provider metadata/unknown size, expired grants and restart persistence,
SSH disconnect during transfer/export, partial-document warnings, and GPU blur/performance.
Secondary transfer/search/service controls still need a full touch/accessibility review.
Owner visual acceptance, clean Windows qualification and production signing remain separate gates.
