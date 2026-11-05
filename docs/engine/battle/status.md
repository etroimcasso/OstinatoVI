# Battle statuses

How statuses go on the battlers and come off. An attack never changes a battler's statuses on the
spot: for each target it hits, the attack's statuses are worked out and added to what is waiting on
that target, and once the attack is over every battler's waiting statuses take effect together. Two
free functions do the two halves, and `BattleHooks` calls them from the attack frame's status hooks
([commands.md](commands.md#hooks)). Both are checked against the cartridge's own routines, run on the
engine's SNES machine over the same inputs.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_status.md`](../../contracts/battle_status.md).

## Files

| File | Holds |
|---|---|
| `src/battle/status.h` | `statusModification()`, `calcStatus()`, `statusEffect()`, `applyStatusChanges()`, `StatusChange`, the four durations `kSleepDuration`, `kStopDuration`, `kReflectDuration`, `kFreezeDuration`, and the named sets `kStatusesAZombieCannotHave`, `kStatusesPetrifyTakesOff`, `kStatusesDeadTakesOff` |
| `src/battle/status.cpp` | the two functions, and the two tables of what each status does going on and coming off |
| `src/battle/battle_hooks.h` | `BattleHooks`: `statusEffect` and `applyPendingStatuses` one call each into `status.h` |
| `src/battle/battle_state.h` | `Battler::status`, `statusesToSet`, `statusesToClear`, `inflictableStatuses`, `statusForCounter` |
| `tools/damage-trace/damage_oracle.h` | `DamageOracle::statusChanges()` and `statusEffect()`, the cartridge's routines over a `DamageScenario`; `portStatusChanges()`, `portStatusEffect()`, `differences()`; `drawStatusChangesScenario()`, `drawStatusEffectScenario()` |

## Using the battle's statuses

A battle whose attacks should put statuses on and take them off runs its commands with
`BattleHooks`:

```cpp
#include "battle/battle_hooks.h"
#include "battle/command_executor.h"

battle::BattleHooks hooks;
battle::CommandExecutor executor(hooks, text, sprites);
```

During an attack, `statusEffect` adds to each target's `Battler::statusesToSet` and
`statusesToClear`; the damage adds to them too (a battler brought to 0 HP is to be set Dead). After
the attack, its bounce off Reflect and before a weapon's spell, `applyPendingStatuses` makes them
take effect for every battler on the field and empties them.

## An attack's statuses on one target

```cpp
#include "battle/status.h"

// What an attack with these statuses does to a target that has `current`:
const battle::StatusChange change = battle::statusModification(
    current,                       // the target's statuses
    attack.record.statuses,        // the attack's statuses
    attack.record.flags2,          // REMOVE_STATUS / TOGGLE_STATUS pick the mode
    attack.record.traits.has(AttackTrait::PHYSICAL),
    attack.record.element.has(Element::FIRE));
// change.toSet:   statuses to put on
// change.toClear: statuses to take off
```

The attack's second flags pick what it does with its statuses:

| `flags2` | Puts on | Takes off |
|---|---|---|
| neither | those the target does not have | — |
| `REMOVE_STATUS` | — | those the target has |
| `TOGGLE_STATUS` | those the target does not have | those the target has |

Then two more come off: Invisible, when the attack is not physical, and Frozen, when it is a Fire
attack — each only from a target that has it, and only when the attack's own statuses do not name it.
`statusModification()` reads nothing but its arguments, so it serves an item or spell used with no
battle around it.

`statusEffect(state, attack, attacker, target)` runs it over the attack's record and the target, adds
two rules of the battle's own, and adds the result to the target's pending statuses:

- With `attack.vanishApplies` false — an Item, an attack a battler makes on itself — Invisible does
  not come off, whichever rule would have taken it.
- Life on an undead target is a kill: a remove-status attack that inverts on the undead
  (`AttackTrait::INVERT_ON_UNDEAD`) and names Dead or Zombie sets Dead.
- An attack with `AttackMiscFlag::MISS_IF_STATUS_IMMUNE` misses a target when nothing it would put on
  or take off is a status the target can be given (`Battler::inflictableStatuses`). The target goes
  into `attack.scratch.missedByEffect`, so the frame skips its damage, and into
  `attack.scratch.missed`, so it is shown "Miss".

## The statuses taking effect

`applyStatusChanges(state)` goes over the battlers last slot first, skipping any that is not
present. For each:

1. **What goes on** is its statuses to set that it can be given. Three go on whatever its
   immunities: Petrify again if it is petrified; Near fatal when its HP is at most an eighth of its
   maximum (rounded down), coming off when it is above; and, with its Overcast trigger, Zombie in
   place of a Dead to set, with Condemned coming off.
2. **The statuses for a counterattack.** A battler waiting to retaliate (`retaliationPending`) has
   `statusForCounter` set to the statuses it is to have, worked out at this point — before the
   steps below change anything. Whether it answers the attack is decided on them.
3. **What each status does going on** — statuses 1-2 first, then 3-4, each half highest status first.
4. **What comes off** is its statuses to clear that it can be given — a status it is immune to stays
   on. Then what each status does coming off, in the same order.
5. **The new statuses:** what goes on is added, what comes off is taken away; a Zombie then loses
   `kStatusesAZombieCannotHave`. Its statuses to set and to clear are emptied.

What the statuses do going on and coming off:

| Status | Going on | Coming off |
|---|---|---|
| Zombie | Dead comes off; it has died this action (`diedAwaitingCounter`); a party member's next command is aimed again (`statusChanged`) | a monster is back among `monstersStanding`; a party member is aimed again |
| Imp | its spells and commands are worked out again; a party member's equipment too | the same |
| Petrify | it has died this action; `kStatusesPetrifyTakesOff` comes off, of what it has or is given; Phantasm's drain ends; its menu does not open; its actions are dropped | a monster is back among `monstersStanding`; its gauge empties after the action; its actions are dropped |
| Dead | Petrify and Condemned come off, and a Dead to clear does not; then everything Petrify's going on does | the same as Petrify's |
| Condemned | its count starts | its count stops |
| Silence | its commands are worked out again | the same |
| Confuse | a party member is aimed again | the same |
| Sap / Regen | the other comes off | — |
| Sleep | its timer starts at `kSleepDuration`; its menu does not open; its actions are dropped | its gauge empties after the action |
| Slow / Haste | the other comes off; its speed is worked out again | its speed is worked out again |
| Stop / Reflect / Frozen | its timer starts at `kStopDuration` / `kReflectDuration` / `kFreezeDuration` | — |
| Rage | — | a party member's equipment is worked out again |
| Morph | its menu is worked out again | the same |

"Worked out again" is a flag in `Battler::pendingUpdates` (or `equipmentChanged`) that the battle
acts on once the action is over ([core.md](core.md)).

```cpp
battle::Battler& slime = state.at(battle::BattlerSlot::monster(0));
slime.present = true;
slime.maxHp = slime.hp = 500;
slime.status = StatusSet::of(StatusId::POISON, StatusId::SLOW);
slime.statusesToSet = StatusSet::of(StatusId::HASTE);
slime.statusesToClear = StatusSet::of(StatusId::POISON);

battle::applyStatusChanges(state);
// slime.status: Haste on, Poison and Slow off; slime.pendingUpdates.speed is set
// slime.statusesToSet and statusesToClear are empty
```

## Checking a rule against the cartridge

The damage oracle ([damage.md](damage.md#checking-a-formula-against-the-cartridge)) runs the
cartridge's own two status routines over a `DamageScenario`:

```cpp
#include "damage_oracle.h"

trace::runOnLoop([&] {
    trace::DamageOracle oracle{image};
    oracle.start();

    trace::DamageScenario s;
    battle::Battler& monster = s.state.at(battle::BattlerSlot::monster(0));
    monster.present = true;
    monster.maxHp = monster.hp = 2000;
    monster.statusesToSet = StatusSet::of(StatusId::SLEEP);

    // Every battler's statuses taking effect, both ways, every cell compared.
    const std::string apart =
        trace::differences(trace::portStatusChanges(s), oracle.statusChanges(s));   // ""

    // An attack's statuses on one target, both ways.
    s.attack.record.statuses = StatusSet::of(StatusId::POISON);
    const battle::BattlerSlot target = battle::BattlerSlot::monster(0);
    const std::string hit =
        trace::differences(trace::portStatusEffect(s, target), oracle.statusEffect(s, target));   // ""
});
```

`statusChanges()` returns a `StatusChangesResult` — every battler's statuses, pending statuses,
statuses for a counterattack, what is to be worked out again, menu and gauge flags, Phantasm, the
equipment flag and the four timers, and the battle's died-this-action, statuses-changed, standing and
fallen sets. `statusEffect()` returns a `StatusEffectResult` — the target's pending statuses and the
two miss sets. `drawStatusChangesScenario()` and `drawStatusEffectScenario()` draw scenarios with every
bit of every status set at random from a seeded generator, so a sweep's draw can be run again alone by
its index.

## Gotchas

- **Immunity works both ways.** A status a battler cannot be given never comes off it either; that is
  how the statuses its equipment keeps stay on.
- **A petrified battler is petrified again on every pass.** Each time statuses take effect, it is
  marked as having died this action, its menu is closed and its actions are dropped — not only the
  first time.
- **A dead battler is near fatal.** At 0 HP the eighth-of-maximum test holds.
- **Only party members are aimed again.** `statusChanged` is added to for party members alone; a
  monster's command is never re-aimed for a status change.
- **The statuses for a counterattack are taken early.** They are worked out before immunity holds a
  clear back, before any status does its work and before a Zombie's statuses come off. With Sleep,
  Confuse, Berserk, Frozen or Stop among them a monster does not answer the attack, so whether it
  answers can turn on a status it never gets ([`docs/Bugs.md`](../../Bugs.md)).
- **Order shows.** Within each half the highest status runs first: Dead takes itself out of the
  clears, and Zombie, which runs after it, puts it back, so a battler given both ends up a Zombie.
- **HP is untouched.** A battler set Dead keeps its HP until the battle checks who has fallen.
- **Both status modes together** are handled as a toggle. No attack in the game has them; the oracle's
  draws leave them out, because the cartridge runs unrelated code there.

## Where to change

| To change | Edit |
|---|---|
| What an attack's modes set and clear, Vanish and Frozen coming off | `statusModification()` in `src/battle/status.cpp` |
| Vanish not applying, Life on the undead, the immune miss | `statusEffect()` in `src/battle/status.cpp` |
| Petrify, Near fatal and Overcast before the handlers | `startingWork()` in `src/battle/status.cpp` |
| When the statuses for a counterattack are taken | `takeEffect()` in `src/battle/status.cpp` |
| What a status does going on or coming off | its handler, and its row in `kGoingOn` / `kComingOff`, in `src/battle/status.cpp` |
| How long Sleep, Stop, Reflect and Frozen last | `kSleepDuration`, `kStopDuration`, `kReflectDuration`, `kFreezeDuration` in `src/battle/status.h` |
| What Petrify, Dead and a Zombie take off | `kStatusesPetrifyTakesOff`, `kStatusesDeadTakesOff`, `kStatusesAZombieCannotHave` in `src/battle/status.h` |
| The cells the oracle writes for the status routines | `writeStatusCells()` in `tools/damage-trace/damage_oracle.cpp` |
