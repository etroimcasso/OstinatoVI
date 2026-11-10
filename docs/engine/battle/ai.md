# Battle AI

How a monster's AI script runs in a battle: how a script is readied when its monster takes a slot,
how a turn and a counterattack read it and queue what it reaches, who a script names as its targets,
what its conditions test before a block of the script acts, and the three commands only a script
queues. The scripts themselves — one per monster, a turn part and a counterattack part, each a list of
typed steps — are data, described in [the AI scripts page](../data-layer/monster-ai-scripts.md).

Free functions hold the rules:

| Function | What it does |
|---|---|
| `readyAi()` | sets where a battler's two parts start, from `Battler::aiScript` |
| `runAiTurn()` | reads the turn part and queues what it reaches |
| `runAiCounter()` | reads the counterattack part, under the counterattack's guard |
| `aiTargets()` | turns a script's target into the battlers it names |
| `aiCondition()` | tests one condition and aims the run as the condition does |
| `changeBattleVariable()`, `changeBattleSwitch()`, `runAiEffect()` | the three commands a script queues, run when the battle reaches them |

They read and change a `BattleState` and nothing else, and draw from the battle's random numbers where
the game does. `AiRun` carries what a run of a script keeps from one step to the next.

The battle's own AI seams do not call the first three: `CommandExecutor::initAi()`, `runAi()` and
`runCounterAi()` take no action from a script, so a battle fought through `CommandExecutor` has its
monsters act only when they choose their own attack. A monster that is charmed, berserk, confused or
fighting in the colosseum chooses its own attack (`chooseMonsterAction()`, [commands.md](commands.md)),
as in the game.

All of it is checked against the cartridge's own routines, run on the engine's SNES machine over the
same battle, `SetStatus` and `ClearStatus` with the attack they make.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_ai.md`](../../contracts/battle_ai.md).

## Files

| File | Holds |
|---|---|
| `src/battle/ai.h` | `readyAi()`, `runAiTurn()`, `runAiCounter()` |
| `src/battle/ai.cpp` | a part's run, step by step (`PartRun`), and the counterattack's guard |
| `src/battle/action_queue.h` | the three actions only a script queues: `BattleVariableAction`, `BattleSwitchAction`, `AiEffectAction` |
| `src/battle/system_commands.h` | `changeBattleVariable()`, `changeBattleSwitch()`, `runAiEffect()` |
| `src/battle/system_commands.cpp` | the commands' bodies and `kAiEffectRules`, one row per effect |
| `src/battle/ai_targets.h` | `AiRun`, `aiTargets()` |
| `src/battle/ai_targets.cpp` | the battlers in the battle, one rule per selector from `SELF` on and the table that picks one |
| `src/battle/ai_conditions.h` | `aiCondition()` |
| `src/battle/ai_conditions.cpp` | the target test, one rule per condition |
| `src/battle/targeting.h` | `removeAbsentTargets()` and `reachableTargets()`, the cut to the battlers an attack can reach |
| `src/battle/battle_state.h` | `AiResume`, `kNoResume`; `BattleState::battleVariables`, `monsterNamesListed`, `awayFromTheFieldUnreachable`; `Battler::turnScript`, `counterScript`, `turnResume`, `counterResume`, `aiVariable`, `turnTimer`, `targetingTarget`, `lastHit`, `lastAttacker`, `lastDamagedBy` |
| `src/data/ai_scripts.h` | `AiTarget`, `AiMonsters`, `AiVariable`, `AiSwitch`, `AiCondition` and the `ai::` condition types |
| `include/ostinato/ai_target_selector.h` | `AiTargetSelector`, the thirty selectors |
| `tools/damage-trace/damage_oracle.h` | `DamageOracle::aiTargets()`, `aiCondition()`, `readyAi()`, `aiTurn()`, `nextAiTurn()`, `aiCounter()` and `aiCommand()`, the cartridge's routines over an `AiScenario`; the `port…` functions and `differences()`; `drawAiScenario()`, `drawAiTargetsCase()`, `drawAiConditionCase()`, `drawAiRunScenario()`, `drawAiCommandCase()` |

## Readying a script

`readyAi()` reads `Battler::aiScript` — a monster's own script, or the one a scripted battle gives a
party member — and sets where its two parts start:

```cpp
#include "battle/ai.h"

const battle::BattlerSlot monster = battle::BattlerSlot::monster(0);
state.at(monster).aiScript = static_cast<std::uint16_t>(MonsterId::LOBO);
battle::readyAi(state, monster);
// state.at(monster).turnScript is Lobo's turn part; Lobo has no counterattack part
```

A script with no counterattack part leaves the one the slot already holds, and nothing else is reset:
where each part stopped at a wait, the battler's own variable and its timer stay as the slot had them.
A monster that takes a slot in a formation change therefore keeps the old monster's script memory.

## A turn and a counterattack

`runAiTurn()` reads the battler's turn part with no targets. `runAiCounter()` reads its counterattack
part with the battler it answers as its targets — none for a battler answering its own death:

```cpp
battle::runAiTurn(state, executor, monster);
// what the turn part reached is on state.at(monster).actions

