# Register Mapping and ABI Differences (x64 → arm64)

ABI-level rules for what each register is, what obligations come with using
it, and where the two x64 ABI variants (System V vs Windows) differ.

## x64 ABI Variants — Identify First

**x64**: Two incompatible x64 ABIs exist. Always identify which one the
original code targets before porting.

| | System V AMD64 (Linux/macOS) | Windows x64 |
|--|--|--|
| Integer args | `rdi, rsi, rdx, rcx, r8, r9` | `rcx, rdx, r8, r9` |
| Shadow space | none | caller reserves 32 bytes above return address |
| GP callee-saved | `rbx, rbp, r12–r15` | `rbx, rbp, rdi, rsi, r12–r15` |
| SIMD callee-saved | none | `xmm6–xmm15` |

**ARM64**: AAPCS64 is unified. `x0–x7` for integer args, `v0–v7` for FP/SIMD
args. No shadow space. `x19–x28` callee-saved GP, `v8–v15` callee-saved (lower
64-bit only).

**Workaround**: First identify the x64 source ABI from build context (Linux/Mac
toolchain → SysV; cl.exe / MSVC project → Windows x64). Then map to AAPCS64.

**Pitfalls**:
- Misidentifying SysV as Windows or vice versa silently swaps argument register order — the asm runs without crashing but produces wrong results.

**Validation**:
- Source ABI is documented or inferred from build files before translation begins.
- Argument-register references in the body match AAPCS64 regardless of source ABI.

## Argument Register Mapping

**x64**: First 4–6 integer args in named registers (varies by ABI).

**ARM64**: First 8 integer args in `x0–x7`. First 8 FP/SIMD args in `v0–v7`.
Indirect-result-pointer register is `x8` (XR), a *dedicated* register — not
the same role as x64 `rdi`'s "first hidden arg".

| | x64 (System V AMD64) | x64 (Windows) | arm64 (AAPCS64) |
|--|--|--|--|
| Integer args 1–8 | `rdi, rsi, rdx, rcx, r8, r9` (1–6) | `rcx, rdx, r8, r9` (1–4) | `x0–x7` |
| Integer args 9+ | stack | stack | stack |
| Integer return | `rax` (`rdx` for 128-bit) | `rax` | `x0` (`x1` for 128-bit) |
| FP/SIMD args 1–8 | `xmm0–xmm7` | `xmm0–xmm3` | `v0–v7` |
| FP/SIMD return | `xmm0` | `xmm0` | `v0` |
| Indirect result ptr | `rdi` (first hidden arg) | `rcx` (first hidden arg) | `x8` (XR — do not reuse as scratch) |

**Workaround**: Translate one register at a time, consulting the table for the
identified source ABI. Hard-coded `mov x0, x0` self-moves are typical when the
register happens to alias.

**Pitfalls**:
- `x8` is XR (indirect result), **not** a general scratch register on AAPCS64. Using it as scratch when the function returns a struct-by-reference will corrupt the return path.
- 32-bit args use `w0–w7`, not the same registers as 32-bit return.

**Validation**:
- All argument registers in arm64 body are within `x0–x7` / `v0–v7`.
- `x8` is used only for indirect result pointer, never as scratch.

## The Contract Comes From the Callee's Declaration, Not the Source Asm

Register mapping (above) only relabels *where* each argument arrives. It does
**not** tell you what each argument *means* — argument order, which pointer
uses which stride, fixed-vs-passed strides, in/out parameters, struct layout,
units (bytes vs elements), and return semantics. That contract is defined by
the **destination's function declaration / reference implementation** (the C
prototype the symbol is linked against, or the project's reference C version of
the same routine) — **not** by the conventions baked into the x64 source.

Hand-written x64 asm often encodes assumptions that are NOT part of the
portable contract: a hard-coded source stride, a register reused across calls,
an argument the C caller actually passes but the asm recomputes. Translating
those verbatim produces code that assembles and even *looks* right but silently
returns wrong results, because it honors the *source's* private convention
instead of the *callee contract* the new caller expects.

**Procedure**: before porting a function, read its authoritative declaration in
the destination project (prototype + the C reference body if one exists) and
derive, per parameter: its role, unit, and — for pointers — exactly which
stride advances it. Port to *that*, then diff against the reference.

