# Run in the Cloud (GitHub Actions)

You don't need a local ARM64 machine. The repo ships a workflow —
[`.github/workflows/wos-port-copilot.yml`](../.github/workflows/wos-port-copilot.yml) —
that runs the full porting pipeline on a GitHub-hosted **`windows-11-arm`** runner
using the [GitHub Copilot CLI](https://docs.github.com/en/copilot/reference/copilot-cli-reference/cli-command-reference)
backend. Give it a repository URL and it clones, analyzes, ports, builds, and
uploads a ready-to-apply patch as a workflow artifact.

> There is also a Claude Code counterpart,
> [`.github/workflows/wos-port-claude.yml`](../.github/workflows/wos-port-claude.yml),
> which runs the same pipeline via the Claude Code plugin. Use whichever backend
> you have credentials for — the inputs and artifacts are the same.

## 1. Configure the Copilot token (one-time)

The Copilot CLI needs a token with the **Copilot Requests** permission. The
built-in `GITHUB_TOKEN` does **not** carry that scope, so you must supply your own:

1. Create a **fine-grained personal access token (v2)** with the **Copilot Requests**
   permission — GitHub → *Settings* → *Developer settings* → *Fine-grained tokens*.
2. Add it as a **repository secret** named `COPILOT_GITHUB_TOKEN` —
   repo *Settings* → *Secrets and variables* → *Actions* → *New repository secret*.

The workflow fails fast with a clear message if this secret is missing.

## 2. Run the workflow

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

## 3. Collect the results

When the run finishes, open it and download the artifacts from the **Summary** page:

| Artifact | Contents |
|---|---|
| `arm64-port-copilot-<repo>` | `arm64-port.patch` (combined `git apply` patch), per-commit `git format-patch` series, `commits.txt`, `diffstat.txt`, and the `ARM64-PORT.md` report |
| `ported-repo-copilot-<repo>` | The full working tree on the `arm64-port` branch (build outputs excluded) |
| `copilot-run-log` | The Copilot CLI JSONL run log for inspection |

Apply the patch to a local checkout of the target repo:

```bash
git checkout -b arm64-port
git apply arm64-port.patch
```

The porter only ever works on an `arm64-port` branch forked from the target repo's
default branch — `main`/`master` is never modified. Artifacts are uploaded even if
the run fails partway, so you always have something to inspect.