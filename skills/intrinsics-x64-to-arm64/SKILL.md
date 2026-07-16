---
name: intrinsics-x64-to-arm64
description: >-
  Expert guidance for porting x86/x64 SSE/AVX vectorized C++ to ARM64 NEON,
  grounded in the Microsoft STL's two real backends (vector_algorithms.cpp).
  Use this skill when: translating __m128i/__m256i intrinsics to NEON
  (uint8x16_t/uint16x8_t/etc.), replacing _mm_*/_mm256_* with
  vld1q/vst1q/vceqq/vmaxvq/vminq, converting AVX2 tail-mask patterns
  (_Avx2_tail_mask_32) to ARM64 descending-granularity loops, replacing
  __isa_enabled with IsProcessorFeaturePresent, removing _Zeroupper_on_exit,
  handling 64-bit int min/max exclusion on ARM64, fixing _M_ARM64-only guards
  that miss _M_ARM64EC, auditing #ifdef guards for cross-arch correctness, or
  any x86-to-ARM64 SIMD migration in Windows C++ codebases. Also trigger for
  NEON equivalents of SSE/AVX ops, or why an x64 pattern cannot port to ARM64.
---

# x86/x64 to ARM64 Vectorized Code Migration

Practical porting guide derived from the Microsoft STL's two parallel SIMD backends.
Every pattern here is grounded in real production code from `vector_algorithms.cpp`.

## The Two-Backend Mental Model

The STL's vectorized layer is the clearest real-world example of this migration:

```
x64/x86 backend                    ARM64/ARM64EC backend
─────────────────────────────────  ──────────────────────────────────
#include <intrin.h>                #include <arm64_neon.h>
#include <isa_availability.h>      #include <Windows.h>
extern "C" long __isa_enabled;

_Use_avx2()  via __isa_enabled     _Use_FEAT_SVE() via IsProcessorFeaturePresent
_Use_sse42() via __isa_enabled     (baseline NEON needs no runtime check)

__m256i (256-bit)                  uint8x16_t x4 (4 x 128-bit = 64 bytes/iter)
__m128i (128-bit)                  uint8x16_t (128-bit)

_mm256_loadu_si256()               vld1q_u8() x2 or x4
_mm256_storeu_si256()              vst1q_u8() x2 or x4

_Avx2_tail_mask_32() + masked op   descending-granularity if-chain
_Zeroupper_on_exit RAII guard      (not needed — no upper-half contamination)
```

The key insight: **ARM64 NEON is always 128-bit wide**. There is no 256-bit NEON.
To match AVX2 throughput, use 4 x 128-bit registers per iteration (64 bytes/iter).

---

## Migration Workflow

When porting an x64 vectorized function to ARM64, follow these steps in order:

1. **Identify the x64 ISA tier** — SSE4.2 only, or AVX2 + SSE4.2 fallback?
2. **Map register widths** — 256-bit AVX2 → 2x128-bit NEON; 128-bit SSE → 1x128-bit NEON
3. **Replace the loop structure** — AVX2 tail mask → descending-granularity if-chain
4. **Translate intrinsics** — use the mapping tables in `references/intrinsic-mapping.md`
5. **Replace feature detection** — `__isa_enabled` → `IsProcessorFeaturePresent`
6. **Remove x64-only constructs** — `_Zeroupper_on_exit`, `_Avx2_tail_mask_32`
7. **Handle ARM64 limitations** — 64-bit int min/max, no horizontal 64-bit ops
8. **Fix preprocessor guards** — `_M_ARM64` alone → `_M_ARM64 || _M_ARM64EC`
9. **Wrap in the correct `#if` block** — see Guard Patterns below

For the intrinsic translation tables, see `references/intrinsic-mapping.md`.
For ARM64-specific limitations and workarounds, see `references/arm64-limitations.md`.
For guard pattern rules and ABI notes, see `references/guard-patterns.md`.

---

## Loop Structure Migration

