# FORTIQ — Architecture Contract

**Status:** target architecture / implementation contracts. Baseline: `cb737744` (2026-10-11).
**Product:** Distributed Desktop Software. See [PRODUCT_SCOPE.md](PRODUCT_SCOPE.md) and [LICENSING.md](LICENSING.md).

## 1. Current architecture (verified in code)

- Flutter Windows application; `lib/main.dart` gates the desktop on `SshClientService.isSessionActive`.
- `lib/widgets/desktop_workspace.dart` owns the window catalog and window state. Most app types are single-instance; Editor can have several windows.
- `SshClientService` manages SSH connection, host-key confirmation, liveness, reconnect and SOCKS forwarding.
- `RemoteShell` and `shQuote/asRoot` reside in `lib/services/shell.dart`.
- Separate `FileService`, `SystemService`, `NetworkService`, `TerminalService`, `MailService`, `ServerSetupService`.
- User profiles, known host keys and update preferences are stored locally; persistent drafts, desktop state and a licensing service have not yet been implemented.
- Windows-only WebView2 browser. Android includes INTERNET permission but needs Windows-only component replacement and device verification.

These statements describe source structure. They do not certify end-to-end behavior on a real server.

## 2. Boundary diagram

    Flutter Desktop Shell
      ├── App Registry + Window/Workspace State
      ├── ResourceLocation + Action Router
      ├── Task Manager + Notifications
      ├── Local Data Store + Credential Store
      ├── Entitlement Client + Local License Cache
      └── Application Services
            ├── Local resources
            ├── Remote hosts → Session Manager → SSH/SFTP/Shell
            └── Accounts → dedicated account adapters

    Paddle checkout (website/external authenticated flow)
                  ↓ verified webhook
    Small FORTIQ Licensing Backend (server secret + entitlement DB)
                  ↓ signed/verified entitlement
    FORTIQ License Client

Keep remote-server management inside the desktop's existing in-process services. The dedicated licensing backend holds payment/webhook secrets and license state, not SSH credentials or client files.

## 3. Resource identity (CORE-03)

Define a typed `ResourceLocation`:
- `kind`: local | remoteHost | account;
- stable `sourceId`: device, saved server ID or account ID;
- resource `path` or opaque resource identifier;
- `capabilities` declared by the owning service.

All file, editor, notification, task and window actions carry the location. Displayed titles, warnings and confirmations show the actual resource owner. A response from an old host/session must never update the current host's view.

## 4. Desktop lifetime (CORE-01/02/08/09)

The desktop starts independently of an SSH session. The connection surface is a window, resource picker or workspace action; existing open local windows remain usable when SSH disconnects. Use one registry for app type, display name, icon, factory and multiple-window policy; keep all 12 existing app types.

Persist non-secret desktop layout, windows, drafts, selected resources and task references. Rehydrate remote views in an explicit disconnected/reconnecting state. Never replay a destructive operation merely because the application restarted.

Concurrency rules:
- asynchronous responses check resource/session identity and widget disposal state;
- background tasks own their lifecycle outside the initiating window;
- modal confirmation and close/disconnect preserve dirty state;
- duplicate startup and overlapping setup jobs must be coalesced per host.

## 5. Session and remote operations (CORE-07/08/11)

Each remote source owns independent connection state: disconnected, connecting, connected, reconnecting, authentication required, untrusted host key. Keep the existing TOFU confirmation; a changed host key must stop automatic connection.

Reconnect is **single-flight per resource**: timer and manual Retry cannot open two sessions concurrently; generation/epoch guards reject late sessions and close abandoned sockets.

Remote operations carry:
- `operationId` and `resourceId`;
- cancellation/timeout and progress when supported;
- `safeToRetry` only if the operation is provably idempotent;
- outcome state succeeded | failed | cancelled | unknown.

If a connection breaks during a modifying command, mark the outcome **unknown** and query actual state before taking another action. Never silently repeat writes, package operations, firewall changes or service restarts.

