# SIMD Translation (SSE/AVX → NEON)

ARM64 baseline NEON is 128-bit; YMM (256-bit) and ZMM (512-bit) have no
direct equivalent. NEON registers are `v0`–`v31`, accessed with type/lane
suffixes: `v0.16b` (16 bytes), `v0.8h` (8 halfwords), `v0.4s` (4 single-words),
`v0.2d` (2 doublewords). Inline-asm operand constraint for NEON is `"=w"`,
not `"=r"`.

## SIMD Register Aliases and Width

**x64**: `xmm0`–`xmm15` (128-bit); `ymm0`–`ymm15` (256-bit); `zmm0`–`zmm31` (512-bit).
Sub-register access via `xmmN` referring to the lower 128 of `ymmN`.

**ARM64**: `v0`–`v31` (128-bit). Lane-typed views: `v0.16b`/`v0.8h`/`v0.4s`/`v0.2d`.
Scalar views: `q0` (full 128), `d0` (low 64), `s0` (low 32), `h0` (low 16),
`b0` (low 8). Q/D/S/H/B are aliases of the same physical register but used
with scalar (non-vector) instructions.

**Workaround**:
| x64 | arm64 | Width |
|---|---|---|
| `xmm0` | `v0` (`q0`) | 128-bit |
| `ymm0` | (none — split) | 256-bit |
| `zmm0` | (none — split) | 512-bit |

256-bit operations split into two 128-bit ops on `vN` and `vN+1`. 512-bit
operations split into four. SVE (when available) gives variable-width — but
SVE is not the baseline target.

**Pitfalls**:
- XMM6–XMM15 are callee-saved on Windows x64; their ARM64 counterparts V8–V15
  are also callee-saved on AAPCS64, but the obligation covers only the lower
  64 bits (D8–D15). If the function uses the full 128 bits of V8–V15, save
  and restore the full Q register.
- AVX `_mm256_zeroupper()` has no equivalent — remove entirely. There is no
  upper-half contamination issue on arm64.

**Validation**:
- All `xmm` references become `v` (or `q`) in arm64 output.
- Every `ymm` operation is split into two 128-bit `v` operations.
- V8–V15 saves use full Q-register form when upper 64 bits are written.

## SIMD Load / Store

**x64**: `movdqa` (aligned), `movdqu` (unaligned), `movaps`/`movups` (FP),
`movntdq` (non-temporal).

**ARM64**: `ldr q0, [x1]` is always alignment-safe — no separate aligned form.
`stnp q0, q1, [x1]` is the non-temporal pair store.

**Workaround**:
| x64 | arm64 | Notes |
|---|---|---|
| `movdqa xmm0, [rcx]` | `ldr q0, [x1]` | aligned 128-bit load |
| `movdqu xmm0, [rcx]` | `ldr q0, [x1]` | unaligned — same instruction |
| `movdqa [rcx], xmm0` | `str q0, [x1]` | store |
| `movaps xmm0, [rcx]` | `ldr q0, [x1]` | FP load — same |
| `movntdq [rcx], xmm0` | `stnp q0, q1, [x1]` | non-temporal *pair* store |
| `vmovdqu ymm0, [rcx]` | `ldr q0, [x1]; ldr q1, [x1, #16]` | 256-bit split |
| `movq xmm0, [rcx]` | `ldr d0, [x1]` | 64-bit load into low half |
| `movd xmm0, eax` | `fmov s0, w0` | 32-bit GP→SIMD |
| `movd eax, xmm0` | `fmov w0, s0` | 32-bit SIMD→GP |
| `movq rax, xmm0` | `fmov x0, d0` | 64-bit SIMD→GP |

**Pitfalls**:
- arm64 `ldr q0, [x1]` is unaligned-tolerant by default. The `movdqa` /
  `movdqu` distinction in the x64 source is informational only — both map
  to the same arm64 instruction.
- `stnp` requires a *pair* of registers; single non-temporal stores use
  `str q0, [...]` with no non-temporal hint (or rely on `dc cvap` / cache
  hints separately).
