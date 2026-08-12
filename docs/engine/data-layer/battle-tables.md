# Battle tables — formula support and mappings

The battle engine leans on a set of small tables that are not attached to any one
record: the probability ladders behind a dance step and Umaro's attack choice, the
evade bonus a piece of equipment grants, the chain of formations the final battle
walks, which background each dance switches to, how long each AI script command
is, what a thrown tool turns into, and what each slot reel outcome does. This file
covers all of them and their accessors.

These are the *data* half of formulas whose arithmetic lives in the battle engine
proper — this layer answers "what are the numbers", not "how are they applied".

```cpp
#include "data/battle_tables.h"
```

## Probability ladders

```cpp
struct DanceStepThresholdEntry { std::uint8_t index; RandomThreshold threshold; };
struct RandomBitRateEntry      { std::uint8_t index; std::array<RandomThreshold, 4> weights; };

inline constexpr std::array<DanceStepThresholdEntry, 3> kDanceStepThresholds;
inline constexpr std::array<RandomBitRateEntry, 5>      kRandomBitRates;
```

A [`RandomThreshold`](typed-wrappers.md) is one rung of a ladder the engine walks
after rolling a random byte, counting how many rungs the roll reaches. The share
belonging to one outcome is the gap between its threshold and the next one up, so
the thresholds are kept as the numbers the engine actually compares against
rather than rewritten as fractions.

- `kDanceStepThresholds` picks which of a dance's four attacks is performed —
  16, 48, 144 give the documented 7/16, 3/8, 1/8, 1/16 split.
- `kRandomBitRates` weights a choice among four bits. Rows 0–3 are Umaro's attack
  choice; row 4 weights the battle type.

```cpp
ostinato::kDanceStepThresholds[0].threshold.value();   // 16
ostinato::kRandomBitRates[4].weights[0].value();       // 30
```

## Equipment evade

```cpp
struct EquipEvadeBoostEntry { std::uint8_t index; std::int16_t boost; };

inline constexpr std::array<EquipEvadeBoostEntry, 11> kEquipEvadeBoost;

std::int16_t equipEvadeBoost(std::uint8_t rating);
```

An item's evade and magic-block ratings each index this table. The boost is
**signed** — ratings 0–5 add 0…50, ratings 6–10 subtract 10…50:

```cpp
ostinato::equipEvadeBoost(5);    //  50
ostinato::equipEvadeBoost(10);   // -50
```

## The final battle chain

```cpp
inline constexpr std::array<FormationId, 4> kFinalBattleFormations;

struct FinalBattleScrollEntry { std::uint8_t index; std::uint8_t scroll; };
inline constexpr std::array<FinalBattleScrollEntry, 6> kFinalBattleScroll;
```

Clearing one formation in `kFinalBattleFormations` advances to the next, ending at
`FormationId::FINAL_KEFKA`. `kFinalBattleScroll` carries the background scroll
position each step sets.

## Dance and background

```cpp
struct DanceBackgroundEntry { DanceId dance; BattleBackgroundId background; };
struct BackgroundDanceEntry { BattleBackgroundId background; DanceId dance; };

inline constexpr std::array<DanceBackgroundEntry, 8>  kDanceBackgrounds;
inline constexpr std::array<BackgroundDanceEntry, 56> kBackgroundDances;

BattleBackgroundId danceBackground(DanceId dance);
DanceId            backgroundDance(BattleBackgroundId background);
```

The two directions are a pair: a dance switches the battle to its background, and
each background names the dance that "belongs" there. Dancing a background's own
dance keeps the background; dancing anything else has a chance to switch it.

```cpp
ostinato::danceBackground(DanceId::WIND_SONG);                    // FIELD_WOB
ostinato::backgroundDance(BattleBackgroundId::FIELD_WOB);         // WIND_SONG
```

The ROM tables are wider than the named key spaces — 10 rows for 8 dances and 64
for 56 backgrounds. The extra rows are unreachable padding, so they are not
modeled here; the tests still check their raw bytes.

## AI script

```cpp
struct AiCommandSizeEntry { AiScriptCommand command; std::uint8_t size; };

inline constexpr std::array<AiCommandSizeEntry, 16> kAiCommandSizes;

std::uint8_t aiCommandSize(AiScriptCommand command);
```

A monster's AI script is a byte stream: a value below the first command is an
attack to use, and `AiScriptCommand` introduces everything else. `size` is the
whole command including its own byte, which is what an interpreter advances by:

```cpp
ostinato::aiCommandSize(AiScriptCommand::USE_ATTACK);      // 4
ostinato::aiCommandSize(AiScriptCommand::END_OF_SCRIPT);   // 1
```

