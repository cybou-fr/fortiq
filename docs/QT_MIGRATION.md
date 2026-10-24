# FORTIQ — Qt 6 / C++20 migration, Windows & Android first

23 October remembered secrets (Windows): the login screen has "Se souvenir du mot de passe". After a successful login
the password (or key passphrase) is stored in the Windows Credential Manager (`SecretStore`, target `FORTIQ/ssh/<server id>`,
per-user DPAPI, never in QSettings or a file); the profile keeps only a `secretSaved` flag. Clicking a server with a remembered
secret connects at once; "Oublier" deletes it. Removing a server deletes it; editing host, port, user or key path drops it so it
can never reach another target. If the store is unavailable (platforms other than Windows and Android) remembering is refused with an
explicit message and nothing is written. **Android (23.10.2026):** `fr.cybou.fortiq.SecretVault` (Java, `android/src`)
keeps one non-exportable AES-256-GCM key in the Android Keystore; the app stores only `iv || ciphertext` in its private
settings, bound to the server id as authenticated data (a blob copied to another id, or altered, is rejected). The C++ side
calls it through JNI. **Not done: ssh-agent/Pageant.** Verified: `fortiq_credentials` QtTest
(real Credential Manager round trip incl. Unicode/quotes, 5000-byte refusal, edit/remove/forget rules, nothing in QSettings) and the
layout test with a fake controller; not run against a live server, nor on a clean machine. Android: the developer flag
`fortiq --secret-store-selftest` (throw-away entry, prints PASS/FAIL, exit code 0/4) passed on the FortiqTablet emulator
(API 37, arm64 translation, 23.10.2026): Unicode/quote round trip, no plaintext in the stored blob, blob rejected under another
server id, tampered blob rejected, remove. Not verified: a full login with "Se souvenir" on Android (no SSH test account was
used), survival across a device reboot, a physical device, key invalidation after a screen-lock change.

23 October login (owner decision): connecting to a server now works like a Linux login. A full-screen login
screen (`ServerManager.qml`, saved servers as account tiles, password or key-passphrase prompt, first-use
fingerprint step, add/edit/remove) is shown at start and after logout; it replaces the previous Servers dialog,
so Files/login no longer appear together. The top-bar indicator opens a session menu with
"Se déconnecter", which protects unsaved MDok drafts, closes all windows, ends the SSH session and returns to the
login screen. The connection indicator (dot + server name, ▾) sits at the right of the top bar in place of the user avatar and
opens that menu; the reference scene keeps the sketch layout. A dropped connection does not log out: windows stay, the menu offers logout. Layout QtTest covers
the login screen, secret entry and logout with a fake controller; the real connect-then-close path is not
covered by an automated test and was not run against a live server in this slice.

23 October shell: the production right panel is a floating card against the right edge and contains the wallpaper switcher
(owner decision, [UI SPEC](UI_SPEC_V1.md)); layout QtTest 47/47 on Windows MSVC. The layout test source also
hides one raw string from moc (moc silently generated nothing since the MDok slice) and its launcher
expectation was updated for the 9-app bottom panel. Not visually accepted by the owner; Android not rebuilt.
Production now starts with no app window open (Files is opened from the panel); the reference fixture still opens it.

23 October MDok: [first Markdown editor](MDOK.md), independent RAM document, pinned SFTP,
existing safe-save/copy primitives, protected draft and Qt preview without resource loading. PDF pending.

22 October Disks slice: [read-only storage overview](DISKS.md), lsblk/findmnt JSON over pinned SSH,
partial-source errors and server-generation isolation. No partition/mount mutation or health reporting.

22 October follow-up: [Terminal](TERMINAL.md) now has an interactive SSH PTY, a libvterm screen,
auto-start on an opened window's connected source and server-generation isolation.
The primary screen now retains up to 1000 output lines in RAM (wheel, scrollbar, Shift+PageUp/PageDown);
alternate-screen input remains separate. Mouse selection and selection-first copying are available;
selection clears when the displayed grid changes. Local literal search covers output and scrollback
(Ctrl+Shift+F), with bounded results and explicit invalidation on new output. The regular window layout now includes saved Bash history (click types a command without Enter). Shared WindowDragArea serves Files/Logs/
Browser/Terminal. Windows/Android builds, 37 layout cases and two live PTY/controller cases passed
(4 live QtTest passes including init/cleanup). Full Android IME/TUI qualification is still pending.

**Decision:** single C++20 + Qt Quick/QML codebase for Windows and Android. Linux server remains the sole application backend. **Git workflow:** update `main` only; preserved Flutter sources at tag `flutter-final`. This is an implementation plan with a truthful snapshot of the Qt progress.

## Android foundation slice — 22.10.2026

Adaptive shell uses real viewport units below 1280 logical pixels or in portrait: one Files/Logs
foreground surface, touch toolbar, drawers, scrollable app/filter panels, narrow incident tabs,
Back/IME handling and retained editor state. Desktop reference geometry remains at 1536×1024.
Local document URIs stay encoded; QFile reads selected keys into bounded RAM for libssh2 memory
key auth, with no disk key copy. SAF downloads finish a private temporary before opening the provider;
non-atomic provider failure/cancellation after opening is explicitly `unknown`. Native downloads
retain QSaveFile. The manifest omits broad storage permission requests.

**Actual builds:** Windows MSVC and Android arm64 (Qt 6.12, NDK 28.2, JDK 21, SDK 36, min API 28).
OpenSSL/libssh2/zlib were built for Android; a debug-signed APK was produced and signature/manifest/ABI
checked. FortiqTablet AVD (API 37, x86_64 with arm64 translation) passed installation, pinned key
authentication, SFTP navigation, rotation/IME/Back with dirty-editor protection, remote UTF-8 save,
and native SAF upload/download with byte-for-byte round-trip verification. Physical hardware and
GPU/visual acceptance remain unqualified. Window-level Back routing and an explicit private-cache
download staging directory fix failures found in that run. Current Windows regressions: layout
26/26, local-document 10/10, live publication 28/28, including failed download preflight preserving
the destination. Earlier full CTest was 7/7; the changed targets were rerun in this slice.
Detailed evidence, commands and pending device checks:
[Android foundation](ANDROID_FOUNDATION.md).

