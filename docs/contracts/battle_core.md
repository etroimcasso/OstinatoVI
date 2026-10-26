# Battle core — behavioral contract

Behavioral specification for the battle core: battle time (the ATB gauges, the advance waits and
the status timers), the lines battlers wait in, the battle loop that takes turns, what happens
after every action, how a battle ends, a battle's start from a formation and the party — who
stands in the party slots, each member's battle stats from their equipment, their commands and
spells, the battle inventory, the monsters, the battle type and the starting gauges — the player's
commands as the battle takes them in, what the game keeps when the battle ends, and a battle run
from a frame tick. All citations point into the pinned reference tree `original-src/`
(everything8215/ff6 @ `1ea47b5`); line numbers are into `src/battle/battle_main.asm` unless another
file is named. This document is the test authority for `tests/test_battle_state.cpp`,
`tests/test_battle_clock.cpp`, `tests/test_action_queue.cpp`, `tests/test_battle_loop.cpp`,
`tests/test_party_setup.cpp`, `tests/test_spell_lists.cpp`, `tests/test_battle_start.cpp`,
`tests/test_player_intake.cpp` and `tests/test_battle_sim.cpp`.

What each command does, how actions are aimed, how an AI chooses, and the status effects that tie
two battlers together are outside this contract: the battle core calls them through
`ActionExecutor` and specifies only when.

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| Battle time, per displayed frame | `UpdateBattleTime` `:2668-2722` | `tickBattleTime()` |
| The gauge | `actioncounter` `:2755-2779`, `_inputwindowopen` `:2785-2809` | `AtbGauge`, `Battler::gauge` |
| The advance wait | `gaugefull` `:2728-2749` | `Battler::advanceWait`, `advanceWaitCounter` |
| The ATB rate | `CalcSpeed` `:1539-1571` | `recalcAtbRate()` |
| The timer jobs | `DecCounters` `:14839-14905`, `timer` `:14939-14963`, `rigenetimer` `:14990-15035`, `GlobalCounter_00/01/05` `:15040-15114` | `tickBattleTime()` |
| Escaping | `CheckRunAway` `:15120-15143`, `CreateRunAwayAction` `:15083-15094`, `UpdateMonsterGfxBuf` `:15169-15250` | `refreshRunDifficulty()` |
| The loop | `BattleLoop` `:67-144` | `stepBattle()` |
| Adding actions | `CreateImmediateAction` … `CreateAction` `:12881-12964`, `GetMPCost` `:12970-13013`, the line writers `:12832-12871` | `addAction()` and its siblings |
| Advance wait for a command | `CalcCmdDelay` `:614-647`, `CmdDelayTbl` `:1060-1062` | `addAdvanceWait()` |
| A battler's turn | `ExecAction` `:194-290`, `InitPlayerAction` `:389-441`, Mimic `:298-368`, `EndAction` `:374-380` | `stepBattle()` |
| Starting the advance wait | `_c22188` `:5731-5790`, `QueueAction` `:488-568` | `stepBattle()` |
| Counterattacks | `ExecRetal` `:12410-12462`, `CheckRetal` `:12525-12614` | `stepBattle()` |
| Interrupting actions | `_c22163` `:5704-5723` | `stepBattle()` |
| After every command | `ExecCmd` `:3089-3129`, `AfterAction1` `:1068-1116`, `CheckDeadMonsters` `:3135-3153`, `UpdateDead` `:12291-12361`, `_timeenable` / `_timeenable2` `:3159-3212`, `AfterAction2` `:1324-1377` and the routines it calls `:1383-1533` | `stepBattle()`, `refreshAliveSets()` |
| Removing actions | `RemoveAction` `:469-482`, `RemoveAllActions` `:1579-1600`, `QuetzEffect` `:1779-1803` | `removeAction()`, `removeAllActions()` |
| The battle's end | `CheckBattleEnd` `:11891-12017`, `CheckFinalBattle` `:12245-12262` | `stepBattle()` → `BattleOutcome` |
| Random numbers | `Rand` `:12378`, `RandA` `:12390`, `RandCarry` `:12367`, `RandBit` `:13495` | `BattleRng` |
| Placing the party | `InitParty` `:7716-7919`, `GetCharID` `:7927-7934` | `placeParty()` |
| Loading the party | `InitChars` `:6201-6222` | `loadParty()` |
| Equipment into stats | `UpdateEquip` `:2289-2389`, `UpdateEquipTbl` `:2394-2450`, `CheckHPBoost` `:2456-2472`, `CalcEquipEffect` `:2480-2646`, `EquipEvadeTbl` `:2651-2662` | `deriveEquipment()` |
| Stats into the battler | `UpdateEquipBattle` `:6733-6834`, `InvertEvade` `:6719-6727`, `FixImmuneStatus` `:6359-6380` | `applyDerivedStats()` |
| HP, MP and statuses | `LoadCharProp` `:6602-6663`, `CalcMaxHPMP` `:6669-6712` | `loadPartyMember()`, `boostedMaximum()` |
| Battle commands | `InitCmdList` `:13697-13807`, `InitCmd_00`-`_05` `:13813-13868`, `RelicCmdTbl1/2` `:13879-13886` | `buildCommandList()` |
| Spell lists | `InitSpellList` `:14049-14200`, `ValidateSpellList` `:14206-14295`, `CalcMPCost` `:14321-14336`, `UpdateEnabledMagic` `:14362-14420` | `buildSpellLists()`, `loadSpellList()`, `refreshCastableSpells()` |
| Status immunity | `UpdateImmuneStatus` `:6386-6433` | `refreshStatusImmunity()` |
| The battle inventory | `InitInventory` `:13911-13956`, `LoadItemProp` `:13981-14037` | `loadBattleInventory()`, `battleItem()` |
| Known skills | `InitSkills` `:14473-14513` | `loadKnownSkills()` |
| Equipment changed mid-battle | `UpdateCharProp` `:5595-5631` | `applyEquipmentChanges()` |
| A battle's start, in order | `InitRAM` `:6317-6332`, `InitBattle` `:6053-6195` | `startBattle()` |
| The formation | `LoadBattleProp` `:7940-7992` | `chooseFormation()` |
| The monsters | `InitMonsters` `:7672-7710`, `LoadMonsterProp` `:7307-7436`, `InitAI` `:7442-7466`, `CheckFirstStrike` `:7483-7498`, `LoadRageProp` `:7504-7550`, `AddHalf` `:11864-11873` | `loadMonster()` |
| The battle type | `ChooseBattleType` `:7556-7578`, `RandBitWithRate` `:13525-13548`, `InitBattleType` `:7584-7666` | `chooseBattleType()`, `applyBattleType()` |
| Status immunity at the start | `InitStatus` `:6439-6445` | `refreshStatusImmunity()` |
| The starting gauges | `InitGauge` `:6228-6299` | `fillStartingGauges()` |
| The player's commands | `GetPlayerAction` `:12620-12653`, `FixPlayerCmd` `:12671-12680`, `FixPlayerAttack` `:12686-12795` | `takePlayerActions()` |
| What the game keeps | `UpdateSRAM` `:12092-12179`, `RemoveChar` `:11879-11885`, `ShadowLeaves` `:11990-11997` | `writeBack()` |
| The final battle's stages | `CheckFinalBattle` `:12245-12262`, `BattleEnd_06` `:12204-12239` | `startNextFinalBattleStage()` |
| A battle from a frame tick | `BattleLoop` `:67-144`, `UpdateBattleTime` `:2668-2722`, `TerminateBattle` `:150-154` | `BattleSim` |

