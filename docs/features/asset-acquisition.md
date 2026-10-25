# Asset Acquisition

Feature doc, authored at inception 2026-08-01; rewritten 2026-08-28 when the acquisition model changed from extraction-to-files to reading in place. Design record for how copyrighted content reaches the running game.

## The model

The player supplies a Final Fantasy VI cartridge. It is copied once into their own files, and every launch reads what it needs directly out of that copy. **Nothing is extracted to intermediate files.**

The copy lives under the engine's asset root at `rom/cartridge.sfc`. The asset root is the per-user data directory — beside the player's saves — except for a binary still sitting inside its own source checkout, which uses the checkout so a developer's install lands in the tree they are working in.

## One route, every environment

A developer, a CI runner, and a player all take the same two steps:

1. **Install.** The image is identified by size and CRC32 and copied into the player's files. A player does this through a native file dialog on first launch; a developer or CI runner does it headlessly with `ostinato-vi --install-rom <path> [--out <dir>]`. Both call the same function, at the same point of startup.
2. **Ingest.** Each launch reads each content family whole out of the copy, by a version-keyed table of cartridge addresses. The bytes are held in memory for the life of the program. FF6 is a HiROM cartridge, so an address becomes an image offset by arithmetic — no emulator is involved.

Accepted cartridges are the three the upstream disassembly accepts: Final Fantasy VI 1.0 (J), Final Fantasy III 1.0 (U), and Final Fantasy III 1.1 (U). A 512-byte copier header is recognized by length and dropped before anything reads or identifies the bytes; the checksum is taken over the headerless image. Anything else is refused with a message naming what would work.

Addresses come from the upstream rip lists, which are keyed by **language** rather than revision — the two US cartridges share one address set because their ripped data is identical. Where a family exists in only one language (item type names in the US cartridges, character titles in the Japanese one), asking for the other says so rather than answering with a wrong place.

The developer-facing surface is documented at [docs/engine/assets/rom-ingestion.md](../engine/assets/rom-ingestion.md).

The engine's SNES virtual machine will eventually be able to host a cartridge and answer reads itself. That path waits on Snaggletooth, the clean-room SNES implementation the engine uses, which is both unfinished and LoROM-only at present. When it arrives it becomes a second implementation of the same step, checkable against this one.

## Ship rules (locked)

- No ROM, no disassembly, and no content derived from either is ever distributed.
- `.gitignore` bans `*.sfc` / `*.smc` / `*.fig` everywhere in the tree and `assets/*/default/` contents (`.gitkeep` excepted) — in force since the root commit. The ban covers the installed cartridge too: a development install writes into the checkout, and that file is ignored by the same rule.
