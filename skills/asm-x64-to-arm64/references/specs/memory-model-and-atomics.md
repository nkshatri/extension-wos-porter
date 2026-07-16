# Memory Model, Barriers, and Atomics

x64 is TSO (Total Store Order) — strong, with implicit store-release / load-
acquire on every plain load/store. arm64 is weakly ordered: explicit barriers
or acquire/release variants are required. Atomic RMW changes structure
entirely (named-register RAX dependency vs three explicit operands).

## Plain Load / Store Ordering

**x64**: Every plain `mov` from/to memory has implicit acquire/release ordering
(no reordering across the access from the same thread's perspective; multi-
core ordering follows TSO).

**ARM64**: Plain `ldr`/`str` are weakly ordered — loads and stores can be
reordered by the hardware. Code that relied on x64 TSO must use one of:
- `dmb ish` for a region barrier;
- `ldar`/`stlr` for single-access acquire/release;
- LSE atomics or LL/SC for atomic RMW.

**Workaround**: Identify the *intent* of each x64 ordering reliance:

| Intent | x64 | arm64 |
|---|---|---|
| Single-load acquire | plain `mov` (TSO) | `ldar wN/xN, [xM]` |
| Single-store release | plain `mov` (TSO) | `stlr wN/xN, [xM]` |
| Region barrier | `mfence` | `dmb ish` |
| Load-load barrier | `lfence` | `dmb ishld` |
| Store-store barrier | `sfence` | `dmb ishst` |

**Pitfalls**:
- Defaulting plain `mov`→`ldr`/`str` without reviewing ordering produces a port that works on test (race-free in benchmarks) but breaks under contention.

**Validation**:
- Every concurrent access in the original is reviewed for required ordering.
- Single load/store with acquire/release semantics uses `ldar`/`stlr`, not `dmb` + `ldr`/`str`.

## Memory Barriers

**x64**: `mfence` (full), `lfence` (load), `sfence` (store).

**ARM64**: `dmb ish` (full, inner-shareable), `dmb ishld` (load barrier),
`dmb ishst` (store barrier). Inner-shareable domain (`ish`) is the right
choice for SMP; `sy` (system) is broader but slower.

**Workaround**:
| x64 | arm64 |
|---|---|
| `mfence` | `dmb ish` |
| `lfence` (memory order) | `dmb ishld` |
| `sfence` | `dmb ishst` |
| `lfence` (Spectre barrier) | `csdb` (see [[flags-and-conditions]]) |

**Pitfalls**:
- Misidentifying `lfence` as memory-order when it was meant as speculation barrier (or vice versa) leaves either redundant cost or missing protection.
- For a single load or store that needs acquire/release semantics, prefer `ldar`/`stlr` over `dmb + ldr`/`str` — they are lower overhead.

**Validation**:
- Every `mfence`/`lfence`/`sfence` translation is reviewed against [[flags-and-conditions]] CSDB rule.
- Single-access acquire/release uses `ldar`/`stlr`, not barrier-wrapped plain access.

## Compare-And-Swap (CAS)

**x64**: `lock cmpxchg [mem], reg` — `RAX` is the implicit "expected value";
on success the new value `reg` is written to `[mem]`; on failure `RAX` receives
the actual value at `[mem]`. Single instruction, three implicit operands.

**ARM64**: Two paths:

1. **LSE atomics (ARMv8.1+)**: `casal xN, xM, [xK]` — three **explicit**
   register operands: `xN` is expected (and receives the actual on failure or
   success), `xM` is the new value, `xK` is the address.
2. **LL/SC loop** (baseline): `ldaxr` + `cmp` + `b.ne` + `stlxr` + `b.ne` —
   load-acquire-exclusive, compare, store-release-exclusive, retry on store
   failure.

**Workaround**:
```asm
; x64
mov rax, expected
lock cmpxchg [mem], new_val      ; RAX = old; ZF=1 on success

; arm64 LSE
mov x0, expected
casal x0, new_val, [mem]         ; x0 = old; cmp x0, expected for ZF analog

; arm64 LL/SC
.retry:
    ldaxr  x0, [mem]
    cmp    x0, expected
    b.ne   .fail
    stlxr  w1, new_val, [mem]
    cbnz   w1, .retry
.fail:
```

**Pitfalls**:
- Structurally rewrite the operand constraints — RAX-implicit cannot be
  preserved. In inline asm, replace `"=a"`(rax) with three explicit
  register-tied operands.
- LSE `casal` is ARMv8.1+; check target compiler flags (`-march=armv8.1-a` or
  `+lse` feature flag). Fall back to LL/SC on baseline ARMv8.0.

**Validation**:
- All `lock cmpxchg` translate to `casal` (LSE) or LL/SC loop.
- Inline-asm operand constraints reflect three explicit register operands, not implicit RAX.

## 128-bit CAS

**x64**: `lock cmpxchg16b [mem]` — 128-bit atomic CAS using `RDX:RAX` as
expected and `RCX:RBX` as new.

**ARM64**: `ldxp` + `stxp` LL/SC pair, or `casp` (ARMv8.1 LSE) for 128-bit
atomic CAS using a register pair.

**Workaround**:
```asm
; arm64 LSE
caspal x0, x1, x2, x3, [mem]     ; x0:x1 expected; x2:x3 new
```

**Pitfalls**:
- Register pair operands must be (Xn, Xn+1) consecutive — register allocation
  must respect this.

**Validation**:
- All `lock cmpxchg16b` translate to `casp[al]` or `ldxp`+`stxp` loop.

## Atomic Add / Exchange

**x64**: `lock xadd [mem], reg` (atomic fetch-add); `xchg [mem], reg`
(atomic exchange — implicit `lock` when memory is operand).

**ARM64**: LSE `ldaddal` (atomic fetch-add, full barrier); `swpal` (atomic
exchange). Or LL/SC `ldaxr`+`add`+`stlxr`+retry loop.

**Workaround**:
| x64 | arm64 LSE |
|---|---|
| `lock xadd [mem], rax` | `ldaddal x0, x0, [mem]` |
| `xchg [mem], rax` | `swpal x0, x0, [mem]` |
| `lock add [mem], rax` (no return) | `staddl x0, [mem]` |
| `lock or [mem], rax` | `ldsetal x0, x0, [mem]` |
| `lock and [mem], rax` | `ldclral x0, x0, [mem]` (clears specified bits) |

**Pitfalls**:
- `lock and` translates to `ldclral` which **clears** specified bits — operand semantics inverts. The arm64 source must be the *complement* of what x64 ANDed.
- Suffixes: `a` = acquire, `l` = release, `al` = both. Match the original
  ordering — `xchg` had implicit `lock` (= full SC), so `swpal` is the
  default; if the original was `lock-free` (single-thread temp swap), `swp`
  without suffixes suffices.

**Validation**:
- `lock` prefixes translate to LSE atomics with appropriate `a`/`l`/`al` suffix.
- `lock and` → `ldclral` with complemented mask.

## See Also

- [[flags-and-conditions]] for `csdb` speculation-barrier vs `dmb` memory barrier.
- [[inline-asm-constraints]] for clobber lists when porting `lock`-prefixed inline asm.
- [[memory-addressing]] for addressing-mode constraints on atomic operands.
