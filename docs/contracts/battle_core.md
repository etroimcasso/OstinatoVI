# Battle core — behavioral contract

Behavioral specification for the battle core: battle time (the ATB gauges, the advance waits and
the status timers), the lines battlers wait in, the battle loop that takes turns, what happens
after every action, and how a battle ends. All citations point into the pinned reference tree
`original-src/` (everything8215/ff6 @ `1ea47b5`); line numbers are into `src/battle/battle_main.asm`
unless another file is named. This document is the test authority for `tests/test_battle_state.cpp`,
`tests/test_battle_clock.cpp`, `tests/test_action_queue.cpp` and `tests/test_battle_loop.cpp`.

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

## The two clocks

A battle runs on two clocks.

**Battle time** moves on every displayed frame, including the frames of an attack's animation: the
graphics program calls `UpdateBattleTime` from both its own frame wait and the one it uses inside
animations, unless it has stopped time itself (battle events other than Gau's and Wrexsoul's, the
entry animation, the final battle's set pieces). `tickBattleTime(state, timeRunning, executor)` is
called once per displayed frame; `timeRunning` carries the presentation's stop.

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

1. Characters whose menus need redrawing get `RefreshBattleMenu` (this never ends the step).
2. An **interrupting action**.
3. **Gau has just joined** on the Veldt: every living party member on the ground drops what it had
   chosen (`ResetForVeldtGau` `:1663-1673`).
4. A **counterattack** in progress, else the next battler in the counter line.
5. **Monsters that died** and are still standing leave the field.
6. If the monster list changed, run difficulty is worked out again and `RefreshMonsterNames` is
   requested (this never ends the step). The escape may start again.
7. **The battle's end** is checked.
8. The battle forgets who was attacked and whose counter AI ran this pass, and that a counterattack
   was running.
9. The next battler in the **advance-wait line** starts its wait.
10. A battler **mid-turn**, else the next in the **action line**, takes its turn.
11. With nothing in the action line, a battle that was being kept going may end.

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
   marks cleared and its gauge emptied, and its menu is cancelled (below). Its pending updates run
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
may open. Its menu is cancelled if it is leaving, seized or charmed, or is dead, petrified, a zombie,
asleep, confused, berserk, dancing, hidden or raging. Otherwise a battler whose menu may open and
who has nothing chosen stops its gauge and opens its menu.

**Cancelling the menu** (`CancelAction` `:1483-1506`): a battler not hidden, not a party member leaving,
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
  that chance is used up, and with the next entry below **160** (five times in eight) and Gau
  recruited or two or more allies, **Gau joins**: he is set to join the field, fighting for the other
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

## Random numbers

`BattleRng` reads the 256-entry RNG table (`rngByte`). `next()` moves the position on one entry, then
reads it (`Rand`); `below(n)` is the entry × *n* / 256, rounded down, and draws even for *n* of 1
(`RandA`); `coin()` is the low bit (`RandCarry`); `pickFrom(set)` counts the set's members, draws
`below(count)`, and takes that member counting the party's slots first (`RandBit`) — an empty set
draws nothing. The position also moves on once per loop pass without drawing.

## Version differences

The control-menu choice when a character's menu opens is the only version difference here: the
English versions read the control-menu flag and the Japanese version the Chant status (`:2795-2801`).
