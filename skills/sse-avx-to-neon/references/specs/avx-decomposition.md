# AVX 256-bit Decomposition for NEON

ARM NEON has no 256-bit registers. All `__m256*` operations must be decomposed into two independent 128-bit operations on the low and high halves.

## Loading and Splitting `__m256*`

**From a raw pointer** (most common when porting memory-bound kernels):

```c
// Load 256-bit data as two 128-bit NEON vectors
const int32_t* ptr = ...;
int32x4_t lo = vld1q_s32(ptr + 0);   // elements 0-3 (lower 128 bits)
int32x4_t hi = vld1q_s32(ptr + 4);   // elements 4-7 (upper 128 bits)

// Float variant
const float* fptr = ...;
float32x4_t lo = vld1q_f32(fptr + 0);
float32x4_t hi = vld1q_f32(fptr + 4);
```

**From a `__m256i` value** (pointer-cast; relies on little-endian layout matching x86):

```c
static inline void split_m256i(const __m256i* v, int32x4_t* lo, int32x4_t* hi) {
    const int32_t* p = (const int32_t*)v;
    *lo = vld1q_s32(p + 0);
    *hi = vld1q_s32(p + 4);
}

static inline __m256i join_m256i(int32x4_t lo, int32x4_t hi) {
    __m256i r;
    vst1q_s32((int32_t*)&r + 0, lo);
    vst1q_s32((int32_t*)&r + 4, hi);
    return r;
}
```

**From a wrapper struct** (common in abstraction layers):

```c
struct m256i { __m128i lo; __m128i hi; };
// Extract as NEON:
int32x4_t lo = vreinterpretq_s32_s8(vld1q_s8((const int8_t*)&val.lo));
int32x4_t hi = vreinterpretq_s32_s8(vld1q_s8((const int8_t*)&val.hi));
// Or simply:
const __m128i* halves = (const __m128i*)&val;
// ... use halves[0] and halves[1] in x86 code; load with vld1q for NEON
```

## Basic Decomposition (Independent Halves)

For any element-wise AVX operation, apply the 128-bit SSE equivalent to each half independently:

```c
// _mm256_add_epi32(a, b)
int32x4_t r_lo = vaddq_s32(a_lo, b_lo);
int32x4_t r_hi = vaddq_s32(a_hi, b_hi);

// _mm256_add_ps(a, b)
float32x4_t r_lo = vaddq_f32(a_lo, b_lo);
float32x4_t r_hi = vaddq_f32(a_hi, b_hi);

// _mm256_add_pd(a, b)  — AArch64 only
float64x2_t r_lo = vaddq_f64(a_lo, b_lo);
float64x2_t r_hi = vaddq_f64(a_hi, b_hi);
```

The same pattern applies to all element-wise operations: sub, mul, div, and/or/xor, min/max, compare, shift, etc.

## Cross-Lane Operations

### Broadcast (replicate one element to all lanes)

```c
// _mm256_broadcastss_ps — broadcast lowest float to all 8 lanes
float lane0 = vgetq_lane_f32(src_lo, 0);
float32x4_t r_lo = vdupq_n_f32(lane0);
float32x4_t r_hi = vdupq_n_f32(lane0);

// _mm256_broadcastd_epi32
int32_t elem = vgetq_lane_s32(src_lo, 0);
int32x4_t r_lo = vdupq_n_s32(elem);
int32x4_t r_hi = vdupq_n_s32(elem);
```

### Extract / Insert 128-bit Lane

```c
// _mm256_extracti128_si256(a, 0) — lower half
int32x4_t result = a_lo;

// _mm256_extracti128_si256(a, 1) — upper half
int32x4_t result = a_hi;

// _mm256_inserti128_si256(a, b128, 0) — replace lower half
int32x4_t r_lo = b128;
int32x4_t r_hi = a_hi;

// _mm256_inserti128_si256(a, b128, 1) — replace upper half
int32x4_t r_lo = a_lo;
int32x4_t r_hi = b128;
```

### Permute Across 128-bit Lanes

```c
// _mm256_permute2f128_ps(a, b, imm8)
// bits[1:0] = source for dst_lo, bits[5:4] = source for dst_hi
// bit 3 zeros dst_lo, bit 7 zeros dst_hi
// Source indices: 0 = a_lo, 1 = a_hi, 2 = b_lo, 3 = b_hi
const float32x4_t* srcs[4] = { &a_lo, &a_hi, &b_lo, &b_hi };
float32x4_t r_lo = (imm8 & 0x08) ? vdupq_n_f32(0.0f) : *srcs[imm8 & 0x03];
float32x4_t r_hi = (imm8 & 0x80) ? vdupq_n_f32(0.0f) : *srcs[(imm8 >> 4) & 0x03];
```

### `_mm256_set_m128i` / `_mm256_setr_m128i`

```c
// Construct 256-bit result from two 128-bit halves
int32x4_t r_lo = lo_128;   // _mm256_set_m128i(hi, lo): lo goes to r_lo
int32x4_t r_hi = hi_128;
```

## Gather (No NEON Hardware Equivalent)

`_mm256_i32gather_*` and related instructions have no NEON equivalent. Use a scalar loop:

```c
// _mm256_i32gather_epi32(base_addr, vindex, scale)
int32_t idx[8];
vst1q_s32(idx + 0, idx_lo);
vst1q_s32(idx + 4, idx_hi);
int32_t out[8];
for (int i = 0; i < 8; i++)
    out[i] = *(const int32_t*)((const uint8_t*)base_addr + (size_t)idx[i] * scale);
int32x4_t r_lo = vld1q_s32(out + 0);
int32x4_t r_hi = vld1q_s32(out + 4);
```

## Storing the Result

```c
// Store two halves back to memory
vst1q_s32((int32_t*)dst + 0, r_lo);
vst1q_s32((int32_t*)dst + 4, r_hi);

// Into a wrapper struct
struct m256i r;
vst1q_s32((int32_t*)&r.lo, r_lo);
vst1q_s32((int32_t*)&r.hi, r_hi);
```

## Size Reference

| AVX type | Bits | Low half | High half | 128-bit NEON type |
|---|---|---|---|---|
| `__m256i` (epi32) | 256 | elements 0-3 | elements 4-7 | `int32x4_t` |
| `__m256` | 256 | elements 0-3 | elements 4-7 | `float32x4_t` |
| `__m256d` | 256 | elements 0-1 | elements 2-3 | `float64x2_t` (AArch64) |
| `__m512i` | 512 | lower `__m256i` | upper `__m256i` | recurse |
