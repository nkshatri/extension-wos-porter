# SSE/AVX to NEON Intrinsic Mapping

Comprehensive translation tables derived from the Microsoft STL's `vector_algorithms.cpp`.
All mappings are verified against real production usage in that codebase.

## Table of Contents
1. [Register Type Mapping](#register-type-mapping)
2. [Load / Store](#load--store)
3. [Broadcast (Splat)](#broadcast-splat)
4. [Compare](#compare)
5. [Arithmetic: Integer](#arithmetic-integer)
6. [Arithmetic: Floating-Point](#arithmetic-floating-point)
7. [Bitwise](#bitwise)
8. [Shift](#shift)
9. [Shuffle / Permute / Reverse](#shuffle--permute--reverse)
10. [Horizontal Reduction](#horizontal-reduction)
11. [Type Conversion / Reinterpret](#type-conversion--reinterpret)
12. [Conditional Select](#conditional-select)
13. [Prefetch](#prefetch)

---

## Register Type Mapping

| x64 Type | Width | ARM64 NEON Type | Notes |
|---|---|---|---|
| `__m128i` | 128-bit integer | `uint8x16_t` / `uint16x8_t` / `uint32x4_t` / `uint64x2_t` | Choose based on element size |
| `__m128` | 128-bit float32 | `float32x4_t` | |
| `__m128d` | 128-bit float64 | `float64x2_t` | |
| `__m256i` | 256-bit integer | 2 x `uint8x16_t` (or typed equivalent) | No native 256-bit; split into two 128-bit ops |
| `__m256` | 256-bit float32 | 2 x `float32x4_t` | |
| `__m256d` | 256-bit float64 | 2 x `float64x2_t` | |
| `__m64` | 64-bit integer | `uint8x8_t` / `uint16x4_t` / `uint32x2_t` | 64-bit NEON (D-register) |

---

## Load / Store

### 128-bit (SSE ↔ NEON 128-bit)

| SSE Intrinsic | NEON Equivalent | Notes |
|---|---|---|
| `_mm_loadu_si128(ptr)` | `vld1q_u8(ptr)` | Unaligned load, reinterpret as needed |
| `_mm_load_si128(ptr)` | `vld1q_u8(ptr)` | NEON vld1q handles both aligned/unaligned |
| `_mm_storeu_si128(ptr, v)` | `vst1q_u8(ptr, v)` | Unaligned store |
| `_mm_store_si128(ptr, v)` | `vst1q_u8(ptr, v)` | |
| `_mm_loadu_ps(ptr)` | `vld1q_f32(ptr)` | float32 |
| `_mm_storeu_ps(ptr, v)` | `vst1q_f32(ptr, v)` | |

### 256-bit (AVX2 → 2 x NEON 128-bit)

```cpp
// x64:
__m256i _Data = _mm256_loadu_si256(static_cast<__m256i*>(ptr));

// ARM64 — split into two 128-bit loads:
uint8x16_t _Lo = vld1q_u8(static_cast<uint8_t*>(ptr) +  0);
uint8x16_t _Hi = vld1q_u8(static_cast<uint8_t*>(ptr) + 16);

// x64:
_mm256_storeu_si256(static_cast<__m256i*>(ptr), _Data);

// ARM64:
vst1q_u8(static_cast<uint8_t*>(ptr) +  0, _Lo);
vst1q_u8(static_cast<uint8_t*>(ptr) + 16, _Hi);
```

### 64-bit (sub-128-bit)

| SSE/scalar | NEON Equivalent | Notes |
|---|---|---|
| `memcpy` 8 bytes | `vld1_u8(ptr)` / `vst1_u8(ptr, v)` | 64-bit D-register load/store |
| `memcpy` 4 bytes | `vld1_lane_u32(ptr, vdup_n_u32(0), 0)` | Use lane intrinsic — direct `*(uint32_t*)` is UB for unaligned |
| `memcpy` 2 bytes | `vld1_lane_u16(ptr, vdup_n_u16(0), 0)` | |

---

## Broadcast (Splat)

| SSE/AVX Intrinsic | NEON Equivalent | Notes |
|---|---|---|
| `_mm_set1_epi8(v)` | `vdupq_n_u8(v)` | Broadcast byte to all 16 lanes |
| `_mm_set1_epi16(v)` | `vdupq_n_u16(v)` | Broadcast 16-bit to all 8 lanes |
| `_mm_set1_epi32(v)` | `vdupq_n_u32(v)` | Broadcast 32-bit to all 4 lanes |
| `_mm_set1_epi64x(v)` | `vdupq_n_u64(v)` | Broadcast 64-bit to both lanes |
| `_mm256_set1_epi8(v)` | `vdupq_n_u8(v)` x2 | Two 128-bit registers |
| `_mm_setzero_si128()` | `vdupq_n_u8(0)` | Zero vector |
| `_mm256_setzero_si256()` | `vdupq_n_u8(0)` x2 | |

---

## Compare

| SSE/AVX Intrinsic | NEON Equivalent | Result |
|---|---|---|
| `_mm_cmpeq_epi8(a, b)` | `vceqq_u8(a, b)` | 0xFF where equal, 0x00 elsewhere |
| `_mm_cmpeq_epi16(a, b)` | `vceqq_u16(a, b)` | |
| `_mm_cmpeq_epi32(a, b)` | `vceqq_u32(a, b)` | |
| `_mm_cmpgt_epi8(a, b)` | `vcgtq_s8(a, b)` | Signed: 0xFF where a > b |
| `_mm_cmpgt_epi16(a, b)` | `vcgtq_s16(a, b)` | |
| `_mm_cmpgt_epi32(a, b)` | `vcgtq_s32(a, b)` | |
| `_mm_cmplt_epi8(a, b)` | `vcltq_s8(a, b)` | |
| `_mm256_cmpeq_epi8(a, b)` | `vceqq_u8(a_lo, b_lo)` + `vceqq_u8(a_hi, b_hi)` | Two 128-bit results |

> **Sign note**: SSE `_mm_cmpgt_epi8` is always signed. NEON has separate `vcgtq_s8`
> (signed) and `vcgtq_u8` (unsigned). Choose the correct one based on your data type.

---

## Arithmetic: Integer

| SSE/AVX Intrinsic | NEON Equivalent | Notes |
|---|---|---|
| `_mm_add_epi8(a, b)` | `vaddq_u8(a, b)` | Wrapping add |
| `_mm_add_epi16(a, b)` | `vaddq_u16(a, b)` | |
| `_mm_add_epi32(a, b)` | `vaddq_u32(a, b)` | |
| `_mm_sub_epi8(a, b)` | `vsubq_u8(a, b)` | |
| `_mm_sub_epi16(a, b)` | `vsubq_u16(a, b)` | |
| `_mm_min_epu8(a, b)` | `vminq_u8(a, b)` | Unsigned min |
| `_mm_min_epi16(a, b)` | `vminq_s16(a, b)` | Signed min |
| `_mm_min_epi32(a, b)` | `vminq_s32(a, b)` | |
| `_mm_max_epu8(a, b)` | `vmaxq_u8(a, b)` | Unsigned max |
| `_mm_max_epi16(a, b)` | `vmaxq_s16(a, b)` | |
| `_mm_max_epi32(a, b)` | `vmaxq_s32(a, b)` | |
| `_mm_abs_epi8(a)` | `vabsq_s8(a)` | |
| `_mm_abs_epi16(a)` | `vabsq_s16(a)` | |
| `_mm_mullo_epi16(a, b)` | `vmulq_s16(a, b)` | Low 16 bits of product |
| `_mm_mullo_epi32(a, b)` | `vmulq_s32(a, b)` | Low 32 bits of product |

> **No 64-bit horizontal min/max in NEON**: `_mm_min_epi64` / `_mm_max_epi64` have
> no NEON equivalent. See `references/arm64-limitations.md` for the workaround.

---

## Arithmetic: Floating-Point

| SSE/AVX Intrinsic | NEON Equivalent |
|---|---|
| `_mm_add_ps(a, b)` | `vaddq_f32(a, b)` |
| `_mm_sub_ps(a, b)` | `vsubq_f32(a, b)` |
| `_mm_mul_ps(a, b)` | `vmulq_f32(a, b)` |
| `_mm_div_ps(a, b)` | `vdivq_f32(a, b)` |
| `_mm_min_ps(a, b)` | `vminq_f32(a, b)` |
| `_mm_max_ps(a, b)` | `vmaxq_f32(a, b)` |
| `_mm_sqrt_ps(a)` | `vsqrtq_f32(a)` |
| `_mm_add_pd(a, b)` | `vaddq_f64(a, b)` |
| `_mm_min_pd(a, b)` | `vminq_f64(a, b)` |
| `_mm_max_pd(a, b)` | `vmaxq_f64(a, b)` |

---

## Bitwise

| SSE/AVX Intrinsic | NEON Equivalent |
|---|---|
| `_mm_and_si128(a, b)` | `vandq_u8(a, b)` |
| `_mm_or_si128(a, b)` | `vorrq_u8(a, b)` |
| `_mm_xor_si128(a, b)` | `veorq_u8(a, b)` |
| `_mm_andnot_si128(a, b)` | `vbicq_u8(b, a)` | Note: operand order is reversed! `vbicq(b, a)` = `b & ~a` |
| `_mm_not_si128(a)` (via xor) | `vmvnq_u8(a)` | Bitwise NOT |
| `_mm256_and_si256(a, b)` | `vandq_u8(a_lo, b_lo)` + `vandq_u8(a_hi, b_hi)` | |

> **`_mm_andnot_si128` operand order trap**: SSE `_mm_andnot_si128(a, b)` computes
> `(~a) & b`. NEON `vbicq_u8(b, a)` computes `b & (~a)`. The operands are swapped
> relative to SSE — this is a very common porting bug.

---

## Shift

| SSE/AVX Intrinsic | NEON Equivalent | Notes |
|---|---|---|
| `_mm_slli_epi16(a, n)` | `vshlq_n_u16(a, n)` | Left shift by immediate |
| `_mm_slli_epi32(a, n)` | `vshlq_n_u32(a, n)` | |
| `_mm_srli_epi16(a, n)` | `vshrq_n_u16(a, n)` | Logical right shift by immediate |
| `_mm_srli_epi32(a, n)` | `vshrq_n_u32(a, n)` | |
| `_mm_srai_epi16(a, n)` | `vshrq_n_s16(a, n)` | Arithmetic right shift (signed) |
| `_mm_srai_epi32(a, n)` | `vshrq_n_s32(a, n)` | |
| `_mm_sll_epi32(a, count)` | `vshlq_u32(a, vdupq_n_s32(n))` | Variable shift — see limitations |
| `_mm_srli_si128(a, n)` | `vextq_u8(a, vdupq_n_u8(0), n)` | Byte shift right (extract) |
| `_mm_slli_si128(a, n)` | `vextq_u8(vdupq_n_u8(0), a, 16-n)` | Byte shift left |

---

## Shuffle / Permute / Reverse

| SSE/AVX Intrinsic | NEON Equivalent | Notes |
|---|---|---|
| `_mm_shuffle_epi8(a, mask)` | `vqtbl1q_u8(a, mask)` | SSSE3 byte shuffle → NEON table lookup |
| `_mm_unpacklo_epi8(a, b)` | `vzip1q_u8(a, b)` | Interleave low halves |
| `_mm_unpackhi_epi8(a, b)` | `vzip2q_u8(a, b)` | Interleave high halves |
| `_mm_unpacklo_epi16(a, b)` | `vzip1q_u16(a, b)` | |
| `_mm_unpackhi_epi16(a, b)` | `vzip2q_u16(a, b)` | |
| `_mm_unpacklo_epi32(a, b)` | `vzip1q_u32(a, b)` | |
| `_mm_unpackhi_epi32(a, b)` | `vzip2q_u32(a, b)` | |
| `_mm_unpacklo_epi64(a, b)` | `vcombine_u64(vget_low_u64(a), vget_low_u64(b))` | |
| `_mm_unpackhi_epi64(a, b)` | `vcombine_u64(vget_high_u64(a), vget_high_u64(b))` | |

### Byte Reversal (used in `__std_reverse_*`)

```cpp
// Reverse 16 bytes (uint8):
// x64: _mm_shuffle_epi8(v, _mm_set_epi8(0,1,2,...,15))
// ARM64:
uint8x16_t _Rev = vrev64q_u8(v);
_Rev = vcombine_u8(vget_high_u8(_Rev), vget_low_u8(_Rev));

// Reverse 8 x uint16:
// ARM64:
uint16x8_t _Rev = vrev32q_u16(v);  // reverse within 32-bit groups
_Rev = vcombine_u16(vget_high_u16(_Rev), vget_low_u16(_Rev));

// Reverse 4 x uint32:
// ARM64:
uint32x4_t _Rev = vcombine_u32(vrev64q_u32(vget_high_u32(v)),
                                vrev64q_u32(vget_low_u32(v)));
```

---

## Horizontal Reduction

| SSE/AVX Intrinsic | NEON Equivalent | Notes |
|---|---|---|
| `_mm_movemask_epi8(a)` | `vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(a), 4)), 0)` | See note below |
| `_mm_testz_si128(a, b)` | `vmaxvq_u8(vandq_u8(a, b)) == 0` | Test if (a & b) == 0 |
| Horizontal max (u8) | `vmaxvq_u8(v)` | Reduce to scalar max |
| Horizontal min (u8) | `vminvq_u8(v)` | Reduce to scalar min |
| Horizontal max (u16) | `vmaxvq_u16(v)` | |
| Horizontal min (u16) | `vminvq_u16(v)` | |
| Horizontal max (u32) | `vmaxvq_u32(v)` | |
| Horizontal min (u32) | `vminvq_u32(v)` | |
| Horizontal max (u64) | **No direct equivalent** | See `arm64-limitations.md` |
| Horizontal min (u64) | **No direct equivalent** | See `arm64-limitations.md` |
| Horizontal add (u8) | `vaddvq_u8(v)` | Sum all lanes |
| Horizontal add (u32) | `vaddvq_u32(v)` | |

### `_mm_movemask_epi8` Replacement

The x64 `_mm_movemask_epi8` extracts the MSB of each byte into a 16-bit integer.
The NEON equivalent uses `vshrn` to pack the high bits:

```cpp
// x64:
int mask = _mm_movemask_epi8(cmp_result);  // 16-bit bitmask
if (mask != 0) { /* found */ }

// ARM64 — two approaches:

// Approach 1: horizontal max (sufficient for "any match?" test)
if (vmaxvq_u8(cmp_result) != 0) { /* found */ }

// Approach 2: full bitmask via vshrn (for exact position finding)
uint8x8_t _Narrowed = vshrn_n_u16(vreinterpretq_u16_u8(cmp_result), 4);
uint64_t _Mask = vget_lane_u64(vreinterpret_u64_u8(_Narrowed), 0);
// Each matching byte contributes 0xF0 to its nibble; use __builtin_ctzll / 4
// to find the first match position
```

---

## Type Conversion / Reinterpret

| SSE/AVX Intrinsic | NEON Equivalent |
|---|---|
| `_mm_castsi128_ps(v)` | `vreinterpretq_f32_u32(v)` |
| `_mm_castps_si128(v)` | `vreinterpretq_u32_f32(v)` |
| `_mm_cvtepi8_epi16(v)` | `vmovl_s8(vget_low_s8(v))` | Sign-extend low 8 bytes to 16-bit |
| `_mm_cvtepu8_epi16(v)` | `vmovl_u8(vget_low_u8(v))` | Zero-extend |
| `_mm_cvtepi16_epi32(v)` | `vmovl_s16(vget_low_s16(v))` | |
| `_mm_cvtepu16_epi32(v)` | `vmovl_u16(vget_low_u16(v))` | |
| `_mm_packs_epi16(a, b)` | `vcombine_s8(vqmovn_s16(a), vqmovn_s16(b))` | Saturating narrow |
| `_mm_packus_epi16(a, b)` | `vcombine_u8(vqmovun_s16(a), vqmovun_s16(b))` | Saturating narrow unsigned |

---

## Conditional Select

| SSE/AVX Intrinsic | NEON Equivalent | Notes |
|---|---|---|
| `_mm_blendv_epi8(a, b, mask)` | `vbslq_u8(mask, b, a)` | Select b where mask=0xFF, a where mask=0x00. **Note operand order**: NEON is `(mask, true_val, false_val)` |
| `_mm_blend_epi16(a, b, imm)` | Manual `vbslq_u16` with constant mask | |

> **`vbslq` operand order**: `vbslq_u8(mask, b, a)` selects `b` where mask bit is 1,
> `a` where mask bit is 0. This is the opposite of `_mm_blendv_epi8(a, b, mask)`
> which selects `b` where mask MSB is 1. Always double-check operand order.

---

## Prefetch

| x64 Intrinsic | ARM64 Equivalent |
|---|---|
| `_mm_prefetch(ptr, _MM_HINT_T0)` | `__builtin_prefetch(ptr, 0, 3)` or `__pld(ptr)` |
| `_mm_prefetch(ptr, _MM_HINT_NTA)` | `__builtin_prefetch(ptr, 0, 0)` |
