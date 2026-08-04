# Espers

## Public surface

```cpp
#include "data/esper_properties.h"

const ostinato::EsperProperties& ramuh =
    ostinato::getEsperProperties(ostinato::EsperId::RAMUH);
// ramuh.spells[0].learnRate, ramuh.spells[0].spell, ..., ramuh.bonus

ostinato::kEsperProperties;   // std::array<EsperPropertiesEntry, 27>
```

## The record

```cpp
struct EsperSpell {
    std::uint8_t learnRate;   // x1..x25 learn multiplier; 0 in empty slots
    AttackId     spell;       // the taught spell; AttackId::NONE in empty slots
};
static_assert(sizeof(EsperSpell) == 2);

struct EsperProperties {
    std::array<EsperSpell, 5> spells;   // five teach slots
    EsperBonus bonus;                   // level-up bonus; EsperBonus::NONE when none
};
static_assert(sizeof(EsperProperties) == 11);
```

One 11-byte record per esper: five learn-spell pairs, then the level-up bonus
byte. Within each pair the ROM byte order is **rate first, spell second** — the
opposite of the natural-magic pairs ([natural-magic.md](natural-magic.md)); don't
conflate the two. An empty teach slot is `{ .learnRate = 0, .spell =
AttackId::NONE }`, and a record with no level-up bonus stores `EsperBonus::NONE` —
the two are independent (Shiva has five spells and no bonus; Ragnarok has one
spell and no bonus).

## The table and the index space

```cpp
struct EsperPropertiesEntry { EsperId id; EsperProperties record; };

const EsperProperties& getEsperProperties(EsperId id);   // debug-asserts $36..$50
```

27 records in `EsperId` order. `EsperId` is **not zero-based**: esper identity
lives inside the unified attack space at `RAMUH=0x36 .. PHOENIX=0x50`
(`static_cast<AttackId>(esperId)` is the esper's summon attack), so the table
lookup subtracts `EsperId::RAMUH` and the entry law is
`id == position + EsperId::RAMUH` — checked at compile time.

## Backing data / where to change

Rows live in `src/data/generated/genju_prop_data.inc` — each teach slot names its
rate and spell, so changing what an esper teaches is a direct edit:

```cpp
EsperPropertiesEntry{
    .id = EsperId::RAMUH,
    .record = EsperProperties{
        .spells = { EsperSpell{ .learnRate = 10, .spell = AttackId::BOLT },
                    EsperSpell{ .learnRate =  2, .spell = AttackId::BOLT_2 },
                    /* ... */ },
        .bonus = EsperBonus::STAMINA_1,
    },
},
```

A deliberate change must also update the matching row in
`tests/fixtures/genju_prop_expected.h` (original ROM values).

## What's tested

`tests/test_esper_properties.cpp` — every one of the 27 records `memcmp`-compared
byte-for-byte against the fixture (identity checked against the $36-based law on
both sides); semantic spot-checks of Ramuh's rate-first pairs and Starlet's bonus;
and the empty-slot / missing-bonus sentinels via Ragnarok and Shiva.
