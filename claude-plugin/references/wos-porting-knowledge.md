---
description: "ARM64 porting knowledge reference for Windows. Use when: porting x64 code to ARM64, translating SSE/AVX intrinsics to NEON, understanding Windows ARM64 specifics, ARM64EC considerations."
---

# ARM64 Porting Knowledge Reference

## Windows ARM64 Platform Overview

### Architecture Differences: x64 vs ARM64

| Feature | x64 (AMD64) | ARM64 (AArch64) |
|---|---|---|
| Endianness | Little-endian | Little-endian (Windows) |
| Pointer size | 8 bytes | 8 bytes |
| Page size | 4 KB | 4 KB (Windows), 16 KB (Linux) |
| Cache line | 64 bytes | 64 bytes (typical, varies) |
| SIMD registers | 16 x 128-bit (XMM), 16 x 256-bit (YMM), 32 x 512-bit (ZMM) | 32 x 128-bit (V0–V31 NEON) |
| SIMD width | 128/256/512-bit | 128-bit (NEON), scalable (SVE/SVE2) |
| Memory model | Strong (TSO) | Weak (requires explicit barriers) |
| Unaligned access | Fast | Supported but may be slower |
| Hardware breakpoints | 4 | 16 |

### Windows ARM64 Specifics

- **Compiler**: MSVC (`cl.exe`) supports ARM64 natively. Use with `vcvarsarm64.bat` or `vcvarsamd64_arm64.bat` for cross-compilation.
- **Macro**: MSVC defines `_M_ARM64` and `_WIN64`. GCC/Clang define `__aarch64__`.
- **NEON is always available**: On Windows ARM64, NEON SIMD is guaranteed. No runtime feature check needed.
- **Calling convention**: ARM64 uses AAPCS64. `__vectorcall` is NOT supported. `__fastcall` is silently ignored.
- **Exception handling**: SEH works on ARM64, but exception context structure (`CONTEXT`) differs.
- **WoW64**: ARM64 Windows can run x86 apps via emulation and x64 apps via hybrid emulation. Native ARM64 provides best performance.
- **ARM64EC**: "Emulation Compatible" — allows mixing ARM64 and emulated x64 code in the same process. Useful for gradual migration of large projects.

### MSVC ARM64 Compiler Flags

| Flag | Purpose |
|---|---|
| `/arch:armv8.0` | Target ARMv8.0 (default) |
| `/arch:armv8.1` | Target ARMv8.1 (adds atomics, CRC) |
| `/D_ARM64_` | Define ARM64 macro (usually auto-defined) |
| `/favor:` | Not applicable for ARM64 (x64 only) |

Flags to **remove** when building for ARM64:
- `/arch:SSE`, `/arch:SSE2`, `/arch:AVX`, `/arch:AVX2`, `/arch:AVX-512`
- `/favor:INTEL64`, `/favor:AMD64`

## SSE / AVX → NEON translation

**Policy: no translation-shim libraries** (no `sse2neon.h`, `simde`, `xsimd`, `highway`, etc.). Every NEON instruction must be hand-written from `<arm_neon.h>` / `<arm_acle.h>` (C/C++), `core::arch::aarch64` (Rust), or `System.Runtime.Intrinsics.Arm.*` (.NET).

For the intrinsic mapping tables (SSE/SSE2 float + integer, Windows ARM64 baseline ISA including AES / SHA1 / SHA2 / PMULL / CRC32, ARMv8.2+ optional features), load the [wos-neon-reference](../skills/wos-neon-reference/SKILL.md) skill on demand.

## Memory Ordering on ARM64

