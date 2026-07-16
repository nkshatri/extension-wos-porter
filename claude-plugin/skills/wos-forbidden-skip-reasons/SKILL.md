---
name: wos-forbidden-skip-reasons
description: "Canonical list of forbidden vs valid skip reasons for ARM64 SIMD porting, plus the PowerShell regex audit block used by wos-porter Phase 8 gate G7c and wos-optimizer Hard Constraints. Load when auditing an optimizer report, when the optimizer decides whether to skip a Tier-S file, or when writing the Limitations section of ARM64-PORT.md."
---

# Forbidden Skip Reasons â€” Tier-S x86 SIMD Files

Using any pattern in the FORBIDDEN list as the reason to skip an x86-SIMD-heavy translation unit is an automatic invocation failure. The `wos-optimizer` MUST either hand-port the file with baseline ARMv8.0 NEON intrinsics and let the per-kernel benchmark gate decide, or cite a VALID reason with concrete evidence.

## FORBIDDEN reasons (each forces re-invocation)

1. **Size / effort claims** â€” "would require N LOC", "too large to hand-port", "no NEON port attempted", "non-trivial port". Break the file into kernels and hand-port the top-K hottest until budget is hit; defer the cold remainder with `budget exhausted after kernels <X>, <Y>, <Z>`.
2. **Popularity / usage / age claims** â€” "rarely used", "not benchmarked by upstream", "academic only", "niche", "legacy", "obscure", "deprecated by upstream". The optimizer does not decide which code users care about.
3. **Optional-ISA-extension unavailability alone** â€” "MSVC does not auto-define `__ARM_FEATURE_X`", "target SoC lacks X", "baseline ARMv8.0 doesn't require X". This is a reason to skip the *optional* intrinsic, not the file. Attempt a baseline ARMv8.0 port; let the benchmark gate decide.
4. **Unmeasured "fast enough"** â€” "default path is fast", "scalar fallback is fine/adequate", "existing path is sufficient". Without a measurement against `benchmarks/base_bench_win_arm.*`, this is unsupported. Measure or port.
5. **Scope / deferral non-reasons** â€” "could be ported â€” deferred", "out of scope", "opportunistic-only", "future work", "left as a follow-up".
6. **Unsubstantiated duplication** â€” "alternate implementation exists", "sibling provides equivalent path" â€” without naming the sibling AND citing its measured throughput.

## VALID reasons (each requires concrete evidence)

- `build-failed: <compiler error line, verbatim>` â€” per-kernel build broke; `git checkout --` restored scalar.
- `test-failed: <test name + failure mode>` or `diff-harness-mismatch: max-delta=<N>, tolerance=<M>` â€” correctness regression; reverted.
- `per-kernel gate: scalar faster <pct>% (scalar=<N>, neon=<M>, median of 3 runs)` â€” benchmark gate measured NEON slower; reverted.
- `vendored upstream <path>: <citation forbidding local changes>` â€” third-party pinned code under `third_party/` / `vendor/` / `extern/` / `external/`, with the README/UPDATING line quoted.
- `budget exhausted: deferred to next invocation after kernels <X>, <Y>, <Z>` â€” used ONLY when the wall-clock cap was actually hit AND the named kernels were committed this invocation.
- `duplicate of <other_file>: NEON path already provided by sibling at <perf number from benchmark file>` â€” must name the sibling AND cite measured throughput.

## Audit regex â€” `$forbiddenPatterns`

Use these patterns to scan any optimizer report or `ARM64-PORT.md` for forbidden justifications:

```powershell
$forbiddenPatterns = @(
    # Size / effort
    'would require .* LOC',
    'no NEON port attempted',
    'too large to hand-port',
    'non-trivial port',
    # Popularity / usage / age
    '\brarely used\b',
    'not benchmarked by upstream',
    '\bacademic only\b',
    '\bniche\b',
    '\blegacy\b',
    '\bobscure\b',
    'deprecated by upstream',
    # Optional-ISA-extension unavailability alone
    'MSVC does not (auto-)?define\s+__ARM_FEATURE_',
    '__ARM_FEATURE_\w+ not (set|defined|available)',
    'target (CPU|SoC) does not implement',
    'baseline ARMv8\.0 .* does not require',
    # Unmeasured "fast enough"
    'default .* path is .* fast',
    'scalar fallback is (fine|fast|adequate|sufficient)',
    'existing path is sufficient',
    # Scope / deferral non-reasons
    'could be ported.*deferred',
    'out of (scope|opportunistic scope)',
    'opportunistic[- ]only',
    '\bfuture work\b',
    'left as a follow[- ]up',
    # Unsubstantiated duplication
    'alternate .* implementation',
    'sibling provides equivalent'
)
```

## Usage snippet (drop into the calling agent / gate)

```powershell
$artifactsToScan = @($optimizerReport)
if (Test-Path 'ARM64-PORT.md') { $artifactsToScan += (Get-Content 'ARM64-PORT.md' -Raw) }
$offending = @()
foreach ($text in $artifactsToScan) {
    foreach ($p in $forbiddenPatterns) {
        $m = Select-String -InputObject $text -Pattern $p -AllMatches
        foreach ($hit in $m.Matches) { $offending += @{ Pattern = $p; Line = $hit.Value } }
    }
}
if ($offending) {
    Write-Host "FORBIDDEN skip reasons detected â€” re-invoke wos-optimizer" -ForegroundColor Yellow
    $offending | ForEach-Object { Write-Host "  - matches '$($_.Pattern)': $($_.Line)" }
}
```

On a non-empty result, re-invoke `wos-optimizer` ONCE with a prompt that names every offending file and its forbidden pattern, instructing it to either hand-port with baseline ARMv8.0 NEON, or cite a VALID reason from the list above with concrete evidence.
