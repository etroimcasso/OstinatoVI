# Build System & Test Harness

**Date:** 2026-08-01 (updated 2026-11-14)
**Status:** Complete

## Concept

The build rails for the port: a CMake project that consumes the Polyrhythm engine as a source
subproject and builds three static libraries — all port code, and the damage oracle's port side and
cartridge side — the game binary, four developer tools and 31 GoogleTest binaries, each test binary
covering one area so its baseline moves only when that area does.

## Design decisions

### Engine consumption (verified against `engine/docs/guide/build-and-consume.md`)

- **`add_subdirectory(engine)` + link `retropp::engine`.** The engine ships as source and is
  attached as a submodule; the consumer builds it in-tree. Target alias `retropp::engine` (real
  target `retroppengine`).
- **`RETROPP_BUILD_TESTS` / `RETROPP_BUILD_EXAMPLES` left at their subproject default (OFF).** The
  engine turns both off when it is not the top-level project, so `ctest` shows only the port's tests
  and the engine fetches no GoogleTest of its own. The port does not set them.
- **`retropp::testkit` is not linked.** The port checks itself against the cartridge through its own
  oracle on the engine's SNES machine (`tools/damage-trace/src/oracle/damage_oracle.h`), which needs
  only `retropp::engine`.
- **Engine pin `bb0e3dec`.** Nested engine submodules: SameBoy `v1.0.3`, SDL `release-3.4.10`,
  `third_party/snaggletooth` (the SNES core the hosted cartridge runs on). A pin moves only
  deliberately, on its own `ci/` branch.
- **Engine license:** PolyForm Noncommercial 1.0.0. The port's own license is AGPL-3.0 and governs only
  the port's code; nothing derived from the cartridge ships or is committed.

### Target topology

Thirty-nine targets:

| Target | Kind | Links | Purpose |
|---|---|---|---|
| `ostinato-vi-lib` | STATIC | `retropp::engine` (PUBLIC) | All port code — assets, battle, data, game, render, text — so the binary, the tools and the tests build against it without duplicating a `main`. Public include dirs: `include/` (generated enums and port headers) + `src/`. |
| `ostinato-battle-scenarios` | STATIC | `ostinato-vi-lib` (PUBLIC) | The damage oracle's port side: the scenarios it runs, the port's answer to each, the comparisons, the seeded draws, the reports and the trace's parsers, with no machine in any file. Public include dir: `tools/damage-trace/src`. |
| `ostinato-damage-oracle` | STATIC | `ostinato-battle-scenarios` (PUBLIC) | The damage oracle's cartridge side: `DamageOracle`, which hosts the cartridge on the engine's SNES machine and runs its battle routines over a scenario, the trace of those routines, and a machine's state written and restored. |
| `ostinato-vi` | executable | `ostinato-vi-lib` | The game binary: publishes its identity, settles the asset root, installs a cartridge (`--install-rom <path> [--out <dir>]`, or the first-start flow), and prints the port name and engine version. |
| `ostinato-preview-dialog` | executable | `ostinato-vi-lib` | Field dialog messages typed by a scripted player, in a window or captured to files. |
| `ostinato-preview-battle` | executable | `ostinato-vi-lib` | Battles fought to their end by a scripted player, in a window or captured to files. |
| `ostinato-frames` | executable | `retropp::engine` | The cartridge on the engine's SNES machine, its frames and spans recorded between marked moments — the reference the previews are measured against. |
| `ostinato-damage-trace` | executable | `ostinato-damage-oracle` | The cartridge's battle routines traced as the game is played, checked against the port over seeded scenarios, and recordings replayed ([`tools/damage-trace/README.md`](../../tools/damage-trace/README.md)). |
| 31 test binaries | executable | `ostinato-vi-lib` + `GTest::gtest_main`; the eight battle binaries from `battle-commands` to `battle-whole` link `ostinato-damage-oracle` | Below. |

The test binaries, one per area:

