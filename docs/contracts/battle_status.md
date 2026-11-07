# Battle statuses — behavioral contract

Behavioral specification for how statuses go on the battlers and come off: what an attack's status
bytes do to one target it hits, how the statuses waiting on every battler take effect once the attack
is over, the statuses a battle starts with, and the spells a battler's relics cast when it is near
fatal. All citations point into the pinned reference tree `original-src/` (everything8215/ff6 @
`1ea47b5`); line numbers are into `src/battle/battle_main.asm` unless another file is named, and
`ram:` names a line of `notes/battle-ram.txt`. This document is the test authority for
`tests/test_status_effect.cpp`, `tests/test_status_changes.cpp`, `tests/test_status_oracle.cpp`,
`tests/test_near_fatal.cpp` and `tests/test_status_start.cpp`.

The attack frame (`docs/contracts/battle_commands.md`) calls both at its `AttackHooks` status points;
this contract starts at those calls. The battle's own hooks are `BattleHooks`
(`src/battle/battle_hooks.h`), one call each into `src/battle/status.h`.

The claims about `MagicStatusEffect` and `UpdateStatus` are also checked against the cartridge
itself: the two routines are run on the cartridge, hosted on the engine's SNES machine, over the same
inputs the port is given (`tools/damage-trace/damage_oracle.h`), and every call of either that the
trace tool records replays against the port.

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| An attack's statuses on one target | `MagicStatusEffect` `:11094-11147` | `statusEffect()` |
| What the attack sets and clears | `ApplyStatusMod` `:11169-11201`, `StatusModTbl` `:11206-11209`, `StatusMod_00`–`02` `:11214-11242` | `statusModification()` |
| The statuses taking effect | `UpdateStatus` `:11032-11088` | `applyStatusChanges()` |
| The working sets | `InitStatusVars` `:11260-11313` | the first step of `applyStatusChanges()` |
| What each status does going on and coming off | `SetStatusTbl` `:11661-11693`, `RemoveStatusTbl` `:11696-11728`, the handlers `:11348-11632`, their helpers `:11334-11342`, `:11378-11383`, `:11520-11535`, `:11638-11656` | the two handler tables in `status.cpp` |
| The new statuses | `CalcStatus` `:11153-11163` | `calcStatus()` |
| A Zombie's statuses | `_c24585` `:11319-11328` | the last step of `applyStatusChanges()` |
| The pending statuses emptied | `ResetStatusMod` `:11248-11254` | the same |
| The statuses a party member starts with | `LoadCharProp` `:6632-6659` | `loadPartyMember()` and `buildCommandList()` (`src/battle/party_setup.cpp`) |
| The statuses a monster starts with, and a rager takes on | `LoadRageProp` `:7518-7528` | `loadMonsterTraits()` (`src/battle/battle_start.cpp`), written by `loadMonster()` and Rage |
| The near-fatal relics | `NearFatalEffect` `:1619-1655` | `nearFatalRelicSpells()` |
| Reraise | `ReraiseEffect` `:1217-1225`, from `AfterAction1` `:1104-1109` | `checkTheFallen()` (`src/battle/battle_loop.cpp`) |

## The status words

A battler's statuses are four bytes, `$3ee4-$3ee5` (statuses 1-2, read as one 16-bit word) and
`$3ef8-$3ef9` (statuses 3-4): `Battler::status`. `StatusSet` packs the same four bytes, and `StatusId`
numbers each bit 0-31 in that order, so bit *k* of the first word is `StatusId` *k* and bit *k* of the
second is `StatusId` 16 + *k*.

Beside them sit, per battler:

| Cells | Port | Meaning |
|---|---|---|
| `$3dd4` / `$3de8` | `Battler::statusesToSet` | statuses to put on it when the action's statuses take effect |
| `$3dfc` / `$3e10` | `Battler::statusesToClear` | statuses to take off it then |
| `$331c` / `$3330` | `Battler::inflictableStatuses` | a set bit is a status that **can be put on it** — it is not immune |
| `$3e60` / `$3e74` | `Battler::statusForCounter` | the statuses it will have, taken for a battler waiting to retaliate (§ The statuses for a counterattack) |

## Where the battle calls them

