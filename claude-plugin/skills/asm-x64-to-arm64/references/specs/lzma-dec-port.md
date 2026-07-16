# 7-Zip LzmaDecOpt arm64 port — worked example

This reference captures patterns observed in the upstream 7-zip arm64 port
(`Asm/arm64/7zAsm.S` + `Asm/arm64/LzmaDecOpt.S`, by Igor Pavlov). Read this
when porting `Asm/x86/LzmaDecOpt.asm` to arm64, or any large MASM x64 hot-loop
where keeping diff-locality with the x64 source has high review value.

## The 7zAsm.S macro shim

The upstream port keeps the `LzmaDecOpt.S` body **textually similar to the x64
version** by introducing a thin macro layer in `7zAsm.S`. This is project-specific
in inventory but the *technique* (general advice) lives in SKILL.md.

### Register aliases — make `r0..r30` mean `x0..x30`

```gas
#define  r0 x0
#define  r1 x1
...
#define  r30 x30

#define  REG_ABI_PARAM_0 r0
#define  REG_ABI_PARAM_1 r1
#define  REG_ABI_PARAM_2 r2
```

Why: lets the body write `mov r0, r1` regardless of the dialect; gives a single
place to change if an ABI ever needs different physical regs.

### x86-mnemonic macros that emit arm64 instructions

```gas
.macro xor reg:req, param:req
        eor     \reg, \reg, \param
.endm

.macro shl reg:req, param:req
        lsl     \reg, \reg, \param
.endm

.macro shr reg:req, param:req
        lsr     \reg, \reg, \param
.endm

.macro sar reg:req, param:req
        asr     \reg, \reg, \param
.endm

.macro p2_add reg:req, param:req      // "p2_" = "produces 2-operand x86 form"
        add     \reg, \reg, \param
.endm

.macro p2_sub_s reg:req, param:req    // "_s" suffix = sets flags
        subs    \reg, \reg, \param
.endm

.macro inc reg:req
        add     \reg, \reg, 1
.endm
.macro inc_s reg:req                  // flag-setting variant
        adds    \reg, \reg, 1
.endm
.macro dec reg:req
        sub     \reg, \reg, 1
.endm
.macro dec_s reg:req
        subs    \reg, \reg, 1
.endm

.macro imul reg:req, param:req
        mul     \reg, \reg, \param
.endm
```

Naming convention used by Igor:
| Prefix/suffix | Meaning |
|---|---|
| `p2_` | "produces 2-operand": x86 had `op dst, src` (dst = dst op src); macro expands to arm64 3-op `op dst, dst, src` |
| `p1_` | "produces 1-operand": x86 had `op dst` (e.g. `neg`); arm64 needs `op dst, dst` |
| `_s` | flag-setting variant (maps to arm64 S-suffix instructions) |

### x86 jump mnemonics → arm64 conditional branches

```gas
.macro jmp lab:req
        b       \lab
.endm
.macro je lab:req                     // also: jz
        b.eq    \lab
.endm
.macro jne lab:req                    // also: jnz
        b.ne    \lab
.endm
.macro jb lab:req                     // unsigned below
        b.lo    \lab
.endm
.macro jbe lab:req
        b.ls    \lab
.endm
.macro ja lab:req
        b.hi    \lab
.endm
.macro jae lab:req
        b.hs    \lab
.endm
```

Beware the **inverted carry semantics**:
```
arm64-arm     :     x86
b.lo / b.cc   :  jb  / jc          (unsigned <)
b.hs / b.cs   :  jae / jnc         (unsigned >=)
```

### x86 `cmov*` → arm64 `csel`

x86 `cmov*` is two-operand (`cmov<cc> dst, src`: if cc, dst=src). arm64 `csel`
is three-operand (`csel dst, src_true, src_false, cc`). The 7zAsm shim collapses
the 3-op into a 2-op via `csel \dest, \srcTrue, \dest, <cc>`:

```gas
.macro cmove dest:req, srcTrue:req
        csel    \dest, \srcTrue, \dest, eq
.endm
.macro cmovb dest:req, srcTrue:req
        csel    \dest, \srcTrue, \dest, lo
.endm
```

Caveat: this fixes `srcFalse = dest`. Genuine three-way selects must call `csel`
directly, not the macro.

### Alignment macros