state.counterattackRunning = true;
battle::runAiCounter(state, executor, monster, battle::BattlerSet::of(battle::BattlerSlot::character(0)));
// while a counterattack runs, what it reaches goes on state.at(monster).counterActions
```

`executor` aims the battle commands a run queues (`ActionExecutor::aim()`); a `CommandExecutor` aims
them as the battle does.

How a part is read:

- **Every run starts at the top of the part.** A block's conditions are tested in order. A block whose
  condition fails is skipped to its `endIf()`, the run counts it as skipped and goes on after it; the
  part's `end()` after a failed block ends the run.
- **A block that acts goes on to its `endIf()`, which ends the run.** The next run starts again from the
  top. A condition after acting steps in the same block tests the battle as it is then — the actions
  queued before it have not happened yet.
- **`wait()` ends the run** and remembers the step after it and how many blocks the run had skipped
  (`Battler::turnResume`, `counterResume`). The next run reads from the top as always; when it comes to
  a step that acts having skipped that same number of blocks, it goes on from the remembered step
  instead, once.
- **The targets are the run's.** `setTarget()` and a condition that names a target change them; they
  stay for every later action of the run, and a skipped block puts back the ones the run started with.
- **An action is queued, not run.** An attack goes as the battle command it belongs to
  (`commandForAttack()`); one of three attacks, commands or items is drawn, and a choice of nothing
  queues nothing and the run goes on. A confused battler's commands go with no targets. Every battle
  command takes its advance wait and its aim as it is queued — given targets stand, none are chosen —
  except Lore's Roulette, which goes as the AI's Roulette with neither. The other steps queue their own
  actions with no wait: a formation change, a dialog, monsters shown or hidden, a battle event, a
  variable or switch changed, a monster animation, an effect.
- **While a counterattack runs**, everything a run queues is a counterattack (`addActionOrCounter()`).

### The counterattack's guard

A battler's counterattack part runs once a battle step (`BattleState::counterAiRanThisStep`). Its run
is **restricted** — only a block that `ai::MonstersDead` or `ai::Always` opens may act, and every other
block is skipped — when:

- it died this step (`BattleState::diedAwaitingCounter`) and no other battler attacked it this step
  (`attackedThisStep`);
- Quick is running for another battler;
- the statuses it answers with (`Battler::statusForCounter`) hold Sleep, Confuse, Berserk, Stop or
  Frozen;
- it is charmed.

A block that is skipped restricts the run again, even after one of the two conditions lifted it. This
is how a monster asleep, confused or charmed still makes the attack its script gives it as it dies. A
monster that chooses its own attack on its turn still runs its counterattack part.

## The three commands a script queues

A script's variable, switch and effect steps queue `BattleVariableAction`, `BattleSwitchAction` and
`AiEffectAction`. `CommandExecutor::execute()` runs each for the battler that queued it:

```cpp
#include "battle/system_commands.h"

state.battleVariables[0] = 255;
battle::changeBattleVariable(state, monster,
                             battle::BattleVariableAction{.variable = AiVariable::battle(0),
                                                          .change = AiVariableChange::Add,
                                                          .amount = 1});
// state.battleVariables[0] == 0: the sum stays within the variable's byte

battle::changeBattleSwitch(state, monster,
                           battle::BattleSwitchAction{.which = AiSwitch::battleSwitch(73),
                                                      .change = AiSwitchChange::Set});
// state.switches.isSet(73)
```

| Effect | What it does |
|---|---|
| `ResetOwnTimer`, `ResetBattleTimer` | `Battler::turnTimer` or `BattleState::battleCounter` back to 0 |
| `MakeInvincible`, `EndInvincible` | the target joins or leaves `BattleState::invincible` |
| `MakeUntargetable`, `MakeTargetable` | the target's monsters join or leave `BattleState::aiUntargetable` |
| `EndBattle`, `EndBattleLearningRages` | `BattleState::endOfBattleEvent` becomes `EndNow` or `RagesLearned` |
| `RecruitGau` | Gau is recruited (`gauRecruited`), and the battler's character record joins the party being played at the battler's slot, its row kept |
| `FillGauge` | if the target names anyone, the battler's own gauge fills at the next time update |
| `SetStatus`, `ClearStatus` | an attack by the battler on itself that puts the status on or takes it off, showing nothing |
| `HideSelf` | Hide goes straight onto the battler's statuses |
| `Nothing` | nothing; no script holds it |

An effect's target is found as a condition finds one: every battler in the battle, with no cut to
those in reach.

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
| `ai::HitByCommand`, `ai::HitByAttack`, `ai::HitByItem` | its last hit was made with one of the two commands, spells or items; the targets become whoever made it. A hit by one of the battle's own actions — a Poison beat, a spell with no caster, Seize's drain — was made with that action's own command number, not Fight ([commands.md](commands.md#the-battles-own-actions)) |
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
routine is found through the cartridge's own table of conditions.

A script's run is checked the same way. `DamageOracle::aiTurn()` and `aiCounter()` run the cartridge's
turn and counterattack over a scenario whose battlers' scripts are readied, and `portAiTurn()` and
`portAiCounter()` run the port's; the result holds the actions queued as the cartridge's lists hold
them, the advance wait, the script's places and the counterattack's sets. `nextAiTurn()` runs the
cartridge's next turn on its own state, so a turn after a turn is compared without the port's state
written back:

```cpp
trace::AiScenario s = trace::drawAiRunScenario(rng, static_cast<std::uint16_t>(MonsterId::LOBO),
                                               battle::BattlerSlot::monster(0));
