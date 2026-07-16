---
description: "ARM64 build error diagnosis and resolution patterns for Windows. Use when: fixing ARM64 compilation errors, resolving linker errors for ARM64, debugging dumpbin validation failures, troubleshooting ARM64 test failures, resolving platform configuration issues in Visual Studio projects."
applyTo: "**/{build,build-arm64,out}/**/*.{log,err,txt},**/*.{vcxproj,sln,props,targets},**/CMakeLists.txt,**/CMakeCache.txt"
---

# ARM64 Build Error Resolution Reference

## Common Build Errors and Fixes

### Compiler Errors (C-series)

#### C1083: Cannot open include file
| Header | Cause | Fix |
|--------|-------|-----|
| `xmmintrin.h`, `emmintrin.h`, `immintrin.h`, `intrin.h` (x86 SIMD) | x86 header not available on ARM64 | Wrap in `#if defined(_M_IX86) \|\| defined(_M_X64)` |
| `arm_acle.h` | Not shipped with MSVC | Guard with `#if !defined(_MSC_VER)` or provide defines manually |
| `arm_neon.h` | Usually available on ARM64 MSVC | Verify VS ARM64 workload is installed |

#### C2065 / C3861: Undeclared identifier
| Identifier Pattern | Cause | Fix |
|---|---|---|
| `_mm_*`, `_mm256_*`, `_mm512_*` | SSE/AVX intrinsic on ARM64 | Guard with `#if defined(_M_IX86) \|\| defined(_M_X64)` |
| `__m128`, `__m128i`, `__m128d` | SSE types on ARM64 | Same arch guard |
| `__cpuid`, `__cpuidex` | x86 CPU detection | Guard; use `IsProcessorFeaturePresent()` on ARM64 |
| `_xgetbv` | x86 XSAVE feature | Guard with x86 arch check |
| `__rdtsc`, `__rdtscp` | x86 timestamp counter | Use `QueryPerformanceCounter()` on ARM64 |
| `_BitScanForward64`, `_BitScanReverse64` | Available on ARM64 too | These work on ARM64 — check include `<intrin.h>` |
| `_umul128`, `__umulh` | `_umul128` is x64-only | Use `__umulh` on ARM64 (available in MSVC) |

