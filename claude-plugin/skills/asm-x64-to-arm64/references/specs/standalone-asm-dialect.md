# Standalone .asm Dialect (MASM x64 → GAS arm64 .S)

Rules for porting an entire x64 MASM file to arm64 GAS syntax. Translate the
dialect layer first (directives, macros, prologues), then translate the body.

## File-Section Directives

**x64 (MASM)**: `.code` for code, `.data` for data, `.const` for read-only data.
`SEGMENT ALIGN(64)` for cache-line-aligned hot loops.

**ARM64 (GAS)**: `.text` for code, `.data` for data, `.section .rdata` for
read-only. Custom sections via `.section .text.foo, "ax"` plus `.balign 64`.

**Workaround**:
```gas
.text                                    // was: .code
.data                                    // was: .data
.section .rdata                          // was: .const
.section .text.hotloop, "ax"             // was: SEGMENT ALIGN(64)
.balign 64
```

**Pitfalls**:
- `.code` and `.text` look interchangeable but the MASM version implies more (default alignment, calling conventions); always explicitly add `.balign` if the original section had alignment.

**Validation**:
- No `.code` / `.const` / `SEGMENT` directives remain in arm64 output.
- Cache-aligned sections have explicit `.balign N` after the `.section`.

## Procedure Definition (PROC/ENDP → label + .global + ret)

**x64 (MASM)**: `name PROC` ... `name ENDP` with implicit prologue (under
`OPTION PROLOGUE`) and `ret` synthesized.

**ARM64 (GAS)**: No `PROC`/`ENDP` keyword. Emit a label and a `.global`
directive; explicit `ret` instruction at the end. `OPTION PROLOGUE:NONE` has no
arm64 equivalent — GAS never emits implicit prologues.

**Workaround**:
```gas
.global LzmaDec_DecodeReal_3
LzmaDec_DecodeReal_3:
        stp     x29, x30, [sp, #-48]!    // save FP+LR + allocate frame
        // ... body ...
        ldp     x29, x30, [sp], #48
        ret
```

**Pitfalls**:
- Forgetting `ret` at the end: GAS does not synthesize one. The function falls through into whatever follows.
- Forgetting `.global` makes the symbol private (file-scope), breaking external links.

**Validation**:
- Every porter-visible function has a `.global <name>` line and a label.
- Every function ends with an explicit `ret`.

## Symbol Visibility (Apple underscore prefix)

**x64**: MASM x64 typically emits bare names; cdecl-on-Win32 prepends underscore via name-decoration macros (`MY_PROC name, n` → `_name`).

**ARM64**: AAPCS64 has no name decoration. Apple Mach-O alone requires a
leading underscore on globals; ELF (Linux/Android) and COFF (Windows ARM64
via clang) use bare names.

**Workaround**:
```gas
#ifdef __APPLE__
        .globl _LzmaDec_DecodeReal_3
#else
        .global LzmaDec_DecodeReal_3
#endif
```

Mirror this `#ifdef __APPLE__` switch for every `.globl`/`.global` line.

**Pitfalls**:
- Hard-coding `_name` (Apple form) breaks ELF/COFF links: the linker looks for `name`, not `_name`.
- Hard-coding `name` breaks Apple builds: dyld cannot resolve the missing-underscore form.

**Validation**:
- Every `.globl`/`.global` directive is wrapped in `#ifdef __APPLE__` switch with both forms.

## Symbol-Type / Size Directives (ELF-only)

**x64**: GAS sources written for Linux freely use `.type name,@function` and
`.size name, .-name` to annotate symbols in the ELF symbol table.

**ARM64**: these directives are **ELF-only**. The COFF assembler (Windows ARM64
via clang `--target=aarch64-pc-windows-msvc`) and Mach-O (Apple) **reject**
`.type`/`.size` — a `.S` that emits them unconditionally fails to assemble on
those targets even though it is fine on Linux. The same applies to other
ELF-isms: `.section .note.GNU-stack`, `@function`/`%function` symbol types,
and `.hidden`/`.internal` visibility forms.

**Workaround** — gate them on `__ELF__` (defined only by ELF-targeting
toolchains). Centralize in a macro so each function body stays portable:
```gas
#if defined(__ELF__)
.macro endfunc name
    .size   \name, . - \name
.endm
#else                 /* COFF (Windows ARM64) / Mach-O (Apple): no .type/.size */
.macro endfunc name
.endm
#endif
```
Do not wrap the directive in a CPP function-like macro that takes the operands
(e.g. `ELF(.size name, .-name)`) — the `,` in the operand list is parsed as a
macro-argument separator and breaks the cpp pass. Define the whole
`function`/`endfunc` macro twice, once per branch, instead.

**Pitfalls**:
- A `.S` that assembles cleanly on Linux can still fail the Windows/Apple build
  solely because of an ungated `.type`/`.size`. Always compile-check every
  target object format, not just the host's.

**Validation**:
- `.type`/`.size`/`@function` and other ELF-isms appear only under
  `#if defined(__ELF__)` (directly or via a gated macro).
- The `.S` assembles for every target object format in scope (ELF + COFF, and
  Mach-O if Apple is a target), not just the build host's.

## Constant and Macro Definitions

**x64 (MASM)**: `name equ value` for constants. `macro foo:req, bar:req` ... `endm`
for macros.

**ARM64 (GAS)**: `.equ name, value` for constants. `.macro foo, bar:req` ...
`.endm` for macros. The `:req` parameter syntax is identical.

