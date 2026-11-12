# Monster AI scripts

What every monster does on its turn and how it answers an attack, as lists of
typed steps. There is one script for each of the 384 monsters; a scripted
battle can also give a party member one of the scripts from 256 up
([character-ai.md](character-ai.md)).

This page covers the scripts as data: the step types, the table, and the
generated rows.

## A script

A script has two parts. Each is a list of steps ending in `AiStep::end()`.

| Part | When it is read |
|---|---|
| `turn` | On the monster's turn |
| `counter` | When the monster is attacked. Empty for a monster that answers nothing — 174 of the 384 |

A step is one of four things:

- an **action** — use an attack, show a dialog, change a variable;
- a **condition** — `ifHpAtMost`, `ifHitByCommand`, …;
- `AiStep::wait()` — the turn ends here and the next turn goes on from the
  step after it;
- `AiStep::endIf()` — the end of a block.

Conditions and the steps after them, up to the next `endIf()`, form a block:
the block's steps are for when every one of its conditions holds. A condition
can also stand between two steps of a block, and then governs the steps after
it.

```cpp
constexpr AiStep kLoboTurn[] = {
    AiStep::ifLevelAtLeast({ .target = AiTargetSelector::RAND_CHAR, .level = 7 }),
    AiStep::attackOneOf({ AttackId::BATTLE, AttackId::SPECIAL, kNoAttack }),
    AiStep::attackOneOf({ AttackId::SPECIAL, kNoAttack, kNoAttack }),
    AiStep::endIf(),
    AiStep::attack(AttackId::BATTLE),
    AiStep::end(),
};
```

Read as: for a party member of level 7 or more, one of Battle, Special or
nothing with equal odds, then Special one time in three; otherwise Battle.

## The surface

```cpp
#include "data/ai_scripts.h"

const AiScript& script = ostinato::aiScript(MonsterId::REPO_MAN);

script.monster;        // MonsterId::REPO_MAN
script.turn;           // std::span<const AiStep>
script.hasCounter();   // true
script.counter;        // the steps it answers an attack with

for (const AiStep& step : script.turn) {
    if (const ai::Attack* attack = step.as<ai::Attack>()) {
        // attack->attack
    } else if (const ai::AttackOneOf* oneOf = step.as<ai::AttackOneOf>()) {
        // oneOf->choices: three std::optional<AttackId>
    } else if (const ai::HpAtMost* hp = step.condition<ai::HpAtMost>()) {
        // hp->target, hp->hp
    } else if (step.isCondition()) {
        // another condition
    }
}
```

`aiScript(std::uint16_t)` takes a script's index directly, which is how a
scripted party member's script is found. `aiScripts()` returns all 384 in
`MonsterId` order.

| Type | Header | What it is |
|---|---|---|
| `AiScript` | `data/ai_scripts.h` | One monster's `turn` and `counter` parts |
| `AiStep` | `data/ai_scripts.h` | One step, with a builder for every kind |
| `ai::Attack`, `ai::HpAtMost`, … | `data/ai_scripts.h` | What each kind of step holds |
| `AiCondition` | `data/ai_scripts.h` | Any one of the 29 conditions |
| `AiTarget` | `data/ai_scripts.h` | A `CharacterPropId` or an `AiTargetSelector` |
| `AiTargetSelector` | `ostinato/ai_target_selector.h` | A slot, a side, a random member of a side, the last attacker |
| `AiMonsters` | `data/ai_scripts.h` | A set of the six monster slots, or the monster itself |
| `AiVariable` | `data/ai_scripts.h` | One of the battle's four variables, or the monster's own |
| `AiSwitch` | `data/ai_scripts.h` | One bit of a variable, or one of the game's battle switches |
| `AiEffect` | `data/ai_scripts.h` | The effects a script asks for besides attacks |
| `MonsterAnimation` | `ostinato/monster_animation.h` | The animations a script plays |
| `MonsterVisibilityMode` | `ostinato/monster_visibility_mode.h` | What showing or hiding monsters does to them |

## Actions

