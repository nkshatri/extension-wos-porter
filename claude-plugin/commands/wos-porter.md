---
description: "Port an open-source x64 application to Windows ARM64 (clone, analyze, port build+source, build, test, NEON-optimize, verify, report). Runs the wos-porter pipeline in the main loop."
argument-hint: "<GitHub repository URL to port to ARM64>"
---

You are now acting as the **wos-porter** orchestrator, running directly in this main conversation (NOT as a spawned subagent). This is required because subagents on this Claude Code version cannot spawn further subagents — so YOU, the main agent, must invoke the sub-agents yourself.

## Steps

1. Read your full operating instructions from `${CLAUDE_PLUGIN_ROOT}/agents/wos-porter.md`. Follow that agent's entire 8-phase pipeline verbatim as if those instructions were your own system prompt. Ignore its YAML frontmatter (name/description/tools) — just execute the body.

2. When those instructions tell you to invoke a sub-agent (referred to in the pipeline as `wos-analyzer`, `wos-build-porter`, `wos-code-porter`, `wos-builder`, `wos-tester`, `wos-optimizer`), spawn it with the **Agent** tool using the matching `subagent_type` — i.e. `subagent_type: "wos-analyzer"`, `"wos-build-porter"`, `"wos-code-porter"`, `"wos-builder"`, `"wos-tester"`, `"wos-optimizer"`. These are first-level subagent spawns from the main loop, which is supported.

3. When the instructions reference the Phase 8 verification prompt (`${CLAUDE_PLUGIN_ROOT}/prompts/wos-verify-port.prompt.md`), read `${CLAUDE_PLUGIN_ROOT}/prompts/wos-verify-port.prompt.md` and execute its G1–G8 gates inline yourself.

4. Execute ALL 8 phases in order. Do not skip Phases 4–7. Do not generate the Phase 8 report until Phases 4, 5, 6, and 7 have run.

## Repository to port

$ARGUMENTS
