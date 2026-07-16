# AVX-512 Decomposition for NEON

ARM NEON's maximum native register width is 128 bits. Every `_mm512_*` operation must therefore decompose into **four** independent 128-bit operations (mirroring the way `_mm256_*` decomposes into two — see [avx-decomposition.md](avx-decomposition.md)). Several AVX-512 features go beyond width and require NEON-specific recipes: opaque `__mmaskN` registers, masked load/store, compare-to-bitmask, full-vector horizontal reductions, AVX-512-VNNI dot products, four-lane CLMUL, and cross-lane permutes.

This document is the AVX-512 sibling of `avx-decomposition.md`. Read that first if you have not, and apply the same "halves are independent variables" mental model — only with four halves instead of two.

## 1. Loading and Splitting `__m512*`

```c
// Load 512-bit data as four 128-bit NEON vectors
const uint8_t *ptr = ...;
uint8x16_t v0 = vld1q_u8(ptr +  0);   // bytes 0-15
uint8x16_t v1 = vld1q_u8(ptr + 16);   // bytes 16-31
uint8x16_t v2 = vld1q_u8(ptr + 32);   // bytes 32-47
uint8x16_t v3 = vld1q_u8(ptr + 48);   // bytes 48-63

// 32-bit-element view of the same data
const int32_t *iptr = (const int32_t*)ptr;
int32x4_t i0 = vld1q_s32(iptr +  0);  // elements 0-3
int32x4_t i1 = vld1q_s32(iptr +  4);  // elements 4-7
int32x4_t i2 = vld1q_s32(iptr +  8);  // elements 8-11
int32x4_t i3 = vld1q_s32(iptr + 12);  // elements 12-15
```

For memory-bound loops, a single struct load compiles to four LDR (or two LDP) instructions and gives the prefetcher more context:

```c
uint8x16x4_t blk = vld1q_u8_x4(ptr);
// blk.val[0..3] are the four halves
```

Replace each `__m512i` / `__m512` / `__m512d` typed local with **four** NEON variables (or one `uint8x16x4_t` struct).

## 2. Storing Back

```c
vst1q_u8(dst +  0, v0);
vst1q_u8(dst + 16, v1);
vst1q_u8(dst + 32, v2);
vst1q_u8(dst + 48, v3);

// Or as a struct store:
vst1q_u8_x4(dst, blk);
```

NEON has no alignment-requiring store; `_mm512_store_si512` and `_mm512_storeu_si512` collapse to the same `vst1q_u8` call.

## 3. Element-wise Operations

Apply the matching 128-bit NEON intrinsic to each of the four halves independently:

```c
// _mm512_add_epi32(a, b)
int32x4_t r0 = vaddq_s32(a0, b0);
int32x4_t r1 = vaddq_s32(a1, b1);
int32x4_t r2 = vaddq_s32(a2, b2);
int32x4_t r3 = vaddq_s32(a3, b3);

// _mm512_andnot_si512(a, b) is (~a) & b — operand swap on NEON
uint8x16_t r0 = vbicq_u8(b0, a0);   // note: vbicq is (b AND NOT a), arg order swapped
```

Compares, shifts, min/max, abs, mul, and bitwise ops all follow the same pattern. The four halves never share data.

## 4. Constants and Casts