| Builder | What the step says |
|---|---|
| `attack(AttackId)` | Use this attack. Nothing is drawn. |
| `attackOneOf({ a, b, c })` | Use one of three, each as likely as the others. `kNoAttack` is a choice that does nothing. |
| `commandOneOf({ a, b, c })` | The same for battle commands, with `kNoCommand`. |
| `useItemOneOf({ .twoInThree, .oneInThree })` | Use an item: the first two times in three, the second the third. `kNoItem` does nothing. |
| `throwItemOneOf({ .twoInThree, .oneInThree })` | Throw one, by the same odds. |
| `setTarget(AiTarget)` | Aim the actions that follow. |
| `dialog(index)` | The monster says monster dialog `index`. |
| `battleEvent(index)` | Battle event `index` plays. |
| `changeBattle({ .formation, .exit, .restoreMonsters, .scrollBackground })` | The battle moves to another formation. |
| `showOrHideMonsters({ .mode, .monsters, .type })` | Monsters come onto the field or leave it. |
| `animate({ .animation, .monsters })` | An animation plays on monsters. |
| `partyRunsLeftToRight()`, `partyRunsRightToLeft()` | The party runs across the screen. |
| `soundEffect({ .sound, .pan })` | A sound effect plays. |
| `setVariable({ .variable, .value })` | A variable becomes `value`. |
| `addToVariable({ .variable, .amount })`, `subtractFromVariable(…)` | A variable goes up or down by `amount`. |
| `toggleSwitch(AiSwitch)`, `setSwitch(…)`, `clearSwitch(…)` | A switch changes. |
| `resetOwnTimer()`, `resetBattleTimer()` | A timer starts again from 0. |
| `makeInvincible(target)`, `endInvincible(target)` | Damage to the target is not taken, or is taken again. |
| `makeTargetable(target)`, `makeUntargetable(target)` | The target can or cannot be aimed at. |
| `fillGauge(target)` | The monster's gauge fills, if the target is there. |
| `setStatus(StatusId)`, `clearStatus(StatusId)` | The monster gives itself a status or takes one off. |
| `hideSelf()` | The monster hides. |
| `recruitGau()` | Gau joins the party. |
| `endBattle()`, `endBattleLearningRages()` | The battle ends at once; with the second, Gau learns the rages. |

## Conditions

| Builder | Holds when |
|---|---|
| `ifHitByCommand(a)` or `(a, b)` | The monster was last hit with battle command `a` or `b` |
| `ifHitByAttack(a)` or `(a, b)` | The same for the last spell or skill |
| `ifHitByItem(a)` or `(a, b)` | The same for the last item |
| `ifHitByElement(ElementSet)` | The attack it was last hit with has one of the elements |
| `ifDamaged()` | Some attack has taken HP or MP from it |
| `ifHpAtMost({ .target, .hp })` | The target's HP is `hp` or less |
| `ifMpAtMost({ .target, .mp })` | The target's MP is `mp` or less |
| `ifStatus({ .target, .status })` | One of the battlers the target names has the status |
| `ifNoStatus({ .target, .status })` | None of them has it |
| `ifLevelAtLeast({ .target, .level })`, `ifLevelBelow(…)` | The target's level compared with `level` |
| `ifWeakTo({ .target, .elements })` | The target is weak to one of the elements |
| `ifTargetValid(target)` | The target names at least one battler |
| `ifVariableAtLeast({ .variable, .value })`, `ifVariableBelow(…)` | A variable compared with `value` |
| `ifSwitchSet(AiSwitch)`, `ifSwitchClear(AiSwitch)` | A switch's state |
| `ifHalfOwnTimerAtLeast(value)` | Half the monster's own timer is `value` or more |
| `ifHalfBattleTimerAtLeast(value)` | Half the battle's timer is `value` or more |
| `ifMonstersAlive(AiMonsters)` | Every one of the monsters is alive |
| `ifMonstersDead(AiMonsters)` | Every one of them is a monster of the formation that is no longer alive |
| `ifInSlot(AiMonsters)` | The monster stands in one of the slots |
| `ifPartyAtLeast(count)` | At least `count` party members are still fighting |
| `ifMonstersAtMost(count)` | At most `count` monsters are still fighting |
| `ifOneKindOfMonster()` | The list of monster names holds one name or none |
| `ifFormation(FormationId)` | The formation being fought is this one |
| `ifGauNotRecruited()` | Gau is not one of the characters the party can field |
| `always()`, `never()` | Always; never |

## Naming who and what

**Targets.** A step's target is one party member by who it is, or a selector:

```cpp
AiStep::setTarget(CharacterPropId::BANON)
AiStep::setTarget(AiTargetSelector::RAND_CHAR)
AiStep::ifStatus({ .target = AiTargetSelector::CHAR_SLOT_2, .status = StatusId::REFLECT })
```

**Monsters.** Steps that act on monster slots take an `AiMonsters`:

