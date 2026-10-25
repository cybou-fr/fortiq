# OpenSSL overlay port (R15)

Copy of `ports/openssl` from the vcpkg `master` branch: OpenSSL **3.6.5** (port-version 2). The local vcpkg checkout
(snapshot of 2026-08-01) only offers 3.6.3, which misses 24 CVE fixes of 3.6.4 and 3.6.5 (see `docs/SBOM.md`, `docs/STABILIZATION_PLAN.md`).

- Used through `--overlay-ports=third_party/vcpkg-overlay-ports` for both the Windows and the Android arm64 dependency trees.
- Unchanged from upstream: portfile, patches, scripts and the archive SHA-512.
- **Remove this directory** as soon as the vcpkg checkout used for builds ships OpenSSL >= 3.6.5 (update the checkout, then rebuild).
