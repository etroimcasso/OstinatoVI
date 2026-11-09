# Battle AI

How a monster's AI script reads the battle: who a script names as its targets, and what its
conditions test before a block of the script acts. The scripts themselves — one per monster, a turn
part and a counterattack part, each a list of typed steps — are data, described in
[the AI scripts page](../data-layer/monster-ai-scripts.md).

Two free functions hold the rules. `aiTargets()` turns a script's target into the battlers it names;
`aiCondition()` tests one condition and aims the run as the condition does. Both read and change a
`BattleState` and nothing else, and both draw from the battle's random numbers where the game does.
`AiRun` carries what a run of a script keeps from one step to the next.

The battle does not run monster scripts: `CommandExecutor::runAi()` and `runCounterAi()` take no action
from them. A monster that is charmed, berserk, confused or fighting in the colosseum chooses its own
attack (`chooseMonsterAction()`, [commands.md](commands.md)), as in the game.

Both are checked against the cartridge's own routines, run on the engine's SNES machine over the same
battle.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_ai.md`](../../contracts/battle_ai.md).

## Files

| File | Holds |
|---|---|
| `src/battle/ai_targets.h` | `AiRun`, `aiTargets()` |
| `src/battle/ai_targets.cpp` | the battlers in the battle, one rule per selector from `SELF` on and the table that picks one |
| `src/battle/ai_conditions.h` | `aiCondition()` |
| `src/battle/ai_conditions.cpp` | the target test, one rule per condition |
| `src/battle/targeting.h` | `removeAbsentTargets()` and `reachableTargets()`, the cut to the battlers an attack can reach |
| `src/battle/battle_state.h` | `BattleState::battleVariables`, `monsterNamesListed`, `awayFromTheFieldUnreachable`; `Battler::aiVariable`, `turnTimer`, `targetingTarget`, `lastHit`, `lastAttacker`, `lastDamagedBy` |
| `src/data/ai_scripts.h` | `AiTarget`, `AiMonsters`, `AiVariable`, `AiSwitch`, `AiCondition` and the `ai::` condition types |
| `include/ostinato/ai_target_selector.h` | `AiTargetSelector`, the thirty selectors |
| `tools/damage-trace/damage_oracle.h` | `DamageOracle::aiTargets()` and `aiCondition()`, the cartridge's routines over an `AiScenario`; `portAiTargets()`, `portAiCondition()` and `differences()`; `drawAiScenario()`, `drawAiTargetsCase()`, `drawAiConditionCase()` |

## Who a script names

`aiTargets()` takes the battler whose script it is and an `AiTarget` — a party member by who it is, or
an `AiTargetSelector`:

```cpp
#include "battle/ai_targets.h"

const battle::BattlerSlot monster = battle::BattlerSlot::monster(0);

battle::AiRun run;
run.targets = battle::aiTargets(state, monster, AiTargetSelector::RAND_CHAR, true);
// one party member in reach, drawn at random; none, with no number drawn, when no one is in reach

const battle::BattlerSet banon = battle::aiTargets(state, monster, CharacterPropId::BANON, false);
// Banon's party slot, or no one when he is not in the party
```

A side — every monster, every party member, everyone else — is worked out from the battlers in the
battle: the party slots that hold a party member and the monster slots that hold a monster, on the
field or not. The last argument cuts the sides to the battlers an attack can reach: on the field, not
fallen or hidden, counted in the battle, not held in Seize, not made untargetable by a monster's AI, and
not away from the field (jumping, carried off by Palidor, held in Seize) — the last only while the last
command left them out of reach (`BattleState::awayFromTheFieldUnreachable`). A script's step that sets
the targets of its next attacks asks for the cut; its conditions do not.

| Selector | Names |
|---|---|
| a `CharacterPropId` | the party slot holding that party member — found whatever the cut |
| `MONSTER_SLOT_1` – `MONSTER_SLOT_6` | that slot, if it holds a monster — on the field or not, alive or not |
| `SELF` | the battler itself |
| `ALL_MONSTERS`, `ALL_OTHER_MONSTERS` | the monsters, or the monsters but itself |
| `RAND_MONSTER`, `RAND_OTHER_MONSTER` | one of them, drawn |
| `ALL_CHARS`, `RAND_CHAR` | the party members, or one of them |
| `ALL_TARGETS` | both sides |
| `RAND_TARGET` | everyone but itself: all of them on one face of a coin, one of them drawn on the other |
| `ALL_DEAD_CHARS`, `ALL_DEAD_MONSTERS` and the `RAND_DEAD_` pair | the slots whose statuses hold Dead — whatever the cut, and whether or not the slot holds anyone |
| `ALL_REFLECT_CHARS`, `ALL_REFLECT_MONSTERS` and the `RAND_REFLECT_` pair | the same for Reflect |
| `CHAR_SLOT_1` – `CHAR_SLOT_4` | that party slot, while it is on the field |
| `PREV_ATTACKER` | the battler that last aimed at it, unless it is still waiting to answer that battler |
| `TARGETTING_TARGET` | the battler Targetting marked, if it is on the field; the mark is used up |
| `NOTHING` | no one |

A drawn member is the next random number scaled by the count, counted from the first party member up;
a set with no one in it draws nothing.

## What a script tests

A block of a script opens with its conditions. `aiCondition()` tests one:

```cpp
#include "battle/ai_conditions.h"

