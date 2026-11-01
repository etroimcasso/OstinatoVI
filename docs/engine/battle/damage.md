# Battle damage

The numbers an attack does: a spell's and a blow's base damage from the power, the attacker's stat
and level; the attacker's relic bonus; the multiplier that critical hits, Jump and blows in the back
add steps to; and the damage to each target hit, which the target's defenses, stance, statuses and
elements change and which goes into a ledger of what each battler is about to lose and gain. Four
free functions fill four of the attack frame's `AttackHooks` points
([commands.md](commands.md#hooks)). Every number is a whole number of HP or MP, and every formula is
checked against the cartridge's own routines, run on the engine's SNES machine over the same inputs.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_damage.md`](../../contracts/battle_damage.md).

## Files

| File | Holds |
|---|---|
| `src/battle/damage.h` | `magicDamage()`, `physicalDamage()`, `relicBonus()`, `baseDamage()`, `applyMultiplier()`, `scaleDamage()`, `targetDamage()`, `kDamageWordMax`, `kDamageCap` |
| `src/battle/attack.h` | `DamageLedger`, kept as `AttackScratch::ledger` |
| `tools/damage-trace/damage_oracle.h` | `DamageOracle`, the cartridge's routines run over a `DamageScenario`; `runOnLoop()`; `drawScenario()`, `drawTargetScenario()`; `TargetDamageResult`, `portTargetDamage()`, `differences()` |
| `tools/damage-trace/main.cpp` | `ostinato-damage-trace`, the routines watched in a window or checked against the port |

## The formulas

```cpp
#include "battle/damage.h"

// A spell: 4 × power + power × magicStat × level / 32, as the game keeps it.
std::uint16_t spell = battle::magicDamage(/*power=*/150, /*magicStat=*/120, /*level=*/50);

// A blow and its relic bonus together, written to attack.damage. The attack carries the power
// in its record copy and the attacker's vigor or magic stat and level beside it.
battle::baseDamage(state, attack, attacker);

// The multiplier: attack.damage plus half of it per step of attack.multiplier, the steps used up.
battle::scaleDamage(attack);

// The damage to one target, added to attack.scratch.ledger.
battle::targetDamage(state, attack, attacker, target, script);
```

`magicDamage()` takes the three numbers and no battle state, so anything that works a spell out
without a battle around it can call it. `physicalDamage()` reads the attacker's fourth relic effects
from `state` for the Offering and the Genji Glove. `relicBonus()` reads the attacker's first and third
relic effects. `applyMultiplier()` is the multiplier's arithmetic on its own.

The attack frame calls `baseDamage` before it aims the attack and `scaleDamage` after the critical
hit is decided; between them it halves the damage for a split and for a back-row attacker. Then, for
each target hit that the special effect did not miss, highest slot first, it adds a step for a target
hit in the back and calls `targetDamage`. Taking the damage from the battlers and the numerals are
not part of this page.

## The damage to one target

`targetDamage()` works the damage out from `attack.damage` for one target and adds it to the
ledger. It reads the target's defense and magic defense, its Safe, Shell, Morph, Petrify and Zombie,
whether it is Defending or in the back row, whether it is undead, its elements and Forcefield's
(`BattleState::elementsNullifiedByForcefield`); it draws from `state.rng` for the variance and for a
Zombie attacker's blow. It writes the ledger, the target's statuses to set and to clear, its
controller link, and for a drain the drainer's last-attacker mark.

```cpp
battle::BattleScriptBuilder script;
script.addAnimation(battle::animationOf(attack));          // the frame adds the attack's animation first
battle::targetDamage(state, attack, attacker, target, script);

const battle::DamageLedger& ledger = attack.scratch.ledger;
const auto at = static_cast<std::size_t>(target.index());
if (ledger.taken[at]) { /* *ledger.taken[at] HP (or MP) to come off the target */ }
if (ledger.healed[at]) { /* *ledger.healed[at] to go back on */ }
```

In order, as [the contract](../../contracts/battle_damage.md) states each rule:

| Step | What it does |
|---|---|
| The damage | a fraction of the target's HP or MP for `AttackFlag2::FRACTIONAL_DAMAGE`; otherwise `attack.damage` varied by 224 to 255 two-hundred-fifty-sixths and cut by defense, Safe or Shell, Defend, the back row and Morph, unless it ignores defense — all of it only while `attack.scratch.damageModification` holds; a party member hitting a party member halved; the multiplier's remaining steps applied |
| Who gains | a petrified target takes 0; with `AttackTrait::INVERT_ON_UNDEAD` a cure hurts the undead and a drain on the undead or a Zombie runs backward |
| The elements | Forcefield over every element, absorbing, nullifying, halving and weakness, the first that applies |
| The Atma Weapon | the damage scaled by the wielder's level and HP left; from 501 a longer beam, written into `script` |
| A blow | the target woken, its Muddle cleared and its Control broken; a Zombie attacker's blow poisons or blinds one time in sixteen each |
| A drain | capped by the loser's pool and, while `attack.drainCapped` holds, the gainer's room; the drainer marked and given the same amount the other way |

A `DamageLedger` holds, for each of the ten battlers, an optional amount taken and an optional
amount healed. An empty entry is a battler the attack does nothing to on that side; an entry adds up
over the targets of one attack (a drainer's own entry, a second hit on the same battler) and stops
at `kDamageCap`.

## Checking a formula against the cartridge

`DamageOracle` hosts a cartridge image, starts it as far as the game's reset code and parks it in
native mode, binds the five routines where the cartridge holds them, and calls them over a
`DamageScenario` — the port's `BattleState`, `AttackInFlight` and attacker — written into the cells
the routines read. It runs from a tick of a run loop; `runOnLoop()` is one with no window that steps
as fast as the host allows:

```cpp
#include "damage_oracle.h"   // tools/damage-trace

trace::runOnLoop([&] {
    trace::DamageOracle oracle{image};   // a cartridge image, copier header stripped
    oracle.start();
    trace::DamageScenario s;
    s.attack.record.traits.set(AttackTrait::PHYSICAL);
    s.attack.record.power = 100;
    s.attack.attackerStat = 50;
    s.attack.attackerLevel = 10;
    const std::uint16_t theirs = oracle.calcDamage(s);                                   // 187
    const std::uint16_t ours = battle::physicalDamage(s.state, s.attack, s.attacker);    // 187
    for (const trace::WatchedWrite& write : oracle.writes()) { /* $7E:11B0 = 0xBB, $7E:11B1 = 0x00 */ }

    // The damage to one target, both ways, every cell compared.
    s.attack.damage = 1000;
    s.attack.scratch.damageModification = false;
    const battle::BattlerSlot target = battle::BattlerSlot::monster(0);
    const trace::TargetDamageResult guest = oracle.targetDamage(s, target);
    const trace::TargetDamageResult port = trace::portTargetDamage(s, target);
    const std::string parted = trace::differences(port, guest);                          // ""
});
```

`magicDamage()`, `calcDamage()`, `relicBonus()`, `applyMultiplier()` and `targetDamage()` each call
one routine; `writes()` is what the watches saw it store. `targetDamage()` returns a
`TargetDamageResult` — the ledger, every battler's statuses to set and to clear, controller links
and last-attacker marks, the multiplier's steps left, the animation's second value, the second value
the attack's animation was written again with, and the random-number position — and
`portTargetDamage()` builds the same from the port; `differences()` names each cell where they part.
`drawScenario(rng)` and `drawTargetScenario(rng)` draw scenarios within the game's own ranges from a
seeded generator, so a sweep's draw can be run again alone by its index.

From the command line, `ostinato-damage-trace <rom> <dir> --check 2000` runs the base damage, relic
bonus and multiplier sweep and writes every scenario with both answers to `check.log`; without
`--check` it plays the cartridge in a window and logs every read and write the damage routines make
([tools/damage-trace/README.md](../../../tools/damage-trace/README.md)).

## Gotchas

- **Three 16-bit cuts are the game's.** A spell's damage wraps past 65,535 — Ultima at a magic stat of
  140 and level 99 does 32 — a blow's first product by the level is kept to 16 bits, which the
  Guardian at Vector reaches, and an Atma Weapon hit of 42,367 or more at level 99 and full HP wraps
  ([`docs/Bugs.md`](../../Bugs.md)). `magicDamage()`, `physicalDamage()` and `targetDamage()` keep
  them; nothing else in `damage.cpp` cuts.
- **The relic bonus's magic path reads the third relic effects.** One item that raises magic damage
  is `RelicEffect3::RAISE_MAGIC_DAMAGE` (a quarter); two are `RelicEffect1::RAISE_MAGIC_DAMAGE`
  (half). An attack on MP takes the magic path even when it is physical.
- **An attack that ignores defense keeps its steps.** `scaleDamage()` and `targetDamage()` leave
  `attack.multiplier` alone when they do not apply it, so the steps carry to the next place the
  multiplier runs; a Throw turns `ignoreDefenseStopsMultiplier` off and is multiplied anyway.
- **A back-hit step carries when a target's damage leaves it.** The frame adds one step before each
  target hit in the back, and `targetDamage()` uses the steps up only on the modified damage; with
  damage modification off, a fraction, power 0 or an ignored multiplier, the step goes on to the next
  target ([`docs/Bugs.md`](../../Bugs.md)).
- **Ignoring defense skips more than defense.** An attack with `AttackTrait::IGNORE_DEFENSE` is also
  untouched by Safe, Shell, Defend, the back row and Morph; the variance and the halving between
  party members still apply.
- **The ledger is the attack's.** `targetDamage()` adds to `attack.scratch.ledger` and never empties
  it; a caller that runs it for a fresh attack starts from an empty ledger. A fraction counts what
  the ledger already takes from the target.
- **A drain marks its drainer.** A drain that is a blow leaves the drainer waiting to answer its own
  target, so the loop's counter check answers it; a spell drain does not.
- **A monster's blow ignores the relic bits.** The party-member step, the Offering and the Genji
  Glove apply to party slots only; a monster with those bits set in its battler is unaffected.
- **The oracle needs a started machine.** Calling a routine before `start()` throws; `start()` runs
  from a loop's tick.

## Where to change

| To change | Edit |
|---|---|
| A spell's or a blow's formula | `magicDamage()` / `physicalDamage()` in `src/battle/damage.cpp` |
| Which relic bit gives which bonus | `relicBonus()` in `src/battle/damage.cpp` |
| How a step multiplies | `applyMultiplier()` in `src/battle/damage.cpp` |
| The variance, the defenses or the stance halvings | `modifiedDamage()` in `src/battle/damage.cpp` |
| A fraction's base | `fractionalDamage()` in `src/battle/damage.cpp` |
| The elements, the undead and the order they apply in | `targetDamage()` in `src/battle/damage.cpp` |
| The Atma Weapon's strength or beam | `atmaWeapon()` in `src/battle/damage.cpp` |
| A drain's caps or the drainer's mark | `drained()` and `targetDamage()` in `src/battle/damage.cpp` |
| The ledger's cap | `kDamageCap` in `src/battle/damage.h` |
| The cells the oracle writes or watches | `writeScenario()`, `writeTargetCells()` and the watches in `tools/damage-trace/damage_oracle.cpp` |
| What the trace watches, or the routines it spans | `kWatched` and `kTraced` in `tools/damage-trace/main.cpp` |