## The two clocks

A battle runs on two clocks.

**Battle time** moves on every displayed frame, including the frames of an attack's animation: the
graphics program calls `UpdateBattleTime` from both its own frame wait and the one it uses inside
animations, unless it has stopped time itself (battle events other than Gau's and Wrexsoul's, the
entry animation, the final battle's set pieces). `tickBattleTime(state, timeRunning, executor)` is
called once per displayed frame; `timeRunning` carries the presentation's stop. Before anything
else on a frame time runs, it takes in the commands the player has finished choosing (below, "The
player's commands").

**The battle loop** takes one step per frame while no battle script is playing. Its first act in
each pass is to wait for the next frame (`:73-74`), and every command it runs waits for the
command's script to play (`ExecCmd` `:3120-3121`). `stepBattle()` runs from one wait to the next.
A step that starts a script returns with `BattleState::waitingForScript` set; the next call, made
once the presentation has finished playing it, carries on from there.

Within a frame, battle time runs first and then the loop (battle time runs inside the loop's frame
wait). The loop's per-pass bookkeeping (the random-number advance and the Quick reset, `:68-71`)
comes at the end of `stepBattle()`, just before the next wait — the same order as the original's
pass, so the random numbers each clock draws come out in the same sequence.

## Battle time

### The two-frame update

Battle time advances only when **at least two frames have passed** since its last update
(`:2675-2681`), so the gauges move at half the frame rate. While time is stopped the frames still
pass, so the first running frame afterwards updates at once. In wait mode, a command's sub-menu
being open (`BattleState::timeStoppedByMenu`) holds time, and every frame it holds counts as an
update (`:2672-2674`, `:2717-2718`), so two running frames are needed after it closes. In active
mode the sub-menu does not stop time.

The original counts frames in a byte, so a stop lasting exactly 256*k* or 256*k* + 1 frames would
delay one update by a frame; the port counts in an `int` and does not reproduce that.

### One update

1. The **turn counter** goes up by one (`:2682-2684`).
2. One **timer job** runs (below).
3. For each battler from slot 9 down to slot 0 (`:2686-2715`): the morphed battler's Morph meter runs
   down (`executor.tickMorph`); then a battler that is present, not the monster Zinger has taken, and
   not in its menu, held, or with an action ready (`:2690-2696`) —
   - if its gauge is stopped, its **advance wait** advances, unless the party is running and it is a
     character on the party's side, on the ground and not a zombie (`:2697-2710`);
   - then, if its gauge is not full, the **gauge** fills (`:2711-2713`).

### The gauge

The gauge is a 16-bit counter whose high byte is the meter: 1 is empty, it climbs to 255, and when
the counter passes `$FFFF` it reads 0 — **full** (`:2758-2767`). The low byte carries the part of a
step the gauge has gained and is kept when it fills, so the next turn starts that much ahead. The
display shows the high byte less one: 0 empty, 255 full (`UpdateCounterGfxBuf` `:15153-15155`).

Each update adds the battler's **ATB rate**. When that fills it (`:2763-2809`):

- a character starts the party's escape if it is due (below);
- the advance-wait counter's high byte and the advance wait are cleared;
- the gauge stops; a battler that does not take menu input **joins the advance-wait line**;
- otherwise the battler's menu **may open**: a party member on the ground, with menus allowed,
  opens its battle menu (`OpenBattleMenu`); a monster reaching this point joins the advance-wait
  line. The menu shows the controlled monster's attacks when the character is controlling one:
  the English versions read the control-menu flag (`$3e4d.0`), the Japanese version the Chant
  status (`:2795-2801`).

### The ATB rate

`recalcAtbRate()` (`CalcSpeed`): let *c* be **32 under Slow, 84 under Haste, 64 otherwise** (Slow is
tested first, `:1541-1549`). *c* is also the battler's status-timer step. Let *s* = speed + 20; a
monster's *s* is first scaled by the battle-speed setting, *s* × (255 − 24 × setting) / 256, rounded
down (`:1560-1563`). The rate is (*s* × ⌊1.5*c*⌋) / 16, rounded down (`:1565-1568`).

| Battler | Speed | Status | Rate |
|---|---|---|---|
| Character | 40 | — | 360 |
| Character | 40 | Haste | 472 |
| Character | 40 | Slow | 180 |
| Monster, setting 0 / 2 / 5 | 40 | — | 354 / 288 / 186 |

At speed 40 a character's empty gauge (counter `$0100`) fills on its 182nd update, 364 frames.

In the original, speed + 20 is a byte addition. The highest speed any monster has is 99 and a
character's stays far below 236, so the sum never wraps; the port adds in an `int`.

The rate is worked out again at a battler's next action, not the moment its speed changes: every
action leaves a pending speed update (`:261-263`), applied after the action (`:1361-1363`).

### The advance wait

The advance wait is the pause between a battler's gauge filling and its action running. Each update
it adds half the ATB rate to the 16-bit advance-wait counter (`:2731-2735`); when the counter
overflows, or its high byte reaches the battler's advance wait, the action is **ready**: the wait is
cleared, the battler joins the action line and is marked as having an action ready (`:2737-2747`).
A wait not yet chosen counts as 255. At rate 360 a wait of 16 is ready on the 23rd update.

### The timer jobs

A phase counter goes up by one each update; the value before the increase, modulo 16, picks the
job (`:14841-14845`): **0-9 runs battler 0-9's timers, 10-15 run the battle's own jobs.** Each battler's
timers, and each battle job, therefore run once every 16 updates (32 frames).

**A battler's timers** (`:14848-14885`), only while it is present: its status timer adds its step
(32/64/84); only when that passes 255 does anything below happen —

1. A running **Stop** timer counts down and nothing else happens; when it reaches 0, Stop ends and
   the Reflect, Freeze and Sleep timers take their step too (`:14858-14863`).
2. A battler held by Dead, Petrify or Stop stops here.
3. A **condemned count** of 2 or more counts down; reaching 1, Doom strikes (`SpellWithoutCaster`
   with Doom, an action that interrupts the line, `:14867-14874`, `CondemnDeath` `:15058-15062`).
4. A character **tries to get away** (below).
5. A due damage-over-time beat becomes an action (below).
6. The **Reflect, Freeze and Sleep** timers each count down while above 0; any that reaches 0, or Stop
   having just ended, creates one `StatusTimersExpired` action that interrupts the line, naming
   which ran out (`timer` `:14939-14963`).
7. The **damage-over-time beat** moves on, and of eight beats, 0 and 4 make regen due and 2 makes
   poison due (`:14879-14884`, `StatusCounterTbl`).

**Damage over time** (`rigenetimer` `:14990-15035`), unless one is already waiting: poison due on a
poisoned battler → `DamageOverTime{Poison}`; else regen due → `{Sap}` for Sap or Phantasm, `{Regen}`
for Regen; else, holding a battler in **Seize** → `SeizeDrain`. Each goes on the battler's
counterattack list (so it joins the counter line) and marks one as waiting. A due beat is used up
whether or not it creates an action.

**The battle's jobs:** 10 — Roulette's choice, if any, is struck by Doom and cleared
(`GlobalCounter_00`); 11 — the battle counter goes up, and so does the own timer of every present
battler that is neither dead nor petrified (`GlobalCounter_01`); 15 — while the party is running:
if it cannot run, the escape action (which shows "can't run away") starts; otherwise, once in every
eight passes of this job (the phase after its increase has bits 4-6 clear, `:15073-15075`), the
escape starts if a character has got away (`GlobalCounter_05`); 12-14 do nothing.

### Escaping

While the party is running, a character on the party's side that has not left the battle tries on
each of its timer steps (`CheckRunAway`): with a **run difficulty** of 0 it gets away at once and the
escape starts; otherwise its progress gains 1 + random(0 … run factor − 1) and it gets away when the
progress reaches the difficulty.

The **escape** (`PartyEscape`, an action that interrupts the line) starts only while the party is
running, at least one character has just got away and the battle is not in the colosseum, and only
once until the loop's next pass (`:15083-15094`, `:111-112`).

**Run difficulty** (`refreshRunDifficulty()`, from `UpdateMonsterGfxBuf` `:15179-15250`), worked out
whenever the monster list changes: 2 for each monster present, not fallen, in play and not dead,
petrified or a zombie, 6 for one that is harder to run from; 0 in a side attack or after a pre-emptive
strike. The party cannot run when a monster prevents it, when a pincer attack has monsters standing
on both sides, or when a party member is fighting for the other side.

## The lines and the lists

Each battler keeps its **actions** in the order they were chosen, and its **counterattacks** in the
order they were triggered. The battle keeps three lines of battlers — the **advance-wait line**,
the **action line**, the **counter line** — and one line of **actions that interrupt** everything
else.

| Function | Source | Where the action goes |
|---|---|---|
| `addAction()` | `CreateNormalAction` `:12928-12938` | the end of the battler's list |
| `addCounterAction()` | `CreateRetalAction` `:12910-12922` | its counterattack list; a battler with none waiting joins the counter line |
| `addActionOrCounter()` | `CreateDefaultAction` `:12900-12904` | the counterattack list while a counterattack is running, the battler's list otherwise |
| `addImmediateAction()` | `CreateImmediateAction` `:12881-12894` | the interrupting line, behind any already there |

**MP cost** is set when an action is added (`GetMPCost`): Magic, X-Magic, Lore and Summon cost a party
member what its own spell list says (nothing for a spell not in it) and a monster the spell's base
cost; every other action costs nothing. An action added while Mimic is copying costs nothing, and
the next action added, of any kind, uses that up (`:12975-12977`).

**The advance wait for a command** (`addAdvanceWait()`, `CalcCmdDelay`): the command's wait from the
command table (16 for most; 32 for Magic, Lore, X-Magic, Health and Shock; 0 for Morph and
Revert; 224 for Jump) adds to the
battler's current wait, which stops at 254; a wait not yet set counts as 255. The command is then
aimed (`executor.aim`).

**Removing everything** (`removeAllActions()`): the battler's actions go (its counterattacks stay), it
leaves the advance-wait line, a full gauge is set one step from full with the menu, stopped and
ready marks cleared, and a party member goes back to standing normally (`ResetBattlePose`).

The original keeps the actions of all ten battlers in one shared pool of 128 entries and the lines
in 256-entry rings, with an overflow rule for a full pool; neither size is reachable in a battle,
and the port keeps each list and line as a growable sequence with the same order.

## The battle loop

Each `stepBattle()` serves, in this order, the first of these that has work, and that one thing is
the step (`BattleLoop` `:67-144`):

1. Party members whose equipment the Item menu changed are worked out again (below, "Equipment
   changed mid-battle"; this never ends the step).
2. Characters whose menus need redrawing get `RefreshBattleMenu` (this never ends the step).
3. An **interrupting action**.
4. **Gau has just joined** on the Veldt: every living party member on the ground drops what it had
   chosen (`ResetForVeldtGau` `:1663-1673`).
5. A **counterattack** in progress, else the next battler in the counter line.
6. **Monsters that died** and are still standing leave the field.
7. If the monster list changed, run difficulty is worked out again and `RefreshMonsterNames` is
   requested (this never ends the step). The escape may start again.
8. **The battle's end** is checked.
9. The battle forgets who was attacked and whose counter AI ran this pass, and that a counterattack
   was running.
10. The next battler in the **advance-wait line** starts its wait.
11. A battler **mid-turn**, else the next in the **action line**, takes its turn.
12. With nothing in the action line, a battle that was being kept going may end.

After the step, the random-number position moves on by one entry, and a Quick count that has run
out is cleared (`:68-71`). A step that waits on a script does this only when it finishes.

### An interrupting action

The first interrupting action runs with no battler taking it (`_c22163`); a counterattack is marked
as running, so nothing counters it.

### A battler's turn

`ExecAction` (`:194-290`). The battler stops being the one mid-turn. Then, while it has actions:

- A **Mimic** becomes the last command a character used — its last Jump, or its last command, with
  an X-Magic second spell added behind it at no MP cost (`:298-327`). With no command yet used,
  Mimic is a Fight with no targets.
- The action is prepared (`:389-441`). A command other than Magitek whose user's statuses have
  changed since it was chosen loses its targets, to be aimed again. An **Imp** using a command its
  Imp flag does not allow fights instead, for no MP, at one random target from those it had — never
  one on its own side unless that one fights for the other side.
- An **AI turn** is removed and the AI runs (`executor.runAi`); the loop goes round again for what it
  chose (`:211-224`).
- A **Jump** by a battler on the ground leaves the field: it is marked away, its gauge stops, it is no
  longer ready, its advance-wait counter's high byte is cleared, and it rejoins the advance-wait
  line; the turn ends there (`:225-230`, `:178-185`). A Jump by a battler already away lands now.
- If this is the battler's **last** action, battle menus are allowed again, and the Quick target's
  count goes down — reaching 0, Quick ends (`:232-244`).
- A battler **held** by status or control keeps the action and is marked to go back in line later;
  the turn ends (`:245-250`).
- The action is taken off its list.

A battler with no actions takes a turn with no effect (`NoAction`). Then it is no longer stopped or
ready and is marked held-by-control while it acts (`:255-258`); if present, its speed update is set
pending and the action runs; if not, the turn ends without one.

**After the turn** (`:269-290`), once its script has played: the monster list is marked changed and
the battler as still acting. If it has more actions and Sonic Dive has not ended its turn, it stays
mid-turn and the next pass runs the next one. Otherwise its gauge, unless stopped, starts again from
empty (a full gauge to 1; any other one step on, unless one step from full), its advance wait and the
counter's high byte are cleared, and it **steps back** (`StepBack`, a script the loop waits on).

**Mimic remembers** a party member's command when the command table marks it as copyable
(`SaveForMimic` `:333-368`): X-Magic is remembered as the second spell; anything else is remembered as
the last command, with the targets it ended up hitting, and clears the second spell. A monster's
command is never remembered.

### Starting the advance wait

`_c22188` (`:5731-5790`). A held battler is marked to go back in line and stops. Otherwise it will skip
the advance wait for its next action once it is ready, and its turn is set up (`QueueAction`
`:488-568`): an unset wait becomes 0; Def, Runic and Retort end; a monster with nothing chosen gets
an AI turn (with no wait added); a character's statuses-changed mark is cleared, a character with an
AI script runs it, and a character with nothing chosen chooses on its own
(`executor.chooseAutomaticAction`); otherwise every action it has waiting adds its command's wait and
is aimed again.

If its first action is a command: a **Jump** (by a battler that may act after jumping) is remembered
for Mimic when a party member makes it, and the battler is marked away and hidden; a monster's other
commands show nothing; every party member's command, and every Jump, plays the **ready stance**
(`ReadyStance` — stepping forward, or the leap), a script the loop waits on.

### Counterattacks

`ExecRetal` (`:12410-12462`). A counterattack is marked as running. While the battler has
counterattacks waiting, the first is prepared and taken off; an AI turn runs the counter AI
(`executor.runCounterAi`) and goes round again. A party member's command runs only if it has not left
the battle and, fighting for the party, the monsters are not all gone and it is not held; one
fighting for the other side always runs it. Afterwards: with more counterattacks waiting it stays
countering; otherwise, unless the battler whose turn it was is still acting, it steps back.

**Who answers a command** (`CheckRetal` `:12525-12614`), checked after every command for each present
battler, slot 9 first, while the command has not disabled counters:

1. A battler hit by another (not itself) is marked as attacked this pass and will answer the
   attacker.
2. Unless it died this action and may answer from its AI, nothing answers during a counterattack
   or with no one to answer.
3. A battler with a counter AI script and no counterattack waiting adds an AI turn to its
   counterattack list.
4. Otherwise only a party member hit by a **physical** attack can answer: **Retort** answers with
   Bushido's Retort; then, only against a monster: guarded by **Interceptor**, one random entry
   decides — odd: Interceptor answers, with Wild Fang if bit 1 is set and Takedown if not, both
   under the Magic command; and a **Black Belt** wearer the action allows, when the next entry is
   below **192** (three times in four) and it is not dead, petrified, in Magitek armor, a zombie,
   asleep, confused, dancing, stopped, frozen, chanting or hidden, answers with Fight.

### After every command

`ExecCmd` (`:3089-3129`), in this order:

1. The action's report is reset: counters enabled, not physical, no Interceptor guard, no Black Belt
   allowed, the targets it hit = its targets.
2. The command runs (`executor.execute`).
3. **The fallen** (`AfterAction1`), for each present battler not just joining, slot 9 first: control
   links are checked; a dead or zombie battler's HP is 0; a dead, petrified or zombie battler is
   marked fallen if it is a monster, releases its links, and ends Quick if it was the Quick target;
   a dead **Banon** sets the end-of-battle event; a dead battler away from the field (jumping, carried
   off, held in Seize) survives until it lands and is air-anchored; a dead zombie stays alive; a dead
   battler with **Reraise** loses it, stops standing, and gets Life (`SpellWithoutCaster`, interrupting
   the line). Then control links marked for release are released.
4. **Who answers** (above).
5. A fallen monster leaves the field at once if it is hidden, a piranha, or has no counterattack
   waiting; one with a counterattack still to make stays standing until it has made it
   (`CheckDeadMonsters`).
6. **The script plays**; the loop waits.
7. **Who is still fighting** (`refreshAliveSets()`, `UpdateDead`): from presence, the joining and
   leaving marks, Dead/Petrify/Zombie and the fallen mark; a party member fighting for the other side
   counts as an enemy. Ally count = party members alive; enemy count = monsters alive plus party
   members fighting for the other side.
8. **Battlers joining** the field become present, standing, and lose Dead, Petrify, Imp, Zombie and
   Hide; a monster with first strike stops its gauge full, its advance-wait counter's high byte at
   255, and joins the advance-wait line.
9. **Settling** (`AfterAction2`), for each present battler, slot 9 first: its "hit, may answer" mark
   clears; a dead battler, or one whose action empties its gauge, has its menu, stopped and ready
   marks cleared and its gauge emptied, and its menu is canceled (below). Its pending updates run
   once each, in this order: carried off by Palidor (marked away, gauge stopped, out of the action
   line, may not act after jumping, an advance wait of 224); its castable spells; removing all its
   actions (and marking it to go back in line); a condemned count starts — **20 + max(0, 60 − (L +
   random(0 … L − 1)))** with L the attacker's level (20 for counts that start with the battle,
   `:1512-1525`); the condemned count stops; a party member's battle commands; its ATB rate; its menu
   after Morph. Then its menu may open (below); then whether it is held (below); a held battler loses
   Def; and the near-fatal relics may cast.
10. **Battlers leaving** the field are no longer present or standing, are hidden, release their
    links and end Quick if it was theirs.

**Its menu may open** (`CheckPlayerAction` `:1426-1463`), for party members other than Umaro, with no
AI script, outside the colosseum: it takes menu input; the Quick target's gauge stops and its menu
may open. Its menu is canceled if it is leaving, seized or charmed, or is dead, petrified, a zombie,
asleep, confused, berserk, dancing, hidden or raging. Otherwise a battler whose menu may open and
who has nothing chosen stops its gauge and opens its menu.

**Canceling the menu** (`CancelAction` `:1483-1506`): a battler not hidden, not a party member leaving,
whose menu may open, loses that and joins the advance-wait line — it acts on its own; every battler
stops taking menu input and leaves its menu; a party member's menu closes (`CloseBattleMenu`).

**Held** (`_countercheck` `:1383-1420`): held by both status and control while another battler has
Quick; held by status when dead, petrified or stopped; otherwise held by control when controlled,
asleep, frozen or seized; otherwise free, and a battler marked to go back in line with actions
waiting rejoins the action line (when it was skipping its advance wait) or the advance-wait line.

## The battle's end

Checked every pass of the loop (`CheckBattleEnd`), in this order. Outside the final battle, an
end-of-battle event ends the battle as that event says. A story countdown having run out ends it
(`TimerExpired`). A hidden monster about to return keeps it going. Then:

- **No party member fighting:** everyone swallowed by Zone Eater → `EatenByZoneEater`; a party member
  having left (escaped, sneezed away) → `RanOutOfTime`; otherwise **lost**, with the "annihilated"
  message (41), or none in the colosseum.
- **Enemies left:** the battle goes on.
- **No enemies left:** in the final battle, a stage with a next stage ends with
  `FinalBattleNextStage` and that stage's formation. Then, if Gau is in the party and may appear,
  that chance is used up, and with the next entry below **160** (five times in eight) and either
  Gau having joined the party before (the battle switch for it, `:11936-11938`) or two or more
  allies, **Gau joins**: he is set to join the field, fighting for the other
  side, his battle event (27) interrupts the line, and the pass ends; the next pass drops the party's
  choices. Otherwise, if Shadow is in the party: with the next entry below **16**, a front attack,
  two or more allies, Shadow not dead, petrified or a zombie, not kept, and available, **Shadow
  leaves**: he is no longer available, no longer present, there is no victory fanfare, his battle
  event (11) interrupts the line, and the pass ends. Otherwise the battle is **won**.

| End-of-battle event | Outcome |
|---|---|
| Ran out of time (also Warp, and every character sneezed away) | `BattleEnded{RanOutOfTime}` |
| Gau leapt | `BattleEnded{GauLeapt}` |
| Banon fell | `BattleLost` with message 54 |
| End now | `BattleEnded{EndNow}` |
| Rages learned | `BattleEnded{RagesLearned}` |
| The final battle's next stage | `BattleEnded{FinalBattleNextStage}` |

The final battle's stages are `kFinalBattleFormations`; each but the last leads to the next
(`CheckFinalBattle`). Once a battle has an outcome, `stepBattle()` returns it and does nothing more.

## The party at the battle's start

The party's records (`game::Party`, the sixteen character records and the party's inventory and
learned abilities) are copied into `BattleState::party` when a battle starts; the battle reads and
changes that copy.

### Placing the party

`placeParty()` (`InitParty` `:7716-7919`) fills the four party slots one of three ways:

- **The current party.** Every character record whose party is the current one stands in its
  lined-up slot. The records are walked from the last to the first (`:7757-7774`), so when two claim
  one slot the lower record wins. **Gau may return from the Veldt** only when he has not been
  recruited, the formation's sixth monster slot is empty, and fewer than four characters are in the
  party (`:7775-7785`); otherwise `gauMayAppear` is cleared. When he may, the Veldt setup
  (`GAU_VELDT`) is staged in place of the formation's own.
- **A colosseum match** (`:7748-7756`): the chosen fighter stands alone in the first slot, the
  battle is a colosseum battle, and the fighter enters at full strength like a scripted setup's cast.
- **A stage of the final battle** (`:7721-7747`): the fighters still standing keep their slots, and
  each empty slot takes the next character from `finalBattleReserve`, which is used up.

**A scripted setup** (`CharAI`, `:7793-7844`) then places its cast. A setup that hides the party
empties every slot first. Each cast member takes the slot of a party member who is that character
and stands as an ordinary ally; failing that, the first empty slot; with none empty, it sits the
battle out. It brings its flags (not in the party, fighting for the other side), its sprite unless
it uses the character's own, and its AI script — script *n* of the setup is script 256 + *n*, and
the character then takes its turns from it. The slots a setup filled are returned, and those
characters enter at full strength.

**Seating** (`:7845-7917`), for each filled slot: a character in the party is present; one fighting
for the other side acts as an enemy. Every record holding the character supplies the row, the
record, the sprite (unless the setup named one) and the bits an item's equippable-by mask must share
for them to hold it in battle: Terra to Umaro each have a bit of their own; for anyone else it is
the fifth and sixth bytes of their name (`:7891-7908` — see `docs/Bugs.md`). With two records
holding the same character, the lower record's values are the ones kept.

### Equipment into battle stats

`deriveEquipment()` (`UpdateEquip` `:2289-2389`) starts from the character's battle properties —
battle power in both hands, defense, magic defense, evade and magic block — and the record's
vigor, speed, stamina and magic power. The run factor is 5 down to 2 for a run-factor trait of 0
to 3 (`:2328-2332`).

Each of the six slots is then added, **relic 2 first and the weapon last** (`:2342-2348`). For every
item (`CalcEquipEffect` `:2480-2646`): its protections, the statuses it keeps on its wearer and its
relic effects are gathered; its four stat boosts (−7 to +7) and its evade and magic-block boosts
(`EquipEvadeTbl`) are added. **Gear worn the wrong way round for an Imp** — imp gear on someone who
is not an Imp, or any other gear on an Imp — **counts as power 1** (`:2533-2545`).

- A **weapon** (the empty slot is typed a weapon) in the weapon or shield slot sets that hand: its
  element, power added to the hand's battle power (capped at 255), hit rate, spell and weapon
  flags. In any other slot it adds nothing. A bare hand is the empty item: **power 10, hit rate 200**.
- **Anything else** adds its power to defense and its magic defense to magic defense (each capped at
  255), and its elements and the statuses it inflicts on its wearer.
- An item that **raises magic damage** gives the single effect; a second one gives the double
  (`:2576-2582`; in the shipped data the Earrings and the Hero Ring).
- The weapon and shield slots' block effects are kept per hand, and a block graphic is noted for
  physical and for magic blocks.

An item's grant of the third status byte's first bit is Float, which the battle keeps among the
fourth byte's statuses (`:6648-6659`).

After the items: **HP and MP boosts** — +50% before +25% before +12.5% (`CheckHPBoost`); every stat
and the evade and magic-block ratings are clamped to 0-255 (`:2363-2373`); the hands are settled
(`UpdateEquipTbl`); a **Hyper Wrist** adds half the vigor again (`:2376-2383`).

| Weapon slot | Shield slot | Attacks with | Two-handed grip |
|---|---|---|---|
| weapon | weapon | both (two weapons) | lost |
| weapon | shield | weapon slot | lost |
| weapon | bare | weapon slot | kept |
| bare | weapon | shield slot | kept |
| bare | shield | weapon slot | lost |
| bare | bare | weapon slot | lost |
| shield | weapon | shield slot | lost |
| shield | bare | shield slot | lost |
| shield | shield | both keep the character's battle power | kept |

"Attacks with" names the hand that keeps its battle power; the other's is 0.

### Stats into the battler

`applyDerivedStats()` (`UpdateEquipBattle` `:6733-6834`):

- A **Moogle Suit** draws its wearer as Mog.
- A **Genji Glove** lets the battle's Item menu give the wearer two weapons. Its effect stays among
  the relic effects only while the wearer does not hold two weapons (`:2749-2772`).
- **Vigor is twice the character's**, capped at 255. A Hyper Wrist takes the character's vigor past
  255 only from 171, and the most a character can reach is 128 (the level-up cap,
  `:15936-15939`) plus 7 from each of six items, so the cap is never passed.
- **Evade and magic block** are stored as the hit check reads them: 255 for a rating of 0,
  256 − 2 × the rating below 128, and 1 for 128 or more (`InvertEvade`).
- Relic effects; undead from a Relic Ring; every party member counts as human; the party's second
  relic effects are gathered and never taken away.
- Without a **Gauntlet** neither hand keeps the two-handed grip (`:6802-6806`).
- The first sixteen statuses can be put on the battler unless its equipment protects against them.
  Protection against **Dead** becomes protection against instant death instead, and Dead itself can
  still be put on; a battler that **nullifies poison** cannot be poisoned (`FixImmuneStatus`).
- A **Merit Award** lets the wearer hold heavy items (`:6830-6832`).

### HP, MP and statuses

`loadPartyMember()` (`LoadCharProp` `:6602-6663`): HP and MP are the record's; the maximums are the
record's raised by the boosts — an eighth, a quarter or a half more, rounding down — and capped at
**9999** and **999**. A character a setup brought in is **healed to full**, and their record keeps
only Blind, Poison, Magitek and Imp of its first eight statuses (`:6622-6631`). The battle starts
the battler with the record's first eight statuses, Interceptor and Float from the record, and the
equipment's statuses. A record in **Magitek armor** leaves Mimic with Magitek's Fire Beam. Equipment
that keeps **Condemned** on its wearer cancels the stop of the condemned count the battle's start
leaves pending.

### Battle commands

`buildCommandList()` (`InitCmdList` `:13697-13807`), from the record's four commands:

1. In **Magitek armor**, or where **only magic** works (Fanatics' Tower), every command but Item,
   Mimic and Magic goes; Fight (and Gau's Rage) becomes Magitek. Where only magic works, Magitek then
   becomes Magic and Magic goes.
2. **Relic swaps** (`RelicCmdTbl1/2`): Fight → Jump, Magic → X-Magic, Sketch → Control, Slot →
   GP Rain, Steal → Capture.
3. **Commands with conditions:** Morph goes unless the story allows it, and with a permanent morph
   the character starts morphed, the Morph supply is filled and the command goes; Leap goes off the
   Veldt; Dance goes with no dance known; Magic and X-Magic go with no spell and no esper.
4. Each row keeps the command, shown usable, and its targeting; an empty row has neither.
5. A party member with no Magic, X-Magic or Lore has **no MP** (`:13797-13803`).

### Spell lists

`buildSpellLists()` (`InitSpellList` `:14049-14200`): the spells any party member among the first
twelve characters has learned are laid out by the **spell-order setting** — black, effect and white
magic each moved by their own offset (`BlackMagicOrderTbl`, `EffectMagicOrderTbl`,
`WhiteMagicOrderTbl`) — then closed up **three at a time**: a group of three places with none known
is left out. Lores have places of their own after the spells, Condemned first. Every party member
gets the list with each spell's targeting and MP cost; **Step Mine** costs one MP per half hour of
the play time **recorded at the last save** (`:14187-14192`, which reads the save's copy of the
time).

`loadSpellList()` (`ValidateSpellList` `:14206-14295`): the equipped esper becomes the list's
summon. The first twelve characters keep the spells they have learned themselves and every lore;
Gogo keeps everything; anyone else keeps nothing. Each kept entry's cost is the member's own
(`CalcMPCost`): a **Gold Hairpin** halves it rounding up, an **Economizer** makes it 1. The spells
kept, not counting lores, are the member's spell count.

`refreshCastableSpells()` (`UpdateEnabledMagic` `:14362-14420`): an entry is castable when its cost
is below the member's MP plus one, at most 255; an **Imp** can cast only Imp; an esper already
summoned this battle cannot be summoned again. It runs whenever an action leaves it pending.

**What a party member's spell costs** (`GetMPCost` `:12970-13013`): its entry's cost; a summon costs
the list's summon; a spell not in the list costs nothing.

### Status immunity

`refreshStatusImmunity()` (`UpdateImmuneStatus` `:6386-6433`): Float can no longer be put on a
battler whose equipment keeps Float on, nor Morph with a permanent morph, nor any of Silence,
Berserk, Confuse, Sap, Regen, Slow, Haste, Shell, Protect and Reflect that its equipment keeps on it.
Then **Regen and Sap**, and **Slow and Haste**, can each be put on only if both can. Nothing here
makes a status possible again.

### The battle inventory

`loadBattleInventory()` (`InitInventory` `:13911-13956`): the party's 256 inventory slots, then
what each party member holds in each hand (quantity 1). `battleItem()` (`LoadItemProp`
`:13981-14037`) gives each item its targeting, whether it can be used in battle and thrown, whether
it is a tool, a weapon or a shield (`ItemTypeMaskTbl`), and — for weapons, shields and consumables —
which party slots' members can hold it: those whose equippable-by bits it shares. The tools the
inventory holds are noted.

### Known skills

`loadKnownSkills()` (`InitSkills` `:14473-14513`): how many swdtechs and blitzes the party knows,
which dances, and the rages known **among the first 255**, in order. The number of lores known is
counted with the spell lists.

### Equipment changed mid-battle

When the battle's Item menu changes what a party member holds (`UpdateCharProp` `:5595-5631`), the
next loop step writes the hands into their record, copies their first eight statuses into it, works
out their stats again (above), asks for their battle commands to be refreshed, and works out their
status immunity again.

## A battle's start

`startBattle()` builds a battle from a `BattleStart` (the formation, the background, what the field
allows), the party, the battle switches and the game's text, in the original's order (`InitBattle`
`:6053-6195`), so its random numbers are drawn in the same sequence:

1. Mimic's memory starts as Mimic (`InitRAM` `:6329-6331`); the final battle is recognized when the
   formation asked for is its first stage (`:6078-6082`).
2. The random numbers start at **four times the frames part of the play clock** (`:6088-6090`).
3. The formation (below).
4. The party is placed (above, "Placing the party"): a final-battle stage, a colosseum match when
   the formation is one of the last two, or the current party; the formation's scripted setup, if
   it has one.
5. Battle switches 63 and 64, 68 and 71 — party defeated, ran out of time, swallowed by Zone Eater —
   are cleared (`:6093-6096`). The battle's rules are read from the switches: only magic, a
   permanent morph, Morph available, Gau having joined before, Shadow kept.
6. For every slot from the last to the first, one draw sets its **damage-over-time beat**, and the
   updates for its castable spells, a condemned count's start and stop, its battle commands and its
   ATB rate are left pending — its menu after Morph too, for a morphed battler (`:6097-6106`).
7. The party is loaded (above), then the battle inventory and the known skills.
8. The monsters (below).
9. The statuses waiting to be set are put on (`executor.applyStatusChanges`); the after-command
   checks run once — the fallen, then settling with condemned counts worked out at **level 20**
   (`:6129-6132`); who is still fighting is worked out.
10. The battle type (below); status immunity for every battler (`InitStatus`); what the battle type
    does; the starting gauges (below); the opening message (below); run difficulty.
11. A fighter still away from the field from the final battle's last stage comes down as the stage
    opens: its gauge full and stopped, in the action line with a Jump (`:6166-6179`).
12. The opening: `OpenBattleScene`, and the random numbers move on one entry — the loop's own once-a-
    pass advance, which comes before its first wait (`:67-68`). A final-battle stage after the first
    instead plays `FinalBattleStageArrives` — the background scrolls to the stage and its monsters
    appear — and the loop waits for it (`:6180-6193`).

### The formation

`chooseFormation()` (`LoadBattleProp` `:7940-7992`): the eight **conditional battles** are checked
from the last to the first; battle switch 48 + *n* turns on row *n*, and a turned-on row whose
trigger is the formation asked for — its randomize flag included — replaces it with the row's
replacement, which a lower row may replace again (`:7943-7955`). A formation with the randomize flag
becomes itself plus the low two bits of one draw (`:7956-7965`). The formation the battle began with
is remembered (`:7966-7970`): in the final battle it stays the first stage's, and Doom Gaze, the
colosseum and the Veldt look at it.

### The monsters

Every monster slot is present or not by the formation (`:7675-7682`). The slots are then loaded
from the last to the first (`:7686-7708`): a slot with a monster loads it; in a colosseum match
every slot whose monster number is below 256 loads the opponent instead.

`loadMonster()` (`LoadMonsterProp` `:7307-7436`) gives a monster, from its record:

- its AI script, which is the monster's own (`executor.initAi` gets it ready, `InitAI`); its two
  steal items;
- its **name group**: the monster in the highest slot loaded so far, itself included, whose name
  compares equal — the battle's list of monsters' names counts the group on one line
  (`:7325-7359`). Names are compared eight bytes at a time, eight bytes into the name table per
  monster. The Japanese table's names are eight bytes long; the English table's are ten, so in the
  English versions two monsters with the same name are counted on separate lines unless the eight
  bytes at their own offsets happen to match (`docs/Bugs.md`);
- defense, magic defense, experience, gold, HP and MP (both full); Doom Gaze, in his own
  formation, takes the HP he last fled with, kept in the battle switches, unless that is 0
  (`:7378-7384`);
- **stamina: 16 plus one for each 512 of maximum HP, at most 40** (`:7385-7392`);
- attack power and the item whose animation its Fight borrows, as its weapon hand; evade and magic
  block as the hit check reads them (`InvertEvade`); hit rate, level and speed; **magic power half
  again, at most 255** (`AddHalf`);
- its own Runic and its piranha mark (`:7412-7415`); its battle flags — first strike, harder to
  run from, can't run, and the immunities to Suplex, Scan, Sketch and Control;
- from `LoadRageProp` (`:7504-7550`): its special attack; its weak, absorbed and nullified
  elements; which of the first three status bytes' statuses can be put on it; the statuses it
  starts with — the record's first two status bytes, its third less the first bit, which is the
  monster's flying flag and becomes **Float**, and of the fourth byte only Reraise and Float; its
  second and third status bytes kept on it the way equipment keeps statuses, the flying flag again
  as Float; the True Knight cover effect from the fourth byte's first bit; its Metamorph
  information and its traits (dies at 0 MP, not shown in the list of names, human, Imp critical,
  undead);
- its special attack's animation;
- **first strike** (`CheckFirstStrike`): a monster with first strike that is present starts with
  its gauge full and stopped, its advance-wait counter's high byte at 255, in the advance-wait line,
  and the battle menus shut until a battler finishes its last action (`:7423-7426`);
- **vigor: 56 plus the low three bits of one draw** (`:7427-7431`);
- protection from instant death and from poison as for a party member (`FixImmuneStatus`).

### The battle type

`chooseBattleType()` (`ChooseBattleType` `:7556-7578`) starts from the formation's possible types.
A party wearing a **Back Guard** loses pincer attacks, then back attacks, each unless nothing else is
left; a party of **fewer than three** allies loses side attacks, unless nothing else is left. Then
`RandBitWithRate` (`:13525-13548`) with the battle-type row of `kRandomBitRates` (30, 7, 7, 207):
each possible type takes its weight plus one, in the order side, pincer, back, front; one draw
scaled to the total picks the first type whose running total it is below. With all four possible
the bands are side below 31, pincer below 39, back below 47 and front up to 255 — a front attack
four times in five.

`applyBattleType()` (`InitBattleType` `:7584-7666`): anything but a front attack keeps Gau from
appearing. In a front or side attack, unless the battle menus are shut or the formation forbids
it, one draw below **32 (front) or 44 (side)** — twice that with a **Gale Hairpin** — is a
**preemptive strike** (`:7621-7641`). A pincer attack puts every party slot in the front row; a back
attack swaps every party slot's row; both are back attacks for the battle (`:7646-7666`).

### The starting gauges

`fillStartingGauges()` (`InitGauge` `:6228-6299`). Each of the ten slots, from the last to the
first, is given one of the places 0-9 by a draw over the places left. A slot whose gauge is already
set (a first strike) keeps it, and in a final-battle stage after the first no gauge is set at all,
so every battler fills from one step short of full. Otherwise — empty slots included — with the
place *p* and *e* the number of empty slots:

| Side | Front | Back, pincer | Side attack | Preemptive strike |
|---|---|---|---|---|
| Party | random | 8*p* | not set | not set |
| Monsters, and party members fighting for them | random | random | 1 | 1 |

"Random" is speed + random(0 … speed − 1), then + 8*p*, then + 16*e*, stopping at 255 as soon as a
sum passes it. The gauge's meter is that value plus one, or 255; a gauge **not set** fills at the
battler's first update, so after a preemptive strike or in a side attack the party acts first and
the monsters' meters start at 2.

### The opening

The opening message (`:6138-6161`) is a battle message action that interrupts the line: **35** for a
back attack, **37** for a pincer, **36** for a side attack and **34** for a preemptive strike, which
takes the side attack's place; a front attack without one shows none. A formation that forbids
preemptive strikes shows no opening message at all.

## The player's commands

When the player finishes choosing a command, the battle menu adds a `PlayerAction` to
`BattleState::chosenActions`. `takePlayerActions()` (`GetPlayerAction` `:12620-12653`) takes every
one waiting, in the order chosen, on each frame battle time runs — first thing, before the wait-mode
stop (`:2671`). For each: the character joins the advance-wait line, its menu is closed and may not
open again until its gauge fills, and its command, settled as below, is added to its list. X-Magic
adds two actions: the first spell as **Magic**, then the second as X-Magic.

Settling a command (`FixPlayerAttack` `:12686-12785`, `FixPlayerCmd` `:12671-12680`):

- **Summon** of the equipped esper becomes that esper, and the character is marked as having
  summoned (`:12694-12704`).
- **Item** and **Throw** put the item in the character's hand (`:12705-12711`).
- **Slot**: the reel outcome becomes its spell (`kSlotOutcomes`). The two Joker Dooms take their
  side's targets from `kJokerDoomTargets` — the first the party's, the second the monsters' — and
  the party's targets then have every party member fighting for the other side switched; an outcome
  whose spell is none calls a random esper (`executor.chooseRandomEsper`) (`:12712-12731`).
- **Dance** remembers the dance for the dancer and as the last dance chosen, and the dancer's step
  is chosen (`executor.chooseDanceStep`); **Rage** remembers the monster and its attack is chosen
  (`executor.chooseRageAttack`). A dance or rage of none is chosen at random when it is used
  (`:12732-12748`).
- **Blitz**: a blitz the party has not learned becomes none (`:12749-12763`).
- **Summon, Lore, Magitek, Blitz and Bushido** count their attack from a base
  (`kCommandAttackBases`): the attack's place in its own list plus the base, and one that runs past
  the last attack becomes the plain attack, `BATTLE` (`:12764-12774`).
- The commands that **aim again** when they run (`kRetargetCommands`: Bushido, Blitz, Rage, Leap,
  Dance) drop their targets (`:12775-12781`).
- A **confused** or **zombie** character's targets are dropped, for both of X-Magic's spells.

## What the game keeps

`writeBack()` (`UpdateSRAM` `:12092-12179`) runs once a battle has an outcome, and returns a
`BattleResult`:

- For each party slot with a character: a **ghost** that is dead, petrified or a zombie, and anyone
  **Possess** has taken, **leaves the party**. The record keeps the first eight statuses, and
  Float and Interceptor; its HP; and its MP, unless the character has no MP at all.
- The inventory is the battle inventory's 256 slots, an empty slot holding 0.
- In the colosseum, the **wager is given up** — one from every inventory slot holding it, a slot
  reaching 0 emptied — unless the match ran out of time, which forgets the wager (`:12135-12150`,
  `:11972-11976`).
- Doom Gaze's HP is kept in the battle switches.
- The battle switches go back, with how the battle ended: **64** for a loss (`:15590`), **68** for
  running out of time, **69** for a story countdown reaching zero, **71** for Zone Eater
  (`:11900-11914`, `:11975`).
- Shadow walking off takes him out of the party and unequips his esper (`:11990-11997`).
- **The Veldt**: unless the formation is kept off it, and if a monster slot holds a monster
  numbered below 256, the formation the battle began with joins those Gau can find on the Veldt —
  if it is one of the first 512 (`:12163-12178`).

## The final battle's stages

A won stage that has a next stage ends with `FinalBattleNextStage` (above). The stage after it
starts with `startNextFinalBattleStage()` (`BattleEnd_06` `:12204-12239`, then `InitBattle` again
without `InitRAM`):

- The background scroll for the next stage is entry 2*n* of `kFinalBattleScroll`, *n* being the
  stage just won (`CheckFinalBattle` `:12245-12262`).
- A party member who is present and not dead, petrified, a zombie or air-anchored **stands**: it
  keeps its slot and its statuses, less Dance, Rage and Chant. Every other party member makes way,
  and is no longer away from the field or marked as having summoned. Every monster's statuses are
  cleared.
- What lasts the whole final battle is carried on: statuses as above, the Reflect and Freeze timers,
  Mimic's memory, who is away from the field, who has summoned, the morphed battler, the formation
  the battle began with and the characters still waiting their turn.
- Then the stage starts like any battle, with the standing fighters in their slots and the next
  characters in line in the empty ones.

## A battle from a frame tick

`BattleSim` runs one battle from the host's frame. `tick(BattleInput)` adds the player's finished
commands to the battle, runs `tickBattleTime()` with the presentation's `timeRunning`, and — only
when the presentation is idle, no script still playing — one `stepBattle()`. Once the battle has an
outcome, `executor.playBattleEnd` plays out its end, `writeBack()` hands the party back, and
`CloseBattleScene` is requested (`TerminateBattle` `:150-154`); the result stays and nothing more
runs. A won final-battle stage instead requests `ScrollFinalBattleBackground` (`:12235-12236`) and
the next stage starts in its place. Requests come out of `takeRequests()` in the order they were
made.

## The battle switches

`game::BattleSwitches` are the numbered switches and small counters the battle shares with event
scripts and monster AI: switch *n* is bit *n* mod 8 of variable *n* / 8 of twenty. The battle reads
48-55 (the conditional battles), 56 (only magic), 58 (permanent morph), 66 (Morph available), 73
(Gau has joined before) and 75 (Shadow stays); it clears 63, 64, 68 and 71 as it starts and sets
64, 68, 69 or 71 as it ends; variables 10 and 11 hold Doom Gaze's HP.

## Random numbers

`BattleRng` reads the 256-entry RNG table (`rngByte`). `next()` moves the position on one entry, then
reads it (`Rand`); `below(n)` is the entry × *n* / 256, rounded down, and draws even for *n* of 1
(`RandA`); `coin()` is the low bit (`RandCarry`); `pickFrom(set)` counts the set's members, draws
`below(count)`, and takes that member counting the party's slots first (`RandBit`) — an empty set
draws nothing. The position also moves on once per loop pass without drawing.

## Version differences

The control-menu choice when a character's menu opens is the one place the code differs: the
English versions read the control-menu flag and the Japanese version the Chant status (`:2795-2801`).
The monsters' name groups come out differently from the same code, because the English name table's
records are longer (above, "The monsters").
