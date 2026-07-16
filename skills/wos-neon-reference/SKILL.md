---
description: "SSE/AVX → ARM NEON translation reference for Windows ARM64. Load when actively converting x86 SIMD intrinsics to NEON, choosing between baseline ARMv8.0 and optional ARMv8.2+ extensions, or checking the Windows ARM64 baseline ISA (AES/SHA1/SHA2/PMULL/CRC32) — no runtime feature check needed for baseline. Do NOT load for general questions about ARM64; load only when a specific intrinsic must be translated."
---

# Windows ARM64 NEON Reference

Hand-write every NEON instruction from `<arm_neon.h>` (C/C++), `core::arch::aarch64` (Rust), or `System.Runtime.Intrinsics.Arm.*` (.NET). **Do NOT vendor `sse2neon.h`, `simde`, `xsimd`, `highway`, or any other SIMD translation/abstraction library.**

## Windows ARM64 baseline ISA (unconditionally available — no runtime check)

Every Windows-on-ARM SKU (Snapdragon 835 / 850 / 7c / 8c / 8cx / X Elite, Surface SQ1/SQ2/SQ3, Ampere, Cobalt) implements the following ARMv8.0-A extensions. Use them directly wherever the x86 path used the equivalent hardware extension. **Do NOT guard with `IsProcessorFeaturePresent` for any of these.**

| Feature | NEON / ACLE intrinsic family | x86 analogue to replace |
|---|---|---|
| ASIMD (NEON) | `<arm_neon.h>` `v*q_*` | SSE/SSE2/SSE3/SSSE3/SSE4.1/SSE4.2 |
| AES | `vaeseq_u8`, `vaesdq_u8`, `vaesmcq_u8`, `vaesimcq_u8` | AES-NI (`_mm_aesenc_si128`, `_mm_aesenclast_si128`, `_mm_aesdec_si128`, `_mm_aesdeclast_si128`, `_mm_aeskeygenassist_si128`, `_mm_aesimc_si128`) |
| SHA1 | `vsha1cq_u32`, `vsha1pq_u32`, `vsha1mq_u32`, `vsha1h_u32`, `vsha1su0q_u32`, `vsha1su1q_u32` | SHA-NI (`_mm_sha1rnds4_epu32`, `_mm_sha1nexte_epu32`, `_mm_sha1msg1_epu32`, `_mm_sha1msg2_epu32`) |
| SHA2 (SHA-256) | `vsha256hq_u32`, `vsha256h2q_u32`, `vsha256su0q_u32`, `vsha256su1q_u32` | SHA-NI (`_mm_sha256rnds2_epu32`, `_mm_sha256msg1_epu32`, `_mm_sha256msg2_epu32`) |
| PMULL / PMULL2 | `vmull_p64`, `vmull_high_p64`, `vmull_p8`, `vmull_high_p8` | CLMUL / PCLMULQDQ (`_mm_clmulepi64_si128`) — esp. GHASH/GCM, GF(2^n), CRC reflection |
| CRC32 (CRC32C + CRC32) | `__crc32b/h/w/d`, `__crc32cb/ch/cw/cd` from `<arm_acle.h>` | `_mm_crc32_u8/u16/u32/u64`, software CRC tables |
| FP16 storage | `vld1q_f16` / `vst1q_f16` via `_Float16` (MSVC 19.40+) | — |

## ARMv8.2+ optional features — gate at runtime

| Feature | Intrinsic | Runtime check | Where present |
|---|---|---|---|
| DotProd | `vdotq_s32`, `vdotq_u32` (+ `_lane` variants) | `PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE` | Snapdragon 8cx Gen 3, SQ3, X Elite, Cobalt 100, Ampere Altra+ |
| FP16 arith | `vfmaq_f16`, `vaddq_f16`, etc. | `PF_ARM_V82_FP16_INSTRUCTIONS_AVAILABLE` | Same as DotProd |

SVE/SVE2 (`<arm_sve.h>`) is NOT used on Windows ARM64 — MSVC support is limited and device support is uneven. No `#pragma`, `-mfpu`, or `/arch:` flag is needed for baseline ISA.

## Arch guards (canonical)

```c
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#  include <immintrin.h>
#elif defined(_M_ARM64) || defined(__aarch64__)
#  include <arm_neon.h>
#  include <arm_acle.h>
#else
#  error "Unsupported architecture"
#endif
```

Rust: `#[cfg(all(target_arch = "aarch64", target_os = "windows"))]`
.NET: `if (AdvSimd.IsSupported) { ... } else { /* scalar */ }`

## SSE / SSE2 → NEON — float ops