## File publication reliability slice — 22.10.2026

Upload Replace no longer moves the old pathname to a backup: it publishes with one
`posix-rename@openssh.com`, exactly as editor save does. There is no fallback that removes the
original pathname. No-overwrite rename remains for Fail/KeepBoth and an initially absent Replace
destination. Existing regular-file replacements preserve mode, uid and gid; unavailable metadata,
foreign ownership that cannot be preserved, initial symlinks/directories and detected concurrent
metadata changes cause refusal/conflict. New uploads keep the server-created 0644/umask mode:
while still empty, staging captures it, switches to verified 0600 for content writes, then restores
the intended mode before publication. Editor staging starts at 0600.

Both paths verify size, applied metadata, supported fsync and CLOSE before publication. A zero-byte
write cannot spin indefinitely. Metadata refusal or a false success that leaves different rights
is not ignored. Empty-upload cancellation is checked before publishing. Editor rechecks content
and destination metadata and validates read-back size, completeness and SHA-256. A lost publication
request/acknowledgement or mismatched editor read-back is `unknown`, never an automatic retry.

**Limits:** SHA checks and metadata checks are optimistic; a generic external writer can modify
a pathname after the final check. Explicit upload Replace has no content-version precondition.
No compare-and-swap, ACL/xattr preservation, hard-link preservation, or power-loss durability
(including parent-directory fsync) is claimed. Unsupported fsync is tolerated. SSH loss before
publication may leave a hidden staging file; the destination has not been published by this attempt.

**Evidence:** Windows MSVC 2022 / Qt 6.12 build; `fortiq_publication` against the dedicated WSL
`fortiq-test` OpenSSH/SFTP server using test user `fqshell` and a test-only key. It creates and cleans
its own random `.fortiq-publication-*` directory under the test user's home. The suite exercises
real SFTP for create/keep-both/replace, Unicode/spaces/quote filenames, mode preservation, staged
concurrent edits/permission changes, a concurrent new name, cancellation and editor read-back.
Deterministic test-only hooks simulate protocol/extension refusal, lost publication request/ack,
metadata refusal/false acknowledgement, flush and CLOSE errors. These hooks are compiled only into
the test executable. The lost-ack cases execute the real rename first; this is **fault injection**,
not proof from physically cutting a network link or crashing the server. No private VPS writes,
Android-device or clean-Windows-machine tests are included.

Final run: publication QtTest **27 passed, 0 failed, 0 skipped**; Logs QtTest **10 passed**
(including its WSL reader); RemotePath **5 passed**; CTest **5/5 targets passed**. The general
`fortiq_ssh_integration` suite explicitly skipped because `FORTIQ_IT_*` was not configured;
this does not negate the independently configured live publication fixture above.

Re-run with `FORTIQ_PUBLICATION_HOST`, `FORTIQ_PUBLICATION_PORT`, `FORTIQ_PUBLICATION_USER`,
`FORTIQ_PUBLICATION_KEY` (local test-key path) and `FORTIQ_PUBLICATION_FINGERPRINT` (independently
verified SHA-256 host pin), then `ctest --test-dir build-msvc -R fortiq_publication --output-on-failure`.
Never set these to a production account: tests mutate only their allocated fixture directory.
Without fixture variables the suite skips explicitly; the detailed result is `publication-results.txt`.

## Reliability slice — 22.10.2026

Journal following now uses `--after-cursor` with `--no-tail`, rather than selecting the newest
2000 entries of an unread backlog. A transport-capped reply checkpoints only complete parsed
events and explicitly reports incomplete coverage. Initial journal views remain bounded tails.

Logs file reads now use an opened descriptor (GNU stat on `/proc/self/fd/3`) and pages up to
256 KiB, below the 8 MiB SSH stdout cap. Headers carry inode, observed size, start, reset and
expected count. Only a successful complete page advances the cursor by received bytes;
failed/truncated/short reads keep the cursor and pending line. Incomplete UTF-8 lines span pages;
long lines retain only a bounded display prefix (4000 characters), without stalling byte progress.
Initial/replaced files show a bounded tail, omit its cut first line and explicitly mark limited
history; following shows remaining bytes when behind. The timeline stays capped at 2000 events.
Rotation cannot recover writes to a file replaced between polls; copy-truncate followed by regrowth
between polls remains undetectable from inode/size alone. This is a bounded viewer, not a lossless archive.

All Logs completions (timeline, sources, service view, inspector) check the captured connection
generation; sources/service additionally reject superseded requests. Disconnect clears server
sources and service data and invalidates pending work. Changing the selected source invalidates
service/inspector work. Logs consumers distinguish transport success from a nonzero remote exit;
source lists reject truncated output. This does not change `runScript` semantics for other callers.

Verified locally: Windows MSVC 2022 / Qt 6.12 application and test build; Logs QtTest **10 passed,
0 failed, 0 skipped** with `FORTIQ_TEST_WSL=Debian`, including the actual shell reader on Linux
with Unicode/spaces/quote in the filename, 30000 appended lines over multiple pages, initial tail
and rename/recreate rotation. Deterministic cases cover >10 MiB long lines, split UTF-8, short /
truncated / failed responses, stale server/request results and disconnect invalidation.
CTest now supplies matching Qt/libssh2 runtime paths on Windows; QtTest reports (including failures and skips) are saved
as `remote-path-results.txt`, `ssh-results.txt` and `logs-results.txt` in the build directory. No new live SSH, device or clean-machine qualification is claimed.

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

