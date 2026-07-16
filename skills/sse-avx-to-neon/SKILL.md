---
name: sse-avx-to-neon
description: "Port x86 SSE/AVX intrinsics to ARM NEON, including CRC32 PCLMULQDQ → PMULL. Use when translating x86/x64 intrinsic code to NEON equivalents, adding ARM paths alongside existing x86 code, writing ARM NEON compute kernels from SSE/AVX specifications, or porting PCLMULQDQ-based CRC32 to PMULL. Triggers on: port SSE to NEON, add ARM NEON path, translate AVX to NEON, port CRC32 to ARM, intrinsic names like _mm_add_epi32 / _mm256_sub_ps / _mm_load_si128 / _mm_clmulepi64_si128, __m128i, __m256i, vmull_p64, ARM_PMULL, or any task involving mapping x86 vector intrinsics to ARM NEON."
author: Hao Zeng
---

# SSE/AVX → ARM NEON Porting Guide

## Platform Guard Pattern

Standard structure for adding a NEON path alongside existing x86 code:

```c
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  include <arm_neon.h>
#elif defined(__SSE2__)
#  include <immintrin.h>
#endif

void compute(float* dst, const float* a, const float* b) {
#if defined(__ARM_NEON)
    float32x4_t va = vld1q_f32(a);
    float32x4_t vb = vld1q_f32(b);
    vst1q_f32(dst, vaddq_f32(va, vb));
#elif defined(__SSE__)
    _mm_storeu_ps(dst, _mm_add_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)));
#else
    for (int i = 0; i < 4; i++) dst[i] = a[i] + b[i];
#endif
}
```

For AArch64-only operations (float64, `vzip2q_*`, `vdivq_f32`, most rounding), use `defined(__aarch64__)` in addition to or instead of `__ARM_NEON`.

**Project wrapper headers:** If the project has its own NEON header (e.g., `neon_intrins.h` in zlib-ng), include that instead of `<arm_neon.h>` directly. Project wrappers handle compiler portability (MSVC vs GCC), define convenience macros like `vqsubq_u16_x4_x1`, and extend standard intrinsics with alignment-hint variants (`_ex` suffix). Always check for a project-level NEON header before reaching for the system one.

## Type Selection

`__m128i` is a generic 128-bit container. The NEON type depends on the **operation's element type** (read it from the intrinsic suffix):

| SSE suffix / type | NEON type | Element |
|---|---|---|
| `_epi8` / `__m128i` | `int8x16_t` | signed 8-bit |
| `_epi16` / `__m128i` | `int16x8_t` | signed 16-bit |
| `_epi32` / `__m128i` | `int32x4_t` | signed 32-bit |
| `_epi64` / `__m128i` | `int64x2_t` | signed 64-bit |
| `_epu8` / `__m128i` | `uint8x16_t` | unsigned 8-bit |
| `_epu16` / `__m128i` | `uint16x8_t` | unsigned 16-bit |
| `_epu32` / `__m128i` | `uint32x4_t` | unsigned 32-bit |
| `_epu64` / `__m128i` | `uint64x2_t` | unsigned 64-bit |
| `_ps` / `__m128` | `float32x4_t` | 32-bit float |
| `_pd` / `__m128d` | `float64x2_t` | 64-bit float (AArch64 only) |

For reinterpretation between types (e.g., treating `float32x4_t` as `uint32x4_t` for bitwise ops), use `vreinterpretq_<dst>_<src>()`.

For operations that mix input/output types (pack, unpack, cvt), choose the NEON type matching the **output** element type.

## AVX-512 (512-bit) Decomposition

NEON has no 512-bit registers. Each `_mm512_*` op decomposes into **four** independent 128-bit NEON ops, mirroring the AVX-256 → NEON pattern but with four halves instead of two. Several AVX-512 features go beyond width and need NEON-specific recipes: `__mmaskN` mask types (replace with plain `uintNN_t`), masked load/store (no NEON equivalent — use scalar tail or `vbslq_u8` blend), compare-to-bitmask (`vshrn_n_u16(_, 4)` packer), full-vector horizontal reductions (4-way tree + `vXXXvq_*`), SAD / maddubs / madd / VNNI dot products, four-lane CLMUL, and cross-lane permutes (`vqtbl4q_u8`, AArch64-only).

