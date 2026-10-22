# FORTIQ — Logs / Linux operational intelligence specification v1

**Status:** first slice in the C++/Qt client (22.10.2026), see [QT_MIGRATION](QT_MIGRATION.md); the rest of this document remains target. **Second flagship after Smart Files.** Must remain useful **without AI**, and without turning FORTIQ into a DevOps-only dashboard. **Visual reference:** [UI SPEC](UI_SPEC_V1.md); [SSH boundary](SSH_FRONTEND_CONTRACT.md).

## Product value

Understand **what happened, where, when and why it might be related**, across allowed Linux services and application log files — with readable, explainable summaries **linked to actual source records**, not invented causal assertions.

### Sources and permissions

| Source | Remote executable or subsystem | First status |
| --- | --- | --- |
| systemd system journal | `journalctl -o json` with scoped `--unit`/`--since`/`--until`/`--boot`/priority as applicable | Sprint 3 candidate |
| per-user journal | `journalctl --user` scoped to authenticated Linux user | Sprint 3 candidate |
| service status | `systemctl show` / `systemctl status`, capability checked | Sprint 3 candidate |
| plain application logs | `tail`/`sed`/`rg` with explicit file path and access checks, optionally SFTP stream | Sprint 3 candidate |
| Nginx/Apache formats | source-specific parsers atop allowed file access | After baseline |
| Docker/Podman | `docker logs` / `podman logs` only when installed and permission granted | Later; not product priority |
| kernel logs | `journalctl -k`/`dmesg` where permitted | Later |
| third-party cloud/APM | none: direct client APIs prohibited | Out of scope |

**No root/sudo escalation by default.** Missing `adm`/`systemd-journal` rights, absent `journalctl`, remote errors, rotation, permission denial and unknown format each receive an explicit state. A legitimate empty result is distinct from a failed command. No automatically installed custom log daemon.

### Unified event contract

`LogEvent = {ServerId, SourceId, BootId?, JournalCursor?, EventTimeUTC?, IngestTimeUTC, Unit?, ProcessId?, Priority?, Message, Fields, SourceReference, ReadStatus}`.

- UTC normalized **without erasing original timezone/precision**, host clocks may skew; do not imply strict global ordering across unrelated machines.
- Journald cursor for robust source pagination/follow where available; file path+inode/identity+offset/fingerprint for text, detect truncate/rotation. Restart must not silently skip or double count.
- Severity maps RFC/syslog priorities when source provides them; for arbitrary text use **unknown** until a documented parser supplies mapping.
- SourceRef always allows user to open the original journal entry / file segment, if permissions persist.
- Bounded read ranges, event size, memory, CPU, retention and user-selected scopes; never silently tail every log on a VPS.
- No cloud telemetry or hidden upload of raw logs. No local persistent log DB. Cache or index may be stored later **only on chosen Linux server**, with explicit retention and permissions.

### UX / views

Styled as a sibling of the reference **light-content Files window with dark navigation sidebar**, not an unrelated monitoring SaaS dashboard:

1. **Overview:** counts by severity/source for user-selected interval, uptime/restarts, high-signal changes; date range and selected server prominent.
2. **Timeline:** chronologically navigable events with source badges, priority, readable timestamp, stable event IDs; streaming `live` shows paused/connected/lost states.
3. **Services:** unit selector, status, related journal and restart history, link to Services app.
4. **Patterns:** folded repetition clusters, first/last occurrence, count and error template; drill-down preserves source events.
5. **Incident inspector:** user-picked time window, related changes/events, timeline of preceding/following records, jump-to-source; estimated links are labeled hypotheses, not root-cause facts.
6. **Search/filter:** server, unit, process, boot, priority, time, word/regex (remote, bounded), text query. Preserve filter provenance on share/export.
7. **Reports:** explainable synthesis with linked event IDs and method, locally displayed but produced from **server-side Linux CLI operations** / approved remote parsers. FORTIQ may format/visualize bounded results in C++; heavy processing/aggregation occurs on Linux per product contract.