**Persistent session + Smart Files window (21.10.2026):** `SshSession` keeps one authenticated SSH connection and SFTP channel on a single persistent worker thread; listings reuse it, a dropped connection is reopened once with the host pin re-checked, and a RAM-only listing cache makes back/forward instant (cleared on disconnect). Entries carry size, mtime, mode and owner (from the display-only longentry); symlinks are not stat'ed per entry and resolve on open. Measured: WSL — 3 listings in 11 ms; OVH VPS — 3 listings (/, /etc 150 entries, home) in 673 ms, down from 4.6 s with per-link stat. Integration test 8/8 on WSL. Files window: glass, places sidebar, history, clickable/typed breadcrumb, live filter and type-to-filter, grid/list with sortable columns, hidden files toggle, status bar with latency, keyboard shortcuts, drag/maximize/resize. The interactive GUI has not been driven automatically; file opening, preview and writes are not implemented.

**File viewer (21.10.2026):** `SshSession::readFile` opens a regular file read-only (`LIBSSH2_FXF_READ`) on the persistent session and reads at most a limit; nothing is written to the device. The Files window opens files on double-click / Enter / Space: UTF-8 text (first 1 MiB, line numbers, truncation notice), images png/jpg/gif/webp/bmp/svg/ico up to 16 MiB (fit, 1:1, wheel and keyboard zoom), otherwise a hex dump of the first 1 KiB. Verified on WSL: integration test 10/10 incl. controller-level preview of Cyrillic text, PNG, random binary and a 1.2 MB truncated file. Visual check via `--reference-fixture --fixture-preview text|image`; the live GUI was not driven automatically. No editing, download or external-app opening yet.

**Transfers (21.10.2026, publication details superseded by the file reliability slice above):** download and upload run one at a time on a second SSH connection to the same pinned server (browsing stays responsive), in a queue with progress, cancel and per-transfer states queued / running / conflict / done / error / cancelled / unknown. Download: regular files only, written through `QSaveFile` (local temporary + atomic commit), size verified before commit; cancel or failure leaves no local file. Upload (SMART_FILES_SPEC §Safety): exclusive unique hidden temporary `.<name>.fortiq-upload-<random>`, size verified with `fstat`, `fsync` best effort, then published with SFTP rename, which never replaces an existing name; a name conflict asks the user — keep both (`name (2).ext`), replace (explicit confirmation; the original is moved to a backup name until the new file is in place, then removed) or skip. Cancel removes only the owned temporary; a drop during publication reports `unknown` instead of retrying. Transfers are bound to the server: disconnecting stops them, never redirects them. UI: toolbar buttons, Ctrl+D / Ctrl+U, system file dialogs, drag and drop onto the Files content, transfer panel. Verified on WSL: integration test 12/12 (round-trip byte equality of 3 MiB, conflict / keep both / replace, cancel without leftovers, cancelled download, Unicode names, path escape rejected, controller queue). Not yet: folders, multi-select download, resume, digest check, live GUI driven automatically.

**Folder, rename, Trash (21.10.2026):** new folder (`sftp mkdir`, 0755 before umask), rename within the folder and move to Trash, on the browsing session, one at a time, never replayed after a drop (`unknown`). Existing names are never reused or replaced (`conflict`); names with `/`, `.` or `..` are refused. Trash follows the freedesktop.org Trash specification in `~/.local/share/Trash` on the server (`.trashinfo` reserved exclusively first, then rename into `files/`), so `trash-cli`, `gio` and desktop file managers can list and restore items; verified with `trash-list` / `trash-restore` on WSL Debian 13. No copy-and-delete fallback: items on another filesystem fail with a message. Home, `/`, the Trash itself and items already in it are refused. UI: + button, Ctrl+Shift+N, F2, Delete, right-click menu, glass dialogs with confirmation for Trash, Trash in the sidebar, result notice in the status bar. Integration test 14/14 on WSL.

**Restore and empty Trash (21.10.2026):** in the Trash folder Files shows a banner with Restore and Empty Trash, and the context menu offers Restore / Delete permanently. Restore reads `Path=` from the item's `.trashinfo` and renames it back; an occupied name asks to keep both (`name (2)`), a vanished original folder is refused (nothing is recreated silently), the record is removed after the move. Permanent deletion is allowed only for whole items of `Trash/files` (recursive, symlinks unlinked and never followed, non-UTF-8 names refused rather than guessed) and needs an explicit irreversible-action confirmation; Empty Trash clears `files/` and `info/`, hidden items included. Verified on WSL: integration test 15/15, incl. restoring an item trashed by `trash-put`, conflict / keep both, missing original folder, nested folder with a symlink to an outside folder (target untouched), refusal outside the Trash, empty Trash.

**Edit and safe save (22.10.2026, metadata/flush guarantees strengthened by the file reliability slice above):** complete UTF-8 text files (not truncated, not binary) open for editing from the viewer (Modifier / Ctrl+E); Ctrl+S saves. `SshSession::saveFile` follows SMART_FILES_SPEC edit/save: the server bytes must still hash (SHA-256) to the opened version, new bytes go to an exclusive hidden temporary in the same folder with the original permissions (and group when allowed), size-checked, the version is re-checked, then published atomically with `posix-rename@openssh.com` and read back; no non-atomic fallback. Symlinks are saved through to their target; files owned by another user are refused (an atomic replace would change their owner). A changed server version is a conflict: overwrite (explicit), save a copy `name (n)` or keep editing; a drop while publishing is `unknown` and offers a reload. Line endings (CRLF) and a UTF-8 BOM are preserved; unsaved changes are protected on close and on leaving edit mode. Verified on WSL: integration test 17/17. Not checked on the owner's VPS (no write without consent); hard-linked files lose the link on save (SFTP v3 does not report link counts).