**Example (generic, strided 2-D kernel)**: a block-compare routine may take
`(src, dst, stride, ...)` where the C reference advances `src` by a *fixed*
internal pitch (a compile-time constant) while `dst` advances by the *passed*
`stride`. An x64 asm port that advances both by the passed `stride` matches the
source asm but violates the contract — it must mirror the C reference's
per-pointer stride instead. The only way to catch this is to validate numeric
output against the reference (see the verification checklist), not to eyeball
the register mapping.

**Pitfalls**:
- A correct register *mapping* with a wrong argument *meaning* compiles, links,
  and runs — and is wrong. Mapping correctness ≠ contract correctness.
- The x64 source is not authoritative for the contract; the destination
  prototype / reference implementation is.

**Validation**:
- Each argument's role, unit, and (for pointers) governing stride is taken from
  the destination declaration / reference, and the ported body matches it.
- Numeric output is diffed against the reference implementation, not assumed
  from the register mapping (and that diff check is itself failure-tested).

## Callee-Saved Registers

**x64**: SysV: `rbx, rbp, r12–r15`. Windows: adds `rdi, rsi`. SIMD: SysV none,
Windows `xmm6–xmm15` (lower 64 bits).

**ARM64**: GP: `x19–x28`, `x29` (FP), `x30` (LR). SIMD: `v8–v15` lower 64 bits
only (top half is caller-saved). No XMM6–XMM15-style SIMD save obligation —
the obligation only covers `d8–d15`.

**Workaround**:
```gas
// Save 4 callee-saved GP regs
stp     x19, x20, [sp, #-32]!
stp     x21, x22, [sp, #16]
// ... use x19–x22 as long-lived regs ...
ldp     x21, x22, [sp, #16]
ldp     x19, x20, [sp], #32
```

**Pitfalls**:
- Saving the top half of `v8–v15` is wasted work — only `d8–d15` (lower 64 bits) are callee-saved on AAPCS64.
- `x18` is platform-reserved on Windows (TEB pointer) and Apple (interrupt scratch). It is **not** callee-saved — it is reserved. Never use as a general-purpose register.

**Validation**:
- Saved GP register set is a subset of `x19–x28, x29, x30`.
- Saved SIMD register set is a subset of `v8–v15` (saving only the `d` half).
- `x18` is never written.

## Caller-Saved (Scratch) Registers

**x64**: SysV: `rax, rcx, rdx, rsi, rdi, r8–r11`. Windows: drops `rsi/rdi`.

**ARM64**: `x0–x18` are caller-saved (with `x18` reserved for the platform).
SIMD: `v0–v7, v16–v31` caller-saved.

**Workaround**: Inline asm using these registers must declare them in the
clobber list (see [[inline-asm-constraints]]). Standalone asm that calls a
function must spill any live values from caller-saved registers before the
`bl`.

**Pitfalls**:
- `x16` (IP0) and `x17` (IP1) are linker veneer scratch on arm64 — the linker may overwrite them when inserting PLT stubs or range-extension veneers between an asm `bl` and its target. Do not rely on their values surviving a call.

**Validation**:
- Callee-saved values are not held in `x0–x18` or `v0–v7, v16–v31` across a `bl` call.
- `x16`/`x17` are not relied on across `bl`.

## Special-Purpose Registers

| Register | x64 | arm64 | Notes |
|--|--|--|--|
| Indirect result | `rdi` (SysV first hidden arg) / `rcx` (Windows) | `x8` (XR — dedicated) | Never use `x8` as scratch when returning a struct by reference |
| Linker veneer scratch | none | `x16` (IP0), `x17` (IP1) | Linker may overwrite across `bl` |
| Frame pointer | `rbp` (optional) | `x29` | Required on Apple platforms for stack unwinding; optional but strongly recommended on Linux/Windows |
| Stack pointer | `rsp` | `sp` | Must be 16-byte aligned at all times on arm64 (see [[memory-addressing]]) |
| Link register | (none — return addr on stack) | `x30` (LR) | `bl` writes; `ret` reads |
| Platform reserved | (none) | `x18` (Windows TEB / Apple) | Never touch |

**Pitfalls**:
- `x29` (FP) omission: Apple platforms require it for stack unwinding — leaf functions can skip but non-leaf must save FP+LR with `stp x29, x30, [sp, #-16]!`.