| Binary | Covers |
|---|---|
| `ostinato-vi-tests` | The engine reached from the port library (one case, frozen). |
| `ostinato-vi-enum-tests` | The generated enums, the version axis and the packed wrappers. |
| `ostinato-vi-data-tests` | Character base stats and the RNG table. |
| `ostinato-vi-spell-tests` | Attack properties, magic points, dances, espers, natural magic. |
| `ostinato-vi-item-tests` | Item properties, shops, colosseum wagers. |
| `ostinato-vi-monster-tests` | Monster properties, Metamorph, steals, drops, rages, sketches, control, special attacks, alignment, sprites. |
| `ostinato-vi-formation-tests` | Formations, their auxiliary bytes, conditional battles. |
| `ostinato-vi-encounter-tests` | Battle groups and encounter rates. |
| `ostinato-vi-command-tests` | Battle command properties and their satellite tables. |
| `ostinato-vi-formula-tests` | The battle formula and mapping tables and the level progression. |
| `ostinato-vi-anim-tests` | Attack and item animation data. |
| `ostinato-vi-text-tests` | The text corpus, its codec and offsets. |
| `ostinato-vi-map-tests` | Map properties, parallax, animations, triggers. |
| `ostinato-vi-event-tests` | Event triggers, startup events, NPC properties. |
| `ostinato-vi-rom-tests` | The ROM regions and identity, the cartridge install and the first-start decision. |
| `ostinato-vi-world-tests` | The world map's data, tiles, sprites and the train ride. |
| `ostinato-vi-background-tests` | Battle backgrounds and character AI. |
| `ostinato-vi-misc-tests` | Blitz inputs, the trig tables, menu orders, game limits. |
| `ostinato-vi-ai-scripts-tests` | The monster AI scripts as data. |
| `ostinato-vi-text-engine-tests` | The field dialog text engine, over the whole English script. |
| `ostinato-vi-dialog-window-tests` | The field dialog window. |
| `ostinato-vi-battle-preview-tests` | The battle's picture and its presentation. |
| `ostinato-vi-battle-core-tests` | The battle's slots, clock, gauges, lines and loop; its start and its write-back. |
| `ostinato-vi-battle-commands-tests` | Targeting, the hit check, the attack frame, every command, the command menu and the executor. |
| `ostinato-vi-battle-damage-tests` | The damage formulas and the damage taken and shown. |
| `ostinato-vi-battle-status-tests` | Statuses set, cleared and taking effect. |
| `ostinato-vi-battle-effects-tests` | Special effects, the links between battlers, Morph. |
| `ostinato-vi-battle-espers-tests` | An esper's spells learned and its level-up bonus. |
| `ostinato-vi-battle-ai-tests` | Monster AI: targets, conditions, turns, counterattacks and the script-only commands. |
| `ostinato-vi-battle-rewards-tests` | What a battle's end gives: experience, levels, spells, items obtained, every ending, and the traced recordings. |
| `ostinato-vi-battle-whole-tests` | The cartridge's battle program called whole, as the field calls it: its entry, the scenario it starts from, and its start compared cell by cell with the port's: eight cases by hand, every formation once, and 1,300 drawn starts; the frames a whole battle's graphics program spends, a battle run to its end a loop step at a time against the same battle called once, and the first fight in Narshe whole against the port's. |

Every data table is tested over its whole corpus against a fixture generated from the original. The
battle binaries check each rule three ways — the port, the value worked out from the original, and the
cartridge's own routine run on the engine's SNES machine. Their hand checks sit in `tests/`; the tests
that ask only the cartridge — sweeps over drawn scenarios, replays, the battle whole, the oracle's own
checks — sit in `tests/oracle/` and are built into the same binaries. A binary that reads the cartridge
finds it through `FF6_VANILLA_ROM` and fails, naming the path, when it cannot read it; the binaries that
replay recordings find them through `OSTINATO_DAMAGE_TRACE_RECORDINGS`, baked in as
`tests/fixtures/damage-trace/`.

One more test is registered beside the binaries: `DamageTraceTool.ChecksEveryRoutineBriefly` runs
`ostinato-damage-trace --check 20` through `cmake/run_damage_trace_check.cmake`, passing on the tool's
exit code and removing the directory the tool writes.

How much of the cartridge a run asks is chosen when the tests run, by `OSTINATO_CARTRIDGE_CHECKS`
(`tests/with_cartridge.h`; `tools/damage-trace/README.md` § Checking). The build is the same either way.

| Run | Passed / total / skipped |
|---|---|
| Everything (`OSTINATO_CARTRIDGE_CHECKS` unset) — a developer's machine | 2166/2174/8 |
| Brief (`OSTINATO_CARTRIDGE_CHECKS=brief`) — every CI build job | 2040/2174/134 |

Eight skips are in both runs: the Japanese-cartridge cases, until a Japanese cartridge is available.
The brief run's other 126 are the tests that host the cartridge and have nothing but the cartridge to
check.

### C++ standard & compiler minimums

- **C++20 per target** via `target_compile_features(<t> PUBLIC cxx_std_20)` — not a global
  `CMAKE_CXX_STANDARD`, so the requirement rides each target and propagates through the link.
- **Configure-time compiler floor**, matching the engine's stated minimums: GCC 13+, Clang 16+,
  MSVC 19.38+ (VS 2022 17.8+), AppleClang 15+ (Xcode 15, first full C++20). A too-old compiler is a
  `FATAL_ERROR` at configure, not a confusing mid-build failure.
