# FORTIQ — Desktop Blueprint R0.2 → R1 (Windows + Android)

**Status:** implementation blueprint, not proof of implementation. **Baseline:** 2026-10-21 main, C++20 / Qt 6 / QML, libssh2, read-only SFTP listing, no confirmed Windows/Android binary. **Owner choices:** develop on main only; private commercial product; Debian/Ubuntu remote servers.

## 0. Product contract: one page

**FORTIQ is a *local desktop shell for a remote Linux system*.** It renders its own desktop-like experience *inside the Windows or Android application*; it does **not** replace the host OS desktop or stream GNOME/KDE/X11/Wayland pixels.

- **Local / client:** wallpaper, top bar, launcher, app surfaces/windows, dock, quick settings, notifications, keyboard/touch input, rendering, temporary in-memory unsaved edits, minimum protected SSH bootstrap, trusted host keys.
- **Remote / Debian or Ubuntu:** files, working state, CLI execution, jobs, documents, mail tools, workspace persistence, servers' configuration, and other domain computations.
- **The ONLY work transport:** SSH, channels, SFTP, PTY and SSH tunneling via the user's selected Linux server. Do not use direct IMAP/SMTP/FTP/REST for remote apps. License purchasing/validation may use the expressly allowed direct HTTPS exception.
- **No proprietary Linux daemon/agent** and no need for a graphical session on Linux. Reuse existing inspected CLI tools; capability absence must be surfaced clearly.
- **No automatic system changes, no command replay after ambiguous failures.** All actions are bound to an immutable server identity and operation ID. A failed SSH acknowledgement may mean `unknown`, not `failed` or `safe to retry`.
- **Same C++ backend on Windows x64 and Android arm64.** Same declarative QML component library with *different responsive compositions*, rather than forcing draggable desktop windows onto narrow portrait mobile screens.
- **AI optional and offline from product-critical flow.** No extra AI agent/API/daemon. This is not a developer-only console.

**BINDING VISUAL REFERENCE:** the user's **exact 1536×1024 master sketch** (21 Oct 2026), formalized with coordinates, sampled colors, layering and visual acceptance in [UI SPEC v1](UI_SPEC_V1.md). **Not merely inspiration.** Reproduce shown appearance faithfully; complement only missing details. Keep original FORTIQ branding and respect third-party artwork rights. The golden PNG is supplied in conversation and not yet committed to the repo.

### Honest product limits

Only apps implemented by FORTIQ are visualized. A remote Linux package does not automatically get a GUI window. Browser over server SSH proxy is a separately gated later feature: cross-platform embedded browser with enforced per-view proxy on Windows+Android is unproven. Do not allow direct-network fallback.

## 1. Visual system and responsive behavior

**Only [UI SPEC v1](UI_SPEC_V1.md) controls geometry/color/glass.** Screenshot actual composite pixels are documented there. Values in earlier blueprints were tentative and **must not override the screenshot**. Android performance and accessibility require faithful fallbacks, not an unrelated theme.

**Desktop >= 1280 logical dp (Windows or large Android tablet):**
- Top bar matched to screenshot y=0–47 at the reference 1536×1024 canvas: FORTIQ/server identity, centered search, compact indicators and clock.
- Left app rail matched to screenshot x=4–88 (about 84px including glass border), visible Files, Terminal, Editor, Logs and planned-app placeholders. No arbitrary redesign.
- Desktop canvas + wallpaper + unobtrusive time/status widgets; server identity obvious.
- Floating local application surfaces (Files first) with bounds, focus, z-order, close/minimize; never confuse an OS window with a remote desktop stream.
- Right control center overlay: Wi-Fi, connection state, volume only when locally available, transfers/tasks, account; unavailable OS-level toggles must not pretend to control host.
- Bottom dock for *running/favorite apps*, not a duplicate of the left app rail; decide duplicates deliberately (one persistent primary nav).
- Workspace strip is optional **after** multi-window state is reliable.

**700–1279dp:** tablet adaptations as specified in [UI SPEC v1](UI_SPEC_V1.md): 900–1279 landscape retains recognizable shell; 700–899 maximizes app surfaces. Touch targets >=44dp.

