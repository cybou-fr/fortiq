# FORTIQ: план дальнейшей разработки

Статус на 23.10.2026. Это **план, а не отчёт о реализации**: фактическое состояние кода — только
[README](../README.md), [QT_MIGRATION](QT_MIGRATION.md) и [DEVELOPMENT_MATRIX](DEVELOPMENT_MATRIX.md).
Приоритеты продукта задаёт [PRODUCT_SCOPE](PRODUCT_SCOPE.md); внешний вид — [UI SPEC v1](UI_SPEC_V1.md);
контракты flagship-приложений — [Smart Files](SMART_FILES_SPEC.md) и [Logs](LOGS_INTELLIGENCE_SPEC.md).
План не меняет принятых решений ([DOCUMENTATION_AUTHORITY](DOCUMENTATION_AUTHORITY.md)); любое отступление
от них требует решения владельца.

## 0. Состояние после аудита (23.10.2026, позже анализа ниже)

С момента анализа ниже добавлены Navigateur, Réseau, Système, Bases de données и Courrier (чтение, теги, отправка, мастер, вложения), хранилище секретов Windows/Android, TaskManager (часть приложений) и разбиение крупных файлов.
Уровень проверки и риски — [AUDIT_2026-10-23](AUDIT_2026-10-23.md); **упорядоченный бэклог следующей фазы — §6 аудита** (сначала выход с идущими задачами, политика внутренних адресов Navigateur, проверка прав роли БД,
стабильность layout-тестов и CI, санитайзеры/фаззинг, квалификация чистой Windows и физического Android). Ниже — исходный анализ; его пункты 3, 4 и 6 уже устарели (секреты — в хранилище ОС, TaskManager есть, инструмент Mail выбран: notmuch + mbsync + msmtp).

**Трек стабилизации (24.10.2026):** безопасность → надёжность → рефакторинг → производительность → зрелость приложений → квалификация, без новых платформ и SDK — [STABILIZATION_PLAN](STABILIZATION_PLAN.md) (план и бэклог S-1…S-11, не отчёт о реализации).

## 1. Исходная точка (анализ 23.10.2026)

В исходниках инженерный прототип R0.1 (≈5 тыс. строк C++, ≈5,5 тыс. строк QML): SSH (пароль/ключ,
закреплённый отпечаток, постоянные сессии, менеджер серверов), Files (просмотр, редактор с атомарным
сохранением, передачи, Trash, поиск), Logs (журнал, файлы, инспектор инцидентов без AI, службы),
Terminal (PTY/libvterm), Disks (только чтение), MDok (Markdown без PDF), сборки Windows MSVC и
Android arm64, установщик Windows.

**Риски**

1. Desktop Shell визуально не принят; обои, иконки и шрифт — заглушки; [SHELL_FIDELITY](SHELL_FIDELITY.md)
   отстаёт от кода (описывает `AppRail`/`Dock`, заменённые `AppPanel`).
2. Нет квалификации на чистой Windows, физическом Android, touch/IME.
3. Секреты только в RAM; нет защищённого хранилища, ssh-agent и теста зашифрованных ключей.
4. Нет общего TaskManager, WorkspaceState на сервере и подготовки сервера.
5. Крупные файлы: `SshClient.cpp` (≈1400 строк; разбит 23.10.2026, см. QT_MIGRATION), `LogsController.cpp`, `RemoteController.cpp`, `FilesWindow.qml`.
6. Серверный инструмент для Mail не выбран; Browser с движком: шлюз (этап 1) и окно Qt WebEngine для Windows (этап 2, 27.10.2026) готовы, этап 3 (Android: системный WebView + `ProxyController`, 27.10.2026) проверен на эмуляторе ([BROWSER](BROWSER.md)); этап 4 (вкладки, индикатор «через сервер», общий профиль в памяти, popup как вкладки) сделан 27.10.2026 и проверен на Windows-стенде и на эмуляторе Android; Linux-сборка не проверялась, Android на физическом устройстве не проверялся, закладки и история — только на сервере и позже.
7. Открыты подпись кода, размер пакета и коммерческая лицензия Qt.

## 2. Этап A — закрыть R0.x (≈2–3 недели)

