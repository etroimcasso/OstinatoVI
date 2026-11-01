# Battle damage

The numbers an attack does and what they do to the battlers: a spell's and a blow's base damage from
the power, the attacker's stat and level; the attacker's relic bonus; the multiplier that critical
hits, Jump and blows in the back add steps to; the damage to each target hit, which the target's
defenses, stance, statuses and elements change and which goes into a ledger of what each battler is
about to lose and gain; and that ledger taken from HP or MP and shown as damage numerals. Five free
functions fill the attack frame's damage hooks ([commands.md](commands.md#hooks)), and `BattleHooks`
is the hooks object that calls them. Every number is a whole number of HP or MP, and every formula is
checked against the cartridge's own routines, run on the engine's SNES machine over the same inputs.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_damage.md`](../../contracts/battle_damage.md).

## Files

| File | Holds |
|---|---|
| `src/battle/damage.h` | `magicDamage()`, `physicalDamage()`, `relicBonus()`, `baseDamage()`, `applyMultiplier()`, `scaleDamage()`, `targetDamage()`, `applyDamage()`, `kDamageWordMax`, `kDamageCap`, `kNumeralsShownTogether` |
| `src/battle/battle_hooks.h` | `BattleHooks`, the battle's `AttackHooks`: each damage hook one call into `damage.h` |
| `src/battle/attack.h` | `DamageLedger`, kept as `AttackScratch::ledger`; `resolveReflectedAttack()`, a spell bounced off Reflect |
| `src/battle/battle_script.h` | `DamageNumbersShown`, `ReflectedAttackShown`, `ScatteredHitsShown` |
| `tools/damage-trace/damage_oracle.h` | `DamageOracle`, the cartridge's routines run over a `DamageScenario`; `runOnLoop()`; the seeded draws; the results and `differences()` for the per-target pass, taking and showing, and the bounce; `DamageTrace`, `parseTrace()` and `DamageOracle::replay()`, a game's damage calls recorded and replayed against the port |
| `tools/damage-trace/main.cpp` | `ostinato-damage-trace`, the routines watched in a window or checked against the port |

## Using the battle's damage

A battle that should do real damage runs its commands with `BattleHooks`:

```cpp
#include "battle/battle_hooks.h"
#include "battle/command_executor.h"

battle::BattleHooks hooks;
battle::CommandExecutor executor(hooks, text, sprites);
```

`BattleHooks` overrides `baseDamage`, `scaleDamage`, `targetDamage` and `applyDamage`, each with one
call into `damage.h`. Its status and special-effect hooks are `AttackHooks`' own, which do nothing,
so the statuses the damage sets and clears — a battler brought to 0 HP is set to die — wait in
`Battler::statusesToSet` and `statusesToClear`. A caller that needs them to take effect overrides
`applyPendingStatuses` on a class derived from `BattleHooks`, as the battle preview does
(`tools/previews/battle/preview_hooks.h`).

## The formulas

```cpp
#include "battle/damage.h"

// A spell: 4 × power + power × magicStat × level / 32, as the game keeps it.
std::uint16_t spell = battle::magicDamage(/*power=*/150, /*magicStat=*/120, /*level=*/50);

// A blow or a spell and its relic bonus together, written to attack.damage. The attack carries the
// power in its record copy and the attacker's vigor or magic stat and level beside it.
battle::baseDamage(state, attack, attacker);

// The multiplier: attack.damage plus half of it per step of attack.multiplier, the steps used up.
battle::scaleDamage(attack);

// The damage to one target, added to attack.scratch.ledger.
battle::targetDamage(state, attack, attacker, target, script);

