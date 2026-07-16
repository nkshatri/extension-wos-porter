# Output templates

Use this reference when the user explicitly wants ready-to-paste wording such as a diagnosis summary, commit message, issue description, PR description, or review comment.

## Short diagnosis

```text
Symptom:
JIT-generated code in an ARM64EC process is being treated as X64 code or otherwise misclassified.

Root cause:
The executable JIT memory was allocated with VirtualAlloc/VirtualAllocEx instead of VirtualAlloc2 with MEM_EXTENDED_PARAMETER_EC_CODE.

Fix:
Allocate ARM64EC JIT executable pages with VirtualAlloc2 and pass MEM_EXTENDED_PARAMETER_EC_CODE.
```

## Commit message

```text
Fix ARM64EC JIT executable allocation to avoid X64 code misclassification

ARM64EC JIT executable memory was being allocated with VirtualAlloc, which
does not provide the required EC-specific allocation attributes.

Switch the ARM64EC JIT executable allocation path to VirtualAlloc2 and pass
MEM_EXTENDED_PARAMETER_EC_CODE so JIT-generated ARM64/ARM64EC code is
classified correctly.
```

## Issue or PR description

```text
Issue:
JIT-generated ARM64/ARM64EC code in an ARM64EC process may be misclassified
when its executable memory is allocated through VirtualAlloc/VirtualAllocEx.

Root cause:
ARM64EC JIT executable pages require VirtualAlloc2 with
MEM_EXTENDED_PARAMETER_EC_CODE. The previous allocation path did not provide
the required EC code attribute.

Fix:
Use VirtualAlloc2 for the ARM64EC JIT executable allocation path and pass
MEM_EXTENDED_PARAMETER_EC_CODE via MEM_EXTENDED_PARAMETER.
```

## Review comment template

```text
This executable JIT allocation path appears to be ARM64EC-specific, but it is
still using VirtualAlloc/VirtualAllocEx. For ARM64EC JIT executable pages, the
allocation should move to VirtualAlloc2 and provide
MEM_EXTENDED_PARAMETER_EC_CODE via MEM_EXTENDED_PARAMETER. Please also verify
that the protection flags and any PAGE_TARGETS_INVALID usage still match the
intended runtime behavior.
```

## Template usage notes

- Prefer the short diagnosis when the user wants a concise technical explanation.
- Prefer the commit message template when the user asks for a git commit summary.
- Prefer the issue / PR description when the user wants report-ready wording.
- Prefer the review comment template when the user is annotating a patch or code review.
- Adapt wording to the user's actual evidence; do not overstate certainty if the code was not shown.