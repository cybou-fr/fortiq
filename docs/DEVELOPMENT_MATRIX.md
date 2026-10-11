# FORTIQ — Development Matrix

**Snapshot:** 2026-10-11. **Reviewed commit:** `cb737744cc970a5388f1edfbf45e6c185f071857`.
**Scope:** Distributed Desktop Software, all existing apps retained.
**Evidence level:** source inspection of `main`; tests and real-server integration were not executed as part of this documentation review.

Related documents: [README](../README.md) (current build), [PRODUCT_SCOPE](PRODUCT_SCOPE.md) (product), [ARCHITECTURE](ARCHITECTURE.md) (contracts), [LICENSING](LICENSING.md) (Paddle business logic).

## 1. Decisions and priorities

- Product is FORTIQ, a paid distributed desktop application with one set of full capabilities.
- Two purchase options only: **Intro €1 for seven days once, no renewal**; **Pro billed annually, price TBD**. Paddle is the payment and billing layer.
- Existing 12 application types remain. Existing Linux environment auto-preparation remains and is strengthened.
- Prioritize commercially usable Windows builds, trustworthy licensing, robust SSH/setup, resource identity and integrated user workflows.
- The roadmap has acceptance gates, not guaranteed delivery dates. Work not yet implemented must be described as planned.
- Reuse app capabilities as common services; preserve resource ownership across local, remote and account sources.

## 2. Current code audit

| Area | Observed implementation | Gap or verification |
|---|---|---|
| UI | `main.dart` requires `isSessionActive` to show `HeadlessDesktopWorkspace` | Start shell and activation independently of SSH |
| Applications | `FortiqAppType` in `desktop_workspace.dart` lists 12 types | Central registry; resource-aware instances; persistence |
| Session | SSH TOFU, reconnect, ping, dynamic SOCKS | Single-flight reconnect; late-result cleanup; transport integration testing |
| Terminal | PTY, UTF-8 stream, tmux session naming | `_openWindow` reuses same Terminal instance |
| Files | `file_service.dart`: shell ls/cat/base64 and write | SFTP, streaming transfers, robust metadata, save conflicts |
| System | `system_service.dart`: processes, service control, logs/metrics | Structured outputs, lifecycle and error coverage |
| Network | `network_service.dart`: ports/UFW | UDP and reliable parsing/error categories |
| Server setup | `server_setup_service.dart` auto-installs/updates required packages when permitted | Dispose/cancellation, apt lock, no overlapping remote jobs, clear status |
| Browser | Windows WebView2 over SSH SOCKS | Actual DNS/route/fail-closed integration tests |
| Mail | `mail_service.dart` uses Linux system mail | Future account mail/MIME, cache and independent drafts |
| Tests | `test/shell_quote_test.dart`, `test/services_test.dart` | CI, widget and integration coverage |
| Android | `android/app/src/main/AndroidManifest.xml` includes INTERNET | Windows-only browser dependency and device validation |
| Licensing | No Paddle/entitlement modules or integration | Entire LIC-01…07 track |
| Distribution | Windows Flutter build instructions | Signing, release process, installation and update tests |

**Risks confirmed in source:** `_setup.run()` is launched unawaited from desktop init and the service is disposed when the desktop closes; async completion/disposal needs explicit protection. Reconnect retry paths have no explicit single-flight guard. Current tool code and some old documentation refer to different locations. These are implementation findings, not proof of a production failure.

## 3. Shared engineering work