1. **Приёмка оболочки.** Финальные обои, шрифт, иконки; адаптации UI SPEC §4 (планшет, узкий экран);
   сверка `tool/compare_shell_screenshot.py` и ревью владельца; актуализация SHELL_FIDELITY.
2. **Защита учётных данных.** Хранилище ОС (Windows Credential Manager, Android Keystore); при его
   недоступности — явный отказ, без тихого файла; затем ssh-agent/Pageant и тест зашифрованного ключа.
3. **Платформенная квалификация.** Чек-лист и сборки для чистой Windows (проверку выполняет владелец,
   VM владельца не используются) и для физического Android (touch, IME, фон, потеря сети).
4. **Рефакторинг без смены поведения (сделан 23.10.2026, см. QT_MIGRATION).** Разделить SSH-сессию, SFTP, передачи, поиск и PTY; вынести
   подкомпоненты из `FilesWindow.qml`; опора — существующие тесты.
5. **TaskManager (первый срез сделан 23.10.2026: Files; остальные приложения — позже).** Единая модель задач (queued/running/cancelling/cancelled/succeeded/failed/`unknown`)
   вместо очередей отдельных приложений ([ARCHITECTURE](ARCHITECTURE.md) §4).

## 3. Этап B — Files и Logs до контракта (R1 / R1.5)

- **Files:** папки и множественные передачи, возобновление и проверка хеша, подборки, превью
  документов, «Открыть с помощью», копирование и перемещение на сервере.
- **Logs:** корреляция нескольких источников, ядро, локализация разбора systemd (сейчас только
  английские сообщения), прогон на реальном VPS с согласия владельца. Без AI.
- **Terminal:** mouse reporting для TUI, выбор слов, touch-выделение Android, интеграция tmux.
- **Поддержка:** System/Services/Processes/Network на `systemctl`, `ps`, `ss`, `df`, `free`; только
  чтение, недоступный источник — ошибка, а не пустой список.
- **WorkspaceState** на выбранном Linux-сервере; новых локальных баз рабочих данных нет.

## 4. Этап C — повседневные приложения (R2)

- **Документы:** PDF через проверенный серверный инструмент (кандидаты `pandoc`, `pdftoppm` не
  установлены и не выбраны), поиск и замена, история.
- **Editor / Viewer** поверх существующих safe-save примитивов.
- **Mail:** сначала исследование существующих консольных клиентов (JSON-вывод, лицензия, наличие в
  Debian/Ubuntu); выбор представляется владельцу до реализации. Собственных серверных агентов нет.
- **Server Setup:** проверка возможностей, затем изменения только с информированным согласием, живым
  логом, вторым входом и откатом ([ARCHITECTURE](ARCHITECTURE.md) §8).
- **Disks:** SMART и `du`; операции изменения — отдельный срез с повторной идентификацией устройства.

## 5. Этап D — релиз (R3)

- У клиента нет прямых внешних подключений: только SSH к выбранному серверу.
- Подпись кода Windows и Android, уменьшение пакета, решение по лицензии Qt.
- Browser: первый срез (GUI к `curl`, [BROWSER](BROWSER.md)) сделан 23.10.2026. Полноценный движок (решение владельца 26.10.2026: движок на устройстве, весь трафик через сервер): этап 1 — локальный HTTP-прокси-шлюз + SSH-туннель `direct-tcpip` с политикой адресов — **сделан и проверен без движка** (26.10.2026); этап 2 — Qt WebEngine на Windows/Linux с принудительным прокси и тестами отсутствия утечек; этап 3 — Android WebView + `ProxyController` и проверка на эмуляторе; этап 4 — вкладки (вопрос о внутренних адресах уже в окне движка). Этап 2 для Windows сделан 27.10.2026 (тест утечек проходит); этап 3 сделан 27.10.2026 и проверен **только на эмуляторе** (физическое устройство, фон дольше минуты, другие версии WebView — нет; остаточный риск WebRTC через `window.frames[n]`); Linux не проверялся; этап 4 сделан 27.10.2026 (вкладки; закладки и история на сервере остаются).
- Опциональный `ai` над llama.cpp — после стабильных Files и Logs ([AI_CLI](AI_CLI.md)).

## 6. Порядок и критерии

