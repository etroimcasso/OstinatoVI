# Continuous Integration

**Status:** Complete — five build jobs, each building the whole tree and running the tests briefly,
and a parser unit-test job beside them.

## Concept

Every push to a `ci/**` branch builds the project and runs its tests on all five target platforms, on a
self-hosted runner fleet (`.github/workflows/ci.yml`). `main` receives source only by a squash-merge
from a `ci/**` branch that has gone green, and is never built on its own: the same source would rebuild
for no new signal.

Each build job builds every target — the game, its tools, the previews and every test binary, the
tests that ask the cartridge among them — and runs the tests the way a brief run does
(`OSTINATO_CARTRIDGE_CHECKS=brief`, below): every hand-worked check of the port, every test that needs
no cartridge, the damage oracle's own tests and the damage trace's 20-draw check. The long runs of the
oracle and the trace — the sweeps over drawn scenarios, the replays, the battle whole — run on a
developer's machine.

## Platforms

| Job | Runner | Toolchain |
|---|---|---|
| Linux (x64) | `beefserve` | GCC + Ninja |
| macOS (ARM64) | `ericmacmini` | AppleClang + Ninja |
| Windows (x64) | `WINDOWS-BVDA56O` | clang-cl (VS 2022, `-T ClangCL`) |
| Linux (ARM64) | `linmac-arm64` | GCC + Ninja |
| Windows (ARM64) | `WINMACARM64` | clang-cl (VS 2022, `-A ARM64 -T ClangCL`) |
| Parser unit tests | `beefserve` | Python 3 (see below) |

The x64 builds and the parser job run in parallel. The three ARM64 jobs share one physical host —
macOS on the metal, the Linux and Windows ARM64 runners in VMs on it — so they run **in series**: macOS
first, then Linux, then Windows, each `needs:` the one before it with `if: always()`, so a failed link
still yields signal from the rest of the chain.

## What a run builds and runs

Every build job configures with `-DCMAKE_BUILD_TYPE=Release` (Ninja on Linux and macOS, Visual Studio
with clang-cl on Windows), builds every target, and runs `ctest` over the whole build directory with
`--output-on-failure`. The workflow's top-level `env:` sets one variable for every job:

```yaml
env:
  OSTINATO_CARTRIDGE_CHECKS: brief
```

`OSTINATO_CARTRIDGE_CHECKS` is read by the tests alone (`tests/with_cartridge.h`); the build is the same
with it or without it. Set to `brief`:

- every hand check runs on the port, the cartridge not asked;
- every test that hosts the cartridge on the engine's machine and has nothing but the cartridge to
  check is skipped, with the reason `asks the cartridge on the engine's machine; a brief run leaves it
  out (OSTINATO_CARTRIDGE_CHECKS=brief)`;
- the damage oracle's own tests (`tests/oracle/test_damage_oracle.cpp`) and
  `DamageTraceTool.ChecksEveryRoutineBriefly`, which runs `ostinato-damage-trace --check 20`, run in full.

A developer's run leaves the variable unset and runs everything. `tools/damage-trace/README.md`
§ Checking describes the two runs and where each kind of test lives.

The Linux jobs run their tests with `SDL_VIDEODRIVER=offscreen`, so SDL's video starts with no display
and the GPU device (lavapipe) is reachable.

## Baselines

The same 2,150 tests are registered on every platform.

| Run | Passed / total / skipped | Where |
|---|---|---|
| Brief (`OSTINATO_CARTRIDGE_CHECKS=brief`) | **2028/2150/122** | every CI build job |
| Everything (unset) | **2142/2150/8** | a developer's machine |

Eight skips are in both runs, on every platform: the tests of the Japanese cartridge, which needs a
Japanese image to check against. The brief run's other 114 are the tests that ask only the cartridge.
A run is green only when every build job reports the brief baseline, with the same `(Skipped)` names on
all five, and the parser job its full suite: **1223** tests.

## Design decisions

- **Trigger is `push` on `ci/**` only.** No `main` trigger and — deliberately — no
  `pull_request`-family trigger. The repo is public, so anyone can open a fork PR; with push-only
  triggers a fork PR schedules nothing onto the self-hosted fleet. Any addition of `pull_request` /
  `pull_request_target` / `workflow_run` must be weighed against that, since write access to the repo
  equals code-execution access to the runners.
- **One run per branch at a time.** The workflow's `concurrency` group is the workflow and the branch,
  with `cancel-in-progress`: each runner has one workspace and one fixed build directory per platform,
  so a new push to a branch cancels the run still going on it.
- **The engine is the only submodule, and the build jobs initialize only it.** The build consumes the
  engine (`engine/`); it never reads the upstream disassembly (`original-src/`), which is not part of
  the repository and is cloned only by the parser job. Each job fetches the submodule, recursively,
  handing git the `ENGINE_PAT` repository secret per invocation (`git -c url.…insteadOf=…`) rather than
  writing it to the runner's config. A runner keeps its workspace between runs, so each job syncs the
  submodule's URL first and, if the update still fails, removes the submodule's clone and fetches it
  again.
- **Unix jobs build in the workspace's normal `build/` directory** — the same `cmake -S . -B build` a
  developer runs locally — and keep it between runs, so builds are incremental. Their checkouts set
  `clean: false`, because the checkout action's default `git clean -ffdx` would delete the gitignored
  `build/` on every run (the `parser-tests` job shares a workspace with the Linux x64 job and carries
  the same setting). After every build, each Unix job replaces a staged copy of the **entire** build
  directory at `/tmp/ostinato-vi-build` — the whole directory rather than the bare binary, so anything
  the build lays out beside the executable comes along — where the machine's owner can run the result
  without touching the runner service account's workspace. Windows builds at
  `C:\ostinato-vi-ci-build` (the 260-character path limit is real for a FetchContent `_deps` tree
  nested under the workspace), which already sits at an accessible fixed root, so it needs no staging
  step. A Windows configure that fails on a cache another generator or toolset wrote removes that
  directory and configures again.