battle::AiRun run;
const bool weak = battle::aiCondition(state, monster,
                                      ai::HpAtMost{.target = AiTargetSelector::SELF, .hp = 1280}, run);
// true while the monster has 1,280 HP or less; run.targets is then the monster itself

const bool hit = battle::aiCondition(state, monster,
                                     ai::HitByElement{ElementSet::of(Element::FIRE)}, run);
// true when its last hit had Fire in it; run.targets is then whoever made it
```

A condition that names a target runs **the target test** first: the battlers the target names, both
sides whole. When it names no one the condition fails. When it names more than one, the stats the
condition compares are read from the one in the highest slot — the sixth monster first, the first party
member last. A condition that holds leaves `run.targets` as the battlers it found, so the attacks after
it in the block are aimed at them.

| Condition | Holds when |
|---|---|
| `ai::Never` | never |
| `ai::HitByCommand`, `ai::HitByAttack`, `ai::HitByItem` | its last hit was made with one of the two commands, spells or items; the targets become whoever made it |
| `ai::HitByElement` | its last hit had one of the elements; the targets become whoever made it |
| `ai::Damaged` | an attack has taken HP or MP from it; the targets become whoever made it |
| `ai::HpAtMost`, `ai::MpAtMost` | the target's HP or MP is the value or less |
| `ai::HasStatus` | one of the battlers named has the status; one of them is drawn as the target |
| `ai::NoStatus` | none of them has it — or the target names no one |
| `ai::HalfOwnTimerAtLeast`, `ai::HalfBattleTimerAtLeast` | half its own timer, or half the battle's, is the value or more |
| `ai::VariableAtLeast`, `ai::VariableBelow` | a battle variable or its own is at least, or below, the value |
| `ai::SwitchSet`, `ai::SwitchClear` | a bit of a variable, or one of the game's battle switches, is set or clear |
| `ai::LevelAtLeast`, `ai::LevelBelow` | the target's level is at least, or below, the value; below also holds when the target names no one |
| `ai::OneKindOfMonster` | the list of monster names holds one name or none |
| `ai::MonstersAlive` | every monster named is alive |
| `ai::MonstersDead` | every monster named is in its slot and not alive |
| `ai::PartyAtLeast`, `ai::MonstersAtMost` | at least that many party members, or at most that many enemies, are still fighting |
| `ai::TargetValid` | the target names anyone |
| `ai::GauNotRecruited` | Gau has not joined the party |
| `ai::InSlot` | the monster stands in one of the slots |
| `ai::WeakTo` | the target is weak to one of the elements |
| `ai::FormationIs` | the formation being fought is that one |
| `ai::Always` | always |

`AiMonsters::self()` names the monster whose script it is; for a party member with a script it names
no monster, so `ai::MonstersAlive{AiMonsters::self()}` holds for it and `ai::InSlot` never does.

## The run and its restriction

`AiRun` is what a run of a script keeps from step to step: the targets its actions are aimed at, the
targets it started with, and whether it is restricted — the restriction a counterattack can run under,
in which only a block that `ai::MonstersDead` or `ai::Always` opens may act. Those two conditions lift
it, whatever their answer; no other condition touches it.

## The battle's AI state

| Field | What it is |
|---|---|
| `BattleState::battleVariables` | four variables the scripts of one battle share, 0 as it starts (`AiVariable::battle(0)` – `battle(3)`) |
| `Battler::aiVariable` | a battler's own variable (`AiVariable::own()`); its top bit is also set while it holds someone in Seize |
| `BattleState::switches` | the game's battle switches, which outlast the battle (`AiSwitch::battleSwitch()`) |
| `Battler::turnTimer`, `BattleState::battleCounter` | the two timers the half-timer conditions read |
| `BattleState::monsterNamesListed` | how many names the list of monster names holds, worked out with the run difficulty by `refreshRunDifficulty()` |
| `BattleState::awayFromTheFieldUnreachable` | whether the last command left the battlers away from the field out of reach: true unless it made an attack aimed at its own attacker; written at the end of `CommandExecutor::execute()` |
| `Battler::targetingTarget` | the battler a Targetting cast marked |

## Checking against the cartridge

`DamageOracle::aiTargets()` and `DamageOracle::aiCondition()` run the cartridge's own routines over an
`AiScenario` — the battle, the battler whose script it is, and the run so far — and
`portAiTargets()` and `portAiCondition()` run the port's. `differences()` names each field where they
part:

```cpp
#include "damage_oracle.h"

