# World animation frames

## Public surface

```cpp
#include "data/world_anim.h"
#include "data/rom_regions.h"

// The frames, over the region read out of the player's cartridge.
auto place  = ostinato::romRegion(ostinato::RomAsset::WORLD_ANIM_FRAMES,
                                  ostinato::Language::EN);
auto frames = ostinato::WorldAnimFrames{cartridgeBytesFor(*place)};

auto frame  = frames.frameAt(ostinato::WorldAnimFrameId::FRAME_1);
frame.spriteCount();        // how many sprites to draw
frame.sprite(0);            // WorldAnimSprite — offsets, tile, palette, flips

// The tables that step through the frames.
ostinato::dismountChocoboFrames();   // std::span<const WorldAnimFrameStep>
ostinato::smokingAirshipFrames();
ostinato::birdFrames();
```

## What it is

Everything the overworld animates — the airship, chocobos, the party leader, the
ship, the bird, the Blackjack and the Falcon — is drawn from a set of 108
**frames**. A frame is one arrangement of hardware sprites: how many to place,
and for each, where it sits relative to the object, which tile it shows, and how
that tile is coloured, layered and mirrored.

Those arrangements are game artwork, so they are **not** part of this repository.
They live in the player's cartridge and are read in place, the same way the
graphics they arrange are. What the port ships is the vocabulary that names them
(`WorldAnimFrameId`), the decode views that read them, and the small tables that
say which frame comes next in a cycle.

### The frame ids are positions, not descriptions

`WorldAnimFrameId` runs `FRAME_0` to `FRAME_107`. The names are deliberately
uninformative: the cartridge labels *groups* of frames — airship, chocobo,
character, ship, esper Terra, smoking airship, bird, falcon — and never an
individual one, so naming each frame would invent meaning the game does not
state. The group headings are carried into the enum as comments, quoted as the
cartridge writes them (so a frame number inside a heading is hexadecimal, while
the enumerators count in decimal).

Better names are welcome. The right time is once the drawing code exists and each
frame's use is plain from a caller.

`FRAME_0` is the blank frame. It stores nothing, and both world draw routines skip
an object showing it, so it is how "this object is not drawn right now" is
expressed.

## Reading a frame

`WorldAnimFrames` takes the whole region — the pointer table followed by the
records it addresses — and hands out views:

```cpp
class WorldAnimFrames {
    explicit WorldAnimFrames(std::span<const std::uint8_t> region);
    bool valid() const;

    std::uint16_t   offsetOf(WorldAnimFrameId frame) const;
    WorldAnimFrame  frameAt(WorldAnimFrameId frame) const;

    std::span<const std::uint8_t> pointerTable() const;
    std::span<const std::uint8_t> records() const;
};

class WorldAnimFrame {
    bool            valid() const;
    std::uint8_t    spriteCount() const;   // what the game draws
    std::size_t     storedRows() const;    // what the record holds
    WorldAnimSprite sprite(std::size_t row) const;
    std::span<const std::uint8_t> bytes() const;
};
```

Both are **views, not copies**: they hold a `std::span` over the cartridge bytes
and decode on access, so the image has to outlive them. A span too short to hold
what it is being asked for yields an empty view — `valid()` returns false and
every reader returns zero or empty — rather than reading past the end.

One sprite:

```cpp
struct WorldAnimSprite {
    std::uint8_t x, y, tileLow, attributes;   // the cartridge's four bytes

    std::int8_t   offsetX() const;            // signed, from the object's position
    std::int8_t   offsetY() const;
    std::uint16_t tileIndex() const;          // 0..511 — attributes carries bit 8
    std::uint8_t  paletteIndex() const;       // 0..7
    std::uint8_t  layerPriority() const;      // 0..3
    bool flippedHorizontally() const;
    bool flippedVertically() const;
};
```

The four bytes are stored untouched and the fields are named through accessors,
so a caller asks a question instead of masking. `tileIndex` and `paletteIndex`
both live in the attribute byte, which is why the type keeps that byte rather
than pre-splitting it — `sizeof` is 4, alignment 1, and a row can be read
wherever a record puts it.

### Draw `spriteCount()`, not `storedRows()`

Eight frames — the six serpent-trench arrows and two others — physically store
more sprite rows than their count byte declares. The game reads exactly the
declared count, so those rows never reach the screen.

