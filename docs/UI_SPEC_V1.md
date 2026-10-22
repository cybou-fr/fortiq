# FORTIQ — UI Specification v1.0: EXACT sketch fidelity

**STATUS: BINDING DESIGN CONTRACT; not implemented/accepted.** Approved visual target: the owner's **1536×1024 screenshot supplied on 21 October 2026**, titled “Bureau Linux futuriste au bord du lac”. **Original reference SHA-256:** `852b5944c61ebb8551cc10dadeda47e7924b92baec0fd4b7758ffe2a5c6ffaef`. Preserve the original lossless image as the immutable golden screenshot when importing it into the repository. The reference is supplied in the project conversation; **it is not yet checked into this Git repository**. Do not pretend pixel-regression testing is operational until that asset is imported.

**Design principle:** RECONSTRUCT THIS SPECIFIC SCREEN. Not “inspired by”, not a new general “glassmorphism design”, not a Windows/GNOME clone. The screenshot decides appearance wherever it shows an element. Only unspecified states, unsupported actions and narrower screen adaptations may be designed, in the same visual grammar. **Desktop mode must not arbitrarily remove, relocate, recolor or resize reference structures.**

**Product contract:** [SSH frontend](SSH_FRONTEND_CONTRACT.md); [product](PRODUCT_SCOPE.md). **Functionality:** [Smart Files](SMART_FILES_SPEC.md), [Logs](LOGS_INTELLIGENCE_SPEC.md). Current actual implementation: [Qt migration](QT_MIGRATION.md). This document is target UI, not a list of working features.

## 1. Coordinate-locked master composition

The reference canvas is **1536×1024 px**, landscape, at **100% screenshot scale**. Bounding boxes are read visually and may vary by a few pixels owing to shadows, edge antialiasing and translucency. Define these as **reference bounding regions**, not guessed Qt property values:

| ID | Screenshot region (left, top → right, bottom) | Appearance and hierarchy |
| --- | --- | --- |
| TOP | (0, 0) → (1536, 47) | Dark navy translucent top bar, full width; round logo at left; global centered search; compact system indicators and time right |
| RAIL | (4, 59) → (88, 940) | Narrow near-black/navy, continuous translucent vertical app rail; blue highlights and icon labels |
| CLOCK | (119, 70) → (361, 275) | Large white `13:45`, date/day below, small sunny weather and degree; left desktop canvas |
| FILES | (471, 72) → (1189, 475) | Prominent top-center **light** file-manager window with dark navy sidebar; blue file/folder icons |
| FILES SIDEBAR | (472, 73) → (628, 475) | Dark translucent vertical pane, selection blue; title controls at upper left |
| LAUNCHER | (97, 498) → (536, 941) | Dark translucent application launcher; search, pinned icon grid, recommendations; anchored above bottom |
| BROWSER | (554, 512) → (1204, 912) | Dark chrome/frame, rounded window; scenic content below browser toolbar |
| CONTROL | (1215, 64) → (1526, 744) | Right floating control center: pale-blue two-column toggles, sliders, media widget, today's timeline |
| WORKSPACES | (1215, 765) → (1526, 894) | Separate compact workspace thumbnails with active blue-outline selection |
| DOCK | (384, 948) → (1077, 1017) | Center-bottom dark translucent rounded dock with running/favorite icons |
| TRASH | (1081, 948) → (1153, 1019) | Separate trash segment adjoining dock |

**Layer order observed:** landscape wallpaper → full-width top bar → left rail + clock → app windows → launcher (front when open) → right control center → workspace selector → bottom dock. App windows overlap wallpaper but not hide fixed rail/dock by default. Screenshot captures simultaneous *opened states* (Files + Browser + Launcher + Control Center); do not automatically open each panel on startup and confuse layout fidelity with application startup policy.

The master scene's shape, spacing, colors, icon placement, sidebar widths and transparency are **acceptance targets**. Use actual screenshot overlay/diff to tune exact values; the coordinates above are starting anchors, not a claim that the design has already passed pixel comparison.