trace::runOnLoop([&] {
    trace::DamageOracle oracle{image};
    oracle.start();

    trace::AiScenario s;
    s.actor = battle::BattlerSlot::monster(0);
    s.state.at(s.actor).monster = MonsterId::LOBO;
    s.state.at(s.actor).hp = 1000;

    const ai::HpAtMost low{.target = AiTargetSelector::SELF, .hp = 1024};
    const std::string apart = trace::differences(trace::portAiCondition(s, low), oracle.aiCondition(s, low));  // ""
});
```

The condition is written into the cells the cartridge's script reader leaves a step in, and its
routine is found through the cartridge's own table of conditions. `drawAiTargetsCase()` and
`drawAiConditionCase()` draw battles from a seeded generator, and `ostinato-damage-trace --check` runs
them as `ai-targets` and `ai-conditions`.

## Gotchas

- **A test on the monster itself leaves the monster as the target.** The game means to clear the
  targets and clears only the party's half of them. A scripted party member's targets are left empty.
- **"At most" and "at least" include the value.** HP, MP, levels, variables, the party count and the
  monster count all compare with the value counted in.
- **The timers are compared at half.** `ai::HalfOwnTimerAtLeast{10}` holds from a timer of 20.
- **A status test draws a random number** whenever someone has the status, whether `ai::HasStatus`
  holds or `ai::NoStatus` fails.
- **A status from Dance on is looked for only among the battlers still fighting**; one before it is
  looked for among the dead too.
- **Reading the Targetting mark uses it up**, in a condition as in a target step.
- **The dead and the reflecting are found by status alone.** The cut does not reach them, and a status
  left in an empty slot counts.
- **`AiMonsters::all()` names the two slots past the sixth.** No monster is alive in them, so
  `ai::MonstersAlive{AiMonsters::all()}` never holds.
- **`PREV_ATTACKER` names no one while the monster is still to answer it.**

## Where to change

| To change | Edit |
|---|---|
| Who a selector names | its rule in `kSelectorRules` in `src/battle/ai_targets.cpp` |
| What the sides are | `battlersInTheBattle()` in `src/battle/ai_targets.cpp` |
| The cut to the reachable | `reachableTargets()` and `removeAbsentTargets()` in `src/battle/targeting.cpp` |
| What a condition tests | its `operator()` in `ConditionRules` in `src/battle/ai_conditions.cpp` |
| The target test | `ConditionRules::testTarget()` in `src/battle/ai_conditions.cpp` |
| The names counted | `refreshRunDifficulty()` in `src/battle/battle_loop.cpp` |
| The cells the oracle writes and reads for a script | `writeAiCells()`, `DamageOracle::aiTargets()` and `DamageOracle::aiCondition()` in `tools/damage-trace/damage_oracle.cpp` |
