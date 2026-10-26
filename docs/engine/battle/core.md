# The battle core

The battle core decides **when** things happen in a battle: whose gauge fills, who waits, whose
turn it is, what answers an attack, when a status runs out, when the battle is over. It does not
decide **what** a command does — that is handed to an `ActionExecutor` — and it draws nothing:
whatever needs showing becomes a `BattleRequest` for the presentation.

The behavior it reproduces is specified in
[`docs/contracts/battle_core.md`](../../contracts/battle_core.md); this page is about using and
changing the code.

## Files

| File | Holds |
|---|---|
| `src/battle/battle_sim.h` | `BattleSim`, `BattleInput`: a whole battle from a frame tick |
| `src/battle/battle_core.h` | `tickBattleTime()`, `stepBattle()`, `recalcAtbRate()`, `refreshAliveSets()`, `refreshRunDifficulty()`, `checkTheFallen()`, `settleAfterAction()` |
| `src/battle/battle_state.h` | `BattleState`, `Battler`, `AtbGauge`, `PendingUpdates`, `BattlerTriggers`, `BattleConfig`, `BattleOutcome` and its parts |
| `src/battle/battler_set.h` | `BattlerSlot`, `BattlerSet` |
| `src/battle/action_queue.h` | `Action` and its alternatives, `BattlerQueue` |
| `src/battle/actions.h` | adding and removing actions, MP cost, advance wait |
| `src/battle/action_executor.h` | `ActionExecutor`, `NullExecutor` |
| `src/battle/battle_requests.h` | `BattleRequest` |
| `src/battle/battle_script.h` | `BattleScriptCommand`, `BattleScriptBuilder` ([commands.md](commands.md)) |
| `src/battle/battle_rng.h` | `BattleRng` |
| `src/battle/player_intake.h` | `PlayerAction`, `takePlayerActions()` |
| `src/battle/battle_clock.cpp` | battle time |
| `src/battle/battle_loop.cpp` | the battle loop |

How a battle is built before its first frame — the party, the monsters, the battle type, the
starting gauges — and what the game keeps when it ends are in [start.md](start.md).

## Driving a battle

`BattleSim` runs a battle from start to finish off the host's frame tick. Each frame, pass what the
presentation knows and take the requests:

```cpp
#include "battle/battle_sim.h"

using namespace ostinato;
using namespace ostinato::battle;

BattleSim battle(start, party, switches, cartridge.text, executor);
while (!battle.result()) {
    battle.tick({.timeRunning = presentation.timeRuns(),
                 .presentationIdle = !presentation.playingScript(),
                 .playerActions = menu.finishedActions()});
    for (const BattleRequest& request : battle.takeRequests()) {
        presentation.handle(request);
    }
}
party = battle.result()->party;
```

`tick()` runs battle time every frame and one loop step on a frame the presentation is idle. When the
battle has an outcome it plays out the end through the executor, hands the party back
(`result()`, a `BattleResult`) and requests `CloseBattleScene`. A won stage of the final battle
requests `ScrollFinalBattleBackground` and carries straight on into the next stage instead. After the
result is set, `tick()` does nothing.

A host can also drive the two clocks itself. Battle time runs on every displayed frame; the loop
steps only while no battle script is playing, because each script it starts is something the
original waited on.

```cpp
#include "battle/battle_core.h"

using namespace ostinato::battle;

void runBattleFrame(BattleState& state, ActionExecutor& executor, Presentation& presentation) {
    tickBattleTime(state, presentation.timeRuns(), executor);
    if (!presentation.playingScript()) {
        if (const auto outcome = stepBattle(state, executor)) {
            presentation.endBattle(*outcome);
        }
    }
    for (const BattleRequest& request : state.requests) {
        presentation.handle(request);  // a RunBattleScript starts a script playing
    }
    state.requests.clear();
}
```

`Presentation` stands for whatever plays scripts and opens menus. It reports two things back:
whether time runs this frame (it stops time during a battle event or the entry animation) and
whether a script is still playing. A step that starts a script returns with
`state.waitingForScript` set; the next `stepBattle()` after the script has played carries on from
there. Calling `stepBattle()` again while a script is still playing would finish the action before
the player saw it.