```c
// _mm512_setzero_si512()
uint8x16_t v0 = vdupq_n_u8(0);
uint8x16_t v1 = vdupq_n_u8(0);
uint8x16_t v2 = vdupq_n_u8(0);
uint8x16_t v3 = vdupq_n_u8(0);

// _mm512_set1_epi32(x)
int32x4_t v0 = vdupq_n_s32(x);
int32x4_t v1 = vdupq_n_s32(x);
int32x4_t v2 = vdupq_n_s32(x);
int32x4_t v3 = vdupq_n_s32(x);

// _mm512_set_epi8(byte63, byte62, ..., byte0)  — note reversed order (set_ vs setr_)
static const uint8_t pattern[64] = { /* 64 bytes in lane order */ };
uint8x16_t v0 = vld1q_u8(pattern +  0);
uint8x16_t v1 = vld1q_u8(pattern + 16);
uint8x16_t v2 = vld1q_u8(pattern + 32);
uint8x16_t v3 = vld1q_u8(pattern + 48);

// _mm512_zextsi128_si512(x128) — place x128 in the low 128 bits, zero the rest
uint8x16_t v0 = x128;
uint8x16_t v1 = vdupq_n_u8(0);
uint8x16_t v2 = vdupq_n_u8(0);
uint8x16_t v3 = vdupq_n_u8(0);

// _mm512_castsi128_si512(x128) — same but upper 384 bits are *undefined* (do not read them)
```

Watch the order: `_mm512_set_*` accepts elements **highest-lane-first**, `_mm512_setr_*` accepts them **lowest-lane-first**. The const array layout must match.

## 5. Lane Extract / Insert

The four halves are already separate NEON variables, so extract / insert is variable selection at compile time:

```c
// _mm512_extracti32x4_epi32(a, k)   — k ∈ {0,1,2,3}
int32x4_t r = ak;   // pick a0/a1/a2/a3 by compile-time switch on k

// _mm512_inserti32x4(a, b128, k)
int32x4_t r0 = (k == 0) ? b128 : a0;
int32x4_t r1 = (k == 1) ? b128 : a1;
int32x4_t r2 = (k == 2) ? b128 : a2;
int32x4_t r3 = (k == 3) ? b128 : a3;
// (in practice the imm8 is a compile-time constant — emit only the chosen branch)
```

`_mm512_extracti64x4_epi64` returns a 256-bit pair; the NEON caller must accept two return variables. `_mm512_extracti64x2` may return a sub-128-bit lane — that becomes a `vextq_u64` within one half.

## 6. Mask Types

`__mmask8` / `__mmask16` / `__mmask32` / `__mmask64` are opaque AVX-512 mask registers. Replace with plain integer types:

| AVX-512 type  | NEON replacement |
|---------------|------------------|
| `__mmask8`    | `uint8_t`        |
| `__mmask16`   | `uint16_t`       |
| `__mmask32`   | `uint32_t`       |
| `__mmask64`   | `uint64_t`       |

Mask intrinsics collapse to plain C operators:

```c
// _kand_mask16(a, b)   →   a & b
// _kor_mask32(a, b)    →   a | b
// _knot_mask64(a)      →   ~a
// _kshiftli_mask16(a, n) → (uint16_t)(a << n)
// _kshiftri_mask16(a, n) → (uint16_t)(a >> n)
// _ktestc_mask16_u8(a, b) → ((a & ~b) == 0)
```

