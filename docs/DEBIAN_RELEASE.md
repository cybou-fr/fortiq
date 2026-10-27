# FORTIQ Debian 13 (.deb) — initial packaging

**Status: build recipe, not a qualified production release.** Debian is a client
desktop target here; it connects to an existing Linux server over SSH, just
like Windows/Android. No FORTIQ daemon or remote component is installed.

## Requirements

Run in a Debian 13 **graphical desktop VM** (GNOME, KDE or another Qt-capable
desktop; Wayland or X11). Install the build dependencies:

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build python3 pkg-config \
  qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools \
  qt6-tools-dev libssh2-1-dev libssl-dev zlib1g-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtquick-window
```

Qt **>= 6.8** is required by CMake; use `--qt-prefix` with a matching Qt
kit if the distribution kit does not satisfy the requirement. The runtime
QML modules above are intentionally explicitly installed, because ELF
dependency detection does not detect QML `import` statements.

## Build and inspect

```sh
python3 tool/package_debian.py
# Result: build-debian/fortiq_<version>_amd64.deb (on an amd64 VM)
sudo apt install ./build-debian/fortiq_*.deb
fortiq
```

For an application on a machine without the build dependencies:

```sh
sudo apt install ./fortiq_*.deb
```

The package registers an application launcher, icon and Debian dependencies.
CPack runs `dpkg-shlibdeps` to derive shared-library dependencies.
It does **not** bundle Qt, libssh2 or OpenSSL, and therefore depends on the
Debian shared libraries. Confirm all required QML modules are available via
APT and on a clean installed VM.

## Package with its own Qt 6.12 (`--qt-prefix`)

Debian 13 ships Qt 6.8, but the shell uses `RectangularShadow` (Qt 6.9+), so on the
system Qt it does not load (`fortiq_layout` fails 45 of 47 checks). When the build uses a Qt
that is not under `/usr` (`--qt-prefix /path/to/Qt/6.12.0/gcc_64`, a Qt 6.12 kit with
`qtwebengine`, `qtwebchannel`, `qtpositioning`, installed for example with `aqtinstall`),
CMake switches `FORTIQ_DEB` to the **bundled** mode:

- the application and its Qt (libraries, plugins, QML modules, Qt WebEngine with
  `QtWebEngineProcess`, resources and `qt.conf`) are installed into `/opt/fortiq` by
  `qt_generate_deploy_qml_app_script`; `postinst`/`prerm` link `/usr/bin/fortiq`, the launcher and
  the icon into `/usr` and remove them on uninstall;
- Debian dependencies are only the system libraries that `dpkg-shlibdeps` finds for the bundle
  (libc, X11/xcb, Mesa, NSS, fontconfig, libssh2, OpenSSL …); the `qml6-module-*` packages are not needed;
- the package is large (Chromium): about 160 MB packed, about 700 MB installed.

Needed in the build environment besides the list above: `file` and `dpkg-dev` (CPack `SHLIBDEPS`).
`tool/package_debian.py` runs the whole ctest set before packaging. With Qt 6.12 on Debian 13 (WSL)
25 of 26 pass reliably; `fortiq_gateway` fails on `httpPostBodiesContentLengthAndChunked`
(a 2 MB request body echoed through the fake tunnel: about 3 runs of 4 stall for ~47 s instead of 1 s;
it passes on Windows and passed in one full Linux run). The cause is **not found** — it may be a real
back-pressure stall of the gateway on Linux, so the Linux browser path must not be called verified until
it is fixed; the package can be built with `--skip-tests` meanwhile (not qualified). Remaining of this
section's claims that are **not** verified: a clean Debian VM, Wayland,
GNOME/KDE launcher, upgrade/uninstall, the `libssh2` security gate below. The
`qtwebengine_devtools_resources.pak` is shipped although DevTools are closed (it can be removed).

## Release gates — mandatory before public distribution

1. **libssh2 security:** current Windows/Android builds use a pinned, fixed
   upstream commit because of known flaws in earlier libssh2 releases.
   Debian's version number alone does not prove that its package includes
   the relevant backported fixes. Verify the Debian security changelog and
   linked library version, or build against a verified patched libssh2.
2. Verify GPLv3 source delivery, third-party notices and license information.
3. Run the complete unit/layout/live SSH suite on Debian; this script runs
   offline CTest but not the disposable live fixture.
4. Install on a separate clean Debian graphical VM and test pinning, file
   operations, terminal, mail, documents, launcher, uninstall and upgrade.
5. Validate app icon/launcher under GNOME/KDE, both X11/Wayland if supported.
6. Check ELF dependencies with `ldd /usr/bin/fortiq` and runtime QML imports;
   no developer-installed Qt libraries may be required.
7. Build signed/checksummed release artifacts with traceable commit hash and
   tested install/upgrade/rollback path.

This first recipe deliberately makes **no production/security qualification
claim** and does not change the project's existing Windows/Android workflows.
