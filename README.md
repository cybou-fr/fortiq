# FORTIQ — Distributed Desktop (Qt rebuild)

**One Linux workspace, on Windows and Android.** FORTIQ is a native client for an existing Linux server. The Linux host performs work and stores persistent working data; FORTIQ displays results and requests operations through **SSH/SFTP**.

> **Current source status (Qt milestone R0.1):** new C++20 / Qt 6 / QML project and a limited SSH/SFTP Files vertical slice. This is **not** a complete port of the former Flutter application, not a published Android/Windows build and not a security-qualified release. The final Flutter sources remain available at tag `flutter-final`; the active `main` contains only the new implementation and documentation. See [QT_MIGRATION](docs/QT_MIGRATION.md).

## Current Qt implementation

- One Qt Quick Controls application using C++20, the same QML UI and native SSH/SFTP adapter on Windows and Android.
- Responsive desktop navigation rail and compact mobile bottom navigation.
- Linux server address, port, username and password fields (password is **RAM-only**, never stored to settings).
- SSH host-key **SHA-256 fingerprint pinning**. On first connection, the user must verify the displayed fingerprint **through an independent trusted channel** and explicitly pin it before password authentication. Changed host keys are blocked. Host/port/user and pinned fingerprints are the only locally persisted bootstrap values.
- Actual SFTP directory listing, navigation into directories, parent navigation and refresh with filenames and type. No fake local filesystem or direct network fallback. No mutations or file uploads are implemented yet.
- Work in a background thread to avoid blocking the QML event loop. Responses are tied to the request generation so a late result cannot reopen a disconnected session.
- Tests for remote path validation; no implicit shell command execution or automatic server setup in this milestone.

**Not implemented in the Qt version:** persistent SSH session, private-key authentication, reconnect/cancel, file open/edit/save and transfers, multi-server windows, mail, browser, system tools, server setup, licensing, AI and server-side workspace persistence. The historical Flutter README and prior test claims **do not apply to Qt**.

## Build prerequisites

- Qt **6.8 or newer** with Qt Quick, Quick Controls, Network, Concurrent and Qt Test (if `BUILD_TESTING=ON`). Use a matching C++20 toolchain and CMake 3.21+.
- **libssh2** compiled for the **same target architecture/ABI**. OpenSSL or another configured crypto backend and the TLS/dependency libraries must be included according to the selected libssh2 build. No SSH binaries, Termux or platform-specific shell calls are used.
- On Windows: Qt MSVC 2022 x64 (or matching MinGW), a suitable CMake libssh2 package (e.g. a vcpkg-installed target for the same toolchain), and the Qt Windows deployment tool.
- On Android: Qt for Android and matching SDK/NDK/JDK kits, plus a libssh2 build for **arm64-v8a** (and its crypto dependencies). If shared libraries are used, they must be packaged with the APK. Prefer a static cross-compiled libssh2 bundle with its required dependencies resolved.

Example CMake configuration (adjust installation prefixes, Qt kit and vcpkg toolchain to your environment):

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/kit -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Qt Creator: open `CMakeLists.txt`, select **Desktop Qt** or **Android arm64-v8a** kit, configure the target libssh2 dependency, and build/run. `qt_add_executable` uses a regular executable for Windows and Android packaging for the Android kit. The manifest must contain `android.permission.INTERNET`; the CMake code registers it with Qt 6.9+ and Qt Network also contributes its permission requirements. **Inspect the generated merged manifest before Android installation.**

Qt 6.8 builds require verifying how that kit generates the INTERNET manifest entry. No APK or EXE has been built in this repository session.

## Using the R0.1 slice

1. Enter the Linux SSH host, port, username and password (the Linux server must permit password authentication and SFTP). This first milestone does **not** yet support private-key-only servers.
2. On a new host, compare the offered `SHA256:` fingerprint to the server's known fingerprint from its trusted console. Example on Linux server: `ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub -E sha256` (the server might negotiate a different host-key algorithm; compare the exact negotiated key).
3. Accept only after independent verification; connect and open **Files** to browse. Remote data is only visible with SSH. No file writes are offered.
4. Disconnect to clear the password kept in the controller's RAM. Session and request cancellation remain next-stage work; ongoing individual requests have transport timeouts.

Security caveat: a native SSH client has substantial untested attack surface. First-use pinning is not a substitute for out-of-band verification, and the Qt app is currently an engineering prototype.

## Architecture and scope

```text
Windows / Android
 └── FORTIQ Qt 6/QML UI
      ├── RemoteController (server context, request generation, UI state)
      ├── SshClient (libssh2: SSH host key check, password auth, SFTP)
      └── UI bootstrap settings (host, port, user, verified fingerprint)
              │ SSH encrypted channel
              ▼
         Existing Linux sshd / SFTP service
              └── Files (read-only first); Linux CLI adapters later
```

The original product rules remain binding: **universal desktop, not DevOps-only; all business operations over SSH to Linux; no proprietary server daemon, no direct IMAP/SMTP; existing Linux tools first; AI optional**. Product planning is in [PRODUCT_SCOPE](docs/PRODUCT_SCOPE.md), [SSH_FRONTEND_CONTRACT](docs/SSH_FRONTEND_CONTRACT.md), and the migration plan in [QT_MIGRATION](docs/QT_MIGRATION.md). These long-form documents contain older Flutter-specific implementation notes; consult the Qt migration document for the new actual state.

Private repository; no change to the product's commercial licensing policy is implied.
