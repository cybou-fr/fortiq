# Android foundation — 22 October 2026

This is an implemented prototype slice, **not device qualification or visual acceptance**.
The binding visual contract remains [UI SPEC v1](UI_SPEC_V1.md); product/network rules remain
[Product Scope](PRODUCT_SCOPE.md) and [SSH frontend](SSH_FRONTEND_CONTRACT.md).

## Implemented

- At 1536×1024 landscape the existing desktop composition and Files geometry remain. Below
  1280 logical pixels, or in portrait, the shell uses actual viewport units, without shrinking
  the complete 1536×1024 desktop. One foreground Files/Logs surface maximizes within the shell;
  the single bottom app panel scrolls horizontally, with unavailable apps explicitly dimmed.
  On a wide Android tablet the same viewport layout is used while the IME is visible, avoiding
  a miniaturized desktop above the keyboard; hiding the IME restores the desktop composition.
- Files has a touch toolbar, path/search fields, an action menu, single-tap opening and long-press
  context actions. Its sidebar becomes a drawer below 900 pixels. Logs uses the same drawer
  pattern and scrollable filters; its incident inspector switches synopsis/events on narrow screens.
  Existing window instances survive resizing and app switching, preserving unsaved editor text.
- Back hides the IME, closes overlays/drawers or requests viewer closure with the existing unsaved
  confirmation before leaving the editor. ApplicationWindow supplies Qt's safe-area padding;
  keyboard overlap is accounted for, the app panel hides during text input, and server-form fields
  can scroll into view. These native interactions still require real-device tests.
- Android Back is routed at the Qt window before focus-dependent QML handlers, using the same
  shell navigation/dirty-editor guard. With nothing to close, Android may background the app normally.
  Missing desktop symbol glyphs use small Canvas silhouettes on Android, including navigation,
  search, transfer arrows and Logs. The touch action menu uses the existing glass material.
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
into an auto-removing temporary file under the explicit app-private CacheLocation/document-staging
directory (not the process-wide temporary directory), verifies byte count
when the server reports size, then opens the chosen provider destination. Provider publication is
**not atomic**: an open/write/flush/close failure or cancellation after opening reports `unknown`
with an explicit partial-document warning. Disconnecting during a running provider download
also conservatively marks it `unknown`, even when a new server generation would discard its late result. It never silently enables QSaveFile direct-write fallback.
Cancellation before publication does not open the destination; the picker itself may already have
created an empty document. Abrupt process death can leave temporary storage requiring OS/app cleanup.
An idle transfer transport lost during the initial read-only SFTP stat is closed and reconnected
once, rechecking the host pin, before retrying that preflight. Permission/path errors, uploads and
provider writes are not replayed. This is not general automatic recovery after publication begins.

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

Earlier Windows regression evidence: CTest **7/7 targets passed**, Logs **11 passed**. This emulator
slice rebuilt Windows/Android and reran the changed suites: layout QtTest **26 passed**,
document/export QtTest **10 passed**, live publication **28 passed**. New cases exercise window-level
Back with focused forms at every viewport, writable/auto-removed private staging, and lost download
preflight preserving an existing destination and invalidating the dead session. The general
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

## Emulator run and remaining Android qualification

On 22 October 2026 the debug arm64 APK was installed successfully on the `FortiqTablet` AVD
(Android 17 / API 37, x86_64 with arm64 translation, 2560×1600). The shell and Files rendered,
and tapping the server chooser opened Server Manager. The first translated launch took about
30 seconds. Screenshots: ignored `build-dist/emulator-launch.png` and `emulator-servers.png`.
The follow-up run verified pinned host-key authentication with a disposable key in app-private storage,
SFTP navigation (including a Cyrillic/accented filename with a space), UTF-8 editing/saving on the Linux
fixture, portrait/landscape rotation retaining the edit, native IME, and Back/unsaved confirmation.
Native DocumentsUI opened a Downloads document for upload; the 43-byte UTF-8/CRLF source matched
the remote SHA-256. Downloading it to a new SAF document matched the source byte for byte.
The fixture was an isolated WSL Debian server; the temporary key is revoked/deleted after testing.
Failures found and fixed: missing symbol fonts, focus-dependent Back backgrounding the Activity,
whole-desktop shrink above the landscape IME, and default temporary-file creation failing during SAF
download. Read-only SFTP preflight transport loss is now distinguished from permission/path errors.
Screenshots (ignored `build-dist/`): `emulator-menu-fixed.png`, `emulator-back-verified.png`,
`emulator-landscape-ime-fixed.png`, `emulator-dirty-back.png`, `emulator-saf-verified.png`.
The GFXSTREAM GL 0x502 warning observed in the first run and occasional brand-texture rendering
remain GPU/fidelity follow-up items; these functional checks do not qualify performance or visuals.
The key picker itself was not exercised with a real private-key provider URI in this run.

