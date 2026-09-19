# Battle backgrounds

The scenery behind a battle. Fifty-six backgrounds, each naming the graphics
blocks, tilemaps and palette that compose it.

Which background a battle uses is decided elsewhere — the map's own default
(`map-metadata.md`), the world-map sector tables (`encounters.md`), or a
character-A.I. setup that overrides it. This page covers what a background *is*
once something has chosen one.

## The surface

```cpp
#include "data/battle_backgrounds.h"

const BattleBackgroundProperties& bg =
    ostinato::battleBackgroundProperties(BattleBackgroundId::ZOZO);

bg.graphics1.id();              // BattleBackgroundGraphicsId
bg.graphics1.isDoubleWidth();   // 128 tiles instead of 64
bg.graphics2;                   // BattleBackgroundGraphicsId
bg.graphics3;                   // BattleBackgroundGraphicsId
bg.tilemap1;                    // BattleBackgroundTilemapId
bg.tilemap2;                    // BattleBackgroundTilemapId
bg.palette.id();                // BattleBackgroundPaletteId
bg.palette.hasWavyEffect();     // the rippling background effect
```

`battleBackgroundTable()` returns all fifty-six entries in id order for
iteration; each entry carries its `BattleBackgroundId` as `.id` and its data as
`.record`.

| Type | Header | What it is |
|---|---|---|
| `BattleBackgroundId` | `ostinato/battle_background_id.h` | Which background (56 of them, plus `DEFAULT`) |
| `BattleBackgroundProperties` | `data/battle_backgrounds.h` | One background's six bytes |
| `BattleBackgroundGraphicsSlot` | `ostinato/battle_background_graphics_slot.h` | A graphics block plus its double-width flag |
| `BattleBackgroundPaletteSlot` | `ostinato/battle_background_palette_slot.h` | A palette plus its wavy-effect flag |
| `BattleBackgroundGraphicsId` | `ostinato/battle_background_graphics_id.h` | One of 75 graphics blocks, or `NONE` |
| `BattleBackgroundTilemapId` | `ostinato/battle_background_tilemap_id.h` | One of 49 tilemaps |
| `BattleBackgroundPaletteId` | `ostinato/battle_background_palette_id.h` | One of 53 palettes |

## How a background is composed

Three graphics blocks, a tilemap, and a palette:

- **Graphics** are 64 tiles (4096 bytes) each. The three blocks land at
  different places in video memory. If the first block sets its double-width
  flag it is 128 tiles instead, and the second block is not loaded at all —
  the wide block occupies the space both would have used.
- **Tilemaps** are 32×32. Only the first nineteen rows are on screen unless the
  background scrolls vertically, which is how the clouds and the waterfall
  work. Every record names a tilemap twice; only the first is read.
- **The palette** holds 48 colours and loads into background palettes 5, 6 and
  7. Its flag turns on the rippling effect used by the desert.

A graphics slot holding `NONE` loads nothing. Three backgrounds — Narshe's
exterior, the town exterior and the village exterior — have no first block at
all and build themselves from the second and third alone.

## Changing a background

Edit the row in `src/data/generated/battle_bg_prop_data.inc`. Rows are in id
order and every field is named, so swapping Zozo's palette for the desert's is
one line:

```cpp
.palette = BattleBackgroundPaletteSlot::plain(BattleBackgroundPaletteId::DESERT_WOB),
```

Use `::wavy(...)` instead of `::plain(...)` to add the ripple, and
`::doubleWidth(...)` instead of `::single(...)` to make a first graphics block
128 tiles — remembering that doing so drops the second block.

Then update the matching row in `tests/fixtures/battle_bg_prop_expected.h`,
which holds the original bytes. `ostinato-vi-background-tests` compares every
record against that fixture, so a row changed in one file and not the other is
reported as a divergence rather than silently accepted.

## Gotchas

- **`NONE` is `$FF`, and zero is not `NONE`.** A slot holding 0 names the first
  graphics block, and the loader treats it as a real block. Three records —
  the waterfall, the Magitek train and Cyan's dream — do hold zero in a slot,
  so they load that block whether or not their scenery shows it. Use
  `isEmpty()` to ask whether a slot is genuinely unused; comparing the id
  against `NONE` answers the same question, but reading the raw byte does not.
- **Only two of the six bytes carry a flag.** The first graphics block and the
  palette. The second and third blocks are plain ids — nothing sets bit 7 on
  them, and a value that did would name a block that does not exist.
- **The second tilemap is never read.** It is present in every record and
  always matches the first. It is kept because it is part of the record.
- **Passing `DEFAULT` to the accessor is a programming error.** `DEFAULT`
  ($FF) is the "no choice made" value used by the things that *select* a
  background; it has no properties row. The accessor asserts against it.

## Where to change things

| Change | File |
|---|---|
| A background's graphics, tilemap or palette | `src/data/generated/battle_bg_prop_data.inc` (+ the fixture) |
| What a slot's flags mean | `include/ostinato/battle_background_*_slot.h` |
| Adding a graphics block, tilemap or palette id | the corresponding `include/ostinato/battle_background_*_id.h` |
| The lookup itself | `src/data/battle_backgrounds.cpp` |

## See also

- [encounters.md](encounters.md) — the world-map tables that pick a background per sector
- [map-metadata.md](map-metadata.md) — a map's own default battle background
- [formations.md](formations.md) — what fights in front of it
