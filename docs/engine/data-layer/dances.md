# Dances

## Public surface

```cpp
#include "data/dance_properties.h"

const ostinato::DanceProperties& rondo =
    ostinato::getDanceProperties(ostinato::DanceId::WATER_RONDO);
// rondo.attacks[0] .. rondo.attacks[3] — the four candidate attacks

ostinato::kDanceProperties;   // std::array<DancePropertiesEntry, 8>
```

## The record

```cpp
struct DanceProperties {
    std::array<AttackId, 4> attacks;   // candidate attacks, slot order
};
static_assert(sizeof(DanceProperties) == 4);
```

One 4-byte record per dance: the four attacks the dance can produce, in slot
order. Slot position **is** the probability tier — the battle routine rolls
7/16 : 3/8 : 1/8 : 1/16 across slots 0–3. Those rate thresholds live in the battle
logic, not in this record; the record is just the four candidates.

## The table

```cpp
struct DancePropertiesEntry { DanceId id; DanceProperties record; };

const DanceProperties& getDanceProperties(DanceId id);   // debug-asserts 0..7
```

8 records in `DanceId` order (`WIND_SONG=0x00 .. SNOWMAN_JAZZ=0x07`). `DanceId` is
`uint8_t` and not every byte value is a dance, so the accessor debug-asserts the
`0..7` bound.

## Backing data / where to change

Rows live in `src/data/generated/dance_prop_data.inc` — each row's four attacks
are named `AttackId` enumerators, so changing what a dance can produce is a
one-word edit:

```cpp
DancePropertiesEntry{
    .id = DanceId::WATER_RONDO,
    .record = DanceProperties{ .attacks = { AttackId::EL_NINO, AttackId::PLASMA,
                                            AttackId::SPECTER, AttackId::WILD_BEAR } },
},
```

A compile-time assert verifies every row's `.id` matches its array position. A
deliberate change must also update the matching row in
`tests/fixtures/dance_prop_expected.h` (original ROM values).

## What's tested

`tests/test_dance_properties.cpp` — every one of the 8 records `memcmp`-compared
byte-for-byte against the fixture, plus semantic spot-checks of Water Rondo's full
slot order and Wind Song's boundary slots.