- **Test steps propagate failure through the log-capture pipe.** `set -o pipefail` on Linux and macOS
  and a `$LASTEXITCODE` check on Windows make a failing `ctest` fail the step — `tee` / `Tee-Object`
  alone would report green regardless.
- **Windows results file names are run-unique** (`...-<run_id>-<run_attempt>.txt`), so a stale file
  handle from an earlier run can never block the write.
- **The ARM64 jobs are serialized, not parallel.** All three ARM64 jobs live on one physical host;
  running them at once would contend for its cores and memory. The `needs:` chain (macOS → Linux ARM64
  → Windows ARM64) keeps at most one job on the host at a time; `if: always()` keeps a red link from
  suppressing the rest.

## Parser unit-test job

The port-time generator scripts in `tools/asm_parser/` (which emit the enum headers, data tables and
test fixtures from the disassembly) carry their own Python unit suites (`test_parse_*.py`,
stdlib-only). The `parser-tests` job runs them on every push with
`python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py' -v`, guarding the generators
against helper-logic and structural-assert regressions that the C++ jobs — which build the generated
artifacts already committed — cannot see.

Job mechanics:

- **`original-src` is cloned** into the workspace (ignored by the repository) from the public
  upstream, `https://github.com/everything8215/ff6.git`, at the commit the parsers are written
  against, the step's `ORIGINAL_SRC_COMMIT`, so each suite's end-to-end layer runs against the real
  upstream sources (`const.inc`, `char_prop.asm`). A clone already on the runner is reused and moved to
  that commit.
- **The rip.** Where `FF6_VANILLA_ROM` names a file, the job copies it into `original-src/vanilla/` and
  runs `make -C original-src rip`, which fills the rip products the rip-product end-to-end tests read
  (`rng_tbl.dat` among them). Where no cartridge is provided, the step says so and those tests skip
  visibly in the unittest output.
- **The rip needs numpy** (`tools/extract_assets.py`). The job uses the runner's `python3` when numpy
  imports; otherwise it builds a cached venv once (`~/.cache/ostinatovi-rip-venv`) and rips with it. If
  numpy still cannot be provided while a cartridge is present, the step fails: that is a provisioning
  defect, not a skip. `ci/runner-setup/linux/deps-install.sh` installs `python3-numpy` and
  `python3-venv` for new runners.
- Results upload as the `parser-test-results` artifact; green is read from the unittest summary line
  (`Ran N tests … OK`), not the step icon.

## Runner provisioning

**One-time software install** — run the matching script once per runner:

- Linux: `ci/runner-setup/linux/deps-install.sh` (as root)
- macOS: `ci/runner-setup/macos/deps-install.sh` (Homebrew)
- Windows: `ci/runner-setup/windows/deps-install.ps1` (elevated PowerShell)

These install the CMake/Ninja/compiler toolchain and — on Linux — the system dev headers SDL3 links
against. The engine builds SDL3 and SameBoy from its own submodules, so no SDL or SameBoy package is
required. The Linux x64 job installs `libxtst-dev` (SDL3's XTEST dependency) itself when it is missing.

**Repository secret** — `ENGINE_PAT`: the token the build jobs hand git when they fetch the `engine/`
submodule.

### The cartridge on each runner

The tests read the cartridge in place, and the parser job rips from it. It is never placed in the
repository or a workspace: each runner holds a verified copy (3,145,728 bytes, CRC32 `C0FA0464`, FF3
1.1 (U)) at a stable path outside its Actions work directory, and every job sees it as
`FF6_VANILLA_ROM`.

| Runner | Where `FF6_VANILLA_ROM` comes from |
|---|---|
| `beefserve` | the runner's `.env` file |
| `WINDOWS-BVDA56O` | a machine-scope environment variable; the runner service reads it when it starts |
| `ericmacmini` | the job's first step, `$HOME/ci-assets/ostinatovi/FF3-1.1-USA.smc`, exported to `GITHUB_ENV` from a shell that has expanded `$HOME` (the service environment holds it unexpanded) |
| `linmac-arm64` | the job's first step, `$HOME/ci-assets/ostinatovi/FF3-1.1-U.smc` |
| `WINMACARM64` | the job's first step, `C:\ci-assets\ostinatovi\FF3-1.1-USA.smc` |

To provision a new runner, copy the cartridge to a stable location outside the work directory and
either set `FF6_VANILLA_ROM` in the runner's environment or add a step that exports its path to
`GITHUB_ENV`, as the jobs above do. A runner whose cartridge cannot be read fails every test that wants
one, naming the variable and the path (`tests/vanilla_rom.h`); none is skipped past.

## Reading results

Each job uploads its test output as a build artifact (`linux-test-results`, `macos-test-results`,
`windows-test-results`, `linux-arm64-test-results`, `windows-arm64-test-results`,
`parser-test-results`). Green is confirmed by reading the test runner's own output — the pass total and
the `(Skipped)` lines, the same on every platform — not by the step's status icon.

## Open questions / future work

- Additional runners can join as parallel jobs when they have their own hardware; anything sharing an
  existing host joins that host's serial chain instead.