## 2. Master color and material reference

The following values are **composited RGB samples from the rendered screenshot**, not the original alpha/token sources (which cannot be reverse-engineered uniquely from a flat picture). Preserve these samples in golden-screenshot testing; calibrate effective tint/opacity and blur in Qt against the actual wallpaper:

| Screenshot point | Measured rendered value | Design use |
| --- | --- | --- |
| Top navy (145,24) | `#001C42` | top bar reference |
| App rail (50,475) | `#06162B` | rail/side dark blue |
| Files bright body (680,300) | `#FEFEFE` | paper-white Files content |
| Files toolbar (925,94) | `#F2F4F9` | light window header |
| Files sidebar (536,279) | `#343A55` | tinted dark navigation |
| Launcher (455,740) | `#1B2D42` | dark translucent glass |
| Browser chrome (916,532) | `#19325A` | browser dark frame |
| Control panel (1223,400) | `#414559` | layered glass against wallpaper |
| Active blue-light tile (1260,100) | `#EFF9FE` | control tile pale background |
| Dock (1064,985) | `#1A2333` | dock over scenery |

**Implementation tokens, to be visually tuned:** dark navy base `#06152A`; accent electric/sky blue `#168BFF`–`#3B93FF`; light content `#FBFDFF`; bright foreground `#F5F9FF`; panel edge faint cool-blue white; glass tint with real backdrop sampling/blur when available. Do not substitute teal/green-dominant branding or flat opaque cards for screenshot's blue glazing.

**Glass fidelity:** backdrop visible *through* the rail, launcher, right control center, browser chrome and dock; subtle noise, 1px cool border, restrained inner shine and shadow. Use `MultiEffect` or a verified, performant Qt-compatible blur implementation where needed, with a blur-free high-contrast fallback; a plain opaque rectangle is not reference-equivalent. Benchmark blur and layer caching on Android hardware.

**Shadows / radii:** soft large window elevation, main floating pane ~15–20px corner curvature; controls/pills slightly rounder; app icons individually rounded; active selections blue. Avoid neomorphic heavy embossing, cartoon-sized icons, high-saturation gradients unrelated to reference.

**Typography:** clean modern UI sans (platform available, no font bundling without rights); hierarchy visibly matches reference. At master dimensions large clock ~60px, UI small labels ~11–14px, folder labels ~12–14px, app names under icons ~11–13px. Compare real glyph metrics in golden images rather than claiming font identity from pixels.

**Imagery:** master wallpaper is the *specified alpine lake + sunset mountains* picture in the supplied reference. Exact same underlying wallpaper or a licensed equivalent **visually matching the same composition** is required for screenshot matching; do not substitute an unrelated abstract gradient. Audit wallpaper/icons/third-party trademarks before redistribution.

## 3. Exact shell components and interaction contract

### TOP: app/system bar
FORTIQ brand icon and selected server label on left (do not literally label it “Linux” when multiple servers exist unless this is its chosen name). Search centered with magnifier and French placeholder as shown; right has connection/network status, notifications, clock, profile. **Only display live host toggles for capabilities that FORTIQ can actually inspect/control locally; avoid fake Bluetooth/Wi-Fi actions.** All local OS statuses are explicitly identified as local; server state is named as remote.

### Owner decisions, 22.10.2026 (override the master where they differ)

- **One application panel** instead of the master's left rail *and* bottom dock. It docks either on the left or at the bottom (right-click the panel → "Ancrer à gauche / en bas"; default bottom; remembered on the device). Unavailable apps stay visible but dimmed ("bientôt"); open apps show a running dot; the server Trash sits at the end of the panel.
- **SSH connection is global, not part of Files.** The top bar shows the connection state and server name; clicking it opens the **server manager** (saved servers, connect / disconnect, add / edit / remove, first-use host-key verification). It opens by itself at start when no server is connected. Files and Logs only ask the user to choose a server when offline.

