---
name: jit-arm64ec-virtualalloc-fix-skill
description: Analyze ARM64EC JIT executable memory allocation bugs where JIT-generated ARM64 or ARM64EC code is allocated with VirtualAlloc/VirtualAllocEx and then misclassified or treated as X64 code. Use this skill whenever the user mentions ARM64EC, JIT code pages, VirtualAlloc vs VirtualAlloc2, MEM_EXTENDED_PARAMETER_EC_CODE, executable page allocation, or wants a bug report, patch explanation, review checklist, or fix guidance for this exact Windows issue.
tags:
  - arm64ec
  - jit
  - virtualalloc
  - virtualalloc2
  - windows
  - memory-allocation
  - debugging
---

# ARM64EC JIT executable allocation fix skill

Use this skill to help the user diagnose, explain, review, or document the ARM64EC JIT allocation issue where executable memory is allocated incorrectly with `VirtualAlloc`/`VirtualAllocEx` instead of `VirtualAlloc2`.

## What this skill should do

When this skill triggers, help the user do one or more of the following:

- explain the bug clearly
- identify the likely root cause
- recommend the correct API and allocation attributes
- review a patch or code snippet
- produce a concise issue description, PR description, commit message, or review comment
- produce a practical review checklist for validating the fix

Stay tightly focused on this ARM64EC executable-page-allocation problem. Do not generalize into unrelated JIT or Windows memory topics unless the user explicitly asks.

## Core diagnosis

Use this reasoning unless code evidence shows a different situation:

### Symptom

In an ARM64EC process, JIT-generated ARM64/ARM64EC code may be misidentified, mishandled, or effectively treated like X64 code because it was placed in executable memory allocated through the wrong API/path.

### Root cause

`VirtualAlloc` and `VirtualAllocEx` are not sufficient for allocating executable memory intended for JIT-generated ARM64EC code in an ARM64EC process.

For ARM64EC JIT code, executable pages should be allocated with `VirtualAlloc2` and the extended parameter `MEM_EXTENDED_PARAMETER_EC_CODE`. Without that EC-specific allocation metadata, the resulting executable memory can be classified incorrectly for the intended execution model.

### Fix direction

Replace the relevant `VirtualAlloc` / `VirtualAllocEx` executable JIT allocation path with `VirtualAlloc2`, and pass a `MEM_EXTENDED_PARAMETER` using `MEM_EXTENDED_PARAMETER_EC_CODE`.

If the user is asking for a patch review, make sure the fix is applied specifically to the executable JIT allocation path for ARM64EC code, not blindly to every allocation call.

## Progressive loading guide

Read extra reference files only when needed:

- read `references/arm64ec-jit-allocation.md` when the user needs deeper API-level explanation, Microsoft-guidance-backed details, parameter explanation, or implementation review details
- read `references/output-templates.md` when the user explicitly wants ready-to-paste wording such as a diagnosis summary, commit message, issue text, PR text, or review comment

Do not load both references by default if the user only needs a short diagnosis.

## Recommended explanation style

Default to concise technical wording.

Structure the response in this order:

1. symptom
2. root cause
3. fix
4. any validation notes

Avoid speculative claims beyond the available evidence. If the user has not shown code, phrase implementation details carefully, for example: “the likely fix is…”, “the affected allocation path is usually…”, or “verify that…”.

## Code review checklist

When helping review or implement the fix, check the following:

- identify every executable-memory allocation path used for ARM64EC JIT code
- confirm whether `VirtualAlloc` or `VirtualAllocEx` is currently used for those executable pages
- replace the ARM64EC JIT executable allocation path with `VirtualAlloc2`
- provide `MEM_EXTENDED_PARAMETER_EC_CODE` through `MEM_EXTENDED_PARAMETER`
- verify the allocation/protection flags match the intended ARM64EC JIT behavior
- preserve any required protection flags such as `PAGE_TARGETS_INVALID` when the existing design or Microsoft guidance requires it
- avoid changing unrelated non-executable allocations unless there is a clear reason
- verify X64 paths and non-ARM64EC JIT paths are not unintentionally regressed

## How to adapt to the user's request

### If the user provides code

- inspect the exact allocation call
- identify whether it is an executable JIT allocation path
- point out the specific line or function that should move to `VirtualAlloc2`
- mention any missing `MEM_EXTENDED_PARAMETER_EC_CODE`
- avoid claiming the entire subsystem is wrong if only one path is shown

If the user wants deeper implementation detail, read `references/arm64ec-jit-allocation.md`.

### If the user asks for a bug explanation only

- do not overproduce code
- give a short symptom/root-cause/fix explanation
- mention Microsoft ARM64EC guidance only if useful

### If the user asks for patch guidance

- provide a concrete change list
- call out API replacement, extended parameter setup, and protection-flag review
- mention regression checks for non-ARM64EC or X64 paths

If the user wants the underlying API reasoning or a reference example, read `references/arm64ec-jit-allocation.md`.

### If the user asks for wording

Return polished, ready-to-paste text in the requested format.

Read `references/output-templates.md` when you need concrete wording templates.

## Things to avoid

- do not say `VirtualAlloc` is acceptable for ARM64EC JIT executable pages
- do not omit `MEM_EXTENDED_PARAMETER_EC_CODE` when describing the ARM64EC fix
- do not mix up generic ARM64, ARM64EC, and X64 behavior without noting the distinction
- do not invent crash signatures or runtime symptoms unless the user provided evidence
- do not prescribe broad refactors outside the relevant executable allocation path unless clearly justified

## Success criteria

A good answer from this skill should:

- correctly identify the ARM64EC JIT executable allocation issue
- point to `VirtualAlloc2` plus `MEM_EXTENDED_PARAMETER_EC_CODE`
- stay concise and technically accurate
- match the user's requested output format
- help the user move directly toward diagnosis, documentation, or patching