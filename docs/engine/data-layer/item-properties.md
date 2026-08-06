# Item properties

## Public surface

```cpp
#include "data/item_properties.h"

const ostinato::ItemProperties& excalibur =
    ostinato::getItemProperties(ostinato::ItemId::EXCALIBUR);

for (const auto& entry : ostinato::itemPropertiesEn()) {
    // entry.id     — ItemId
    // entry.record — ItemProperties
}
```

## The record

```cpp
struct ItemProperties {
    ItemTypeUsage    typeAndUsage;       // +0     item type + usage flags
    EquipPermissions equippableBy;       // +1-2   who can equip it
    std::uint8_t     spellLearnRate;     // +3
    AttackId         spellLearned;       // +4
    FieldEffectSet   fieldEffects;       // +5     sprint shoes / charm bangle / ...
    Status1Set       status1Protection;  // +6
    Status2Set       status2Protection;  // +7
    Status3Set       status3Granted;     // +8
    RelicEffect1Set  relicEffects1;      // +9     damage raisers, HP/MP boosts
    RelicEffect2Set  relicEffects2;      // +10    command replacements
    RelicEffect3Set  relicEffects3;      // +11    rate raisers, MP costs
    RelicEffect4Set  relicEffects4;      // +12    x-fight, counter, genji glove, ...
    RelicEffect5Set  relicEffects5;      // +13    low-HP auto-casts, exp/GP doublers
    Targeting        targeting;          // +14
    ElementSet       element;            // +15    (role varies by type)
    StatBoostPair    vigorSpeed;         // +16    two signed nibbles
    StatBoostPair    staminaMagicPower;  // +17
    ItemSpellCast    spellCast;          // +18    spell + cast-mode bits
    WeaponFlagSet    weaponFlags;        // +19    (role varies by type)
    std::uint8_t     power;              // +20    (role varies by type)
    std::uint8_t     hitRateOrDefense;   // +21    (role varies by type)
    ElementSet       elementsAbsorbed;   // +22    (role varies by type)
    ElementSet       elementsNullified;  // +23    (role varies by type)
    ElementSet       elementsWeak;       // +24    (role varies by type)
    Status2Set       status2Set;         // +25    cursed-gear statuses
    EvadeBlockPair   evadeMagicBlock;    // +26    two evade-table indices
    ItemSpecialEffect specialEffect;     // +27    (role varies by type)
    std::uint16_t    price;              // +28-29
};
static_assert(sizeof(ItemProperties) == 30);
```

One 30-byte record per item, byte-identical to the ROM's `item_prop` record.
Items, weapons, armor, and relics all share this one table, so several fields
are **role-overloaded**: their meaning depends on `typeAndUsage.type()`. `power`
is a weapon's battle power, a piece of armor's defense, and a consumable's HP/MP
restored; `hitRateOrDefense` is hit rate on a weapon, magic defense on armor, and
a status byte on a consumable; `element` is a weapon's attack element but armor's
halved elements. Members carry equipment-primary names (five of the seven item
types are equipment); the full per-type role tables live in
[`docs/contracts/item-shop-data.md`](../../contracts/item-shop-data.md).

## The packed wrappers

Six field types decompose the record's packed bytes. Each is a one-byte (or
two-byte) struct with named accessors and an `of(...)` builder that re-packs to
the exact ROM byte:

- **`ItemTypeUsage`** (in `data/item_properties.h`) — `type()` returns the
  `ItemType` (bits 0-2), `usage()` the `ItemUsage` flags (throwable /
  battle-usable / menu-usable).
- **`EquipPermissions`** (`ostinato/equip_permissions.h`) — the 16-bit equip
  mask stored as its little-endian byte pair. `canEquip(CharacterId)` mirrors
  the equip menu's `1 << actor` test (bits 0-13 are the playable characters;
  guest actors map onto the top two bits); `impGear()` / `heavyGear()` test
  those top bits' special roles; `bits()` returns the assembled mask.
- **`StatBoostPair`** (`ostinato/stat_boost_pair.h`) — two signed nibbles
  (−7..+7); `first()` / `second()` decode them, `of(first, second)` re-packs.
  `vigorSpeed` holds vigor then speed; `staminaMagicPower` stamina then magic
  power.
