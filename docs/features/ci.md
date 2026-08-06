# Continuous Integration

**Date:** 2026-08-02 (updated 2026-08-04)
**Status:** Complete (current scope — build + full test suites on all platforms, plus a parser unit-test job)

## Concept

Every push to a `ci/*` branch builds the project and runs the test suite on all
five target platforms, on a self-hosted runner fleet. `main` receives clean,
already-verified source via squash-merge from a `ci/*` branch that has gone green;
it is never built directly (the same source would rebuild for no new signal).

## Platforms

| Job | Runner | Toolchain |
|---|---|---|
| Linux (x64) | `beefserve` | GCC + Ninja |
| macOS (ARM64) | `ericmacmini` | AppleClang + Ninja |
| Windows (x64) | `WINDOWS-BVDA56O` | clang-cl (VS 2022, `-T ClangCL`) |
| Linux (ARM64) | `linmac-arm64` | GCC + Ninja |
| Windows (ARM64) | `WINMACARM64` | clang-cl (VS 2022, `-A ARM64 -T ClangCL`) |
| Parser unit tests | `beefserve` | Python 3 (see below) |

The x64 builds and the parser job run in parallel (the two `beefserve` jobs don't
contend — the parser job is sub-second Python). The three ARM64 jobs share one
physical host — macOS on the metal, the Linux and Windows ARM64 runners in VMs on
it — so they run **in series**: macOS first, then Linux, then Windows, each
`needs:` the previous link with `if: always()` so a failed link still yields
signal from the rest of the chain.

A run is green only when every build job reports the full test count (current C++
baseline: `23/24/1` — passed/total/skipped, the skip being the pending Japanese
attack-properties variant — on every one of the five platforms) and the parser job
reports its full suite (currently 145 cases).

## Design decisions

- **Trigger is `push` on `ci/**` only.** No `main` trigger and — deliberately — no
  `pull_request`-family trigger. The repo is public, so anyone can open a fork PR;
  with push-only triggers a fork PR schedules nothing onto the self-hosted fleet.
  Any future addition of `pull_request` / `pull_request_target` / `workflow_run`
  must be weighed against that, since write access to the repo equals code-execution
  access to the runners.
- **Only the engine submodule is initialized in the build jobs.** The build consumes
  the engine (`engine/`); it does not read the upstream disassembly submodule
  (`original-src/`), which is a port-time asset source, not a build input. Skipping it
  keeps build checkouts lean. The engine is a private repo, so its checkout uses the
  `ENGINE_PAT` repository secret, supplied per-invocation to git rather than written
  to the runner's config. (The parser job is the exception — it initializes
  `original-src`, which is public and needs no PAT.)
- **Unix jobs build in the workspace's normal `build/` directory** — the same
  `cmake -S . -B build` a developer runs locally — and persist it between runs, so
  builds are incremental. Their checkouts set `clean: false`, because the checkout
  action's default `git clean -ffdx` would delete the gitignored `build/` on every
  run and silently force cold builds (the `parser-tests` job shares a workspace
  with the Linux build job and carries the same setting). After every build, each
  Unix job replaces a staged copy of the **entire** build directory at
  `/tmp/ostinato-vi-build` — the whole directory rather than the bare binary, so
  anything the build lays out beside the executable comes along — where the
  machine's owner can run the result without touching the runner service account's
  workspace. Windows is the one exception: it builds at `C:\ostinato-vi-ci-build`
  (the 260-character path limit is real for a FetchContent `_deps` tree nested
  under the workspace), which already sits at an accessible fixed root, so it
  needs no staging step.
- **Test steps propagate failure through the log-capture pipe.** `set -o pipefail`
  on Linux/macOS and a `$LASTEXITCODE` check on Windows ensure a failing `ctest`
  fails the step — `tee`/`Tee-Object` alone would report green regardless.
- **Windows results filenames are run-unique** (`...-<run_id>-<run_attempt>.txt`) so a
  stale file handle from a prior run can never block the write.
- **The ARM64 jobs are serialized, not parallel.** All three ARM64 jobs live on one
  physical host; running them concurrently would contend for its cores and memory.
  The `needs:` chain (macOS → Linux ARM64 → Windows ARM64) keeps at most one job on
  the host at a time; `if: always()` keeps a red link from suppressing the rest.

