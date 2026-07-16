# WoS Porter — Windows ARM64 Porting Agent

AI-powered agents that automatically port open-source x64 Windows applications to native ARM64. Ships in two forms from this one repo: a **GitHub Copilot** VS Code extension and a **Claude Code** plugin (see [Using It in Claude Code](#using-it-in-claude-code-plugin)).

## What It Does

Give it a GitHub repository URL and it will:

1. **Analyze** the repo for x64-specific code (SIMD intrinsics, inline assembly, architecture guards)
2. **Port the build system** — adds ARM64 targets to CMake, MSBuild, Meson, Cargo, Make, and 15+ other build systems
3. **Port source code** — translates SSE/AVX intrinsics to NEON, adds ARM64 preprocessor guards, fixes calling conventions
4. **Build** the project for ARM64 using MSVC, iteratively fixing compilation errors
5. **Validate** every output binary with `dumpbin` to confirm ARM64 architecture
6. **Run tests** on ARM64 hardware (when available)
7. **Generate a report** with a ready-to-apply git patch

## Included Agents

| Agent | Role |
|-------|------|
| `wos-porter` | Main orchestrator — runs the full 8-phase porting pipeline |
| `wos-analyzer` | Read-only deep scan of a repo for ARM64 readiness |
| `wos-build-porter` | Modifies build configurations (CMake, MSBuild, Meson, Cargo, etc.) |
| `wos-code-porter` | Ports x64-specific source code (SIMD, inline asm, arch guards) |
| `wos-builder` | Builds, validates binaries with dumpbin, and fixes build errors |
| `wos-tester` | Runs and fixes ARM64 test suites and benchmarks |
| `wos-optimizer` | Applies hand-written ARM NEON intrinsics to hot kernels for performance |

## Included Instructions

Auto-loaded (via `applyTo` globs) only when a matching file is open — keeps idle-turn token cost low.

| File | Applies to |
|------|------------|
| `wos-build-errors.instructions.md` | Build logs, `.vcxproj`/`.sln`, `CMakeLists.txt` — ARM64 compile/link error patterns |
| `wos-porting-knowledge.instructions.md` | C/C++/Rust source — high-level ARM64 porting knowledge |
| `wos-build-recipes-cmake.instructions.md` | `CMakeLists.txt`, `.cmake`, `CMakePresets.json` |
| `wos-build-recipes-msbuild.instructions.md` | `.vcxproj`, `.sln`, `.props`, `.targets`, `packages.config` |
| `wos-build-recipes-cargo.instructions.md` | `Cargo.toml`, `.cargo/config.toml`, `build.rs` |
| `wos-build-recipes-meson.instructions.md` | `meson.build`, `meson_options.txt`, cross files |
| `wos-build-recipes-nodegyp.instructions.md` | `binding.gyp`, `package.json` |
| `wos-build-recipes-python.instructions.md` | `setup.py`, `pyproject.toml`, `.pyx` |
| `wos-build-recipes-misc.instructions.md` | Autotools, Make/NMake, Bazel, GN, Premake, SCons, Waf, qmake, xmake, B2, Go/cgo |
| `wos-ci-arm64.instructions.md` | GitHub Actions, AppVeyor, Azure Pipelines, GitLab CI, Jenkins workflows — drop-in `windows-11-arm` matrix per Arm AppReady |

## Included Skills

Loaded on demand — never in the always-visible system prompt.

| Skill | Load when |
|------|-----------|
| `wos-neon-reference` | Actively translating x86 SIMD intrinsics to NEON, or checking the Windows ARM64 baseline ISA |
| `wos-build-error-recipes` | Triaging an ARM64 compile/link error line-by-line |
| `wos-forbidden-skip-reasons` | Auditing an optimizer report or writing the Limitations section |
| `wos-toolchain-discovery` | Any agent needs the ARM64 `cl.exe` / `msbuild` / `dumpbin` / `vcvars` paths — result is cached to `<repo>\.copilot\state\wos-toolchain.json` and reused across phases |
| `wos-woa-dashboard` | Classifying dependencies against the [Arm Windows on Arm Software Dashboard](https://developer.arm.com/ecosystem-dashboard/windows) (native / building / unsupported / unknown + ARM64 native / Emulated x64 / Blocking); used by `wos-analyzer` Phase 2 and the Phase 8 AppReady Status section |
| `sse-avx-to-neon` | Extended SSE/AVX → NEON translation guide including PCLMULQDQ → PMULL (CRC32 kernels). Loaded by `wos-code-porter` and `wos-optimizer` when translating `_mm_*` / `_mm256_*` intrinsics |
| `intrinsics-x64-to-arm64` | STL-grounded patterns for `__m128i` / `__m256i` → NEON, `IsProcessorFeaturePresent` swap, AVX2 tail-mask conversion, `_Zeroupper_on_exit` removal, `_M_ARM64EC` guard hygiene. Used alongside `sse-avx-to-neon` for Windows C++ codebases |
| `asm-x64-to-arm64` | Translate x64 inline asm (`asm volatile`) and standalone MASM `.asm` files (PROC/ENDP) to AArch64 GAS `.S` form. Used by `wos-code-porter` when analyzer flags inline asm or hot-loop `.asm` files |
| `arm64-inlineasm-to-intrinsics` | Convert existing ARM64 `asm volatile(...)` blocks to MSVC-compatible C/C++ intrinsics, with a mandatory GoogleTest verification harness (`assets/Verification/`). Used by `wos-code-porter` and `wos-optimizer` |
| `arm64-baseline-porting` | Fallback constraints for any freeform ARM64 code emission with no more specific match — Windows ARM64 ABI, weak memory ordering, NEON 128-bit ceiling, ARM64EC ABI shims, intrinsic header hygiene |
| `jit-arm64ec-virtualalloc-fix-skill` | Detect and fix the ARM64EC JIT allocation bug where JIT pages get misclassified as x64 (missing `VirtualAlloc2` + `MEM_EXTENDED_PARAMETER_EC_CODE`). Used by `wos-analyzer` Step 4e and downstream `wos-code-porter` |

## Included Prompts

| Prompt | Purpose |
|------|---------|
| `wos-verify-port.prompt.md` | Phase 8 semantic-gate verification (`G1`–`G8`): re-derives toolchain state, runs dumpbin, verifies commit/test/benchmark/NEON claims against the filesystem before writing `ARM64-PORT.md` |

## Requirements

**For the VS Code / GitHub Copilot extension:**

- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **MSVC v143 - ARM64/ARM64EC build tools** and **Windows 11 SDK**
- [GitHub Copilot](https://marketplace.visualstudio.com/items?itemName=GitHub.copilot) extension
- VS Code 1.100+
- `git` on PATH

**For the Claude Code plugin:**

- [Claude Code](https://docs.claude.com/en/docs/claude-code) installed and authenticated
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **MSVC v143 - ARM64/ARM64EC build tools** and **Windows 11 SDK**
- `git` on PATH

(See [`claude-plugin/README.md`](claude-plugin/README.md#prerequisites) for the full Claude Code prerequisites and install steps.)

### Optional environment variables

| Variable | Default | Purpose |
|---|---|---|
| `WOS_PORTER_WORKDIR` | `C:\src\wos-porter` (Windows), `$HOME/wos-porter` (other) | Root folder where the porter clones target repos and writes `<repo>\.copilot\state\wos-toolchain.json`. Set this if the default drive is unwritable or you need clones on a different volume. |

## Installation

### From VSIX File

1. **Package the extension** (if you haven't already):
   ```
   npm run package
   ```
   This produces a `.vsix` file in the project root.

2. **Install in VS Code** using one of these methods:

   **Option A — VS Code UI:**
   - Open VS Code
   - Go to the Extensions view (`Ctrl+Shift+X`)
   - Click the `...` menu (top-right of the Extensions panel)
   - Select **"Install from VSIX..."**
   - Browse to and select the `.vsix` file

   **Option B — Command Palette:**
   - Open the Command Palette (`Ctrl+Shift+P`)
   - Type **"Extensions: Install from VSIX"**
   - Browse to and select the `.vsix` file

   **Option C — Command Line:**
   ```
   code --install-extension wos-porter-<version>.vsix
   ```

3. **Reload VS Code** when prompted.

## Usage

1. Install the extension (see [Installation](#installation) above)
2. Open Copilot Chat
3. Select the **wos-porter** agent from the agent picker
4. Paste a GitHub repository URL:

```
Port https://github.com/user/repo to ARM64
```

The agent will clone the repo, analyze it, port it, build it, and produce a patch — all automatically.

### Other agents

You can also invoke individual agents directly:

- `wos-builder` — Build and validate an already-ported project
- `wos-tester` — Run and fix ARM64 test suites and benchmarks
- `wos-optimizer` — Apply ARM NEON intrinsics to hot kernels

## Using It in Claude Code (plugin)

The same agents, skills, prompts, and reference material are also packaged as a **Claude Code plugin** under [`claude-plugin/`](claude-plugin/). You do **not** need to copy files into `~/.claude/` by hand — Claude Code installs the plugin from this repository through its marketplace mechanism.

### Install from GitHub

1. Open Claude Code in any project.
2. Add this repository as a marketplace (it advertises the plugin via the root `.claude-plugin/marketplace.json`):

   ```
   /plugin marketplace add qualcomm/extension-wos-porter
   ```

   Pin a branch if the plugin isn't on the default branch yet:

   ```
   /plugin marketplace add qualcomm/extension-wos-porter@refactor
   ```

3. Install the plugin:

   ```
   /plugin install wos-porter@extension-wos-porter
   ```

4. Confirm it loaded with `/plugin` (you should see **wos-porter** enabled).

### Install from a local clone

Point the marketplace at your checkout root (the folder containing `.claude-plugin/marketplace.json`), then install:

```
/plugin marketplace add C:\path\to\extension-wos-porter
/plugin install wos-porter@extension-wos-porter
```

### Run it

```
/wos-porter:wos-porter https://github.com/user/repo
```

This command runs the full 8-phase pipeline **in the main conversation** — it reads the `wos-porter` agent's instructions and spawns the six `wos-*` sub-agents itself (sub-agents cannot spawn further sub-agents). The Phase 8 verification gate (`G1`–`G8`) in [`claude-plugin/prompts/wos-verify-port.prompt.md`](claude-plugin/prompts/wos-verify-port.prompt.md) runs inline before `ARM64-PORT.md` is written. Ported code stays local on an `arm64-port` branch; `main`/`master` is never modified.

See [`claude-plugin/README.md`](claude-plugin/README.md) for the full step-by-step install, usage, uninstall, and troubleshooting guide.

### How the plugin maps to the Copilot assets

| Copilot asset (this repo) | Claude Code plugin location |
|---|---|
| `agents/wos-*.agent.md` | `claude-plugin/agents/wos-*.md` (frontmatter converted to Claude subagent spec) |
| `prompts/wos-verify-port.prompt.md` | `claude-plugin/prompts/wos-verify-port.prompt.md` |
| `instructions/wos-*.instructions.md` | `claude-plugin/references/wos-*.md` (loaded on demand) |
| `skills/wos-*/SKILL.md` | `claude-plugin/skills/wos-*/SKILL.md` (verbatim) |
| entry point | `claude-plugin/commands/wos-porter.md` → `/wos-porter:wos-porter` |

### Token-saving notes for Claude Code

- The 7 agent `description` fields are short (~150 chars each) so the router keeps them cheap.
- References load only when an agent explicitly reads them (there is no `applyTo` auto-load in Claude Code — linking on demand is the whole strategy).
- Skills are read only when a specific task needs them (e.g. `wos-toolchain-discovery` on Phase 4 / 5 / 6 / 8 rather than every turn).
- `wos-toolchain-discovery` caches its result to `<repo>\.copilot\state\wos-toolchain.json`; every subsequent phase reads the cache instead of re-invoking `vswhere`.

## Commands

| Command | Description |
|---------|-------------|
| `WoS Porter: Install Assets` | Install/reinstall agents, instructions, skills, and prompts under `~/.copilot/{agents,instructions,skills,prompts}` and register the four locations with Copilot Chat |
| `WoS Porter: Uninstall Assets` | Remove all installed files and settings entries |
| `WoS Porter: Check Asset Status` | Show installed counts (agents / instructions / skills / prompts) |

## Supported Build Systems

CMake, MSBuild/Visual Studio, Meson, Make/NMake, Cargo (Rust), Autotools, Bazel, GN, Premake, SCons, Waf, qmake, xmake, B2/Boost.Build, Go, node-gyp, .NET SDK, Gradle, Python C extensions.

## License

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.  
SPDX-License-Identifier: BSD-3-Clause-Clear

## Build Commands

All build and development commands should be run from the project root directory.

### Install Dependencies

```
npm install
```

### Compile TypeScript

```
npm run compile
```

### Watch for Changes (Auto-compile)

```
npm run watch
```

### Package Extension (VSIX)

```
npm run package
```

### Publish Extension

```
npm run publish
```

### Uninstall Agents

```
npm run vscode:uninstall
```

### Clean Build Artifacts

To clean build files manually, remove the following:
- `out/` directory
- Any `*.js`, `*.js.map`, `*.tsbuildinfo` files
- `node_modules/.cache` (if present)

---
