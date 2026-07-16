# EFLAGS / NZCV and Conditional Branches

x64 sets EFLAGS implicitly on most arithmetic; arm64 sets NZCV only when the
instruction carries the `S` suffix. Carry-flag polarity differs after `cmp`,
several x64 flags have no arm64 equivalent, and condition mnemonics differ.

## S-Suffix Discipline

**x64**: `add` / `sub` / `and` / `or` / `xor` set EFLAGS automatically. Every
arithmetic instruction is implicitly flag-setting.

**ARM64**: Plain `add` / `sub` / `and` do **not** set NZCV. Use the S-suffix
form (`adds`, `subs`, `ands`) to update flags. `cmp` is `subs xzr, ...`,
`tst` is `ands xzr, ...`. There are no S-suffix forms of `orr`, `eor`, `mvn`,
`bic`, `orn`.

**Workaround**: For each x86 instruction whose flag side-effect is read by a
subsequent `jcc`, use the S-suffix form. Otherwise prefer the non-S form to
avoid a false dependency on NZCV.

```asm
; x64
sub rax, 1
jnz .loop

; arm64 — WRONG (flags stale)
sub x0, x0, #1
b.ne .loop

; arm64 — CORRECT
subs x0, x0, #1
b.ne .loop
```

**Pitfalls**:
- Defaulting to non-S form silently breaks any branch that reads flags. Defaulting to S form costs a stall and creates a false flag dependency.
- `or`/`xor`/`bic`/`orn` have no S-form. If the original code did `or rax, rbx; jz .L`, the arm64 port must add a separate `cbz` or `cmp ...,#0` after the `orr`.

**Validation**:
- Every `b.<cc>` reads flags set by an immediately preceding S-suffix instruction or `cmp`/`tst`.
- No `b.<cc>` follows a non-flag-setting `add`/`sub`/`and`/etc.
- `orr`/`eor`/etc. that need to set flags are followed by an explicit `cmp` or `cbz`/`cbnz`.

## EFLAGS → NZCV Direct Equivalents

**x64 → arm64 flag mapping**:

| Flag | x64 | arm64 | Notes |
|--|--|--|--|
| Negative | SF | N | Direct equivalent |
| Zero | ZF | Z | Direct equivalent |
| Carry | CF | C | **Inverted polarity after `cmp`** (see below) |
| Overflow | OF | V | Direct equivalent |
| Aux carry | AF | — | No arm64 equivalent (BCD only on x64) |
| Direction | DF | — | No arm64 equivalent (no string instructions) |
| Parity | PF | — | No arm64 equivalent (rarely used) |

**Workaround**: Code that reads AF/DF/PF cannot be directly ported.

- `DF` was used by `rep movs`/`scas` direction control — no string instructions on arm64, so any `std`/`cld` plus string-op pattern must be rewritten as a `ldr`/`str` loop with explicit pointer arithmetic.
- `AF` was used for BCD arithmetic (`daa`, `aaa`, etc.) — none exist on arm64.
- `PF` (parity) — replace with explicit parity calculation if needed: `eor` chained reduction or table lookup.

**Pitfalls**:
- Code that uses `pushfq` to save EFLAGS and `popfq` to restore: there is no equivalent `mrs nzcv` ... `msr nzcv` direct save/restore in user mode that preserves *every* flag bit. Most code only needs N/Z/C/V; if the original code relied on AF/DF/PF, the port is impossible without algorithmic rewrite.

**Validation**:
- No reads of AF/DF/PF remain (no `setp`, `seta`-via-AF, `std`/`cld`).
- `pushfq`/`popfq` patterns are reviewed for required flag bits.

## Carry-Flag Polarity After `cmp`

**x64**: After `cmp a, b`, `CF=1` iff unsigned `a < b`. So `jb` (jump if
below) tests `CF=1`.

**ARM64**: After `cmp x0, x1`, `C=1` iff unsigned `x0 >= x1` — opposite
polarity from x64. So unsigned-below test is `b.lo` (which reads `C=0`),
**not** `b.cs` / `b.cc` interpreted with x64 semantics.

| Test | x64 | arm64 | NZCV reading |
|--|--|--|--|
| Unsigned `<` | `jb` / `jc` | `b.lo` (= `b.cc`) | `C=0` |
| Unsigned `>=` | `jae` / `jnc` | `b.hs` (= `b.cs`) | `C=1` |
| Unsigned `<=` | `jbe` | `b.ls` | `C=0 \|\| Z=1` |
| Unsigned `>` | `ja` | `b.hi` | `C=1 && Z=0` |

**Workaround**: Use the named arm64 conditions (`lo`, `hs`, `ls`, `hi`) which
encode the *unsigned comparison result* directly. Avoid using `cs`/`cc` with
x64-mental-model carry semantics — `cs`/`cc` are aliases for `hs`/`lo`, but
when the code is doing `sbc`/`adc` rather than comparison, the polarity matters.

