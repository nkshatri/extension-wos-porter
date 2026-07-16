# WoS Porter — Windows ARM64 Porting Agent for GitHub Copilot

AI-powered agents that automatically port open-source x64 Windows applications to native ARM64.

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

- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **MSVC ARM64 build tools** workload installed
- [GitHub Copilot](https://marketplace.visualstudio.com/items?itemName=GitHub.copilot) extension
- VS Code 1.100+

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

## Using the Agents in Claude Code

In [Claude Code](https://docs.claude.com/en/docs/claude-code) the recommended setup mirrors the VS Code extension: install the four asset categories (agents, instructions, skills, prompts) under `~/.claude/` (or `.claude/` for a single project), and expose two slash commands — `/wos-porter` for the full pipeline and `/wos-verify-port` for the Phase 8 semantic gate.

The agents refer to their sibling instructions/skills/prompts via **relative paths** (e.g. `../skills/wos-toolchain-discovery/SKILL.md`). Preserving the four sibling folders under one root — `~/.claude/` or `.claude/` — keeps every link working with no rewrites.

### Layout

```
~/.claude/                                (or .claude/ for one project)
├── agents/          wos-*.md             (7 files)
├── instructions/    wos-*.instructions.md (9 files — reference material)
├── skills/          wos-*/SKILL.md        (4 dirs)
├── prompts/         wos-verify-port.prompt.md
└── commands/        wos-porter.md, wos-verify-port.md
```

### 1. Install the seven agents

Copy them from the extension's install location (`~/.copilot/agents/`) into `.claude/agents/`, renaming `*.agent.md` → `*.md`:

```bash
mkdir -p ~/.claude/agents
for f in ~/.copilot/agents/wos-*.agent.md; do
  name=$(basename "$f" .agent.md)
  cp "$f" "$HOME/.claude/agents/$name.md"
done
```

Claude Code and VS Code Copilot use slightly different frontmatter keys. In each copied agent, adjust:

| Copilot key | Claude Code subagent key |
|---|---|
| `tools: [execute, read, edit, search, todo, agent]` | `tools: Bash, Read, Edit, Write, Grep, Glob, TodoWrite, Task` (or omit to grant all) |
| `description`, `name` | keep as-is |
| `argument-hint`, `user-invocable`, `agents` | remove |

Tool mapping: `execute`→`Bash`, `read`→`Read`, `edit`→`Edit`/`Write`, `search`→`Grep`/`Glob`, `todo`→`TodoWrite`, `agent`→`Task`. **`wos-porter` must keep the `Task` tool** so it can invoke the other six subagents.

### 2. Install the instructions, skills, and prompts

These are plain markdown; copy them verbatim so the relative links inside the agents resolve:

```bash
mkdir -p ~/.claude/instructions ~/.claude/skills ~/.claude/prompts
cp -R ~/.copilot/instructions/* ~/.claude/instructions/
cp -R ~/.copilot/skills/*       ~/.claude/skills/
cp -R ~/.copilot/prompts/*      ~/.claude/prompts/
```

Notes:

- **Instructions** use `applyTo` frontmatter in VS Code; Claude Code ignores that key. The recipe files are loaded on demand by the agents via markdown links, so the `applyTo` glob is inert but harmless.
- **Skills** are stored one-directory-per-skill (`skills/wos-neon-reference/SKILL.md`, etc.). The agents `read` them on demand, so Claude Code treats them as plain reference documents.
- **Prompts** are Claude Code's closest equivalent of the VS Code prompt file, but they are exposed to the user via the slash command layer (see next step).

### 3. Create the two slash commands

`.claude/commands/wos-porter.md`:

```markdown
---
description: Port an open-source x64 application to Windows ARM64.
argument-hint: <github-repo-url>
allowed-tools: Task
---

Use the **wos-porter** subagent to port the following repository to native
Windows ARM64. Run its full pipeline — analyze, port the build system and
source, build, validate with dumpbin, test, and optimize — delegating each
phase to the wos-analyzer, wos-build-porter, wos-code-porter, wos-builder,
wos-tester, and wos-optimizer subagents. Load skills and instructions on
demand as directed by the agent body (e.g. wos-toolchain-discovery,
wos-neon-reference, wos-forbidden-skip-reasons, and the per-build-system
recipe instructions).

Repository: $ARGUMENTS
```

`.claude/commands/wos-verify-port.md` (Phase 8 gate — the same content as `prompts/wos-verify-port.prompt.md`, callable as a slash command):

```markdown
---
description: Run the Windows ARM64 port verification gates G1–G8 against the on-disk repo.
argument-hint: <repoName>
allowed-tools: Bash, Read, Grep, Glob
---

Follow the verification workflow in ~/.claude/prompts/wos-verify-port.prompt.md
against repo "$ARGUMENTS" (expected under $WOS_PORTER_WORKDIR if set,
otherwise C:\src\wos-porter\$ARGUMENTS on Windows or ~/wos-porter/$ARGUMENTS
elsewhere). Re-derive toolchain state via the wos-toolchain-discovery skill,
run every gate (G1–G8), and report failures without proceeding to the
ARM64-PORT.md report.
```

### 4. Run it

Type `/` to confirm both commands are listed, then invoke the full pipeline:

```
> /wos-porter https://github.com/user/repo
```

Claude Code launches `wos-porter`, which spawns the analyzer, porters, builder, tester, and optimizer subagents. When it reaches Phase 8, either the porter agent invokes the verify workflow inline, or you can run it manually:

```
> /wos-verify-port repo
```

Run `/agents` to verify all seven subagents loaded.

### Token-saving notes for Claude Code

The same design that reduces per-turn tokens in VS Code applies here:

- The 7 agent `description` fields are short (~150 chars each) so the router keeps them cheap.
- Instructions load only when the agent explicitly reads them (there is no `applyTo` auto-load in Claude Code, so linking-on-demand is the whole strategy).
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