### RAIL + DOCK
App rail visually matches screenshot order/spacing and hover/selection. Use original/licensed icons for Files, Browser, Terminal, Editor, Documents, Media, Logs, Settings. The screenshot shows a dock containing a mixture of pinned/running apps; support this without duplicating navigation responsibilities. Unknown/unimplemented apps are clearly disabled/`Coming later` and must not pretend to launch.

### CLOCK / desktop
Big time + date + weather block with white lettering. Actual weather requires an explicitly permitted server-side Linux provider through SSH, so until implemented show **time/date only or clearly labeled unavailable weather**; no direct external weather API from FORTIQ client. Client-local time is a display feature, not remote data processing.

### FILES window
**Owner decision, 21.10.2026:** all application windows (Files, Browser and future apps) use the **dark frosted glass** material of the rail/launcher/control center instead of the master's white Files body. Geometry, sidebar placement, window controls and blue folder glyphs stay as in the master; the white-body colour samples in §2 no longer apply to Files. The original rule below is kept for history:

Match high-contrast **white content** and dark sidebar, light top toolbar, blue square folder glyphs, 4-column folder grid and file examples where present in reference. Names, counts and metadata come from the remote server; screenshot example labels are sample data only. Toolbar: back/forward/breadcrumb/search/view and safe selected-file actions. Window controls sit at the *upper-left of dark sidebar* in the sketch; preserve placement, not host-OS default titlebar.

### LAUNCHER
Rounded dark-glass overlay anchored near lower left, search pill, `Épinglé` app grid, `Recommandé` recent server resources with time labels only if real evidence exists. Do not display sample Office/Steam/Spotify shortcuts as installed Linux executables; third-party icons only if rights are established and entry is supported or identified as external links.

### BROWSER window
Replicate exact *visual shell* as disabled placeholder until a cross-platform SSH-routed web engine is proven, including DNS/WebRTC/no direct fallback. A picture of a browser is **not** evidence that secure browsing works.

### RIGHT CONTROL CENTER
2-column light blue and dark tiles, volume/brightness sliders, media block, “Aujourd'hui” agenda. **State-dependent:** no claiming remote Wi-Fi, Bluetooth, airplane mode or brightness control without real platform provider; display honest unavailable/disabled status where missing. Agenda/media widgets get remote CLI integrations later; sample items must be labeled demo.

### WORKSPACES + bottom dock
Workspace 1/2/3 preview style as shown, active outline blue, plus action. Remote saved workspace layout is a separate feature; until then previews are ephemeral in RAM and not falsely persisted. Desktop windows must be bound to a ServerId; switching workspace must never redirect a running action to a different server.

### Connection / failure states not shown in sketch
Design matching supplementary overlay in same blue/navy-glass language: onboarding, independently verified host fingerprint, key-change hard block, SSH offline, reconnecting, permission denied, busy, source not installed, unsaved edits in RAM, job `unknown`, canceled transfer. **Do not obscure or bypass security decisions to match an illustration.**

## 4. Screen adaptations (the ONLY approved departures from master composition)

- **Master >= 1280 logical dp landscape:** full reference geometry and panels, anchored/responsive to viewport while preserving the original visual proportions. Precise screenshot comparison at `1536×1024, DPR=1`.
- **Android tablet 900–1279 logical dp landscape:** same wallpaper, glass, icons, file-manager colors and top bar. Dock and rail remain recognizable; control center can slide over content; windows maximize or snap when width does not support reference overlaps. Touch target >= 44 logical dp without inflating the screenshot's icon artwork.
- **700–899 logical dp / portrait tablet:** app surfaces maximize one at a time; rail may collapse, control center as right-side drawer; preserve materials, typography and icon family. Do not create a different brand/theme.
- **Narrow < 700 logical dp:** one foreground app, compact bar/dock and task switcher, system back/IME semantics. Desktop composition cannot be pixel-identical on a narrow screen; **semantic and material fidelity** replaces the spatial master only at this breakpoint.
- **High DPI:** reference pixels are not an assumption of QML dp. Anchor visual ratios, render at actual device DPR, document font and line-height decisions.

## 5. Files and Logs as native visual siblings