- **`ld1`/`st1` immediate post-index must equal the access size.** With the
  structured load/store form, the immediate post-index on a single-register
  access is fixed to the bytes transferred — `16` for one `{Vn.16b}` /
  `{Vn.8h}` / `{Vn.4s}` (a full Q reg), `8` for a `D`-form. To advance the
  pointer by an arbitrary stride (row pitch, element gap ≠ load width), use the
  **register** post-index form, not an arbitrary immediate:
  ```
  ld1  {v0.8h}, [x1], #16      ; OK    — load 16 bytes, pointer += 16
  ld1  {v0.8h}, [x1], #32      ; ERROR — immediate must equal access size (16)
  mov  x9, #32
  ld1  {v0.8h}, [x1], x9       ; OK    — load 16 bytes, pointer += 32 (skip a row)
  ```
  `ldr q` / `ldp` accept arbitrary signed immediate offsets, but the immediate
  *write-back* on `ld1`/`st1` does not. This trips up ports of row-strided
  loops (image / matrix / DSP) where the stride differs from the load width.

**Validation**:
- All SSE/AVX 128-bit loads/stores become `ldr q` / `str q`.
- 256-bit loads/stores explicitly produce two 128-bit instructions.
- GP↔SIMD scalar transfers use `fmov`, not the inline-asm `mov` mnemonic.
- Any `ld1`/`st1` with an immediate post-index uses an immediate equal to the
  access size; arbitrary strides use a register post-index (`[xN], xM`).

## SIMD Integer Arithmetic

**x64**: `paddb/w/d/q xmm0, xmm1` etc.

**ARM64**: `add v0.<lane>, v0.<lane>, v1.<lane>` where `<lane>` is `16b` /
`8h` / `4s` / `2d`.

**Workaround**:
| x64 | arm64 |
|---|---|
| `paddb xmm0, xmm1` | `add v0.16b, v0.16b, v1.16b` (16 bytes) |
| `paddw xmm0, xmm1` | `add v0.8h, v0.8h, v1.8h` (8 halfwords) |
| `paddd xmm0, xmm1` | `add v0.4s, v0.4s, v1.4s` (4 words) |
| `paddq xmm0, xmm1` | `add v0.2d, v0.2d, v1.2d` (2 doublewords) |
| `psubb` ... | `sub v0.16b, v0.16b, v1.16b` |
| `pmullw xmm0, xmm1` | `mul v0.8h, v0.8h, v1.8h` (low half) |
| `pmulld xmm0, xmm1` | `mul v0.4s, v0.4s, v1.4s` |
| `pmaddwd xmm0, xmm1` | `smull v2.4s, v0.4h, v1.4h; smlal2 v2.4s, v0.8h, v1.8h` (multiply-add chain) |
| `pabsb/w/d` | `abs v0.16b/8h/4s, v1.<>` |
| `pminsb/w/d` | `smin v0.<>, v0.<>, v1.<>` |
| `pmaxsb/w/d` | `smax v0.<>, v0.<>, v1.<>` |
| `pminub/uw/ud` | `umin v0.<>, v0.<>, v1.<>` |
| `pmaxub/uw/ud` | `umax v0.<>, v0.<>, v1.<>` |

**Pitfalls**:
- Lane suffix must match the operation width: `paddb` → `.16b`, `paddw` → `.8h`, etc. Forgetting the suffix or mismatching it changes the operation entirely.
- `pmaddwd` (multiply-and-horizontal-add) has no single-instruction NEON form; it requires `smull` + `smlal2` (or NEON pairwise `addp`) to reproduce.

**Validation**:
- All `padd*`/`psub*`/`pmul*` translate with correct lane suffix.
- `pmaddwd`-class operations use multi-instruction sequences.

## SIMD Bitwise

**x64**: `pand`/`por`/`pxor`/`pandn`.

**ARM64**: `and`/`orr`/`eor`/`bic` on `v0.16b` form (always 16-byte lane).
**Operand order for `pandn` reverses** in `bic`.

**Workaround**:
| x64 | arm64 |
|---|---|
| `pand xmm0, xmm1` | `and v0.16b, v0.16b, v1.16b` |
| `por xmm0, xmm1` | `orr v0.16b, v0.16b, v1.16b` |
| `pxor xmm0, xmm1` | `eor v0.16b, v0.16b, v1.16b` |
| `pandn xmm0, xmm1` | `bic v0.16b, v1.16b, v0.16b` (note operand swap: result = v1 AND NOT v0) |

