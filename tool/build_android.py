#!/usr/bin/env python3
"""Developer-only Android arm64 build using explicit, already installed toolchains."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("qt-android", "qt-host", "sdk", "ndk", "jdk", "deps"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--build", type=Path, default=Path("build-android-arm64"))
    parser.add_argument("--cmake", default=shutil.which("cmake"))
    parser.add_argument("--ninja", default=shutil.which("ninja"))
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--build-type", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--sdk-version", type=int, default=36)
    parser.add_argument("--define", action="append", default=[], metavar="NAME=VALUE", help="extra CMake cache entry, e.g. FORTIQ_ANDROID_BROWSER_SELFTEST=ON (developer APK of tool/android_browser_check.py)")
    parser.add_argument("--package", action="store_true", help="Also build an unsigned/debug APK; never installs it")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    toolchain = args.qt_android.resolve() / "lib/cmake/Qt6/qt.toolchain.cmake"
    for path in (toolchain, args.qt_host, args.sdk, args.ndk, args.jdk / "bin", args.deps):
        if not path.exists():
            parser.error(f"Missing installed dependency: {path}")
    if not args.cmake or not args.ninja:
        parser.error("Specify installed --cmake and --ninja executables")
    env = dict(os.environ)
    env["JAVA_HOME"] = str(args.jdk.resolve())
    env["ANDROID_SDK_ROOT"] = str(args.sdk.resolve())
    env["ANDROID_NDK_ROOT"] = str(args.ndk.resolve())
    env["PATH"] = str(args.jdk.resolve() / "bin") + os.pathsep + str(args.qt_host.resolve() / "bin") + os.pathsep + env.get("PATH", "")
    build = (root / args.build).resolve()
    configure = [args.cmake, "-S", str(root), "-B", str(build), "-G", "Ninja",
                 f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}", f"-DCMAKE_MAKE_PROGRAM={Path(args.ninja).as_posix()}",
                 f"-DQT_HOST_PATH={args.qt_host.resolve().as_posix()}",
                 f"-DANDROID_SDK_ROOT={args.sdk.resolve().as_posix()}", f"-DANDROID_NDK_ROOT={args.ndk.resolve().as_posix()}",
                 f"-DCMAKE_PREFIX_PATH={args.deps.resolve().as_posix()}",
                 f"-DCMAKE_FIND_ROOT_PATH={args.deps.resolve().as_posix()}",
                 "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-28", f"-DFORTIQ_ANDROID_SDK={args.sdk_version}",
                 f"-DCMAKE_BUILD_TYPE={args.build_type}", "-DBUILD_TESTING=OFF"] + [f"-D{d}" for d in args.define]
    subprocess.run(configure, env=env, check=True)
    subprocess.run([args.cmake, "--build", str(build), "--target", "fortiq", "-j", str(args.jobs)], env=env, check=True)
    if args.package:
        # androiddeployqt only ADDS libraries to android-build/libs: after a dependency or plugin is dropped (audit backlog 10: Qt Virtual
        # Keyboard and Qt Quick Timeline are GPL-only) the stale copies would still end up in the APK.
        shutil.rmtree(build / "android-build" / "libs", ignore_errors=True)
        subprocess.run([args.cmake, "--build", str(build), "--target", "fortiq_make_apk", "-j", str(args.jobs)], env=env, check=True)
        apk = build / "android-build/build/outputs/apk" / ("release" if args.build_type == "Release" else "debug") / (
            "android-build-release-unsigned.apk" if args.build_type == "Release" else "android-build-debug.apk")
        if apk.exists():
            checker = Path(__file__).with_name("check_third_party.py")
            subprocess.run([sys.executable, str(checker), "--apk", str(apk), "--qt", args.qt_host] + (["--allow-debug-tooling"] if args.build_type != "Release" else []), check=True)


if __name__ == "__main__":
    main()