See [avx512-decomposition.md](references/specs/avx512-decomposition.md) for the full recipe set covering load/store split, element-wise decomposition, constants & casts, lane extract/insert, mask types, masked load/store, compare-to-bitmask, horizontal reductions, SAD/maddubs/madd/VNNI, CLMUL (`_mm512_clmulepi64_epi128` → 4× `vmull_p64`), and cross-lane permute / shuffle / ternarylogic.

## AVX 256-bit Decomposition

NEON has no 256-bit registers. Split `__m256*` into two 128-bit halves and apply the SSE equivalent to each:

```c
// Porting _mm256_add_epi32(a, b) → NEON
// Load 256-bit data as two 128-bit halves (from raw pointer)
int32x4_t a_lo = vld1q_s32((const int32_t*)ptr_a + 0);
int32x4_t a_hi = vld1q_s32((const int32_t*)ptr_a + 4);
int32x4_t b_lo = vld1q_s32((const int32_t*)ptr_b + 0);
int32x4_t b_hi = vld1q_s32((const int32_t*)ptr_b + 4);

// Apply the SSE op to each half independently
int32x4_t r_lo = vaddq_s32(a_lo, b_lo);
int32x4_t r_hi = vaddq_s32(a_hi, b_hi);
```

When the codebase wraps `__m256i` in a struct, extract halves via pointer cast:

```c
const __m128i* halves = (const __m128i*)&avx_val;
__m128i lo = halves[0];
__m128i hi = halves[1];
```

See [avx-decomposition.md](references/specs/avx-decomposition.md) for cross-lane operations (permute, broadcast, extract/insert).

### Prefer xN Struct Loads for Memory-Bound Loops

When porting a loop that applies a uniform operation (e.g., saturating subtract) across a large array, using `_x4` struct loads/stores is better than 4 individual `vld1q` calls — it gives the compiler and hardware more context to optimise memory access:

```c
// Naive decomposition (4 independent loads — suboptimal for bandwidth-bound loops)
uint16x8_t v0 = vld1q_u16(table);
uint16x8_t v1 = vld1q_u16(table + 8);
uint16x8_t v2 = vld1q_u16(table + 16);
uint16x8_t v3 = vld1q_u16(table + 24);
// ... 4 subtracts, 4 stores

// Better: xN struct load (processes 64 bytes in one operation)
uint16x8x4_t chunk = vld1q_u16_x4(table);
chunk.val[0] = vqsubq_u16(chunk.val[0], wsize);
chunk.val[1] = vqsubq_u16(chunk.val[1], wsize);
chunk.val[2] = vqsubq_u16(chunk.val[2], wsize);
chunk.val[3] = vqsubq_u16(chunk.val[3], wsize);
vst1q_u16_x4(table, chunk);
table += 32;   // advance forward (idiomatic ARM direction)
```

If the project wrapper defines `_ex` alignment-hint variants, add the alignment:

```c
uint16x8x4_t chunk = vld1q_u16_x4_ex(table, 256);  // 256-bit = 32-byte alignment hint
vst1q_u16_x4_ex(table, chunk, 256);
```

The `256` is the alignment in bits; the hint lets the microarchitecture use wider memory bus transactions. Only use it when the pointer is actually aligned to that boundary.

**Loop direction:** Iterate forward through the array (increment `table += N`), not backward. Mirroring x86 backward iteration adds complexity with no ARM benefit and prevents the hardware prefetcher from recognising the sequential access pattern.

**Broadcast inside, not outside:** Pass the scalar value to the inner function and call `vdupq_n_u16` there, rather than pre-building the broadcast vector in the caller and passing `uint16x8_t`. This keeps the API simple and matches idiomatic ARM NEON style:

```c
// Prefer this:
static inline void process(uint16_t *table, uint32_t n, uint16_t wsize) {
    uint16x8_t v = vdupq_n_u16(wsize);
    ...
}

// Over this:
static inline void process(uint16_t *table, uint32_t n, const uint16x8_t wsize) { ... }
```

