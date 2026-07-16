---
name: arm64-inlineasm-to-intrinsics
description: >-
  Convert ARM64 NEON inline assembly written in Clang/GCC-style `asm volatile(...)`
  blocks into maintainable C/C++ intrinsics, and must build a Verification
  project that compares the original asm against the translated intrinsics for
  semantic equivalence. Use when: (1) the user provides a source file path and
  line range and wants a specific ARM64 asm block rewritten as intrinsics; (2)
  the main source file needs an MSVC-compatible
  `#if defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM64EC))`
  intrinsics branch while preserving the original asm in `#else`; (3) the task
  requires a mandatory GoogleTest-based verification workflow using the bundled
  template stored under `assets/Verification/`. Keywords: asm to intrinsics,
  ARM64 asm to intrinsics, NEON intrinsics, clang asm, msvc arm64, verification,
  gtest equivalence.
---

# arm64-inlineasm-to-intrinsics

Convert a user-specified ARM64 NEON inline assembly region into intrinsics and must instantiate and update a Verification project from the bundled `assets/Verification/` template to prove semantic equivalence against the original asm.

Prioritize correctness and safety by validating the translation in the Verification project first, then apply the proven intrinsics into the main source file. Verification is mandatory for every translation; it exists to confirm semantic equivalence before landing changes in production code.

## Required input from user

Require the user to provide:

1. Source file path
2. Start line for the target code region

Infer the full asm block extent from that starting point. Do not require the user to provide an end line unless the block boundaries remain ambiguous after reading surrounding context.

If the asm depends on nearby locals, macros, typedefs, struct fields, or setup code, read enough lines above and below the starting point to recover the full semantics.

## workflow overview

1. Read the target region with enough surrounding context
2. Identify the asm text, constraints, clobbers, loop structure, pointer updates, and dataflow
3. **Hard requirement:** instantiate/update a `Verification/` test project based on the bundled template in `assets/Verification/` (do not create an ad-hoc test harness from scratch)
4. Extract the user's inline-asm reference implementation into `Verification/src/kernel_reference.cpp` (a wrapper function whose body is the **user-provided** `asm volatile(...)` / `__asm__ volatile(...)` block; do not replace it with C/C++)
5. Write the candidate intrinsics implementation into `Verification/src/kernel_intrinsics.cpp`, same semantics as `kernel_reference.cpp` but using intrinsics instead of asm
6. Update `Verification/include/kernel_compare.h` and `Verification/tests/test_kernel_compare.cpp` to exercise both implementations
7. Build and run the Verification executable in the current environment

## Verification-first, then update the main source

Default to a verification-first workflow so you do not land unproven changes into production code.

Follow this order:

1. Build the Verification project around the target asm, with:
   - `kernel_reference(...)` implemented using the **user-provided inline asm** (`asm volatile(...)`), not a C/C++ re-implementation
   - `kernel_intrinsics(...)` as the candidate intrinsics translation
2. Build and run the Verification executable and iterate until all tests pass
3. Only after Verification passes, replace the asm region in the main source with conditional compilation:
   - `#if defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM64EC))`
   - intrinsics implementation (identical to the verified `kernel_intrinsics(...)` body, adapted to the real function locals)
   - `#else`
   - original asm
   - `#endif`
4. Immediately re-check that `Verification/src/kernel_intrinsics.cpp` still matches what you landed (copy/paste the final version back if needed)

Never maintain two independently evolving intrinsics implementations.

## Consistency requirements

The intrinsics in the main source and in `Verification/src/kernel_intrinsics.cpp` must stay in sync. Before finishing, verify all of the following:

- Bias or accumulator initialization matches the asm
- Post-increment pointer behavior matches the asm
- Main loop and remainder loop counts match the asm
- Lane mapping, store order, and output layout match the asm
- FMA or MLA operand order matches the asm semantics
- Any fix applied in one location is also applied in the other

Add the same provenance comment in both places, for example:

```cpp
// intrinsics for foo.cpp: lines 1379-1416
```

## How to analyze the asm

Read at least 20 lines above and below the requested region, and extend further whenever the current context is insufficient.

Identify:

- The asm instruction stream
- Input/output constraints such as `=r`, `+r`, `r`, and numeric matching constraints
- Clobbers, especially `memory`, `cc`, and vector registers
- Label-based control flow such as `0:` and `bne 0b`
- Post-index loads/stores and the pointer increments they imply
- Whether accumulators start from bias, zero, prior outputs, or earlier computation

