# FORTIQ — Smart Files specification v1 (remote Debian/Ubuntu)

**Status:** target design; implemented slices and test evidence are tracked in [Qt migration](QT_MIGRATION.md), not implied by this specification. **Visual contract:** [UI SPEC v1](UI_SPEC_V1.md). **Mandatory network contract:** [SSH frontend](SSH_FRONTEND_CONTRACT.md).

## Product value

Premium, fast, touch-capable **remote file manager that feels local**. The content is always remote and server-bound; the rendering/UI are local. No client-side fallback filesystem, local permanent document/cache database or independent cloud drive backend.

### Navigation & presentation
- The **exact light Files window from the master sketch**: white canvas, dark left sidebar, light toolbar, blue icons, 4-column grid at master 1536px reference.
- Server name, current path and permissions always visible. Breadcrumb/back/forward/up; list/grid modes; keyboard navigation, multi-select, Android touch selection, drag/drop with confirmation; empty/locked/offline states.
- Sidebar: Home, Recents, Favorites, Documents, Pictures, Video, Downloads, Projects, Work, Trash. These are **capabilities/collections** resolved from actual Linux locations, not unconditional physical directories. Unsupported items say `not configured`, never fake folders.
- File preview: images/text/Markdown/PDF via reviewed server capabilities and bounded transfer/rendering; no unapproved direct client network fetch. No automatic bulk local caching. Preview UX must respect memory limits and media licensing.
- Never equate `mtime` with “recently opened”; for `recently modified` use mtime; `recently opened` requires authoritative per-server workspace event recording.
- File operations: create, rename, move, copy, delete/trash where supported, file properties, upload/download, verify publish; use progress and conflict modals rather than invisible busy cursor.
- Hidden files, symlinks and permissions: preserve semantics; symlink navigation must not be sold as isolated/sandboxed unless verified. Arbitrary filename bytes are nontrivial; Qt prototype currently skips non-UTF8 names: **display explicit limitation**, do not mutate them by lossy conversion.

### Smart capabilities (no AI required)
- **Remote search:** scoped filename/content, bounded results and cancel, backend `find`/`rg` where installed/compatible (executable/version/permissions checked). Search is server-side, not a large client download. Query text is safely passed as data, no unsanitized shell concatenation.
- **Collections:** recently *modified*, large, config (`.conf`/known paths), logs, code/repositories, archives; rules are transparent and user-editable on server, not opaque local magic. Do not scan arbitrary protected system paths without explicit scope/permission.
- **Context actions:** open text in Editor, log in Logs, service config from known mapping, image/PDF preview, archive actions only via available CLI. Each action carries immutable `ServerId + ResourceRef`.
- **Meaningful grouping:** by project/content-type and duplicates (optional later; costs disclosed). No hidden “AI understands everything” marketing.
- **Relevant metadata only:** size, type, owner, permissions, change time, server; avoid blocking grid on expensive stats.

### Integrity and task semantics
- Every mutating operation has stable `TaskId`, target server+path and source revision. Users see queued/running/cancelling/success/error/`unknown`.
- Upload: unique remote temporary name, write/flush, bytes/digest validation where possible, conflict-safe publish without silently replacing destination, clean up only owned temporary file. On SSH drop do not blindly replay.
- Edit/save: optimistic remote version check → remote temporary write → recheck policy → atomic publish where supported → read-back/verify; conflicts require user choice and must protect the original.
- Rename, copy, delete and trash are **not** assumed atomic on all filesystems; distinguish reversible Trash from permanent delete. Strong confirmation for recursive/destructive operations.
- Multi-server transfer must not accidentally switch the target when active workspace changes. If cross-server copy is not implemented, show unsupported, not hidden local staging.
- Directory huge/slow: pagination/limit and visible count. Current 3000-entry limit is a **prototype constraint**, not finished UX. Use QAbstractItemModel, stable entries, cancel and backpressure for later scaling.

### Server-side caching and persistence
- Optional remote metadata cache under the server user's controlled directory (`~/.cache/fortiq/`), bounded TTL/quota, explicit refresh, permission-safe. This is a **future design**; no server daemon or local working-data persistence.
- Server favorites, smart collections, workspace and recents persist only on Linux (via SSH). No fake “recently opened” derived from filename.
- If offline, show source unavailable; permitted unsaved UI text survives only in memory. Never silently open a local mirrored folder.