| Routine | Called from | Port seam |
|---|---|---|
| `MagicStatusEffect` | each target the attack hits, `:8356` in the frame and `:8434` in the bounce, after the hit check and before the special effect and the damage | `AttackHooks::statusEffect` → `statusEffect(state, attack, attacker, target)` |
| `UpdateStatus` | `ExecAttack` `:8109`, once per attack, after the attack and its bounce and before the weapon's spell | `AttackHooks::applyPendingStatuses` → `applyStatusChanges(state)` |
| `UpdateStatus` | a battle's start (`:6128`) and a formation change (`:13231`), after the monsters are loaded | `ActionExecutor::applyStatusChanges` → `CommandExecutor::applyStatusChanges` → `applyStatusChanges(state)` |
| `NearFatalEffect` | `AfterAction2` `:1374`, the last thing for each present battler as the battle settles after an action (and at the start, `:6132`) | `ActionExecutor::checkNearFatal` → `CommandExecutor::checkNearFatal` → `nearFatalRelicSpells(state, slot)` |

None of the three routines has any other caller. `ActionExecutor`'s own `applyStatusChanges` and
`checkNearFatal` do nothing, so a battle run on an executor that does not override them puts no status
on and casts no relic spell.

The frame clears the "missed by the effect" cell `$3a48` (`attack.scratch.missedByEffect`) just before
each `MagicStatusEffect` call (`:8355`, `:8432`), runs the special effect after it (`:8357`), and skips
the target's damage when `$3a48` is not zero (`:8358-8359`). A Fight with two hands, or an Offering's
eight, is that many attacks, and each applies its own statuses.

## An attack's statuses on one target — `MagicStatusEffect`

### What the attack sets and clears (`ApplyStatusMod`)

The target's current statuses are loaded (`:11097-11100`) and two empty sets are started: to set and
to clear. The attack's second flags byte (`$11a4`) picks one of three modes by its bits 2 and 3
(`AttackFlag2::REMOVE_STATUS`, `TOGGLE_STATUS`; `and #$0c; lsr` is a table index, `:11172-11175`):

| Flags | Mode | To set | To clear |
|---|---|---|---|
| neither | set (`StatusMod_00`) | the attack's statuses the target does not have | nothing |
| REMOVE | remove (`StatusMod_01`) | nothing | the attack's statuses the target has |
| TOGGLE | toggle (`StatusMod_02`, both of the above) | the attack's statuses the target does not have | the attack's statuses the target has |
| both | — | — | — |

The attack's statuses are `$11aa-$11ad` (`record.statuses`). With both bits set the index is 6, one
past the three-entry table; the original would take the two bytes after it, the opening of
`StatusMod_00`, as an address and jump into unrelated code. No attack in the game carries both: no
record of the 256 does (`TOGGLE_STATUS` is on Imp, Vanish and Imp Song alone), items never toggle, and
the commands that add `REMOVE_STATUS` add it to a record without `TOGGLE_STATUS` (Revert, the status
timers running out). The port handles that case as a toggle.

Then two rules that add to the clears:

1. **A spell takes Vanish off** (`:11182-11190`): unless the attack is physical (`$11a2.0`,
   `AttackTrait::PHYSICAL`), a target with Invisible loses it — unless the attack's own statuses name
   Invisible.
2. **Fire thaws** (`:11191-11199`): an attack whose element includes Fire (`$11a1.0`) takes Frozen off a
   frozen target — unless the attack's own statuses name Frozen.

`statusModification(current, attackStatuses, flags2, physical, fire)` returns both sets. It reads
nothing of the battle, so the field menu's use of an item or spell can use it too.

### What `statusEffect` adds

3. **Vanish does not apply** (`:11103-11106`): when `attack.vanishApplies` is false (`$b3.7` clear — an
   Item, and an attack a battler makes on itself), Invisible is taken back out of the clears, whichever
   rule put it there.
4. **Life kills the undead** (`:11107-11119`): when the target is undead (`$3c95.7`,
   `Battler::undead`), the attack inverts on the undead (`$11a2.3`, `AttackTrait::INVERT_ON_UNDEAD`), is
   in remove mode (`$11a4.2`) and its statuses name Dead or Zombie (`$11aa & $82`), Dead is added to the
   statuses to set — whether the target can be killed or not; the immunity is applied when the
   statuses take effect.
5. **Into the pending statuses** (`:11120-11131`): both sets are added to the target's statuses to set
   and to clear. They are added to, never replaced: everything an attack's earlier targets or hits
   put there stays.
6. **A miss for a status the target is immune to** (`:11132-11145`): when the attack has
   `AttackMiscFlag::MISS_IF_STATUS_IMMUNE` (`$11a7.0`) and nothing in this attack's sets is a status the
   target can be given (`(toSet | toClear) & $331c` is zero for statuses 1-2, and the same with `$3330`
   for 3-4), the target goes into `attack.scratch.missedByEffect` and into `attack.scratch.missed`, and is
   shown "Miss". The sets were already added in step 5. One status that can change is enough for a
   hit.

