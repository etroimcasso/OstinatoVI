# Field encounters — battle groups, sector tables, rates

The data that decides which battle the player runs into on the field, and how
often. Six tables answer two questions — *which formation?* (the battle groups
plus the per-sector / per-map indices into them) and *how often?* (the rate
tables) — and five small inline tables supply the world-map background and rate
math. The formation the player then fights comes from
[formations.md](formations.md).

This layer is the **static data plus one-entry accessors**. The full selection
algorithm (the RNG rolls, the sector math wiring, the 80/80/80/16 slot odds) is
a runtime system added later; the odds and index math are documented in
`docs/contracts/encounters.md`.

## Public surface

```cpp
#include "data/encounters.h"

using ostinato::WorldId;

// Which formation(s) a battle group can spawn.
const auto& g = ostinato::getRandomBattleGroup(112);   // 0-255
g.formations[0].formationId();     // FormationId — the candidate formation
g.formations[0].randomizePlus3();  // add a random 0-3 to the index at load?

const auto& e = ostinato::getEventBattleGroup(93);     // 2 candidates
e.formations[0].formationId();

// Which rand group a world-map sector / a map uses.
ostinato::getWorldBattleGroup(WorldId::WORLD_OF_BALANCE, /*xSector*/0,
                              /*ySector*/0, /*bgGroup*/0);   // rand-group index
ostinato::isVeldtSector(WorldId::WORLD_OF_BALANCE, 0, 0, 0); // $FF sentinel?
ostinato::getMapBattleGroup(/*mapId*/32);                    // rand-group index

// How often a sector / a map has battles.
ostinato::getWorldBattleRate(WorldId::WORLD_OF_BALANCE, /*sector*/5,
                             /*rateSlot*/0);   // WorldBattleRateClass::LOW
ostinato::getMapBattleRate(/*mapId*/41);       // SubBattleRateClass::LOW
```

Each table also exposes a full span for iteration — `randomBattleGroups()`,
`eventBattleGroups()`, `worldBattleGroup()`, `subBattleGroup()`,
`worldBattleRate()`, `subBattleRate()` — of `{ index, ... }` entries in table
order.

## Battle groups

```cpp
struct RandomBattleGroup { std::array<FormationRef, 4> formations; };  // sizeof 8
struct EventBattleGroup  { std::array<FormationRef, 2> formations; };  // sizeof 4

struct RandomBattleGroupEntry { std::uint16_t index; RandomBattleGroup record; };
struct EventBattleGroupEntry  { std::uint16_t index; EventBattleGroup  record; };
```

A group is a fixed set of candidate formations: random groups hold four, event
groups two. Each candidate is a `FormationRef` — a formation plus a
`randomizePlus3()` flag (bit 15) that tells the loader to add a random 0-3 to
the formation index. The selection between candidates is consumer math; this
layer gives you the candidates.

The `index` field on each entry is the group's own 0-255 index — the value the
world/map group tables below point at.

## World / map group tables

```cpp
std::uint8_t getWorldBattleGroup(WorldId, xSector, ySector, bgGroup);
bool         isVeldtSector      (WorldId, xSector, ySector, bgGroup);
std::uint8_t getMapBattleGroup  (std::uint16_t mapId);        // mapId 0-511
```

Both return a **rand-group index** (feed it to `getRandomBattleGroup`).

- On the world map, the sector is `xSector`/`ySector` (0-7, the high bits of the
  party's position) plus a `bgGroup` (0-3, from `kBattleBgGroupOffset`). A value
  of `kVeldtSector` (`$FF`) means the sector is on the Veldt, where the formation
  comes from the party's running encounter list instead of a fixed group — test
  for it with `isVeldtSector`.
- In a map, the lookup is just the map id.

## Rate tables

```cpp
enum class WorldBattleRateClass : std::uint8_t { NORMAL, LOW, HIGH, NONE };
enum class SubBattleRateClass   : std::uint8_t { NORMAL, LOW, HIGH, VERY_HIGH };

WorldBattleRateClass getWorldBattleRate(WorldId, sector /*0-63*/, rateSlot /*0-3*/);
SubBattleRateClass   getMapBattleRate  (std::uint16_t mapId);
```

Each rate byte packs four 2-bit classes. On the world map the `rateSlot` (from
`kBattleBgRateSlot`) picks which of the four applies to the current background;
in a map the low bits of the map id pick the field. The fourth class (`NONE` /
`VERY_HIGH`) exists in the type but no sector or map selects it in the shipped
data.

## Inline tables and battle backgrounds

Five small public constants, indexed by the sector/background:

```cpp
inline constexpr std::array<std::array<BattleBackgroundId, 8>, 2> kWorldBattleBackgrounds;
inline constexpr std::array<std::uint8_t, 8> kBattleBgRateSlot;      // which rate field
inline constexpr std::array<std::uint8_t, 8> kBattleBgGroupOffset;   // bg-group offset
inline constexpr std::array<BattleRateIncrements, 4> kWorldBattleRateIncrements;  // by CharmState
inline constexpr std::array<BattleRateIncrements, 4> kSubBattleRateIncrements;
```

`kWorldBattleBackgrounds[world][slot]` is the battle background
(`BattleBackgroundId`, `include/ostinato/battle_background_id.h`) for each
world-map sector slot. `BattleRateIncrements` holds the four **counter
increments** (one per rate class) that pace random battles; they are magnitudes
(decimal `std::uint16_t`, 0-65535), not packed bytes, and are indexed by
`CharmState` — Charm Bangle halves the world-map rate, the Moogle Charm zeroes
every class.

## Backing data / where to change

Rows live in `src/data/generated/` — `rand_battle_group_data.inc`,
`event_battle_group_data.inc`, `world_battle_group_data.inc`,
`sub_battle_group_data.inc`, `world_battle_rate_data.inc`,
`sub_battle_rate_data.inc`, and `encounter_bg_tables_data.inc` — each
`#include`d into its array (the six `.dat` tables into `src/data/encounters.cpp`,
the inline tables into `encounters.h`). Every value is named:

```cpp
    RandomBattleGroupEntry{
        .index = 0,
        .record = RandomBattleGroup{ .formations = {{
            FormationRef::of(FormationId::LEAFER),
            FormationRef::of(FormationId::LEAFER_X2_DARK_WIND),
            FormationRef::of(FormationId::LEAFER),
            FormationRef::of(FormationId::LEAFER_X2_DARK_WIND),
        }} },
    },
```

To change which formations a group can spawn, edit its `FormationRef::of(...)`
entries; to move a sector or map to a different group, edit its `.value`; to
retune a rate, edit the class byte or the increment magnitude. A deliberate
change must also update the matching row in the fixture under `tests/fixtures/`
(e.g. `rand_battle_group_expected.h`), which holds the original ROM values.
Compile-time asserts verify every entry's `index` matches its array position.

For the consumer semantics — the RNG rolls, the sector index math, and the
selection odds — see `docs/contracts/encounters.md`.

## What's tested

`tests/test_encounter_data.cpp` — every entry of all six tables and the five
inline tables compared in full against its fixture (no subsets); hand-traced
spot checks (the World of Balance sector-0 backgrounds, map 32's group, a `$55`
world-rate byte and a `$04` map-rate byte, the 28 Veldt sectors, event group
93's first formation, and the rand-group-112 randomize flag); and a check that
the rate increments read as decimal magnitudes.
