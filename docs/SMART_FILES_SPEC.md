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

**Qt R0.1 truth (22.10.2026):** navigation, preview, transfers, Trash, editor and search exist in source; see [Qt migration](QT_MIGRATION.md) for executed tests. Upload Replace has a backup-rename visibility gap; editor hash checks are not atomic compare-and-swap. Full Windows/Android qualification and the complete target contract remain unfinished.
