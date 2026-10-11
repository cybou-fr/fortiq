# FORTIQ — Product Scope

**Product:** FORTIQ — Distributed Desktop Software.
**Status:** product direction and release requirements, not proof of implemented behavior.
**Source baseline:** `main` @ `cb737744`, 2026-10-11.

## 1. Product definition

FORTIQ provides a unified desktop workspace for accessing, managing and working with computing resources across local and remote environments. Applications share a consistent window system, resource identity, navigation, tasks and session state.

Public product description (EN):

> FORTIQ is distributed desktop software that brings local and remote computing resources into one unified workspace.

Describe FORTIQ through its functionality and benefits. All public messaging should distinguish verified present capabilities from planned ones.

## 2. Product experience

- One desktop shell, with multiple independently identified resources.
- Every window and operation has an explicit resource owner: local device, remote host or connected account.
- Launch an application from the desktop and move to related resources without copying addresses or losing context.
- Automatic remote-environment preparation is part of the experience: discover required tools, install/update required dependencies when privileges permit, show progress and errors, and coordinate changes.
- User-created documents, profiles and settings survive reconnection and application restart.
- One consistent entitlement to the **complete FORTIQ functionality** for both paid offers.

Current status: Windows prototype; the shell appears after an SSH connection. Multi-resource workspace, persistence and portable client features are targets, not completed features.

## 3. Application catalog

Existing widgets stay in the product. Functional categories are navigation, not separate products or paid add-ons.

| Area | Applications / capabilities | Primary user outcome |
|---|---|---|
| Workspace | Files, Editor, Docs & Markdown, Mailbox, Browser, Media Viewer | Work with resources and content |
| System | Terminal, Processes, Services, Logs, Network; planned Disks | Diagnose and operate remote Linux |
| Tools | Search Hub; planned shared Tasks, Transfers, Connections, History | Find, track and move work |
| Platform | Desktop Shell, App Registry, Session/Resource Manager, Local Store, Notifications, Licensing | Consistent behavior across apps |

The existing Mailbox reads Linux system mail; account mail (IMAP/SMTP/MIME) is a separate future capability within the same Mail application. The planned Disks module begins with observability before modification.

## 4. Product principles

1. **Complete product:** Intro and Pro expose the same applications and capabilities; pricing changes the entitlement duration only.
2. **Connected resources:** actions retain server/account identity; changing the active desktop context never silently redirects work.
3. **Local-first shell:** the shell, billing view, license state and saved local data are available without an active SSH connection.
4. **Automatic preparation:** retain and improve the existing ServerSetupService behavior; distinguish required application dependencies from wider operating-system updates.
5. **Continuity:** long-running tasks, drafts and terminals have explicit recovery or failure behavior.
6. **Small releases:** validate end-to-end workflows, not only added windows.
7. **Honest status:** implemented, verified and released are different states.

## 5. Reference workflows

- Services → Logs → Editor → Save → Restart → Verify status.
- Files → Open in Editor/Viewer → Save/Transfer with observable progress.
- Network port → Process → Service → Logs.
- Connect to host A → open documents on A → switch to host B → return to A without context loss.
- Mail → Attachment → Viewer/Files after account-mail support is implemented.
- First run → purchase Intro/Pro → activate FORTIQ → use the desktop.
- Intro expiration → purchase Pro → regain access with existing settings and documents intact.

## 6. Commercial product

The distribution channel is Paddle. Details and lifecycle invariants are in [LICENSING.md](LICENSING.md).

| Offer | Purchase | Entitlement |
|---|---|---|
| Intro | **€1, one-time** | Full FORTIQ access for seven consecutive 24-hour periods, once per customer; no auto-renewal |
| Pro | **Annual recurring subscription** | Full FORTIQ access while entitled; annual price is **to be decided** |

Intro does not convert automatically to Pro. Pro renewal and cancellation must be plainly displayed. Purchase and license management are part of the desktop, using a minimal licensing backend.

## 7. Delivery priorities

- **R0 — Paid Windows foundation:** secure entitlement and checkout, repeatable release, essential SSH/setup reliability and baseline automated tests.
- **R1 — Connected workspace:** stable multi-resource identity and desktop context, SFTP/Tasks, terminal sessions, safe editor saves, Services/Logs/Network/Disks read-only observability.
- **R2 — Daily applications:** documents and search, PDF/image viewer, verified browser workflow, real account mail and cross-app actions.
- **R3 — Polished distributed desktop:** persistent workspaces, notifications/history, advanced integrations and tablet validation.
- **R4 — Advanced operations:** advanced disk management, further platform coverage and optional workspace capabilities subject to evidence.

See [DEVELOPMENT_MATRIX.md](DEVELOPMENT_MATRIX.md) for milestones, dependencies and acceptance tests; [ARCHITECTURE.md](ARCHITECTURE.md) for cross-application contracts.

## 8. Scope governance

Every planned feature has an owner, priority, acceptance test and verified status. Adding new products, extra tariff tiers, per-feature payments or infrastructure subscriptions requires an explicit product decision. Pricing, device limits, offline duration and other unresolved policy parameters must not be invented in implementation.