`statusEffect` draws no random number and does not test the hit rate; the hit check did that. It does
not change `status`: the statuses take effect at the end of the attack.

## The statuses taking effect — `applyStatusChanges`

The pass runs over the battlers **last slot first** (`ldy #$12 … dey2`, `:11036`, `:11084`), skipping any
that is not present (`$3aa0.0`, `Battler::present`). For each one, in order:

### 1. The working sets (`InitStatusVars`)

- **to clear** ← its statuses to clear, as they are;
- **to set** ← its statuses to set that **can be put on it** (`& $331c` / `& $3330`, `:11266-11270`);
- **current** ← its statuses.

Then three additions that do not go through that mask, in this order:

1. **Petrify stays** (`:11272-11275`): a petrified battler has Petrify added to its statuses to set,
   so Petrify's set handler runs for it on every pass.
2. **Near fatal follows its HP** (`:11278-11287`): with `low = maxHp >> 3` (`$3c1c`, 16-bit, three
   shifts), compared with `hp` (`$3bf4`): if it has Near fatal and `low < hp`, Near fatal is added to the
   clears; if it does not and `low >= hp`, Near fatal is added to the sets. At 0 HP the test reads
   `0 >= 0`, so a dead battler is set near fatal.
3. **Overcast dies as a Zombie** (`:11288-11297`): when Dead is to be set (the 16-bit load of `$fb`
   tests bit 7 of `$fc`, the Dead bit of the sets) and the battler's Overcast trigger is on (`$3e4d.1`,
   `triggers.overcast`), Zombie is set in place of Dead and Condemned is added to the clears.

### 2. The statuses for a counterattack (`:11298-11312`)

When the battler is waiting to retaliate (bit 7 of `$32e0`, read as the top bit of the 16-bit load of
`$32df`; `Battler::retaliationPending`), `statusForCounter` ← `calcStatus(current, toSet, toClear)` with
the sets **as they stand at this point**: the clears not yet masked, no handler run and the Zombie rule
of step 6 not applied. The sets themselves are left as they were. Otherwise `statusForCounter` keeps
what it had. A monster's answer to an attack reads it (`ExecAIRetal` `:12481-12493`): with Sleep,
Confuse, Berserk, Frozen or Stop there, the counter script runs in a mode that performs none of its
actions. Every command below `$fc` — every attack, item, event and switch command — is treated as a
condition that failed (`:4370-4374`), so the interpreter skips to the next "end if" or the script's end
(`:4314-4325`); only the conditions and the end commands run. The monster does not answer. Whether a
monster answers can therefore turn on a status the pass then does not give: a clear the
battler is immune to, a status a handler adds or takes away, or a Zombie's statuses (`docs/Bugs.md`, "A
counterattack is decided on statuses the battler never gets").

### 3. The set handlers (`:11041-11058`)

For statuses 1-2 and then 3-4: each status in **to set** runs its set handler, **highest bit first**
(`ldx #$1e; asl $f0; … dex2`). The word is copied before its handlers run, so a status a handler adds
to the sets does not run a handler of its own. A handler may add to the clears; those are taken off,
and run their clear handlers, in step 4.

### 4. The clear handlers (`:11059-11080`)

For statuses 1-2 and then 3-4: **to clear** ← to clear that can be put on it (`& $331c` / `& $3330`), and
**this masked set is the one kept** (`sta $f4` / `sta $f6`). Then each status in it runs its clear
handler, highest bit first.

So a status the battler is immune to is never put on it **and never taken off it**. That is how a
status its equipment keeps on it stays: the equipment's statuses are put on at the battle's start and
then taken out of `inflictableStatuses`.

### 5. The new statuses (`CalcStatus`)

`calcStatus(current, toSet, toClear)` = `(current | toSet) & ~toClear`: what is set is added, then what is
cleared is taken off. A status both set and cleared ends up off.

### 6. A Zombie's statuses (`_c24585`)

When the new statuses include Zombie, Blind, Poison, Near fatal, Berserk, Confuse and Sleep are taken
off (`and #$4dfa`; Condemned, Image, Silence and Sap stay). The result is the battler's `status`.

### 7. The pending statuses emptied (`ResetStatusMod`)

`statusesToSet` and `statusesToClear` become empty.

## What each status does

