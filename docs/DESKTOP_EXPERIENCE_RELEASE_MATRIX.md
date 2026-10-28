# FORTIQ — Desktop Experience release readiness

**Audit date:** 2026-10-28  
**Status:** SOURCE REVIEW ONLY — **NOT RELEASE APPROVED**.  
**Verification policy:** CI disabled; no Qt builds, tests or screenshots executed.

## State presentation contract

- The controller owns the truth: state, error, freshness and pending operations.
- `AppStateMessage` renders *plain-text* loading/empty/error messages; it does not invent or infer successful results.
- `LinkBanner` communicates a lost SSH link when cached data may be stale. A stale view must never be presented as fresh merely because it contains rows.
- A command-oriented app (Terminal) uses its own interactive-session status; an empty buffer is not an “empty result” state.
- A document editor (MDok) preserves dirty documents and source identity independently of transient connection status.

## Cross-app static review

| App | Shared states / existing behavior | Residual product verification |
| --- | --- | --- |
| Files | Shared empty/filter message, busy indicator, LinkBanner, TaskManager | Pagination/reveal; offline/stale directory; filtered and hidden-only folders; dialog keyboard navigation |
| MDok / Documents | Dirty draft + conflict guards, busy overlay, source availability status | Open/save/disconnect races, stale tab callbacks, source switch; keyboard/IME; modal focus |
| Mail | Shared fatal load error, LinkBanner; mail-specific thread/compose statuses | Partial messages, composing during link loss, attachment retries, credentials and busy-state transitions |
| Terminal | Interactive output and session status (intentionally not mapped to list-empty state) | Disconnect outcome, focus/search, paste/IME, huge output, resumption |
| Logs | Shared filtered-empty message, busy indicator, LinkBanner | Empty source vs filtered empty vs failed read; follow/rotation/source switch |
| System | Shared loading/error message by section, LinkBanner | Process/service/overview data consistency and stale refresh |
| Network | Shared empty/error for relevant sections, LinkBanner | DNS/diagnostics special states, cancellation and stale results |
| Disks | Shared empty/error with mount/device-specific error, LinkBanner | Partial tool failure vs no devices; refresh/timeout |
| Database | Shared query-error message, LinkBanner | Empty query result, schema/credential failures, cancellation, escaped text |
| Browser / Web | Separate browser-specific loading/security semantics | Navigation failure, permissions, crash/recovery, per-site identity |
| Settings | Distinct local preferences, not remote-list state | Keyboard focus, persistence and overlay/dialog behavior |

## Release gates — blocking acceptance

1. **Build and runtime:** Qt build and app launch on target Windows and Android configurations, when test policy is lifted.
2. **SSH integration:** reconnect/disconnect, server switching, cancel/unknown outcomes and preserved in-flight document writes.
3. **Desktop visual review:** compare all shell states to the approved reference; reference image currently not tracked per `UI_SPEC_V1.md`.
4. **Keyboard accessibility:** Tab order/Enter/Escape, restored child control focus, modal trapping, input method and on-screen keyboard.
5. **Adaptive layouts:** minimum practical viewport, drawers, scrolling, conflict actions, clipped content and overlay placement.
6. **Performance:** profile large directories/logs, many tabs, terminal output, QML bindings and responsiveness under slow SSH.
7. **State truthfulness:** no false “empty” on error and no stale remote values presented as current.

**No claims of feature-complete, architecture-complete, desktop-UX-complete or release readiness follow from the source changes alone.**
