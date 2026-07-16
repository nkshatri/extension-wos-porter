---
name: wos-build-error-recipes
description: "ARM64 build/link error diagnosis and fix recipes for Windows (C-series compiler errors, LNK linker errors, D80xx conflicts, .sln/.vcxproj platform mis-configuration, dumpbin AA64 validation). Load when triaging an ARM64 build log line-by-line â€” do NOT load for general porting questions."
---

# ARM64 Build Error Recipes

## Compiler errors

### C1083 â€” Cannot open include file
| Header | Cause | Fix |
|---|---|---|
| `xmmintrin.h`, `emmintrin.h`, `immintrin.h`, `intrin.h` (x86 SIMD) | x86 header not on ARM64 | Wrap include in `#if defined(_M_IX86) \|\| defined(_M_X64)` |
| `arm_acle.h` | Missing in old MSVC | Guard with `#if !defined(_MSC_VER)` or manually define needed macros |
| `arm_neon.h` | Usually present on ARM64 MSVC | Verify VS ARM64 workload is installed |

### C2065 / C3861 â€” Undeclared identifier
| Identifier | Cause | Fix |
|---|---|---|
| `_mm_*`, `_mm256_*`, `_mm512_*` | SSE/AVX intrinsic on ARM64 | Guard with `#if defined(_M_IX86) \|\| defined(_M_X64)` |
| `__m128`, `__m128i`, `__m128d` | SSE types on ARM64 | Same arch guard |
| `__cpuid`, `__cpuidex` | x86 CPU detection | Guard; use `IsProcessorFeaturePresent()` on ARM64 |
| `_xgetbv` | x86 XSAVE feature | Guard with x86 arch check |
| `__rdtsc`, `__rdtscp` | x86 TSC | Use `QueryPerformanceCounter()` on ARM64 |
| `_BitScanForward64`, `_BitScanReverse64` | Available on ARM64 too | Ensure `<intrin.h>` is included |
| `_umul128` | x64-only | Use `__umulh` on ARM64 |

### C2059 â€” Syntax error with `__asm`
MSVC inline asm is x86-32 only (not x64, not ARM64). Wrap in `#if defined(_M_IX86)` and provide a compiler-intrinsic / C fallback for other targets.

### D8016 â€” Conflicting compiler options
| Conflict | Fix |
|---|---|
| `/arch:SSE2` with ARM64 | Remove for ARM64 (NEON implicit) |
| `/arch:AVX*` with ARM64 | Remove for ARM64 |
| `/favor:INTEL64` or `/favor:AMD64` with ARM64 | Remove for ARM64 |

## Linker errors

### LNK1112 â€” module machine type 'x64' conflicts with target 'ARM64'
An x64 `.lib` is being linked into an ARM64 build. Identify via `AdditionalDependencies` and library paths; rebuild the dep for ARM64 or use `dumpbin /HEADERS` on each candidate to confirm.

### LNK2019 â€” unresolved external symbol
Common causes: source file excluded from ARM64 config, or function behind `#ifdef _M_X64` with no ARM64 alternative. Ensure the ARM64 impl (or scalar fallback) is compiled in.

### LNK1246 â€” '/DYNAMICBASE:NO' not compatible with ARM64
ARM64 Windows requires ASLR. Set `<RandomizedBaseAddress>true</RandomizedBaseAddress>` or remove `/DYNAMICBASE:NO`.

### LNK1104 â€” cannot open file '<name>.lib'
Library path likely hardcodes `x64`. Replace with `$(Platform)` macro or an ARM64 path.

## Platform configuration

### "Platform 'ARM64' not found in solution"
`.sln`:
```
GlobalSection(SolutionConfigurationPlatforms) = preSolution
    Debug|ARM64 = Debug|ARM64
    Release|ARM64 = Release|ARM64
EndGlobalSection
```

`.vcxproj` â€” duplicate x64 PropertyGroup / ItemDefinitionGroup for ARM64:
```xml
<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|ARM64'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <PlatformToolset>v143</PlatformToolset>
</PropertyGroup>
```

## dumpbin validation

Machine type codes:

| Code | Arch | Expected? |
|---|---|---|
| `AA64` | ARM64 (AArch64) | YES |
| `8664` | x64 (AMD64) | NO |
| `14C` | x86 (i386) | NO |
| `1C0` | ARM (32-bit) | NO |

```powershell
# Machine type
dumpbin /HEADERS <binary> | Select-String "machine \("

# Bulk check
Get-ChildItem -Recurse -Include *.exe,*.dll | ForEach-Object {
    $m = & dumpbin /HEADERS $_.FullName 2>&1 | Select-String "machine \("
    "$($_.Name): $m"
}
```

Common dumpbin issues:

| Symptom | Cause | Fix |
|---|---|---|
| Shows `8664 (x64)` instead of `AA64` | Wrong toolchain | Verify `Platform=ARM64` in build command |
| Mixed archs across outputs | Some projects not switched | Check each `.vcxproj` has ARM64 platform |
| "not a valid Win32 application" | Wrong-arch binary | Rebuild with correct target |