| ID | Priority | Contract / work | Acceptance |
|---|---|---|---|
| CORE-01 | R0/R1 | Shell and activation independent of SSH | Open desktop/activation without remote connection; connect later without losing local state |
| CORE-02 | R1 | Central App Registry | All 12 widgets appear consistently; per-app multiple-window policy defined |
| CORE-03 | R1 | ResourceLocation (local/host/account) | Every operation uses explicit resource ID; switching server never redirects an in-flight action |
| CORE-04 | R1 | Shared Task Manager | Progress, cancellation, failure and unknown outcome visible outside initiating window |
| CORE-05 | R0/R1 | Local database and OS credential store | No secret in preferences/logs; restore drafts and saved workspace metadata |
| CORE-06 | R1/R2 | Notifications and cross-app Action Router | Service opens its logs/config on the same host; task opens source |
| CORE-07 | R0/R1 | Host capabilities, privilege discovery, automatic dependency setup | Missing packages installed when allowed, system updates honor configured policy, error and progress visible |
| CORE-08 | R0 | Safe async lifecycle and reconnect | Single-flight reconnect; no use of disposed notifier; late session closed |
| CORE-09 | R2/R3 | Persistent workspaces | Layout, windows, source references and drafts restored without replaying remote writes |
| CORE-10 | R0/R1 | Remote operation safety | Unknown outcome identified; no automatic repetition of potentially destructive actions |
| CORE-11 | R1 | Multi-resource Session Manager | A/B sources maintain independent connection state, host keys and tunnels |
| CORE-12 | R0 | CI and verified Windows packaging | Static analysis + unit tests on every PR; install/run/update smoke test; repeatable release artifacts |

Detailed behavior is defined in [ARCHITECTURE.md](ARCHITECTURE.md).

## 4. Licensing and commercial release

| ID | Priority | Work | Acceptance |
|---|---|---|---|
| LIC-01 | R0 | Paddle product, seller verification, one-time Intro and yearly Pro | €1 Intro, seven days once, no auto-renewal; Pro annual price explicitly approved |
| LIC-02 | R0 | Purchase/activation/subscription UI in FORTIQ | Checkout, restore and expiry visible even without SSH |
| LIC-03 | R0 | Small licensing backend and Paddle webhooks | Signatures verified, idempotent events, reconciliation, no billing secrets in app |
| LIC-04 | R0 | Entitlement token, local cache and renewal state | Access consistent through restart/outage/cancel/expiry/refund |
| LIC-05 | R0 | Intro one-time eligibility | Reinstall or duplicate transaction never grants repeat Intro |
| LIC-06 | R0 | Self-service Pro billing and cancellation | Buyer can cancel renewal; access remains through paid-through date |
| LIC-07 | R0 | Sales release readiness | Price, renewal, platform, license terms and support channel honestly documented |

Policy choices still open: **Pro annual price**, device count, offline grace length, activation identity mechanism, Windows signing/distribution. Do not silently select defaults. All Intro/Pro functionality is identical.

## 5. Application matrix

Keep APP identifiers stable. Present status is from repository source, not certification on real servers.

### APP-01 — Files
- **Now:** explorer plus `FileService` listing via shell, reading, delete/create, basic search.
- **R1:** SFTP file metadata and streaming upload/download, rename, large-file progress, proper Unicode/symlink handling, explicit source IDs.
- **R2:** recently used, favorites, transfer center, attach/open actions.
- **Acceptance:** exact names including spaces and Unicode; interrupted transfer reports failure; no cross-host transfer without explicit destination.

### APP-02 — Editor
- **Now:** text editing, dirty state, Ctrl+S, fallback non-atomic writes.
- **R1:** local durable drafts, find/replace, external-change conflict detection, diff before service config changes.
- **R2:** multiple files, line navigation, reusable document storage.
- **Acceptance:** crash preserves draft; concurrent edit warns; save errors remain visible; staged save is not reported atomic when it is not.

### APP-03 — Docs & Markdown Studio
- **Now:** remote Markdown editing/preview.
- **R2:** local/remote document locations, persistent drafts, templates, open resource links, search.
- **R3/R4:** document export and runbook linking.
- **Acceptance:** open another note without overwriting edits; reopen the same note after restart.

### APP-04 — Mail
- **Now:** Linux system mailbox via `MailService`, minimal send/read.
- **R2:** real IMAP/SMTP account source, TLS, MIME, attachments, read/reply, drafts and local cache; retain system mailbox source.
- **R3:** multiple accounts, optional OAuth, search/sync and cross-app actions.
- **Acceptance:** read/compose/attach/store sent mail; avoid duplicate sending on uncertain outcomes; account mail works with SSH disconnected.

