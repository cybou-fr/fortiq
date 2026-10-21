# FORTIQ — Qt 6 / C++20 migration, Windows & Android first

**Decision:** single C++20 + Qt Quick/QML codebase for Windows and Android. Linux server remains the sole application backend. **Git workflow:** update `main` only; preserved Flutter sources at tag `flutter-final`. This is an implementation plan with a truthful snapshot of the Qt progress.

## Migration constraints, not negotiable

1. Not a DevOps dashboard. **Exact master-screenshot desktop UI** (not merely inspired by it) → **Smart Files** → **Logs** (deterministic analysis without AI) → supporting Terminal/System → Editor/Documents → Mail → Browser/media; AI optional later. [UI SPEC](UI_SPEC_V1.md), [Files](SMART_FILES_SPEC.md), [Logs](LOGS_INTELLIGENCE_SPEC.md).
2. `GUI → SSH → existing Linux CLI/SFTP → GUI`. No new server daemon/agent, no HTTP backend or client-side IMAP/SMTP/FTP fallback.
3. Linux retains all persistent work data and tool/workspace settings. Only minimal bootstrap (address, trusted host keys, credentials protected at rest when introduced) and separate commercial license state may persist locally.
4. A workspace operation binds to an immutable server identity, account and path. On SSH disconnect, avoid replaying writes. `unknown` means the remote action may have happened.
5. Both Windows and Android receive the *same core* and QML component model, with responsive navigation (mouse/keyboard vs touch). Do not force a Windows desktop window model onto a phone.
6. No automatic system modifications before explicit consent and capability detection; reuse mature Linux tools.

## Completed in R0.1 source (not yet qualified by a full Qt build)

- Qt 6.8+ CMake application with QML module and desktop/mobile navigation **only as an initial layout**. It does NOT match the exact owner screenshot ([UI SPEC](UI_SPEC_V1.md)) yet.
- `RemoteController`: asynchronous calls on a worker pool, status, server affinity via generation token, remembered bootstrap only.
- `SshClient`: native libssh2 TCP/SSH handshake, host-key fingerprint read and SHA256 pin comparison **before user authentication**; SFTP directory listing (read-only), no shell execution.
- Files: home-directory listing, directory navigation, parent path, refresh; directories and regular files displayed separately.
- Tests for rejecting ambiguous remote path segments.
- Builds depend on **target-ABI-compatible libssh2** with crypto dependencies; Windows and Android validation has not been completed. Do not report these platforms as tested.

## R0.2 Desktop Shell slice (in source, not accepted)

- `Fortiq.Shell` QML module reproduces every UI_SPEC §1 region at master coordinates; the Files window hosts the R0.1 connect/fingerprint flow and real SFTP listing.
- `--reference-fixture` (visual-only sample scene) and `--screenshot <png>` (1536×1024, DPR=1); `fortiq_shell_preview` builds without libssh2; `tool/compare_shell_screenshot.py` produces diff/overlay/report.
- Not accepted: placeholder wallpaper, placeholder icons/font, no §4 tablet/narrow layouts, no Android run. Details and metrics: [SHELL_FIDELITY](SHELL_FIDELITY.md).

## Live SSH evidence (21.10.2026, WSL `fortiq-test`)

`tests/SshIntegrationTest.cpp` (`fortiq_ssh_it`, skipped unless `FORTIQ_IT_HOST/PORT/USER/PASSWORD/FINGERPRINT` are set) ran against Debian 13 / OpenSSH 10.0 in WSL on `127.0.0.1:2222`, MSVC 2022 + Qt 6.12.0 + libssh2 1.11.1 (vcpkg): **6/6 passed** — unknown host returns trust-required with the fingerprint read independently on the server, a wrong pin blocks before authentication, a wrong password fails, the pinned host lists the home directory including `Mes documents` and `Notes été.txt`. libssh2 negotiated the **ECDSA** host key, not ED25519: compare against all `ssh-keygen -lf /etc/ssh/ssh_host_*_key.pub -E sha256` lines. The GUI path (typing into the Files window) has not been driven automatically. WSL stops an idle distro, which kills sshd; keep it running during the test.

**Private-key login (21.10.2026):** `libssh2_userauth_publickey_fromfile_ex` with a local OpenSSH key path (passphrase from the RAM-only password field; only the path is remembered). Verified: WSL `fortiq-test` with a generated ED25519 key (7/7) and a real OVH Debian VPS with the owner's ED25519 key (host-key trust prompt + key login + home listing). No ssh-agent / Pageant, no encrypted-key test yet.

## Vertical slices to deliver (updated binding priorities)

| Milestone | User value | Engineering acceptance |
| --- | --- | --- |
| **R0.1 — existing source baseline** | Preliminary connect and read-only SFTP navigation | Not yet proven on Windows and Android; existing code is not pixel-fidelity UI |
| **R0.2 / Sprint 1 — exact shell & session** | Windowed desktop **matching the owner's sketch**, authenticated persistent SSH + SFTP listing | screenshot/diff at master 1536×1024, correct real Window/Android build, fingerprint checks, WSL/VPS |
| **R1 / Sprint 2 — Smart Files** | Remote search/collections/previews and safe transfers with visible tasks; minimum Terminal | real Debian and Android/Windows acceptance, conflict/permission/offline testing, Task §unknown§ |
| **R1.5 / Sprint 3 — Logs flagship** | Scoped journald and service logs, filters, live tail, repeated error groups, explainable evidence-linked summaries without AI; System/Services support | real journal and permissions, rotation and network loss, UI material fidelity |
| **R2 — Editor/Docs/Mail + server setup** | Deeper everyday Linux working apps | native/CLI proofs, explicit consent on changes |
| **R3 — Browser/media/licensing/release** | Larger product reach | verified SSH proxy without fallback on both platforms, commercial release gates |

Android and Windows are **both first-class from Sprint 1**; no milestone moves Android into a late “platform phase”. Shell fidelity is **blocking**, not optional polish.

### Browser Android constraint

Qt WebEngine and Windows WebView2 do not provide a single browser component suitable for the two target platforms. Android WebView through Qt WebView is a different platform backend, and reliable per-webview SSH proxy routing is **not yet demonstrated**. The browser app is **deferred**, not replaced with an Android direct-network shortcut that violates the product's SSH contract. An actual compatible transport needs testing before UI work.

### Credential limitations in R0.1

- Password or local private-key file (unencrypted or passphrase); no ssh-agent yet. Password remains only in process RAM until disconnect; no persistent work data/cache is written.
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

**Authority:** [SSH_FRONTEND_CONTRACT](SSH_FRONTEND_CONTRACT.md) defines work transport and persistence; [UI SPEC](UI_SPEC_V1.md) visual fidelity; [PRODUCT_SCOPE](PRODUCT_SCOPE.md) product scope; [Smart Files](SMART_FILES_SPEC.md) and [Logs](LOGS_INTELLIGENCE_SPEC.md) feature contracts; this migration and [DEVELOPMENT_MATRIX](DEVELOPMENT_MATRIX.md) milestones; [README](../README.md) only current implemented source status. Historical Flutter tests at `flutter-final` do not qualify Qt features.
