# FF6 C++ Port — Design Context

Authored at inception, 2026-08-01. This document holds project intent, hardware scoping, premises with rationale, and open questions.

## Intent

A behaviorally faithful native reimplementation of Final Fantasy VI (Super Famicom / SNES) as a consumer of the **Retro++ engine**. Same observable behavior as the original given the same inputs and RNG state — running as ordinary native code on Windows/macOS/Linux through the engine's platform layer. Not an emulator; not a mechanical ASM translation.

Two goals coexist by design:

1. **The port itself** — FF6 running natively on Retro++.
2. **Engine growth** — this is Retro++'s first SNES consumer. The engine's SNES surface (SPC700 + S-DSP audio backend, `Wdc65816` VM backend, any renderer capability the SNES demands beyond current coverage) **gets designed against this project's needs**. The SNES audio system's absence from the engine today is deliberate; this port is the anvil it is forged on.

## Upstream

- `everything8215/ff6` @ `1ea47b5` — ca65/cc65 toolchain, GPL-3.0.
- Builds three ROMs: FF6 1.0 (J) `45EF5AC8`, FF3 1.0 (U) `A27F1C7A`, FF3 1.1 (U) `C0FA0464`.
- **Rip model (differs from pret repos):** much of the data/asset corpus is not committed upstream — `make rip` extracts it from a user-supplied vanilla ROM into the module directories (`tools/extract_assets.py`, CRC-identified, pure Python). A fresh checkout's corpus is incomplete until a ROM is provided.
- Ten source modules: `battle`, `btlgfx`, `cutscene`, `event`, `field`, `gfx`, `menu`, `sound`, `text`, `world` — plus `include/` (constants, macros, hardware defs), `cfg/` (ld65 linker configs, incl. a separate one for the SPC program), `tools/` (rip/encode/checksum tooling).

## Hardware scoping (SNES)

| Field | Value |
|---|---|
| CPU | WDC 65C816 (16-bit, 24-bit address bus), NTSC master clock; FF6 is a SlowROM title (~2.68 MHz effective) |
| Mapping | HiROM, 24 Mbit (3 MiB) ROM |
| Save RAM | 8 KiB battery-backed SRAM |
| Video | S-PPU, 256×224 NTSC output (`ViewportResolution::Snes` preset already in engine); background modes incl. Mode 7 (world map / airship); HDMA effects; color math / transparency; OAM sprites |
| Audio | S-SMP (Sony SPC700 @ ~1.024 MHz) + S-DSP: 8 BRR sample channels, echo, pitch modulation, noise. FF6's driver is `src/sound/ff6-spc.asm` — a **separate program for a separate CPU**, linked via its own `cfg/ff6-spc.cfg` |
| Expansion chips | None (no SA-1/SuperFX/DSP-n) |
| Input | SNES joypad (B Y Select Start ↑↓←→ A X L R) — maps onto the engine's declared-action input vocabulary |
| Timing | NTSC SNES ≈ 60.0988 Hz field rate — locked as a 16,639,265 ns sim tick; derivation recorded in `docs/features/build-system.md` |

**Renderer capability survey required before game-code porting begins:** enumerate FF6's actual PPU usage (modes per game state, HDMA patterns, color-math cases, sprite limits it leans on) against the engine's current surface (per-layer projective transforms, screen-space effects, shader hook) and identify gaps as engine work items.

## The port law

**The dev surface is clean C++; ROM fidelity is byte VALUES in backing data, never instruction shapes.** No 65C816 idioms; no magic numbers ever (sole carve-out: a documented preserved original-game bug, tracked in `docs/Bugs.md` when the first one appears). The two principles: (1) C++ pleasant in C++ terms; (2) data tables self-labeling — designated initializers at every nesting level, never positional.

65C816 offender classes to watch for:

- Accumulator/index width residue — masking or branching that transcribes `REP`/`SEP` 8↔16-bit mode juggling where the C++ type already fixes the width.
- Bank-byte arithmetic — `>>16`/`&0xFF0000` shapes carrying 24-bit address mechanics onto values that are typed IDs or plain indices in the port.
- Carry-flag comparison transcriptions (`CMP`/`SBC` carry idioms spelled as arithmetic).
- BCD shapes — decimal-mode math transcribed digit-wise where the observable is just a number rendered in decimal.
- Mechanism-shaped emitted constants — bit positions/shift amounts whose only consumer reconstructs a hardware mechanism; emit the observable boundary value instead.

## Data derivation

