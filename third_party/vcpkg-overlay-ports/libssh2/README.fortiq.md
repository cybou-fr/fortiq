# libssh2 overlay port (R14)

Copy of the vcpkg port `libssh2` 1.11.1#3 with two changes:

1. `REF` is the master commit `6e7b2076a6ced2230968b27ee7c1abb77fe5a88d` (24 October 2026, all 88 upstream CI checks green) instead
   of the 1.11.1 tag. It contains the fix for CVE-2026-55200 (`97acf3d`, packet-length bounds) and the other memory-safety fixes
   made since the 1.11.1 release (hundreds of commits; see docs/SBOM.md). Version string: `1.11.1-git6e7b207`.
2. vcpkg's `cmake-config.diff` / `pkgconfig.diff` are dropped: they do not apply to master (the files they edit were restructured upstream).
   Check the installed `share/libssh2/*.cmake` after a rebuild (the build and `find_package(libssh2 CONFIG)` in CMakeLists.txt are the test).

Use: `vcpkg install libssh2:x64-windows --overlay-ports=third_party/vcpkg-overlay-ports --recurse`, same for `arm64-android`
(Android also needs `ANDROID_NDK_HOME`; see docs/ANDROID_FOUNDATION.md for the dependency directory).
Remove this directory when a libssh2 release newer than 1.11.1 contains the fixes and the vcpkg port has it.