| SSE | NEON | Notes |
|---|---|---|
| `_mm_set1_ps(x)` | `vdupq_n_f32(x)` | Broadcast |
| `_mm_setzero_ps()` | `vdupq_n_f32(0)` | Zero |
| `_mm_load_ps(p)` | `vld1q_f32(p)` | Load 4 floats |
| `_mm_store_ps(p, v)` | `vst1q_f32(p, v)` | Store 4 floats |
| `_mm_add_ps(a, b)` | `vaddq_f32(a, b)` | |
| `_mm_sub_ps(a, b)` | `vsubq_f32(a, b)` | |
| `_mm_mul_ps(a, b)` | `vmulq_f32(a, b)` | |
| `_mm_div_ps(a, b)` | `vdivq_f32(a, b)` | ARMv8 has native div |
| `_mm_min_ps(a, b)` | `vminq_f32(a, b)` | |
| `_mm_max_ps(a, b)` | `vmaxq_f32(a, b)` | |
| `_mm_sqrt_ps(a)` | `vsqrtq_f32(a)` | |
| `_mm_and_ps(a, b)` | `vreinterpretq_f32_u32(vandq_u32(...))` | |
| `_mm_or_ps(a, b)`  | `vreinterpretq_f32_u32(vorrq_u32(...))` | |
| `_mm_xor_ps(a, b)` | `vreinterpretq_f32_u32(veorq_u32(...))` | |
| `_mm_cmpeq_ps(a, b)` | `vreinterpretq_f32_u32(vceqq_f32(a, b))` | |
| `_mm_cmplt_ps(a, b)` | `vreinterpretq_f32_u32(vcltq_f32(a, b))` | |
| `_mm_shuffle_ps(a,b,i)` | complex — use `vextq`, `vzip`, `vuzp`, `vtbl` | |
| `_mm_movemask_ps(a)` | no direct — use `vshrn` + manual bits | |

## SSE2 → NEON — integer ops

| SSE2 | NEON |
|---|---|
| `_mm_set1_epi32(x)` | `vdupq_n_s32(x)` |
| `_mm_add_epi32(a, b)` | `vaddq_s32(a, b)` |
| `_mm_sub_epi32(a, b)` | `vsubq_s32(a, b)` |
| `_mm_mullo_epi32(a, b)` | `vmulq_s32(a, b)` |
| `_mm_and_si128(a, b)` | `vandq_s32(a, b)` |
| `_mm_or_si128(a, b)`  | `vorrq_s32(a, b)` |
| `_mm_slli_epi32(a, n)` | `vshlq_n_s32(a, n)` |
| `_mm_srli_epi32(a, n)` | `vshrq_n_u32(a, n)` |
| `_mm_cmpeq_epi32(a, b)` | `vceqq_s32(a, b)` |

## AVX / AVX2 → NEON

Hand-write the NEON equivalents (typically 2× 128-bit NEON ops per 256-bit AVX op). Do NOT include `sse2neon` or `simde` even to cover AVX.

## Memory ordering

ARM64 is a weak memory model (vs x64's TSO). Use `<atomic>` / `_Interlocked*` — they generate correct barriers on both. Only add raw `dmb ish`/`ish{ld,st}` when using assembly or intrinsics directly.

## Common pitfalls

1. `_mm_movemask_ps` / `_mm_movemask_epi8`: no direct NEON equivalent — expect multi-instruction emulation.
2. `_mm_shuffle_ps` with compile-time constant: NEON shuffle is more restrictive.
3. Horizontal ops (`_mm_hadd_ps`): NEON has `vpaddq` but semantics differ.
4. `__rdtsc`: use `QueryPerformanceCounter` on ARM64 (portable and correct).
5. Stack alignment: ARM64 requires 16-byte alignment in hand-written asm.
6. SEH works on ARM64 but `CONTEXT` structure uses `X0`–`X28`, `Fp`, `Lr`, `Sp`, `Pc`.
7. 128-bit atomics: use `_InterlockedCompareExchange128` (works on both x64 `cmpxchg16b` and ARM64 `ldxp`/`stxp`).

## Processor feature detection

```c
#include <windows.h>
bool has_neon    = IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE);      // always true
bool has_crc32   = IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE);   // baseline on WoA
bool has_crypto  = IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE);  // baseline on WoA
bool has_atomics = IsProcessorFeaturePresent(PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE);
bool has_dp      = IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE);
bool has_jscvt   = IsProcessorFeaturePresent(PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE);
bool has_lrcpc   = IsProcessorFeaturePresent(PF_ARM_V83_LRCPC_INSTRUCTIONS_AVAILABLE);
```

## ARM64EC

`cl /arm64EC` (or `<Platform>ARM64EC</Platform>` / `CMAKE_GENERATOR_PLATFORM=ARM64EC`) lets an ARM64 binary call into emulated x64 code. Small transition overhead; prefer native ARM64 unless a hard x64 dependency blocks the port.
