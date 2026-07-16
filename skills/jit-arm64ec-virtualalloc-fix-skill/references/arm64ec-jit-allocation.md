# ARM64EC JIT executable allocation reference

Use this reference when the user needs deeper implementation guidance, API-level explanation, or source-backed wording for the ARM64EC JIT executable allocation issue.

## Microsoft guidance

Reference:
`https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi#dynamically-generating-jit-compiling-arm64ec-code`

The key guidance is that dynamically generated / JIT-compiled ARM64EC code should use `VirtualAlloc2` with `MEM_EXTENDED_PARAMETER_EC_CODE`, rather than a plain `VirtualAlloc` / `VirtualAllocEx` executable allocation path.

## Why `VirtualAlloc2` matters here

For ARM64EC JIT executable code, the allocation needs EC-specific metadata so the runtime and system can classify the executable pages correctly for the intended execution model.

A plain `VirtualAlloc` or `VirtualAllocEx` path does not provide that EC-specific attribute. That is why it is not the correct allocation path for ARM64EC JIT executable pages.

## Reference example

```cpp
MEM_EXTENDED_PARAMETER parameter = {};
parameter.Type = MemExtendedParameterAttributeFlags;
parameter.ULong64 = MEM_EXTENDED_PARAMETER_EC_CODE;

void* address = VirtualAlloc2(
    GetCurrentProcess(),
    nullptr,
    numBytesToAllocate,
    MEM_RESERVE | MEM_COMMIT,
    PAGE_EXECUTE_READ | PAGE_TARGETS_INVALID,
    &parameter,
    1);
```

## What to explain about the example

- `VirtualAlloc2` is the correct API for ARM64EC JIT executable page allocation.
- `MemExtendedParameterAttributeFlags` indicates the extended parameter contains attribute flags.
- `MEM_EXTENDED_PARAMETER_EC_CODE` marks the allocation as EC code.
- `PAGE_TARGETS_INVALID` appears in Microsoft's example and should not be removed casually.
- Final protection flags should still match the runtime's intended execution and security behavior.

## Practical review points

When reviewing a patch, check for these details:

- whether the changed path is specifically an executable JIT allocation path
- whether the old path used `VirtualAlloc` or `VirtualAllocEx`
- whether `VirtualAlloc2` is now used
- whether `MEM_EXTENDED_PARAMETER_EC_CODE` is actually passed
- whether allocation flags and protection flags remain correct
- whether unrelated allocations were changed without justification
- whether X64 or non-ARM64EC paths were accidentally affected

## Scope cautions

Be careful not to overstate the evidence:

- do not claim every `VirtualAlloc` call in the codebase is wrong
- do not assume every failure mode is a crash unless the user showed that evidence
- do not blur ARM64, ARM64EC, and X64 into the same execution model