**Pitfalls**:
- `sbb rax, rbx` (subtract with borrow) → `sbcs x0, x0, x1`: the C flag bit polarity differs (arm64 C=1 means *no* borrow). If asm reads the flag back as a value, negate via `cset`+`sub`.

**Validation**:
- Unsigned comparison branches use `b.lo` / `b.hs` / `b.ls` / `b.hi` — not `b.cs` / `b.cc`.
- Multi-precision subtract chains (`sbcs`) account for inverted carry polarity.

## Conditional Branch Mnemonic Table

| x64 | arm64 | Meaning |
|--|--|--|
| `je` / `jz` | `b.eq` | equal / zero |
| `jne` / `jnz` | `b.ne` | not equal / not zero |
| `jl` / `jnge` | `b.lt` | signed `<` |
| `jle` / `jng` | `b.le` | signed `<=` |
| `jg` / `jnle` | `b.gt` | signed `>` |
| `jge` / `jnl` | `b.ge` | signed `>=` |
| `jb` / `jc` / `jnae` | `b.lo` | unsigned `<` |
| `jbe` / `jna` | `b.ls` | unsigned `<=` |
| `ja` / `jnbe` | `b.hi` | unsigned `>` |
| `jae` / `jnc` / `jnb` | `b.hs` | unsigned `>=` |
| `js` | `b.mi` | negative |
| `jns` | `b.pl` | non-negative |
| `jo` | `b.vs` | overflow |
| `jno` | `b.vc` | no overflow |
| `loop` | `subs xN, xN, #1` + `b.ne` | decrement-and-branch |
| `jecxz` / `jrcxz` | `cbz wN`/`xN` | compare-and-branch zero (no flags) |

**Workaround**: For the `loop` instruction specifically, prefer
`subs`+`b.ne`. For zero-equality branches against a register, prefer the
flag-free `cbz`/`cbnz` — they are cheaper and avoid an unnecessary NZCV write.

**Pitfalls**:
- `cbz`/`cbnz` only test for zero — they do not set NZCV. They cannot be substituted for `jz`/`jnz` if a *later* conditional instruction depends on the same flags.

**Validation**:
- All `j<cc>` mnemonics are translated to the corresponding `b.<cc>` form.
- `loop` is rewritten as `subs`+`b.ne`.
- Plain register-vs-zero branches use `cbz`/`cbnz` where possible.

## Conditional Move (CMOV) → CSEL / CSET

**x64**: `cmov<cc> dst, src` — two-operand: `if (cc) dst = src; else dst unchanged`.

**ARM64**: `csel dst, src_true, src_false, cc` — three-operand: dest is one of
two source registers based on cc. `cset dst, cc` sets dst to 0 or 1 based on cc.
`csinc`, `csinv`, `csneg` are conditional-then-modify forms.

**Workaround**: To collapse a 3-op `csel` back into a 2-op `cmov`-shaped form
(useful for diff-locality with the x64 source), write `csel \dst, \src_true,
\dst, \cc` — this fixes `src_false = dst`, matching the cmov semantic.
Genuine three-way selects must call `csel` directly.

```asm
; x64
cmovz rax, rbx
; arm64
csel x0, x1, x0, eq

; setb dl  →  cset w3, lo
```

**Pitfalls**:
- `cmov<cc>` source can be a memory operand on x64; `csel` cannot. If the original used `cmovz rax, [rcx]`, port as a load + csel.

**Validation**:
- All `cmov<cc>` translate to `csel` (or `cset` for set-to-bool patterns).
- `cmov` from memory is rewritten as `ldr` + `csel`.

## CSDB — Speculation Barrier (Spectre `lfence`)

**x64**: `lfence` is sometimes used as a speculation barrier (Spectre v1
mitigation), distinct from its memory-ordering use.

**ARM64**: Use `csdb` (Consumption of Speculative Data Barrier) for
speculation-barrier semantics — *not* `dmb ishld` (which is a load-load memory
barrier, not a speculation barrier).

**Workaround**: Identify the *intent* of `lfence` first:
- Memory ordering between loads → `dmb ishld` (see [[memory-model-and-atomics]]).
- Spectre / speculation barrier → `csdb`.

**Pitfalls**:
- Using `dmb ishld` where `csdb` is needed leaves the speculation gadget intact — the mitigation silently fails.

**Validation**:
- Every `lfence` translation is annotated with intent (memory order vs speculation barrier).
- Spectre-mitigation uses of `lfence` map to `csdb`.

## See Also

- [[register-and-abi]] for which registers participate in flag operations.
- [[memory-model-and-atomics]] for memory-ordering barriers vs speculation barriers.
- [[inline-asm-constraints]] for `"cc"` clobber and `=@cc<x>` flag-output constraints.