**Server search (22.10.2026):** typing in the Files search field still filters the open folder; Enter searches the server by name and Ctrl+Enter by content, below the open folder (a hint shows both). Search runs on a third SSH connection so browsing stays live, shows progress, can be stopped and returns at most 500 results ("limit reached"). Tools on the server, checked at run time: name — GNU `find -xdev -iname` with `-printf` metadata (plain `-print0` fallback), glob characters made literal, Trash pruned; content — `rg -i -F` (hidden files, `.git` skipped, 3 lines per file, 20 MB file cap) or `grep -rIinF` when `rg` is absent, binary files skipped. The query is data: NUL and line breaks are refused and the value is single-quoted into a POSIX `sh` script sent on stdin to `sh -s`, so the login shell never parses it; stderr is discarded, closing the channel stops the tool. Results show the relative folder, line number and highlighted snippet; double-click opens the file in the viewer or enters the folder, "Emplacement" opens the containing folder. Verified on WSL: integration test 19/19 with grep and with ripgrep 14.1.1, incl. command-injection attempts (`'; …`, `$(…)`, backticks, `"`) that created nothing. The owner's VPS has `find`/`grep` but no `rg`; search was not run there.

**Logs, first slice (22.10.2026):** a glass Logs window (launcher → Journaux) with its own SSH connection, read-only. Sources: system journal, the user's journal (`--user`), one service (`-u`, list from `systemctl list-units`, failed first) and plain files under `/var/log` (unreadable ones listed with a lock). Filters: period (15 min, 1 h, 24 h, 7 d, current boot), severity threshold (`-p 0..N`), local text filter. Bounded to the latest 2000 events. Live follow polls every 3 s with the journal cursor (`--after-cursor`) or, for files, inode + size: exactly the new bytes are read; truncation or rotation is detected and stated. Deterministic intelligence, no AI: counts by severity and repeat groups (numbers, hex, UUIDs and IPs masked for grouping only) with first/last time and worst severity; a group filters the timeline. Explicit states: limited journal access (decided from `id` — root, adm, systemd-journal, wheel — because `journalctl -o json` prints no hint), `journalctl` missing, file denied (no escalation), rotation, source disconnected (view frozen, follow stopped). Log text is plain text only. Commands are POSIX `sh` scripts sent on stdin to `sh -s`, values single-quoted (`SshSession::runScript`). Verified on WSL: integration test 21/21 incl. three `logger -p user.err` lines grouped, append and rotation of a file, `/var/log/fail2ban.log` denied, disconnect. Not done: incident inspector, synopsis, Services view, kernel log, multi-source correlation, VPS run.

**Logs incident inspector (22.10.2026):** from an expanded timeline event, "Inspecter autour" reads every unit of the current journal scope (system or user) at severities 0..6 within ±1 / ±5 / ±15 min (`journalctl --since=@… --until=@…`, bounded to 2000), or the loaded lines of a file source. `LogsController::synthesize` is a pure, tested rule set — no AI — producing four sections, each statement linked to event references: **observed** (window totals, chosen event, first error, per-unit error/warning counts and times, systemd lifecycle messages: process exit, failure, scheduled restart, start-limit, started/stopped; repeats), **inferred** (only labelled "Hypothèse": order of first errors between units "proximité dans le temps seulement", a restart or failure following errors of the same unit, patterns that appear only after the chosen event), **unknown** (cause not determined — always; limited access; 2000-event cap; debug and /var/log not included; no errors in the window) and **next** read-only checks (open the unit's 24 h journal, `systemctl status` as text, widen the window). Clicking a statement highlights its events; "Copier le rapport" copies a plain-text report with method and referenced events. User services are now named from `_SYSTEMD_USER_UNIT`. Verified on WSL: integration test 23/23 including a real failing user service (`systemd-run --user`, Restart=on-failure) whose restart the inspector reports as observed and links to its error as a hypothesis. systemd messages are matched in English only.

**Logs service view (22.10.2026):** selecting a service opens its read-only status: `systemctl show --timestamp=unix` (state and since when, unit-file state, last result, main PID, automatic restart count and policy, memory, CPU time, tasks, exit status, unit file path), a 7-day chart of starts / restarts / failures and the lifecycle history parsed from the unit's journal (`journalctl -u … --since=-7d`, bounded to 5000): started, stopped, process exited, failed, restart scheduled, start limit hit. User services (`systemctl --user`, `journalctl --user`) are listed next to system ones and tagged. As an unprivileged user the status of system services is readable but systemd's messages about them are not: the history is marked limited instead of looking empty. Unknown units report "n'existe pas". FORTIQ never starts, stops or edits units. Verified on WSL: integration test 25/25 incl. a failing user service (failed / exit-code, restart, start limit, started in history) and `ssh.service` with limited history.

**Windows package (22.10.2026):** `tool/package_windows.py` turns the MSVC Release build into `build-dist/`: a staged folder (windeployqt with QML imports from `qml/`, QML debugging plugins skipped; libssh2, OpenSSL 3 and zlib DLLs found by following imports; MSVC runtime DLLs app-locally, no vc_redist run on the target; license texts and THIRD_PARTY_NOTICES), a portable ZIP and a Qt Installer Framework 4.11 offline installer (per user, `%LOCALAPPDATA%\Programs\FORTIQ`, no admin rights, Start menu entries, maintenance tool). Every shipped .exe/.dll is checked with `dumpbin /dependents`: each import is shipped or a Windows system DLL (0 unresolved). Size of 0.1.0: 1502 files, 116 MiB installed, 45 MiB ZIP, 65 MiB installer — unused Qt Quick Controls styles and modules are still deployed. Verified **on the development machine only**: silent install (`install --root … --accept-licenses --confirm-command`) into an empty folder, start with `PATH` limited to Windows folders (shell rendered, libssh2/OpenSSL loaded from the install folder), purge uninstall removed the files, shortcuts and the HKCU uninstall entry; FORTIQ's own settings (HKCU\Software\CYBOU\FORTIQ: last server, host pins) are kept by design. **Not yet verified on a clean Windows machine** (no Windows Sandbox here; the owner's Hyper-V VMs were not used without consent), not code-signed, Qt commercial licensing still open.

