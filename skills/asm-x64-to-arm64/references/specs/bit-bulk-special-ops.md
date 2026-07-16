# Bit Manipulation, Bulk Memory, and Special Instructions

Translation rules for instructions that don't fit into the basic
arithmetic/branch/memory categories: bit manipulation (BSR/BSF/POPCNT),
shift-double, byte swap, string/REP operations, cache management,
prefetching, timestamp, CPU pause/yield, and CPUID-style identification.

## Bit Scan and Count

**x64**: `bsr` (bit scan reverse — find highest set bit), `bsf` (bit scan
forward — find lowest set bit), `popcnt`, `lzcnt`, `tzcnt`.

**ARM64**: `clz` (count leading zeros) is the building block; combine with
`rbit` (reverse bits) and `eor` to derive the others.

| x64 | arm64 | Notes |
|---|---|---|
| `bsr reg, src` | `clz x0, x1` then `eor x0, x0, #63` | bsr returns bit *index*, not count |
| `bsf reg, src` | `rbit x0, x1; clz x0, x0` | rbit + clz = count trailing zeros |
| `popcnt reg, src` | `fmov d0, x1; cnt v0.8b, v0.8b; addv b0, v0.8b; fmov x0, d0` | NEON path; no scalar popcount |
| `lzcnt reg, src` | `clz x0, x1` | direct equivalent |
| `tzcnt reg, src` | `rbit x0, x1; clz x0, x0` | direct equivalent |

**Workaround**: Watch for the **zero-input** behavior difference:
- x64 `bsr`/`bsf` are **undefined** when src=0 (ZF=1 indicates the case).
- arm64 `clz` returns the register width (64 or 32) for zero input.
- x64 `lzcnt`/`tzcnt` are **defined** for zero (return register width).

If the original `bsr`/`bsf` code did not guard against zero input, the arm64
port behaves *better-defined* — but if the surrounding code reads ZF
post-`bsr` to detect the zero case, that branch must be rewritten as an
explicit `cbz` before the `clz`.

**Pitfalls**:
- `popcnt` requires a NEON detour: GP → SIMD via `fmov`, byte-popcount with `cnt`, lane-sum with `addv`, SIMD → GP via `fmov`. Cheaper to call `__builtin_popcountll` and let the compiler emit the same sequence.

**Validation**:
- All `bsr`/`bsf` translations check whether the original code relied on the zero-input UB or the ZF check.
- `popcnt` translations use the NEON sequence or invoke a builtin.

## Bit Test (BT / BTR / BTS / BTC)

**x64**: `bt reg, imm` (test bit), `btr` (reset), `bts` (set), `btc`
(complement). Single instruction, sets CF.

**ARM64**: `tbz` / `tbnz` (test-bit-and-branch) is the most efficient form
for the common `bt + jc` pattern. For bit-set/clear/complement: `orr` /
`bic` / `eor` with an immediate mask.

**Workaround**:
| x64 pattern | arm64 |
|---|---|
| `bt rax, 5; jc .L` | `tbnz x0, #5, .L` |
| `bt rax, 5; jnc .L` | `tbz x0, #5, .L` |
| `bts rax, 5` | `orr x0, x0, #(1<<5)` |
| `btr rax, 5` | `and x0, x0, #~(1<<5)` (or `bic`) |
| `btc rax, 5` | `eor x0, x0, #(1<<5)` |
| `bt rax, rbx` | `lsr x2, x0, x1; and x2, x2, #1` (variable position) |

**Pitfalls**:
- `tbz`/`tbnz` branch range is small (±32 KB) compared to `b.<cc>` (±1 MB). For long-range targets, fall back to `tst`+`b.<cc>`.

**Validation**:
- `bt + j<cc>` patterns prefer `tbz`/`tbnz` when target is in range.
- `bts`/`btr`/`btc` translations use immediate-mask `orr`/`and`/`eor`.

## Shift-Double (SHRD / SHLD)

**x64**: `shrd rax, rdx, cl` shifts `rdx:rax` right by `cl`, writes to `rax`.
`shld` is the left-shift dual.