## PCLMULQDQ → PMULL (CRC32)

> **First decide the algorithm, then translate.** A line-for-line PCLMULQDQ→PMULL port
> is usually **not** the fastest ARM CRC and measured **0.30×** of upstream on zlib-ng.
> Unlike x86, AArch64 has a **hardware CRC32 instruction** (`__crc32b/h/w/d`, feature
> `+crc`) on a separate execution port from PMULL. The upstream-class kernel runs
> **scalar hardware-CRC lanes in parallel with wide PMULL folding** (e.g. 9-way fold +
> 3-way `__crc32d`, 192 B/iter, `EOR3`-combined). If the target has `+crc`, write that
> hybrid as the primary kernel — do not transliterate the x86 fold. See the
> **"Choosing the ARM algorithm"** section of [crc32-pmull.md](references/specs/crc32-pmull.md) before writing any code.

For the fold mechanics: `_mm_clmulepi64_si128` maps to `vmull_p64` / `vmull_high_p64` from `<arm_acle.h>` (feature guard: `__ARM_FEATURE_PMULL`). The result is `poly128_t`; reinterpret back with `vreinterpretq_u8_p128(...)`. Each of the four `imm8` lane-select variants maps to a different `vgetq_lane_p64` / `vmull_high_p64` combination — define named `clmul_lo_lo` / `clmul_hi_lo` / `clmul_lo_hi` / `clmul_hi_hi` macros rather than inlining the reinterpret chain. Use `EOR3` (`veor3q_u64`, feature `__ARM_FEATURE_SHA3`) to fuse the two fold XORs plus the new data load into one instruction.

See [crc32-pmull.md](references/specs/crc32-pmull.md) for the algorithm-choice decision rule, the hardware-CRC baseline (`+crc` variant) kernel, macros, `_mm_set_epi32` / `_mm_shuffle_epi8` / `_mm_blend_epi16` replacements, polynomial constants, fold-state patterns, Chorba algorithm, and Barrett reduction.

## Common Intrinsic Mappings

See [type-mappings.md](references/specs/type-mappings.md) for comprehensive tables.

Quick reference for the most frequent operations:

```c
// Arithmetic
vaddq_s32 / vsubq_s32 / vmulq_s32       // epi32 add/sub/mul
vaddq_f32 / vsubq_f32 / vmulq_f32       // ps add/sub/mul
vdivq_f32                               // ps div (AArch64); use Newton-Raphson on ARMv7

// Bitwise (element type is arbitrary for bitwise ops — pick any matching width)
vandq_s32 / vorrq_s32 / veorq_s32       // and/or/xor
vmvnq_s32                               // NOT; andnot = vmvnq then vand

// Shift (immediate — count must be compile-time constant)
vshlq_n_s32 / vshrq_n_s32 / vshrq_n_u32 // slli / srai / srli
// Variable shift — NEON shifts left on positive count, right on negative
vshlq_s32(a, vdupq_n_s32(-n))           // right-shift by n

// Compare (result is all-ones mask per lane, matching SSE semantics)
vceqq_s32 / vcgtq_s32 / vcgeq_s32       // returns uint32x4_t — cast with vreinterpretq_s32_u32
```

## ARM NEON Performance Patterns — MANDATORY for hot kernels

> **A correct translation is not a finished port.** The single biggest cause of a
> slow ARM64 port is **mechanically mirroring the x86 microarchitecture** — copying
> the SSE 16-byte stride, the per-iteration multiply, the single accumulator, the
> backward loop — instead of re-tuning for the ARM pipeline. A line-for-line
> transcription that "produces identical results to the SSE2 source" will typically
> run at **0.4×–0.8× of upstream's hand-tuned ARM kernel** (measured on zlib-ng:
> adler32 0.78×, slide_hash 0.58×, compare256 0.48×, adler32_copy 0.41×).
>
> **x86 and ARM have different SIMD shapes.** x86 has 256/512-bit registers and
> tolerates unaligned access cheaply; ARM NEON is 128-bit but typically has more
> SIMD issue ports and a load pipeline that rewards wider unrolling and more ILP.
> The right port re-derives the loop structure for ARM — it does not transliterate
> the x86 one.