**Global server manager and single app panel (22.10.2026, owner decisions):** SSH is no longer configured inside Files. `RemoteController` keeps a list of saved servers in QSettings (`servers/profiles`: id, name, host, port, user, key path — never a password or passphrase; host pins stay keyed by host:port; the previous single "last server" is migrated once). `ServerManager.qml` (opened from the server label in the top bar, and automatically at start when offline) lists them with state, auth kind and pin status; connect asks for a password or an optional key passphrase only in RAM; first-use fingerprint verification stays mandatory; add / edit (with a key file picker) / remove (two-step). Files and Logs show "Aucun serveur connecté — Choisir un serveur…". The rail and the dock are replaced by `AppPanel.qml`, one panel docked left or bottom (the choice is remembered on the device as a display preference, owner decision 22.10.2026: QSettings `display/panelEdge`). Integration test 26/26 on WSL incl. saved servers (validation, no password stored, edit keeps id, connect through a profile with the trust step, remove the active server disconnects).

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

- Password or local private-key file (unencrypted or passphrase); no ssh-agent yet. Password remains in process RAM until disconnect unless the user ticks "Se souvenir" on Windows (Credential Manager, see above); no persistent work data/cache is written.
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

23 October refactor, step 1 (no behaviour change): `src/ssh/SshClient.cpp` (1383 lines) is split into
`SshSession.cpp` (connect, host-key pin, auth, list, read), `SshTerminal.cpp`, `SshTransfers.cpp` (download/upload),
`SshFileOps.cpp` (mkdir/rename), `SshTrash.cpp`, `SshEdit.cpp` (safe save/copy), `SshExec.cpp` (search, runScript) and the
internal `SshInternal.h`; the code was moved verbatim by script. The public header is unchanged. Verified: Windows build,
CTest 11/11, and the live `fortiq_publication` suite **28/28** against the disposable WSL `fortiq-test` SFTP server (the
distro must be kept running during the test or sshd stops). The general `fortiq_ssh_integration` suite stayed skipped.

Refactor, step 2 (no behaviour change): `RemoteController.cpp` is split into `RemoteController.cpp` (construction, request
pipeline, navigation, disconnect), `RemoteProfiles.cpp` (saved servers, remembered secrets, connect), `RemotePreview.cpp`,
`RemoteTransfers.cpp`, `RemoteOperations.cpp` (folder/rename/trash, safe save) and `RemoteSearch.cpp`, plus the internal
`RemoteControllerInternal.h`; code moved verbatim by script, public header unchanged. Verified: CTest 11/11 and the live
`fortiq_ssh_integration` suite **27 passed, 0 failed, 3 skipped** (logs service fixtures) against the disposable WSL
`fortiq-test` server, with a key login, the upload/trash/save/search fixtures under `/home/fqshell/it`, and a throw-away
`fqshell` password set on that disposable distro for the run.

Refactor, step 3 (no behaviour change): `LogsController.cpp` is split into `LogsController.cpp` (state, sources, filters,
follow, journal/file scripts, apply), `LogsAnalysis.cpp` (repeat groups, journal-line and service parsing, history/daily counts),
`LogsInspector.cpp` (evidence-linked synthesis, incident inspector, report), `LogsServices.cpp` (service and source loading)
and the internal `LogsControllerInternal.h`; moved verbatim by script. Verified: CTest 11/11, Logs QtTest **11 passed, 0 skipped**
with `FORTIQ_TEST_WSL=Debian` (real Linux reader), and the live integration suite **27 passed, 0 failed, 3 skipped**.

23 October System (read-only, [SYSTEM](SYSTEM.md)): the Système app lists processes (CPU, memory, state, threads, command; filter, sort, auto-refresh every 5 s,
details of the selected one), systemd services (failed first, filters) and an overview (load against cores, memory/swap, uptime, OS, systemd state) of the selected
server, over one persistent SSH session. Existing tools only (`ps`, `systemctl list-units`, /proc files); the script contains no command that changes anything
(tested). Verified: `fortiq_system` 12 unit tests with real Debian outputs, the live `systemAppReadsTheServer` test, a layout test with a fake controller, CTest 15/15.
Not done: stopping/restarting services, signals to processes, user services, process tree; Android not run on a device.

23 October Network (read-only, [NETWORK](NETWORK.md)): the Réseau app shows interfaces (addresses, counters), listening ports with who can reach them
(all interfaces / local only / one address), routes and DNS of the selected server, and runs resolve/ping/trace there as cancellable tasks. Existing tools
only (`ip -j`, `ss`, `getent`, `dig`, `ping`, `mtr`/`tracepath`); targets are validated before anything is sent. Verified: `fortiq_network` 14 unit tests with
real outputs of the test Debian, the live `networkAppReadsTheServer` test (sshd listed as reachable on all interfaces, a cancelled ping to a silent address,
session clearing), and a layout test with a fake controller; CTest 14/14. Not done: any change to the network, packet capture, port scanning, firewall;
Android not run on a device.