```gas
.macro MY_ALIGN_16 macro
        .p2align 4,, (1 << 4) - 1
.endm
.macro MY_ALIGN_32 macro
        .p2align 5,, (1 << 5) - 1
.endm
```

The 3-arg `.p2align <power>, <fill>, <max-skip>` form keeps the assembler from
inserting unbounded NOPs between far-apart labels.

### Apple `_name` symbol prefix

Apple Mach-O requires a leading underscore on globals; ELF/COFF do not:

```gas
#ifdef __APPLE__
        .globl _LzmaDec_DecodeReal_3
#else
        .global LzmaDec_DecodeReal_3
#endif
```

Mirror this for every `.globl`/`.global` directive in the file.

## LZMA-specific data layer (PSHIFT / PLOAD / PSTORE)

The probability table can be 16-bit (`UInt16`, default) or 32-bit (`UInt32`,
under `_LZMA_PROB32`). Loads and stores are abstracted so the inner-loop body
doesn't fork on this:

```gas
#ifdef _LZMA_PROB32
        .equ PSHIFT , 2
        .macro PLOAD dest:req, mem:req
                ldr     \dest, [\mem]
        .endm
        .macro PSTORE src:req, mem:req
                str     \src, [\mem]
        .endm
        // ... PLOAD_2, PLOAD_LSL, PSTORE_2, PSTORE_LSL, PSTORE_LSL_M1
#else
        #define PSHIFT  1
        .macro PLOAD dest:req, mem:req
                ldrh    \dest, [\mem]
        .endm
        .macro PSTORE src:req, mem:req
                strh    \src, [\mem]
        .endm
        // ... PLOAD_2, PLOAD_LSL, PSTORE_2, PSTORE_LSL, PSTORE_LSL_M1
#endif

.equ PMULT    , (1 << PSHIFT)
.equ PMULT_2  , (2 << PSHIFT)

.equ kMatchSpecLen_Error_Data , (1 << 9)
```

Variants of `PLOAD`/`PSTORE` cover the addressing forms the inner loop needs:
- `PLOAD dest, mem` — `[mem]`
- `PLOAD_PREINDEXED dest, mem, offset` — `[mem, offset]!` (writeback)
- `PLOAD_2 dest, mem1, mem2` — `[mem1, mem2]`
- `PLOAD_LSL dest, mem1, mem2` — `[mem1, mem2, lsl #PSHIFT]`
- `PSTORE_LSL_M1 src, mem1, mem2, temp_reg` — when `[mem1, mem2, lsl #-1]` is needed (no negative-shift form on arm64; emulate via `add temp = mem1 + mem2; str src, [temp, mem2]`).

This is the LZMA-specific part you would author from scratch when porting; the
macro NAMES are project convention but the technique (abstract probability load
width via macros) is generally reusable for any algorithm with switchable
element width.

## Register-purpose comment block

LzmaDecOpt's inner loop is at GPR-pressure limit. Top of file documents the
mapping in a single block, then `#define`s give every purpose its own name:

```gas
#       x7      t0 : NORM_CALC    : prob2 (IF_BIT_1)
#       x6      t1 : NORM_CALC    : probs_state
#       x8      t2 : (LITM) temp  : (TREE) temp
#       x4      t3 : (LITM) bit   : (TREE) temp
#       x10     t4 : (LITM) offs  : (TREE) probs_PMULT : numBits
#       x9      t5 : (LITM) match : sym2 (ShortDist)
#       x1      t6 : (LITM) litm_prob : (TREE) prob_reg : pbPos
#       x2      t7 : (LITM) prm   : probBranch  : cnt
#       x3      sym : dist
#       x12     len
#       x0      range
#       x5      cod

#define range   w0
// t6
#define pbPos     w1
#define pbPos_R   r1
#define prob_reg  w1
#define litm_prob    prob_reg
// ... etc
```

Key observation: each physical register has **multiple aliases** corresponding
to the role it plays in different code phases (LITM vs TREE vs NORM_CALC).
Reuse ekes out an extra register-equivalent. The `_R` suffix marks the
64-bit (`r1` = `x1`) form when address arithmetic needs the full pointer.

## Calling-convention contract with C side

`LzmaDec_DecodeReal_3` is *not* a free-floating function — it is co-designed with
`LzmaDec.c`:

