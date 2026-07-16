# CRC32 PCLMULQDQ → PMULL: Detailed Reference

Companion to the CRC32 section in [SKILL.md](../../SKILL.md). Covers named macros, helpers,
standard CRC-32/ISO-HDLC constants, fold-state patterns, the Chorba algorithm, and
the full Barrett reduction sequence.

---

## Choosing the ARM algorithm (READ FIRST)

> **A literal PCLMULQDQ → PMULL transcription is usually NOT the fastest ARM CRC.**
> This is the single highest-value decision in a CRC port. On a zlib-ng benchmark,
> a faithful 4-register / 64-byte PMULL fold (the direct translation of the x86
> PCLMULQDQ kernel) ran at **0.30× of upstream's hand-tuned ARM CRC** (14.3 GB/s vs
> 48.0 GB/s at 4 MiB) — a 3.3× gap, entirely from algorithm choice.

**Why the gap exists: ARM has a hardware CRC32 instruction that x86 does not.**

- x86's only fast CRC primitive is `PCLMULQDQ` (carryless multiply). The fastest x86
  CRC is therefore *all* PMULL-style folding, because that is the only tool available.
- AArch64 (with `+crc`, present on essentially all server/laptop ARMv8.1+ parts) has
  `CRC32B/H/W/X` (`__crc32b/h/w/d`) — a **dedicated CRC instruction** on a different
  execution port from PMULL. The fastest ARM CRC runs **both engines in parallel**:
  scalar `__crc32d` lanes *and* PMULL folding lanes, in the same loop, so the two
  pipelines overlap instead of competing.

**The upstream-class ARM kernel (corsix/fast-crc32 family, what zlib-ng ships):**

- **Wide PMULL fold + parallel hardware-CRC**: e.g. **9-way PMULL folding (144 B) interleaved
  with 3-way scalar `__crc32d` (48 B) → 192 bytes per iteration**, vs the 4-way / 64 B of a
  literal PCLMULQDQ port. More independent fold lanes hide PMULL's ~3-cycle latency.
- **`EOR3` (SHA3 `veor3q_u64`)** folds `hi*k`, `lo*k`, and the freshly-loaded data in a
  **single** instruction — replacing two `veorq` per lane. Gate on `__ARM_FEATURE_SHA3`.
- **Tree reduction** of the N fold accumulators down to one 128-bit value, then Barrett
  reduction (below) to 32 bits, then combine with the scalar-CRC lanes via the standard
  CRC-combine constants.

**Decision rule when porting an x86 PCLMULQDQ CRC:**

