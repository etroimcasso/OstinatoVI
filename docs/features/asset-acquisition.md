# Asset Acquisition & Pack System

Feature doc, authored at inception 2026-08-01; rewritten 2026-08-28 when the acquisition model changed from extraction-to-files to reading in place. Design record for how copyrighted content reaches the running game, and for the pack system that sits alongside it.

## The model

The player supplies a Final Fantasy VI cartridge. It is copied once into their own files, and every launch reads what it needs directly out of that copy. **Nothing is extracted to intermediate files**, so there is no populated asset tree to detect, no half-populated state, and no second code path that only developers exercise.

The copy lives under the engine's asset root at `rom/cartridge.sfc`. The asset root is the per-user data directory — beside the player's saves — except for a binary still sitting inside its own source checkout, which uses the checkout so a developer's install lands in the tree they are working in.

## One route, every environment

A developer, a CI runner, and a player all take the same two steps:

1. **Install.** The image is identified by size and CRC32 and copied into the player's files. A player does this through a native file dialog on first launch; a developer or CI runner does it headlessly with `ostinato-vi --install-rom <path> [--out <dir>]`. Both call the same function, at the same point of startup.
2. **Ingest.** Each launch hosts the copy on a virtual machine, reads each content family whole by a version-keyed table of cartridge addresses, and destroys the machine. The bytes are held in memory for the life of the program.

Accepted cartridges are the three the upstream disassembly accepts: Final Fantasy VI 1.0 (J), Final Fantasy III 1.0 (U), and Final Fantasy III 1.1 (U). A 512-byte copier header is recognised by length and dropped before anything reads or identifies the bytes; the checksum is taken over the headerless image. Anything else is refused with a message naming what would work.

Addresses come from the upstream rip lists, which are keyed by **language** rather than revision — the two US cartridges share one address set because their ripped data is identical. Where a family exists in only one language (item type names in the US cartridges, character titles in the Japanese one), asking for the other says so rather than answering with a wrong place.

The developer-facing surface is documented at [docs/engine/assets/rom-ingestion.md](../engine/assets/rom-ingestion.md).

### Why in place rather than extracted

Reading the cartridge directly removes the whole class of problems that an extracted asset tree creates: a partially written tree, a stale tree after a format change, a developer tree populated by a different tool than the player's, and a build that has to know whether content is present. It also means the addresses are exercised on every launch rather than once at extraction time, so a wrong one surfaces immediately and is attributable.

The cost is that reading needs the machine. Until the engine's SNES backend is built, constructing that machine fails, and the tests that read a cartridge skip with that reason stated. The install, the address table, and the identity table need no machine and are exercised now.

## Pack system

Graphics and audio packs remain the model for **replacement** content — swappable directories under `assets/gfx/` and `assets/audio/`, selected independently, with `default/` as the canonical load target. Packs are how a player substitutes their own art or music; they are not how the original content arrives, which is the change from the earlier design.

Engine-side per Retro++'s pack model: startup scan, independent gfx/audio selection persisted in engine config. Fallback chain (selected pack → `default/` for missing assets) and the `pack.json` manifest format are locked when the pack loader is implemented against this consumer; record decisions here.

## Ship rules (locked)

- No ROM, no disassembly, and no content derived from either is ever distributed.
- The distributable build empties `assets/*/default/` before packaging; a CI smoke check fails the package if any default-pack byte is present.
- User packs may ship only if they contain zero copyrighted-derived bytes.
- `.gitignore` bans `*.sfc` / `*.smc` / `*.fig` everywhere in the tree and `assets/*/default/` contents (`.gitkeep` excepted) — in force since the root commit. The ban covers the installed cartridge too: a development install writes into the checkout, and that file is ignored by the same rule.