**Pitfalls**:
- `pandn` semantics: `dst = (NOT dst) AND src`. arm64 `bic` semantics: `dst = src1 AND NOT src2`. The operand order swap is exact.

**Validation**:
- All `pand*`/`por*`/`pxor*` use `.16b` lane.
- `pandn` translation has operands swapped from naive 1:1 mapping.

## SIMD Shift

**x64**: `pslld/q/w xmm0, imm` (left shift), `psrld/q/w` (logical right),
`psrad/w` (arithmetic right).

**ARM64**: `shl v0.<lane>, v0.<lane>, #imm` for left; `ushr` (unsigned) /
`sshr` (signed) for right.

**Workaround**:
| x64 | arm64 |
|---|---|
| `psllw xmm0, imm` | `shl v0.8h, v0.8h, #imm` |
| `pslld xmm0, imm` | `shl v0.4s, v0.4s, #imm` |
| `psllq xmm0, imm` | `shl v0.2d, v0.2d, #imm` |
| `psrlw xmm0, imm` | `ushr v0.8h, v0.8h, #imm` |
| `psrld xmm0, imm` | `ushr v0.4s, v0.4s, #imm` |
| `psrlq xmm0, imm` | `ushr v0.2d, v0.2d, #imm` |
| `psraw xmm0, imm` | `sshr v0.8h, v0.8h, #imm` |
| `psrad xmm0, imm` | `sshr v0.4s, v0.4s, #imm` |
| `pslld xmm0, xmm1` (variable) | `ushl v0.4s, v0.4s, v1.4s` (signed shift count, negative = right) |

**Pitfalls**:
- Variable-shift `pslld xmm0, xmm1` has different semantics in NEON: arm64
  `ushl`/`sshl` interpret a negative count as a right shift, not a wrap-around.
- arm64 has no `psra` for 64-bit lanes (`psraq`); workaround is `cmp v.2d,
  #0` + `eor` chain or `sshr` on a sign-broadcast.

**Validation**:
- All immediate-count shifts have correct lane suffix and direction (`shl`/`ushr`/`sshr`).
- Variable shifts handle signed-count semantics correctly.

## SIMD Compare

**x64**: `pcmpeqb/w/d`, `pcmpgtb/w/d`. Sets each lane to 0xFF...F or 0x00...0.

**ARM64**: `cmeq` (equal), `cmgt` (signed greater-than), `cmhi` (unsigned hi).
Lane width via `.<lane>` suffix.

**Workaround**:
| x64 | arm64 |
|---|---|
| `pcmpeqb xmm0, xmm1` | `cmeq v0.16b, v0.16b, v1.16b` |
| `pcmpeqw xmm0, xmm1` | `cmeq v0.8h, v0.8h, v1.8h` |
| `pcmpeqd xmm0, xmm1` | `cmeq v0.4s, v0.4s, v1.4s` |
| `pcmpgtb xmm0, xmm1` | `cmgt v0.16b, v0.16b, v1.16b` (signed) |
| (unsigned compare) | `cmhi v0.<>, v0.<>, v1.<>` |

**Pitfalls**:
- x64 `pcmpgt*` is signed; for unsigned use NEON `cmhi` (no x64 equivalent
  exists in same single instruction — x64 typically used `pmaxub` + `pcmpeq`).

**Validation**:
- All `pcmpeq*` translate to `cmeq` with correct lane suffix.
- `pcmpgt*` translate to `cmgt` (signed); separately translated unsigned compares use `cmhi`.

## SIMD Shuffle / Permute

**x64**: `pshufb`, `pshufd`, `punpcklbw`/`hbw` etc., `palignr`.

**ARM64**: `tbl` (table lookup, byte-granular), `zip1`/`zip2`/`uzp1`/`uzp2`/
`trn1`/`trn2` (interleaving), `ext` (byte-aligned extract).