1. Does the target have `+crc` (`__ARM_FEATURE_CRC32`)? → **Do not transliterate.** Write a
   hardware-CRC + PMULL hybrid (scalar `__crc32d` lanes ∥ wide PMULL fold). This is the
   fast path and should be the primary kernel. Reference: corsix/fast-crc32 generator
   (https://github.com/corsix/fast-crc32) and zlib-ng `arch/arm/crc32_armv8_pmull_eor3.c`.
2. Also provide the plain hardware-CRC path (`__crc32*` only) as the `+crc`-baseline
   variant for smaller buffers and parts without PMULL/EOR3.
3. The literal PMULL-only fold below is the **fallback** for targets *without* `+crc`,
   and the reference for the fold/Barrett mechanics — it is not the performance target
   on any modern AArch64 part.
4. **Benchmark the hybrid against the PMULL-only port at ≥1 MiB.** If the hybrid is not
   ~2–3× faster, the scalar and vector lanes are not actually overlapping — check that the
   `__crc32d` lanes and PMULL fold are in the *same* loop body, not sequential phases.

The macros, constants, fold-state, Chorba, and Barrett material below remain correct and
are used by **both** the hybrid fast path (for its PMULL lanes) and the PMULL-only fallback.

---

## Hardware-CRC Baseline (the `+crc` variant)

The `+crc` **baseline** items (those titled "→ ARMv8 +crc baseline") are a distinct kernel
from both the PMULL fold and the HW+PMULL hybrid: a **pure hardware-CRC chain** using the
ARMv8 CRC32 instructions, gated on `__ARM_FEATURE_CRC32`. Do **not** translate the x86
PCLMULQDQ carryless-multiply fold into these items — that algorithm belongs to the
`pmull-eor3` sibling item. The baseline exists as the no-PMULL / no-SHA3 path for smaller
buffers and for parts without PMULL+EOR3.

```c
#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>           // __crc32b / __crc32h / __crc32w / __crc32d

static uint32_t crc32_armv8_baseline(uint32_t crc, const uint8_t *buf, size_t len) {
    crc = ~crc;
    for (; len >= 8; len -= 8, buf += 8)
        crc = __crc32d(crc, *(const uint64_t *)buf);   // 8 bytes/iter
    if (len >= 4) { crc = __crc32w(crc, *(const uint32_t *)buf); buf += 4; len -= 4; }
    if (len >= 2) { crc = __crc32h(crc, *(const uint16_t *)buf); buf += 2; len -= 2; }
    if (len)        crc = __crc32b(crc, *buf);
    return ~crc;
}
#endif
```

**Polynomial caveat:** ARM `__crc32*` computes the reflected CRC-32/ISO-HDLC polynomial
(`0xEDB88320` — zlib/gzip/PNG/Ethernet). x86 SSE4.2 `_mm_crc32_u*` computes **CRC32C**
(Castagnoli) — a *different* polynomial. If the x86 source uses `_mm_crc32_u*`, the values
are not interchangeable; confirm which CRC the source computes before mapping. PCLMULQDQ
fold code, by contrast, carries its own polynomial constants and typically targets
ISO-HDLC, so it maps to `__crc32*` directly.

For the **fastest** kernel — these `__crc32*` lanes running in parallel with wide PMULL
folding — see "Choosing the ARM algorithm" above; this baseline is the simple, always-correct
floor that the hybrid builds on.

---

## Feature Guard and Headers

```c
#ifdef __ARM_FEATURE_PMULL   // standard compiler define for PMULL support
#include <arm_neon.h>
#include <arm_acle.h>        // vmull_p64, vmull_high_p64, poly64_t, poly128_t
```

Some build systems define their own capability macro (e.g., `ARM_PMULL` in zlib-ng).
Always map that to `__ARM_FEATURE_PMULL` at the guard level; use the standard intrinsics inside.

---

## Named clmul Macros

Define four macros covering all four `_mm_clmulepi64_si128` imm8 lane-select variants.
Do not inline the reinterpret chain at call sites — use these names consistently.

```c
#define clmul_lo_lo(a, b) \
    vreinterpretq_u8_p128(vmull_p64( \
        vgetq_lane_p64(vreinterpretq_p64_u8(a), 0), \
        vgetq_lane_p64(vreinterpretq_p64_u8(b), 0)))
#define clmul_hi_lo(a, b) \
    vreinterpretq_u8_p128(vmull_p64( \
        vgetq_lane_p64(vreinterpretq_p64_u8(a), 1), \
        vgetq_lane_p64(vreinterpretq_p64_u8(b), 0)))
#define clmul_lo_hi(a, b) \
    vreinterpretq_u8_p128(vmull_p64( \
        vgetq_lane_p64(vreinterpretq_p64_u8(a), 0), \
        vgetq_lane_p64(vreinterpretq_p64_u8(b), 1)))
#define clmul_hi_hi(a, b) \
    vreinterpretq_u8_p128(vmull_high_p64( \
        vreinterpretq_p64_u8(a), vreinterpretq_p64_u8(b)))
```

The form above is the GCC/Clang (ACLE) spelling. **MSVC ARM64 diverges** and the
lo/hi variants will not compile as written: MSVC's `vmull_p64` maps to
`neon_pmull_64(__n64, __n64)` and takes a *vector* `poly64x1_t` lane, not a
scalar `poly64_t`. Split on the compiler:

```c
#if defined(_MSC_VER)
#  define clmul_lo_lo(a, b) vreinterpretq_u8_p128(vmull_p64( \
        vget_low_p64(vreinterpretq_p64_u8(a)),  vget_low_p64(vreinterpretq_p64_u8(b))))
#  define clmul_hi_lo(a, b) vreinterpretq_u8_p128(vmull_p64( \
        vget_high_p64(vreinterpretq_p64_u8(a)), vget_low_p64(vreinterpretq_p64_u8(b))))
#  define clmul_lo_hi(a, b) vreinterpretq_u8_p128(vmull_p64( \
        vget_low_p64(vreinterpretq_p64_u8(a)),  vget_high_p64(vreinterpretq_p64_u8(b))))
#else
   /* ...the vgetq_lane_p64 forms above... */
#endif
/* clmul_hi_hi (vmull_high_p64 over the whole Q-reg) is identical on both. */
```

Never write `(poly64_t)some_uint64` — `poly64_t` is a 64-bit int on GCC/Clang
but the opaque `__n64` on MSVC, so the cast only compiles on the former. Also:
MSVC rejects NEON-vector brace-init (`uint32x4_t v = {..}`); build constant
vectors with `vld1q_u32(tmp_array)`. `poly128_t` is unnamed on MSVC — only use
the result through `vreinterpretq_u8_p128()`.

---

## Helper Macros and Constructors

### `xor3` — three-operand XOR

Eliminates nested `veorq_u8` pairs. Useful in fold-step XOR expressions:

```c
#define xor3(a, b, c)  veorq_u8(veorq_u8((a), (b)), (c))
```

### `set_u32x4` and `set_u64x2`

Drop-in replacements for `_mm_set_epi32(e3,e2,e1,e0)` and `_mm_set_epi64x(hi,lo)`.
`_mm_set_epi32` takes args in **high→low** order but stores them low→high in memory;
these helpers use the same logical lane order as the NEON register layout.

```c
/* _mm_set_epi32(e3,e2,e1,e0) -> u32 lanes [e0,e1,e2,e3] */
static inline uint8x16_t set_u32x4(uint32_t e0, uint32_t e1, uint32_t e2, uint32_t e3) {
    const uint32x4_t v = {e0, e1, e2, e3};
    return vreinterpretq_u8_u32(v);
}

/* _mm_set_epi64x(hi, lo) -> u64 lanes [lo, hi] */
static inline uint8x16_t set_u64x2(uint64_t hi, uint64_t lo) {
    return vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(lo), vcreate_u64(hi)));
}
```

---

## CRC-32/ISO-HDLC Polynomial Constants

These constants apply to the standard CRC-32 polynomial (`0xEDB88320`, reflected),
used by zlib, gzip, PNG, and Ethernet. Substitute your own polynomial's fold constants
for other CRC variants.

```c
/* fold4: k1=0x00000001c6e41596, k2=0x0000000154442bd4  (fold by 64 bytes) */
const uint8x16_t fold4 = set_u32x4(0xc6e41596, 0x00000001, 0x54442bd4, 0x00000001);

/* fold12: used by the Chorba large-buffer path */
const uint8x16_t fold12 = set_u64x2(0x596C8D81ULL, 0xF5E48C85ULL);

/* k12: fold 4×128-bit → 1×128-bit (fold by 48 and 32 bytes) */
const uint8x16_t k12 = set_u32x4(0xccaa009e, 0x00000000, 0x751997d0, 0x00000001);

/* Barrett reduction: {mu_lo, mu_hi, poly_lo, poly_hi} */
const uint8x16_t barrett_k = set_u32x4(0xf7011641, 0xb4e5b025, 0xdb710640, 0x00000001);

/* Initial CRC register seed */
uint8x16_t crc0 = vreinterpretq_u8_u32(vsetq_lane_u32(0x9db42487, vdupq_n_u32(0), 0));
```

---

## fold_state_* Pattern

Each `fold_state_N` function advances the 4-register pipeline by folding N registers
using `clmul_hi_lo` and `clmul_lo_hi`, rotating the remaining registers to make room.
This is the ARM equivalent of the x86 PCLMULQDQ fold loop body.

> **Short-buffer guard (segfault risk).** The fold pipeline — and any separate
> "fold the initial CRC in" branch that loads 16 bytes unconditionally — assumes
> `len >= 16`. The kernel ENTRY must short-circuit before the first
> `vld1q_u8(src)` / `len -= 16`:
>
> ```c
> if (len < 16) {                 /* mirror the x86 template's < 16 branch */
>     if (COPY && len) memcpy(dst, src, len);
>     return crc32_braid(crc, src, len);   /* scalar fallback, any length */
> }
> ```
>
> Omitting it (transcribing only the main loop) reads out of bounds AND
> underflows `len -= 16` on the unsigned `size_t`, so the next
> `while (len >= 64)` runs ~`SIZE_MAX` iterations off the end → crash. Each
> width variant needs the guard; a wider kernel (8-lane/avx2, 9-lane/eor3) may
> delegate `len < its_stride` to the proven narrower kernel, but the narrowest
> still needs the `< 16` scalar fallback. Always fuzz `len ∈ {0,1,8,15,16,17}`
> with a non-zero initial `crc` — large aligned buffers never hit this path.

```c
/* fold_state_4: fold all four registers in place (no rotation) */
static inline void fold_state_4(uint8x16_t *crc0, uint8x16_t *crc1,
                                uint8x16_t *crc2, uint8x16_t *crc3,
                                const uint8x16_t fold4) {
    uint8x16_t xl0 = clmul_hi_lo(*crc0, fold4), xh0 = clmul_lo_hi(*crc0, fold4);
    uint8x16_t xl1 = clmul_hi_lo(*crc1, fold4), xh1 = clmul_lo_hi(*crc1, fold4);
    uint8x16_t xl2 = clmul_hi_lo(*crc2, fold4), xh2 = clmul_lo_hi(*crc2, fold4);
    uint8x16_t xl3 = clmul_hi_lo(*crc3, fold4), xh3 = clmul_lo_hi(*crc3, fold4);
    *crc0 = veorq_u8(xl0, xh0);
    *crc1 = veorq_u8(xl1, xh1);
    *crc2 = veorq_u8(xl2, xh2);
    *crc3 = veorq_u8(xl3, xh3);
}
```

`fold_state_1`: rotates crc0→crc1→crc2→crc3 and places the fold result into the vacated crc3.
`fold_state_2` / `fold_state_3`: rotate 2 or 3 registers out; fold only the exiting registers.

The rotation handles the tail of the input where fewer than 4 full 16-byte blocks remain.

---

## Chorba Folding Algorithm

For large buffers, the Chorba algorithm from
**"Saving Computing Resources with Chorba"** (arxiv.org/abs/2412.16398) amortises PMULL
latency by interleaving seed blocks with fold steps.

Structure (stride = 512 bytes, 8 inner blocks of 64 bytes each):

1. Load 8 × 16-byte seed blocks (`chorba1`–`chorba8`) and store them to dst (if copying).
2. Eight inner iterations: call `fold_state_12` (first) or `fold_state_4` (subsequent),
   load 4 × 16-byte input blocks, store them, then XOR seeds into `crc0`–`crc3`
   per the paper's mixing coefficients.
3. Advance `src`, `dst`, `len` by 512.

This technique applies to any PMULL-based CRC, not only CRC-32.

---

## Barrett Reduction (128-bit → 32-bit, full sequence)

```c
uint8x16_t x_tmp0 = clmul_lo_lo(crc3, barrett_k);
uint8x16_t x_tmp1 = clmul_lo_hi(x_tmp0, barrett_k);

/* Isolate u32 lane 2 (bytes 8–11), zero the rest.
 * Equivalent to x86: _mm_blend_epi16(zero, x_tmp1, 0xcf) */
{
    uint32_t lane2 = vgetq_lane_u32(vreinterpretq_u32_u8(x_tmp1), 2);
    x_tmp1 = vreinterpretq_u8_u32(vsetq_lane_u32(lane2, vdupq_n_u32(0), 2));
}
x_tmp0 = veorq_u8(x_tmp1, crc3);

uint8x16_t x_res_a = clmul_hi_lo(x_tmp0, barrett_k);
uint8x16_t x_res_b = clmul_lo_hi(x_res_a, barrett_k);

uint32_t crc = vgetq_lane_u32(vreinterpretq_u32_u8(x_res_b), 2);
return ~crc;
```