**ARM64**: `extr xN, xH, xL, #imm` extracts a 64-bit value from a 128-bit
concatenation `xH:xL` shifted by immediate. For variable shift count, no
single-instruction form — use `lsr` + `lsl` + `orr` triplet.

**Workaround**:
```asm
; x64
shrd rax, rdx, #8

; arm64 (immediate shift)
extr x0, x1, x0, #8
```

**Pitfalls**:
- `extr` shift amount is immediate only; variable count needs explicit triplet:
  ```
  lsr  x2, x0, x4    ; lower part shifted right
  neg  w5, w4
  lsl  x3, x1, x5    ; upper part shifted left by (64-count)
  orr  x0, x2, x3
  ```
- The `neg w5, w4` trick computes `(64 - count) mod 64`. If count is 0, this
  produces 64, and `lsl ... #64` is undefined — guard against count=0.

**Validation**:
- Immediate-count `shrd`/`shld` translations use `extr`.
- Variable-count translations include count=0 guard.

## Byte Swap (BSWAP / MOVBE)

**x64**: `bswap reg` reverses byte order in a register. `movbe` reads from
memory with byte swap (BE↔LE conversion in one instruction).

**ARM64**: `rev` reverses bytes in a 64-bit register; `rev32` reverses bytes
within each 32-bit half; `rev16` within each 16-bit half. There is no
combined load+swap — use `ldr` + `rev`.

**Workaround**:
| x64 | arm64 |
|---|---|
| `bswap rax` | `rev x0, x0` |
| `bswap eax` | `rev w0, w0` |
| `movbe rax, [rsi]` | `ldr x0, [x1]; rev x0, x0` |
| `movbe [rsi], rax` | `rev x0, x0; str x0, [x1]` (clobbers x0; use temp) |

**Pitfalls**:
- arm64 has no scalar `bswap16` for 16-bit values — use `rev16 w0, w0` (which reverses bytes in each 16-bit lane within the register; sometimes called `bswap16`).

**Validation**:
- `bswap` translates to `rev` (64-bit) or `rev w0, w0` (32-bit).
- `movbe` decomposes into separate load/swap or swap/store sequence.

## String / REP Operations

**x64**: `rep movsb` (memcpy), `rep stosb` (memset), `rep movsd`/`movsq`
(word/qword copy). Uses `RSI`/`RDI` as src/dst pointers, `RCX` as count, `DF`
flag controls direction.

**ARM64**: No string instructions. Translate to:
- Library call (`memcpy`, `memset`) — best for large or unknown-length copies.
- Unrolled `ldp`/`stp` loop — for known-length, performance-critical hot paths.

**Workaround**:
| x64 | arm64 |
|---|---|
| `rep movsb` | `memcpy` call, or `ldr w0, [x1], #4; str w0, [x2], #4; subs x3, x3, #1; b.ne .L` |
| `rep stosb` | `memset` call, or `str xzr, [x1], #16; subs x2, x2, #1; b.ne .L` (16 bytes/iter) |
| `rep movsq` | `ldp x0, x1, [x2], #16; stp x0, x1, [x3], #16; subs x4, x4, #1; b.ne .L` |

**Pitfalls**:
- arm64 has no equivalent of `DF` (direction flag); reverse-direction copies
  (`std; rep movs`) must be open-coded with explicit pointer decrement.
- `rep stosb` zeroes — `str xzr, [...]` is cheaper than loading a zero into a register first.

**Validation**:
- All `rep` instructions translate to either a library call or an explicit unrolled loop.
- Reverse-direction copies use explicit decrement.

## Cache Management

**x64**: `clflush`, `clflushopt` (weaker), `clwb`.

**ARM64**: `dc civac` (clean+invalidate by VA to PoC), `dc cvac` (clean to
PoC), `dc cvap` (clean to PoP, ARMv8.2+). Cache-management instructions
operate on cache-line-sized regions; loop over an address range if needed.

