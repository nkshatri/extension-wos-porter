#!/usr/bin/env python3
"""
x86-to-ARM64 Migration Auditor

Scans C++ source files for x86/x64 SIMD patterns that need to be ported
to ARM64 NEON, and flags ARM64 guard mistakes. Grounded in the real
patterns from the Microsoft STL's vector_algorithms.cpp.

Usage:
    python audit_migration.py <file_or_directory> [--arm64-only] [--fix-guards]

Examples:
    python audit_migration.py stl/src/vector_algorithms.cpp
    python audit_migration.py stl/src/ --arm64-only
"""

import sys
import re
from pathlib import Path
from dataclasses import dataclass, field
from typing import List


@dataclass
class Finding:
    line_no: int
    line: str
    category: str
    severity: str   # ERROR / WARNING / INFO
    message: str
    suggestion: str = ""


# ── Rule definitions ──────────────────────────────────────────────────────────

# Patterns that are x64-only and must NOT appear in ARM64 code paths
X64_ONLY_PATTERNS = [
    (r'\b__isa_enabled\b',
     "ERROR", "__isa_enabled is x64/x86-only (vcruntime internal)",
     "Use IsProcessorFeaturePresent(PF_ARM_*) for ARM64 feature detection"),

    (r'\b_Use_avx2\s*\(\)',
     "ERROR", "_Use_avx2() is x64-only",
     "Use _Use_FEAT_SVE() or remove (baseline NEON needs no check)"),

    (r'\b_Use_sse42\s*\(\)',
     "ERROR", "_Use_sse42() is x64-only",
     "Baseline NEON is always available; no runtime check needed"),

    (r'\b_Zeroupper_on_exit\b',
     "ERROR", "_Zeroupper_on_exit is x64-only (AVX upper-half cleanup)",
     "Remove entirely — NEON has no upper-half contamination"),

    (r'\b_mm256_zeroupper\s*\(\)',
     "ERROR", "_mm256_zeroupper() is x64-only",
     "Remove entirely — not needed on ARM64"),

    (r'\b_Avx2_tail_mask_32\b',
     "ERROR", "_Avx2_tail_mask_32 is x64-only (AVX2 masked tail)",
     "Replace with descending-granularity if-chain (64→32→16→8→4→scalar)"),

    (r'\b_mm256_loadu_si256\b|\b_mm256_storeu_si256\b',
     "ERROR", "AVX2 256-bit load/store — no ARM64 equivalent",
     "Replace with 2-4 x vld1q_u8/vst1q_u8 (128-bit each)"),

    (r'\b_mm_loadu_si128\b|\b_mm_storeu_si128\b',
     "WARNING", "SSE 128-bit load/store",
     "Replace with vld1q_u8/vst1q_u8"),

    (r'\b_mm_cmpeq_epi8\b|\b_mm_cmpeq_epi16\b|\b_mm_cmpeq_epi32\b',
     "WARNING", "SSE compare-equal",
     "Replace with vceqq_u8/vceqq_u16/vceqq_u32"),

    (r'\b_mm_movemask_epi8\b',
     "WARNING", "_mm_movemask_epi8 has no direct NEON equivalent",
     "Use vmaxvq_u8(cmp)!=0 for 'any match', or vshrn_n_u16 for bitmask"),

    (r'\b_mm256_set1_epi8\b|\b_mm_set1_epi8\b',
     "WARNING", "SSE/AVX broadcast",
     "Replace with vdupq_n_u8"),

    (r'\b__m256i\b|\b__m128i\b|\b__m256\b|\b__m128\b',
     "WARNING", "x64 SIMD register type",
     "Replace with uint8x16_t/uint16x8_t/uint32x4_t/float32x4_t etc."),

    (r'#include\s*<intrin\.h>',
     "ERROR", "<intrin.h> is x64/x86-only",
     "Replace with #include <arm64_neon.h> and #include <Windows.h>"),

    (r'#include\s*<isa_availability\.h>',
     "ERROR", "<isa_availability.h> is x64/x86-only",
     "Remove; use IsProcessorFeaturePresent for ARM64 feature detection"),
]

# Guard mistakes — wrong ARM64 preprocessor patterns
GUARD_MISTAKES = [
    # Bare _M_ARM64 without _M_ARM64EC (in #ifdef / #if defined)
    (r'#\s*ifdef\s+_M_ARM64\b(?!EC)',
     "ERROR",
     "#ifdef _M_ARM64 misses ARM64EC",
     "Use: #if defined(_M_ARM64) || defined(_M_ARM64EC)"),

    (r'#\s*if\s+defined\s*\(\s*_M_ARM64\s*\)\s*$',
     "ERROR",
     "#if defined(_M_ARM64) alone misses ARM64EC",
     "Use: #if defined(_M_ARM64) || defined(_M_ARM64EC)"),

    # Bare !_M_ARM64 without !_M_ARM64EC
    (r'#\s*ifndef\s+_M_ARM64\b(?!EC)',
     "WARNING",
     "#ifndef _M_ARM64 may miss ARM64EC (unless this is the ABI shim)",
     "Use: #if !defined(_M_ARM64) && !defined(_M_ARM64EC)  (unless intentional ABI shim)"),

    (r'#\s*if\s+!defined\s*\(\s*_M_ARM64\s*\)\s*$',
     "WARNING",
     "#if !defined(_M_ARM64) alone may miss ARM64EC",
     "Use: #if !defined(_M_ARM64) && !defined(_M_ARM64EC)"),
]

