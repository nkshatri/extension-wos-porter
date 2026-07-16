---
name: wos-woa-dashboard
description: "Windows on Arm Ecosystem Dashboard lookup â€” resolves each project dependency to a native-ARM64 / building / unsupported / unknown status per Arm AppReady, with citation links. Load during wos-analyzer Phase 2 (dependency audit) and when writing the Phase 8 Arm AppReady Status section. Also emits the ARM64-native vs emulated-x64 vs blocking three-way classification Arm's guidance recommends."
---

# Windows on Arm Ecosystem Dashboard lookup

> **Snapshot date:** 2026-07-09
> **Freshness policy:** the curated tables below reflect the Windows on Arm ecosystem as of the snapshot date. If **more than 6 months** have passed since that date, treat any package status as advisory only and re-verify against the live [Arm Windows on Arm Software Dashboard](https://developer.arm.com/ecosystem-dashboard/windows) plus the package manager (`vcpkg search`, `npm view <pkg> cpu`, `pip download --platform win_arm64`, crates.io platform list). Report the snapshot date and any drift in the Phase 8 report so downstream reviewers know which entries were re-checked live vs served from cache.

Reference: [Arm Windows on Arm Software Dashboard](https://developer.arm.com/ecosystem-dashboard/windows) and [Arm AppReady program](https://developer.arm.com/laptops-and-desktops/windows-app-ready).

Every dependency the project imports MUST be classified into one of four dashboard states plus one of three runtime states. The `wos-analyzer` populates the mapping; the `wos-porter` Phase 8 report cites the results.

## Dashboard states (per Arm's dashboard)

| State | Meaning | Action |
|---|---|---|
| **native** | Ships an ARM64 build; commonly used on WoA | Use directly with the ARM64 target selector |
| **building** | Port in progress; may not be stable | Use with a version constraint; document risk in report |
| **unsupported** | Explicitly x64-only per upstream, no ARM64 plan | Blocker â€” needs replacement, emulation, or ARM64EC |
| **unknown** | Not in the dashboard | Probe the manifest / release page; if no ARM64 artifact and no plan, treat as unsupported |

## Runtime classification (Arm three-way split)

| Class | Meaning | When to accept |
|---|---|---|
| **ARM64 native** | Loaded as an `AA64` binary | Always preferred; the goal |
| **Emulated x64** | Loaded as `8664` and run under Windows' x64 emulation | Acceptable for cold-path binaries (installers, one-shot tools) and closed-source libs with no ARM64 build. Never for a hot path. |
| **Blocking** | Kernel driver, HAL-adjacent, or platform code that cannot be emulated | Hard stop â€” either replace, drop the feature on ARM64, or escalate to [Microsoft App Assure](https://learn.microsoft.com/en-us/microsoft-365/business/app-assure) |

## Curated known-status list

Cross-check every dependency against this list first; fall back to the live dashboard if the package isn't listed. Update entries when upstream state changes.

### C / C++ ecosystem

| Package | Manager | Dashboard | Runtime | Notes |
|---|---|---|---|---|
| OpenSSL 3.x | vcpkg (`openssl:arm64-windows`) | native | ARM64 native | 1.1.x is EOL â€” force upgrade |
| zlib | vcpkg | native | ARM64 native | |
| libpng, libjpeg-turbo | vcpkg | native | ARM64 native | |
| Boost 1.83+ | vcpkg | native | ARM64 native | |
| Qt 6.5+ | vcpkg / installer | native | ARM64 native | Qt 5 is EOL for ARM64 Windows |
| ICU 74+ | vcpkg | native | ARM64 native | |
| curl 8+ | vcpkg | native | ARM64 native | |
| ffmpeg 6+ | vcpkg | native | ARM64 native | Guard AVX kernels; NEON path enabled by default |
| Protobuf 25+ | vcpkg | native | ARM64 native | |
| gRPC 1.60+ | vcpkg | native | ARM64 native | |
| Freetype | vcpkg | native | ARM64 native | |
| SDL2 / SDL3 | vcpkg | native | ARM64 native | |
| CUDA / cuDNN | vendor | **unsupported** | Blocking | NVIDIA has no ARM64 Windows build â€” drop or gate |
| Intel MKL / IPP | vendor | **unsupported** | Blocking on ARM64 | Replace with OpenBLAS / Accelerate-equivalent NEON kernel |
| Intel TBB (oneTBB 2021+) | vcpkg | native | ARM64 native | Older TBB versions were x64-only |

### Rust ecosystem

| Crate | Dashboard | Runtime | Notes |
|---|---|---|---|
| `ring` 0.17+ | native | ARM64 native | Earlier versions lacked ARM64 Windows asm |
| `openssl-sys` | native (via vcpkg triplet) | ARM64 native | Set `OPENSSL_DIR` to vcpkg `installed/arm64-windows` |
| `windows-rs`, `winapi` | native | ARM64 native | |
| `mimalloc-sys`, `tikv-jemalloc-sys` | native | ARM64 native | |
| `criterion` | native | ARM64 native | Benchmarks work fine on ARM64 |
| `sqlx` (with `sqlite-sys`) | native | ARM64 native | Ensure sqlite bindgen picks ARM64 headers |
| `wasmer`, `wasmtime` | native | ARM64 native | JIT paths use aarch64 cranelift backend |

### Node.js / npm

| Package | Dashboard | Runtime | Notes |
|---|---|---|---|
| Node.js 20+, 22 LTS | native | ARM64 native | Use `node-v22.x.x-win-arm64.msi` |
| Electron 22+ | native | ARM64 native | Older Electron shipped no ARM64 Windows build |
| `sharp` 0.33+ | native | ARM64 native | `libvips` bundled ARM64 |
| `sqlite3` (mapbox) 5.1+ | native | ARM64 native | |
| `bcrypt` 5.1+ | native (build-from-source) | ARM64 native | Older versions failed on ARM64 Windows |
| `node-sass` | **unsupported** | Blocking / replace | Use `sass` (Dart Sass) instead |
| `canvas` (`node-canvas`) | building | Prefer replace | Use `@napi-rs/canvas` on ARM64 |
| `puppeteer` (bundled Chromium) | native (Chromium 128+) | ARM64 native | Older bundles were x64 only |

### Python

| Package | Dashboard | Runtime | Notes |
|---|---|---|---|
| CPython 3.11+ | native | ARM64 native | Use ARM64 installer from python.org |
| numpy 1.26+, 2.x | native | ARM64 native | Older wheels were x64 only |
| scipy 1.13+ | native | ARM64 native | |
| pandas 2.2+ | native | ARM64 native | |
| PyTorch 2.4+ (CPU) | building | Native CPU / no CUDA | GPU inference requires x64 emulation |
| TensorFlow | **unsupported** on Windows ARM64 | Blocking / replace | Consider ONNX Runtime with QNN provider |
| ONNX Runtime 1.18+ | native | ARM64 native | QNN + CoreML EPs on Snapdragon X |
| Pillow 10+ | native | ARM64 native | |
| cryptography 42+ | native | ARM64 native | OpenSSL 3 backend |
| lxml 5+ | native (wheel) | ARM64 native | |

### .NET / managed

| Component | Dashboard | Runtime | Notes |
|---|---|---|---|
| .NET 8, 9 SDK/runtime | native | ARM64 native | Use `-r win-arm64` |
| .NET Framework 4.x | building (limited) | Prefer .NET 8+ | 4.8.1 has partial ARM64 support |
| WPF | building | Prefer .NET 8+ | WPF for ARM64 added in .NET 8 |
| WinForms | native (.NET 8+) | ARM64 native | |
| Xamarin | **unsupported** | Blocking / replace | Migrate to .NET MAUI |

### Kernel / driver code

| Component | Runtime | Notes |
|---|---|---|
| Any `*.sys` (KMDF/WDM) | **Blocking** on ARM64 | Requires ARM64 build + ELAM/HVCI-compatible signing chain |
| Filter drivers, mini-filters | **Blocking** on ARM64 | ARM64 driver signing has stricter requirements |
| Hypervisors / VBS-adjacent code | **Blocking** on ARM64 | HAL differences prevent naive port |

Kernel-mode ARM64 porting is out of scope for `wos-porter` â€” flag every `*.sys` / `*.inf` targeting `NTx86` or `NTamd64` as `Blocking + escalate to App Assure`.

## Lookup workflow

For each dependency `wos-analyzer` finds in a manifest (`vcpkg.json`, `Cargo.toml`, `package.json`, `requirements.txt`, `*.csproj`, `binding.gyp`, `packages.config`, etc.):

1. Look up the exact package name in the tables above.
2. If not found, probe manifest metadata:
   - vcpkg: `vcpkg search <pkg>` + check the port for `supports = "!(uwp | arm)"` (means unsupported on ARM) or `supports = "arm64"` (native).
   - Cargo: `cargo tree -e no-dev` and check crates.io for `aarch64-pc-windows-msvc` in the "Platform" list.
   - npm: `npm view <pkg> os cpu` â€” a `cpu` field missing `arm64` means no prebuilt.
   - PyPI: `pip download --platform win_arm64 --only-binary=:all: <pkg>` â€” no download means no wheel.
3. Classify runtime state:
   - `AA64` binary shipped â†’ ARM64 native
   - Buildable from source with MSVC ARM64 â†’ ARM64 native (mark as build-from-source)
   - Only x64 binary + no source â†’ Emulated x64 (acceptable only for non-hot-path)
   - Kernel / HAL / hypervisor / Intel-only ISA â†’ Blocking

## Output format for `wos-analyzer`

Append this table to the analyzer's Dependencies section:

```markdown
### Windows on Arm Dashboard Classification

| Dependency | Version | Manager | Dashboard status | Runtime class | Citation |
|---|---|---|---|---|---|
| openssl | 3.3.0 | vcpkg | native | ARM64 native | https://developer.arm.com/ecosystem-dashboard/windows |
| sharp | 0.32.6 | npm | building | ARM64 native (rebuild-from-source) | package.json |
| tensorflow | 2.15.0 | pip | unsupported | Blocking | https://www.tensorflow.org/install/pip#windows-native |
| my_kmdf_driver.sys | â€” | in-tree | â€” | **Blocking** (kernel) | escalate: App Assure |
```

## Output format for the Phase 8 `Arm AppReady Status` section

```markdown
- **Target profile**: ARM64-native
- **AppReady stages reached**: Assess âœ“ | Build âœ“ | Optimize âœ“
- **WoA Ecosystem Dashboard status**: 12 native, 1 building (sharp 0.32.6 â†’ rebuild), 0 unsupported, 0 unknown
- **Emulation vs native**: 13/13 dependencies loaded as ARM64 native; 0 emulated; 0 blocking
- **Blockers requiring Microsoft App Assure**: None
```

If any Blocking row exists, the Phase 8 gate G4b (add to G4 family) fails and the porter must record it in the `Limitations & Known Issues` section with the App Assure escape-hatch link.