**Workaround**:
| x64 | arm64 |
|---|---|
| `clflush [mem]` | `dc civac, x0` |
| `clflushopt [mem]` | `dc cvac, x0` (weaker than clflush — clean only, no invalidate) |
| `clwb [mem]` | `dc cvap, x0` (ARMv8.2+) |

After cache maintenance, often need a `dsb ish` to ensure completion before
subsequent dependent operations.

**Pitfalls**:
- arm64 cache-management instructions sometimes require kernel privilege depending on `SCTLR_EL1` — verify the target supports user-space cache ops.

**Validation**:
- Cache flushes are followed by `dsb ish` if completion is required before subsequent operations.

## Prefetch

**x64**: `prefetchnta` (non-temporal), `prefetcht0`/`t1`/`t2` (L1/L2/L3).

**ARM64**: `prfm <op>, [xN]` where `<op>` encodes target level and locality:
`pldl1keep`, `pldl2keep`, `pldl3keep`, `pldl1strm` (streaming = non-temporal),
`pstl1keep` for store prefetch.

**Workaround**:
| x64 | arm64 |
|---|---|
| `prefetchnta [mem]` | `prfm pldl1strm, [x0]` |
| `prefetcht0 [mem]` | `prfm pldl1keep, [x0]` |
| `prefetcht1 [mem]` | `prfm pldl2keep, [x0]` |
| `prefetcht2 [mem]` | `prfm pldl3keep, [x0]` |

**Pitfalls**:
- Some arm64 implementations treat all `prfm` variants as no-ops; performance impact varies. Benchmark before assuming the prefetch is doing work.

**Validation**:
- All `prefetch*` translate to `prfm` with appropriate cache-level encoding.

## Timestamp / CPU ID

**x64**: `rdtsc` (timestamp via EDX:EAX), `rdtscp` (also writes ECX = CPU ID
on Linux).

**ARM64**: `mrs x0, cntvct_el0` (virtual timer counter — the canonical TSC
analog). For CPU ID: `mrs x0, tpidr_el0` (Linux convention) or
`mrs x0, tpidrro_el0` (Apple).

**Workaround**:
```asm
; x64 timestamp
rdtsc                  ; EDX:EAX
shl  rdx, 32
or   rax, rdx

; arm64 timestamp
mrs  x0, cntvct_el0    ; single 64-bit value
```

**Pitfalls**:
- `cntvct_el0` is virtual time (paravirtualized for VMs); does not always have
  the same nanosecond granularity as TSC. The frequency is in `cntfrq_el0`.
- `pause` (x64 spin-loop hint) → `yield`. Distinct from `wfe` (wait-for-event,
  which suspends the CPU and is heavier).

**Validation**:
- `rdtsc`/`rdtscp` translate to `mrs cntvct_el0`.
- `pause` translates to `yield`.

## CPUID

**x64**: `cpuid` (with EAX as leaf number) returns CPU feature flags in
EAX/EBX/ECX/EDX. Fundamental for runtime feature dispatch.

**ARM64**: No direct user-space equivalent. `mrs midr_el1` gives the main ID
register but is **EL1-only** (kernel mode). User-space code must use OS APIs:
- Linux: `getauxval(AT_HWCAP)` / `AT_HWCAP2`.
- Apple: `sysctlbyname("hw.optional.arm.<feature>", ...)`.
- Windows: `IsProcessorFeaturePresent(...)`.

**Workaround**: Replace inline `cpuid` checks with OS-API calls on init, and
cache results in globals.

**Pitfalls**:
- The asm code must not contain `cpuid` directly; it must be wrapped behind a
  C-level feature-detection function compiled separately.

**Validation**:
- No `cpuid` instruction remains in arm64 path.
- Feature detection is delegated to OS API calls performed in C.

## See Also

- [[register-and-abi]] for `tpidr_el0` TLS access pattern.
- [[memory-model-and-atomics]] for `dmb`/`dsb` distinctions.
- [[flags-and-conditions]] for `tbz`/`tbnz` branch range vs `b.<cc>`.