SFTP should replace shell text parsing for normal directory listing and transfers. Preserve explicit shell execution for system commands and tools.

## 6. Automatic Linux environment preparation

**Automatic preparation is a core feature and remains enabled where privileges permit.** Preserve the current behavior: discover required components, install missing FORTIQ dependencies, keep required packages up to date, and separately honor the user's policy for broader system upgrades.

Improve `ServerSetupService` through:
- distinct detection, dependency installation, app-dependency maintenance and system update stages;
- status/progress for all stages and accurate errors;
- one package-manager job per resource; apt-lock and timeout handling;
- recovery after disconnection and inspection of previously unknown outcomes;
- cancellation/disposal and session-owner checks so one server's result never appears on another;
- capability discovery when packages or privileges differ;
- minimum necessary privileges and specific security review of suggested sudo policies.

Server bootstrap and environment preparation should require no manual package commands in the normal privileged flow. Do not regress automatic dependency installation while strengthening privilege scope and lifecycle.

## 7. Shared tasks, secrets and privacy (CORE-04/05/06/10)

Tasks: single observable center for uploads, downloads, installs, updates, scans and long-running queries. A task has source, label, state, progress (when available), timestamps and an actionable error. Notification clicks navigate to the original resource.

Secrets: OS-secured credential storage for tokens, SSH passphrases and future account credentials; secrets never go into preferences, drafts, desktop restore snapshots, diagnostic archives or logs. Keep profile metadata in the local store. Determine secure storage adapters separately for Windows and Android.

Files and documents: conflict detection before overwrites, crash-resilient drafts, clear outcomes for non-atomic saves, correct filenames and Unicode. Prefer preserving user data over strict entitlement UI flow.

## 8. Licensing boundary

The Flutter application opens a Paddle checkout from the product's approved payment surface and obtains entitlements from a small server service. Paddle webhook signatures must be verified on the server. Webhook events are idempotent and reconciled against canonical provider state; event delivery order cannot determine access by itself.

Only the backend knows Paddle API/webhook secrets. The desktop validates server-issued entitlements or checks status securely. An offline policy is configurable and documented, but its precise duration and device policy must be decided before release. Never trust a user-editable preferences flag or local clock alone as a subscription authority.

Entitlement check is **not** coupled to SSH resource access: checkout, restore purchase, subscription status, expiration UI and local profile/data access work when SSH is disconnected. Expiration must preserve user data and offer a route to renew or export it.

Full lifecycle is defined in [LICENSING.md](LICENSING.md).

## 9. Distribution and verification (CORE-12)

Windows is release baseline. Produce repeatable signed builds, verify packaging of WebView2 dependency, establish release versioning, basic crash/error reporting with user consent, and test installation/update/uninstallation. Store no sensitive server logs by default.

Minimum required test suites:
- unit tests for parsers, quoting, entitlement state machine, event idempotency;
- widget tests for login/checkout outcomes, resource identity and unsaved-data dialogs;
- integration on disposable Debian/Ubuntu VMs for SSH host keys, reconnect, tmux, package setup, services, logs, SFTP, file collisions and firewall;
- negative tests for timeouts, changed host key, apt-lock, disconnected operations and delayed responses;
- browser integration tests for routing, name resolution, tunnel failure and reconnection;
- smoke tests for fresh Windows installation and correct entitlement restoration.

Android/tablet is a separate verified platform track. A platform folder, build command or manifest permission alone does not indicate a working release.

## 10. Architecture decisions to resolve before code

- Where the approved Paddle checkout flow is hosted.
- License customer identity and identity verification during activation.
- Pro yearly price; allowed active devices; offline grace; refund/revocation policy and notification UX.
- Local database and secure-store adapters.
- Resource/session identities; multi-server application ownership.
- Which capabilities require a host-specific privilege check.
- Whether optional future remote graphical app transport is in scope for a later research milestone.

Do not turn these undecided items into implicit defaults. Document the chosen value and test before shipping.