### APP-05 — Browser
- **Now:** Windows WebView2 over dynamic SOCKS on active SSH session.
- **R0:** integration checks for proxy routing, DNS, localhost, failure, reconnect; accurate route state.
- **R2:** bookmarks, downloads through task center, links from other apps.
- **Acceptance:** only verified supported routes advertised; tunnel failure reliably reported.

### APP-06 — Viewer
- **Now:** remote-image viewing with base64 transfer.
- **R2:** scalable image handling and PDF reading on Windows, open from Files/Mail.
- **R4:** advanced media support if justified.
- **Acceptance:** large file does not freeze UI; failures are shown; downloaded assets are resource-tagged.

### APP-07 — Search Hub
- **Now:** filename/content queries using server shell commands.
- **R2:** unified scoped search by host, local documents and app type; result opens correct resource; search tasks cancellable.
- **Acceptance:** no mixed-host results; unknown permissions visibly indicated; exact filename/line reference preserved.

### APP-08 — Terminal
- **Now:** SSH PTY and tmux, but desktop opens only one Terminal app instance.
- **R1:** independent terminal windows/sessions, stable host/session associations, open terminal in selected folder.
- **Acceptance:** two terminals simultaneously, correct tmux reattach, no cross-host session reuse.

### APP-09 — Processes
- **Now:** processes from shell ps, search and kill.
- **R1:** full sorted process list, SIGTERM then explicit SIGKILL, details, resource identity.
- **R2:** linked ports/services.
- **Acceptance:** verify PID before action; handle privilege failure and PID reuse.

### APP-10 — Services
- **Now:** systemd list and start/stop/restart.
- **R1:** structured properties, enable state, jump to logs and config, restart-with-verification.
- **Acceptance:** complete flow Services → Logs → Editor → Save → Restart → Verify.

### APP-11 — Network
- **Now:** interfaces, TCP and UFW tools with textual parsers.
- **R1:** UDP, accurate listening statuses, no silently empty results, audited rule modifications.
- **R2:** ports → processes → services.
- **Acceptance:** network query errors are distinct from "no ports"; rule result confirmed.

### APP-12 — Logs
- **Now:** journal/syslog/auth/dmesg views and text-level heuristics.
- **R1:** structured journal fields, source/time/priority filters and bounded tail.
- **R2:** saved filters and service-to-log route.
- **Acceptance:** service context preserved, tail cancellable, permission-denied visible.

### APP-13 — Disks
- **Now:** no standalone disk app, root filesystem metric only.
- **R1:** disk inventory, mounts, sizes and capacity, read-only directory usage scan.
- **R4:** mounting, SMART where supported, partition operations with tested safety workflow.
- **Acceptance:** correct lsblk fields; scan cancellable; unsupported capabilities explicit; changing actions tested only on disposable VM.

### APP-14 — Server Status & Setup
- **Now:** required-package discovery/installation/update and OS update checks.
- **R0:** retain full automatic required-package setup, add single-flight lifecycle, structured status, retry-safe verification, apt lock handling and stable cancellation.
- **R1:** shared capability model, clear privileged-op results, per-server policy/status/history.
- **Acceptance:** fresh supported Debian/Ubuntu host prepares itself when privileges allow; second concurrent setup does not run; policy for wider upgrades is respected; reconnect does not send unexpected extra apt changes.

## 6. Delivery milestones

| Stage | Deliverable | Release gate |
|---|---|---|
| **R0 — Paid Windows foundation** | LIC-01…07; independent activation shell; CORE-08/10/12; APP-14 hardened; APP-05 verified route; clean README and release build | One-time Intro and yearly Pro complete verified purchases in Paddle Sandbox; Windows install; non-destructive user data on expiration; SSH/connect/setup stability tests |
| **R1 — Connected desktop** | CORE-02/03/04/07/11; APP-01 SFTP, APP-02 durable editor, APP-08 multi-terminal, APP-09/10/11/12 structured tools, APP-13 disk overview | Multi-host context reliable; files transfer with progress; service diagnosis flow succeeds; no silent remote re-execution |
| **R2 — Integrated daily work** | CORE-06/09 foundation; APP-03 Docs, APP-04 first real mail account, APP-05 browser workflow, APP-06 PDF/images, APP-07 search | Mail attachment → Files/Viewer and services/logs/editor flows; restore workspace and drafts; verified integration tests |
| **R3 — Distributed desktop maturity** | restored workspaces, improved mail/sync, history/notifications, tablet UX track, cross-resource navigation | Multi-source persistence and device-specific workflows verified |
| **R4 — Advanced capabilities** | advanced Disks operations, extended media/docs, additional validated platforms, optional research features | Dedicated safety tests on disposable environments and clearly supported target platforms |