1. **Function name and `_3` version stamp.** `LzmaDec.c` contains
   `LzmaDec_TryDummy()` and `LzmaDec_AllocateProbs2()` that link-time-check the
   suffix matches `LZMA_DEC_REAL_VER`. Bumping the asm requires bumping the C.
2. **`CLzmaDec` struct field offsets** must match what the C compiler produces
   for that struct on the *same* arm64 target. Use `.equ` derived from
   `offsetof()` results, and verify with `clang -S LzmaDec.c` once.
3. **`(probs)` array layout** (16-bit vs 32-bit) is selected by the same
   `_LZMA_PROB32` macro on both sides. The arm64 PSHIFT macro family above is
   the asm-side dual.
4. **Argument convention:** `(p, limit, bufLimit)` in `(x0, x1, x2)`. Identical
   under Windows AAPCS64 and SysV AAPCS64 (first 8 args in `x0–x7`).

## Toolchain notes for Windows on ARM64

- Microsoft `armasm64.exe` cannot consume GAS-syntax `.S` files. It uses a
  Microsoft-specific dialect closer to ARMASM (separate `MACRO`/`MEND`,
  different label rules, no `.equ` etc.).
- Use clang's integrated assembler instead:
  `clang -c --target=aarch64-pc-windows-msvc LzmaDecOpt.S -o LzmaDecOpt.o`
- The same source file then assembles on Linux (`clang --target=aarch64-linux-gnu`),
  Apple (`clang --target=arm64-apple-darwin`), and Windows-on-ARM64.

## Build-system selection (where the asm vs C choice happens)

`CPP/7zip/7zip_gcc.mak` and `Build.mak` pick between asm and C using `USE_*_ASM`:

```make
ifdef USE_X86_ASM
$O/LzmaDecOpt.o: ../../../../Asm/x86/LzmaDecOpt.asm
        $(MY_ASM) $(AFLAGS) $<
else
$O/LzmaDec.o: ../../../../C/LzmaDec.c
        $(CC) $(CFLAGS) -DZ7_LZMA_DEC_OPT $<
endif
```

When porting, add a parallel `ifdef USE_ARM64_ASM` branch pointing at
`../../../../Asm/arm64/LzmaDecOpt.S`. The C file `LzmaDec.c` is **still
compiled**: it provides the surrounding glue (`LzmaDec_TryDummy`,
`LzmaDec_AllocateProbs2`, init code) — only `LzmaDec_DecodeReal_3` is replaced
by the asm symbol.

## Verification checklist

| Test | Expectation |
|---|---|
| Link `7zz.exe` | succeeds; `nm 7zz.exe \| grep LzmaDec_DecodeReal_3` shows the symbol from the asm object, not the C object |
| `7zz t corpus.7z` | passes for archives produced by the x64 build (cross-arch interop) |
| `7zz b -mm=lzma2:x5` | MIPS rating ≥ 110% of the arm64-C-only build |
| Fuzz: random LZMA2 streams | no crash, output bit-identical to C reference |

## Translation-difficulty hot spots in LZMA decoder body

When you actually translate the loop, these idioms recur — they are *not* covered
by the simple `xor`/`shl` macro shim and require explicit arm64 thinking:

| x64 idiom | arm64 translation |
|---|---|
| `cmovb rax, rbx` (cond move on carry) | `csel x0, x1, x0, lo` — single instruction, no flag dependency on a fall-through path |
| `shrd rax, rdx, cl` (shift-right-double) | `extr x0, xH, xL, #imm` (immediate) or `lsr` + `lsl` + `orr` triplet (variable count) |
| `sbb rax, rbx` (sub with borrow) | `sbcs x0, x0, x1` — but flag bit polarity differs (arm64 C=1 means *no* borrow); negate via `cset`+`sub` if the asm reads the flag back as a value |
| `bt reg, imm` then `jc` | `tbnz reg, #imm, label` — single instruction, much cheaper |
| `lea rax, [rsi+rdx*4+8]` | `add x0, x1, x2, lsl #2` then `add x0, x0, #8` (two instructions; no scaled+disp combined form) |
| `rep movsb` for short copy (≤ 16 bytes) | unrolled `ldrb`+`strb`, or `ldp x_, x_, [src], #16` + `stp` for 16-byte aligned |
| Accessing low/high 16 of a 32-bit reg (`ax` vs `eax`) | `ubfx` / `uxth` to extract; no register subname |