### Acceptance test matrix
1. Debian/Ubuntu test user via WSL/VPS, with Unicode, newline/space names, symlink, access denial, empty folder, >3000 entries, deep tree; explicit handling of unsupported bytes.
2. Search scope/permissions, cancellation, large result limits, stable source mapping.
3. Upload/download with abort before publish, conflict, overwrite confirmation, disconnect at every stage, cleanup of owned temp, publish verification, two concurrent same-name transfers.
4. Rename/copy/delete with auth denial, cross-device moves, false success prevention and crash recovery states.
5. Windows mouse/keyboard and Android tablet touch/IME; screenshot diff against Files portion of [golden design](UI_SPEC_V1.md).
6. No direct work networking, no unintended local disk cache, no secret-bearing logs.

**Qt R0.1 truth (22.10.2026):** navigation, preview, transfers, Trash, editor and search exist in source; see [Qt migration](QT_MIGRATION.md) for executed tests. Upload Replace now uses a single atomic posix-rename without a backup visibility gap; editor hash checks remain optimistic, not atomic compare-and-swap. Full Windows/Android qualification and the complete target contract remain unfinished.

### File publication reliability slice, 22.10.2026

- Existing-file upload Replace and editor save use `posix-rename@openssh.com`, with no
  move-original-to-backup or non-atomic fallback. Unsupported replacement fails; an uncertain
  publication acknowledgement is `unknown` and is never retried automatically.
- Upload Fail/KeepBoth, and Replace whose destination was initially absent, use no-overwrite
  SFTP rename. A name created by another writer during transfer is not overwritten.
- Upload staging is created empty with mode 0644 to capture the server's creation mode/umask,
  then restricted and verified as 0600 before writing source bytes. The captured creation mode
  is restored for a new file. Existing-file Replace preserves mode, uid and gid and refuses
  initial symlinks/directories/unknown destination metadata or an owner it cannot preserve.
- Editor staging starts as 0600. Mode and owner/group are restored after writing and verified
  before publication, as are size, flush (when supported) and CLOSE. Failed or falsely acknowledged
  metadata updates prevent publication. Concurrent destination type/mode/owner/group changes
  detected before publication are conflicts. Empty-upload cancellation is checked too.
- Editor checks SHA-256 before staging and before publication and verifies the published content
  by reading it back; mismatch or lost acknowledgement is `unknown`. These are **optimistic** checks:
  an external writer can still change the file between the last check and rename. Explicit upload
  Replace overwrites content without a source-version compare. No atomic compare-and-swap is claimed.
- Unsupported fsync extensions are tolerated; this does not guarantee power-loss durability or a
  durable parent-directory rename. ACLs, xattrs and hard-link relationships are not preserved by
  this slice. A disconnected pre-publication operation may leave its owned hidden temporary.

Executed tests and repeatable fixture requirements: [Qt migration](QT_MIGRATION.md#file-publication-reliability-slice--22102026).

### Android document integration snapshot, 22.10.2026

Files now has an adaptive foreground layout and opaque SAF URI handling. Keys are loaded via
QFile into bounded RAM for authentication; uploads stream provider content using actual bytes
when reported length is unknown. Downloads stage completely before provider publication, which
is explicitly non-atomic and reports possible partial results on interruption. No broad storage
permissions or direct network fallback are introduced. The FortiqTablet emulator run verified Unicode
file navigation/edit/save and native SAF upload/download with identical round-trip bytes; staging
now uses an explicit private app-cache directory. Physical hardware and provider failure/grant
coverage remain pending. [Implementation and Android evidence](ANDROID_FOUNDATION.md).

### Transfer lifecycle hardening (2026-10-28)

The Files transfer engine rejects attempts to start a transfer when there is no active SSH connection, the TaskManager job is terminal or cancelling, or the conflict policy is out of range. A late conflict dialog action cannot rearm a cancelled job after disconnect. The allowed policies are `Fail`, `Replace`, and `KeepBoth`; the UI only submits `skip`, `replace`, or `keepBoth` at conflict resolution. Transfer completion already treats a lost SSH connection during remote publication as an *unknown* outcome, requiring user verification before retrying. These changes were committed without running a build or tests, as requested; CI remains disabled.

### Last cancellation gate before remote publish (2026-10-28)

Uploads now re-check the shared cancel signal immediately before the irreversible SFTP rename, after destination metadata validation. If cancelled at this gate, only the owned staging file is removed and the final destination is not modified. A cancellation arriving *during* rename cannot safely establish whether publication occurred; SSH publication errors continue to use `unknown` when the connection is lost. This change was not compiled or tested; CI remains disabled.
