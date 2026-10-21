# FORTIQ — Qt 6 / C++20 migration, Windows & Android first

**Decision:** single C++20 + Qt Quick/QML codebase for Windows and Android. Linux server remains the sole application backend. **Git workflow:** update `main` only; preserved Flutter sources at tag `flutter-final`. This is an implementation plan with a truthful snapshot of the Qt progress.

## Migration constraints, not negotiable

1. Not a DevOps dashboard. A universal remote Linux desktop with Files → Documents → Mail → Browser/media and systems tools; optional AI later.
2. `GUI → SSH → existing Linux CLI/SFTP → GUI`. No new server daemon/agent, no HTTP backend or client-side IMAP/SMTP/FTP fallback.
3. Linux retains all persistent work data and tool/workspace settings. Only minimal bootstrap (address, trusted host keys, credentials protected at rest when introduced) and separate commercial license state may persist locally.
4. A workspace operation binds to an immutable server identity, account and path. On SSH disconnect, avoid replaying writes. `unknown` means the remote action may have happened.
5. Both Windows and Android receive the *same core* and QML component model, with responsive navigation (mouse/keyboard vs touch). Do not force a Windows desktop window model onto a phone.
6. No automatic system modifications before explicit consent and capability detection; reuse mature Linux tools.

## Completed in R0.1 source (not yet qualified by a full Qt build)

- Qt 6.8+ CMake application with QML module and desktop/mobile navigation.
- `RemoteController`: asynchronous calls on a worker pool, status, server affinity via generation token, remembered bootstrap only.
- `SshClient`: native libssh2 TCP/SSH handshake, host-key fingerprint read and SHA256 pin comparison **before user authentication**; SFTP directory listing (read-only), no shell execution.
- Files: home-directory listing, directory navigation, parent path, refresh; directories and regular files displayed separately.
- Tests for rejecting ambiguous remote path segments.
- Builds depend on **target-ABI-compatible libssh2** with crypto dependencies; Windows and Android validation has not been completed. Do not report these platforms as tested.

## Vertical slices to deliver

| Milestone | User value | Engineering acceptance |
| --- | --- | --- |
| **R0.1 — Bootstrap & Files read-only** | Connect and explore Linux home via SSH | Windows + Android arm64 builds, actual sshd/SFTP integration, unknown host explicit verification, changed key rejected, Unicode/permission/error cases, app background/resume tests |
| **R0.2 — Credential & session hardening** | Connect securely to real configured servers | Ed25519 private-key auth via platform key store or secure credential provider, no plaintext key files, persistent session, bounded reconnect, cancellation and late-result isolation, host verification, password clearing, device rotation |
| **R1 — Everyday Files & Editor** | Open text, edit, safe save, file transfers | SFTP upload/download progress, abort/unknown result, remote temp+rename and version check, safe conflict UI, large file and Unicode tests, one server-bound action context |
| **R1.5 — Workspace** | Useful home screen with apps and tasks | Stable app descriptors, server-specific windows/tabs on desktop, mobile stack navigation, statuses/notifications, remote workspace persistence, setup capability discovery |
| **R2 — Documents & Mail** | Daily working application | Linux CLI-backed document preview, PDF viewer and server-side mail CLI; no direct mobile IMAP/SMTP; mail delivery is verified, not assumed |
| **R3 — Browser / Media / licensing / release** | Broader Linux desktop | Proven SSH-only proxying per platform, explicitly no direct WebView fallback; verified subscription/license flow, lifecycle and packaging |

### Browser Android constraint

Qt WebEngine and Windows WebView2 do not provide a single browser component suitable for the two target platforms. Android WebView through Qt WebView is a different platform backend, and reliable per-webview SSH proxy routing is **not yet demonstrated**. The browser app is **deferred**, not replaced with an Android direct-network shortcut that violates the product's SSH contract. An actual compatible transport needs testing before UI work.

### Credential limitations in R0.1

- Password authentication only; no private-key auth yet. Password remains only in process RAM until disconnect; no persistent work data/cache is written.
- A new host requires explicit SHA-256 fingerprint acceptance *after independent verification*. Only a pin is written to QSettings; changed pins are not silently reset. No known_hosts migration yet.
- Each folder listing makes a short SSH/SFTP connection, so throughput/latency is not production-grade. Persistent authenticated SSH and cancellation are R0.2.
- Non-UTF8 SFTP names are skipped instead of sent as lossy Qt paths; arbitrary-byte filename support needs a byte-safe path identity.
- Directory listing has a limit of 3000 entries; no writes, uploads, downloads or hidden backend mode.
- Connection timeouts exist, but QtConcurrent work cannot be interrupted immediately; a disconnected tab ignores stale results.

## Manual acceptance before claiming Windows / Android support

1. **Windows:** configure Qt Desktop kit + libssh2 dependencies; compile Release and launch, inspect DLL dependency deployment; run `ctest` and a real Linux SSH server with clean and incorrect fingerprints, bad password, revoked access, 10kB/Unicode names.
2. **Android:** configure arm64 kit + cross-compiled libssh2/crypto, inspect merged APK permissions (`INTERNET`), build signed debug APK and install on device/emulator; test Wi-Fi/mobile connectivity, screen rotation, touch, pause/resume, background interruption, keyboard and memory.
3. **Both:** SSH first-use pin check with an independent server fingerprint, legitimate changed-key rejection, no password persistence, no unintended DNS/network fallback, connection failure, 3000+ directory safety, file permissions and logout. Confirm per-device packaging of libssh2/OpenSSL licenses.
4. **Proof:** versioned log, commit SHA, Qt/libssh2 ABI/build identifiers and environment information; do not infer success from CMake, source files or historical Flutter tests.

## Old documents

`README.md` is authoritative for **current Qt implemented** status. `docs/PRODUCT_SCOPE.md`, `docs/ARCHITECTURE.md`, `docs/DEVELOPMENT_MATRIX.md`, `docs/SSH_FRONTEND_CONTRACT.md` and `AGENTS.md` retain older Flutter-era implementation statements. Treat them as product requirements/historical background only until incrementally updated; do not transfer their “implemented” claims to the new Qt client. The legacy implementation is available at `flutter-final`.
