# Battle espers

What an equipped esper does for a party member when a battle is won. The spells the esper teaches
are learned a little further, by the battle's magic points; and each level-up the battle's experience
brings adds the esper's level-up bonus — more HP or MP gained, or a stat raised by 1 or 2.

Both are free functions over the party's records, `game::CharacterRecord` and `game::SpellProgress`
(`src/game/character_record.h`): they read and write a record and a spell list and nothing of a battle
in progress. They hold the rules a won battle's end applies to each party member, after the battle's
experience and magic points are worked out; the battle loop does not call them, so a host that wins a
battle applies them itself.

Both are checked against the cartridge's own routines, run on the engine's SNES machine over the same
records.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_espers.md`](../../contracts/battle_espers.md).

## Files

| File | Holds |
|---|---|
| `src/battle/spell_learning.h` | `addSpellProgress()`, `learnFromEsper()` |
| `src/battle/leveling.h` | `levelUp()`, `applyEsperBonus()`, `LevelUpGains` |
| `src/battle/leveling.cpp` | the percentages and the stat limit as named constants, one rule per bonus and the table that picks one by the bonus |
| `src/game/character_record.h` | `SpellProgress::kSpellJustLearned` and `kSpellLearned`; `kMaxHp`, `kMaxMp` |
| `src/data/esper_properties.h` | `getEsperProperties()`: each esper's five spells with their learn rates, and its bonus |
| `src/data/level_up.h` | `levelUpHp()`, `levelUpMp()`: the HP and MP gained on reaching each level; `kMaxLevel` |
| `src/data/battle_magic_points.h` | `magicPointsForBattle()`: the magic points a battle gives |
| `tools/damage-trace/damage_oracle.h` | `DamageOracle::learnFromEsper()` and `levelUp()`, the cartridge's routines over a `RecordScenario`; `portLearnFromEsper()`, `portLevelUp()` and `differences()`; `drawLearningScenario()`, `drawLevelUpScenario()`; `DamageOracle::replay()` for a traced call of either routine |

## Learning spells

Each of the first twelve characters has a learned percentage for each of the 54 spells an esper or
an item can teach, Fire to Life 3: `Party::spells[actor]`. A won battle raises each spell the
character's esper teaches by its learn rate times the battle's magic points, kept to a byte:

```cpp
#include "battle/spell_learning.h"
#include "data/battle_magic_points.h"

game::SpellProgress& spells = party.spells[static_cast<std::size_t>(CharacterPropId::TERRA)];
const std::uint8_t magicPoints = magicPointsForBattle(battleIndex);

if (record.esper) {
    battle::learnFromEsper(spells, *record.esper, magicPoints);
}
// Ramuh with 5 magic points: Bolt +50, Bolt 2 +10, Poison +25.
```

A spell that reaches 100 % or more is learned, and holds `SpellProgress::kSpellJustLearned` (128)
until the battle shows its "learned" message, which makes it `kSpellLearned` (255). `knows()` is true
only at 255. A spell already learned is left as it is.

An item that teaches a spell raises it the same way, one spell at a time:

```cpp
battle::addSpellProgress(spells, AttackId::CURE, learnRate, magicPoints);
```

Both return whether a percentage was stored. A store counts even with 0 magic points, when the
percentage stays as it was; a rate of 0 or a spell already learned stores nothing.

## Leveling up

`levelUp()` is one level more for a record, with the record's esper's bonus:

```cpp
#include "battle/leveling.h"
#include "data/level_up.h"

game::CharacterRecord& record = party.characters[0];
if (record.level < kMaxLevel) {
    battle::levelUp(record, GameVersion::US_1_1);
}
// From level 69: level 70, maxHp + 162, maxMp + 8 — and with Bahamut equipped, maxHp + 243.
```

The level goes up first, and the gains are the new level's: `levelUpHp(level)` and
`levelUpMp(version, level)`. The MP curve depends on the release, so the call takes the
`GameVersion`. The bonus is applied to the gains, and the new maximums stop at `game::kMaxHp` (9,999)
and `game::kMaxMp` (999). Experience that reaches more than one level is one `levelUp()` per level.

`CharacterRecord::maxHp` and `maxMp` are the character's own maximums; equipment that raises them is
applied when a battle starts and is not part of the record.

## The esper's bonus

`applyEsperBonus()` applies one bonus to a level-up's gains or to the record:

```cpp
battle::LevelUpGains gains{.hp = 162, .mp = 8};
battle::applyEsperBonus(EsperBonus::HP_30, gains, record);
// gains.hp == 211: 162 × 78 / 256 = 49 more
```

| Bonus | What it does |
|---|---|
| `HP_10`, `HP_30`, `HP_50` | the HP gained raised by 26/256, 78/256 or 128/256 of itself, at least 1 |
| `MP_10`, `MP_30`, `MP_50` | the same for the MP gained |
| `HP_100` | the HP gained doubled |
| `STRENGTH_1`, `STRENGTH_2` | vigor +1 or +2 within its byte, at most 128 |
| `SPEED_1`, `SPEED_2` | speed +1 or +2 within its byte, at most 128 |
| `STAMINA_1`, `STAMINA_2` | stamina +1 or +2 within its byte, at most 128 |
| `MAGPWR_1`, `MAGPWR_2` | magic power +1 or +2 within its byte, at most 128 |
| `LV_30`, `LV_50`, `NONE` | nothing |

## Checking against the cartridge

`DamageOracle::learnFromEsper()` and `DamageOracle::levelUp()` run the cartridge's own routines over a
`RecordScenario`: the record is written at its place among the sixteen, the character's spells at
theirs, and the battle's magic points beside them. `portLearnFromEsper()` and `portLevelUp()` run the
port's, and `differences()` names each field where they part:

```cpp
#include "damage_oracle.h"

