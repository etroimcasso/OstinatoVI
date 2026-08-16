# Map metadata — properties, animation, and triggers

The per-map data the field engine reads when it loads a map: the 33-byte
properties record (which graphics, tilesets, and layouts to load; scroll,
palette, window, and battle-background settings), the parallax and animation
satellite tables the properties point at, the new-game NPC-switch seed block, and
the trigger family — the treasures and entrances placed on each map.

This layer is the **static data plus lookup accessors**. It carries *indices*,
not content: a map's graphics/tileset/layout/palette fields are id numbers into
the pack-side graphics sets, and the actual map transfer, animation playback, and
treasure-award behaviour are runtime systems added later. What lands here is
everything the field code reads out of the map tables.

## Placeholder ids

Two id spaces in this data have no in-game names to draw from, so they ship as
plain decimal indices today:

- **Map ids** (0-414). A map is identified by its position in the properties
  table. `MapProperties::titleIndex` points into the shared 73-entry map-title
  text list (`TextCorpus::mapTitle`), not a per-map name, so there is no name
  source for the maps themselves.
- **Song ids** (`MapProperties::songId`). The default-music byte is a raw song
  number; the game never names its music.

Both are numeric placeholders on purpose — giving the maps and the music real
names later is a planned, welcome cleanup, and the surfaces are kept reversible
for it.

## Public surface

```cpp
#include "data/map_properties.h"
#include "data/map_animations.h"
#include "data/map_triggers.h"

using namespace ostinato;

// --- per-map properties (0-414) ---
const MapProperties& m = mapProperties(32);
m.graphics.gfx1();          // 7-bit index into the pack-side graphics sets
m.layouts.bg1Layout();      // 10-bit tilemap index (0 = no layer)
m.effectFlags.warpEnabled(); m.effectFlags.xZoneEnabled();
m.battleBackground.background();  // BattleBackgroundId for battles on this map
m.parallaxIndex;            // -> mapParallax(...)
m.palAnimIndex;             // -> palette animation; 0 = none
m.songId;                   // default music (placeholder id)

// --- satellite tables ---
const MapParallax& p = mapParallax(m.parallaxIndex);   // 0-20
const MapPaletteAnimation& pa = mapPaletteAnimation(3);  // 0-9
const Bg3AnimationRecord& b3 = bg3Animation(2);          // 0-5
std::span<const std::uint8_t> seed = initialNpcSwitches();  // 128-byte block

// --- triggers on a map ---
for (const TreasureProperty& t : treasuresForMap(75)) {   // map 0-414
    t.posX; t.posY;
    if (t.trigger.isItem()) t.item();          // ItemId
    else if (t.trigger.isGil()) t.gilAmount(); // gil (content x 100)
}
for (const LongEntrance& e : longEntrancesForMap(32))     // map 0-511
    e.destination.destMap();                   // 9-bit target map id
shortEntrancesForMap(32);                      // single-tile entrances
```

Each table also exposes a full span for iteration — `mapProperties()`,
`mapParallax()`, `mapPaletteAnimations()`, `bg3Animations()`, plus
`treasureRecords()` / `longEntranceRecords()` / `shortEntranceRecords()` and
their offset tables — of entries in table order.

## The properties record