**Validation**:
- All non-leaf functions save FP+LR in their prologue.
- `x18` is unwritten throughout the file.

## TLS Access

**x64**: `mov rax, fs:[offset]` (Linux) / `mov rax, gs:[offset]` (Windows) —
segment-prefix addressing reads the thread-local storage segment directly.

**ARM64**: No segment prefixes. `mrs x0, tpidr_el0` loads the TLS base pointer,
then add the variable's offset: `ldr x1, [x0, #offset]`.

**Workaround**:
```gas
// x64: mov rax, fs:[0x28]            // SysV stack canary
// arm64:
mrs     x0, tpidr_el0
ldr     x1, [x0, #0x28]
```

Apple uses `tpidrro_el0` for read-only TLS in user-space; Linux uses
`tpidr_el0`. Wrap in `#ifdef __APPLE__` if the original code accessed both.

**Pitfalls**:
- `mrs` is a system instruction; some kernels disable user-space access to certain `tpidr*` registers — verify on the target platform.

**Validation**:
- No segment-prefix (`fs:`, `gs:`) addressing remains.
- TLS access uses `mrs` + offset load pattern.

## Stack Alignment

**x64**: SP must be 16-byte aligned **before a `call`** (8-byte at function
entry due to the return-address push).

**ARM64**: SP must be 16-byte aligned **at every instruction** — hardware
raises an alignment exception otherwise. Single 8-byte `str`/`ldr` adjustments
to SP are not allowed; always use `stp`/`ldp` pairs or adjust SP by a multiple
of 16.

**Workaround**:
```gas
// Allocate 16 bytes of local space
sub     sp, sp, #16
// ... use [sp, #0] and [sp, #8] ...
add     sp, sp, #16

// Save one register: pad to 16
str     x19, [sp, #-16]!     // not str x19, [sp, #-8]!
ldr     x19, [sp], #16
```

**Pitfalls**:
- `str x19, [sp, #-8]!` (8-byte SP adjustment) crashes on hardware alignment exception, but only on actual ARM64 — emulators and some tooling tolerate it, hiding the bug until production.

**Validation**:
- All SP adjustments are multiples of 16.
- Single-register saves pad to 16 (`[sp, #-16]!`), or use STP for register pairs.

## Red Zone

**x64 (SysV)**: 128-byte **red zone** below RSP. Leaf functions can use this
area without moving RSP.

**ARM64**: **No red zone.** Any use of memory below SP is unsafe — a signal
handler or IRQ may clobber it between instructions. Asm that relied on the red
zone must explicitly allocate stack space.

**Workaround**:
```gas
// x64 leaf function used [rsp - 16] without subtracting RSP.
// arm64 must allocate explicitly:
sub     sp, sp, #16
// ... use [sp, #0] ...
add     sp, sp, #16
```

**Pitfalls**:
- Red-zone usage is implicit in the x64 source — it does not show as a `sub rsp, ...` instruction. Spotting it requires noticing memory accesses below the current RSP.

**Validation**:
- All stack-frame uses in arm64 path are above the current SP, never below.

## Return Address

**x64**: `call` pushes the return address onto the stack; `ret` pops and jumps.

**ARM64**: `bl` writes the return address to `x30` (LR); `ret` jumps to `x30`.
Inline asm or hand-written sequences that synthesize a call/return must use
`blr`/`ret` and manage `x30` explicitly. If the function makes any further
calls, save and restore `x30` (typically together with `x29`).

**Workaround**:
```gas
// Non-leaf prologue / epilogue
stp     x29, x30, [sp, #-16]!
mov     x29, sp
// ... function body, may bl other functions ...
ldp     x29, x30, [sp], #16
ret
```

**Pitfalls**:
- Forgetting to save `x30` before a nested `bl` overwrites the outer return address — function returns to wrong site, often crashing far from the actual bug.

**Validation**:
- All non-leaf functions save and restore `x30` in prologue/epilogue.
- `ret` is the last instruction of every function (no fall-through).

## See Also

- [[flags-and-conditions]] for EFLAGS↔NZCV translation.
- [[memory-addressing]] for SP/stack instruction forms.
- [[inline-asm-constraints]] for clobber-list and operand-modifier rules.
