# Monster sprite sizes

## Public surface

```cpp
#include "data/monster_sprites.h"
#include "data/formations.h"

// The stencils, read out of the player's cartridge when the game starts.
const ostinato::MonsterSprites& sprites = content.monsterSprites;   // assets::IngestedContent

// How big a formation's slot-2 monster stands.
const ostinato::Formation& formation = ostinato::getFormation(id);
if (auto box = ostinato::monsterSpriteBox(formation.vramMap(), 2)) {
    ostinato::MonsterSpriteSize size = sprites.size(formation.monsterId(2), *box);
    size.width;    // tiles of 8 pixels across
    size.height;   // tiles down
}

// One monster's stencil, tile by tile.
ostinato::MonsterStencil stencil = sprites.stencil(ostinato::MonsterId::LOBO);
stencil.side();          // 8 or 16
stencil.filled(0, 1);    // is the tile in row 0, column 1 part of the sprite?
stencil.extent();        // {6, 6} for Lobo
```

## What it is

Every monster stands on the battlefield at a size, counted in tiles of 8 pixels. That size comes
from two things:

- **The monster's stencil** — a square map, 8×8 or 16×16 tiles, of which tiles its sprite fills.
  The monster's *extent* is how much of that square it takes up.
- **The box its formation slot gives it** — the largest sprite that slot shows. A sprite bigger
  than its box is cut down to the box.

`MonsterSprites::size(monster, box)` combines the two. The stencils, and the graphics properties
that say which stencil each monster uses, are game artwork, so they are **not** part of this
repository: they are read in place out of the player's cartridge, the same way the monster
graphics are. The boxes are a short table of numbers per formation layout, so they compile in.

## The extent

```cpp
class MonsterStencil {
    MonsterStencil(std::span<const std::uint8_t> bytes, bool large);
    bool valid() const;
    int  side() const;                         // 8 or 16; 0 when empty
    bool filled(int row, int column) const;    // false outside the square
    MonsterSpriteSize extent() const;
};
```

A stencil stores one row of tiles per byte (8×8) or per two bytes (16×16), the leftmost tile in
the top bit of the row's first byte. `extent()` measures it the way the game does:

- **height** — the rows filled from the top, stopping at the first empty row;
- **width** — out to the rightmost filled tile in those rows.

Tiles below an empty row would count for neither measure. No stencil in the cartridge has one —
every stencil is filled from its top row down with no gap, which the tests check over all 416 —
so in practice the extent is the sprite's bounding box measured from the top-left corner.

`MonsterStencil` is a view over the cartridge's bytes, not a copy. A span of the wrong length for
its size gives an empty stencil: `valid()` is false and `extent()` is 0 × 0.

## The boxes

```cpp
struct MonsterSpriteBox {
    std::uint8_t layout;   // Formation::vramMap()
    std::uint8_t slot;     // the formation slot, 0 to 5
    std::uint8_t width;    // in tiles
    std::uint8_t height;
};

std::span<const MonsterSpriteBox> monsterSpriteBoxes();
std::optional<MonsterSpriteSize>  monsterSpriteBox(std::uint8_t layout, std::size_t slot);
```

Every formation names one of 13 layouts, and a layout gives each of its slots a box. Layouts
hold between one and six boxes; `monsterSpriteBox` returns `nullopt` past a layout's last box.
No formation puts a monster — present at the start or arriving later — in a slot its layout has
no box for.

Most boxes are larger than any monster that stands in them. In 27 formation slots a box cuts a
sprite narrower, and in 9 shorter — all of them in battles against a boss built from several
monsters: the final battle's tiers, Atma, the Guardian, the Zone Eater and others. Short Arm, for
one, has an 11 × 8 stencil and stands in a 4 × 4 box.

## Gotchas

- **The Ghost Train is set apart.** Its stencil is a single tile; it always stands 16 × 14,
  whatever its box. `size()` returns that for `MonsterId::GHOSTTRAIN`, and `stencil()` still
  returns the one-tile stencil.
- **There are more graphics than monsters.** The properties hold 416 graphics, the 384 monsters
  first; `graphicsCount()` reports 416. `stencil()` takes any of them as a `MonsterId` value and
  gives an empty stencil past the end.
- **Sizes are in tiles.** Multiply by 8 for pixels.

## Backing data / where to change

| What | Where |
|---|---|
| Views and box lookup | `src/data/monster_sprites.{h,cpp}` |
| Box rows | `src/data/generated/monster_sprite_boxes_data.inc` (generated) |
| Where the properties and stencils live in the cartridge | `src/data/rom_regions.h` — `RomAsset::MONSTER_GFX_PROP`, `MONSTER_STENCIL_SMALL`, `MONSTER_STENCIL_LARGE` |
| Where they are read at start-up | `src/assets/cartridge.cpp`, `IngestedContent::monsterSprites` |

To change **a box**, edit its row in the `.inc` and the matching row in
`tests/fixtures/monster_sprites_expected.h`; the box test compares the two, and a compile-time
assert requires the rows to run layout by layout with slots counting from 0.

To change **a stencil, or which stencil a monster uses**, you are changing cartridge content,
which this port does not carry. That belongs to a replacement-content route, not to an edit here.

The properties region is 416 records of five bytes: the graphics index (bit 15 marks 3-bit-deep
graphics), the palette index stored high byte first (bit 15 of that word marks a 16×16 stencil),
and the stencil number. `MonsterSprites` reads the last two.

## What's tested

`tests/test_monster_sprites.cpp`:

- Every box against its fixture, a box found by layout and slot, and none past a layout's end.
- Every filled slot of all 576 formations has a box.
- Both stencil sizes decoded tile by tile from hand-made bytes, the extent stopping at the first
  empty row, and a wrongly sized span giving an empty stencil.
- Against a real cartridge: all 416 graphics have a stencil, filled from the top with no gap; the
  stencils of the Guard, Lobo, Short Arm and Atma traced by hand; a sprite cut down to its box in
  each measure on its own; the Ghost Train's fixed size; and the 27 slots cut narrower and 9 cut
  shorter across every formation.

The cartridge addresses are proved when the tables are generated: the properties end exactly where
the cartridge's monster palettes begin and every record's stencil number and flags are compared
with the cartridge, and the box table is found in the battle graphics code exactly once, with the
pointer table that addresses it right before it.
