# ARM NEON Performance Patterns

---

## General Patterns

### 1. Loop Unrolling: 64B/iter Instead of 16B/iter

A single 16B/iter loop leaves most of the hardware idle. ARM cores typically have 2–4 SIMD execution units and a dual-issue load pipeline, so a loop body with only one `vld1q` + a few arithmetic instructions cannot saturate them. Processing 64 bytes per iteration (four 16-byte vectors) amortizes loop overhead across 4× more work, gives the out-of-order engine enough independent instructions to fill all execution slots, and lets the hardware prefetcher recognise a coarser, more predictable access pattern.

For **non-copy** paths, use `_x4` struct loads — the compiler and hardware get full visibility of the 64-byte stride in a single operation:

```c
uint8x16x4_t d0_d3 = vld1q_u8_x4(buf);
buf += 64;
```

For **copy** paths, use four **separate** loads instead — `_x4` blocks ILP when stores are interleaved:

```c
uint8x16_t d0 = vld1q_u8(buf);
uint8x16_t d1 = vld1q_u8(buf + 16);
uint8x16_t d2 = vld1q_u8(buf + 32);
uint8x16_t d3 = vld1q_u8(buf + 48);
vst1q_u8(dst,      d0);
vst1q_u8(dst + 16, d1);
vst1q_u8(dst + 32, d2);
vst1q_u8(dst + 48, d3);
buf += 64; dst += 64;
```

### 2. Deferred Multiply for Weighted Sums

When computing a weighted sum (Σ weight[i]×b[i]), **never multiply inside the loop**. Accumulate raw byte sums into `uint16x8_t` lanes with cheap widening adds, then apply weights once after the loop with `vmlal_u16`.

```c
// BAD: multiply every iteration
uint16x8_t wsum = vmull_u8(vget_low_u8(vbuf), weights_lo);
wsum = vmlal_u8(wsum, vget_high_u8(vbuf), weights_hi);
acc = vaddq_u32(acc, vpaddlq_u16(wsum));  // in the loop

// GOOD: cheap widening adds in loop, one multiply pass at end
// -- inside loop --
s2_lo = vaddw_u8(s2_lo, vget_low_u8(d0));   // uint8 → uint16, no multiply
s2_hi = vaddw_high_u8(s2_hi, d0);

// -- after loop --
acc = vmlal_u16(acc, vget_low_u16(weights), vget_low_u16(s2_lo));
acc = vmlal_high_u16(acc, weights, s2_lo);
```

Before applying: verify uint16 won't overflow. Bound = iterations × max_byte_value × lanes_per_accumulator < 65535.

### 3. Multiple Independent Accumulators for ILP

A single accumulator serializes execution. Split into 4 independent accumulators and reduce once at the end:

```c
// BAD: serial chain — each iteration waits for the previous
acc = vaddq_u32(acc, weighted_sum);

// GOOD: 4 independent accumulators — CPU can issue all 4 multiply-adds in parallel
uint32x4_t acc0 = vdupq_n_u32(0);
uint32x4_t acc1 = vdupq_n_u32(0);
uint32x4_t acc2 = vdupq_n_u32(0);
uint32x4_t acc3 = vdupq_n_u32(0);
// ... fill each with a different vmlal_u16 subset ...

// Reduce once at the end
uint32x4_t acc = vaddq_u32(vaddq_u32(acc0, acc1), vaddq_u32(acc2, acc3));
```

### 4. Alignment Preamble Before the SIMD Loop

Align `src` to a natural SIMD boundary (16 or 32 bytes) before entering the vectorized loop to avoid cacheline-crossing penalties:

```c
// Process unaligned head with scalar code
size_t head = (-(uintptr_t)src) & 15;  // bytes to next 16-byte boundary
head = head < len ? head : len;
for (size_t i = 0; i < head; i++)
    scalar_process(src[i]);
src += head;
len -= head;
// src is now 16-byte aligned — safe to use aligned loads
```

### 5. Separate Hot-Path Functions for Copy vs Non-Copy

An `if (copy)` branch inside the SIMD loop adds branch prediction pressure and prevents the compiler from optimising each path independently. Provide two separate inner functions and branch outside:

```c
static void process_neon(uint32_t *s, const uint8_t *buf, size_t blocks);
static void process_neon_copy(uint32_t *s, uint8_t *dst, const uint8_t *buf, size_t blocks);

// Branch is outside the hot loop
if (copy)
    process_neon_copy(state, dst, src, n >> 4);
else
    process_neon(state, src, n >> 4);
```

---

## Project-Specific Extensions (zlib-ng)

The patterns above use standard NEON intrinsics. zlib-ng layered additional wrappers on top — do not apply these outside that codebase.

### `_ex` Alignment-Hint Variants

zlib-ng's `neon_intrins.h` defines `_ex` suffixed wrappers that accept an alignment hint in bits. Use them only when the pointer is actually aligned to that boundary:

```c
// Standard NEON (portable)
uint8x16x4_t d0_d3 = vld1q_u8_x4(buf);

// zlib-ng only — 256-bit = 32-byte alignment hint
uint8x16x4_t d0_d3 = vld1q_u8_x4_ex(buf, 256);
vst1q_u8_x4_ex(dst, d0_d3, 256);
```

### `ALIGN_DIFF` and `OPTIMAL_CMP` Macros

These are zlib-ng internal macros, not standard C:

```c
// zlib-ng alignment preamble
size_t align_diff = MIN(ALIGN_DIFF(src, 32), len);
if (align_diff) {
    scalar_process(src, align_diff);
    src += align_diff;
    len -= align_diff;
}

// zlib-ng copy-path fallback based on hardware capability
#if OPTIMAL_CMP >= 32
    return impl_copy(adler, dst, src, len);
#else
    uint32_t result = impl_no_copy(adler, src, len);
    memcpy(dst, src, len);
    return result;
#endif
```

### Deferred Multiply with `tap_table`

The weighted-sum pattern in zlib-ng uses `tap_table` (adler32 position weights) loaded via `vld1q_u16_x4_ex`:

```c
uint16x8x4_t taps = vld1q_u16_x4_ex(tap_table, 256);
acc   = vmlal_high_u16(acc,   taps.val[0], s2_0);
acc_0 = vmlal_u16     (acc_0, vget_low_u16(taps.val[0]), vget_low_u16(s2_0));
```

The principle (defer multiply) is general; the `tap_table` structure and `_ex` load are zlib-ng specific.