Начинать с A.1 и A.2; рефакторинг (A.4) выполнить до расширения Files/Logs; исследование Mail можно
вести параллельно, не затрагивая код. Срез считается завершённым по правилам приёмки
[DEVELOPMENT_MATRIX](DEVELOPMENT_MATRIX.md) §4: тест на реальном Debian, включая отказы, документация
и честное указание непроверенного. Работа ведётся в `main` без веток.

## Product / architecture completion pass — 2026-10-28

This is an **audit finding, not a release approval**. The repository contains dedicated shared core services (RemoteController, TaskManager, SSH transfer/terminal plumbing), but feature-complete/refactor-complete/Desktop-Experience-complete status has **not** been demonstrated.

Concrete inventory of QML window size (approximate source lines, default branch at review): Documents 847, Logs 676, Files 558, DB 457, System 416, Network 319. The large windows still own substantial coordination (tabs/drafts and pending actions in Documents, filtering/selection and list state in Files, selection/follow/inspection in Logs). That is a **review target**, not proof that each function should become a shared service. Before extraction, identify actual repeated policies with equal semantics; keep app-specific behavior local.

Immediate lifecycle fix: LogsWindow previously invoked `loadSources()` and `reload()` from connection, component completion and window visibility callbacks, potentially scheduling duplicate remote reads. These triggers are now coalesced into one deferred initialization call while the window is visible; a connection/server change still requests fresh data even if frozen events from the previous server remain. This is an implementation change, **not a measured optimization**.

**Remaining product gates:**

- P0: Qt desktop build, TaskManager/SSH tests, source-switch isolation and safe file-operation outcome checks; CI intentionally stays off until owner changes policy.
- P1: inspect Files/Documents/Logs QML lifecycle and ownership before extracting reusable controllers/components; do not create speculative frameworks.
- P1: desktop UX walkthrough covering keyboard focus, resizing, multiwindow/session transitions, dialogs, errors, empty/busy states and visual comparison with the approved screenshot. The golden screenshot is not yet checked into the repository per UI spec.
- P1: performance profiling for large directory lists, logs, document tabs, large shell output and QML binding churn; avoid claims without runtime measurements.

No build or tests were performed in this pass.

### Files / MDok ownership and pagination audit (2026-10-28)

Inspected FilesWindow and DocumentsWindow. Documents owns editor/tab UI coordination while DocumentsController owns remote document state, with server-generation checks for tab association; Files owns directory filtering, sorting and reveal navigation while RemoteController owns paginated listing. Shared extraction must target proven duplicate contracts rather than force these distinct responsibilities into a generic controller.

Found an event-loop pressure bug in Files' cross-app `revealFile` flow: when a file was not in the current page and more remote entries remained, `tryReveal` called `loadMore(true)` and immediately rescheduled itself through `Qt.callLater`, even before the SSH page completed. That unbounded retry loop is now removed; the existing `onShownChanged` and controller `onChanged` signals retry on data/state updates. A paginated reveal must be manually verified with a remote directory containing many entries. No tests or builds run, CI remains disabled.

### Desktop Shell session-end window cleanup — 2026-10-28

Static inspection of `DesktopShell.qml` found `endSession()` closed most app windows but omitted `settingsOpen` (and fixture `browserOpen`). This could leave a stale settings panel visible when returning to server login. Session end now closes both, resets the foreground app to Files, and closes the notifications overlay. The existing guarded-logout checks for MDok dirty/busy tabs and TaskManager operations are unchanged. Verify logout with Settings foreground on desktop and narrow/adaptive layouts; no build, tests or CI were run.

### Desktop window focus after close — 2026-10-28

`DesktopShell.qml` now chooses the topmost remaining open application from `windowOrder` when the foreground window closes, instead of leaving the foreground key pointed at the closed app or unconditionally switching to Files. The same `focusAfterClose()` policy is applied to standard app windows and the dynamically created Chromium browser window. On adaptive layouts, Files is reopened only when it is the chosen fallback. When no windows remain on the desktop, the foreground key resets to `files` without opening a window. This is a source-level lifecycle change; actual keyboard focus and window transitions still require Qt runtime/visual verification. CI, builds and tests remain disabled.

### Keyboard focus on Desktop Shell foreground transitions — 2026-10-28

