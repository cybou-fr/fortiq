# FORTIQ — Documentation authority and consistency register

**Purpose:** one coherent product, not multiple conflicting Flutter/Qt roadmaps. **Status:** documentation policy, 21 October 2026. No build/test claims are created by this file.

## Source-of-truth hierarchy

| Question | Normative document | Never infer from |
| --- | --- | --- |
| What is FORTIQ / where work happens? | [SSH frontend contract](SSH_FRONTEND_CONTRACT.md) | UI screenshot / old Flutter features |
| How does it look? | **[UI SPEC v1](UI_SPEC_V1.md)** and the owner's exact 1536×1024 source screenshot (hash recorded there) | Qt controls defaults, invented glassmorphism themes |
| What do users get, and in what order? | [Product Scope](PRODUCT_SCOPE.md) | old marketing/landing page drafts |
| What does flagship Files do? | [Smart Files](SMART_FILES_SPEC.md) | the current subset of file operations |
| What does flagship Logs do? | [Logs Intelligence](LOGS_INTELLIGENCE_SPEC.md) | arbitrary sysadmin/SIEM features or AI promises |
| How to implement it? | [Architecture](ARCHITECTURE.md), [Desktop Blueprint](FORTIQ_DESKTOP_BLUEPRINT.md) | discontinued Flutter architecture |
| What is actually in code today? | **[README](../README.md), [Qt migration](QT_MIGRATION.md), [Development Matrix](DEVELOPMENT_MATRIX.md)** plus executed test evidence | aspirational specification, unrun tests, screenshots |
| How do licenses and optional AI work? | [Licensing](LICENSING.md), [Licensing Setup](LICENSING_SETUP.md), [AI CLI](AI_CLI.md) | historical Flutter implementation |

**Conflict resolution:** stricter SSH/security guarantees win over UI convenience. For the visual appearance **in the user-provided master screenshot**, [UI SPEC](UI_SPEC_V1.md) wins over generic design suggestions, sample tokens and old wording. For actual implementation status, verifiable **current source + test evidence** wins over all roadmap claims.

## Accepted decisions (non-negotiable without owner approval)

1. **Product:** local windowed desktop shell for a selected remote Debian/Ubuntu system, not GNOME/RDP pixel streaming, not developer-only DevOps dashboard.
2. **UI:** exactly recreate owner image 1536×1024 in desktop reference mode — same geometry, palette, translucency, light Files window/dark sidebar, rail, launcher, control center, workspace selector and dock. Complement only parts the reference cannot answer. On narrow Android, adapt spatial placement but preserve materials/identity.
3. **Platforms:** Windows x64 and Android arm64 **from the beginning**; same C++20/Qt Quick/QML core and native libssh2 strategy.
4. **Remote:** SSH-only for work data, SFTP, CLI, PTY and future browser forwarding. No FORTIQ backend daemon, persistent local working-data copies or direct client mail protocols.
5. **Flagships:** **Smart Files first**, **Logs second**; Logs performs deterministic source-backed grouping and synthesis **without AI**. Terminal/Services/System are supporting.
6. **Deferred:** browser until its SSH-only routing is provable on Windows and Android; mail/docs/editor progressively after flagship basis; optional CPU Linux `ai` later.
7. **Status:** engineering Qt R0.1 has persistent SSH/password/key, file operations and first Logs slices; Windows builds locally, while clean-machine/Android/visual qualification and licensing remain incomplete. Executed test evidence is in Qt migration.
8. **Git workflow:** direct commits to `main`; no branches by default. The Flutter reference exists only at tag `flutter-final`.
9. **Design verification:** master PNG SHA-256 recorded in UI SPEC. **The exact image is not yet committed**. The verified import process is [tool/install_master_reference.py](../tool/install_master_reference.py); once imported it is copied into both `docs/design/reference/` and `www/assets/`, allowing the marketing preview to display the byte-exact design reference. Golden Qt pixel-comparison still requires the running app and separate screenshot evidence.

## Scope/acceptance matrix

| Track | Source now? | Target | Acceptance |
| --- | --- | --- | --- |
| Shell | QML rough page, **not faithful** | exactly reproduce owner screenshot | golden 1536×1024 visual diff + manual review |
| SSH | host pin, password/key, persistent sessions and profiles | persistent trusted session, key auth, cancellation | Windows + Android Debian real tests |
| Smart Files | navigation, preview, transfers, Trash, editor, search | search, collections, preview, safe writes/transfers | permission, Unicode, conflict, interruption tests |
| Logs | journal/files, following, patterns, inspector, service view | journal + file logs, live tail, evidence-linked synthesis | real Debian service logs, failure and rights coverage |
| Terminal/Services | none in Qt | support flagship workflows | real PTY and service query tests |
| Mail/Browser/AI/Licenses | none in Qt | future gated additions | separately qualified |

## Documentation maintenance rule

Any behavioral change must update (as applicable):
- the specific feature spec;
- `README` and `QT_MIGRATION` when a capability becomes implemented or loses support;
- `DEVELOPMENT_MATRIX` and acceptance evidence after real tests;
- `UI_SPEC` **only** after explicit owner approval for a reference-visible change.

No one should move a planned feature into “implemented” because its document or an isolated fixture exists. Do not equate test logs with successful runtime qualification unless source version and platform match.

## Separate marketing website caveat

Static pages in `www/` have been rewritten for Windows+Android, Smart Files, Logs, local Qt/SSH architecture and an explicit **development / no-download-yet** status. Their CSS desktop illustration remains clearly labeled conceptual until the verified original screenshot is imported; it does **not** qualify as exact UI or an app build. The site's source of truth is `docs/website/site-template.html` + `copy.json` and generator `generate.py`.

## Automated checks

Run `python3 tool/check_docs_consistency.py`. This checks relative Markdown links, the presence of normative files, contradictory stale statuses and consistent core UI/flagship references. It **does not** build Qt, prove security, or establish screenshot fidelity. Actual visual tests must use the original master PNG.