- **Warnings** `-Wall -Wextra -Wpedantic` (`/W4` on MSVC), applied PRIVATE to the port's targets
  only — the engine compiles under its own warning regime. There is no `-Werror`. AppleClang and
  clang-cl build without warnings. GCC reports two kinds the others do not:
  `-Wmissing-field-initializers` for designated initializers that leave fields at their defaults (the
  port's tables and the engine's `draw_state.h` alike) and `-Wcomment` for a `/*` inside a comment
  (`ai_target_selector.h`, `rom_asset.h` and the generated tables).

### Lean-build defaults

- **`CMAKE_BUILD_TYPE` defaults to `Release`** when empty on a single-config generator — never left in
  the toolchain's empty or debug default.
- **Dead-strip at link** on the game binary: `-Wl,-dead_strip` (Apple ld64);
  `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` (GNU/LLVM ld); `/Gy` + `/OPT:REF /OPT:ICF`
  (MSVC).
- **Symbol strip** post-build on Release non-MSVC: `strip -x $<TARGET_FILE:ostinato-vi>` (MSVC keeps
  symbols in a separate `.pdb`, so the `.exe` is already lean).

### GoogleTest

- **FetchContent, pinned `v1.17.0`.** The engine does not provide GoogleTest to consumers, so the port
  fetches its own.
- `gtest_discover_tests()` per binary; `enable_testing()` at the top level. `gtest_force_shared_crt ON`
  and `INSTALL_GTEST OFF` set before `FetchContent_MakeAvailable` (MSVC runtime match; no install
  pollution).

### The asset root baked into development builds

`ostinato-vi` and the two previews are built with `OSTINATO_PROJECT_ROOT` set to the checkout, so a
binary still sitting inside it reads and writes the cartridge there rather than in the per-user
directory (`assets::configureAssetRoot`). The test binaries carry no asset root of their own.

### Timing

The FF6 sim tick is the NTSC SNES field rate: master clock 236,250,000/11 Hz (≈21.4773 MHz);
non-interlace frame = 262 lines × 1364 cycles − 2 = 357,366 master cycles; period =
357,366 × 11 / 236,250,000 s = **16,639,265 ns** (≈60.0988 Hz). It is
`ostinato::kFieldTickPeriod` (`include/ostinato/ostinato.h`); a program that runs a loop sets it
on its `EngineConfig` as `static_cast<retropp::TickPeriodNs>(kFieldTickPeriod.count())`.

### Developer previews (`tools/previews/`)

One small program per piece of the picture, each showing that piece running on the installed
cartridge with scripted input — nobody drives a preview. Each plays in a window that closes itself, or
with `--capture <dir>` composes off screen and writes its frames to `<dir>`. They are tools, not the
game: the game binary carries none of their code, and nothing under `tools/` ships.

| Target | Source | Shows |
|---|---|---|
| `ostinato-preview-dialog [--capture <dir>] <index>...` | `tools/previews/dialog/` | Field dialog messages played by a scripted player ([`docs/engine/text/dialog.md`](../engine/text/dialog.md#preview)) |
| `ostinato-preview-battle [--capture <dir>] [--party A,B,C] [--background <n>] [--message-speed <n>] <formation>...` | `tools/previews/battle/` | Battles fought to their end by a scripted player, with the moments worth checking written as pictures in capture mode |

## Implementation details

**Files:**
- `CMakeLists.txt` — top-level: the Release default, the compiler floor, `add_subdirectory(engine)`,
  the warning flags, the libraries, the game binary with its lean-build options, the previews and
  tools, GoogleTest, the 31 test binaries and the damage trace's test.
- `cmake/run_damage_trace_check.cmake` — the damage trace's 20-draw check run as a test.
- `tests/with_cartridge.h` — how much of the cartridge a run of the tests asks, and the entry points a
  test asks it through.
- `include/ostinato/ostinato.h` — `ostinato::portName()` and `ostinato::kFieldTickPeriod`.
- `src/main.cpp` — the game binary's entry: identity, asset root, cartridge install, version line.
- `tests/test_smoke.cpp` — the one smoke case.

**Measured Release binary size (dev Mac, arm64, AppleClang 21, 2026-11-12):** **2,584,408 bytes
(~2.5 MB)**, stripped — no local symbols, 2,183 global ones. The binary carries the cartridge install
and the first-start flow, so it now reaches real engine surface.

## Open questions / future work

- **GCC's warnings.** Clearing them — or silencing `-Wmissing-field-initializers`, which fires on the
  designated-initializer style the port uses throughout — is its own change.
- **The game binary runs no loop yet.** The first window and run loop are the previews'; the game's
  `main()` installs the cartridge and exits.