- **`ItemSpellCast`** (in `data/item_properties.h`) — `spell()` plus the two
  mode bits: `randomOnAttack()` (the rods' 1-in-4 proc) and `castOnItemUse()`
  (cast when used as an item mid-battle).
- **`EvadeBlockPair`** (in `data/item_properties.h`) — two nibble indices into
  the battle engine's evade-boost table: `evadeIndex()` / `mblockIndex()`.
- **`ItemSpecialEffect`** (in `data/item_properties.h`) — role-packed. For
  equipment: `weaponEffect()` (the named `WeaponSpecialEffect` — Drainer, Atma
  Weapon, Dice, ...), `blockGraphic()`, and `blocksPhysical()` /
  `blocksMagic()`. For consumables: `itemUseEffect()` (the named
  `ItemUseEffect` — Magicite, Elixir, Warp Stone, ...) and
  `itemUseDisabled()`.

The flag spaces those fields draw on live in `ostinato/item_effects.h`: the
`FieldEffect` bits, the five `RelicEffect1..5` spaces (every documented bit
named — `RelicEffect2::FIGHT_TO_JUMP`, `RelicEffect3::MP_COST_1`, ...), the
one-byte status slices (`Status1Set`/`Status2Set`/`Status3Set`), the
`ItemUseFlag` bits, and `SpellCastMode`.

## The +19 byte's two roles

`weaponFlags` carries `WeaponFlags` bits on a weapon (SwdTech-capable,
back-row-capable, two-handed, runic) and item-use behavior bits on a consumable
(restores HP/MP, removes status, inverts on undead, fractional damage). Two
bridges convert between the views without changing the byte:

```cpp
.weaponFlags = itemUseFlags(ItemUseFlag::RESTORES_HP, ...)  // build (rows)
itemUseView(potion.weaponFlags).has(ItemUseFlag::RESTORES_HP)  // read
```

Two +19 bits are set in the ROM data but read by no game code — they are kept
verbatim through the named constants `kDeadItemFlagBit0` (Paladin Shld, Memento
Ring, Safety Bit) and `kDeadItemFlagBit6` (Magicite, Super Ball); the trace
notes sit at their definitions in `data/item_properties.h`.

## The table and the index space

```cpp
struct ItemPropertiesEntry { ItemId id; ItemProperties record; };

const ItemProperties& getItemProperties(ItemId id);      // total: every byte value is a row
std::span<const ItemPropertiesEntry> itemPropertiesEn(); // all 256, index order
```

256 records over the full `ItemId` space ($00 Dirk .. $FE Dried Meat, $FF the
EMPTY sentinel). `ItemId` is `uint8_t`, so `getItemProperties` is total — every
argument value indexes a real row.

## Language variants

The ROM table is language-variant: the English and Japanese ROMs carry separate
data (`item_prop_en.dat` / `item_prop_jp.dat` in the disassembly's rip output).
The shipped surface is the English table — hence `itemPropertiesEn()`. A
language-dispatch axis over `getItemProperties` is planned for when the Japanese
table can be ripped from a Japanese ROM; until then the EN table is the sole
backing store and the JP gap is visible as a skipped test.

## Backing data / where to change

Rows live in `src/data/generated/item_prop_en_data.inc` (included into the array
in `src/data/item_properties.cpp`), one designated-initializer row per item.
Every packed byte is written symbolically through the builders —
`.equippableBy = EquipPermissions::of(CharacterId::TERRA, ..., EquipSpecial::HEAVY)`,
`.specialEffect = ItemSpecialEffect::weapon(WeaponSpecialEffect::DRAINER)` — so
a rebalance edit names the bit, never a mask. A compile-time assert verifies
every row's `.id` matches its array position. A deliberate change must also
update the matching row in `tests/fixtures/item_prop_expected.h` (original ROM
values).

## What's tested

`tests/test_item_properties.cpp` — every one of the 256 records
`memcmp`-compared in full against the fixture; semantic spot-checks of
weapon, consumable, and relic surfaces hand-traced from the ROM bytes
(Excalibur, Potion, Magicite, the imp-gear and dead-bit rows); round-trips of
every builder family back to raw ROM bytes; and the visible JP-variant skip.