**Smart Files** is the first open reference window. Treat it as a real interactive remote file manager, not decorative folders; [full contract](SMART_FILES_SPEC.md). Folder icon/grid/toolbar/sidebar aesthetics inherit the screenshot exactly, and search/preview/transfers appear as matching secondary surfaces.

**Logs** is the second flagship app with the same titlebar, side rail, window shadows, accent blue and structured typography. As Logs is **not pictured**, its two/three-pane timeline, filters, grouped incidents and evidence inspector are *complementary extensions*, never a reason to redesign the shell. [Full contract](LOGS_INTELLIGENCE_SPEC.md). AI is not needed for summaries.

## 6. Visual fidelity verification (blocking)

1. Store the user's original screenshot as a separate golden source under `docs/design/reference/fortiq-shell-master.png`, preserving the recorded SHA-256 (do not re-encode and claim same hash). **Not present yet**: create it from the conversation attachment before automated comparison.
2. Produce deterministic QML screenshot at 1536×1024 physical pixels / DPR=1 with a named *visual-only reference fixture*, stable clock/time/weather, sample filesystem, selected workspace, launcher and control center opened. This fixture may **never** be used as real production data.
3. Compare visually and programmatically: region layout, bounding boxes, color difference maps, typography baselines, icon silhouette/grid alignment, effective translucent layering over the exact wallpaper. Record output/diff with Qt build and device information.
4. Manual inspection for difficult GPU effects, transitions, cutouts and overlaps. Approve only traceable changes to unspecified states.
5. Run Android tablet and narrow screen QML snapshot/accessibility checks, plus native interaction with mock vs real SSH (separate evidence).
6. Do not claim `pixel perfect` or accepted fidelity before independently reviewing actual side-by-side screenshots. The *goal* is exactness; a specification alone is not proof.

Tooling for steps 2–3 exists: `--reference-fixture --screenshot` and `tool/compare_shell_screenshot.py`; current results and blockers are in [SHELL_FIDELITY](SHELL_FIDELITY.md).

## 7. Implementation priority

**Sprint 1:** master reference scene + QML component decomposition + connection/Files window + persistent SSH; Windows/Android build proofs.  
**Sprint 2:** Smart Files with search, safe transfers, task states, testable real workflows.  
**Sprint 3:** Logs with journald and permitted log files, filters, deterministic summaries, source-provenance, alongside minimum Terminal/System support. See [roadmap](FORTIQ_DESKTOP_BLUEPRINT.md).

Do not build a fake Wi-Fi toggle, “working” browser mock, remote calendar, streaming media or AI assistant merely to fill the screenshot.

## Explicit owner-approved logo exception (21 October 2026)

The owner approved the **new cyan/blue/violet FORTIQ monogram** and requested native executable integration. In the reference screenshot, the 25px top-left circle is now replaced by the approved F mark while preserving **the same top-bar position, scale, surrounding layout and material**. No other reference-visible geometry, colors or window structure is authorized to change because of branding.

Native assets are generated from the source in `assets/branding/fortiq_mark.png`; CMake integrates them into Windows EXE/Qt windows/Android launcher. See [Branding](../assets/branding/README.md). Windows/Android runtime visual acceptance still needs actual builds and device checks.

## Adaptation implementation snapshot (22 October 2026, acceptance pending)