**< 700dp:** one app surface at a time, app stack and bottom navigation; top bar compact; control center sheet; never show tiny resizable overlapping windows. Keyboard/IME and Android back semantics supported.

**Persistent settings boundary:** do not locally persist remote working data or workspace snapshots. Persist remote workspace state via server SSH only when implemented, and keep unsaved editing buffer RAM-only.

## 2. Reference architecture

```text
Qt Quick QML Shell (window composition / UX)
    ├─ DesktopScene / AppDock / Launcher / ControlCenter
    ├─ WindowLayer (desktop) OR NavigationStack (phone)
    ├─ ConnectionView / FilesView / TerminalView / SystemView
    └─ QML-bound C++ application/session models
                 │
C++20 domain layer
    ├─ WorkspaceController / AppRouter / TaskManager
    ├─ SessionManager (per immutable ServerId)
    ├─ FileService / TerminalService / SystemService
    └─ HostKeyTrustStore
                 │
SSH infrastructure: libssh2 sessions, SFTP, exec and PTY
                 │
Debian / Ubuntu sshd → standard Linux CLI / files / services
```

No QML view may build arbitrary shell strings, hold long-lived credentials, or decide whether a host key is trusted. Each backend operation returns an immutable ServerId, RequestId, state and typed result/error.

## 3. Ten first C++ classes/services (do not create premature abstractions)

| # | Class | Responsibility | First increment |
| --- | --- | --- | --- |
| 1 | `SessionManager` | Profiles, active ServerId, independent connection status, explicit connect/disconnect | R0.2 |
| 2 | `RemoteSession` | Owns *persistent* authenticated libssh2 connection(s) on an SSH worker; failure and bounded reconnect | R0.2 |
| 3 | `SshTransport` | Typed SSH/SFTP/exec/PTY primitives; bounded I/O; no shell fallback | R0.2 |
| 4 | `HostKeyTrustStore` | Strict host:port pinning, out-of-band fingerprint acceptance, reject changed keys | R0.2; extract existing code |
| 5 | `FileService` | Remote-only listing/stat/navigation initially, then transfer and safe edits | R0.2 → R1 |
| 6 | `TerminalService` | PTY stream, resize, exit, server-bound tmux attachment where present | R1 |
| 7 | `SystemService` | Structured OS/CPU/RAM/disk/capability discovery using vetted CLI | R1 |
| 8 | `TaskManager` | Operation IDs, progress, cancel, `unknown`, no automatic write retry | R1 |
| 9 | `WorkspaceController` | Shared state of running apps/windows, selected server and task overlays | R0.2 |
| 10 | `AppRouter` | Typed, immutable ServerId + ResourceRef navigation, e.g. Files → Editor | R1 |

**Existing objects remain useful:** migrate `SshClient::list()` incrementally into `RemoteSession/SshTransport/FileService`; `RemoteController` becomes a thin compatibility facade or is replaced after contract tests. Keep `RemotePath` and its tests. Avoid duplicating public APIs for the same job.

**Security caveat:** libssh2 is blocking in the current implementation; a cancellable worker owning a session is preferable to one `QtConcurrent::run` and one new SSH handshake per folder. A terminal channel must not starve file I/O. Design channel scheduling or a separate authenticated terminal session deliberately. Do not send secrets through shell argv, process logs or QML persistent state.

**Second flagship:** `LogsService`/`LogEventModel` are introduced with Sprint 3 (not counted in the first 10 foundation classes). Log parsing/aggregation is executed by approved Linux CLI via SSH. Read-only UI summaries must cite source events. [Logs contract](LOGS_INTELLIGENCE_SPEC.md).

## 4. Suggested source tree