### Deterministic intelligence — AI optional

- Canonicalize known log templates (mask changing IDs/addresses **for grouping only**, preserve originals and reversible source pointers).
- Group repeats and compute first/last/count within a scoped window, highlight newly appearing errors, sequence around a restart, filter correlated events by unit/time/boot.
- Change detection: last 15m vs previous window **with explicit data coverage**; no statistically dishonest anomaly threshold without baseline.
- `Incident synopsis`: observed symptoms, affected services, timestamps, number of repeated events, relevant evidence, unresolved questions and safe next inspection. Clearly separate **observed**, **inferred**, **unknown**.
- No unverified AI root-cause claim. Future explicit `Explain with AI` uses the optional server `ai` CLI over SSH, only selected/redacted events with consent; returns advice, never executes fixes.

### Failure and security semantics

- Connection loss → frozen view with `source disconnected`, live follow stopped, persisted server-side cursor only if implemented; no fake live stream.
- If an operation may have changed server config, TaskManager state `unknown`; reading logs is read-only but incomplete coverage still must be marked.
- Server/service identity cannot be rewritten by switching the current tab. Log actions carry immutable `ServerId`; links Files→Logs or Services→Logs preserve it.
- Sanitization: log messages are **untrusted content**; never interpret as commands, never render arbitrary HTML/QML from logs, no automatic clickable unsafe URI. Redact secrets in copied/exported summaries on user request, preserve access controls.

### Vertical slice / acceptance

**Sprint 3 baseline (not a promise all views will ship):** choose one server+service, show bounded journald events, filter by severity/time, live follow with reconnect/rotation handling, deterministic repeat grouping and evidence-linked incident synopsis. Read-only service info. File-based logs come next if time allows.

**Real Debian/Ubuntu tests:** boot changes, a small test systemd unit producing known severity lines, induced repeat errors, file rotation, permission denied, clock skew, missing journalctl, SSH drop during `follow`, 100k events bounded processing. Test WSL without systemd journal as a *capability unavailable* state, never a false green. QML visual comparisons use the golden design's window material and sidebar.

**Deferred:** OpenTelemetry collector, Prometheus server, Loki/Elastic, external APIs, custom log daemon, full SOC/SIEM, continuous AI inference or logs stored on Windows/Android.

### Implemented reliability slice, 22.10.2026

Journal following reads forwards from `--after-cursor` using `--no-tail`; initial reads remain
bounded to recent events. A capped response advances only to the last complete JSON event and
marks coverage incomplete.

File following reads at most 256 KiB per poll from a single opened descriptor. The cursor advances
only for a validated complete page, by received bytes, never directly to the observed file size.
Short/truncated/failed responses preserve position and show an error; backlog and initial byte-tail
limits are explicit. An unfinished line (including split UTF-8) is retained across polls, with a
bounded display prefix. Initial/replaced-file views are limited to the last 256 KiB, omit the cut
first line, and display at most 2000 events. Messages display at most 4000 characters. Newline-free
suffixes appear when completed. Rotation is detected by inode/size, not a guarantee of retaining
all writes to replaced files or detecting copy-truncate/regrowth between polls.

Every asynchronous Logs result checks the captured SSH connection generation. Source-list and
service requests also have independent sequence numbers; disconnect invalidates them and clears
server-specific sidebars/status. Source changes invalidate service/inspector requests. A successful
SSH transport does not by itself mean a successful remote command. Tests and platform evidence:
[Qt migration reliability slice](QT_MIGRATION.md#reliability-slice--22102026).

### Adaptive view snapshot, 22.10.2026

Logs maximizes as one foreground app on narrow/portrait screens, with source drawer, horizontally
scrollable filters and narrow incident synopsis/events tabs. Server/data semantics are unchanged.
This is an offscreen-tested layout, not device touch/accessibility/GPU acceptance; see
[Android foundation](ANDROID_FOUNDATION.md).
