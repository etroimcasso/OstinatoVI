# Character base stats

## Public surface

```cpp
#include "data/character.h"

const ostinato::CharacterBaseStats& record =
    ostinato::getCharacterBaseStats(ostinato::CharacterPropId::TERRA);

for (const auto& entry : ostinato::characterBaseStats()) {
    // entry.id     — CharacterPropId
    // entry.record — CharacterBaseStats
}
```

## The record

```cpp
struct CharacterBaseStats {
    std::uint8_t hp;                            // starting max HP
    std::uint8_t mp;                            // starting max MP
    std::array<BattleCommandId, 4> commands;    // battle menu, slot order
    std::uint8_t strength;
    std::uint8_t agility;
    std::uint8_t stamina;
    std::uint8_t magicPower;
    std::uint8_t battlePower;
    std::uint8_t defense;
    std::uint8_t magicDefense;
    std::uint8_t evade;
    std::uint8_t magicBlock;
    ItemId weapon;                              // initial equipment; EMPTY when none
    ItemId shield;
    ItemId helmet;
    ItemId armor;
    ItemId relic1;
    ItemId relic2;
    CharacterTraits traits;                     // packed run/level/fixed-equip byte
};
static_assert(sizeof(CharacterBaseStats) == 22);
```

One 22-byte record per character, byte-identical to the ROM's `char_prop` record —
member order and widths mirror the ROM layout exactly, pinned by the
`static_assert` and the byte-equivalence test. Empty command slots store
`BattleCommandId::NONE` (0xFF); empty equipment slots store `ItemId::EMPTY` (0xFF).
The `traits` byte is the packed run-factor / level-modifier / fixed-equip field —
see [typed-wrappers.md](typed-wrappers.md).

## The table and the index space

```cpp
struct CharacterBaseStatsEntry { CharacterPropId id; CharacterBaseStats record; };

const CharacterBaseStats& getCharacterBaseStats(CharacterPropId id);  // debug-asserts 0..63
std::span<const CharacterBaseStatsEntry> characterBaseStats();        // all 64, index order
```

64 records in `CharacterPropId` order (`0x00..0x3F`): the fourteen playable
characters, the guests (Banon, Leo, ghosts, the moogle rescue cast, Maduin, Wedge,
Vicks), seven Kefka variants, the Colosseum Shadow, seven zero-filled unused slots
(`UNUSED_22..UNUSED_28`, rendered as `CharacterBaseStats{}` — all 22 bytes zero,
distinct from the 0xFF empty-slot sentinels real records use), and the
dragon-den/soul-shrine cast (`TORK..HO`).

The index is a **`CharacterPropId`, not a `CharacterId`** — the 16-value actor
space is heavily aliased and cannot address 64 records, so no conversion between
the two is provided. In the original, the record index arrives from game state (the
actor number the event and battle code multiplies by 22); any actor→record mapping
belongs to that consumer logic, not to this table.

## Backing data / where to change

Rows live in `src/data/generated/char_prop_data.inc` (included into the array in
`src/data/character.cpp`), one designated-initializer row per record:

```cpp
CharacterBaseStatsEntry{  // [$00]
    .id = CharacterPropId::TERRA,
    .record = CharacterBaseStats{
        .hp = 40, .mp = 16,
        .commands = { BattleCommandId::FIGHT, BattleCommandId::MORPH,
                      BattleCommandId::MAGIC, BattleCommandId::ITEM },
        .strength = 31, /* ... */
        .weapon = ItemId::MITHRILKNIFE, /* ... */
        .traits = { RunFactor::NORMAL, LevelMod::NORMAL, false },
    },
},
```

To rebalance a character, edit the field in their row — every value is named at the
row, so there's nothing positional to decode. A compile-time assert in
`character.cpp` verifies every row's `.id` matches its array position, so rows
can't be reordered silently. A deliberate change must also update the matching row
in `tests/fixtures/char_prop_expected.h`, which holds the original ROM values.

## What's tested

`tests/test_character_base.cpp` — every one of the 64 records `memcmp`-compared
byte-for-byte against the fixture (identity fields checked on both sides), plus
semantic spot-checks of the lookup and the trait accessors (Terra's defaults;
Banon's `VERY_LOW` run / `LOW` level / fixed-equip packing).