```cpp
struct MapProperties {           // sizeof 33, offset-pinned per field
    std::uint8_t titleIndex;     // +0  -> TextCorpus::mapTitle (73-entry list)
    MapEffectFlags effectFlags;  // +1  warp/x-zone/wavy/spotlights/timer toggles
    MapBattleBackground battleBackground;  // +2  battle bg + bg3-foreground bit
    std::uint8_t unknown3;       // +3  no consumer; preserved raw
    std::uint8_t tilePropIndex;  // +4  -> tile-properties set (pack-side)
    MapBattleFlags battleFlags;  // +5  random-battles-enabled bit
    std::uint8_t windowMask;     // +6  2-bit window-shape row
    MapGraphicsIds graphics;     // +7..+12  packed 48-bit id group
    MapLayoutIds layouts;        // +13..+16 packed 30-bit layout group
    std::uint8_t overlayIndex;   // +17 -> overlay set (pack-side)
    std::uint8_t bg2ScrollX;     // +18
    std::uint8_t bg2ScrollY;     // +19
    std::uint8_t bg3ScrollX;     // +20
    std::uint8_t bg3ScrollY;     // +21
    std::uint8_t parallaxIndex;  // +22 -> mapParallax (0-20)
    MapBgSizes bgSizes;          // +23..+24 per-bg width/height size codes
    std::uint8_t paletteIndex;   // +25 -> palette set (pack-side)
    std::uint8_t palAnimIndex;   // +26 -> palette animation; 0 = none
    MapAnimationIndexes animation;  // +27 bg1/2 and bg3 animation indices
    std::uint8_t songId;         // +28 default music (placeholder id)
    std::uint8_t unknown29;      // +29 no consumer; preserved raw
    std::uint8_t scrollRangeWidth;   // +30 tiles; 0 = unclamped
    std::uint8_t scrollRangeHeight;  // +31 tiles; 0 = unclamped
    std::uint8_t colorMathMode;  // +32 bg2/bg3 color-math mode
};
```

The record is copied whole into work RAM at map load, so its field order and byte
offsets are the contract — every member is alignment-1 and the record stays
`sizeof == 33`.

Four fields are packed bit-groups with their own wrapper types in
`include/ostinato/`:

- **`MapGraphicsIds`** (`map_graphics_ids.h`) — six bytes holding a 48-bit
  little-endian stream of seven indices: `gfx1()`-`gfx4()` (7 bits each),
  `bg3Gfx()` (6 bits), `tileset1()`/`tileset2()` (7 bits each).
- **`MapLayoutIds`** (`map_layout_ids.h`) — `bg1Layout()`/`bg2Layout()`/
  `bg3Layout()` (10 bits each, `0` = no layer); the top two bits are spare.
- **`MapBgSizes`** (`map_bg_sizes.h`) — per-bg `widthCode()`/`heightCode()`
  (0-4), plus `bg1WidthTiles()`-style helpers that decode the code to a tile
  count (16/32/64/128/256).
- **`MapEffectFlags`**, **`MapBattleBackground`**, **`MapBattleFlags`**,
  **`MapAnimationIndexes`** — one-byte flag/index carriers with named bit
  accessors.

## Satellite tables

- **`MapParallax`** (8 bytes, `mapParallax(0-20)`) — signed bg2/bg3 scroll speeds
  (`std::int8_t`, sign-extended to fixed-point by the consumer) plus unsigned
  scroll multipliers.
- **Palette animation** (`mapPaletteAnimation(0-9)`) — two `PaletteAnimationSlot`
  records per entry; each slot has a control byte (`type()` →
  `PaletteAnimationType` {COUNTER, CYCLE, ROM_COLORS, PULSE}, `disabled()`,
  `stepCount()`), a frame duration, and a color range.
- **bg1/bg2 animation** — a contiguous byte stream plus a per-index offset table
  (`bgAnimationStream()` / `bgAnimationOffsets()`), decoded a sub-record at a time
  with `bgAnimationFrameSet(offset)`. Four indices (11/12/17/19) alias one body,
  and the consumer always reads a fixed eight sub-records per index — both quirks
  are reproduced because the stream is stored contiguously.
- **bg3 animation** (`bg3Animation(0-5)`) — a fixed 20-byte record: speed,
  graphics size, and eight frame offsets.
- **Initial NPC switches** (`initialNpcSwitches()`) — the raw 128-byte block that
  seeds the new-game NPC event-bit array; the per-bit meanings are event-domain
  state handled later.

## Triggers

All three trigger tables share one shape: the records live in a single flat array
(shared across maps — many maps point at the same record, and a map with no
trigger points past the last record), and a **per-map offset table** names where
each map's slice begins. A map's records are the half-open range
`[offset[map], offset[map + 1])`, so a map with no trigger of that kind gets an
empty span. Treasures cover the 415 defined maps; entrances cover the full 9-bit
512-slot map-address space.