Within a frame, call `tickBattleTime()` first and `stepBattle()` second. The two clocks share the
random numbers, and that order keeps their draws in the original's sequence.

## Battle slots and sets

`BattlerSlot` names one of the ten places: `BattlerSlot::character(0..3)` and
`BattlerSlot::monster(0..5)`, with `index()` 0-9 (the party first), `isCharacter()` and `position()`
within its side. `state.at(slot)` is that slot's `Battler`.

`BattlerSet` is a set of slots — `of(slot)`, `has`, `add`, `remove`, `count`, `empty`, `|`, `&`,
`without`, `charactersOnly()`, `monstersOnly()`, `allCharacters()`, `allMonsters()`. It is two bytes,
the party's then the monsters', each a `BattleSlotMask`.

```cpp
BattlerSet targets = BattlerSet::of(BattlerSlot::monster(0)) | BattlerSet::of(BattlerSlot::monster(2));
targets.count();                         // 2
targets.has(BattlerSlot::monster(1));    // false
```

## The battler

The fields of `Battler` group as follows.

| Group | Fields |
|---|---|
| Whose turn it is | `present`, `takesMenuInput`, `inBattleMenu`, `atbStopped`, `heldByStatus`, `heldByControl`, `actionQueued`, `menuMayOpen`, `skipAdvanceWait`, `defending`, `dotActionPending`, `emptyGaugeAfterAction`, `requeuePending`, `pendingUpdates`, `nearFatalSpellAvailable`, `airAnchored`, `mayActAfterJump` |
| The clocks | `gauge`, `atbRate`, `advanceWait`, `advanceWaitCounter`, `statusTimer`, `statusTimerStep`, `stopTimer`, `reflectTimer`, `freezeTimer`, `sleepTimer`, `condemnedNumber`, `damageOverTimePhase`, `morphGauge`, `turnTimer`, `runProgress`, `runFactor` |
| Who it is | `actor`, `record`, `graphics`, `level`, `hp`, `maxHp`, `mp`, `maxMp`, `speed`, `vigor`, `stamina`, `magicPower`, `defense`, `magicDefense`, `evadeFactor`, `magicBlockFactor`, `status`, `innateStatuses`, `equipmentStatuses`, `inflictableStatuses`, `instantDeathProtected`, `backRow`, `human`, `undead`, `triggers`, `actsAsEnemy`, `aiTakesTurns`, `hasCounterScript`, `aiScript` |
| A monster's record | `monster`, `monsterFlags` (first strike, harder to run from, can't run, the immunities), `diesAtZeroMp`, `impCritical`, `nameHidden`, `nameGroup`, `rareSteal`, `commonSteal`, `experience`, `gold`, `metamorph`, `specialAttack`, `specialAttackAnimation` |
| A party member's equipment | `weaponHand`, `shieldHand`, `relics`, the four element sets, `physicalBlockGraphics`, `magicBlockGraphics`, `relicEffects1`-`5`, `mayHoldTwoWeapons`, `equipsAs`, `equipmentChanged`, `commands`, `spells`, `spellCount` |
| What its command took up | `itemInHand`, `dance`, `rage` |
| Links | `controlledBy`, `charmedBy`, `seizing`, `seizedBy`, `lastAttacker`, `retaliationPending`, `bodyguard` (who takes its hits for a Love Token) |
| What the action being run does to it | `statusesToSet`, `statusesToClear`, `lastHit` (who last hit it, with which command, spell, item and elements) |
| What it has waiting | `actions`, `counterActions` |

`BattleState` holds the ten battlers and the battle as a whole: the lines (`advanceWaitQueue`,
`actionQueue`, `counterQueue`, `immediateActions`), who is mid-turn (`actingBattler`,
`counteringBattler`), Quick (`quickTarget`, `quickTurnsLeft`), the clocks, the sets of who is
alive, joining, leaving, standing and fallen, the battle's flags, the story facts its end depends on,
the action's report (`scratch`), Mimic's memory (`mimic`), the player's finished commands waiting
to come in (`chosenActions`), the requests, and the outcome. It also holds the battle's copy of the
party (`party`) and of the battle switches (`switches`), the battle inventory, what the party knows,
the formation and the one the battle began with, the colosseum match, Doom Gaze's slot and the
characters Possess has taken ([start.md](start.md)). What the commands use up or learn is there too:
the desperation attacks used, the lores Strago saw, the last rager and the Golem's HP; and so are
the monsters a monster's AI has hidden from aiming and which way each battler faces, which aiming
reads ([targeting.md](targeting.md)).