```text
CMakeLists.txt
src/
  main.cpp
  core/
    ids/                   # ServerId, RequestId, ResourceRef (simple value types)
    SessionManager.*
    WorkspaceController.*
    AppRouter.*
    TaskManager.*
    RemotePath.*           # existing
  ssh/
    SshTransport.*
    RemoteSession.*
    HostKeyTrustStore.*
  services/
    FileService.*
    TerminalService.*
    SystemService.*
  models/
    FileEntryModel.*       # QAbstractListModel for 3k+ file listings
    AppWindowModel.*
  platform/
    CredentialProvider.*   # Windows and Android implementations, no raw key persistence
qml/
  Main.qml                 # composition ONLY, not all app logic
  design/
    Theme.qml
  shell/
    DesktopScene.qml
    TopBar.qml
    AppDock.qml
    Launcher.qml
    WindowLayer.qml
    ControlCenter.qml
    MobileShell.qml
  apps/
    ConnectionView.qml
    FilesView.qml
    TerminalView.qml
    SystemView.qml
  components/
    GlassPanel.qml
    AppTile.qml
    WindowFrame.qml
    EmptyState.qml
tests/
  unit/
  integration/
  qml/
tool/
  test_server/             # Debian/WSL/VPS fixture setup and cleanup, no prod changes
docs/
  FORTIQ_DESKTOP_BLUEPRINT.md
  QT_MIGRATION.md
```

Create directories/files only at the milestone where a real feature uses them. No plugin system, multi-process service bus, remote agent, local database, or generic abstract framework for five placeholder apps.

## 5. Roadmap: three deliverable sprints (order, not calendar promises)

### Sprint 1 — EXACT sketch shell + persistent SSH, on both platforms

**Build and package:** pin Qt version/kit and libssh2 ABI, Windows x64 executable and Android arm64 debug APK. Record hashes/environment. Fix actual build errors. Verify merged Android permission `INTERNET` and packaging of native libraries.

**SSH:** move from per-listing handshake to worker-owned persistent authenticated session; never report `connected` solely because an old listing succeeded. Preserve out-of-band verified host-key pins, explicit key-change rejection and password-free logging. Add clean disconnect, generation IDs, stale-result suppression, cancellation/timeout. Keep password auth in this milestone; do not silently weaken server SSH settings.

**UI:** split `qml/Main.qml` into DesktopScene, TopBar, AppDock, Launcher, ControlCenter, WindowLayer, ConnectionView and FilesView; **reconstruct** the exact master screenshot geometry/materials as defined by [UI SPEC v1](UI_SPEC_V1.md). Preserve screenshot's bright Files body + dark sidebar and exact panel layering. Include controlled visual-only reference fixture. No fake working toggles or browser.

**Exit:** one verified Debian test account reachable from Windows; one reachable from Android (same VPS or explicitly reachable test WSL host). Launch, connect, list, navigate, disconnect and reconnect on both. Test wrong fingerprint, wrong password, Unicode/spaces, lost network, background/resume on Android. Record logs and test versions.

### Sprint 2 — Smart Files flagship + TaskManager, supporting Terminal

**Smart Files:** [full feature contract](SMART_FILES_SPEC.md): reference-exact white content and dark sidebar, remote collections/search, context preview, properties, user-configured favorites, read-only inspection and SFTP transfers. Use `QAbstractListModel`/pagination/limits, progress, remote temp + verified conflict-safe publish. Never silently retry writes or fake “recently opened” from mtime.

**Terminal:** run native SSH PTY with user-visible stream and resize. Use or evaluate existing permissively licensed terminal component; do not casually create a fully fledged VT/xterm parser. tmux attach is optional and capability-checked. Terminal always tied to visible ServerId.

**TaskManager:** queued/running/completed/failed/unknown/cancelled, with operation ID, audit-friendly sanitized error text, no automatic replay of writes.

**Exit:** on both devices: navigate folders, upload, download, interrupt transfer, resume app and observe truthful operation states. Terminal runs `pwd` / `uname -a` and keyboard/IME input without freezing Files. Test names containing spaces, Unicode and denied permissions.

### Sprint 3 — Logs intelligence flagship + System/Services and quality

**Logs:** implement scoped `journalctl -o json` over SSH with source links, time/priority/unit filters, live follow, permission handling, repeat grouping and a *deterministic* evidence-linked incident synopsis. See [Logs spec](LOGS_INTELLIGENCE_SPEC.md); no required AI.

**Supporting System/Services:** Linux release, hostname, uptime, CPU/memory/disk via guarded CLI, service status for selected log source. Refresh without blocking UI.