battle::BattleState port = s.state;
const std::string first = trace::differences(trace::portAiTurn(port, s.actor), oracle.aiTurn(s));
const std::string second = trace::differences(trace::portAiTurn(port, s.actor), oracle.nextAiTurn(s.actor));
```

A place in a script is compared as the cartridge keeps it, a byte offset into its script block. A
script's readying is `DamageOracle::readyAi()` against `portScriptPlaces()`; the three commands are
`DamageOracle::aiCommand()` against `portAiCommand()`. `SetStatus` and `ClearStatus` make an attack,
which the comparison includes: the battle's cells after it and the script, as `DamageOracle::attack()`
reads them ([commands.md § Checking an attack against the cartridge](commands.md#checking-an-attack-against-the-cartridge)).

`drawAiTargetsCase()`, `drawAiConditionCase()`, `drawAiRunScenario()` and `drawAiCommandCase()` draw
battles from a seeded generator, and `ostinato-damage-trace --check` runs them as `ai-targets`,
`ai-conditions`, `ai-setup`, `ai-turns`, `ai-counters` and `ai-commands`.

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
- **The fourth Mag Roader answers attacks with Wild Cat's turn part.** Its script has no end to its
  counterattack part, so the game reads on into the next script; `readyAi()` gives it the same part.
- **A run goes on from a wait only when it has skipped the same number of blocks.** A run that skips
  more or fewer acts from wherever it is.
- **A formation change keeps the slot's script memory**, including a counterattack part the new
  monster's script does not have, and where the old monster's parts stopped at a wait.
- **`FillGauge` fills the gauge of the monster that asks**, not the target's; the target only decides
  whether.
- **A one-of-three step draws a random number even when it draws nothing.** A single attack draws none.
- **Show-or-hide with `AiMonsters::self()` names the monster itself**; for a party member it names no
  monster.

## The battle's script state

| Field | What it is |
|---|---|
| `Battler::turnScript`, `counterScript` | the two parts `readyAi()` set; `counterScript` empty for a battler that answers nothing |
| `Battler::hasCounterScript` | whether a counterattack part is set; the battle queues a counterattack for a battler with one |
| `Battler::turnResume`, `counterResume` | where each part stopped at a wait (`AiResume::at`) and the blocks it had skipped (`blocksSkipped`, `kNoResume` when it did not stop at one) |
| `BattleState::counterAiRanThisStep` | the battlers whose counterattack part ran this battle step |

## Where to change

| To change | Edit |
|---|---|
| How a part is read, step by step | `PartRun::read()` in `src/battle/ai.cpp` |
| What a step queues | its `PartRun::act()` overload in `src/battle/ai.cpp` |
| When a counterattack is restricted | `runAiCounter()` and `answersRestricted()` in `src/battle/ai.cpp` |
| Where a script's parts are set | `readyAi()` in `src/battle/ai.cpp` |
| What a variable or switch change does | `changeBattleVariable()`, `changeBattleSwitch()` in `src/battle/system_commands.cpp` |
| What an effect does | its rule in `kAiEffectRules` in `src/battle/system_commands.cpp` |
| Who a selector names | its rule in `kSelectorRules` in `src/battle/ai_targets.cpp` |
| What the sides are | `battlersInTheBattle()` in `src/battle/ai_targets.cpp` |
| The cut to the reachable | `reachableTargets()` and `removeAbsentTargets()` in `src/battle/targeting.cpp` |
| What a condition tests | its `operator()` in `ConditionRules` in `src/battle/ai_conditions.cpp` |
| The target test | `ConditionRules::testTarget()` in `src/battle/ai_conditions.cpp` |
| The names counted | `refreshRunDifficulty()` in `src/battle/battle_loop.cpp` |
| The cells the oracle writes and reads for a script | `writeAiCells()`, `writeAiRunCells()`, `readAiRunResult()` and `DamageOracle::aiCommand()` in `tools/damage-trace/damage_oracle.cpp` |