## The ATB gauge

`AtbGauge::counter` is 16 bits; its high byte is the meter. 1 is empty and the meter climbs toward
255; when the counter passes `$FFFF` the meter reads 0 — full. `fillLevel()` is what the display
shows, 0 (empty) to 255 (full).

| Member | Does |
|---|---|
| `meter()` | the high byte |
| `isFull()` | the meter reads 0 |
| `fill(rate)` | adds one update's fill; returns true when that fills it, keeping the low byte |
| `empty()` | meter to 1, keeping the low byte |
| `setNearlyFull()` | meter to 255: it fills at the next update |
| `stepBackAfterTurn()` | after a turn: a full gauge starts again from empty; any other moves one step on, unless one step from full |

Each update fills it by the battler's `atbRate`. `recalcAtbRate(state, slot)` works that out from
`speed` and Haste or Slow, and sets the status-timer step at the same time:

```cpp
battler.speed = 40;
recalcAtbRate(state, slot);   // a character: atbRate 360, statusTimerStep 64
```

At speed 40 a character's empty gauge fills in 182 updates, 364 frames. A monster's rate is scaled
by `state.config.speedSetting` (`monsterSpeedFactor()`); a character's is not.

## The advance wait

When a battler's gauge fills, its action does not run at once: the battler joins the advance-wait
line, its turn is set up, and its advance wait runs — half its ATB rate per update into
`advanceWaitCounter` — until the counter's high byte reaches `advanceWait`. Then it joins the action
line. Each command adds its own wait from the command table when its turn is set up
(`addAdvanceWait()`); a wait stops at 254, and one not yet set counts as 255.

## The status timers

Once every 16 time updates each battler's timers get a turn: `statusTimer` adds `statusTimerStep`
(32 under Slow, 64, 84 under Haste) and only when that passes 255 do the timers below it move — so
Haste makes statuses run out sooner. Stop counts down alone while it runs; otherwise the condemned
count, the escape try, the Reflect, Freeze and Sleep timers, and the poison and regen beat take their
step. The remaining six of the 16 turns are the battle's: Roulette's Doom, the battle counter and the
battlers' own timers, and the party's escape.

## Actions and the lines

