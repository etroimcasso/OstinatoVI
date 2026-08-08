# Monster tables — items, rage, sketch, control, animation, alignment

Six per-monster satellite tables keyed by `MonsterId`, alongside the main
record in [monster-properties.md](monster-properties.md): what a monster
carries (steal/drop items), what it does under Rage, Sketch, and Control,
which animation its special attack plays, and where the colosseum anchors it
vertically.

## Public surface

```cpp
#include "data/monster_align.h"
#include "data/monster_attacks.h"
#include "data/monster_items.h"
#include "data/monster_special_anim.h"

using ostinato::MonsterId;

const auto& items = ostinato::getMonsterItems(MonsterId::NINJA);
items.rareSteal;    // ItemId::CHERUB_DOWN
items.commonDrop;   // ItemId::EMPTY — no drop in this slot

const auto& rage   = ostinato::getMonsterRage(MonsterId::GUARD);     // id < 256
const auto& sketch = ostinato::getMonsterSketch(MonsterId::NINJA);
const auto& ctrl   = ostinato::getMonsterControl(MonsterId::NINJA);
// each: .attacks[slot] — AttackId enumerators

ostinato::monsterSpecialAnim(MonsterId::MAG_ROADER_1);
// MonsterAttackAnimation::WHEEL

ostinato::getMonsterAlignment(MonsterId::TRAPPER);                   // id < 256
// MonsterVerticalAlignment::CEILING
```

Every table also exposes a full-table span for iteration —
`monsterItems()`, `monsterRages()`, `monsterSketches()`,
`monsterControls()`, `monsterSpecialAnims()`, `monsterAlignments()` — each
yielding entries of `{ id, ... }` in `MonsterId` order.

## Steal/drop items

```cpp
struct MonsterItems {
    ItemId rareSteal;     // Steal succeeds rarely
    ItemId commonSteal;   // Steal succeeds usually
    ItemId rareDrop;      // victory drop, rare
    ItemId commonDrop;    // victory drop, usual
};
static_assert(sizeof(MonsterItems) == 4);
```

One 4-byte record per monster, all 384 monsters. `ItemId::EMPTY` marks a
slot with nothing in it.

## Rage, Sketch, and Control attacks

```cpp
struct MonsterRage    { std::array<AttackId, 2> attacks; };  // sizeof == 2
struct MonsterSketch  { std::array<AttackId, 2> attacks; };  // sizeof == 2
struct MonsterControl { std::array<AttackId, 4> attacks; };  // sizeof == 4
```

Three attack-slot tables with structural rules the data itself carries:

- **Rage** (256 records): slot 0 is **always** `AttackId::BATTLE` — the
  monster's normal fight command — and only slot 1 varies per monster. The
  rage behavior coin-flips between the two slots (1/2 each).
- **Sketch** (384 records): two candidate attacks; the sketch effect picks
  slot 1 at 3/4 probability, slot 0 at 1/4.
- **Control** (384 records): slot 0 is always `AttackId::BATTLE`; slots 1-3
  hold the commandable attacks, padded with `AttackId::NONE` where a
  monster offers fewer. Consumers treat `NONE` as the empty sentinel, so a
  fully-empty row (`BATTLE, NONE, NONE, NONE`) means "only Battle."

The rage table covers monsters 0-255 only: the game indexes it with an
8-bit monster id, so monsters 256-383 have no rage row, and
`getMonsterRage` asserts `id < 256`. The slot probabilities live in the
battle logic; the per-slot comments in the generated rows carry them for
reference.

## Special-attack animation

```cpp
enum class MonsterAttackAnimation : std::uint8_t {
    HIT, SICKLE, DIVE, CRITICAL, /* ... 35 enumerators ... */ UNUSED_34,
};

MonsterAttackAnimation monsterSpecialAnim(MonsterId id);
```

One value per monster (all 384): which of the 35 monster attack animations
its special attack plays. The animation rows themselves (graphics timing
data at ROM `EC/E6E8`) are battle-graphics data outside the data layer —
this enum is their id space.

The ROM has no names for the 35 animations; the enumerator names derive
from the game's own special-attack display names — each animation is named
by the **dominant display name among the monsters that use it** (Mag Roader
specials are all named "Wheel" and share the spinning-wheel animation, so
its row is `WHEEL`). The per-enumerator comments in
`ostinato/monster_attack_animation.h` carry the derivation counts; a
1-of-N name (`WING`, `NEAR_FATAL`, `TRADEOFF`) is representative of its
row, not authoritative. Rows 29 and 34 are used by no monster and keep
`UNUSED_n` names.

## Vertical alignment

```cpp
enum class MonsterVerticalAlignment : std::uint8_t {
    CEILING = 0, GROUND = 1, BURIED = 2, FLOATING = 3, FLYING = 4,
};

MonsterVerticalAlignment getMonsterAlignment(MonsterId id);  // asserts id < 256
```

Where the colosseum anchors a monster's sprite vertically. Both game
consumers are colosseum-specific and index with an 8-bit monster id, so
the table covers monsters 0-255 only — same bound rule as rage. `CEILING`
pins the sprite to the top of the screen; the other four apply per-value
y-offsets from the battle-graphics code.

## Backing data / where to change

Rows live in `src/data/generated/` — `monster_items_data.inc`,
`monster_rage_data.inc`, `monster_sketch_data.inc`,
`monster_control_data.inc`, `monster_special_anim_data.inc`,
`monster_align_data.inc` — each included into its table's array in the
matching `src/data/*.cpp`. Every value is a named enumerator, so changes
are one-word edits:

```cpp
    MonsterControlEntry{
        .id = MonsterId::NINJA,
        .record = MonsterControl{ .attacks = {
            AttackId::BATTLE,      // slot 0 (always BATTLE)
            AttackId::FIRE_SKEAN,  // slot 1
            AttackId::WATER_EDGE,  // slot 2
            AttackId::BOLT_EDGE,   // slot 3
        } },
    },
```

Keep the structural rules intact when editing: rage/control slot 0 stays
`BATTLE` (tests verify it on every row), control pads with `NONE`, and
alignment values stay within the five named enumerators. Compile-time
asserts verify every row's identity field matches its array position. A
deliberate change must also update the matching row in the table's fixture
under `tests/fixtures/` (`monster_items_expected.h`,
`monster_rage_expected.h`, `monster_sketch_expected.h`,
`monster_control_expected.h`, `monster_special_anim_expected.h`,
`monster_align_expected.h`), which carry the original ROM bytes.

For the tables' consumer semantics — which game routines read each table
and how — see `docs/contracts/monster-data.md`.

## What's tested

`tests/test_monster_tables.cpp` — every record of all six tables compared
in full against its fixture (no subsets); the rage and control
BATTLE-slot-0 invariant on every row; control's `NONE` padding; and
hand-traced spot checks (Guard's steal/drop line, Ninja's sketch and
control rows, Guard/Mag Roader animations, Guard/Trapper/Pterodon
alignments).
