# NPCs and event triggers — per-map event data

The per-map data the field engine reads to place actors and events on a map: the
tile-triggered event scripts, each map's startup event, and every NPC's placement
and behaviour. Three tables share one shape — a flat record pool located through a
per-map offset table — and one reference type, the opaque event-script offset.

This layer is the **static data plus lookup accessors**. It carries *where* things
are and *which* script or graphics they use, as ids and offsets; the event
interpreter, the NPC object system, and map transfer are runtime systems added
later. An event reference here is an opaque offset into the event-script block —
the script bodies are decoded and run elsewhere.

## Placeholder ids

Maps are identified by their position in the map tables (0-415), a plain decimal
index — there is no per-map name source (see [map-metadata.md](map-metadata.md)).
The offset tables and the startup-event table carry that index as a typed
identity field; giving maps real names later is a welcome, reversible cleanup.

## Public surface

```cpp
#include "data/event_triggers.h"
#include "data/npc_properties.h"

using namespace ostinato;

// --- event triggers on a map (0-415) ---
for (const EventTrigger& t : eventTriggersForMap(9)) {
    t.posX; t.posY;                 // the tile that fires the event
    t.event.offset();               // 24-bit offset into the event-script block
}

// --- a map's startup event (0-511) ---
EventScriptRef start = mapInitEvent(3);
bool none = start.offset() == kEventReturnScript.offset();  // "no startup event"

// --- NPCs on a map (0-415) ---
for (const NpcProperties& n : npcsForMap(3)) {
    n.posX(); n.posY();             // tile position
    n.gfx();                        // MapSpriteGfx sprite set
    n.switchId();                   // event switch gating the NPC (0 = ungated)
    if (n.isSpecial())      n.vramX();        // special-graphics object
    else if (n.isAnimated()) n.animFrame();   // animated NPC
    else                     n.eventRef();     // plain NPC: activation script
}
```

Each table also exposes a full span for iteration — `eventTriggerRecords()`,
`mapInitEvents()`, `npcRecords()` — plus its offset table
(`eventTriggerOffsets()`, `npcOffsets()`). A map with no records returns an empty
span; world maps 0-2 and the extra slot 415 have no NPCs and no triggers.

## Event-script references

Every event reference is a 24-bit offset into the event-script block, stored
little-endian:

```cpp
struct EventScriptRef {            // sizeof 3, alignment 1
    std::array<std::uint8_t, 3> bytes;
    std::uint32_t offset() const;               // the 24-bit value
    static EventScriptRef at(std::uint32_t o);  // asserts o < kEventScriptBlockSize
};
```

`EventScriptRef` is deliberately opaque: it names a location in the script block,
nothing about the script. `kEventReturnScript` is the reference to the shared
"return immediately" script — a startup-event or defaulted-NPC entry set to it
means "no event runs here".

## Event triggers

```cpp
struct EventTrigger {              // sizeof 5, offset-pinned
    std::uint8_t posX;            // +0
    std::uint8_t posY;            // +1
    EventScriptRef event;        // +2..+4
};
```

The records live in one flat pool shared across maps; a map's triggers are the
half-open slice named by the per-map offset table. The offset table is a
`MapTriggerOffsetEntry` array (the same type the map-trigger family uses):

```cpp
struct MapTriggerOffsetEntry { std::uint16_t index; std::uint16_t offset; };
```

There are **416 map slots plus one end entry** — one more slot than the 415
defined maps; the last slot is empty. Each entry's `.index` is the map id and
`.offset` is the record index where that map's triggers begin; a map's slice is
`[offset[map], offset[map+1])`. The field and world encounter code compares the
party's tile against `posX`/`posY` to fire the event.

## Startup events

```cpp
struct MapInitEventEntry { std::uint16_t index; EventScriptRef record; };
```

One entry per map over the full 512-slot map-id space. `mapInitEvent(map)` returns
the startup event that runs when the map loads; `kEventReturnScript` marks a map
with none.

## NPC properties