// Every battler's ledger entries taken from its HP or MP and shown; the ledger empties.
battle::applyDamage(state, attack, attacker, script);
```

`magicDamage()` takes the three numbers and no battle state, so anything that works a spell out
without a battle around it can call it. `physicalDamage()` reads the attacker's fourth relic effects
from `state` for the Offering and the Genji Glove. `relicBonus()` reads the attacker's first and third
relic effects. `applyMultiplier()` is the multiplier's arithmetic on its own.

The attack frame calls `baseDamage` before it aims the attack and `scaleDamage` after the critical
hit is decided; between them it halves the damage for a split and for a back-row attacker. Then, for
each target hit that the special effect did not miss, highest slot first, it adds a step for a target
hit in the back and calls `targetDamage`; then `applyDamage` once.

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

## Taking the damage and showing it

`applyDamage()` runs once per attack, after every target's damage is in the ledger. For each battler
with an entry, last slot first:

| Step | What it does |
|---|---|
| The net | the healed entry less the taken entry; the taken entry is set to 0 for a battler in `state.invincible`, and nothing is taken from anyone while a Golem or Interceptor blocks (`AttackScratch::golemBlocked`, `interceptorBlocked`) |
| The pool | MP for an attack on MP (`AttackFlag1::AFFECT_MP`), else HP; a gain stops at the maximum, a loss at 0; an MP change marks the castable spells to be worked out again |
| Brought down | at 0 HP the battler is set to die unless it is a Zombie, and the weapon's spell is not cast; at 0 MP only a battler that dies at 0 MP (`Battler::diesAtZeroMp`), whose HP becomes 0 too |
| Who did it | a battler that lost anything is marked to answer `attacker` unless it already waits to answer someone; attacked by another battler, it remembers who (`Battler::lastDamagedBy`), and a party member becomes one a Black Belt may counter for |
| The Golem | the battler the Golem blocked for takes its taken entry off `state.golemHp` |

Then the numerals, as `DamageNumbersShown` in `script`: the amount each battler loses, or gains for
one that only gains, and "Miss" over each battler in `attack.scratch.missed` with no entry. A battler
that both loses and gains shows its gain in a second `DamageNumbersShown`. Each waits by its count:
`DamageNumberWait::UntilGone` from `kNumeralsShownTogether` numerals on, `CarryOn` below it.

```cpp
attack.scratch.ledger.taken[static_cast<std::size_t>(foe.index())] = 120;
battle::applyDamage(state, attack, hero, script);
// state.at(foe).hp is 120 lower; script ends with DamageNumbersShown{{foe, 120}}, CarryOn.
```

## A spell bounced off Reflect

`resolveReflectedAttack()` (`src/battle/attack.h`) is the frame's own step after the attack for a
spell with anyone in `attack.params.reflectedOff`. The damage is halved; each battler the spell
bounced off, last slot first, aims it at a random target on the other side from itself and checks it
against that target; a hit is worked out through the hooks with that battler as its attacker. A
Launcher or Super Ball (`AttackScratch::extraHits`) makes its hits the same way, from the attacker,
until the hits run out or no target is left. Then `applyDamage` runs with the caster as the
attacker.

```cpp
attack.params.reflectedOff = battle::BattlerSet::of(foe);   // the spell bounced off the foe
battle::resolveReflectedAttack(state, hooks, script, attack, caster);
// script: ReflectedAttackShown{.bouncedTo[foe] = the battler it went on to, ...}, then the numerals.
```

`ReflectedAttackShown` holds, by the slot of each battler the spell bounced off, the battler it went
on to, the bounces that hit and the party's blocks; `ScatteredHitsShown` holds the attacker and each
hit's target in order, missed hits included.

## Checking a formula against the cartridge

`DamageOracle` hosts a cartridge image, starts it as far as the game's reset code and parks it in
native mode, binds seven routines where the cartridge holds them, and calls them over a
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

    // The ledger taken and shown, both ways.
    s.attack.scratch.ledger.taken[4] = 120;
    const std::string shown = trace::differences(trace::portApplyDamage(s), oracle.applyDamage(s));   // ""
});
```

`magicDamage()`, `calcDamage()`, `relicBonus()`, `applyMultiplier()` and `targetDamage()` each call
one routine; `writes()` is what the watches saw it store. `targetDamage()` returns a
`TargetDamageResult`, `applyDamage()` an `ApplyDamageResult` (every battler's HP, MP, statuses to
set, marks and last damager, the Golem's HP, the ledger and the numerals decoded from the script
queue), and `reflectedAttack()` a `ReflectedAttackResult` (that, the bounce as the script shows it,
the hits to come and the random-number position); `portTargetDamage()`, `portApplyDamage()` and
`portReflectedAttack()` build the same from the port, and `differences()` names each cell where they
part. `drawScenario()`, `drawTargetScenario()`, `drawApplyScenario()` and `drawReflectScenario()`
draw scenarios within the game's own ranges from a seeded generator, so a sweep's draw can be run
again alone by its index.