This is the most structurally significant change. x64 uses a **masked tail** approach;
ARM64 uses a **descending-granularity** approach.

### x64 Pattern: AVX2 + SSE4.2 with masked tail

```cpp
// x64: two ISA tiers, masked tail for AVX2
constexpr size_t _Mask_32 = ~((size_t{1} << 5) - 1);
if (_Byte_length(_First, _Last) >= 32 && _Use_avx2()) {
    const void* _Stop_at = _First;
    _Advance_bytes(_Stop_at, _Byte_length(_First, _Last) & _Mask_32);
    do {
        __m256i _Data = _mm256_loadu_si256(static_cast<__m256i*>(_First));
        // ... operate ...
        _mm256_storeu_si256(static_cast<__m256i*>(_First), _Data);
        _Advance_bytes(_First, 32);
    } while (_First != _Stop_at);
    _mm256_zeroupper();  // ARM64 has NO equivalent
}
constexpr size_t _Mask_16 = ~((size_t{1} << 4) - 1);
if (_Byte_length(_First, _Last) >= 16 && _Use_sse42()) {
    // SSE4.2 fallback loop ...
}
// scalar tail
```

### ARM64 Equivalent: descending-granularity, no masks

```cpp
// ARM64: single ISA tier (baseline NEON), no masks needed
// 64-byte main loop (4 x 128-bit = matches AVX2 throughput)
if (_Byte_length(_First, _Last) >= 64) {
    constexpr size_t _Mask_64 = ~((size_t{1} << 6) - 1);
    const void* _Stop_at = _First;
    _Advance_bytes(_Stop_at, _Byte_length(_First, _Last) & _Mask_64);
    do {
        uint8x16_t _V0 = vld1q_u8(static_cast<uint8_t*>(_First) +  0);
        uint8x16_t _V1 = vld1q_u8(static_cast<uint8_t*>(_First) + 16);
        uint8x16_t _V2 = vld1q_u8(static_cast<uint8_t*>(_First) + 32);
        uint8x16_t _V3 = vld1q_u8(static_cast<uint8_t*>(_First) + 48);
        // ... operate on _V0.._V3 ...
        vst1q_u8(static_cast<uint8_t*>(_First) +  0, _V0);
        vst1q_u8(static_cast<uint8_t*>(_First) + 16, _V1);
        vst1q_u8(static_cast<uint8_t*>(_First) + 32, _V2);
        vst1q_u8(static_cast<uint8_t*>(_First) + 48, _V3);
        _Advance_bytes(_First, 64);
    } while (_First != _Stop_at);
    // NO zeroupper needed
}
// 32-byte tail (2 x 128-bit)
if (_Byte_length(_First, _Last) >= 32) { /* 2 x vld1q/vst1q */ }
// 16-byte tail (1 x 128-bit)
if (_Byte_length(_First, _Last) >= 16) { /* 1 x vld1q/vst1q */ }
// 8-byte tail
if (_Byte_length(_First, _Last) >= 8)  { /* vld1_u8/vst1_u8 */ }
// 4-byte tail — use lane intrinsic, NOT direct dereference
if (_Byte_length(_First, _Last) >= 4) {
    uint32x2_t _V = vdup_n_u32(0);
    _V = vld1_lane_u32(static_cast<uint32_t*>(_First), _V, 0);
    // ...
    vst1_lane_u32(static_cast<uint32_t*>(_First), _V, 0);
    _Advance_bytes(_First, 4);
}
// scalar byte tail
```

> **Why `vld1_lane_u32` for the 4-byte tail?** A direct `*(uint32_t*)ptr` dereference
> is undefined behavior for non-4-byte-aligned pointers. The lane intrinsic is safe.
> This is a common porting bug — x64 code often uses `memcpy` or direct cast here.

---

## Feature Detection Migration