Game data carrying exact original values (stats, tables, formulas) is emitted by deterministic parser tools (`tools/asm_parser/`, targeting this repo's ca65 `.asm`/`.inc`/`.dat` conventions and macro set) reading `original-src/` — never transcribed by hand. Some parser inputs exist only after `make rip` has run against a vanilla ROM.

## Distribution & asset model

- **The distribution never requires the disassembly.** `original-src/` is a dev submodule only; the released artifact carries no disassembly gitlink and no build/runtime reference to it.
- **End users supply their own ROM — selected in-app at first start** (Zelda64 / Ship of Harkinian model). The port's first launch with an empty pack presents a ROM-selection flow, runs extraction internally, and populates the canonical asset directories — the same layout the dev populate path produces. No separate manual tool step in the user experience. **No extraction product is ever committed or shipped.**
- Full pack model in `docs/features/asset-acquisition.md`.

### What is extracted, and what is compiled in

The line is the content's **nature**, not its file format or its size, and it is drawn once for the whole project rather than re-argued at each table:

- **Extracted from the player's ROM, never compiled in and never committed:** game text (dialogue, names, descriptions, battle and menu strings), map layouts and tilemaps, artwork of every kind (backgrounds, sprites, fonts, portraits), and music and sound. These are authored expression.
- **Compiled into the binary:** the mechanics and the numeric tables that encode them — stats, formulas, encounter and formation tables, passability and terrain bits, animation and command properties, ids, curves. These are rules, not expression.

A borderline case is decided by asking what the bytes *are*: a table of tile indices that draws a piece of the world is content even when it is small (the world map's tile patches moved to extraction on exactly this reasoning), while a byte that says whether the party can walk on a tile is mechanics even when it sits beside one.

**One extraction route, one code path.** The port's own extractor reads the ROM and writes the asset files; CI, a developer's machine and a player's install all populate the same way through the same binary. The upstream disassembly's own rip tooling is never a build or CI dependency — it is a design reference, not an asset supply.

## Version support (decided 2026-08-02)

**The port targets all three releases the disassembly builds** — FF6 1.0 (J) `45EF5AC8`, FF3 1.0 (U) `A27F1C7A`, FF3 1.1 (U) `C0FA0464`. This is a foundational design axis, not a later add-on:

- **One binary, runtime version identity.** A `GameVersion` state (JP 1.0 / US 1.0 / US 1.1) is carried at runtime; the cartridge the user supplies determines it, CRC-identified on install and re-derived on every launch from the copy itself, so there is no recorded version to drift. Language (JP/EN) and revision derive from it, mirroring the upstream build's own configuration axes. Cartridge addresses are keyed by **language**, not revision: the two US releases share one address set because their ripped data is identical.
- **Version differences are finite and enumerable.** The disassembly builds all three ROMs from one source tree behind conditional flags — roughly 430 conditional sites, concentrated overwhelmingly in text presentation (menu ~277, battle graphics/messages ~86, cutscenes ~35); core battle/field/event simulation is nearly version-free. Each ported system carries its conditional sites as explicit version-dependent behavior; because the sites are mechanically searchable in the upstream, none can be silently missed.
- **Text pipeline is version-aware from the core.** EN uses DTE compression and EN character tables; JP uses MTE/kana/kanji tables and wide-glyph metrics. Both are first-class inputs to the text and menu system designs — not a retrofit.
- **Validation posture:** behavior is validated against FF3 1.1 (U) now (the development ROM on hand). JP and US 1.0 support is architected in from the data layer up; their behavioral validation activates when those ROMs are available, with per-version tests registered and visibly skipped until then.

## Named presentation enhancements

Framework (FF Pixel Remaster / Halo MCC model): the default install plays exactly like the original ROM; every named enhancement is **user-opt-in, OFF by default, presentation-only** — never gameplay, RNG, AI, scripts, or cue timing.

- **External audio packs** — per the engine's dual-audio-backend design: (a) original chiptune via VM-hosted SPC700 driver + emulated S-DSP; (b) user-supplied audio-pack replacement at engine cue points. Both backends are engine-mandatory; the pack side doubles as this port's first named enhancement.
- Further named enhancements (output scaling ships engine-side already; anything FF6-specific): TBD — none currently planned.

**Opt-in developer tools (distinct class, decided 2026-08-02):** the original game's debug menu (assembled out of shipped ROMs by the upstream's `DEBUG` flag) will be ported as an opt-in facility, OFF by default. It is tracked separately from presentation enhancements because it mutates game state; the presentation-only rule above stays intact.

## Licensing notes

- Upstream disassembly is **GPL-3.0**. It serves as the derivation reference (intent, mechanics, data). Deriving *shipped code* from its expression — including its Python tooling such as `extract_assets.py` — requires a recorded license determination in `docs/licensing/LICENSING.md` first.
- Mechanical game data (stats, tables, formulas) is uncopyrightable mechanics; specific expression (sprites, music sequences, dialogue text) is copyrighted (Square Enix) and never ships, never enters the tree.
- Engine (`engine/`) is its own repo with its own license; SameBoy (MIT) and SDL (zlib) ride inside it.

## Open questions

| # | Question | Decide by |
|---|---|---|
| 1 | **Primary version contract** — ✅ **RESOLVED 2026-08-02: all three releases are in scope** (see § Version support). Behavioral contract validated on FF3 1.1 (U) now; version axis designed in from the data layer; J/1.0 validation deferred until those ROMs are available. | ~~Before game-code porting begins~~ resolved |
| 2 | **CI corpus access** — ✅ **RESOLVED 2026-08-01: runner-local ROM.** Each self-hosted runner holds the vanilla ROM outside the workspace, referenced via runner-env `FF6_VANILLA_ROM`. Parser jobs stage it into `original-src/vanilla/` for the parsers' own byte cross-checks; the C++ tests read it directly, so every platform reaches the same corpus and reports the same result. Missing ROM on a runner surfaces loudly. Provisioning documented in `docs/features/ci.md`. | ~~CI standup~~ resolved |
| 3 | **65C816 VM backend** — SameBoy has no SNES side. Candidates: extracted emulator core (licensing: Snes9x non-commercial = unusable; bsnes/Mesen2 GPL; smaller MIT/BSD cores exist) vs hand-written minimal 65C816 interpreter. SPC700 + S-DSP core doubly so (audio backend). May resolve differently per chip. | Engine VM/audio kickoff (engine-level decision) |
| 4 | **Sim tick-rate value** — ✅ **RESOLVED 2026-08-01: 16,639,265 ns** (357,366 master cycles at 236,250,000/11 Hz ≈ 60.0988 Hz field rate); derivation in `docs/features/build-system.md`. CPU cycle budget deferred to the engine VM-backend kickoff (no current consumer). | ~~Build-system standup~~ resolved |
| 5 | **Engine work sequencing** — how FF6-driven engine units (SNES audio, 65816 VM) interleave with the engine's existing v1 roadmap on shared engine `main`. | Ongoing |