**Workaround**:
```gas
.equ MAX_LEN, 273                       // was: MAX_LEN equ 273

.macro p2_add reg:req, param:req        // was: p2_add macro reg:req, param:req
        add     \reg, \reg, \param      //          add reg, reg, param
.endm                                   //      endm
```

**Pitfalls**:
- GAS macro arguments are referenced as `\name`, not bare `name` — easy to miss when transcribing.
- `.macro` does not support some MASM features like `vararg`; multi-arity macros must be expanded into multiple specific forms.

**Validation**:
- All `equ` definitions become `.equ`; all `macro`/`endm` become `.macro`/`.endm`.
- Macro argument references inside the body use `\name`.

## Data Width Annotation

**x64 (MASM)**: `dword ptr [rsi]` / `qword ptr [rsi]` / `byte ptr [rsi]` —
explicit width keyword on the memory operand.

**ARM64 (GAS)**: Width is implicit in the load/store mnemonic and register
size: `ldr w0, [x1]` (32-bit), `ldr x0, [x1]` (64-bit), `ldrb w0, [x1]`
(8-bit), `ldrh w0, [x1]` (16-bit). No `ptr` keyword.

**Workaround**:
| MASM | GAS arm64 |
|---|---|
| `mov rax, qword ptr [rsi]` | `ldr x0, [x1]` |
| `mov eax, dword ptr [rsi]` | `ldr w0, [x1]` |
| `movzx eax, byte ptr [rsi]` | `ldrb w0, [x1]` |
| `movzx eax, word ptr [rsi]` | `ldrh w0, [x1]` |
| `movsx eax, byte ptr [rsi]` | `ldrsb w0, [x1]` |
| `movsx eax, word ptr [rsi]` | `ldrsh w0, [x1]` |

**Pitfalls**:
- Forgetting `ldrsb`/`ldrsh` (signed byte/half load) and using `ldrb`/`ldrh` instead — silently zero-extends where x64 sign-extended.

**Validation**:
- No `ptr` keyword remains.
- Byte/half-word loads use `ldrb`/`ldrh` for unsigned, `ldrsb`/`ldrsh` for signed contexts.

## Include Mechanism

**x64 (MASM)**: `include 7zAsm.asm` — assembler-level include.

**ARM64 (GAS)**: Use C preprocessor `#include "7zAsm.S"` — but only when the
source filename ends in `.S` (capital S), not `.s` (lowercase, no preprocessing).

**Workaround**:
- Rename source to `.S` extension.
- Use `#include "header.S"` (preprocessor) instead of an assembler `.include`.

**Pitfalls**:
- Lowercase `.s` files are not preprocessed by the toolchain — `#include`, `#ifdef`, `#define` all become assembler errors.
- Some build systems still hardcode `.s`; rename and update the build manifest together.

**Validation**:
- Source files use `.S` extension when they need preprocessing.
- All `include` directives become `#include`.

## Two-Operand vs Three-Operand Idiom

**x64**: Two-operand (`add eax, ebx` ≡ `eax = eax + ebx`).

**ARM64**: Three-operand (`add w0, w0, w1`). For long hot-loop ports where
diff-readability against the x86 source is valuable, define a thin macro
layer that re-exposes a two-operand form (the "p2_" idiom from 7-zip's port):

```gas
.macro p2_add reg:req, param:req         // produces 2-operand x86 form
        add     \reg, \reg, \param
.endm
.macro p2_sub_s reg:req, param:req       // _s suffix = sets flags
        subs    \reg, \reg, \param
.endm
```

When the algorithm genuinely needs `a = b + c` (no operand reuse), write the
arm64 instruction directly — do not force it through the macro.

**Pitfalls**:
- The shim emits non-flag-setting forms; the body must use `_s` variants where flags are needed for a subsequent branch. See [[flags-and-conditions]].
- The shim is documentation aid, not a portability layer — its existence does not buy any architecture neutrality.

**Validation**:
- Macros prefixed `p2_` always have form `op \reg, \reg, \param`.
- Macros with `_s` suffix use flag-setting instructions (`adds`, `subs`, `ands`).

## Toolchain Selection on Windows ARM64

**x64**: `ml64.exe` consumes MASM x64 syntax.

**ARM64**: Microsoft `armasm64.exe` does **not** consume GAS syntax — it uses
an ARMASM-derived dialect with different macro keywords and no `.equ`. Three
options:

| Path | When to use |
|---|---|
| `clang -c --target=aarch64-pc-windows-msvc file.S` | Recommended — same `.S` works on Linux/macOS/Windows-on-ARM64 |
| Maintain two source files (`.S` for GAS, `.asm` for armasm64) | Only if MSVC-only build flow is mandatory |
| Translate to armasm64 dialect | Last resort; loses portability |

**Workaround**: Default to clang's integrated assembler. The same `.S` then
assembles on `aarch64-linux-gnu`, `arm64-apple-darwin`, and
`aarch64-pc-windows-msvc`.

**Pitfalls**:
- Feeding GAS `.S` to `armasm64.exe` produces unreadable error cascades — the dialect mismatch is total, not partial.

**Validation**:
- Build invocation uses `clang -c --target=aarch64-pc-windows-msvc` for `.S` files on Windows ARM64.
- No `armasm64.exe` invocation against a GAS-syntax source.

## See Also

- [[register-and-abi]] for argument register mapping and callee-saved sets.
- [[flags-and-conditions]] for S-suffix discipline and branch translation.
- [[memory-addressing]] for `[base+index*scale+disp]` decomposition.
