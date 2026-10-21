# Brand assets static regression checks

Run after `tool/generate_brand_assets.py`:

```sh
python3 tool/check_brand_assets.py --output build/generated/branding --android-output build/generated/android
```

Verifies: 7-entry valid ICO (16–256), generated Win32 RC, Qt mark PNG, five density mipmap pairs, transparent 432px adaptive foreground + monochrome, Android adaptive icon XML and color resources. This is *asset integrity* only, not a real EXE/APK runtime assertion.

CMake registers it as `ctest -R fortiq_brand_assets` when the full build is configured with BUILD_TESTING.