`DesktopShell` now schedules a focus handoff after `foregroundApp` changes: when the selected app window is visible it receives `forceActiveFocus()`, otherwise the shell takes focus. This addresses keyboard focus left on hidden controls after a close or application switch. This is a focus-scope fallback, not a guarantee that a particular child editor regains its previous caret; modal dialog and per-app focus restoration require runtime verification. No Qt build or tests run; CI remains disabled.

### Modal and search focus protection — 2026-10-28

The deferred `DesktopShell.restoreWindowFocus()` handoff now defers to the server/login screen, the session-end guard, active global overlays, and a nonempty top-bar search query. Without this guard, raising an app while an overlay is in use could steal keyboard input from the overlay or confirmation dialog. Per-app modal dialogs require additional visual/runtime review. This source-level guard was committed without running CI, builds or tests.

### Compact Shell overlay geometry — 2026-10-28

Static review found the notifications overlay used `height: min(shell.height - 120, contentHeight)`, which can become negative on a very short shell and leaves unnecessarily large bottom clearance. It now bounds height to non-negative available space below its 52px top origin (`shell.height - 60`). The session menu x-position also has an 8px left bound on narrow shells. These are defensive geometry changes, not a guarantee that all overlays are usable at arbitrary viewport sizes; manual Qt checks on compact and resized layouts remain necessary. CI, builds and tests were not run.

### Compact task-panel positioning — 2026-10-28

Static Shell review found the task panel (`TaskCenter`) could calculate a negative top position when its desired position above the dock exceeded the available height. Its horizontal width is now nonnegative and its x/y origins bounded with minimum margins, while preserving dock-aware placement on normal desktop windows. This is a defensive layout correction, not a validated responsive design: the panel may still exceed the viewport if the host becomes smaller than its content, and that case needs visual/runtime testing. No CI, builds, or tests were run.

### TaskPanel scrollable viewport height — 2026-10-28

`TaskPanel.qml` now accepts `availableHeight` and caps its expanded height to that viewport allowance (while preserving the header). `DesktopShell` supplies its current height minus small margins. Because TaskPanel already uses a bounded/clipped ListView with a vertical scrollbar, task rows remain scrollable instead of requiring the whole panel to extend beyond a short viewport. This specifically improves access to task action buttons when the host becomes short; conflict-action row width and on-screen keyboard interactions still need real-device inspection. No build or tests run; CI remains disabled.

### TaskPanel conflict actions on narrow displays — 2026-10-28

Conflict decision controls in `TaskPanel.qml` previously sat in a single `Row` with a fixed 64px delegate, so long French labels could overflow the panel. They now use a width-constrained `Flow`; the task delegate grows with the actual wrapped controls, allowing the existing `ListView` to scroll instead of clipping lower actions. On narrow rows the final replace-confirmation label is shortened to `Confirmer`, without changing its action. This requires runtime checking for very small sizes, font scaling and keyboard focus. Build/tests not run; CI disabled.

**Cross-app UX review still required:** active focus inside per-app modals, status/error readability, touch target sizing, responsive navigation, task conflict handling via keyboard, and visual comparison to approved reference. Source-only checks are not release acceptance.

### Cross-app dialog keyboard interaction — 2026-10-28

A source-level review across Files, MDok, Logs, Mail, DB, System, Network and Disks found a shared accessibility gap in `GlassDialog`: action buttons were clickable `Rectangle`/`MouseArea` elements but were not keyboard tab stops. Shared dialog actions now accept Tab focus and Space/Enter activation, show a visible focused border, and route mouse and keyboard through the same guarded handler. This applies to every dialog using the shared component rather than duplicating fixes in apps. Runtime verification remains required for Tab order, screen readers, input method behavior, scaled layouts and busy-state transitions; CI/build/tests were intentionally not run.

### Cross-app lost-SSH state recovery — 2026-10-28

Reviewed the shared `LinkBanner` used by Disks, Network and System (and other apps). A lost-link warning existed, but its `Réessayer` action was mouse-only, unlike the recently improved shared dialogs. The retry control is now keyboard-focusable via Tab, exposes an active-focus border, and accepts Enter/Space as well as mouse clicks. The warning text width is clamped to avoid negative geometry on narrow screens. This preserves each app's own retry callback. Empty/loading/error states are still implemented differently between apps and need unified UX acceptance criteria; no Qt runtime, build, tests or CI were run.