**Workaround**:
| x64 | arm64 |
|---|---|
| `pshufb xmm0, xmm1` (byte shuffle by index) | `tbl v0.16b, {v0.16b}, v1.16b` |
| `pshufd xmm0, xmm0, 0` (broadcast lane 0) | `dup v0.4s, v0.s[0]` |
| `pshufd xmm0, xmm1, imm` (general) | Expand imm into a lane-index byte vector, then `tbl` |
| `punpcklbw xmm0, xmm1` | `zip1 v0.16b, v0.16b, v1.16b` |
| `punpckhbw xmm0, xmm1` | `zip2 v0.16b, v0.16b, v1.16b` |
| `punpcklwd xmm0, xmm1` | `zip1 v0.8h, v0.8h, v1.8h` |
| `palignr xmm0, xmm1, imm` | `ext v0.16b, v1.16b, v0.16b, #imm` |
| `vpbroadcastb ymm0, [mem]` | `ld1r {v0.16b}, [x0]` |
| `vpbroadcastd ymm0, [mem]` | `ld1r {v0.4s}, [x0]` |

**Pitfalls**:
- `pshufd imm` to general patterns has no single-instruction NEON form;
  expand the immediate into a byte-index vector and use `tbl`. The
  immediate-to-vector translation is not mechanical — read the imm as a
  4-element 2-bit lane-permutation, then expand each 32-bit lane into 4
  bytes for the byte-granular `tbl` index.
- `palignr` operand order on arm64 `ext` reverses: x64 `palignr xmm0, xmm1,
  imm` extracts from `xmm1:xmm0` — arm64 `ext v0.16b, v1.16b, v0.16b, #imm`
  has the source pair in the opposite order.

**Validation**:
- `pshufb` translates to `tbl`.
- `punpcklXY`/`punpckhXY` translate to `zip1`/`zip2` with correct lane suffix.
- `palignr` translates to `ext` with operand order reversed.

## No-Direct-Equivalent SIMD Operations

| x64 | Arm64 workaround | Notes |
|---|---|---|
| `pmovmskb xmm0, xmm1` (byte mask to GP) | `umaxv b0, v1.16b` for "any nonzero" test; `addv` + masking for general | NEON has no movemask analog |
| `vmovmskps`/`vmovmskpd` | Restructure logic — usually replaceable with `cmlt` + `addp` reductions | |
| 256-bit (`ymm`) anything | Two 128-bit Q-register operations | |
| 512-bit (`zmm`) anything | Four or more 128-bit Q-register operations | |
| `_mm256_zeroupper()` | Remove entirely | No upper-half contamination |
| `vpermd` (cross-lane permute) | `tbl` with manually constructed index | |
| AES-NI (`aesenc` etc.) | NEON crypto (`aese` + `aesmc`) — different round structure | Verify `__ARM_FEATURE_CRYPTO` |
| CLMUL (`pclmulqdq`) | NEON crypto `pmull`/`pmull2` | |

**Pitfalls**:
- `pmovmskb` is the most common compatibility pain point. Many x64 SIMD
  algorithms branch on `movemask == 0xFFFF` or specific bit patterns. arm64
  needs algorithmic rework — typically replacing the bitmask test with a
  reduction (`addv` of compare result) or per-lane processing.
- AES round structure differs: x64 `aesenc` does ShiftRows + SubBytes +
  MixColumns + AddRoundKey in one; arm64 `aese` is ShiftRows + SubBytes +
  AddRoundKey, with `aesmc` (MixColumns) as a separate instruction. Round
  scheduling must be rewritten.

**Validation**:
- No `pmovmskb`/`vmovmskps` in arm64 output.
- AES sequences use paired `aese` + `aesmc` with correct round structure.

## Inline-Asm NEON Constraint

**x64**: `"=x"`, `"=Yz"` for SSE; `"=v"` for AVX.

**ARM64**: `"=w"` for any NEON/FP register output; `"+w"` for read-write.
Operand template uses `%0`/`%1` (no `%w` / `%x` width modifier — the lane
suffix in the instruction template determines the view).

**Workaround**:
```c
// arm64 NEON inline asm
uint8x16_t v;
uint8x16_t a, b;
asm ("add %0.16b, %1.16b, %2.16b" : "=w"(v) : "w"(a), "w"(b));
```

**Pitfalls**:
- Forgetting `"=w"` and using `"=r"` allocates a GP register; the NEON
  instruction will fail to assemble. See [[inline-asm-constraints]].

**Validation**:
- All inline-asm NEON outputs use `"=w"` constraint.

## See Also

- [[inline-asm-constraints]] for the `"=w"` constraint rule and width modifiers.
- [[register-and-abi]] for V8–V15 callee-saved obligations.
- [[memory-addressing]] for `ldr q` addressing-mode constraints.