```cpp
struct TreasureProperty {          // sizeof 5
    std::uint8_t posX, posY;       // chest position
    TreasureSwitch trigger;        // event-bit + content-type discriminator
    std::uint8_t content;          // type-dependent payload
    std::uint32_t gilAmount() const;      // content x 100 (when isGil)
    ItemId item() const;                  // content as an item (when isItem)
    std::uint8_t formationLowByte() const; // formation low byte (when isMonsterInABox)
};

struct TreasureSwitch {            // sizeof 2 (alignment-1 byte pair)
    std::uint16_t eventBit() const;        // bits 0-8: obtained-bit index
    bool isGil(), isItem(), isMonsterInABox(), isEmpty() const;  // bits 15/14/13/12
};
```

Read the switch first: `isGil()` / `isItem()` / `isMonsterInABox()` / `isEmpty()`
select which content accessor applies. `eventBit()` is the index into the
obtained-treasure bit array that marks the chest opened.

```cpp
struct EntranceDestination {       // sizeof 2 (alignment-1 byte pair)
    std::uint16_t destMap() const; // bits 0-8; kParentMapSentinel = return to parent
    bool isParentReturn() const;
    bool setsParentMap() const;    // store current map as parent before transfer
    bool zLevelLower(), showMapName() const;
    EventDir facing() const;       // party facing after transfer
};

struct LongEntrance  { std::uint8_t srcX, srcY; EntranceRun run;
                       EntranceDestination destination; std::uint8_t destX, destY; };  // 7
struct ShortEntrance { std::uint8_t srcX, srcY;
                       EntranceDestination destination; std::uint8_t destX, destY; };  // 6
```

A **long** entrance triggers anywhere along a run of tiles (`EntranceRun`:
`isVertical()` + `length()`); a **short** entrance triggers on the single tile at
`(srcX, srcY)`. Both transfer to `destMap()` at `(destX, destY)`. Destination map
ids 0-2 are world maps and `kParentMapSentinel` (`0x1FF`) means "return to the
stored parent map".

## Backing data / where to change

Rows live in `src/data/generated/` and are `#include`d into their arrays:

- Properties + parallax + NPC seed → `map_properties.cpp`
  (`map_properties_data.inc`, `map_parallax_data.inc`, `init_npc_switch_data.inc`).
- Animation → `map_animations.cpp` (`map_pal_anim_data.inc`,
  `map_bg_anim_stream_data.inc`, `map_bg_anim_offsets_data.inc`,
  `map_bg3_anim_data.inc`).
- Triggers → `map_triggers.cpp` (`{treasure,long_entrance,short_entrance}_data.inc`
  and their `_offsets_data.inc`).

Every value is named:

```cpp
    TreasureProperty{
        .posX    = 55,
        .posY    = 8,
        .trigger = TreasureSwitch{{ 0x01, 0x40 }},
        .content = 230,
    },
    MapTriggerOffsetEntry{ .index = 75, .offset = 43 },
```

The offset entries carry their map id as `.index` and the record-array position
as `.offset`; compile-time asserts verify `.index` matches array position, that
the offsets are monotonic, and that the final end entry equals the record count.
To move a treasure, edit its `.posX`/`.posY`/`content`; to repoint an entrance,
edit its `destination` bytes; to give a map a different treasure *set*, edit its
offset entry. A deliberate change must also update the matching row in the
fixture under `tests/fixtures/` (e.g. `treasure_expected.h`), which holds the
original ROM values.

Because map and song ids are numeric placeholders (see above), renaming them is a
supported later change: introduce the enum, swap the `.index` / `.songId` values
for the named constants, and update the fixtures alongside.

## What's tested

- `tests/test_map_properties.cpp` — the 415 property records, 21 parallax
  records, and the 128-byte NPC seed compared in full against their fixtures;
  hand-traced decodes of the packed graphics/layout/size/animation groups, the
  effect-flag warp/x-zone bits, the battle-background byte, and a signed parallax
  speed.
- `tests/test_map_animations.cpp` — the palette, bg1/bg2 stream, and bg3
  animation tables in full against fixtures, including the aliased-index and
  fixed-eight-read quirks.
- `tests/test_map_triggers.cpp` — every treasure and entrance record and every
  per-map offset entry against fixtures (no subsets); the per-map slices verified
  against their offset windows; the switch and destination wrappers decoded
  against an independent re-derivation from the raw bytes; and spot checks (map 75
  carries eight treasures, the parent-return sentinel occurs in the corpus).