```cpp
// x64 — REMOVE these:
extern "C" long __isa_enabled;                          // x64-only global
bool _Use_avx2()  { return __isa_enabled & (1 << __ISA_AVAILABLE_AVX2);  }
bool _Use_sse42() { return __isa_enabled & (1 << __ISA_AVAILABLE_SSE42); }

// ARM64 — REPLACE with (inside #if defined(_M_ARM64) || defined(_M_ARM64EC)):
bool _Use_FEAT_DotProd() { return IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE); }
bool _Use_FEAT_SVE()     { return IsProcessorFeaturePresent(PF_ARM_SVE_INSTRUCTIONS_AVAILABLE); }
bool _Use_FEAT_SVE2()    { return IsProcessorFeaturePresent(PF_ARM_SVE2_INSTRUCTIONS_AVAILABLE); }
// Baseline NEON needs NO runtime check — all Armv8-A CPUs have it
```

Key difference: x64 uses a **vcruntime-internal bitmask** (`__isa_enabled`); ARM64 uses
the **Windows OS API** (`IsProcessorFeaturePresent`). This is intentional — SVE requires
OS-level context-save support, so OS-level detection is the correct and safe approach.

---

## Preprocessor Guard Migration

### The Most Common Bug: `_M_ARM64` without `_M_ARM64EC`

```cpp
// WRONG — misses ARM64EC:
#ifdef _M_ARM64
    // NEON implementation
#else
    // x64 implementation  <- ARM64EC falls here and gets x64 code!
#endif

// CORRECT:
#if defined(_M_ARM64) || defined(_M_ARM64EC)
    // NEON implementation
#else
    // x64 implementation
#endif
```

ARM64EC is an ARM64 binary with an x64-compatible ABI. It uses NEON intrinsics,
not SSE/AVX. Forgetting `_M_ARM64EC` is the single most common porting mistake.

See `references/guard-patterns.md` for the complete guard pattern reference.

---

## Quick-Reference: x64 Constructs to Remove or Replace

| x64 Construct | ARM64 Action |
|---|---|
| `#include <intrin.h>` | Replace with `#include <arm64_neon.h>` |
| `#include <isa_availability.h>` | Replace with `#include <Windows.h>` |
| `extern "C" long __isa_enabled;` | Remove entirely |
| `_Use_avx2()` / `_Use_sse42()` | Replace with `_Use_FEAT_*()` or remove (baseline NEON needs no check) |
| `_Zeroupper_on_exit` RAII guard | Remove entirely — no ARM64 equivalent |
| `_Avx2_tail_mask_32()` | Replace with descending-granularity if-chain |
| `__m256i` / `_mm256_*` | Replace with 2-4 x `uint8x16_t` + `vld1q_u8`/`vst1q_u8` |
| `__m128i` / `_mm_*` | Replace with `uint8x16_t` + `vld1q_u8`/`vst1q_u8` |
| `_mm256_zeroupper()` | Remove entirely |
| `__std_*_8` (64-bit int min/max) | Exclude on ARM64/ARM64EC — NEON has no horizontal 64-bit min/max |
| `#ifdef _M_ARM64` alone | Change to `#if defined(_M_ARM64) \|\| defined(_M_ARM64EC)` |
| `#ifndef _M_ARM64` alone | Change to `#if !defined(_M_ARM64) && !defined(_M_ARM64EC)` |

---

## Detailed References

- **`references/intrinsic-mapping.md`** — Comprehensive SSE/AVX to NEON intrinsic
  translation tables: load/store, compare, arithmetic, shuffle/permute, reduction,
  type conversion. Read this when translating specific intrinsic calls.
- **`references/arm64-limitations.md`** — Operations with no direct NEON equivalent
  and how to work around them: 64-bit horizontal min/max, variable-shift, masked
  loads, gather/scatter, cross-lane permutes. Read this when an x64 operation
  seems to have no obvious NEON counterpart.
- **`references/guard-patterns.md`** — All preprocessor guard patterns, ABI shim
  rules, ARM64EC constraints, and the `_VECTORIZED_*` macro tier system. Read this
  when writing or auditing `#if`/`#ifdef` guards in cross-platform code.
