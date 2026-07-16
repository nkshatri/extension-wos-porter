# Preprocessor Guard Patterns for x86/ARM64 Cross-Platform Code

Complete reference for `#if`/`#ifdef` guard patterns in the Microsoft STL's
vectorized algorithm layer, covering ARM64, ARM64EC, x64, and x86.

## Table of Contents
1. [The Core Guard Pattern](#the-core-guard-pattern)
2. [ARM64EC: The Most Common Mistake](#arm64ec-the-most-common-mistake)
3. [All Guard Variants and Their Meanings](#all-guard-variants-and-their-meanings)
4. [The _VECTORIZED_* Macro Tier System](#the-_vectorized_-macro-tier-system)
5. [ABI Shim Guards](#abi-shim-guards)
6. [Header Include Guards](#header-include-guards)
7. [Checklist for New Code](#checklist-for-new-code)

---

## The Core Guard Pattern

The fundamental split in `vector_algorithms.cpp`:

```cpp
#if defined(_M_ARM64) || defined(_M_ARM64EC)
    // NEON path: arm64_neon.h + Windows.h
    // Uses: vld1q_u8, vst1q_u8, vceqq_u8, vmaxvq_u8, etc.
    // Feature detection: IsProcessorFeaturePresent(PF_ARM_*)
#else
    // x64/x86 path: intrin.h + isa_availability.h
    // Uses: _mm_loadu_si128, _mm256_loadu_si256, etc.
    // Feature detection: __isa_enabled & (1 << __ISA_AVAILABLE_*)
#endif
```

The `#else` branch covers: x64 (`_M_X64`), x86 (`_M_IX86`), and any future
non-ARM64 targets. It does NOT cover ARM64EC.

---

## ARM64EC: The Most Common Mistake

ARM64EC (`_M_ARM64EC`) is an ARM64 binary with an x64-compatible ABI. It uses
**NEON intrinsics**, not SSE/AVX. Forgetting to include it in ARM64 guards is
the single most common porting mistake.

### Wrong patterns and their fixes:

```cpp
// WRONG 1: ARM64EC falls into x64 path
#ifdef _M_ARM64
    // NEON code
#else
    // x64 code  <-- ARM64EC incorrectly ends up here
#endif

// WRONG 2: Same problem with #if
#if defined(_M_ARM64)
    // NEON code
#else
    // x64 code  <-- ARM64EC incorrectly ends up here
#endif

// WRONG 3: Negation also wrong
#ifndef _M_ARM64
    // x64 code  <-- ARM64EC incorrectly ends up here
#endif

// CORRECT: Always pair _M_ARM64 with _M_ARM64EC
#if defined(_M_ARM64) || defined(_M_ARM64EC)
    // NEON code
#else
    // x64 code
#endif

// CORRECT: Negation
#if !defined(_M_ARM64) && !defined(_M_ARM64EC)
    // x64-only code
#endif
```

### When `#ifndef _M_ARM64` alone IS correct

There is exactly one case in the STL where `#ifndef _M_ARM64` (without `_M_ARM64EC`)
is intentionally correct — the legacy ABI shim:

```cpp
// INTENTIONALLY excludes only pure ARM64 (not ARM64EC):
#ifndef _M_ARM64
// TRANSITION, ABI: __std_swap_ranges_trivially_swappable() is preserved
// for binary compatibility (x64/x86/ARM64EC)
void* __cdecl __std_swap_ranges_trivially_swappable(...) noexcept { ... }
#endif
```

Reason: Pure ARM64 (`_M_ARM64`) **never** exported this symbol, so it doesn't need
the shim. ARM64EC **did** export it (as an x64-ABI binary), so it must keep it.
This is the only legitimate use of `#ifndef _M_ARM64` without `_M_ARM64EC`.

---

## All Guard Variants and Their Meanings

| Guard | ARM64 | ARM64EC | x64 | x86 | Use case |
|---|---|---|---|---|---|
| `defined(_M_ARM64) \|\| defined(_M_ARM64EC)` | ✓ | ✓ | ✗ | ✗ | All NEON code |
| `!defined(_M_ARM64) && !defined(_M_ARM64EC)` | ✗ | ✗ | ✓ | ✓ | All SSE/AVX code |
| `defined(_M_ARM64)` | ✓ | ✗ | ✗ | ✗ | ARM64-only (rare; avoid unless intentional) |
| `defined(_M_ARM64EC)` | ✗ | ✓ | ✗ | ✗ | ARM64EC-only (very rare) |
| `!defined(_M_ARM64)` | ✗ | ✓ | ✓ | ✓ | x64 + ARM64EC (ABI shim only) |
| `defined(_M_X64) \|\| defined(_M_IX86)` | ✗ | ✗ | ✓ | ✓ | Explicit x64/x86 only |
| `defined(_WIN64)` | ✓ | ✓ | ✓ | ✗ | 64-bit Windows (all 64-bit targets) |

---

## The `_VECTORIZED_*` Macro Tier System

Defined in `stl/inc/xutility`, these macros gate which algorithms are vectorized
per target. They are set based on the architecture macros above.

### Tier Definitions

```cpp
#if !_USE_STD_VECTOR_ALGORITHMS
    // All disabled
    #define _VECTORIZED_FOR_X64_X86             0
    #define _VECTORIZED_FOR_X64_X86_ARM64       0
    #define _VECTORIZED_FOR_X64_X86_ARM64_ARM64EC 0

#elif defined(_M_ARM64) || defined(_M_ARM64EC)
    #define _VECTORIZED_FOR_X64_X86             0
    #define _VECTORIZED_FOR_X64_X86_ARM64       1  // ARM64 + ARM64EC both get this
    #define _VECTORIZED_FOR_X64_X86_ARM64_ARM64EC 1

#elif defined(_M_X64) || defined(_M_IX86)
    #define _VECTORIZED_FOR_X64_X86             1
    #define _VECTORIZED_FOR_X64_X86_ARM64       1
    #define _VECTORIZED_FOR_X64_X86_ARM64_ARM64EC 1
```

### Algorithm-to-Tier Mapping

```
Tier: _VECTORIZED_FOR_X64_X86_ARM64_ARM64EC (ARM64 + ARM64EC + x64/x86)
  adjacent_find, count, find, find_last, includes, is_sorted_until,
  minmax, minmax_element, mismatch, reverse, reverse_copy, rotate, swap_ranges

Tier: _VECTORIZED_FOR_X64_X86 (x64/x86 only — ARM64 not yet implemented)
  bitset_from_string, bitset_to_string, find_end, find_first_of, find_last_of,
  remove, remove_copy, replace, replace_copy, search, search_n, unique, unique_copy
```

### When Adding a New Vectorized Algorithm

- If implementing ARM64 NEON now: use `_VECTORIZED_FOR_X64_X86_ARM64_ARM64EC`
- If deferring ARM64: use `_VECTORIZED_FOR_X64_X86`
- Never use `_VECTORIZED_FOR_X64_X86_ARM64` as a differentiator — after PR #6084,
  ARM64 and ARM64EC always have the same value, making this tier redundant

---

## ABI Shim Guards

Some functions have legacy exports for binary compatibility. The guard logic is
intentionally asymmetric:

### `__std_swap_ranges_trivially_swappable` (no `_noalias`)

```cpp
#ifndef _M_ARM64
// Preserved for x64/x86/ARM64EC binary compatibility
// Pure ARM64 never had this export, so no shim needed
void* __cdecl __std_swap_ranges_trivially_swappable(...) noexcept {
    __std_swap_ranges_trivially_swappable_noalias(...);
    return ...;
}
#endif
```

### `__std_min_element_8` / `__std_max_element_8`

```cpp
// 64-bit integer min/max element NOT declared on ARM64/ARM64EC:
#if !defined(_M_ARM64) && !defined(_M_ARM64EC)
const void* __stdcall __std_min_element_8(...) noexcept;
const void* __stdcall __std_max_element_8(...) noexcept;
#endif
```

---

## Header Include Guards

```cpp
// ARM64/ARM64EC path:
#if defined(_M_ARM64) || defined(_M_ARM64EC)
#include <arm64_neon.h>   // NEON intrinsics
#include <Windows.h>      // IsProcessorFeaturePresent

// x64/x86 path:
#else
#include <intrin.h>           // SSE/AVX intrinsics
#include <isa_availability.h> // __isa_enabled, __ISA_AVAILABLE_*
extern "C" long __isa_enabled;
#endif
```

---

## Checklist for New Code

When writing or reviewing cross-platform vectorized code, verify:

- [ ] All NEON code is inside `#if defined(_M_ARM64) || defined(_M_ARM64EC)`
- [ ] All SSE/AVX code is inside `#if !defined(_M_ARM64) && !defined(_M_ARM64EC)`
      (or equivalently `#else` after the ARM64 block)
- [ ] No bare `#ifdef _M_ARM64` without `_M_ARM64EC` (unless it's the ABI shim)
- [ ] No bare `#ifndef _M_ARM64` without `&& !defined(_M_ARM64EC)` (unless ABI shim)
- [ ] `_VECTORIZED_*` macro uses `_ARM64_ARM64EC` tier (not the now-redundant `_ARM64`)
- [ ] 64-bit integer min/max functions excluded on ARM64/ARM64EC
- [ ] `__isa_enabled` not referenced in ARM64 code
- [ ] `_Zeroupper_on_exit` not present in ARM64 code
- [ ] `_Avx2_tail_mask_32` not present in ARM64 code
- [ ] Feature detection uses `IsProcessorFeaturePresent(PF_ARM_*)` not `__isa_enabled`
- [ ] 4-byte tail uses `vld1_lane_u32` / `vst1_lane_u32`, not direct pointer dereference
