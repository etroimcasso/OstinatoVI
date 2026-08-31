# ROM ingestion

How a player's cartridge reaches the running game, and how to read a part of it.

## The route

Two steps, and no third way in:

1. **Install** — the player points at their cartridge. It is identified, and the image
   is copied into their own files as `rom/cartridge.sfc`. Once, ever.
2. **Ingest** — every launch reads the families the game needs straight out of that
   copy, at the addresses the cartridge itself uses.

The install happens either through a native file dialog on a first launch, or headlessly
via `ostinato-vi --install-rom <path>`. Both call the same function.

```
$ ostinato-vi --install-rom ~/roms/ff3.sfc
Installed Final Fantasy III 1.1 (U).
```

Add `--out <dir>` to install somewhere other than the default location — what CI does,
and what you want when testing an install without touching your own files.

## Where the cartridge lives

Under the engine's asset root, at the literal path `rom/cartridge.sfc`.

The asset root is the per-user data directory — the same place saves go — so a player's
cartridge sits beside the rest of their files, wherever the program itself happens to be
installed. A binary still sitting inside its own source checkout uses the checkout
instead, so a developer's install lands in the tree they are working in. `configureAssetRoot`
in `src/main.cpp` picks between the two; `assets::developmentAssetRoot`
(`src/assets/asset_root.h`) is the decision, and it compares path components rather than
strings so a sibling directory whose name merely begins with the project's is not
mistaken for being inside it.

**Asset paths are always spelled in place.** Every site that names the cartridge writes
the string `"rom/cartridge.sfc"` literally. The engine's build scan reads these paths out
of the source text, so a path held in a variable or returned from a helper is invisible
to it.

## Recognising a cartridge

Three revisions are accepted: Final Fantasy VI 1.0 (J), Final Fantasy III 1.0 (U), and
Final Fantasy III 1.1 (U). Recognition is by size and CRC32 over the whole image, so a
modified or mis-dumped file is refused outright rather than half-read.

```cpp
#include "assets/cartridge.h"

// Some dumps carry a 512-byte copier header. It is recognised by length and dropped
// before anything reads or identifies the bytes.
const std::span<const std::uint8_t> image = assets::stripCopierHeader(bytes);

if (const std::optional<GameVersion> version = assets::identifyRom(image)) {
    // language(*version) says which of the two address sets applies.
}
```

`include/ostinato/rom_identity.h` carries the facts: `kRomSizeBytes`,
`kCopierHeaderBytes`, and `kRomIdentities`, the three `{version, crc32}` rows.

## Installing

```cpp
const assets::InstallResult result = assets::installCartridge(romPath, retropp::assetRoot());
if (!result.succeeded) {
    std::cerr << result.message << "\n";   // names the cartridges that would work
}
```

`installCartridge` takes either a path or an image already in memory. The copy is
written atomically, so an interrupted install leaves the previous cartridge intact
rather than a truncated one, and a refused image writes nothing at all.
`assets::cartridgeInstalled(root)` answers whether one is already there.

The dialog half lives in `src/assets/first_start.h`. `ensureCartridgeInstalled` runs the
whole decision: check, ask, install, check again. A dismissed dialog is re-offered once
with the consequence stated; a second dismissal is taken as an answer; a dialog that
*failed* is not re-offered, because it will fail again. The four-argument overload takes
the presence check, the dialog, and the install as callables, which is how the tests
drive every path without a window or a filesystem.

## Reading families out of it

```cpp
const assets::IngestedContent content = assets::ingestCartridge();

content.version;                       // which revision this is
content.text.itemName(ItemId::POTION); // the corpus, read out of the cartridge
content.worldTiles;                    // the world-map tile pool
```

Reading a cartridge needs no emulator. Final Fantasy VI is a HiROM cartridge, which maps
its banks straight onto the image, so an address becomes an offset by arithmetic —
`assets::HiRomImage` (`src/assets/rom_reader.h`) is the one place that knows how, and it
is the only place in the port that does arithmetic on a cartridge address.

The spans inside `IngestedContent` outlive the call: the tile pool is catalogued with the
engine's data library, which resolves an entry once and keeps it for the life of the
program.

There is an overload taking an image directly, for reading a cartridge that has not been
installed — the tests use it.

Failures throw. A cartridge that cannot be read, or one that is not an accepted revision,
is a real problem, and an empty corpus would look like a game with nothing to say.

## Where each family lives

`src/data/rom_regions.h` maps a family to a place in the cartridge.

```cpp
#include "data/rom_regions.h"

// Where the dialogue lives in a US cartridge.
const std::optional<retropp::MemoryRegion> place = romRegion(RomAsset::DLG1, Language::EN);

// The family a text class's records live in.
const RomAsset asset = textClassAsset(TextClass::ITEM_NAME);
```

- `RomAsset` (`include/ostinato/rom_asset.h`) names all 156 families — the script, the
  graphics, the sound samples, and the numeric tables alike.
- `romRegion` returns `nullopt` when a language does not ship a family. The US cartridges
  name item types and the Japanese one gives characters titles; neither has the other's
  table.
- `romRegions()` is the whole 309-row table, for iteration.

Addresses are the cartridge's own. A family wider than the bank it starts in reads
straight through — the dialogue bank does exactly this — because a read is one extent
rather than a per-bank walk.

## World tile patches

The world map's story-gated modifications point into a pool of tiles that lives in the
cartridge, so the decoded view is built over ingested bytes rather than compiled-in ones.

```cpp
for (const auto& chunk : worldModifications(WorldMapId::WORLD_OF_BALANCE)) {
    const WorldTilePatch patch = content.worldTiles.patchAt(chunk.patch);
    patch.destination();  // where it stamps, in columns and rows
    patch.width();        // the high nybble of the packed size byte
    patch.height();       // the low nybble
    patch.tiles();        // width x height, row-major
}
```

A chunk's reference counts from the start of the modification block, which opens with the
per-world chunk lists; `WorldTilePool` knows how far into that block the tiles begin and
does the subtraction. A reference outside the pool, or a record the pool is too short to
hold, gives a patch whose `valid()` is false rather than a read past the end.

## Gotchas

- **The identity comes first.** `EngineConfig::setActive` must run before anything
  resolves the asset root, because the per-user directory is derived from the app
  identity. `main` does this ahead of the flag and the dialog alike.
- **Nothing here skips.** A test that wants a cartridge either reads one or fails. There
  is no emulator in this path to be missing.
- **A family with no consumer still has a region.** The table carries every family the
  cartridge holds, including ones nothing reads yet. Only the families with a consumer are
  read at launch.

## Where to change things

| To do this | Edit |
|---|---|
| Read a family nothing reads yet | The read section of `ingestCartridge` in `src/assets/cartridge.cpp` — ask `romRegion` for its place and read it |
| Change where the cartridge is kept | The literal `"rom/cartridge.sfc"` at every site that names it (registration, presence, install) |
| Change what the dialog says, or how many times it asks | `src/assets/first_start.cpp` |
| Accept a different set of cartridges | The CRC branches upstream, then regenerate — the identity table is emitted, not hand-written |

`include/ostinato/rom_asset.h`, `include/ostinato/rom_identity.h`, and
`src/data/generated/rom_regions_data.inc` carry an `AUTO-GENERATED` banner naming the
command that produces them. Edit them by regenerating, not by hand; each has a fixture
under `tests/fixtures/` that a test compares every row against.