Section 4 now has a first source implementation: below 1280 logical pixels or in portrait,
one foreground Files/Logs surface uses viewport units, with a single scrollable app panel,
collapsed sidebars and touch-sized primary controls. Window/editor instances survive resizing;
Back preserves unsaved text. The reference desktop geometry remains under tests at 1536×1024.
While the Android IME is visible, wide tablets also use viewport units to keep text and controls
readable above the keyboard; the normal landscape composition returns when the IME hides.
Owner correction on 22 October 2026: the usable viewport must have no navy margins around the
desktop. Uniform artwork scaling is retained, with the logical desktop extending to the viewport
edges at other aspect ratios. Empty space in the Files sidebar is also a drag handle in desktop
mode; navigation rows and window buttons keep their actions. Panels must visibly retain the glass
material: the backdrop sample follows ancestor movement, and Files/app-panel/workspace tints are
less opaque following the owner's feedback. This material adjustment still awaits visual acceptance.
Latest owner correction: the Android status bar retains native system icons and a system-palette
background, without wallpaper. Wallpaper extends beneath the bottom navigation area; controls retain
safe insets. The production launcher opens beside the left app panel or directly above its bottom edge;
its tint is lighter. Frosted wallpaper is blended with the scene underneath so overlapping windows
remain visible. This is translucent composition, not yet blur of the complete lower-window scene.
Owner decision 23.10.2026: connecting is a login screen, not a dialog: full-screen blurred wallpaper with
a large clock, saved servers as round account tiles (initial, name, user@host), a password prompt and an
"Ajouter un serveur" tile; it replaces the whole desktop (no top bar, panel or windows) until a session starts,
and the session menu under the server name logs out. Not part of the master sketch; the reference fixture does not show it.
Owner decision 23.10.2026: in production the right panel (control center) stays a floating glass card but sits against the right edge
(8 px gap) and spans from below the top bar to the bottom, and the wallpaper choice is a section of that panel; the card is only as tall as its content
(no empty tail), and a press anywhere outside it (or outside the launcher and the app panel, for the launcher) closes it
(three previews: Aurora, Désert, Ville), not a separate card. They do not implement saved remote workspaces.
The reference fixture keeps the floating panel and the original workspace cards at master coordinates.
Clicking or touching an exposed app window raises it above the other app windows; choosing its
app-panel entry raises it too. Activation observes the press passively so fields, buttons and drag
handles retain the same gesture. Shell panels and the global server dialog remain above app windows.
Owner material correction: Control Center uses the Logs window tint (`#B3121C30`) and blur amount
(`0.9`), rather than the lighter workspace-card tint, so the right panel has comparable density.
Terminal follows the same sidebar/toolbar/status composition as Files/Logs. The sidebar shows
saved Bash history (click types a command without Enter); narrow screens use a history drawer. Without SSH, the content
shows the server chooser action used by Files. Opening Terminal with an available source starts
the PTY automatically; shell input is never replayed. History rows are compact (40 px minimum
on adaptive touch layouts); clicking returns input focus without sending Enter. The stop and
bottom Ctrl+C/D buttons are removed; keyboard shortcuts remain. The focused cursor is a blinking
filled block, the unfocused cursor an outline; server cursor visibility is respected. A bounded 1000-line RAM output buffer is accessible
with wheel, Shift+PageUp/PageDown and a vertical scrollbar (wider on adaptive layouts).
Shift+End or input returns to live output; the alternate screen has no local scrollback.
Mouse drag selects visible terminal cells (including scrollback). Copier/Ctrl+Shift+C copy
the selection, or the visible screen when none is selected. Ctrl+C remains a shell interrupt.
Selection clears on incoming output, input, scrolling and grid resize; touch handles are pending.
Terminal search (⌕ / Ctrl+Shift+F) is literal and case-insensitive, across current visual rows
and the RAM buffer. Enter/Shift+Enter or arrows cycle through matches; Escape closes it.
Ctrl+F remains shell input. New output/resize invalidates matches; reset clears the query.
Disks uses the same glass and drag behavior, mounted-volumes/device sidebar, server chooser
without SSH, and an adaptive drawer. Its read-only content comes from lsblk/findmnt; mounted
paths open in Files. The production app panel and launcher expose it; the canonical fixture is unchanged.
MDok shares glass, drag, source chooser and activation. Desktop shows Markdown source/preview
side by side; adaptive layouts switch panes and use a sidebar drawer. Dirty documents require
a decision before replacement/close/ordinary app quit. The production panel grows to fit new apps,
and its left layout scrolls when needed; the canonical fixture remains unchanged.
This is not a new visual approval, nor proof of complete Android touch/accessibility or GPU fidelity.
See [Android foundation](ANDROID_FOUNDATION.md) for screenshots, build evidence and device gates.