### Emulator window recovery (22 October 2026)

The Windows emulator window later showed a small guest image in its upper-left corner with large
black margins. The Android framebuffer itself had no such margins. A cold restart restored the host
window; this is emulator presentation evidence, not a QML layout fix. The local `FortiqTablet` AVD
now uses `hw.initialOrientation=landscape`, `hw.gpu.enabled=yes`, `hw.gpu.mode=host` for its
2560×1600 screen. The previous configuration was backed up under ignored `build-dist/`.

With the AVD stopped, the verified development launch is:

```powershell
& "$env:LOCALAPPDATA/Android/Sdk/emulator/emulator.exe" -avd FortiqTablet -no-snapshot -gpu host
```

Wait for the previous emulator process to exit before restarting. This avoids loading/saving Quick
Boot state and preserves userdata; do not wipe the AVD. Native Windows window captures verified
the corrected bounds and portrait/landscape rotation using the emulator toolbar. An ADB-only
rotation changes the guest orientation without rotating the host window and can temporarily leave
the image sideways. Allow the sensor/Qt relayout to settle after using the toolbar.
An experimental cold run with `-gpu swiftshader` produced severe triangular texture corruption;
it was reverted. Host GPU restored the normal scene, but the existing GPU/fidelity qualification
items above remain open. No application code or APK changed for this recovery.

The subsequent owner-requested shell correction rebuilt Windows and the Android APK and installed
the APK on this AVD. Native window capture confirmed the wallpaper fills the usable viewport and
panels show the blurred wallpaper. Layout QtTest passed **32 cases**, including 1600×950 edge
coverage and Files sidebar mouse dragging. The native drag attempt was interrupted by concurrent
owner input; automated dragging passed, but that is not an Android touch qualification claim.
Glass sampling now follows ancestor movement; Files/app-panel/workspace tints are less opaque.
Owner acceptance of the resulting glass appearance is still pending.
The next feedback pass adds artwork behind safe insets, launcher anchoring to the app panel,
a lighter launcher tint, translucent lower-window visibility and three clickable wallpaper previews.
Full composited blur of lower windows is not implemented: the blur source remains the wallpaper.
Windows/Android rebuilds passed; the updated APK was installed and launched. Native capture
confirmed artwork underneath both system bars. Layout QtTest: **32 passed**, now also clicking
all three background cards and checking the bottom launcher's position. Concurrent owner input
prevented additional native clicks; the three-card click evidence is QtTest, not an Android gesture run.
The latest owner revision restores a system-palette background behind Android's native status icons
at the top, while keeping the bottom navigation artwork. The rebuilt APK was installed/launched;
`build-dist/emulator-system-status.png` confirms the solid light status area with readable dark native
icons on this AVD. Dark system theme and physical devices remain unqualified.
Windows native checks also found coalesced fast sidebar drags could fail despite the previous
two-move QtTest. The handle now applies bounded movement from parent-space press coordinates,
including the final release position. QtTest adds a press/release-only case; the native Windows drag
then moved Files successfully. This change is also included in the installed Android APK.

The emulator remains available for further testing.
The [Terminal slice](TERMINAL.md) also built and installed/launched on FortiqTablet. This verifies
APK startup, not interactive Android terminal IME/TUI acceptance. Live PTY and controller tests
ran on Windows against isolated WSL Debian. The disposable public key was revoked and its local
private/public files removed after testing; no production account was used.

On physical Lenovo hardware still verify installation/launch,
rotation and real DPR, safe areas/predictive Back, IME/selection, tap/long press, readable touch controls,
native key/upload/save pickers, provider metadata/unknown size, expired grants and restart persistence,
SSH disconnect during transfer/export, partial-document warnings, and GPU blur/performance.
Secondary transfer/search/service controls still need a full touch/accessibility review.
Owner visual acceptance, clean Windows qualification and production signing remain separate gates.
