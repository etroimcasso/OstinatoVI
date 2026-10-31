# Battle damage

The numbers an attack does: a spell's and a blow's base damage from the power, the attacker's stat
and level; the attacker's relic bonus; and the multiplier that critical hits, Jump and blows in the
back add steps to. Three free functions fill three of the attack frame's `AttackHooks` points
([commands.md](commands.md#hooks)). Every number is a whole number of HP or MP, and every formula is
checked against the cartridge's own routines, run on the engine's SNES machine over the same inputs.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_damage.md`](../../contracts/battle_damage.md).

## Files

| File | Holds |
|---|---|
| `src/battle/damage.h` | `magicDamage()`, `physicalDamage()`, `relicBonus()`, `baseDamage()`, `applyMultiplier()`, `scaleDamage()`, `kDamageWordMax` |
| `tools/damage-trace/damage_oracle.h` | `DamageOracle`, the cartridge's routines run over a `DamageScenario`; `runOnLoop()`; `drawScenario()` |
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
```

`magicDamage()` takes the three numbers and no battle state, so anything that works a spell out
without a battle around it can call it. `physicalDamage()` reads the attacker's fourth relic effects
from `state` for the Offering and the Genji Glove. `relicBonus()` reads the attacker's first and third
relic effects. `applyMultiplier()` is the multiplier's arithmetic on its own.

The attack frame calls `baseDamage` before it aims the attack and `scaleDamage` after the critical
hit is decided; between them it halves the damage for a split and for a back-row attacker and adds a
step for each target hit in the back. The damage to one target, applying it and the numerals are
not part of this page yet.

## Checking a formula against the cartridge

`DamageOracle` hosts a cartridge image, starts it as far as the game's reset code and parks it in
native mode, binds the four routines where the cartridge holds them, and calls them over a
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
});
```

`magicDamage()`, `calcDamage()`, `relicBonus()` and `applyMultiplier()` each call one routine;
`writes()` is what the watches saw it store. `drawScenario(rng)` draws a scenario within the game's
own ranges from a seeded generator, so a sweep's draw can be run again alone by its index.

From the command line, `ostinato-damage-trace <rom> <dir> --check 2000` runs the sweep and writes
every scenario with both answers to `check.log`; without `--check` it plays the cartridge in a window
and logs every read and write the damage routines make ([tools/damage-trace/README.md](../../../tools/damage-trace/README.md)).

## Gotchas

- **Two 16-bit cuts are the game's.** A spell's damage wraps past 65,535 — Ultima at a magic stat of
  140 and level 99 does 32 — and a blow's first product by the level is kept to 16 bits, which the
  Guardian at Vector reaches ([`docs/Bugs.md`](../../Bugs.md)). `magicDamage()` and `physicalDamage()`
  keep them; nothing else in `damage.cpp` cuts.
- **The relic bonus's magic path reads the third relic effects.** One item that raises magic damage
  is `RelicEffect3::RAISE_MAGIC_DAMAGE` (a quarter); two are `RelicEffect1::RAISE_MAGIC_DAMAGE`
  (half). An attack on MP takes the magic path even when it is physical.
- **An attack that ignores defense keeps its steps.** `scaleDamage()` leaves `attack.multiplier`
  alone when it does not apply it, so the steps carry to the next place the multiplier runs; a Throw
  turns `ignoreDefenseStopsMultiplier` off and is multiplied anyway.
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
| The cells the oracle writes or watches | `writeScenario()` and the watches in `tools/damage-trace/damage_oracle.cpp` |
| What the trace watches, or the routines it spans | `kWatched` and `kTraced` in `tools/damage-trace/main.cpp` |
