# GCC Extended Inline-Asm Constraints (x64 → arm64)

Rules for translating `asm volatile (...)` / `__asm__ (...)` blocks. The
surrounding C does not change; only the constraint letters, operand modifiers,
and clobber list need rewriting.

## Named Register Constraints

**x64**: Uses architecture-specific letters: `"=a"` (eax), `"=c"` (ecx),
`"=d"` (edx), `"=S"` (esi), `"=D"` (edi), `"=b"` (ebx).

**ARM64**: Use generic `"=r"` for any GP register; `"=w"` for NEON/FP registers.
Let the compiler allocate — there is no equivalent to forcing a specific
named register through a constraint letter.

**Workaround**: Replace every named-register constraint with `"=r"` (or `"+r"`
for read-write). For NEON, switch to `"=w"`. The compiler picks the actual
register; clobber declarations must match what the template uses.

```c
// x64
asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));

// arm64 — different instruction entirely; use generic constraint
uint64_t cnt;
asm volatile ("mrs %0, cntvct_el0" : "=r"(cnt));
```

**Pitfalls**:
- Carrying over `"=a"` / `"=d"` to arm64 produces a compile error: those letters do not exist in the arm64 backend.
- Using `"=r"` where `"=w"` is required (NEON value) silently allocates a GP register and the FP/NEON instruction will fail to assemble.

**Validation**:
- No `"=a"`, `"=b"`, `"=c"`, `"=d"`, `"=S"`, `"=D"` constraint letters remain in the arm64 path.
- NEON/FP outputs use `"=w"`; integer outputs use `"=r"`.

## Operand Width Modifiers (%w0 / %x0)

**x64**: `%0`, `%1`, ... refer to operands; the assembler picks register width
based on the operand type.

**ARM64**: Without a modifier, GCC may emit either `x0` (64-bit) or `w0`
(32-bit) form. Use `%x0` to force the 64-bit register name and `%w0` to force
the 32-bit name. Mismatching the modifier with the instruction width is a
silent bug — the assembler accepts a wrong-width register if it parses, and
the runtime result is wrong.

**Workaround**: Annotate every operand with `%x` or `%w` matching the
instruction width:

```c
// 64-bit add
asm ("add %x0, %x1, %x2" : "=r"(out) : "r"(a), "r"(b));

// 32-bit add (zero-extends to 64)
asm ("add %w0, %w1, %w2" : "=r"(out) : "r"(a), "r"(b));
```

**Pitfalls**:
- Writing to `%w0` automatically zero-extends to the full `x0` (same as `eax→rax`); writing to `%x0` does not preserve any prior upper bits.
- 8-bit (`al`) / 16-bit (`ax`) sub-registers on x64 do **not** zero-extend; arm64 has no equivalent partial form. Use `ubfx` / `uxtb` / `uxth` for explicit bit-field extraction.

**Validation**:
- Every operand reference in the asm template uses `%w` or `%x`.
- 32-bit operations use `%w`, 64-bit operations use `%x` consistently.

## Clobber List

**x64**: Lists named registers (`"eax"`, `"edx"`, ...) the asm trashes.
`"cc"` for EFLAGS, `"memory"` for memory barrier semantics.

**ARM64**: Same `"memory"` and `"cc"` work (cc means NZCV here). Replace x64
named registers with arm64 register names actually used by the template — or
drop them entirely if the template only references operands the constraints
already cover.

**Workaround**:
```c
// x64
asm volatile ("cpuid" :: : "eax", "ebx", "ecx", "edx", "memory");

// arm64 — different instruction, different clobbers
asm volatile ("dmb ish" ::: "memory");
```

**Pitfalls**:
- Forgetting to add `"cc"` when the asm uses a flag-setting instruction (`adds`, `subs`, `cmp`, `tst`) lets the compiler assume flags survive — corrupts surrounding code.
- Listing x64 register names like `"eax"` in an arm64 clobber is a compile error.

**Validation**:
- Clobber list contains only arm64 register names (`"x0"`, `"v0"`, ...) or the architecture-neutral `"cc"` / `"memory"`.
- Every flag-setting instruction in the template has `"cc"` in the clobber list.

## Memory Operand Constraints

**x64**: `"m"` accepts complex addressing (`[rax + rbx*4 + disp32]`).

**ARM64**: `"m"` only generates simple `[xN]` or `[xN, #imm12]` (12-bit
unsigned immediate offset, scaled by access size). Complex addressing has no
direct equivalent — split the address calculation into a separate `"=r"`
output and reference that register.

**Workaround**:
```c
// x64 with complex address
asm ("movl %1, %0" : "=r"(v) : "m"(arr[i*4 + 8]));

// arm64 — compute the address first
uintptr_t addr = (uintptr_t)&arr[i] + 8;
asm ("ldr %w0, [%1]" : "=r"(v) : "r"(addr));
```

**Pitfalls**:
- Carrying over a complex `"m"` operand often compiles successfully but emits a sub-optimal multi-instruction address materialization; benchmark before assuming the compiler handled it.

**Validation**:
- All `"m"` operands in arm64 path correspond to simple base-or-base+offset addresses.
- Complex pointer arithmetic is computed into a `"r"`-bound variable before the asm block.

## Compiler Barrier

**x64**: `asm volatile ("" ::: "memory")` is a compiler barrier (no instruction
emitted; just blocks reordering across the asm).

**ARM64**: Identical syntax works unchanged — the empty template emits no
instruction and `"memory"` constrains the optimizer.

**Workaround**: No change required.

**Pitfalls**:
- A *compiler* barrier is not a *hardware* barrier. If the original x64 code relied on x64's TSO model for ordering between actual loads/stores, arm64 needs `dmb ish` or `ldar`/`stlr` — not just `asm("" ::: "memory")`. See [[memory-model-and-atomics]].

**Validation**:
- Pure compiler-barrier idiom (`asm volatile ("" ::: "memory")`) is the only one that translates verbatim.
- Any asm block that originally relied on x64 strong ordering is reviewed against [[memory-model-and-atomics]].

## GCC Condition-Code Output Constraint

**x64**: `"=@ccz"` (or `"=@cce"` etc.) returns the result of an EFLAGS test as
a boolean output of the asm.

**ARM64**: Same constraint family but spelled with arm64 condition codes:
`"=@cceq"`, `"=@ccne"`, `"=@cclo"`, `"=@cchs"`, etc.

**Workaround**:
```c
// x64
int eq;
asm ("cmp %1, %2" : "=@ccz"(eq) : "r"(a), "r"(b) : "cc");

// arm64
int eq;
asm ("cmp %x1, %x2" : "=@cceq"(eq) : "r"(a), "r"(b) : "cc");
```

**Pitfalls**:
- Carry-flag conditions invert: x64 `jb` = unsigned `<` matches arm64 `b.lo`, **not** `b.cs`. Same applies to `"=@ccc"` → `"=@cclo"`, not `"=@cccs"`. See [[flags-and-conditions]].

**Validation**:
- All `"=@cc<x>"` constraints in arm64 path use arm64 condition codes (`eq`, `ne`, `lo`, `hs`, `lt`, `ge`, `le`, `gt`, `mi`, `pl`, `vs`, `vc`).