`spriteCount()` is what to draw. `storedRows()` is what the record holds, and it
is exposed only so the extra rows are visible rather than silently dropped.
Drawing `storedRows()` worth of sprites puts arrows and rocks on screen that the
original never shows. See
[Bugs.md](../../Bugs.md) → *World animation frames store sprite rows the game
never draws*.

### Two frames can share one record

Offsets repeat, and that is correct:

- `FRAME_0` and `FRAME_1` share offset 0 — the blank frame is a label with no
  bytes of its own, sitting where the first airship frame begins.
- `FRAME_90` and `FRAME_91` are one record under two names.

Neither pair is deduplicated: the pointer table has one entry per frame either
way. This is also why a record's length cannot come from "the next pointer minus
this one" — for a shared offset that subtraction is zero. `frameAt` bounds a
record by the next offset that is strictly greater, which gives the record's real
extent including any rows past the declared count.

## Frame sequences

Three small tables say which frame comes next in a cycle. They hold frame
numbers, not arrangements, so they are ordinary compiled-in data:

```cpp
struct WorldAnimFrameStep {
    std::uint16_t    index;   // the step, equal to the array position
    WorldAnimFrameId frame;   // the frame shown at it
};
```

| Accessor | Steps | Covers |
|---|---|---|
| `dismountChocoboFrames()` | 5 | The dismounting-chocobo cycle. |
| `smokingAirshipFrames()` | 43 | The damaged airship, six frames per altitude row. |
| `birdFrames()` | 4 | The bird's four-step cycle. |

The airship table has a shape worth knowing before you index it. Altitude picks a
**row** in steps of six, and the step within a row runs **0 to 6** — one wider
than the row. Each row's last read therefore falls through onto the next row's
leading `FRAME_0` and the sprite blanks; the table's trailing entry exists to
serve that read for the final row. `kSmokingAirshipFrameRowStride` is the row
width, and `kSmokingAirshipSecondLabelStep` is where the cartridge's second label
for this table falls — on a row boundary, which is what makes the two labels one
table rather than two.

## Backing data / where to change

| What | Where |
|---|---|
| Frame ids | `include/ostinato/world_anim_frame_id.h` (generated) |
| One sprite row | `include/ostinato/world_anim_sprite.h` |
| Views + sequence accessors | `src/data/world_anim.{h,cpp}` |
| Sequence rows | `src/data/generated/world_anim_*_frames_data.inc` (generated) |
| Where the frames live in the cartridge | `src/data/rom_regions.h`, `RomAsset::WORLD_ANIM_FRAMES` |

To change **which frame a cycle shows**, edit the matching `.inc` row and its row
in `tests/fixtures/world_anim_expected.h`; the sequence test compares the two.
Every row carries its own `.index`, and a compile-time assert requires that index
to equal the array position, so rows cannot be reordered by accident.

To change **an arrangement** — where a sprite sits, which tile it uses — you are
changing cartridge content, which this port does not carry. That belongs to a
replacement-content route, not to an edit here.

`RomAsset::WORLD_ANIM_FRAMES` covers the pointer table and the records as **one**
extent, because they are contiguous in the cartridge; the split between them is
`WorldAnimFrames`' own, at `kWorldAnimFrameCount * kWorldAnimPointerBytes`.

Only an English address is published. The Japanese build shifts every address in
this bank, by an amount that only assembling the source would settle, and there
is no Japanese cartridge to check a derived answer against — so
`romRegion(RomAsset::WORLD_ANIM_FRAMES, Language::JP)` returns `nullopt` rather
than a guess.

## What's tested

`tests/test_world_anim.cpp`, against a real cartridge:

- Every one of the 108 frames decodes as a count byte plus whole sprite rows,
  never claims more sprites than it stores, and lies inside the region.
- The records tile the block with no gap and no overlap, and the whole block is
  accounted for.
- The two shared-record pairs, and all eight frames with undrawn rows, pinned by
  name.
- The first airship frame decoded by hand — twelve sprites, the right-hand column
  mirrored — and the attribute byte unpacked from a byte with every field set.
- Each sequence table compared to its fixture step by step, every step's frame
  proved to exist, and the airship table proved to be whole rows of six that each
  open on the blank frame.

The region's own bytes are proved at port time instead: the generator reassembles
the pointer table and all 108 records from the disassembly and requires the result
to be identical to the cartridge over the whole 5,294-byte extent, so the address
in the region table cannot be wrong and go unnoticed.
