# Level progression

Everything that happens to a character as their level changes: the experience
each level costs, the HP and MP gained on reaching it, the levels Cyan's swdtechs
and Sabin's blitzes are learned at, and the level a character joins the party at.

```cpp
#include "data/level_up.h"
```

Levels run from 1 to 99. Nothing is gained on reaching level 1, so the
progression tables start at level 2 and hold 98 rows each:

```cpp
inline constexpr std::uint8_t kFirstProgressionLevel = 2;
inline constexpr std::uint8_t kMaxLevel = 99;
```

Every progression row is keyed by the level it applies to, so a row reads as "at
level N, gain this much".

## Experience

```cpp
struct LevelUpExpEntry { std::uint8_t level; std::uint16_t exp; };

inline constexpr std::array<LevelUpExpEntry, 98> kLevelUpExp;

std::uint16_t levelUpExp(std::uint8_t level);
```

`exp` is the *step* from the previous level to this one, not a running total. The
total experience a level requires is the sum of every step below it, multiplied
by 8 — that is how the game seeds a joining character's experience:

```cpp
ostinato::levelUpExp(2);    // 4
ostinato::levelUpExp(98);   // 9603
ostinato::levelUpExp(99);   // 11111
```

The level-99 step is a visible outlier against the smooth ramp below it (the
neighbouring steps climb by roughly 200). It is the original value and is carried
as-is.

## HP and MP

```cpp
struct LevelUpStatEntry { std::uint8_t level; std::uint8_t gain; };

inline constexpr std::array<LevelUpStatEntry, 98> kLevelUpHp;
inline constexpr std::array<LevelUpStatEntry, 98> kLevelUpMpEn;
inline constexpr std::array<LevelUpStatEntry, 98> kLevelUpMpJp;

std::uint8_t levelUpHp(std::uint8_t level);
std::uint8_t levelUpMp(GameVersion version, std::uint8_t level);
std::span<const LevelUpStatEntry> levelUpMp(GameVersion version);
```

A character's maximum HP is their base HP plus every `kLevelUpHp` gain up to their
level; maximum MP works the same way against the MP curve.

The MP curve is the one table in the data layer that differs by release — the
Japanese version levels magic users on a different curve — so it is selected by
[`GameVersion`](foundational-enums.md) rather than being a single table:

```cpp
ostinato::levelUpMp(GameVersion::US_1_0, 2);   // 4
ostinato::levelUpMp(GameVersion::JP_1_0, 2);   // 5
```

The two curves agree at some levels and diverge at others, so test a whole curve
rather than a sampled level when comparing them. The no-argument-level form
returns the whole curve for a version, in level order.

## Swdtech and blitz

```cpp
struct AbilityLearnLevelEntry { AttackId ability; std::uint8_t level; };

inline constexpr std::array<AbilityLearnLevelEntry, 8> kBushidoLearnLevels;
inline constexpr std::array<AbilityLearnLevelEntry, 8> kBlitzLearnLevels;

std::span<const AbilityLearnLevelEntry> abilityLearnLevels(BattleCommandId command);
```

Each command's eight abilities are consecutive attacks counting up from that
command's base attack, so every row names the ability it gates:

```cpp
ostinato::kBushidoLearnLevels.front().ability;   // AttackId::DISPATCH, at level 1
ostinato::kBlitzLearnLevels.back().ability;      // AttackId::BUM_RUSH, at level 70

ostinato::abilityLearnLevels(BattleCommandId::BUSHIDO);
```

`abilityLearnLevels` accepts only `BUSHIDO` and `BLITZ` — they are the two
commands whose abilities are level-gated.

```cpp
struct LearnedAbilityFlagsEntry { std::uint8_t learnedCount; AbilityLearnedSet abilities; };

inline constexpr std::array<LearnedAbilityFlagsEntry, 9> kLearnedAbilityFlags;
```

Abilities are learned in order, so the set of abilities a character has is fully
determined by *how many* they have reached the level for. This table maps that
count to the [`AbilityLearnedSet`](typed-wrappers.md) it implies — row *N* has the
low *N* bits set:

```cpp
ostinato::kLearnedAbilityFlags[3].abilities.has(2);    // true
ostinato::kLearnedAbilityFlags[3].abilities.count();   // 3
```

## Joining level

```cpp
struct CharacterLevelModifierEntry { LevelMod mod; std::int8_t levels; };

inline constexpr std::array<CharacterLevelModifierEntry, 4> kCharacterLevelModifiers;

std::int8_t characterLevelModifier(LevelMod mod);
```

A character who joins the party arrives at the party's average level plus their
own offset, clamped to 1–99. The offset is signed, so a `LOW` character joins
three levels behind:

```cpp
ostinato::characterLevelModifier(LevelMod::NORMAL);      //  0
ostinato::characterLevelModifier(LevelMod::VERY_HIGH);   //  5
ostinato::characterLevelModifier(LevelMod::LOW);         // -3
```

The setting is stored inside a character record's field alongside other bits;
mask and shift it to a `LevelMod` before calling.

## Backing data / where to change

Rows live in `src/data/generated/level_up_data.inc`, `#include`d at namespace
scope in `src/data/level_up.h`. To make levelling faster, edit the `exp` steps; to
change a character's growth, edit the `gain` columns; to move an ability earlier,
edit its `level`. A deliberate change must also update the matching entry in
`tests/fixtures/level_up_expected.h`, which holds the original ROM values.

## What's tested

`tests/test_battle_formula_tables.cpp` — all 98 rows of the experience, HP, and
both MP curves compared against their fixtures (no subsets), the level-99
experience outlier pinned, the version selection checked in both directions, the
ability tables verified as named and in ascending order, the learned-ability ramp
checked against its bit counts, and each level modifier decoded.