## Replaying a recorded battle

`ostinato-damage-trace <rom> <dir>` plays the cartridge in a window and logs every call to the base
damage, the per-target pass and taking and showing the damage — the attack's context at each entry
and every cell the routine read and wrote — to `<dir>/damage-trace.log`. `parseTrace()` reads such a
log back into `TracedCall`s, and `DamageOracle::replay()` rebuilds each call as a `DamageScenario`
from the cells it read, works it out with the port and names every cell where the port's answer
parts from what the cartridge wrote:

```cpp
std::ifstream log{"damage-trace.log"};
for (const trace::TracedCall& call : trace::parseTrace(log)) {
    const std::string parted = oracle.replay(call);   // "" when the port agrees
}
```

The attacker comes from the params block the trace logs at each entry (for a bounce, the battler it
bounced off, or for a Launcher's hits the attacker of the block before), and the target from the
battler cells the routine touched. `ostinato-damage-trace <rom> <dir> --replay` does the same from the
command line, and every `.log` under `tests/fixtures/damage-trace/` is replayed by
`tests/test_trace_replay.cpp`. `DamageOracle::trace(true)` logs the oracle's own calls in the same
lines, which is how the replay itself is checked against the cartridge.

From the command line, `ostinato-damage-trace <rom> <dir> --check 2000` runs the base damage, relic
bonus and multiplier sweep and writes every scenario with both answers to `check.log`
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
- **The ledger is the attack's.** `targetDamage()` adds to `attack.scratch.ledger`; `applyDamage()`
  empties it. A fraction counts what the ledger already takes from the target.
- **A drain marks its drainer.** A drain that is a blow leaves the drainer waiting to answer its own
  target, so the loop's counter check answers it; a spell drain does not.
- **"Lost anything" is not "died".** Every battler that loses HP or MP is marked to answer the
  attacker and, for a party member, made one a Black Belt may counter for — whether it is left
  standing or not.
- **A Golem's block is shown but taken from nobody.** While the Golem or Interceptor blocks, the
  numerals still show the damage worked out, nobody's HP changes, and the Golem's HP pays for the
  battler it blocked for.
- **A bounced spell has two attackers.** Its hit record and the target's mark to answer name the
  battler it bounced off; the HP it takes is remembered as the caster's (`Battler::lastDamagedBy`).
- **A Launcher that runs out of targets keeps its count.** `AttackScratch::extraHits` is left as it
  was when no target is left for the next hit; it lasts the command.
- **A monster's blow ignores the relic bits.** The party-member step, the Offering and the Genji
  Glove apply to party slots only; a monster with those bits set in its battler is unaffected.
- **The oracle needs a started machine.** Calling a routine before `start()` throws; `start()` runs
  from a loop's tick.
- **The oracle's bounce leaves out what the hooks leave out.** The cartridge's bounce runs its own
  status and special-effect passes; `reflectedAttack()` is only comparable for a spell those passes
  change nothing for, which is what `drawReflectScenario()` draws.

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
| How HP and MP change, who is set to die, who remembers the attacker | `takeEntries()`, `broughtDown()` and `applyDamage()` in `src/battle/damage.cpp` |
| The numerals: which show, in what order, one at a time or all at once | `applyDamage()` and `kNumeralsShownTogether` in `src/battle/damage.*` |
| How a bounce is aimed, checked and shown | `resolveReflectedAttack()` in `src/battle/attack.cpp` |
| Which hooks the battle plugs in | `BattleHooks` in `src/battle/battle_hooks.*` |
| The cells the oracle writes or watches | `writeScenario()`, `writeTargetCells()`, `writeApplyCells()`, `writeAimCells()` and the watches in `tools/damage-trace/damage_oracle.cpp` |
| What the trace watches, or the routines it spans | `kTracedRanges` and `kTracedRoutines` in `tools/damage-trace/damage_oracle.h` |
| How a recorded call is rebuilt and compared | `readScenario()` and `replay()` in `tools/damage-trace/damage_oracle.cpp` |
