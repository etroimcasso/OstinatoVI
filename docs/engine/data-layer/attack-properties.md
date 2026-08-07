# Attack properties

## Public surface

```cpp
#include "data/attack_properties.h"

const ostinato::AttackProperties& fire =
    ostinato::getAttackProperties(ostinato::AttackId::FIRE);

for (const auto& entry : ostinato::attackPropertiesEn()) {
    // entry.id     — AttackId
    // entry.record — AttackProperties
}

fire.specialEffect == ostinato::AttackSpecialEffect::NONE;   // true — Fire has none
```

## The record

```cpp
struct AttackProperties {
    Targeting      targeting;      // +0  packed targeting byte
    ElementSet     element;        // +1  elemental affinities
    AttackTraitSet traits;         // +2  physical / instant-death / random-target / ...
    AttackFlags1   flags1;         // +3  field-usable / reflect / lore / runic / ...
    AttackFlags2   flags2;         // +4  restore / drain / status ops / fractional / ...
    std::uint8_t   mpCost;         // +5
    std::uint8_t   power;          // +6  spell power (damage/heal formula input)
    AttackMiscFlags misc;          // +7  status-immunity miss / attack message
    std::uint8_t   hitRate;        // +8
    AttackSpecialEffect specialEffect;  // +9  dispatch index; NONE = 0xFF
    StatusSet      statuses;       // +10..13  the four status bytes
};
static_assert(sizeof(AttackProperties) == 14);
```

One 14-byte record per attack, byte-identical to the ROM's `magic_prop` record.
Field order and widths mirror the ROM layout — each field's byte offset is pinned
by an `offsetof` `static_assert`, and the annotated RAM map
(`original-src/notes/battle-ram.txt`) documents what each byte means. The wrapper
field types are covered in [typed-wrappers.md](typed-wrappers.md).

`specialEffect` names the record's entry in the battle engine's special-effect
dispatch space (`AttackSpecialEffect`, `include/ostinato/attack_effects.h`). Each
enumerator is cited to its handler in the disassembly's attacker/target jump
tables; `AttackSpecialEffect::NONE` (`0xFF`) marks the rows with no effect —
most of the table. The dispatch itself, including the transform that disables
the `NONE` sentinel, belongs to the battle engine, not the data layer. Weapon
and consumable effects feed the same dispatch space through their own
sub-encodings — `WeaponSpecialEffect` / `ItemUseEffect` in
[item-properties.md](item-properties.md).

## The table and the index space

```cpp
struct AttackPropertiesEntry { AttackId id; AttackProperties record; };

const AttackProperties& getAttackProperties(AttackId id);       // total: every byte value is a row
std::span<const AttackPropertiesEntry> attackPropertiesEn();    // all 256, index order
```

256 records spanning the full unified attack space — menu spells, esper summons,
skean throws, SwdTech, Blitz, dance attacks, Lores, tools, magitek, enemy attacks,
and desperations all live in this one table
([foundational-enums.md](foundational-enums.md) § `AttackId`). Hence "attack", not
"magic": the ROM file name says `magic_prop`, but only the first 54 rows are menu
spells. `AttackId` is `uint8_t`, so `getAttackProperties` is total — every argument
value indexes a real row.

## Language variants

The ROM table is language-variant: the English and Japanese ROMs carry separate
data (`magic_prop_en.dat` / `magic_prop_jp.dat` in the disassembly's rip output).
The shipped surface is the English table — hence `attackPropertiesEn()`. A
language-dispatch axis over `getAttackProperties` is planned for when the Japanese
table can be ripped from a Japanese ROM; until then the EN table is the sole
backing store and the JP gap is visible as a skipped test.

## Backing data / where to change

Rows live in `src/data/generated/magic_prop_en_data.inc` (included into the array
in `src/data/attack_properties.cpp`), one designated-initializer row per attack.
Flag bytes are written symbolically via the builders —
`.element = ElementSet::of(Element::FIRE)`,
`.flags1 = AttackFlags1::of(AttackFlag1::ENABLE_RUNIC, ...)` — so a rebalance edit
names the bit, never a mask; the special-effect byte likewise names its
enumerator (`.specialEffect = AttackSpecialEffect::DOOM`). A compile-time assert verifies every row's `.id`
matches its array position. A deliberate change must also update the matching row
in `tests/fixtures/magic_prop_expected.h` (original ROM values).

## What's tested

`tests/test_attack_properties.cpp` — every one of the 256 records
`memcmp`-verified byte-identical to the fixture; a semantic spot-check of
`AttackId::FIRE` hand-traced from the ROM bytes; named special-effect
spot-checks (Doom, Golem, Quake, Retort); round-trips of every builder family
back to raw ROM bytes; and the visible JP-variant skip.
