# Continuous Integration

**Date:** 2026-08-02
**Status:** Complete (current scope — build + smoke test on all platforms)

## Concept

Every push to a `ci/*` branch builds the project and runs the test suite on all
three target platforms, on a self-hosted runner fleet. `main` receives clean,
already-verified source via squash-merge from a `ci/*` branch that has gone green;
it is never built directly (the same source would rebuild for no new signal).

## Platforms

| Job | Runner | Toolchain |
|---|---|---|
| Linux (x64) | `beefserve` | GCC + Ninja |
| macOS (ARM64) | `ericmacmini` | AppleClang + Ninja |
| Windows (x64) | `WINDOWS-BVDA56O` | clang-cl (VS 2022, `-T ClangCL`) |

All three jobs run in parallel — there is no serialization chain, because no two
jobs share a host. A run is green only when all three report the full test count
(the current baseline is `1/1/0` — passed/total/skipped — on every platform).

## Design decisions

- **Trigger is `push` on `ci/**` only.** No `main` trigger and — deliberately — no
  `pull_request`-family trigger. The repo is public, so anyone can open a fork PR;
  with push-only triggers a fork PR schedules nothing onto the self-hosted fleet.
  Any future addition of `pull_request` / `pull_request_target` / `workflow_run`
  must be weighed against that, since write access to the repo equals code-execution
  access to the runners.
- **Only the engine submodule is initialized in CI.** The build consumes the engine
  (`engine/`); it does not read the upstream disassembly submodule (`original-src/`),
  which is a port-time asset source, not a build input. Skipping it keeps checkouts
  lean. The engine is a private repo, so its checkout uses the `ENGINE_PAT` repository
  secret, supplied per-invocation to git rather than written to the runner's config.
- **Build directories live outside the checkout** (`/tmp/ostinato-vi-ci-build`,
  `C:\ostinato-vi-ci-build`) and persist between runs, so builds are incremental.
- **Test steps propagate failure through the log-capture pipe.** `set -o pipefail`
  on Linux/macOS and a `$LASTEXITCODE` check on Windows ensure a failing `ctest`
  fails the step — `tee`/`Tee-Object` alone would report green regardless.
- **Windows results filenames are run-unique** (`...-<run_id>-<run_attempt>.txt`) so a
  stale file handle from a prior run can never block the write.

## Runner provisioning

**One-time software install** — run the matching script once per runner:

- Linux: `ci/runner-setup/linux/deps-install.sh` (as root)
- macOS: `ci/runner-setup/macos/deps-install.sh` (Homebrew)
- Windows: `ci/runner-setup/windows/deps-install.ps1` (elevated PowerShell)

These install the CMake/Ninja/compiler toolchain and — on Linux — the system dev
headers SDL3 links against. The engine builds SDL3 and SameBoy from its own
submodules, so no SDL/SameBoy package is required.

**Repository secret** — `ENGINE_PAT`: a token with read access to the private engine
repo, used to fetch the `engine/` submodule during checkout.

### Vanilla ROM on each runner (needed once game data is derived from the ROM)

Later work that derives game data reads the vanilla ROM. Rather than
placing the ROM in the repository (it is never committed) or a workspace, each runner
holds it once at a stable path outside the workspace, exposed to jobs through a
runner environment variable:

1. Copy the vanilla ROM to a stable location on the runner (outside the Actions work
   directory), e.g. `~/ci-assets/ff6-vanilla.smc`.
2. Set `FF6_VANILLA_ROM` to that absolute path in the runner's environment (the
   runner's `.env` file, or the service environment).

A job that needs the ROM stages it from `FF6_VANILLA_ROM` into the workspace and
proceeds. A runner missing the variable or the file is a provisioning failure and
must surface loudly (fail, or skip with a visible reason) — never a silent pass.
This is **not** needed for the current build/smoke jobs; it can be provisioned when
the first ROM-reading job lands.

## Reading results

Each job uploads its `ctest` output as a build artifact (`linux-test-results`,
`macos-test-results`, `windows-test-results`). Green is confirmed by reading the test
runner's own output (the pass/fail counts), not by the step's status icon alone.

## Open questions / future work

- A future job that reads the vanilla ROM will consume `FF6_VANILLA_ROM` and add
  the provisioning-failure guard described above.
- Additional runner architectures can be added as parallel jobs if the fleet grows.