Each handler is the entry for its `StatusId` in `SetStatusTbl` or `RemoveStatusTbl`. A status with no
entry below does nothing either way. The helpers they share:

- **take off what it rules out** (`_c24598` `:11334-11342`): adds to the clears every status of a given
  set that the battler has or is to be given — `toClear |= (current | toSet) & m`, statuses 1-2 only.
- **back among the standing** (`_c2469c` `:11638-11646`): a **monster** goes into `monstersStanding` and
  out of `monstersFallen`; a party member is unaffected (`$3019` is 0 for one).
- **died this action** (`_c246a9` `:11652-11656`): the battler goes into `diedAwaitingCounter`.
- **statuses changed** (`_45c1` `:11378-11383`): a **party member** goes into `statusChanged`, so its next
  command is aimed again. The battler's bit is loaded into the 8-bit index register from `$3018`, which
  holds only the party members' bits; a monster is never added.
- **to work out again** (`SetCharFlag` `:11520-11523`): flags in `Battler::pendingUpdates` (`$3204`).

| Status | Going on | Coming off |
|---|---|---|
| Zombie | takes off Dead; died this action; statuses changed (`:11348-11352`) | back among the standing; statuses changed (`:11358-11360`) |
| Invisible | shown as vanished (`$2f44`, a mirror of the status for the graphics; no field) | the same |
| Imp | enabled magic and the command list to work out again; then for a party member, its equipment is worked out again (`equipmentChanged`) — Rage's coming-off, which follows (`:11413-11432`) | the same |
| Petrify | died this action; takes off Blind, Poison, Invisible, Near fatal, Image, Silence, Berserk, Confuse, Sap and Sleep (`$fe15`); takes off Dance, Regen, Slow, Haste, Stop, Shell, Protect, Reflect, Rage, Frozen, Morph, Chant and Float (`$9bff` — Reraise, Hide and Interceptor stay); Phantasm's drain ends (`triggers.phantasm`); its menu may not open (`menuMayOpen`); all its actions dropped (`pendingUpdates.removeAllActions`) (`:11461-11476`) | back among the standing; its gauge empties after the action (`emptyGaugeAfterAction`); all its actions dropped (`:11438-11444`) |
| Dead | takes off Petrify and Condemned; Dead is taken out of the clears, so a Dead to set wins over a Dead to clear; then everything Petrify's going on does (`:11450-11454`) | the same as Petrify's coming off |
| Condemned | a condemned count starts (`pendingUpdates.startCondemned`) | the count stops (`.stopCondemned`) |
| Silence | the command list to work out again (`.commandList`) | the same |
| Confuse | shown turned around (`$2f53`, a mirror of the status for the graphics; no field); statuses changed | the same |
| Sap | takes Regen off | — |
| Sleep | the sleep timer starts at `kSleepDuration` (18); its menu may not open; all its actions dropped (`:11482-11489`, then Petrify's last two steps) | its gauge empties after the action (`:11529-11535`) |
| Regen | takes Sap off | — |
| Slow | takes Haste off; its speed to work out again (`.speed`) | its speed to work out again |
| Haste | takes Slow off; its speed to work out again | its speed to work out again |
| Stop | the stop timer starts at `kStopDuration` (18) | — |
| Reflect | the reflect timer starts at `kReflectDuration` (26) | — |
| Rage | — | for a party member, its equipment is worked out again (`:11423-11432`) |
| Frozen | the freeze timer starts at `kFreezeDuration` (34) | — |
| Morph | its menu after Morph or Revert to work out again (`.morph`) | the same |

"Takes off" a status means it is added to the clears whether or not the battler can lose it; the
clears are masked in step 4. Sap's and Regen's, Slow's and Haste's take the other off whether the
battler has it or not; the others take off only what it has or is to be given.

The timers count down in the status timers' steps (`docs/contracts/battle_core.md`, the status
timers); when one runs out the battle takes the status off with an attack of its own.

Because the handlers of one word run highest bit first, their order shows wherever two of them touch
the same status. Dead (bit 7) runs before Zombie (bit 1): a battler given both has Dead taken out of
its clears by Dead's handler and put back by Zombie's, and ends up a Zombie, not dead.

## What the pass never does

It does not change HP: a battler set to die keeps its HP until the battle checks the fallen
(`AfterAction1` `:1082-1083`). It does not stop a gauge or open a menu itself; the loop reads Dead,
`emptyGaugeAfterAction` and `pendingUpdates` after the action. It does not read the attack: an attack's
statuses reach it only through the pending statuses `statusEffect` wrote.