ARM64 has a **weak memory model** (unlike x64's TSO). This matters when porting lock-free code:

| x64 Behavior | ARM64 Requirement |
|---|---|
| All loads have acquire semantics | Need explicit `ldar` or `dmb ishld` |
| All stores have release semantics | Need explicit `stlr` or `dmb ishst` |
| `mfence` | `dmb ish` |
| `lfence` | `dmb ishld` |
| `sfence` | `dmb ishst` |

**In C/C++**: Use `<atomic>` or `_Interlocked*` functions — they generate correct barriers on both architectures. Only need manual barriers when using raw assembly or compiler intrinsics.

```c
/* Correct on both x64 and ARM64: */
#include <stdatomic.h>
atomic_store_explicit(&flag, 1, memory_order_release);
int val = atomic_load_explicit(&flag, memory_order_acquire);
```

## Processor Feature Detection on ARM64 Windows

Replace CPUID-based feature detection with the Windows API. For the full baseline ISA / optional-feature table, see the [wos-neon-reference](../skills/wos-neon-reference/SKILL.md) skill; short form:

```c
#include <windows.h>
bool has_neon    = IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE);     // always true
bool has_crc32   = IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE); // baseline on WoA
bool has_crypto  = IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE);// baseline on WoA
bool has_atomics = IsProcessorFeaturePresent(PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE);
bool has_dp      = IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE);   // optional
```

## Common Porting Pitfalls

1. **`_mm_movemask_ps` / `_mm_movemask_epi8`** — no direct NEON equivalent; hand-code with `vshrn` + manual bit assembly. Profile if performance-critical.
2. **`_mm_shuffle_ps` with a compile-time constant** — NEON shuffle is more restrictive; expect multi-instruction sequences using `vextq` / `vzip` / `vuzp` / `vtbl`.
3. **Horizontal ops** (`_mm_hadd_ps`, etc.) — NEON has `vpaddq` but semantics differ.
4. **`__rdtsc` for timing** — use `QueryPerformanceCounter` for portable high-res timing (ARM64's `cntvct_el0` ticks at a different frequency).
5. **Thread-local storage** — TLS works on ARM64 Windows but internal layout differs. Don't hardcode `gs:` segment register offsets.
6. **Stack alignment** — ARM64 requires 16-byte alignment in hand-written assembly.
7. **Structured Exception Handling** — SEH works on ARM64 but `CONTEXT` fields differ (`X0`–`X28`, `Fp`, `Lr`, `Sp`, `Pc`, not `Rax`/`Rbx`/…).
8. **128-bit atomics** — use `_InterlockedCompareExchange128` (maps to `cmpxchg16b` on x64, `ldxp`/`stxp` pair on ARM64).

## ARM64EC (Emulation Compatible)

ARM64EC allows an ARM64 binary to call into x64 code (running under emulation) and vice versa. Useful for:

- Large codebases that can't be fully ported at once
- Dependencies that only ship x64 binaries
- Gradual migration strategy

To build with ARM64EC in MSVC:
```
cl /arm64EC source.c
```

CMake:
```cmake
set(CMAKE_GENERATOR_PLATFORM ARM64EC)
```

MSBuild:
```xml
<Platform>ARM64EC</Platform>
```

**Note**: ARM64EC is not full native ARM64 — there's a small performance overhead for x64 ↔ ARM64 transitions. Prefer full native ARM64 porting when possible.

**JIT / dynamic code generators on ARM64EC**: if the project JIT-compiles executable code (V8, JSC, Wasm engines, .NET RyuJIT, custom trampolines) and targets ARM64EC, the JIT's page allocation MUST use `VirtualAlloc2` with `MEM_EXTENDED_PARAMETER_EC_CODE` — a plain `VirtualAlloc(..., PAGE_EXECUTE_READWRITE)` allocation is misclassified by the hybrid loader as x64 code and either faults or gets emulated. Load the [jit-arm64ec-virtualalloc-fix-skill](../skills/jit-arm64ec-virtualalloc-fix-skill/SKILL.md) skill when analysing / fixing any `VirtualAlloc*` call site that flows into `WriteProcessMemory`, JIT code caches, or executable heap slabs on an ARM64EC target.

## vcpkg ARM64 Support

Most vcpkg packages support ARM64 Windows:

```bash
# Install for ARM64 Windows
vcpkg install <package>:arm64-windows

# Available triplets
# arm64-windows       (dynamic)
# arm64-windows-static (static)
# arm64-windows-static-md (static libs, dynamic CRT)
```

To set default triplet in CMake:
```cmake
set(VCPKG_TARGET_TRIPLET "arm64-windows" CACHE STRING "")
```

## Testing ARM64 Builds

### On x64 Host (Cross-Compilation)

1. Install MSVC ARM64 build tools via Visual Studio Installer
2. Open "ARM64 Cross Tools Command Prompt" or run `vcvarsamd64_arm64.bat`
3. Build normally — the compiler will produce ARM64 binaries
4. Test using: ARM64 Windows VM, physical ARM64 device, or Windows ARM64 emulator

### CMake Cross-Compile Example

```bash
cmake -B build-arm64 -G "Visual Studio 17 2022" -A ARM64
cmake --build build-arm64 --config Release
```

### Build System ARM64 Quick Reference

| Build System | ARM64 Cross-Compile Command / Setting |
|---|---|
| CMake | `cmake -B build -G "Visual Studio 17 2022" -A ARM64` |
| MSBuild | `msbuild /p:Platform=ARM64 /p:Configuration=Release` |
| Meson | `meson setup build --cross-file cross/arm64-windows.txt` |
| Make (GCC) | `make ARCH=arm64 CC=aarch64-w64-mingw32-gcc` |
| NMake (MSVC) | Open "ARM64 Cross Tools Command Prompt", then `nmake` |
| Cargo | `cargo build --target aarch64-pc-windows-msvc` |
| Autotools | `./configure --host=aarch64-w64-mingw32` |
| Bazel | `bazel build --config=arm64 //...` |
| GN | `gn gen out/arm64 --args='target_cpu="arm64" target_os="win"'` then `ninja -C out/arm64` |
| Premake | `premake5 vs2022` then build with `Platform=ARM64` in VS |
| SCons | `scons arch=arm64` |
| Waf | `python waf configure --arch=arm64 && python waf build` |
| qmake | `qmake -spec win32-arm64-msvc2022` |
| xmake | `xmake f -a arm64 -p windows && xmake` |
| B2/Boost.Build | `b2 toolset=msvc architecture=arm address-model=64` |
| Go | `set GOARCH=arm64& set GOOS=windows& go build` |
| node-gyp | `node-gyp rebuild --arch=arm64` |
| .NET SDK | `dotnet publish -r win-arm64 -c Release` |
| Gradle (JNI) | Use CMake/MSBuild inside Gradle with `-A ARM64` |
| Python (ext) | Build on ARM64 host or cross-compile wheel with cibuildwheel |

### MSVC Developer Command Prompts for ARM64

| Prompt | Host → Target | Environment Script |
|---|---|---|
| Native ARM64 | ARM64 → ARM64 | `vcvarsarm64.bat` |
| x64 cross-compile | x64 → ARM64 | `vcvarsamd64_arm64.bat` |
| x86 cross-compile | x86 → ARM64 | `vcvarsx86_arm64.bat` |

### Verifying Binary Architecture

```powershell
# Check if binary is ARM64
dumpbin /headers build-arm64\Release\app.exe | findstr "machine"
# Should show: AA64 machine (ARM64)
```