Every field NPC is one 9-byte record. The record is **variant-polymorphic**: the
same nine bytes are decoded three different ways depending on the NPC kind, and
`isSpecial()` / `isAnimated()` tell the reader which decode applies.

```cpp
struct NpcProperties {            // sizeof 9, alignment 1
    std::array<std::uint8_t, 9> bytes;

    bool isSpecial() const;       // an object with special graphics
    bool isAnimated() const;      // a normal NPC that cycles animation frames
    // ...decode accessors...
};
```

The three kinds:

- **Normal** — a plain NPC. `eventRef()` is the script it runs when activated;
  `dir()` its facing, `react()` whether it turns to face the player,
  `vehicle()` / `showRider()` an optional ridden vehicle.
- **Animated** — a normal NPC that cycles frames. Same as normal but with
  `animType()` / `animFrame()` / `animSpeed()` in place of the facing and vehicle
  fields.
- **Special** — an object with special graphics (airship parts, machinery,
  large sprites). `vramX()` / `vramY()` place its graphic, `hFlip()` / `is32x32()`
  shape it, and an optional master reference (`masterId()`, `masterOffset()`,
  `masterDir()`, `isSlave()`) links a slave sprite to a master object.

Fields common to every kind: `posX()`/`posY()`, `gfx()`, `pal()`, `speed()`,
`movement()`, `spritePriority()`, `layerPriority()`, `scrollsWithBg2()`, and
`switchId()` — the 10-bit event switch that gates whether the NPC appears
(`0` means ungated).

### Building records

Write records through the three named builders, one per kind. Each names only the
properties it overrides; the rest take defaults, and every builder packs to the
exact record bytes:

```cpp
NpcProperties::npc({ .pos = {8, 11}, .switchId = 0x043F,
                     .gfx = MapSpriteGfx::CLYDE, .pal = MapSpritePal::LOCKE,
                     .speed = ObjectSpeed::SLOW, .dir = EventDir::UP,
                     .react = NpcReact::NONE });

NpcProperties::animated({ .pos = {5, 31}, .switchId = 0x048C,
                          .event = EventScriptRef::at(0x0224B),
                          .gfx = MapSpriteGfx::BIG_SPARKLE,
                          .animType = NpcAnimType::FOUR_FRAMES,
                          .animFrame = NpcAnimFrame::SPECIAL,
                          .animSpeed = NpcAnimSpeed::MEDIUM });

NpcProperties::special({ .pos = {2, 9}, .switchId = 0x03FF,
                         .gfx = MapSpriteGfx::FLYING_TERRA_3,
                         .vramPos = {2, 0},
                         .master = { .id = 0, .offset = 1,
                                     .dir = NpcMasterOffsetDir::RIGHT,
                                     .isSlave = true } });
```

`switchId` is the switch id as written (a value at or above `0x0300`, or `0` for
none); the accessor returns it in the same form. The NPC records are a flat pool
located through `npcOffsets()`, the same 416-slot-plus-end offset table shape as
the triggers.

### Value spaces

The NPC fields draw on a set of named enums — the sprite set (`MapSpriteGfx`, 165
sprites) and palette (`MapSpritePal`, eight base palettes with per-sprite aliases
so a palette-less sprite names its own default), the object speed and vehicle
(`ObjectSpeed`, `EventVehicle`), the movement / priority / reaction / scroll
fields (`NpcMovement`, `NpcSpritePriority`, `NpcLayerPriority`, `NpcReact`,
`NpcScroll`), the animation fields (`NpcAnimType`, `NpcAnimFrame`, `NpcAnimSpeed`),
and the master-offset direction (`NpcMasterOffsetDir`). Facing reuses the shared
`EventDir`.

## Where to change these

The record types, builders, and accessors live in `include/ostinato/*.h` and
`src/data/{event_triggers,npc_properties}.{h,cpp}`. The row data and value-space
enum headers are generated — to change a value, edit the source data and
regenerate, never hand-edit a `src/data/generated/*.inc` file or a generated enum
header (each carries a "DO NOT EDIT" banner). The generated rows and the
independent test fixtures both derive from the same source, and the test suite
compares every record against the fixture, so any drift fails the build.