## The statuses a battle starts with

The statuses a battler starts a battle with are written into its **statuses to set** (`$3dd4` /
`$3de8`) — the same cells an attack's statuses wait in — and go on with the pass that follows. There
is no separate cell for them.

- **A party member** (`LoadCharProp` `:6632-6659`): the record's statuses the battle keeps
  (`kRecordStatuses`) together with the statuses its equipment keeps on it; the record's first status
  byte is written whole (`sta $3dd4,x`, `:6641`), so whatever was waiting before is replaced. A party
  member in a battle where Terra stays morphed is also to be set Morph.
- **A monster** (`LoadRageProp` `:7518-7528`): its record's statuses 1-2 and, of statuses 3-4, the
  flying flag as Float and Reraise and Float of the fourth byte (`and #$84fe`). No monster's record
  gives it Reraise, so no monster starts a battle with it.
- **A rager** takes on the monster's statuses the same way, as its Rage command runs; they go on with
  the pass at the end of its attack.

At the start (`:6127-6132`) the monsters are loaded, then the pass runs, then the battle checks the
fallen (`AfterAction1`), the level of the last attack becomes 20 and the battle settles
(`AfterAction2`) — so a party member who starts near fatal has its near-fatal relics' spells waiting
before the first frame. A formation change (`:13230-13233`) loads the new monsters, runs the pass,
checks the fallen and settles. The pass runs for the party too, so a party member's statuses change
only by what is waiting on it, as after any action; nothing of the party is loaded again.

## The near-fatal relics — `nearFatalRelicSpells`

For a battler as the battle settles after an action (`NearFatalEffect` `:1619-1655`):

1. Nothing happens if it is held by a status (`$3aa0.4`, `Battler::heldByStatus` — dead, petrified or
   stopped), is not near fatal (`$3ee5.1`), or has already had its chance this battle (`$3205.1` clear;
   `Battler::nearFatalSpellAvailable` false).
2. The chance is spent (`eor/sta $3205`, `:1628-1629`) — **before** the relics are read. A battler with
   none of the effects below that falls near fatal has no near-fatal spell left for the battle.
3. Its fifth relic effects (`$3c59`, `Battler::relicEffects5`) are read low bit first, and each casts a
   spell on it as an action with no caster, added to the actions that run before anything else
   (`CreateImmediateAction $26`), in this order:

| `RelicEffect5` | Spell |
|---|---|
| `SHELL_WHEN_HP_LOW` (bit 0) | Shell (`$25`) |
| `SAFE_WHEN_HP_LOW` (bit 1) | Safe (`$1c`) |
| `WALL_WHEN_HP_LOW` (bit 2) | Reflect (`$24`) |

A dead battler is near fatal (step 1 of the pass) but held by its status, so it casts nothing. That
the chance is spent before the relics are read could only show if a battler's relics changed during
the battle, and they cannot: the battle's Item menu swaps what is in the hands, not the relics
(`UpdateCharProp` `:5595-5629`).

## Reraise — verified, unchanged

`AfterAction1` (`:1104-1109`) takes a battler that still has Dead once a Zombie's Dead has been dealt
with, and has Reraise (`$3ef9.2`), through `ReraiseEffect` (`:1217-1225`): Reraise comes off, the
monster's bit comes out of `$2f2f` (the monsters standing — the source's "make monster alive" describes
the Life that follows, not the `trb`), and Life (`$30`) is cast on it as an action with no caster.
`checkTheFallen()` (`src/battle/battle_loop.cpp:178-185`) does these three in that order; taking a
party member out of `monstersStanding` changes nothing, as the party member's monster bit (`$3019`) is
0.

## Not here

- The field menu's use of an item or a spell (`CalcMagicEffect` `:11736-11756`) calls `ApplyStatusMod`
  and `CalcStatus` with no battle.
- `$2f44` and `$2f53` are mirrors of Invisible and Confuse for the graphics; what is shown reads
  `status`.
- `diedAwaitingCounter` and `statusChanged` are emptied elsewhere (`ExecAIRetal` `:12475`;
  `docs/contracts/battle_core.md`); the pass only adds to them.
- The special effects that write the pending statuses directly, the status timers' counting and the
  immunity masks themselves are the special effects', the battle core's and the battle start's.
- A petrified battler is marked as having died this action on every pass (step 1 of the pass); whether
  a petrified monster then answers more than the attack that petrified it is decided by the monster AI's
  counterattack step (`ExecAIRetal` `:12475-12492`), which reads that mark.