#### C2059: Syntax error with `__asm`
- **Cause**: MSVC inline assembly is x86-32 only (not x64, not ARM64)
- **Fix**: Wrap in `#if defined(_M_IX86)` — NOT `_M_X64` (it doesn't work there either)
- **Alternative**: Replace with compiler intrinsics or C/C++ equivalent

#### D8016: Conflicting compiler options
| Conflict | Fix |
|---|---|
| `/arch:SSE2` with ARM64 | Remove for ARM64 platform (NEON is implicit) |
| `/arch:AVX*` with ARM64 | Remove for ARM64 platform |
| `/favor:INTEL64` or `/favor:AMD64` | Remove for ARM64 platform |

### Linker Errors (LNK-series)

#### LNK1112: module machine type 'x64' conflicts with target machine type 'ARM64'
- **Cause**: An x64 .lib is being linked into an ARM64 build
- **Fix**: 
  1. Identify which .lib is x64: check `AdditionalDependencies` and library paths
  2. Build the dependency for ARM64, or find ARM64 version
  3. Fix library search paths: replace hardcoded `x64` paths with `$(Platform)` or ARM64 paths

#### LNK2019: unresolved external symbol
- **Cause**: Source file not included in ARM64 build, or ARM64 code path missing implementation
- **Fix**:
  1. Check if the .cpp file is excluded from ARM64 configuration
  2. Check if function is behind `#ifdef _M_X64` without ARM64 alternative
  3. Add the source to the ARM64 build or provide ARM64 implementation

#### LNK1246: '/DYNAMICBASE:NO' not compatible with ARM64
- **Cause**: ARM64 Windows requires ASLR (Address Space Layout Randomization)
- **Fix**: Set `<RandomizedBaseAddress>true</RandomizedBaseAddress>` for ARM64, or remove `/DYNAMICBASE:NO`

#### LNK1104: cannot open file '<name>.lib'
- **Cause**: ARM64 library not at expected path
- **Fix**: Check if path uses `$(Platform)` macro or hardcoded architecture; update for ARM64

### Platform Configuration Errors

#### "Platform 'ARM64' not found in solution"
**Fix for .sln files**: Add ARM64 platform entries by duplicating x64 entries:
```
GlobalSection(SolutionConfigurationPlatforms) = preSolution
    Debug|ARM64 = Debug|ARM64
    Release|ARM64 = Release|ARM64
EndGlobalSection
```

**Fix for .vcxproj files**: Add ARM64 PropertyGroup and ItemDefinitionGroup by copying from x64:
```xml
<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|ARM64'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <PlatformToolset>v143</PlatformToolset>
</PropertyGroup>
```

## dumpbin Validation Reference

### Machine Type Codes
| Code | Architecture | Expected for ARM64? |
|------|---|---|
| `AA64` | ARM64 (AArch64) | YES |
| `8664` | x64 (AMD64) | NO — wrong architecture |
| `14C` | x86 (i386) | NO — wrong architecture |
| `1C0` | ARM (32-bit) | NO — wrong (need 64-bit) |

### dumpbin Commands
```powershell
# Check machine type
dumpbin /HEADERS <binary> | Select-String "machine \("

# Check imports (find DLL dependencies)
dumpbin /IMPORTS <binary> | Select-String "\.dll"

# Check exports (verify symbols)
dumpbin /EXPORTS <binary> | Select-String "ordinal"

# Quick architecture check for multiple files
Get-ChildItem -Recurse -Include *.exe, *.dll | ForEach-Object {
    $m = & dumpbin /HEADERS $_.FullName 2>&1 | Select-String "machine \("
    "$($_.Name): $m"
}
```

### Common dumpbin Issues
| Issue | Cause | Fix |
|---|---|---|
| Shows `8664 (x64)` instead of `AA64` | Built with wrong toolchain | Verify Platform=ARM64 in build command |
| Mixed architectures in output | Some projects built x64 | Check each .vcxproj has ARM64 platform |
| "not a valid Win32 application" | Binary is for different arch | Rebuild with correct target |
| dumpbin shows nothing | File is not a PE binary | Skip non-PE files (.lib may be COFF) |

## Test Failure Patterns on ARM64

### Runtime Failures
| Error | Root Cause | Resolution |
|---|---|---|
| `STATUS_ILLEGAL_INSTRUCTION` (0xC000001D) | Executing x86 instructions | Find unguarded x86 code path and add `#ifdef` |
| `STATUS_DLL_NOT_FOUND` | Missing ARM64 DLL | Copy DLL to exe directory or fix PATH |
| `STATUS_ACCESS_VIOLATION` in SIMD | Alignment or wrong SIMD path | Check 16-byte alignment; verify ARM64 NEON path |
| Incorrect numeric results | Different floating-point behavior | ARM64 uses fused multiply-add by default; adjust tolerance |
| Deadlock or timeout | Memory ordering (weak model) | Add `MemoryBarrier()` or use `std::atomic` with proper ordering |
| Assertion failure with byte order | Endianness assumption | Both are little-endian on Windows — check actual data |

### Memory Model Differences
ARM64 has a **weak memory model** vs x64's **strong (TSO) model**:
- Stores can be reordered with other stores
- Loads can be reordered with other loads
- Use `std::atomic` with `memory_order_seq_cst` for safety
- Use `MemoryBarrier()` / `_ReadWriteBarrier()` for explicit fences
- `volatile` alone does NOT guarantee ordering on ARM64

### Floating Point Differences
- ARM64 NEON uses IEEE 754 but with fused multiply-add (FMA) by default
- Results may differ in last bits compared to x64 SSE
- Use `/fp:strict` if exact x64 parity is required (slower)
- Prefer relaxing test tolerances: `abs(actual - expected) < epsilon`
