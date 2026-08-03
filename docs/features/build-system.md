# Build System & Test Harness

**Date:** 2026-08-01 (updated 2026-08-03)
**Status:** Complete

## Concept

The build rails for the port: a CMake project that consumes the Retro++ engine as a
subproject, a five-target topology (port library / game binary / three test runners), a GoogleTest
smoke harness proving the engine compiles-links-runs consumer-side, and a dev-only script that
stages ripped ROM assets into the canonical pack directories. Zero game behavior — this
stands up the infrastructure everything else builds on.

## Design decisions

### Engine consumption (verified against `engine/docs/guide/build-and-consume.md`)

- **`add_subdirectory(engine)` + link `retropp::engine`.** The engine ships as source and is
  attached as a submodule; the consumer builds it in-tree. Confirmed target alias `retropp::engine`
  (real target `retroppengine`).
- **`RETROPP_BUILD_TESTS` / `RETROPP_BUILD_EXAMPLES` left at their subproject default (OFF).** The
  engine turns both off automatically when it is not the top-level project, so a consumer `ctest`
  shows only the consumer's tests and no engine GoogleTest is fetched by the engine. We do not set
  them.
- **`retropp::testkit` is NOT linked.** The ROM-fidelity harness is unshipped engine
  work with no current consumer here; linking it now would pull test tooling into a build that has
  nothing to feed it.
- **Engine pin bumped `86f9d78` → `7c3707a`** (engine `origin/main` at the time). Picks up the
  frame-pacing debt fix and the tick-span interpolation unit. Nested engine submodules synced:
  SameBoy `v1.0.3`, SDL `release-3.4.10`.
- **Engine license:** dual AGPL-3.0 / commercial. Consumption awareness only; the port's own license
  posture is unchanged (engine + port source + tooling; zero copyrighted-derived content ships).

### Target topology

Five targets:

| Target | Kind | Links | Purpose |
|---|---|---|---|
| `ostinato-vi-lib` | STATIC | `retropp::engine` (PUBLIC) | All port code lives here so the binary and the test runners build against it without duplicating a `main`. Public include dirs: `include/` (port headers) + `src/` (data-layer headers, `data/*.h`). |
| `ostinato-vi` | executable | `ostinato-vi-lib` (PRIVATE) | The game binary. Currently prints the engine version + port name and exits 0. |
| `ostinato-vi-tests` | executable | `ostinato-vi-lib` + `GTest::gtest_main` | Smoke runner (engine reachability) — frozen baseline. |
| `ostinato-vi-enum-tests` | executable | `ostinato-vi-lib` + `GTest::gtest_main` | Full-corpus enum-surface tests + version-axis / packed-type behavior. |
| `ostinato-vi-data-tests` | executable | `ostinato-vi-lib` + `GTest::gtest_main` | Full-corpus data-table tests (character base stats, RNG table) vs generated fixtures. |

Each data table gets its coverage in a dedicated test source; test binaries are split by
concern so earlier baselines stay frozen as later ones grow.

**Static-lib scaffolding note.** There is no port code yet, but a STATIC library needs at least
one real symbol (an empty archive draws `ranlib` "no symbols" warnings). So the build scaffolds
`include/ostinato/ostinato.h` + `src/ostinato.cpp` with a trivial `ostinato::portName()` returning `"OstinatoVI"` — a
genuine (if minimal) port surface that establishes the lib→engine / binary→lib / tests→lib topology.

### C++ standard & compiler minimums

- **C++20 per target** via `target_compile_features(<t> PUBLIC cxx_std_20)` — not a global
  `CMAKE_CXX_STANDARD`, so the requirement rides each target and propagates through the link.
- **Configure-time compiler floor**, matching the engine's stated minimums: GCC 13+, Clang 16+,
  MSVC 19.38+ (VS 2022 17.8+). AppleClang added at 15+ (Xcode 15, first full C++20) since the engine
  guide omits it and the dev Mac builds with AppleClang. A too-old compiler is a `FATAL_ERROR` at
  configure, not a confusing mid-build failure. (Dev Mac verified: AppleClang 21.0.0.)
- **Warnings** `-Wall -Wextra -Wpedantic` (`/W4` on MSVC), applied PRIVATE to the port targets
  only — the engine compiles under its own warning regime. **No `-Werror` yet** (a warnings gate can
  be added once the port surface is real).

### Lean-build defaults

- **`CMAKE_BUILD_TYPE` defaults to `Release`** when empty on a single-config generator — never left in
  the toolchain's empty/debug default.
- **Dead-strip at link** on the shipped binary: `-Wl,-dead_strip` (Apple ld64);
  `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` (GNU/LLVM ld); `/Gy` + `/OPT:REF /OPT:ICF`
  (MSVC).
- **Symbol strip** post-build on Release non-MSVC: `strip -x $<TARGET_FILE:ostinato-vi>` (MSVC keeps
  symbols in a separate `.pdb`, so the `.exe` is already lean).
- Binary size is **measured** and recorded below.

### GoogleTest

- **FetchContent, pinned `v1.17.0`** (latest stable release at the time, verified via
  `git ls-remote`). The engine does not provide GoogleTest to consumers, so the port fetches its own.