# ARM64-specific UB patterns
ARM64_UB_PATTERNS = [
    (r'\*static_cast<uint32_t\s*\*>',
     "WARNING",
     "Direct uint32_t* dereference may be UB for unaligned pointers on ARM64",
     "Use vld1_lane_u32(ptr, vdup_n_u32(0), 0) for safe unaligned 4-byte load"),

    (r'\*static_cast<uint16_t\s*\*>',
     "WARNING",
     "Direct uint16_t* dereference may be UB for unaligned pointers on ARM64",
     "Use vld1_lane_u16(ptr, vdup_n_u16(0), 0) for safe unaligned 2-byte load"),
]


def audit_file(path: Path, arm64_only: bool = False) -> List[Finding]:
    findings: List[Finding] = []
    lines = path.read_text(encoding='utf-8', errors='replace').splitlines()

    # Track whether we're inside an ARM64 guard block (simplified heuristic)
    in_arm64_block = False
    in_x64_block = False
    guard_depth = 0

    for i, line in enumerate(lines, 1):
        stripped = line.strip()

        # Track guard blocks (simplified — doesn't handle nested #if perfectly)
        if re.search(r'defined\(_M_ARM64\)\s*\|\|\s*defined\(_M_ARM64EC\)', stripped):
            in_arm64_block = True
            in_x64_block = False
        elif re.search(r'!defined\(_M_ARM64\)\s*&&\s*!defined\(_M_ARM64EC\)', stripped):
            in_x64_block = True
            in_arm64_block = False
        elif stripped.startswith('#else'):
            in_arm64_block, in_x64_block = in_x64_block, in_arm64_block
        elif stripped.startswith('#endif'):
            in_arm64_block = False
            in_x64_block = False

        # Check guard mistakes (always, regardless of block)
        for pattern, severity, message, suggestion in GUARD_MISTAKES:
            if re.search(pattern, stripped):
                findings.append(Finding(i, line.rstrip(), "GUARD", severity, message, suggestion))

        # Check x64-only patterns inside ARM64 blocks
        if in_arm64_block or (not arm64_only):
            for pattern, severity, message, suggestion in X64_ONLY_PATTERNS:
                if re.search(pattern, stripped):
                    ctx = " [inside ARM64 block]" if in_arm64_block else ""
                    findings.append(Finding(i, line.rstrip(), "X64_PATTERN",
                                            "ERROR" if in_arm64_block else severity,
                                            message + ctx, suggestion))

        # Check ARM64 UB patterns
        for pattern, severity, message, suggestion in ARM64_UB_PATTERNS:
            if re.search(pattern, stripped):
                findings.append(Finding(i, line.rstrip(), "ARM64_UB", severity, message, suggestion))

    return findings


def print_findings(path: Path, findings: List[Finding]) -> int:
    if not findings:
        return 0

    errors = sum(1 for f in findings if f.severity == "ERROR")
    warnings = sum(1 for f in findings if f.severity == "WARNING")

    print(f"\n{'='*70}")
    print(f"File: {path}")
    print(f"  {errors} error(s), {warnings} warning(s)")
    print(f"{'='*70}")

    for f in findings:
        icon = "❌" if f.severity == "ERROR" else "⚠️ " if f.severity == "WARNING" else "ℹ️ "
        print(f"\n  {icon} Line {f.line_no}: [{f.category}] {f.message}")
        print(f"     Code: {f.line.strip()[:100]}")
        if f.suggestion:
            print(f"     Fix:  {f.suggestion}")

    return errors


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    target = Path(sys.argv[1])
    arm64_only = "--arm64-only" in sys.argv

    if target.is_file():
        files = [target]
    elif target.is_dir():
        files = list(target.rglob("*.cpp")) + list(target.rglob("*.h"))
    else:
        print(f"Error: {target} not found")
        sys.exit(1)

    total_errors = 0
    total_files_with_issues = 0

    for f in sorted(files):
        findings = audit_file(f, arm64_only)
        if findings:
            total_errors += print_findings(f, findings)
            total_files_with_issues += 1

    print(f"\n{'='*70}")
    print(f"Summary: {total_files_with_issues} file(s) with issues, {total_errors} error(s) total")
    print(f"{'='*70}\n")

    sys.exit(1 if total_errors > 0 else 0)


if __name__ == "__main__":
    main()