Break the asm into semantic stages before translating:

1. Initialization
2. Main loop
3. Tail or remainder handling
4. Final stores

Do not translate long asm blocks mechanically instruction-by-instruction before understanding what each loop iteration consumes and produces.

## Translation rules

### Preserve semantics, not assembly appearance

Write clear C/C++ intrinsics that preserve the asm semantics. It is acceptable to:

- Replace label jumps with structured `for` or `while` loops
- Use local variables to represent register state
- Combine adjacent operations when the resulting logic is equivalent

Avoid:

- Keeping cryptic register-shaped variable names purely to mirror the asm
- Writing code that is hard to maintain just to resemble assembly
- Dropping remainder paths, edge cases, or pointer updates

### Common mappings

Use the semantically correct NEON intrinsics, for example:

- Duplicate or splat → `vdup_n_*`, `vdupq_n_*`
- Load or store → `vld1_*`, `vld1q_*`, `vst1_*`, `vst1q_*`
- Widening multiply → `vmull_*`
- Fused multiply-add → `vfmaq_*`, `vmlaq_*`, or a clearly equivalent form
- Lane multiply-add → `vfmaq_laneq_*` or the appropriate lane API
- Shift, narrow, saturating narrow → choose the intrinsic that matches signedness, rounding, and saturation behavior

If there is no single direct intrinsic equivalent for an asm instruction, implement the higher-level equivalent behavior.

### Loop conversion

Typical patterns:

- `subs ..., #1` → decrement `nn`
- `bne 0b` → `for (; nn > 0; --nn)` or equivalent

If the asm splits work into a main loop and remainder via bit operations such as `lsr` and `and`, keep the same structure in the translated intrinsics.

### Multiple asm variants in one region

When the target region contains two or more `asm volatile` blocks that share the same surrounding context (for example, an 8×8 spatial tile and an 8×1 tail handled by separate asm blocks), translate all of them together. Use a `mode` parameter (or equivalent) in the Verification API to exercise each variant independently.

## Main-source output shape

Generate or preserve this pattern in the main source:

```cpp
#if defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM64EC))
// intrinsics for foo.cpp: lines 1379-1416
// intrinsics implementation
#else
// original asm block
asm volatile(
    ...
);
#endif
```

Requirements:

- Preserve the original asm unchanged in the `#else` branch
- Keep the intrinsics branch readable and maintainable
- Reuse the surrounding function's existing locals and interface whenever practical
- Avoid unrelated refactoring around the target asm block

## Verification project

**Hard requirement:** every translation must instantiate/update a `Verification/` test project based on the bundled template under `assets/Verification/`.

The skill bundles a reusable Verification template under `assets/Verification/`. The template is based on an 8-output-channel ncnn convolution kernel with two spatial modes (8×8 tile and 8×1 tail). **Adapt the parameter list, buffer sizes, and test cases to match the actual asm block being translated.**

For every translation:

1. Copy that template into the user's working project as `Verification/` if the directory is not already present
2. If `Verification/` already exists, update it in place instead of overwriting unrelated user changes
3. Then update these files:

   1. `Verification/include/kernel_compare.h`
   2. `Verification/src/kernel_reference.cpp`
   3. `Verification/src/kernel_intrinsics.cpp`
   4. `Verification/tests/test_kernel_compare.cpp`

### File responsibilities

#### `kernel_compare.h`

Declare exactly two functions with matching parameter lists:

- `kernel_reference(...)`
- `kernel_intrinsics(...)`

Infer the parameters from the original asm constraints and from the real surrounding context. Do not leave hidden dependencies on locals from the main source file.

#### `kernel_reference.cpp`

`kernel_reference(...)` must be the user's original inline-asm implementation, wrapped in a parameterized function.

**Hard requirement:** do not write a C/C++ reference implementation here.

Requirements:

- Use the user-provided `asm volatile(...)` / `__asm__ volatile(...)` block as the function body (keep the instruction stream as-is; only rewire operands/constraints to match the explicit parameters)
- Preserve the original asm semantics (including pointer post-increments, loop counts, and store order)
- Do not place `#if`, `#else`, or `#endif` inside the function body
- Remove hidden dependencies on main-source state
- Turn implicit inputs such as pointers, counters, strides, and buffers into explicit parameters

#### `kernel_intrinsics.cpp`

Place the candidate intrinsics implementation used for Verification. After the main source is updated, keep this file identical to the landed main-source intrinsics (modulo only the wrapper/signature needed for testing).