## Parser unit-test job

The port-time generator scripts in `tools/asm_parser/` (which emit the enum headers,
data tables, and test fixtures from the disassembly) carry their own Python unit
suites (`test_parse_*.py`, stdlib-only). The `parser-tests` job runs them on every
push via `python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'`,
guarding the generators against helper-logic and structural-assert regressions that
the C++ jobs — which only build the already-committed generated artifacts — cannot
see.

Job mechanics:

- **`original-src` is initialized** (public submodule, no PAT) so each suite's
  end-to-end layer runs against the real committed sources (`const.inc`,
  `char_prop.asm`).
- **Rip-product staging.** Some upstream files are produced by the disassembly's rip
  (`make rip`), not committed — e.g. `rng_tbl.dat`. When the runner provides a ROM
  via `FF6_VANILLA_ROM`, the job copies it into `original-src/vanilla/` and runs the
  rip first, so those end-to-end tests run full. Without a ROM they skip with a
  visible reason in the unittest output (never silently).
- **The rip needs numpy** (`tools/extract_assets.py` → `monster_stencil`). The job
  uses the runner's python3 when numpy imports; otherwise it builds a one-time
  cached venv (`~/.cache/ostinatovi-rip-venv`) and runs the rip with it. If numpy
  still cannot be provided while a ROM is present, the step fails loudly — that is a
  provisioning defect, not a skip. `ci/runner-setup/linux/deps-install.sh` installs
  `python3-numpy` + `python3-venv` for future runner builds.
- Results upload as the `parser-test-results` artifact; green is read from the
  unittest summary line (`Ran N tests … OK`), not the step icon.

## Runner provisioning

**One-time software install** — run the matching script once per runner:

- Linux: `ci/runner-setup/linux/deps-install.sh` (as root)
- macOS: `ci/runner-setup/macos/deps-install.sh` (Homebrew)
- Windows: `ci/runner-setup/windows/deps-install.ps1` (elevated PowerShell)

These install the CMake/Ninja/compiler toolchain and — on Linux — the system dev
headers SDL3 links against. The engine builds SDL3 and SameBoy from its own
submodules, so no SDL/SameBoy package is required.

**Repository secret** — `ENGINE_PAT`: a token with read access to the private engine
repo, which fetches the `engine/` submodule during checkout.

### Vanilla ROM on each runner (consumed by the parser job's rip staging)

Work that derives game data reads the vanilla ROM. Rather than
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

**Provisioned 2026-08-02 on the first three runners.** Verified copy (3,145,728
bytes, CRC32 `C0FA0464`) at a stable path outside each workspace. Mechanism per
platform: the Linux and macOS runners carry `FF6_VANILLA_ROM` in the runner's
`.env` file; the Windows x64 runner carries it as a **machine-scope environment
variable** (`[Environment]::SetEnvironmentVariable(..., 'Machine')`) — the service
inherits it on its next restart, so jobs reading it before that restart fail
visibly per the guard above and re-run clean after a one-service bounce.

**The two ARM64 runners (added 2026-08-04) use fixed paths instead of runner-level
environment.** The ROM lives at `~/ci-assets/ostinatovi/FF3-1.1-U.smc` on
`linmac-arm64` and `C:\ci-assets\ostinatovi\FF3-1.1-U.smc` on `WINMACARM64`; each
ARM64 job's first step exports that path to `FF6_VANILLA_ROM` via `GITHUB_ENV`, so
ROM-consuming steps see the same variable on every runner regardless of the
provisioning mechanism behind it.

## Reading results

Each job uploads its test output as a build artifact (`linux-test-results`,
`macos-test-results`, `windows-test-results`, `linux-arm64-test-results`,
`windows-arm64-test-results`, `parser-test-results`). Green is confirmed by reading
the test runner's own output (the pass/fail counts), not by the step's status icon
alone.

## Open questions / future work

- Additional runners can join as parallel jobs when they have their own hardware;
  anything sharing an existing host joins that host's serial chain instead.
