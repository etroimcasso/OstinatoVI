# Menu ordering lists

Three small lists the menus read: where each esper sits in the esper list, how
the magic list groups spells under each magic-order setting, and which items an
imp can equip.

## The surface

```cpp
#include "data/menu_orders.h"

ostinato::esperMenuPlace(EsperId::RAMUH);        // 1 — first in the list
ostinato::magicListOrder(/*setting=*/0);         // CURE, FIRE, SCAN
ostinato::isImpEquipment(ItemId::IMP_HALBERD);   // true
```

| Function | Returns |
|---|---|
| `esperMenuPlace(esper)` | The esper's place in the list, counting from 1 |
| `esperMenuOrder()` | All 27 `{ esper, place }` entries, in esper id order |
| `magicListOrder(setting)` | The three band-opening spells, in list order |
| `magicListOrders()` | All six `{ setting, firstSpells }` entries |
| `isImpEquipment(item)` | Whether an imp can equip the item |
| `impEquipment()` | All sixteen `{ slot, item }` entries |

## The esper order

The esper list is not in esper id order. Each esper carries the place it
occupies, and the 27 places are the numbers 1 to 27 — a list that repeated or
skipped a place would be a defect, and the tests check for it.

## The magic order

The magic list is built from three bands of spells — black magic, effect magic
and white magic — and the player picks the order the bands appear in. Each of
the six settings names the first spell of each band in the order that setting
shows them, which is how the battle and the menu both find where a band starts.

The six settings are the six possible orderings of three bands, so every
arrangement is available. The config menu clamps the player's choice to 0..5
(`config.asm:1108`), so there is no setting without a row.

## The imp equipment list

Sixteen slots, of which the game reads the first ten (`equip.asm:1670`). The
remaining six are `ItemId::EMPTY` and nothing reads them; they are kept as the
table holds them. `isImpEquipment()` only considers the ten.

## Changing a list

Edit the row in the matching file under `src/data/generated/`:

```cpp
{ EsperId::RAMUH, 1 },                                                  // esper_menu_order_data.inc
MagicListOrderEntry{ .setting = 0, .firstSpells = { AttackId::CURE, AttackId::FIRE, AttackId::SCAN } },
{ 0, ItemId::CURSED_SHLD },                                             // imp_equipment_data.inc
```

Then update the matching entry in `tests/fixtures/menu_orders_expected.h`, which
holds the raw bytes. `ostinato-vi-misc-tests` compares every entry against it
and re-checks each list's invariant, so a swap that leaves two espers in the
same place, or an eleventh imp item, is reported.

## Gotchas

- **Esper places count from 1, not 0.** The list's first entry is place 1.
- **A magic-order row names spells, not bands.** The spell it names is the first
  of its band; work out the band from the spell rather than from the position in
  the row.
- **The magic-order terminator is not a field.** The table stores `$FF` after
  each row's three spells; the port asserts it and does not carry it.
- **The imp list has sixteen slots but ten items.** Iterate `impEquipment()` only
  if you want the empty slots too; otherwise ask `isImpEquipment()`.

## Where to change things

| Change | File |
|---|---|
| An esper's place | `src/data/generated/esper_menu_order_data.inc` (+ the fixture) |
| A magic-order setting | `src/data/generated/magic_list_order_data.inc` (+ the fixture) |
| The imp's equipment | `src/data/generated/imp_equipment_data.inc` (+ the fixture) |
| The entry types or accessors | `src/data/menu_orders.h`, `src/data/menu_orders.cpp` |

## See also

- [espers.md](espers.md) — the esper records the list orders
- [battle-tables.md](battle-tables.md) — the battle's own spell-order offsets,
  keyed by the same setting
- [item-properties.md](item-properties.md) — the items the imp list names