For population count / leading-zero / trailing-zero on the mask, use `__builtin_popcountll` / `__builtin_clzll` / `__builtin_ctzll`. **MSVC ARM64 lacks these** — wrap them in a shim that calls `_BitScanForward64` / `_BitScanReverse64` (see the unit-test workflow's MSVC compatibility shim).

## 7. Masked Load / Store

`_mm512_maskz_loadu_epi8` and `_mm512_mask_storeu_epi8` (and their `epi16`/`epi32`/`epi64`/`ps`/`pd` variants) have **no native NEON equivalent**. Pick the recipe by buffer length and alignment:

**Recipe A — short tail (`len < 16`)**: scalar byte loop is fastest.

```c
for (size_t i = 0; i < tail; i++) dst[i] = src[i];
```

**Recipe B — full-vector zero-mask load**: build a per-lane mask vector, full-vector load, AND with mask.

```c
static const uint8_t indices[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
uint8x16_t idx = vld1q_u8(indices);
uint8x16_t mask = vcltq_u8(idx, vdupq_n_u8((uint8_t)len));   // 0xFF where idx < len
uint8x16_t data = vandq_u8(vld1q_u8(src), mask);
```

(For 64-byte-wide AVX-512, do this four times against `(uint8_t)(len -  0 ... 48)` with `vqsubq_u8` to clamp.)

**Recipe C — masked store via `vbslq_u8`**:

```c
uint8x16_t orig = vld1q_u8(dst);
uint8x16_t merged = vbslq_u8(mask, src, orig);
vst1q_u8(dst, merged);
```

The mask must be all-`0x00` or all-`0xFF` per byte for `vbslq_u8` to behave as expected.

**Recipe D — overlapping 16-byte load** when `len ≥ 16` and the buffer is large enough: read the last 16 bytes from `(ptr + len - 16)` and shift into place via `vextq_u8`. Avoids the `vbsl` chain but adds a `vextq` per overlap.

**Pitfall**: a full 16-byte load that reads past the buffer end can fault near a page boundary. Always either bound `len` first, run a scalar tail, or use the overlapping recipe.

## 8. Compare-to-Bitmask

`_mm512_cmpeq_epu8_mask` and friends produce a `__mmaskN` directly. NEON comparisons produce a per-lane vector of `0xFF` / `0x00`. Pack the per-lane vector into a bitmask via the `vshrn` recipe:

```c
uint8x16_t cmp = vceqq_u8(a, b);   // 0xFF on match, 0x00 on mismatch — per byte

// Pack: vshrn_n_u16 with shift 4 emits a 64-bit value with 4 bits per source byte.
uint8x8_t packed = vshrn_n_u16(vreinterpretq_u16_u8(cmp), 4);
uint64_t lane_mask = vget_lane_u64(vreinterpret_u64_u8(packed), 0);
```

Each set nibble in `lane_mask` corresponds to a matching byte. To find the **first mismatch**:

```c
uint32_t off = (uint32_t)(__builtin_ctzll(~lane_mask) >> 2);   // divide by 4 → byte offset
```

For a 64-byte AVX-512 compare, run the recipe four times against the four halves and `OR` the per-half nibble-packed values into bits `[0..15]`, `[16..31]`, `[32..47]`, `[48..63]` of a single `uint64_t`. The resulting `uint64_t` is bit-equivalent to the AVX-512 `__mmask64`.

**Endianness caveat**: `vget_lane_u64` reads in NEON lane order; a big-endian aarch64 host reverses the bit layout. Guard with `__ARM_BIG_ENDIAN` if you need portability beyond little-endian.

## 9. Horizontal Reductions

`_mm512_reduce_(add|min|max|and|or|xor)_*` is a single-instruction full-vector reduction. NEON has per-128-bit reductions (`vaddvq_*`, `vmaxvq_*`, `vminvq_*`, `vandvq_*` on AArch64). Combine the four halves with the matching pairwise op, then scalarise:

```c
// _mm512_reduce_add_epu32(v)
uint32x4_t s = vaddq_u32(vaddq_u32(v0, v1), vaddq_u32(v2, v3));
uint32_t total = vaddvq_u32(s);

// _mm512_reduce_max_epi32(v)
int32x4_t s = vmaxq_s32(vmaxq_s32(v0, v1), vmaxq_s32(v2, v3));
int32_t max = vmaxvq_s32(s);

// _mm512_reduce_or_epi64(v)
uint64x2_t s = vorrq_u64(vorrq_u64(v0, v1), vorrq_u64(v2, v3));
uint64_t bits = vgetq_lane_u64(s, 0) | vgetq_lane_u64(s, 1);
```

`_mm512_reduce_mul_*` has no NEON intrinsic; emit a scalar loop on the four halves' lanes.

`vaddvq_f32` / `vmaxvq_f32` / `vminvq_f32` are AArch64-only.

## 10. SAD / maddubs / madd / VNNI Dot Products

These four families are AVX-512 specialties without 1:1 NEON equivalents. Use widening pairwise-add and widening multiply recipes.

### `_mm512_sad_epu8` (sum of absolute differences against zero)

```c
uint16x8_t s = vpaddlq_u8(v0);
s = vpadalq_u8(s, v1);
s = vpadalq_u8(s, v2);
s = vpadalq_u8(s, v3);
uint32_t sad = vaddlvq_u16(s);   // total sum
```

`vpaddlq_u8` widens 16 × `u8` to 8 × `u16` with pairwise addition; `vpadalq_u8` adds-and-pairwise-widens additional `u8` data into the same `u16` accumulator. 64 × 255 = 16 320 fits in a `u16`, so the four halves merge without overflow.

### `_mm512_maddubs_epi16` (unsigned×signed byte → 16-bit)

Per half:

```c
uint16x8_t lo = vmull_u8(vget_low_u8(a),  vget_low_u8(b));
uint16x8_t hi = vmull_u8(vget_high_u8(a), vget_high_u8(b));
// Pairwise-add adjacent u16 lanes to u32 → equivalent to one madd_epi16 step
uint32x4_t r = vpaddlq_u16(vaddq_u16(lo, hi));
```

For long accumulations, fold the four halves' `r` vectors into a `uint32x4_t` accumulator and reduce periodically to avoid 16-bit overflow.

### `_mm512_madd_epi16` (signed 16×16 → 32-bit)

```c
int32x4_t lo = vmull_s16(vget_low_s16(a),  vget_low_s16(b));
int32x4_t hi = vmull_high_s16(a, b);
int32x4_t r = vaddq_s32(lo, hi);   // already pairwise-added by vmull semantics
```

### `_mm512_dpbusd_epi32` (AVX-512-VNNI dot product)

When ARMv8.2-A DotProd is available (`__ARM_FEATURE_DOTPROD`):

```c
int32x4_t acc = vdotq_s32(acc, signed_bytes, unsigned_bytes_reinterpreted);
// (vdotq_s32 takes signed×signed, vdotq_u32 takes unsigned×unsigned —
//  for u8×s8 you must reinterpret one operand and account for sign in the accumulator)
```

Software fallback (for cores without DotProd):

```c
int16x8_t prod_lo = vmulq_s16(vmovl_s8(vget_low_s8(s)),
                              vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(u))));
// ... accumulate via vpaddlq_s16 chain
```

## 11. CLMUL — `_mm512_clmulepi64_epi128`

VPCLMULQDQ does four parallel 64×64 → 128 carry-less multiplies, one per 128-bit lane. NEON's `vmull_p64` / `vmull_high_p64` (from `<arm_acle.h>`, feature guard `__ARM_FEATURE_PMULL`) does **one** at a time, so each AVX-512 clmul becomes four NEON pmull calls — one per half.

The four `imm8` lane-select variants (0x00 / 0x01 / 0x10 / 0x11) map directly to which lanes of the two operands feed the multiplier. Use the named macros from [crc32-pmull.md](crc32-pmull.md) (`clmul_lo_lo`, `clmul_hi_lo`, `clmul_lo_hi`, `clmul_hi_hi`) per half — never inline the `vreinterpret` chain at the call site.

```c
poly128_t r0 = clmul_lo_lo(a0, b0);
poly128_t r1 = clmul_lo_lo(a1, b1);
poly128_t r2 = clmul_lo_lo(a2, b2);
poly128_t r3 = clmul_lo_lo(a3, b3);
```

Vmull_p64 is part of the ARMv8 Crypto Extensions, **optional on baseline ARMv8**. For distribution binaries, runtime-detect via `getauxval(AT_HWCAP) & HWCAP_PMULL` and provide a software fallback.

## 12. Permutes, Shuffles, Ternary Logic

`_mm512_permutexvar_epi8/16/32/64` is a full-vector cross-lane permute. The NEON equivalent on AArch64 is `vqtbl4q_u8` (table lookup over four 128-bit halves):

```c
uint8x16x4_t tbl = { v0, v1, v2, v3 };
uint8x16_t r0 = vqtbl4q_u8(tbl, idx0);
uint8x16_t r1 = vqtbl4q_u8(tbl, idx1);
uint8x16_t r2 = vqtbl4q_u8(tbl, idx2);
uint8x16_t r3 = vqtbl4q_u8(tbl, idx3);
```

`vqtbl4q_u8` is **AArch64-only** — gate with `__aarch64__`.

`_mm512_shuffle_epi8` on x86 acts independently within each 128-bit lane. The NEON port should preserve that: four independent `vqtbl1q_u8` calls, **not** `vqtbl4q_u8`. Do not promote a per-128 shuffle into a cross-lane permute.

`_mm512_alignr_epi8(a, b, K)` concatenates `(a:b)` and shifts right by `K` bytes. Within a single 128-bit lane it is `vextq_u8(b_half, a_half, K)`. When `K` crosses 16-byte boundaries, the source half changes — emit the full byte-rotation logic explicitly per half.

`_mm512_ternarylogic_epi32(a, b, c, imm8)` evaluates an arbitrary three-input boolean expression over the bits of `a`, `b`, `c`, where `imm8` is the truth table. Decode `imm8` into a NEON expression of `vandq` / `vorrq` / `veorq` / `vbicq`, e.g.:

| `imm8` | Boolean | NEON |
|--------|---------|------|
| `0x96` | `a ^ b ^ c` | `veorq_u8(a, veorq_u8(b, c))` |
| `0xE2` | `(a & b) \| (~a & c)` | `vbslq_u8(a, b, c)` |
| `0xD8` | `(a & c) \| (~a & b)` | `vbslq_u8(a, c, b)` |
| `0x80` | `a & b & c` | `vandq_u8(a, vandq_u8(b, c))` |
| `0xFE` | `a \| b \| c` | `vorrq_u8(a, vorrq_u8(b, c))` |

For unfamiliar `imm8` values, derive the boolean from the truth table (`(imm8 >> ((a_bit<<2)|(b_bit<<1)|c_bit)) & 1`) and verify with a unit test on randomised inputs.

## Size Reference

| AVX type | Bits | Halves | NEON type per half |
|---|---|---|---|
| `__m128i` (epi32) | 128 | 1 | `int32x4_t` |
| `__m256i` (epi32) | 256 | 2 | `int32x4_t` |
| `__m512i` (epi32) | 512 | 4 | `int32x4_t` |
| `__m512`  (ps)    | 512 | 4 | `float32x4_t` |
| `__m512d` (pd)    | 512 | 4 | `float64x2_t` (AArch64 only) |
| `__mmask8` / `16` / `32` / `64` | 8 / 16 / 32 / 64 | n/a | `uint8_t` / `uint16_t` / `uint32_t` / `uint64_t` |

## Decomposition Checklist

1. Replace each `__m512[id]?` typed local with **four** NEON variables (or one `*x4_t` struct).
2. Replace each `__mmaskN` with the matching `uintNN_t`.
3. For element-wise ops, emit four 128-bit NEON ops with paired operands.
4. For reductions, emit a 4-way tree (3 pairwise ops) then one `vXXXvq_*` scalarisation.
5. For SAD / maddubs / madd / VNNI, use the widening recipes — periodic reduction prevents 16-bit overflow.
6. For masked load/store, choose Recipe A/B/C/D based on length.
7. For cmp-to-mask, use the `vshrn_n_u16(_, 4)` packer and divide `__builtin_ctzll` by 4.
8. For CLMUL, emit four `vmull_p64` calls and use the `clmul_*_*` macros from `crc32-pmull.md`.
9. For cross-lane permute, gate `vqtbl4q_u8` on `__aarch64__`.
10. Validate any non-obvious port (ternarylogic imm8, masked-store edge cases, SAD overflow) with unit tests, not visual inspection.