trace::runOnLoop([&] {
    trace::DamageOracle oracle{image};
    oracle.start();

    trace::RecordScenario s;
    s.recordIndex = 3;
    s.record.level = 69;
    s.record.maxHp = 1000;
    s.record.esper = EsperId::TERRATO;
    s.hpBoostBits = 3;

    const std::string apart = trace::differences(trace::portLevelUp(s), oracle.levelUp(s));   // ""
});
```

The cartridge keeps a record's HP and MP boosts from equipment in the top two bits of its maximum HP
and MP. `RecordScenario::hpBoostBits` and `mpBoostBits` write them there, and `LevelUpResult` reads
them back, so a check shows the level-up leaves them as they were. `drawLevelUpScenario()` and
`drawLearningScenario()` draw scenarios from a seeded generator, and `ostinato-damage-trace --check`
runs them as `level-up` and `esper-learning`.

The damage trace records every call of both routines in a played battle, with the cells each one read
and wrote, and `DamageOracle::replay()` works each call out again with the port. A level-up takes its
record from the record cells the call touched. The spells learned take the character from the spell
cells read, and the esper from which spells those are: each spell an esper teaches is read once,
whatever its percentage, and no two espers teach the same spells.

## Gotchas

- **The percentages are 256ths.** "+30%" adds 78/256 of the gain: 49 on a gain of 162, where 30 %
  would be 48.6.
- **A percentage bonus adds at least 1**, even to a gain too small for the fraction to reach 1.
  "+100%" has no such floor; every HP gain is 11 or more.
- **The gains are the new level's.** A level-up from 9 gains level 10's HP.
- **A spell learned twice in one win stays just learned.** An item and the esper can both teach it;
  the second store leaves it at 128.
- **0 magic points still count as progress.** `learnFromEsper()` returns true for an esper with a
  spell still to learn even when nothing moves; the battle shows no magic-points message for a battle
  that gives none.
- **Only the first twelve characters learn spells.** `Party::spells` holds Terra to Gau; Gogo and Umaro
  have no list, and the original's battle's end passes them over.
- **A level the story gives a character is not a level-up.** It sums the curves with no esper bonus;
  `levelUp()` is the battle's only.
- **A stat bonus adds within the stat's byte.** A stat of 255 + 1 or 254 + 2 comes round to 0, and
  255 + 2 to 1, as on the cartridge. No stat gets that high in play: the bonus stops at 128, and the
  only other write sets a record's stats from the character property records, none above 80.
- **The amount learned is the product's low byte.** A rate of 30 with 10 magic points adds 44, as on
  the cartridge. No esper rate times a battle's magic points passes 255 in play.
- **`levelUp()` at level 99 is a mistake.** The game never calls it there; check `kMaxLevel` first.
- **LV_30, LV_50 and HP_100 belong to no esper.** They work as listed, though no esper in the game has
  them.

## Where to change

| To change | Edit |
|---|---|
| When a spell counts as learned | `kLearnedPercent` in `src/battle/spell_learning.cpp` |
| How a spell is learned further | `addSpellProgress()` in `src/battle/spell_learning.cpp` |
| What one bonus does | its rule in `src/battle/leveling.cpp` |
| Which rule a bonus runs | its row in `kEsperBonusRules` in `src/battle/leveling.cpp` |
| The percentages, the stat limit | `kTenPercentOf256`, `kThirtyPercentOf256`, `kFiftyPercentOf256`, `kStatBonusMax` in `src/battle/leveling.cpp` |
| The HP and MP caps | `kMaxHp`, `kMaxMp` in `src/game/character_record.h` |
| An esper's spells, rates or bonus | the esper's row in `src/data/generated/genju_prop_data.inc`, regenerated by `tools/asm_parser/parse_genju_prop.py` |
| The cells the oracle writes and reads for a record | `writeRecordScenario()`, `readLevelUpResult()` and `readSpellLearningResult()` in `tools/damage-trace/damage_oracle.cpp` |
| How a traced call is rebuilt | `replayRecordCall()` in `tools/damage-trace/damage_oracle.cpp` |