- `gtest_discover_tests()` wiring; `enable_testing()` at the top level. `gtest_force_shared_crt ON`
  and `INSTALL_GTEST OFF` set before `FetchContent_MakeAvailable` (MSVC runtime match; no install
  pollution).

### Smoke test

One GoogleTest case asserting `retropp::version()` (the engine identity stamp, `std::string_view`,
contractually never empty) is non-empty — reached through the port library's PUBLIC engine link.
That proves the engine target compiles, links, and runs from the consumer side. **Baseline 1/1/0**
(passed/total/skipped).

### `src/main.cpp` stub

Logs `ostinato::portName()` + `retropp::version()` and exits 0. No window, no run loop, no
`EngineConfig` — the first windowed consumer feature belongs to later work, and this keeps the
build standup free of platform/windowing surface decisions. (`EngineConfig::setActive` would
otherwise require an identity; unneeded here.)

### Timing lock (recorded now, consumed at first run-loop instantiation)

The FF6 sim tick is the NTSC SNES field rate: master clock 236,250,000/11 Hz (≈21.4773 MHz);
non-interlace frame = 262 lines × 1364 cycles − 2 = 357,366 master cycles; period =
357,366 × 11 / 236,250,000 s = **16,639,265 ns** (≈60.0988 Hz). No consumer yet — recorded here
and wired as a raw `TickPeriodNs` when the run loop is first stood up (there is no SNES
timing preset in the engine yet). The 65C816 VM cycle budget is deferred to the engine VM-backend
kickoff.

### Dev-asset populate script (`scripts/setup-dev-assets.sh`)

- **Scope:** stages raw rip products **verbatim** (no format conversion) from the two unambiguously
  typed upstream modules into the two canonical pack targets:
  - `original-src/src/gfx/` → `assets/gfx/default/` (bitplanes `.1bpp/2bpp/3bpp/4bpp`, palettes
    `.pal`, screens `.scr`, `.cgx`, LZ-compressed `.lz`, and the module's `.asm`).
  - `original-src/src/sound/` → `assets/audio/default/` (`.brr` samples, sfx/song data, the SPC
    driver).
- **Module-selection rationale.** The pack model has exactly two targets (gfx, audio). `src/gfx/` is
  purely graphics and `src/sound/` is purely audio — a clean 1:1 module→pack copy with **zero
  per-file interpretation**, which is deliberate. Every other upstream module (`battle`, `btlgfx`,
  `cutscene`, `field`, `menu`, `text`, `world`, `event`) produces `.dat` **game-data** tables
  (monster props, map props, event scripts, dialogue) that later work parses into C++
  **directly from `original-src/`** — they are not presentation packs and have no home in a
  two-target pack model. `btlgfx`'s battle *graphics* bitplanes already live in `src/gfx/`; its own
  dir holds battle *animation data*, so excluding it from the gfx pack is correct.
- **macOS shell only** (`bash`) — dev populate is a dev-machine path; no `.ps1`.
- **Clear error if the rip has not been run:** the script checks for a representative rip product in
  each source module and aborts with the exact remedy (`make rip` in `original-src/` against a vanilla
  ROM in `original-src/vanilla/`) rather than silently staging nothing.
- **No transformation.** The engine-facing asset formats do not exist yet; any need for conversion or
  upstream-layout interpretation beyond a copy is deliberately out of scope for this script. The
  staged layout is regenerable and may be reshaped by the work that defines the engine formats and
  the runtime extractor's canonical layout.
- **Never commits anything.** `assets/*/default/*` is gitignored (only `.gitkeep` tracked); the script
  writes only into those gitignored trees.

## Implementation details

**Files created:**
- `CMakeLists.txt` — top-level; lean-build default, compiler floor, `add_subdirectory(engine)`, three
  targets, GoogleTest FetchContent, `gtest_discover_tests`.
- `include/ostinato/ostinato.h` — declares `ostinato::portName()`.
- `src/ostinato.cpp` — defines `ostinato::portName()` (the port library's first symbol).
- `src/main.cpp` — prints port name + engine version, exits 0.
- `tests/test_smoke.cpp` — the 1/1/0 smoke case.
- `scripts/setup-dev-assets.sh` — dev populate.
- `assets/gfx/default/.gitkeep`, `assets/audio/default/.gitkeep` — committed pack-dir placeholders.

**Files modified:**
- `engine` submodule pin `86f9d78` → `7c3707a`.

**Measured Release binary size (dev Mac, arm64, AppleClang 21):** **51,944 bytes (~52 KB)**,
stripped. The binary is this small *because* dead-strip works: `main()` references only
`retropp::version()` + `ostinato::portName()`, so the linker discarded the entire unreferenced
engine / SDL / SameBoy footprint. Post-strip local symbol count: 29. This is the lean baseline;
it will grow as the port references real engine surface (windowing, renderer, audio) later.

## Open questions / future work

- **Populate layout is provisional.** The runtime ROM-extractor work defines the *canonical*
  `assets/*/default/` layout that both populate paths must agree on; this dev-populate mapping is
  reshaped to match then (per `docs/features/asset-acquisition.md`). For now the two paths are not
  yet required to be byte-identical.
- **`-Werror`** deferred until the port surface is real enough to hold a clean warnings baseline.
- **Timing** is recorded, not wired — no run loop yet.
- **Windowing / `EngineConfig`** deliberately untouched so far.
