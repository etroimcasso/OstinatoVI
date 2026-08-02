# Asset Acquisition & Pack System

Feature doc, authored at inception 2026-08-01. Design record for the asset pipeline; the populate script exists today, the runtime extraction tool is future work. Engine-side pack selection/loading is Retro++ surface.

## The model

Graphics and audio assets are **packs** — swappable directories under `assets/gfx/` and `assets/audio/`, selected independently. `assets/gfx/default/` and `assets/audio/default/` are the canonical engine load targets: structurally committed via `.gitkeep`, **contents gitignored and never committed**.

## Two populate paths, one layout

1. **Dev populate (dev-time only).** The developer runs `make rip` in `original-src/` once against a vanilla ROM in `original-src/vanilla/` (any filename; identified by CRC32; headerless — 3,145,728 bytes). A checked-in populate script (`scripts/setup-dev-assets.sh`) then **stages raw rip products verbatim** into `assets/*/default/`, layout mirroring the upstream module structure. Engine-facing format conversion is explicitly deferred to the work that defines those formats; the staged layout is regenerable and may be reshaped then.
2. **Runtime populate (end user) — first-start ROM selection, in-app.** On first launch with an empty `default/` pack, the port itself presents a ROM-selection flow: the user picks their legitimate ROM, extraction runs inside the app, the **identical layout** is written into `assets/*/default/`, and the game proceeds — the same first-start model as Zelda64 recompiled or Ship of Harkinian. There is no manual tool step in the user experience. The extraction code may additionally exist as a standalone CLI for power users — decided at extraction-tool design time — but the canonical path is the first-start flow. No ROM, no disassembly, no extracted content is ever distributed.

Both paths land the same bytes in the same layout, so the engine's daily dev load path IS the shipped load path — no dev/prod branch.

## Runtime extraction tool

- Ships with the port (code only), invoked by the first-start ROM-selection flow above. Identifies the ROM by CRC32 (accepting the same three versions upstream does: `45EF5AC8` J 1.0 / `A27F1C7A` U 1.0 / `C0FA0464` U 1.1), refuses headered/modified ROMs with a clear message (or offers header stripping — decide at design time).
- **Design reference vs. code derivation:** upstream's `tools/extract_assets.py` (CRC identification, HiROM mapping, LZSS decompression, full asset walk) is the proof that this tool is tractable in pure Python — but upstream is **GPL-3.0**, so deriving our shipped tool's code from it requires a recorded license determination (`docs/licensing/LICENSING.md`) first. Options at design time: clean implementation from the disassembly's data layout (the layout itself is read from our derivation reference), or accept GPL for the tool as a standalone program.
- Full design (offsets, formats, output manifest) is pinned when that work starts; deferred now.

## First-start sequencing — port-side only, no engine modification

An engine-side bootstrap surface was briefly considered and rejected: the engine is a library the port's `main()` drives, and asset loading happens only when the port requests it — so no engine modification or restart is needed. The sequence is plain port-side control flow:

1. `main()` checks `assets/*/default/` for extracted content.
2. Empty → run the extraction flow (acquire the user's ROM via a simple file-selection prompt, extract, write the layout).
3. Then proceed into normal engine construction / asset loading — same code path as every subsequent launch.

No pre-asset engine state, no restart, no engine work item.

## Selection, fallback, manifest

Engine-side per Retro++'s pack model: startup scan of `assets/gfx/` and `assets/audio/`, independent gfx/audio selection persisted in engine config, `default/` on first launch. Empty-`default/` startup triggers the first-start ROM-selection flow (above) — never a silent failure, and never a bare error message pointing at a tool the user must go run. The flow runs entirely port-side in `main()` before engine asset loading begins (see the First-start sequencing section); no engine involvement. Fallback chain (selected pack → `default/` for missing assets) and `pack.json` manifest format: locked when the pack loader is implemented against this consumer; record decisions here.

## Ship rules (locked)

- Distributable build empties `assets/*/default/` before packaging; a CI smoke check fails the package if any default-pack byte is present.
- User packs may ship only if they contain zero copyrighted-derived bytes.
- `.gitignore` bans `*.sfc` / `*.smc` / `*.fig` everywhere in the tree and `assets/*/default/` contents (`.gitkeep` excepted) — in force since the root commit.