R0 targets the **first paid release**. R1 and later deepen the product. No R0 claim of production readiness is implied until the release gate actually passes.

## 7. Cross-application acceptance flows

| ID | Flow | Pass criterion | Target |
|---|---|---|---|
| FLOW-01 | Mail → attachment → Files/Viewer | Correct source and complete saved attachment | R2 |
| FLOW-02 | Server file → compose mail → send | Same file version and no duplicate send | R2 |
| FLOW-03 | Services → Logs → Editor → Restart → Verify | Save success precedes restart; outcome checked | R1 |
| FLOW-04 | Disk capacity → directory scan → Files | Accurate sizes and cancellation, correct source | R1 |
| FLOW-05 | Port → Process → Service → Logs | Source and process identity retained | R2 |
| FLOW-06 | SSH disconnect with terminal/editor/task | Clear task state, draft preserved, tmux restored | R1 |
| FLOW-07 | App restart → workspace restore | Same windows/resources with no secret persistence | R2/R3 |
| FLOW-08 | First-run Intro purchase → activation | €1 once and seven-day grant, no renewal | R0 |
| FLOW-09 | Annual Pro renewal/cancellation | Paid-through access, self-service cancel, correct new entitlement | R0 |
| FLOW-10 | Switch from remote A to remote B | No side effect redirects to wrong source | R1 |
| FLOW-11 | Auto setup on fresh Debian/Ubuntu | Required tools prepared, progress visible, no duplicate concurrent apt | R0 |

## 8. Platform track and verification

- **PLAT-01 Android/tablet:** INTERNET permission is already declared; assess browser replacement, OS secure store, filesystem dialogs, window scaling, touch, keyboard, reconnect and entitlement policy; verify on a real device.
- **PLAT-02 Linux/macOS:** evaluate portability of native plugins, secure storage and browser support on each OS; ship only after its tests.
- **PLAT-03 iOS:** research signing/store rules and license/checkout distribution requirements before committing to a release.
- **Research only:** remote graphical application streaming needs a separate protocol/design spike; do not imply functionality from the current Flutter widgets.

**Required checks:** `flutter analyze`, `flutter test`, widget integration, disposable real SSH VM, Paddle Sandbox webhooks, Windows install/update and crash recovery. Existing GitHub repository has no workflow shown in the audited tree; CI configuration is a deliverable.

## 9. Engineering order / suggested issue batches

1. **R0-A:** fix async lifecycle, reconnect single-flight, server setup concurrency and predictable operation outcomes; add CI.
2. **R0-B:** introduce standalone desktop/activation surface with local state; keep existing apps and automatic setup.
3. **R0-C:** create Paddle Intro/Pro test catalog, backend, webhooks and signed entitlement; confirm unknown policy parameters.
4. **R0-D:** build purchase/restore/cancel UX and packaged Windows release; pass R0 integration matrix.
5. **R1-A:** implement ResourceLocation, Session Manager and shared Tasks with explicit source ownership.
6. **R1-B:** finish SFTP, multi-terminal and safe editor/drafts; validate cross-resource correctness.
7. **R1-C:** improve structured system observability and add read-only Disks; validate complete service diagnosis.
8. **R2:** integrate user content apps, account mail, viewer/search, workspace persistence and cross-app navigation.
9. **R3/R4:** deepen everyday use, verify platforms and advanced operations only after acceptance gates.

Every issue must include observed baseline, target behavior, dependencies, tests, risk and status. Do not mark a capability "done" only because its widget exists.

## 10. Status language and maintenance

Allowed states: **planned**, **in progress**, **implemented in code**, **verified**, **released**. Mark a result verified only with actual test evidence; update code paths after refactors. Review this matrix on every release and keep README's current-features section factual.