**Windowed workflow:** maintain exact sketch UI while opening Files and Logs side by side, with overlapping/focus/minimize/restore on desktop and task switching on Android. Source ServerId remains immutable. Editor remains deferred; do not replace Logs with unrelated utilities.

**UX polish:** keyboard navigation, touch gestures, reduced motion, accessibility contrast, saved wallpaper/theme preference only (local UI setting), connection onboarding including fingerprint help. Put server setup mutation, mail and browser behind later explicit gates.

**Exit:** first-run Windows and Android can connect to Debian/Ubuntu, use Smart Files, run basic terminal/system checks, inspect one service's actual journal, group repeat events and obtain a source-backed synopsis **without AI**. Capture real app screenshots **against the master golden visual**. No claim of finished product or qualified security release.

## 6. Debian fixture and reproducible testing

**WSL2 Debian/Ubuntu (Windows only):**
1. Provision a disposable distro/test user. Install `openssh-server` and `openssh-client`. Start sshd using distro-appropriate service manager; check with `ss -lnt`.
2. Create fixture paths: directory with spaces, Unicode names, empty directory, permissions denied, large directory, test download/upload target. Avoid real secrets and production accounts.
3. On Windows, verify host fingerprint out of band using `ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub -E sha256` inside WSL; connect to WSL's reachable SSH address/forwarding.
4. Remember: **127.0.0.1 from an Android device is not Windows/WSL.** Use an explicitly configured and firewall-restricted Windows-host LAN port forward to WSL, a routed lab host, or preferably a disposable Debian VPS for Android testing. Do not publish WSL sshd to the Internet merely for convenience.

**Disposable VPS Debian 12/13 or Ubuntu LTS:** one non-root test account, key-based sshd where available; test authentication and permissions. No production data. Record known host key, reset script, small immutable fixtures, temporary write area.

**Three test layers:** C++ pure unit (`RemotePath`, ServerId, trust transitions, task result states), headless real sshd/SFTP integration, QML interaction tests with deterministic fake services plus selected real-user acceptance on target devices. Fakes are labeled as tests; product must never silently use them for real operations.

## 7. First code changes, exact order

1. Record current main SHA and baseline; create `docs/FORTIQ_DESKTOP_BLUEPRINT.md` and link it from the README.
2. Obtain **compilable Windows Release + Android debug** with matching Qt and libssh2; resolve toolchains first.
3. Extract immutable ServerId / session operation state and `HostKeyTrustStore` from `RemoteController` without changing the pin algorithm.
4. Build `RemoteSession` with persistent libssh2 ownership in a dedicated worker; convert Files list through it. Keep blocking operations off GUI thread, bounded cancellation.
5. Extract QML panels, navigation and Files; implement desktop/tablet/mobile adaptive behavior against real models.
6. Add disposable WSL/VPS SSH fixture, negative tests and actual Android device run; record results.
7. Only then enable SFTP writes/transfers and Terminal PTY.

**Stop conditions:** no proof of host identity; Android ABI mismatch; worker hangs on network loss; cross-server stale result attached to another window; write repeated after ambiguous failure; direct work network fallback. Do not proceed to cosmetic modules while these P0 blockers remain.

## 8. Not in these sprints

Mail/MIME, WebView proxy routing, full document/PDF pipeline, CPU AI/llama, Linux/macOS/iOS clients, arbitrary remote GUI application streaming, a Docker/Podman admin suite, custom Linux agents or competing server backends.

## 9. Definition of done

- Real build logs for **Windows x64 and Android arm64** tied to exact commit/Qt/libssh2 version.
- Host fingerprint validated before credential authentication; mismatched key refused.
- Test-server source of truth enforced; no local persisted remote work data.
- All nontrivial operations are server-bound; clear `running/success/error/unknown`.
- No hidden fallback/automatic write replay on network loss.
- Windows mouse/keyboard and Android tablet/touch demonstrated with screenshots and interaction.
- README and `docs/QT_MIGRATION.md` updated to match only verified reality; tests executed with logs, not just test names.

**Next engineering instruction:** implement Sprint 1 on `main` only, with exact [UI SPEC](UI_SPEC_V1.md) visual fidelity. Then Smart Files, then Logs. Never mistake docs/mockups or historical Flutter checks for tested Qt functionality.