23 October Browser (owner decision: "a GUI over curl"): the Navigateur app fetches pages with `curl` on the selected Linux server over
SSH and shows them as safe text without JavaScript, styles or automatic resource loading ([BROWSER](BROWSER.md)). It opens from the
panel/launcher, keeps history in RAM, offers a reader mode and a server-response panel (HTTP version, remote IP, timings, redirects,
TLS verification, headers), and each load is a cancellable task. Verified: `fortiq_browser` 42 unit tests (URL rules, script quoting,
response parser, charsets, sanitizer incl. 3000 random inputs), the live `browserFetchesThroughTheServer` test against a fixture web
server on the disposable WSL Debian 13 (curl 8.14.1; redirects, `file://` redirect refused, DNS failure, loop, 6 MiB refusal, JSON,
ISO-8859-1, binary, JavaScript-only page, stop of a slow page), and a layout test with a fake controller. Not done: images on request,
forms, cookies, page search, bookmarks; Android not run on a device. The sketch window `BrowserWindow.qml` remains only in the reference scene.

23 October TaskManager (first slice): one task model for the whole client (`TaskManager`, [ARCHITECTURE](ARCHITECTURE.md) §4) replaces
the Files-only transfer queue. States queued / running / cancelling / cancelled / succeeded / failed / unknown; terminal states are
final; a cancel is a request (a queued task is cancelled at once, a running cancellable one becomes `cancelling` and its owner
stops the work; tasks that cannot be stopped, such as saves and Trash moves, refuse it); a name conflict returns the task to
`queued` with a pending decision (keep both / replace after confirmation / skip); tasks are bound to the server session
generation, and a disconnect settles them (`unknown` when a mutation may have happened, otherwise `cancelled`). Registered:
uploads, downloads, folder/rename/Trash/restore/delete/empty operations, safe saves and server searches. The Files-local
`TransferPanel` is replaced by a shell-level `TaskPanel` fed by the manager (bottom right). Behaviour changes worth knowing:
an operation or save interrupted by a disconnect is now reported as `unknown` instead of disappearing silently, and a search
is listed as a task. Verified: `fortiq_tasks` unit tests (18), layout test with a real manager (panel, cancel request, clear),
CTest 12/12, live `fortiq_ssh_integration` 27 passed / 0 failed / 3 skipped (transfers, conflict decision, operations and search
as tasks) and `fortiq_publication` 28/28 on the disposable WSL server. Not done: Logs, Documents/MDok, Disks and Terminal do not
register tasks; no persistent task history; the Android debug APK builds (not run on a device for this slice); the fortiq.exe/installer were not rebuilt
because a running instance locked the file.

Refactor, step 4 (no behaviour change): `FilesWindow.qml` (1038 → 494 lines) now composes `FilesSidebar`, `FilesToolbar`,
`FilesTrashBanner`, `FilesGrid`, `FilesList`, `FilesStatusBar` and `FilesFixtureData` (visual-only reference samples); the code
was moved by script with `win` renamed to the injected `files` property, and the same ids/objectNames are kept. Verified: CTest
11/11 (layout 47/47) and a pixel comparison of all 66 layout captures before/after: the 58 deterministic ones are identical
(tolerance 24/255), the other 8 differ only by the live clock. Not covered by automated tests: the sort menu, context menu
and drag-and-drop upload (moved verbatim, not exercised).