An `Action` is one of: `CommandAction` (a battle command with its attack, targets and MP cost),
`AiTurn`, `DamageOverTime`, `SpellWithoutCaster`, `StatusTimersExpired`, `PartyEscape`,
`ResetBattlePose`, `SeizeDrain`, `BattleEventAction`, `BattleMessageAction` (the battle's opening
message), and `NoAction` (handed to the executor when a battler's turn comes with nothing chosen).

```cpp
#include "battle/actions.h"

addAction(state, slot, CommandAction{.command = BattleCommandId::FIGHT,
                                     .targets = BattlerSet::of(BattlerSlot::monster(0))});
addCounterAction(state, slot, AiTurn{});                    // joins the counter line
addImmediateAction(state, PartyEscape{});                   // runs before anything else
addAutomaticAction(state, executor, slot, CommandAction{.command = BattleCommandId::FIGHT});
removeAllActions(state, slot);
```

| Function | Puts the action |
|---|---|
| `addAction()` | at the end of the battler's list |
| `addCounterAction()` | on its counterattack list; a battler with none waiting joins the counter line |
| `addActionOrCounter()` | on the counterattack list while a counterattack is running, otherwise the battler's list |
| `addImmediateAction()` | in the line that runs before everything else |
| `addAutomaticAction()` | at the end of the battler's list, with its advance wait added and aimed |

A command's MP cost is set when it is added (`commandMpCost()`): Magic, X-Magic, Lore and Summon cost
a party member the cost in its spell list — a summon what its esper's entry costs, a spell not in the
list nothing — and a monster the spell's base cost from the attack table. Setting `state.mimicking`
makes the next action added free.

`BattlerQueue` is a line of battlers: `push`, `pop`, `erase` (everywhere), `contains`, `size`,
`at(i)`.

## The player's commands

When the player finishes a command in the battle menu, add it to `state.chosenActions` (or pass it
in `BattleInput::playerActions`). The next frame battle time runs, `takePlayerActions()` takes in
every one waiting: the character joins the advance-wait line, its menu closes, and its command is
added — settled the way the original settles it (X-Magic becomes two spells, a summon of the
equipped esper names it, a slot reel names its spell, the counted commands add their base, the
commands that aim again drop their targets).

```cpp
state.chosenActions.push_back(PlayerAction{.character = BattlerSlot::character(0),
                                           .command = BattleCommandId::X_MAGIC,
                                           .attack = AttackId::FIRE,
                                           .targets = BattlerSet::of(BattlerSlot::monster(0)),
                                           .secondAttack = AttackId::ICE,
                                           .secondTargets = BattlerSet::of(BattlerSlot::monster(1))});
```

`PlayerAction::attack` is what the menu chose: a spell or item by its id, a dance by `DanceId`, a
rage by `MonsterId`, a slot reel outcome 0-7, and for Summon, Lore, Magitek, Blitz and Bushido the
attack's place in its own list. `AttackId::NONE` asks for the equipped esper, a random dance or a
random rage.

## The battle loop

Each `stepBattle()` first works out again any party member whose equipment the Item menu changed,
then does the first of: an interrupting action; dropping the party's choices when Gau
has just joined; a counterattack; taking newly dead monsters off the field; the battle's end; a
battler starting its advance wait; a battler's turn. A battler's turn runs its first action and,
once the script has played, settles every battler (statuses, who is alive, who joins and leaves,
pending updates, menus, who is held) and either stays mid-turn for its next action or steps back.

What happens in each, and in what order, is in the contract.

## The executor

`ActionExecutor` is the seam the rest of the battle plugs into. Every member has a default that does
nothing.

| Member | Called when |
|---|---|
| `execute(state, actor, action)` | an action runs; returns the battle script that shows it |
| `aim(state, actor, command)` | a command is chosen or re-timed and has to be aimed |
| `chooseAutomaticAction(state, actor)` | a party member acting on its own has nothing chosen |
| `runAi(state, actor)` | a battler's AI takes its turn |
| `runCounterAi(state, actor)` | a battler's counter AI answers |
| `checkControl`, `releaseLinks`, `clearControl` | after every command, for control, Zinger, Love Token and Charm |
| `refreshCommandList`, `refreshMorph`, `checkNearFatal` | an action left those updates pending for a battler; `refreshCommandList` also after an equipment change |
| `tickMorph(state, slot)` | every time update, for the morphed battler |
| `applyStatusChanges(state)` | the battle's start: the statuses waiting to be set go on |
| `initAi(state, slot)` | a monster is loaded: get its AI ready from `aiScript` and set `hasCounterScript` |
| `chooseDanceStep`, `chooseRageAttack`, `chooseRandomEsper` | a menu choice comes in: the dance step, the rage's attack, a slot reel's esper; each returns what it is given by default |
| `playBattleEnd(state, outcome)` | `BattleSim`, once the battle has an outcome, before the party is handed back |

Before `execute` runs, the battle resets `state.scratch`; the command reports there whether its hit
was physical, who it ended up hitting, whether Interceptor guarded someone, which characters a Black
Belt may answer for, and whether counters are disabled. The counterattack checks read it straight
after.

```cpp
struct Swing : ActionExecutor {
    std::vector<BattleScriptCommand> execute(BattleState& state, std::optional<BattlerSlot> actor,
                                             const Action& action) override {
        if (const auto* command = std::get_if<CommandAction>(&action); command && actor) {
            for (int i = 0; i < kBattlerSlots; ++i) {
                const BattlerSlot target = BattlerSlot::fromIndex(i);
                if (command->targets.has(target)) {
                    Battler& hit = state.at(target);
                    hit.hp = hit.hp > 10 ? hit.hp - 10 : 0;
                    hit.lastAttacker = *actor;
                    hit.retaliationPending = true;
                }
            }
            state.scratch.physical = true;
        }
        return {};
    }
};
```

`NullExecutor` is the base class itself: nothing happens, no AI acts. `CommandExecutor`
([commands.md](commands.md)) is the battle's own: it carries out the commands and aims them.

## Requests

`state.requests` collects, in order, what the presentation has to do: `OpenBattleScene` (the
battle begins), `OpenBattleMenu`, `CloseBattleMenu`, `RefreshBattleMenu`, `RunBattleScript`,
`RefreshMonsterNames`, `ScrollFinalBattleBackground` (a final-battle stage is won),
`CloseBattleScene` (the battle is over) and `AddItemObtained` (a party member came away from an
action holding an item, which goes into the battle inventory). A `RunBattleScript` holds
`BattleScriptCommand`s and is the one request the loop waits on. The battle core writes
`ReadyStance` (a character stepping forward for its command, or leaping for a Jump), `StepBack` and
`FinalBattleStageArrives` (the next stage's background and monsters); the commands write the rest —
names, animations, messages ([commands.md](commands.md)).

## The battle's end

`stepBattle()` returns a `BattleOutcome` once the battle is over, and keeps returning it:
`BattleWon`, `BattleLost` (with the message shown, if any) or `BattleEnded` (with a
`BattleEndReason`: the story's time ran out, Gau leapt, ended at once, rages learned, the final
battle's next stage with its formation, a story countdown, Zone Eater). Gau joining on the Veldt and
Shadow walking off happen in place of a win (Gau, to a party of fewer than two standing, only once
he has joined the party before — `state.gauObtained`); they start a battle event and the battle goes on until
the next check finds it won.

## Random numbers

`BattleRng` reads the shipped RNG table: `next()` (0-255), `below(n)` (0 to n − 1), `coin()`,
`pickFrom(set)` (one member, the party's counted first; an empty set draws nothing). The loop moves
the position on by one entry per step without drawing, so tests set `state.rng = BattleRng(position)`
to fix what a battle will draw.

## Gotchas

- **Step only while no script plays.** A step that started a script expects the next call to come
  after the script has played; that is where the rest of the action happens.
- **A speed change takes effect at the battler's next action.** Set `pendingUpdates.speed`, or call
  `recalcAtbRate()` yourself if you need it now.
- **A Jump goes round twice.** The first time a Jump reaches the front of the action line the battler
  only leaves the field and goes back to the advance-wait line, where the leap plays; it lands with
  the attack the next time.
- **An interrupting action has no actor.** `execute()` receives an empty `actor` for it; the battler
  it concerns is in the action itself (`SpellWithoutCaster::target`, `StatusTimersExpired::battler`).
- **The player's choices come in only on frames battle time runs.** While the presentation has
  stopped time they wait in `state.chosenActions`; in wait mode an open sub-menu does not keep them
  out.
- **Gauge low bytes matter.** A gauge keeps the part-step it had gained when it filled; tests that
  pin the frame a gauge fills on set the whole counter.

## Where to change

| To change | Edit |
|---|---|
| How fast gauges fill, Haste and Slow | `recalcAtbRate()` and the timer steps in `battle_clock.cpp` |
| A command's advance wait | the command table (`commandAdvanceWait`, `data/battle_commands.h`) |
| When status timers run out | `runBattlerTimers()` in `battle_clock.cpp` |
| The order the loop serves things in | `runLoopStep()` in `battle_loop.cpp` |
| Who answers an attack | `checkCounterattacks()` in `battle_loop.cpp` |
| When the battle ends | `checkBattleEnd()` in `battle_loop.cpp` |
| How a chosen command is settled | `settleCommand()` in `player_intake.cpp` |
| What `BattleSim` does at the end | `BattleSim::finish()` in `battle_sim.cpp` |
| What a command does | `CommandExecutor::execute()` ([commands.md](commands.md)), or your own `ActionExecutor::execute()` |