For **any kernel inside a hot loop** (checksums, compare, hash, copy, CRC), you MUST
evaluate all five patterns below and apply each one that fits. Treat skipping one as
a defect, not an optimization opportunity. See
[neon-performance-patterns.md](references/specs/neon-performance-patterns.md) for the full guide:

1. **Loop unrolling** — 64B/iter with `_x4` struct loads (non-copy) vs. four separate loads (copy). A 16B/iter loop (the SSE stride) leaves most ARM SIMD ports idle.
2. **Deferred multiply** — accumulate widened byte sums (`vaddw_u8`) in the loop, apply the weight table once after with `vmlal_u16`. Never carry the x86 `_mm_madd_epi16`/`maddubs` multiply *into* the NEON loop body.
3. **Multiple accumulators** — ≥4 independent `uint32x4_t` accumulators to expose ILP; reduce once at the end. A single accumulator (the usual x86 shape) serializes the narrower NEON datapath.
4. **Alignment preamble** — peel the unaligned head with scalar code, then align `src` to 32 bytes before the SIMD loop (x86 hid this cost; some ARM cores do not).
5. **Copy vs non-copy hot paths** — separate functions to eliminate the in-loop `if (copy)` branch.

**Iterate forward, not backward.** Mirroring an x86 backward loop defeats the ARM
hardware prefetcher for no benefit.

**Verify, don't assume.** When a benchmark exists, build the ARM64 kernel and compare
its throughput against the prior/upstream implementation at a large buffer (≥1 MiB).
If the port is materially slower, the cause is almost always one of the five patterns
above left unapplied — go back and apply it before declaring the port done.

## Implementation Checklist

1. Identify the element type from the intrinsic suffix (`_epi32`, `_ps`, `_epu8`, etc.)
2. Select the NEON type from the table above
3. For `__m128*`: map directly to a `vXXXq_` (quad, 128-bit) intrinsic
4. For `__m256*`: split into `_lo` / `_hi` halves — or use `vld1q_<type>_x4` for memory-bound loops (see AVX 256-bit Decomposition)
5. Use `vreinterpretq_<dst>_<src>()` when switching element interpretation (e.g., float ↔ int for bitwise)
6. Guard `float64x2_t` usage with `defined(__aarch64__)`
7. If no direct NEON equivalent exists, consult [type-mappings.md](references/specs/type-mappings.md) for multi-step recipes
8. Check for a project-level NEON wrapper header before including `<arm_neon.h>` — it may provide `_ex` alignment-hint variants and convenience macros
9. Prefer forward iteration; only mirror x86 backward loops when the algorithm genuinely requires it
10. Pass scalar broadcast values to inner functions and call `vdupq_n` inside — don't pre-build the vector in the caller
11. **Hot loop? Re-tune, don't transcribe (MANDATORY).** Before finishing, apply every relevant pattern from [neon-performance-patterns.md](references/specs/neon-performance-patterns.md): 64B/iter unroll, deferred multiply, ≥4 accumulators, alignment preamble, copy/non-copy split. A kernel that keeps the SSE 16B stride, an in-loop multiply, or a single accumulator is **not done**. If a benchmark exists, confirm throughput is ≥ the implementation you replaced.
12. **PCLMULQDQ / CRC32:** see [crc32-pmull.md](references/specs/crc32-pmull.md) — and read its "Choosing the ARM algorithm" note first: a literal PCLMULQDQ→PMULL port is usually **not** the fastest ARM CRC.
13. **AVX-512 (`_mm512_*`, `__mmaskN`):** see [avx512-decomposition.md](references/specs/avx512-decomposition.md) — every 512-bit op becomes four 128-bit ops; mask types become `uintNN_t`; masked load/store, compare-to-bitmask, full-vector reductions, SAD/maddubs/VNNI, and `_mm512_clmulepi64_epi128` need the NEON-specific recipes documented there
