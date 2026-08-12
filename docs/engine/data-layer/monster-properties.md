# Monster properties & metamorph tables

## Public surface

```cpp
#include "data/monster_properties.h"
#include "data/metamorph.h"

const ostinato::MonsterProperties& m =
    ostinato::getMonsterProperties(ostinato::MonsterId::SKULL_DRGN);
m.hp;                                  // 32800 — u16 stats read as plain numbers
m.traitFlags.has(MonsterTraitFlag::DIES_AT_ZERO_MP);   // true
m.battleFlags.has(MonsterBattleFlag::CANT_CONTROL);    // true
m.weakElements.has(Element::FIRE);                     // true

// The metamorph byte keys both metamorph tables:
const auto& pack = ostinato::getMetamorphPack(m.metamorph);  // 4 ItemIds
std::uint8_t odds = ostinato::metamorphRate(m.metamorph);    // threshold byte

for (const auto& entry : ostinato::monsterProperties()) {
    // entry.id     — the MonsterId
    // entry.record — MonsterProperties
}
```

## The record

```cpp
struct MonsterProperties {
    std::uint8_t speed, attackPower, hitRate, evade, magicBlock;
    std::uint8_t defense, magicDefense, magicPower;        // +0..+7
    std::uint16_t hp, mp, experience, gold;                // +8..+15, little-endian
    std::uint8_t level;                                    // +16
    MetamorphInfo metamorph;                               // +17 packed
    MonsterTraitFlags traitFlags;                          // +18
    MonsterBattleFlags battleFlags;                        // +19
    BlockedStatusSet blockedStatuses;                      // +20..+22 (three bytes)
    ElementSet absorbElements, nullifyElements, weakElements;  // +23..+25
    ItemId attackGraphic;                                  // +26
    StatusSet innateStatuses;                              // +27..+30
    MonsterSpecialAttack specialAttack;                    // +31 packed
};
static_assert(sizeof(MonsterProperties) == 32);
```

One 32-byte record per monster, byte-identical to the ROM's `MonsterProp`
record; every offset is pinned by a `static_assert`, and the u16 fields
assume (and statically assert) a little-endian platform so their object
bytes stay in ROM order. The stat bytes are the raw stored values — the
battle code applies its own transforms when loading (evade/magic-block
inversion, the magic-power `AddHalf`), so what you edit here is what the
ROM stores, not the derived in-battle number.

Three record fields are packed types of their own:

- **`metamorph`** (`MetamorphInfo`, `ostinato/metamorph_info.h`) — low 5
  bits pick the item pack, high 3 bits the odds row. Read with
  `packIndex()` / `rate()`; build with labeled fields:
  `MetamorphInfo::of({ .packIndex = 6, .rate = MetamorphRate::ODDS_1_8 })`.
  `MetamorphRate` names the eight odds rows (`ODDS_255_256` down through
  `ODDS_1_32`, then `NEVER`).
- **`specialAttack`** (`MonsterSpecialAttack`,
  `ostinato/monster_special_attack.h`) — the monster's special-attack
  byte, built through per-band builders that say what the byte does:
  `inflictStatus(StatusId::...)`, `damageBoost(n)`, `drainHp()`,
  `drainMp()`, or `removeReflect()`; chain `.withCantDodge()` /
  `.withNoDamage()` for the two modifier bits. Read back with
  `effectClass()` / `cantDodge()` / `noDamage()`.
- **`blockedStatuses`** (`BlockedStatusSet`,
  `ostinato/blocked_status_set.h`) — the record's status immunities. The
  ROM record has **no fourth immunity byte**, so this is a three-byte set
  accepting only statuses homed in status bytes 1-3 (`StatusId` values
  below 24); passing `RAGE` or later trips an assert. Same `has`/`set`/
  `of(...)` shape as `StatusSet`.

The two flag bytes (`ostinato/monster_flags.h`) name every bit:
`MonsterTraitFlag` (undead, human, imp critical, dies-at-0-MP,
don't-display-name, plus three named `UNUSED_n` positions) and
`MonsterBattleFlag` (first strike, harder-to-run, and the can't-run /
-scan / -sketch / -suplex / -control set, plus the upstream's own
uncertainly-documented `SPECIAL_EVENT`).

## The table and the index space

```cpp
struct MonsterPropertiesEntry { MonsterId id; MonsterProperties record; };

const MonsterProperties& getMonsterProperties(MonsterId id);  // asserts id < 384
std::span<const MonsterPropertiesEntry> monsterProperties();  // all 384, index order
```

`MonsterId` spans the full 384-monster space (placeholder slots included).
The table is version-invariant: one table backs every supported ROM.

## The metamorph tables

```cpp
struct MetamorphPack { std::array<ItemId, 4> items; };       // sizeof == 4

const MetamorphPack& getMetamorphPack(MetamorphInfo info);   // total (5-bit index)
std::uint8_t metamorphRate(MetamorphInfo info);              // total (3-bit index)
```

Thirty-two 4-item packs plus eight threshold bytes, both keyed off a
monster's `metamorph` field exactly as the game keys them: the effect
succeeds when a random byte compares below the threshold, then hands over
one of the pack's four items — picking which of the four is the metamorph
effect's job, not the table's. A threshold is a magnitude on a 0-255 scale,
so the ladder runs from 255 for `ODDS_255_256` down to 0 for `NEVER`.
Packs 26-31 are
unused by every shipped monster; their bytes resolve like any others.

## Backing data / where to change

Record rows live in `src/data/generated/monster_prop_data.inc` (included
into the array in `src/data/monster_properties.cpp`); pack and rate rows
live in `src/data/generated/metamorph_prop_data.inc` /
`metamorph_rate_data.inc` (included in `src/data/metamorph.h`). To change
a monster's stats, edit its row — numerics are plain decimal, flags and
statuses go through the `of(...)` builders, the metamorph byte through
`MetamorphInfo::of({...})`, and the special attack through its per-band
builder. Compile-time asserts verify every row's identity field matches
its array position. A deliberate change must also update the matching row
in `tests/fixtures/monster_prop_expected.h` (or
`tests/fixtures/metamorph_expected.h`), which carry the original ROM
bytes.

For the record semantics themselves — what each byte does in battle, the
flag-bit consumer traces, the special-attack band decode — see
`docs/contracts/monster-data.md`.

## What's tested

`tests/test_monster_properties.cpp` — every one of the 384 records
`memcmp`-compared in full against the fixture; the 32 packs and 8 rates
likewise; semantic spot-checks hand-traced from the ROM (Guard's full
stat line, Orog's undead/human flags, Soldier's can't-run, Skull Dragon's
flag bytes, metamorph byte, and `$FF` special-attack byte); builder
round-trips for `MetamorphInfo`, `MonsterSpecialAttack`, and
`BlockedStatusSet`; and the metamorph accessors keyed through
`MetamorphInfo`.
