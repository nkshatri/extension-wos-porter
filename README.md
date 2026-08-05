# WoS Porter — Windows ARM64 Porting Agent

AI-powered agents that automatically port open-source x64 Windows applications to native ARM64. Ships as a **GitHub Copilot** VS Code extension and a **Claude Code** plugin.

## What It Does

Give it a GitHub repository URL or a local path to an x64 project and it will:

1. **Analyze** the repo for x64-specific code (SIMD intrinsics, inline assembly, architecture guards)
2. **Port the build system** — adds ARM64 targets to CMake, MSBuild, Meson, Cargo, and 15+ other build systems
3. **Port source code** — translates SSE/AVX intrinsics to NEON, adds ARM64 preprocessor guards, fixes calling conventions
4. **Build** the project for ARM64 using MSVC, iteratively fixing compilation errors
5. **Validate** every output binary with `dumpbin` to confirm ARM64 architecture
6. **Run tests** on ARM64 hardware (when available)
7. **Generate a report** with a ready-to-apply git patch

## Requirements

- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **MSVC v143 - ARM64/ARM64EC build tools** and **Windows 11 SDK**
- [GitHub Copilot](https://marketplace.visualstudio.com/items?itemName=GitHub.copilot) extension **or** [Claude Code](https://docs.claude.com/en/docs/claude-code) installed and authenticated
- VS Code 1.100+ (for Copilot or Claude Code VS Code extension) or Claude Code CLI
- `git` on PATH

## Get Started

- **[GitHub Copilot (VS Code Extension)](docs/copilot-installation.md)** — Install VSIX, open Copilot Chat, and start porting
- **[Claude Code (Plugin)](claude-plugin/README.md)** — Install and run as a Claude Code plugin
- **[Run in the Cloud (GitHub Actions)](docs/github-actions.md)** — No local ARM64 machine needed; runs on `windows-11-arm` runners

## Documentation

<<<<<<< HEAD
- [WoS Porter Details](docs/wos-porter-details.md) — Agents, instructions, skills, prompts, commands, supported build systems
- [Development](docs/development.md) — Build, package, and publish commands for contributors
=======
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

## Run in the cloud (GitHub Actions)

You don't need a local ARM64 machine. The repo ships a workflow —
[`.github/workflows/wos-port-copilot.yml`](.github/workflows/wos-port-copilot.yml) —
that runs the full porting pipeline on a GitHub-hosted **`windows-11-arm`** runner
using the [GitHub Copilot CLI](https://docs.github.com/en/copilot/reference/copilot-cli-reference/cli-command-reference)
backend. Give it a repository URL and it clones, analyzes, ports, builds, and
uploads a ready-to-apply patch as a workflow artifact.

> Disclaimer: (Still in development phase)There is also a Claude Code counterpart,
> [`.github/workflows/wos-port-claude.yml`](.github/workflows/wos-port-claude.yml),
> which runs the same pipeline via the Claude Code plugin. Use whichever backend
> you have credentials for — the inputs and artifacts are the same.

### 1. Configure the Copilot token (one-time)

The Copilot CLI needs a token with the **Copilot Requests** permission. The
built-in `GITHUB_TOKEN` does **not** carry that scope, so you must supply your own:

1. Create a **fine-grained personal access token (v2)** with the **Copilot Requests**
   permission — GitHub → *Settings* → *Developer settings* → *Fine-grained tokens*.
2. Add it as a **repository secret** named `COPILOT_GITHUB_TOKEN` —
   repo *Settings* → *Secrets and variables* → *Actions* → *New repository secret*.

The workflow fails fast with a clear message if this secret is missing.

### 2. Run the workflow

1. Open the **Actions** tab of this repository.
2. Select **WoS Port (Windows ARM64, Copilot CLI)** in the left sidebar.
3. Click **Run workflow** and fill in the inputs:

   | Input | Required | Default | Description |
   |---|---|---|---|
   | `repo_url` | yes | — | Repository to port (`https://github.com/owner/repo` or `owner/repo`) |
   | `copilot_model` | no | `auto` | Copilot CLI model (`auto`, `claude-sonnet-4.6`, `gpt-5.4`, `claude-haiku-4.5`, `gpt-5.3-codex`, …) |
   | `max_autopilot_continues` | no | `400` | Max autopilot continuation messages before the CLI stops |

4. Click **Run workflow**. The job runs on `windows-11-arm` (timeout 6 hours);
   the preinstalled Visual Studio ARM64 toolchain is used, so there's nothing else
   to set up.

### 3. Collect the results

When the run finishes, open it and download the artifacts from the **Summary** page:

| Artifact | Contents |
|---|---|
| `arm64-port-copilot-<repo>` | `arm64-port.patch` (combined `git apply` patch), per-commit `git format-patch` series, `commits.txt`, `diffstat.txt`, and the `ARM64-PORT.md` report |
| `ported-repo-copilot-<repo>` | The full working tree on the `arm64-port` branch (build outputs excluded) |
| `copilot-run-log` | The Copilot CLI JSONL run log for inspection |

Apply the patch to a local checkout of the target repo:

```
git checkout -b arm64-port
git apply arm64-port.patch
```

The porter only ever works on an `arm64-port` branch forked from the target repo's
default branch — `main`/`master` is never modified. Artifacts are uploaded even if
the run fails partway, so you always have something to inspect.

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
>>>>>>> 4b6a105 (Update README.md)

## License

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.  
SPDX-License-Identifier: BSD-3-Clause-Clear — see [LICENSE.txt](LICENSE.txt)