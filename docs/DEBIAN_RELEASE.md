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