Requirements:

- Match the parameter list of `kernel_reference(...)` exactly
- Do not place `#if`, `#else`, or `#endif` inside the function body
- Keep the logic synchronized with the main-source intrinsics implementation once it is landed

#### `test_kernel_compare.cpp`

Write GoogleTest cases that:

- Randomize inputs, weights, bias, and output buffers
- Call both `kernel_reference()` and `kernel_intrinsics()`
- Compare all relevant outputs
- Cover edge cases such as `nn = 0`, `1`, loop-boundary counts, and remainder-triggering counts
- Use `eps + rel_eps * fabs(ref)` style tolerance for floating-point comparison
- Seed randomness deterministically so failures are reproducible

If the kernel writes multiple outputs, tiles, or channels, compare every written buffer.

## Build and execution guidance

Because `kernel_reference.cpp` uses GCC/Clang-style `asm volatile(...)`, the Verification project must be compiled with **clang-cl** on Windows. MSVC `cl.exe` does not support this inline asm syntax.

### Finding cmake on Windows

If `cmake` is not in `PATH`, it ships with Visual Studio at:

```
C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

Assign it to a variable before running commands:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
```

### Build commands (PowerShell)

```powershell
& $cmake -S Verification -B Verification/build-vs-arm64 `
    -G 'Visual Studio 17 2022' -A ARM64 -T ClangCL

& $cmake --build Verification/build-vs-arm64 --config Release --parallel

& 'Verification\build-vs-arm64\Release\unit_tests.exe' --gtest_color=yes
```

### Build commands (cmd.exe)

```bat
cmake -S Verification -B Verification/build-vs-arm64 ^
    -G "Visual Studio 17 2022" -A ARM64 -T ClangCL

cmake --build Verification/build-vs-arm64 --config Release --parallel
Verification\build-vs-arm64\Release\unit_tests.exe --gtest_color=yes
```

Important notes:

- The produced test binary is typically ARM64 or ARM64EC
- An x64 Windows host may be unable to run that binary directly
- In that case, complete the build and instruct the user to run the executable on an ARM64 or ARM64EC machine
- Verification is not complete until the executable has been run and all tests pass
- If the executable fails, inspect the generated source, fix the bug, rebuild, and rerun until the test executable passes cleanly

## Implementation details

### While editing

- Limit changes to the target asm block and directly related code
- If macros or templates obscure the real behavior, resolve the instantiated behavior before translating
- When register roles are unclear, infer them from load/store patterns, pointer progression, and accumulation behavior

### After editing

Check at minimum:

- Conditional compilation is complete and balanced
- The intrinsics branch references all required variables and headers
- Verification reference and intrinsics signatures match
- Tests cover both main-loop and remainder paths
- The provenance comment matches between main source and Verification intrinsics
- The Verification executable has been run and passed, or the project is explicitly documented as awaiting execution on ARM64 / ARM64EC so the fix-and-rerun loop can continue there

## Common failure modes

Do not:

- Translate only the obvious fast path and ignore the remainder or tail logic
- Mis-map lanes such as `v?.s[i]`
- Lose pointer updates implied by post-index addressing
- Skip creating/updating the `Verification/` test project from `assets/Verification/`
- Replace the asm reference with C/C++ (in this skill, `kernel_reference()` must stay inline asm)
- Hand-write a separate intrinsics implementation for Verification
- Add conditional compilation inside `kernel_reference()` or `kernel_intrinsics()`
- Rely only on visual inspection instead of building and running the required Verification project
- Use cmd.exe `^` line continuation in PowerShell (use backtick `` ` `` instead)
- Assume `cmake` is in `PATH` on Windows without checking

## Typical outputs

Produce the updated `Verification/` files for every translation task. After Verification passes (or the user explicitly requests landing the change without runtime verification), also produce the main-source conditional compilation with the intrinsics translation.

In the final response, clearly report:

- which source file and asm region were translated
- whether the Verification project was newly copied from `assets/Verification/` or updated in place
- whether the Verification executable was run locally or must still be run on ARM64 / ARM64EC
- whether any compile/runtime/test failures were found and fixed before completion

Do not stop after editing only the main source. Verification is a required deliverable, and the expected loop is: generate code, build, run the executable, inspect failures, fix the code, and rerun until all tests pass. If the current machine cannot execute the resulting ARM64 test binary, leave the project ready for that same loop to continue on an ARM64 or ARM64EC machine.