```cpp
AiMonsters::of(AiTargetSelector::MONSTER_SLOT_2, AiTargetSelector::MONSTER_SLOT_4)
AiMonsters::self()   // the monster the script belongs to
AiMonsters::all()    // every slot
```

`AiMonsters::of()` takes `MONSTER_SLOT_1` to `MONSTER_SLOT_6`; any other
selector does not compile.

**Variables and switches.** A battle keeps four variables every script in it
shares, and every monster has one of its own. All start at 0.

```cpp
AiVariable::battle(3)             // the battle's fourth variable
AiVariable::own()                 // the monster's own

AiSwitch::own(0)                  // bit 0 of the monster's own variable
AiSwitch::battleVariable(3, 1)    // bit 1 of battle variable 3
AiSwitch::battleSwitch(73)        // the game's battle switch 73
```

A battle switch is one of the numbered switches of `game::BattleSwitches`
(`src/game/battle_switches.h`), which outlast the battle. Gau's script on the
Veldt tests and sets switch 73, the one that records that Gau has joined.

## Changing a script

A script's steps are in `src/data/generated/ai_script_steps_data.inc`, one
array per part named for the monster — `kLoboTurn`, `kRepoManCounter`. Edit the
steps there:

```cpp
constexpr AiStep kRepoManCounter[] = {
    AiStep::ifDamaged(),
    AiStep::setTarget(AiTargetSelector::SELF),
    AiStep::attackOneOf({ AttackId::SPECIAL, kNoAttack, kNoAttack }),
    AiStep::end(),
};
```

Every part ends with exactly one `AiStep::end()`, as its last step.

To give a monster a counter part it does not have, add the array and name it
in the monster's row of `src/data/generated/ai_scripts_data.inc`:

```cpp
{ .monster = MonsterId::LOBO, .turn = kLoboTurn, .counter = kLoboCounter },
```

`.counter = {}` is a monster that answers nothing.

`ostinato-vi-ai-scripts-tests` turns every script back into the bytes the
cartridge holds and compares them, so a changed script fails
`EveryScriptsBytesAreTheCartridges` at the changed step. Update
`tests/fixtures/ai_scripts_expected.h` with the part's new step count.

To regenerate both files and the fixture from the game's script source:

```sh
python3 tools/asm_parser/parse_ai_scripts.py --source-root original-src
```

## Gotchas

- **Two steps use one attack.** `attack(a)` draws nothing. `attackOneOf({ a, a,
  a })` always uses `a` too, but draws a random number first. The game writes
  an attack from `AttackId::RIOT_BLADE` up in the second form even when it is
  the only choice, and the scripts keep that.
- **"At most" and "at least" include the number.** `ifHpAtMost({ .hp = 1280 })`
  holds at exactly 1,280 HP, and `ifVariableAtLeast({ .value = 3 })` at exactly 3.
- **An HP is a multiple of 128.** `ifHpAtMost` does not compile with any other.
- **The timer conditions compare half the timer.** `ifHalfOwnTimerAtLeast(30)`
  holds once the monster's timer reaches 60.
- **A variable step's amount is at most 63.** A larger one does not compile.
- **Mag Roader's counter part is Wild Cat's turn part.** The fourth Mag Roader's
  row names `kWildCatTurn` as its counter. The game's script for it has no end
  to its counter part, so the game reads on into the next script; the table
  says so outright.
- **`ItemId::DRIED_MEAT` cannot be an item choice.** In the cartridge's scripts
  its value is the one that stands for no choice, so the comparison with the
  cartridge reads it as `kNoItem`. No script uses or throws it.
- **A step is read through its kind.** `step.as<ai::Attack>()` is null for any
  other step; a condition is read with `step.condition<ai::HpAtMost>()`.

## Where to change things

| Change | File |
|---|---|
| A script's steps | `src/data/generated/ai_script_steps_data.inc` |
| Which parts a monster has | `src/data/generated/ai_scripts_data.inc` |
| A step's type or its builder | `src/data/ai_scripts.h` |
| The target selectors | `include/ostinato/ai_target_selector.h` |
| The lookups | `src/data/ai_scripts.cpp` |
| Where the cartridge holds each part | `tests/fixtures/ai_scripts_expected.h` |

## See also

- [character-ai.md](character-ai.md) — the scripted battles that give a party
  member a script
- [monster-properties.md](monster-properties.md) — the monsters the scripts
  belong to
- [battle-tables.md](battle-tables.md) — the command an attack is used through
- [attack-properties.md](attack-properties.md) — the attacks the steps name