23 October Databases (read-only, [DATABASES](DATABASES.md)): the Bases de données app runs SQL on the selected server through `sqlite3` and `psql` over SSH (SQLite files and
PostgreSQL), with connection profiles bound to a saved server whose passwords live only in the client's protected store, a schema tree, a result table that tells NULL from an
empty string, time and row limits and cancellable `dbQuery` tasks. Read-only is layered (SQL scanner that also refuses `\` and `.` meta-commands, `sqlite3 -readonly`, a read-only
`psql` session with `statement_timeout`) and documented as an accident guard, not a security boundary. SQL and the password travel inside the stdin script only. Verified: `fortiq_db`
19 unit tests, the live `databaseAppIsReadOnlyAndBounded` test on the Debian 13 fixture (live suite 31 passed, 3 skipped), a layout test, CTest 16/16 and the Android arm64 build. Not done: MariaDB, writes, export, query history; Android not run on a device.

23 October Mail (read and tags, [MAIL](MAIL.md)): the Courrier app is a GUI over `notmuch` on the selected server (mail is fetched there by the user's own tools): folders with counts,
search in notmuch syntax, conversations with decoded headers, nested replies, attachment names and safe HTML (the browser sanitizer: no scripts, styles, forms or remote images; links shown, never
opened). The only changes are tags (read, flagged, archive, trash marker). Queries, ids and tags are single-quoted data after `--`. Verified: `fortiq_mail` 14 unit tests on real notmuch 0.39 outputs
(`tool/make_mail_fixture.sh`), the live `mailAppReadsAndTags` test (live suite 32 passed, 3 skipped), a layout test, CTest 17/17 and the Android arm64 build. Not done: sending (`msmtp`), account
wizard, sync, attachment download, PGP; Android not run on a device.

23 October Mail, slice 2 ([MAIL](MAIL.md)): sending and the account wizard. The client builds the message (RFC 2047 headers, UTF-8 base64 body, Bcc in the envelope only, header-injection checks) and `msmtp` on the
server delivers it from the stdin script; a copy goes to Sent with `notmuch insert` and the parent gets `replied`; a timeout or a dropped SSH link is reported as "unknown", never as sent or failed. The wizard
writes `~/.mbsyncrc` (Sync Pull), `~/.msmtprc` and, only if absent, `~/.notmuch-config` with the password inside the files (mode 0600, the owner's decision) only after the exact text is shown (password masked until
revealed) and the user ticks each file and a consent box; existing files keep their content (FORTIQ manages its own marked block, timestamped backup, atomic replace). Connection tests (`msmtp --serverinfo`, `mbsync -l`)
and a sync (`mbsync` then `notmuch new`) run as tasks. Verified: `fortiq_mailsend` 17 unit tests, the live `mailSendAndWizard` test against dovecot and an SMTP sink on the disposable Debian (live suite 33 passed,
3 skipped, repeated twice), a layout test, CTest 18/18. Real findings: msmtp treats " #" as a comment (the password is written quoted), refuses `auth on` without TLS, and mbsync needs `\` and `"` escaped.
Not done: attachments (download and send), reply-all/forward/drafts, PGP, OAuth accounts; Android not run on a device.

23 October Mail, slice 3 ([MAIL](MAIL.md)): attachments stay on the server. "Sur le serveur" writes the decoded MIME part (`notmuch show --format=raw --part=N`) into `~/Downloads` there without overwriting; "Sur ce poste" extracts it
to a private temporary copy on the server and fetches it with the ordinary Files download task (progress, cancel, conflict prompt), removing the copy when that task ends. File names are sanitized and quoted, only
`~/.cache/fortiq-mail/att.*` can be removed. Verified: unit tests for names, scripts and the cleanup guard, the live `mailAttachmentsStayOnTheServer` test (decoded content, collisions, hostile name, bad part, real download
task with a conflict, temporary copy gone; live suite 34 passed, 3 skipped), a layout test, CTest 18/18. Not done: attaching files to outgoing mail, opening attachments in place; Android not run on a device.

23 October Architecture & Reliability Gate ([AUDIT_2026-10-23](AUDIT_2026-10-23.md)): acceptance matrix per app (implemented / unit / integration / layout / device), invariant matrix, risk register and ordered backlog. Real defects found and fixed:
the sqlite3 shell's `writefile()`/`readfile()`/`edit()`/`load_extension()` bypassed the read-only database mode (`-safe` now mandatory, fails closed, plus a function denylist; PostgreSQL side-effect functions denied), mail-wizard backups
and saved attachments were readable by other local users (now 0600), an interrupted send was forgotten after reconnecting (now "unknown" until dismissed; proven that the message really can be delivered), parser rows longer than the header,
recipients starting with "-". New evidence: 88-statement adversarial database corpus against real clients, session-switch and late-reply tests across five apps, interrupted-send, failed-mutation and secrets-in-settings checks, structured HTML-attack
and mutated-JSON/CSV property tests, the 28 publication tests actually run, and `tool/run_live_tests.py` to reproduce everything on a disposable Debian. Not done: device/clean-Windows qualification, CI, sanitizers, a real fuzzer, two distinct hosts.

23 October Audit backlog 1 ([AUDIT_2026-10-23](AUDIT_2026-10-23.md) R2, R7): TaskManager tasks now carry three flags — `guards` (ending the session must ask: writes, transfers, sends, an open Terminal), `session` (a read, a view or an interactive
session: a connection change settles it as cancelled, not "unknown") and `transient` (a successful or cancelled read leaves no line in the list; failures stay). Logs (loads, sources, service view, incident inspector as transient reads; live follow as
one cancellable task), MDok (open = transient read; save and save-copy = guarding writes with success / conflict / unknown outcomes), Disks (cancellable transient read) and Terminal (the shell session as a guarding, cancellable task: closing it
is a cancel, a shell that exits is a success, a failed link a failure) report to it; read-only database queries, diagnostics, page fetches and mail tests use the same flags. A modal `SessionGuard` lists the guarding tasks before logout, the
"Se déconnecter" link, connecting to another server, removing the open server and closing the window, and "Attendre" changes nothing. Verified: `fortiq_tasks` 21 unit tests, layout test of the dialog (stay, Back, quit, continue, reads do not ask),
live `everyAppReportsItsWorkToTheTaskManager` (all four apps, including a real save conflict and a real shell exit/drop), full live suite 37 passed / 3 skipped, CTest 18/18. Not done: Système has no task (instant read); the dialog was only exercised
on the desktop layout; Android not run on a device.

23 October Audit backlog 2 ([AUDIT_2026-10-23](AUDIT_2026-10-23.md) R1) — Navigateur address policy, owner's choice "block by default, allow by explicit confirmation" ([BROWSER](BROWSER.md)): the server resolves the host (`getent ahosts`), the client classifies what came
back (`UrlPolicy`): public addresses open; internal ones (loopback, private, CGNAT, ULA, reserved) ask first, before any request, and are allowed once for that host and port during that navigation; cloud-metadata addresses and names, link-local, multicast and
unspecified addresses are never opened. curl no longer follows redirects: the client reads every `Location` and applies the same rules to each hop, curl is pinned to the approved address (`--resolve`, no second lookup), ignores `~/.curlrc` and any proxy
(`-q`, `--noproxy '*'`); odd IPv4 spellings and IPv4 inside IPv6 / NAT64 / 6to4 are judged by the address they mean. Verified: `fortiq_browser` 108 checks, a layout test of the question, the live `browserAddressPolicy` test against a fixture with
public-looking loopback aliases (nothing reaches the internal service before the answer; redirect public -> loopback asks; metadata never opens; the pinned address is the one connected to), live suite 38 passed / 3 skipped, CTest 18/18.
Not done: a persistent allow-list of hosts, proxy-only servers, Android not run on a device.

23 October Audit backlog 3 ([AUDIT_2026-10-23](AUDIT_2026-10-23.md) R4) — database role check ([DATABASES](DATABASES.md)): when a profile is opened FORTIQ reads what the connected role may do with a fixed catalogue query (superuser, file/program/write-all roles, write
privileges on tables and views, create rights, replication) or, for SQLite, the SSH user's rights on the file, and shows a banner (ok / note / warning / danger / unknown) with the findings and a copyable read-only role recipe that FORTIQ never executes; the
check never blocks anything and says plainly that function side effects are invisible to it. Found on the way: saving a new profile left the previous profile's rows, schema and results on screen (fixed, D8). Verified: 6 new unit tests, a layout test of the banner,
the live `databaseRoleCheck` test on real roles (owner -> warning, read-only -> ok, file role -> warning, write-file and superuser -> danger, SQLite note/ok) including proof that the read-only role cannot write without any FORTIQ option, full live suite and CTest.
Not done: MariaDB, side-effect functions are invisible to a catalogue query; Android not run on a device.

24 October Audit backlog 4 ([AUDIT_2026-10-23](AUDIT_2026-10-23.md) R6) — layout-test stability and CI: the layout test no longer sleeps fixed times (44 `qWait` calls removed); after each action the scene is brought up to date (`settle`: events + one synchronous render) and checks wait for their condition (`QTRY_*`, 20 s bound reached only on a real failure);
the task-panel Cancel click repeats until it takes effect (it arrived before the panel was in place under load). `tool/stress_layout.py` runs the check under CPU load; `tool/ci_check.py` is the gate (docs consistency, scripts compile, configure/build, ctest, optional stress);
`.github/workflows/ci.yml` runs it on Windows. Not done: the workflow was never executed on GitHub (YAML parsed locally only); live tests and Android are not part of CI.

24 October Audit backlog 5 ([AUDIT_2026-10-23](AUDIT_2026-10-23.md)) — sanitizers and fuzzing ([FUZZING](FUZZING.md)): 12 fuzz targets (`fuzz/`) over the HTML sanitizer, the SQL scanner, URL and address policy, mail building/parsing, database, network and system parsers, each with invariants
(inert HTML, one read-only statement, no header injection, safe attachment names, http(s)-only addresses). `fortiq_fuzz_smoke` is a ctest on every platform; `tool/fuzz_linux.sh` runs libFuzzer + ASan + UBSan; unit tests were run under ASan/UBSan on Debian/clang (`-DFORTIQ_SANITIZE=ON`).
Found and fixed: undefined behaviour converting server-supplied doubles (NaN, 1e40) to `qint64` in 13 places (`toInt64`); a read past a string literal in `BrowserTest`; a Windows-only path in `LocalDocumentTest`. Verified: Windows CTest 19/19; Debian sanitizer CTest 16/16; libFuzzer 150 s per target, no further findings.
Not done: hours-long fuzzing, a persistent corpus, libFuzzer in CI, sanitizers for the SSH/QML layers and Android; the Linux CI job was never run on GitHub.

24 October Audit backlog 6 ([AUDIT_2026-10-23](AUDIT_2026-10-23.md), R5/R11) — platform qualification, part done ([PLATFORM_QUALIFICATION](PLATFORM_QUALIFICATION.md)): the current debug arm64 APK ran on the `FortiqTablet` emulator against the disposable Debian fixture (host key checked by hand, key login, desktop, Réseau/Système/Bases/Courrier, rotation, 30 s in the background, server-side session kill and manual recovery).
Found and fixed: Android resolved `sans-serif` to a small-caps system face and `"Consolas"` (33 uses) to an arbitrary one, so the host fingerprint and all addresses were hard to read (`Theme.fontFamily`/`Theme.monoFamily`); four glyphs missing from Android fonts showed as empty boxes (drawn via `SymbolGlyph`). Windows CTest 19/19.
Not done: clean Windows and a physical Android device (owner's checks, checklists written), connection-loss state in the UI (indicator stays green, stale data unmarked, English message), phone-sized AVD graphics unexplained; `tool/android_emulator_smoke.py` was only run for its refusal path (no emulator).

24 October Connection state at a drop ([PLATFORM_QUALIFICATION](PLATFORM_QUALIFICATION.md), [ARCHITECTURE](ARCHITECTURE.md) section 3): a shared `LinkMonitor` (user@host:port, thread-safe, main-thread state) is fed by the SSH layer itself - `closeLost()` at the 19 places where a dead transport is found, success reports from open/list/runScript -
so Files, Système, Réseau, Disques, Journaux, Bases and Courrier need no per-controller code. `RemoteController` exposes `linkLost/linkLostAt/linkText`; the desktop pill turns red and says "connexion perdue"; `LinkBanner` marks the data as out of date with "Réessayer"; a lost link slows the Système auto-refresh to 15 s (each tick is one reconnection attempt).
Connection-failure texts of the SSH layer are French. Verified: LinkMonitor unit test (6), layout (banner, pill), live `aDroppedConnectionIsNoticedAndRecovered` (sessions killed on the server, noticed, recovered, intentional disconnect is not a loss), live suite ssh_it 40 passed / 3 skipped, Windows CTest 20/20, and the same drop on the Android emulator.
Not done: a link that dies without the socket closing (only libssh2 timeouts), the mark in Terminal and Browser, automatic return without a user action, `reconnecting`/`blocked` states.

24 October Audit backlog 7 ([AUDIT_2026-10-23](AUDIT_2026-10-23.md), R10) — a second real host: `fortiq-test-b` is a copy of the disposable Debian made independent by `tool/setup_second_host_fixture.sh` (own SSH host keys, port 2223, user `fqother`, own files and journal, host name `fqhost-b`).
The live test `twoHostsNeverMix` writes different data on each host and switches A -> B -> A without disconnecting in between: Files listings, Système (host name), MDok (document, `canSave` false for the other host, label kept), Logs (journal lines of each host, the frozen display of A says it is A's), Terminal (a new shell on B, no screen of A) and a Système reply of A still on its way when B is chosen never shows in B; B's address with A's pinned host key is refused.
Found and fixed: the Logs display kept after a disconnect or server change did not say whose events they were - `LogsController::dataLabel` and the state text now name the source. Verified: live suite ssh_it 41 passed / 3 skipped (the three need a crash unit), Windows CTest 20/20.
Not done: Courrier, Bases, Browser and Disks on two hosts; the second host shares the machine and kernel of the first (separate file systems, users, keys and host name only); WSL stops idle distros, so both need a `wsl -d <name> -- sleep` holder while testing.