```cpp
struct AiCommandForAttack { AttackId attack; BattleCommandId command; };
inline constexpr std::array<AiCommandForAttack, 11> kAiCommandsForAttack;
```

`kAiCommandsForAttack` answers which command an AI-chosen attack belongs to. The
engine scans it from the end, so each row is the *first* attack id that maps to
its command — a threshold table, not an exact-match one.

## Throw and slot

```cpp
struct ThrowToolsConversion { ItemId item; std::uint8_t attackOffset; };
struct SlotOutcomeEntry     { std::uint8_t index; AttackId attack; };
struct JokerDoomTargetEntry { std::uint8_t index; BattleSlotMask targets; };

inline constexpr std::array<ThrowToolsConversion, 5> kThrowToolsConversions;
inline constexpr std::array<SlotOutcomeEntry, 8>     kSlotOutcomes;
inline constexpr std::array<JokerDoomTargetEntry, 2> kJokerDoomTargets;
```

- `kThrowToolsConversions` — throwing one of the five tools subtracts
  `attackOffset` from the item id to reach the attack it performs.
- `kSlotOutcomes` — the attack each slot reel outcome performs.
  `AttackId::NONE` is a sentinel meaning "roll a random esper instead", not a
  real attack.
- `kJokerDoomTargets` — the joker-doom outcomes target a whole side of the field,
  as a [`BattleSlotMask`](typed-wrappers.md):

  ```cpp
  ostinato::kJokerDoomTargets[0].targets.has(0);   // true — every character slot
  ```

## Item types in battle

```cpp
struct ItemTypeFlagsEntry { ItemType type; ItemTypeBattleFlags flags; };

inline constexpr std::array<ItemTypeFlagsEntry, 7> kItemTypeBattleFlags;

ItemTypeBattleFlags itemTypeBattleFlags(ItemType type);
```

An item's type contributes bits to its battle-usability state. The wrapper
exposes what the menu does with the byte: `mergedBits()` is what gets merged into
the item's flags, and `skipsEquippableCheck()` is the bit that short-circuits the
equippable-characters calculation.

```cpp
auto tool = ostinato::itemTypeBattleFlags(ItemType::TOOL);
tool.mergedBits();             // 0x40
tool.skipsEquippableCheck();   // true
```

The ROM table has an eighth row past `CONSUMABLE` with no item type; it is
padding and is not modeled.

## Spell order

```cpp
struct MagicOrderOffsetEntry { std::uint8_t setting; std::int8_t offset; };

inline constexpr std::array<MagicOrderOffsetEntry, 6> kBlackMagicOrder;
inline constexpr std::array<MagicOrderOffsetEntry, 6> kEffectMagicOrder;
inline constexpr std::array<MagicOrderOffsetEntry, 6> kWhiteMagicOrder;
```

The config menu's magic-order option chooses how the spell list is sorted. It has
no name of its own — the menu draws it as a numeral, so setting *N* is the order
the player sees as *N+1*.

Magic is banded by attack id (black `$00–$17`, effect `$18–$2C`, white
`$2D–$35`), and the offset is **added** to a spell's attack id to get its list
position, so a negative offset moves a whole band ahead of the others. Setting 2
is every offset zero, which leaves the bands in their natural order:

```cpp
ostinato::kWhiteMagicOrder[0].offset;   // -45 — white magic leads the list
ostinato::kBlackMagicOrder[0].offset;   //   9 — black magic follows it
```

## Desperation attacks

```cpp
struct DesperationAttackEntry { CharacterId character; AttackId attack; };

inline constexpr std::array<DesperationAttackEntry, 14> kDesperationAttacks;

AttackId desperationAttack(CharacterId character);
```

One desperation attack per character; Gau and Umaro have `AttackId::NONE`.
Nothing in the original game reads this table — the desperation attacks a player
sees are chosen elsewhere — so it ships as reference data rather than something
the engine consults.

## Backing data / where to change

Rows live in `src/data/generated/battle_formula_tables_data.inc`, `#include`d at
namespace scope in `src/data/battle_tables.h`. `AiScriptCommand` lives in
`include/ostinato/ai_script_command.h`. To retune a probability, edit its
`threshold`; to change what a dance does, edit its `background`; to rebalance the
evade bonuses, edit each `boost`. A deliberate change must also update the
matching entry in `tests/fixtures/battle_formula_tables_expected.h`, which holds
the original ROM values.

## What's tested

`tests/test_battle_formula_tables.cpp` — every modeled row of every table
compared against its fixture (no subsets), the unmodeled padding rows verified as
padding, the signed evade and spell-order offsets checked against their
two's-complement ROM bytes, and the accessors and wrapper decodes exercised.
