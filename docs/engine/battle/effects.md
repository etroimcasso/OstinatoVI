# Battle special effects

What an attack's special effect does. An attack carries at most one special effect — its own, its
weapon's, its item's or its command's (`SpecialEffect`, [commands.md](commands.md#the-attack-in-flight)) —
and the battle works it out at two points: once on the attack and its attacker, before any target is
checked, and once on each target the attack hits. This page covers the first. A free function works
out the effect on the attacker, and `BattleHooks` calls it from the attack frame's `attackerEffect`
hook ([commands.md](commands.md#hooks)). The effect on each target is still the default hook's, which
changes nothing.

Every effect on the attacker is checked against the cartridge's own routine, run on the engine's SNES
machine over the same inputs.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_effects.md`](../../contracts/battle_effects.md).

## Files

| File | Holds |
|---|---|
| `src/battle/attacker_effects.h` | `attackerEffect()`, `hideAndDie()`, `takeGil()` |
| `src/battle/attacker_effects.cpp` | one function per effect, and the table that picks one by the effect's index |
| `src/battle/battle_hooks.h` | `BattleHooks`: `attackerEffect` one call into `attacker_effects.h` |
| `src/battle/attack.h` | `AttackInFlight`, `AttackScratch::mindBlastPicks`, `initParams()` |
| `src/battle/battle_script.h` | the battle messages the effects show, `kMessageDoesntHaveAnything` to `kMessageOgreNixBroken` |
| `src/battle/battle_state.h` | `BattleState::frameCount`, `takenFromTheParty`, and the cells the effects write |
| `src/game/character_record.h` | `Party::gil`, `Party::steps` |
| `tools/damage-trace/damage_oracle.h` | `DamageOracle::attackerEffect()`, the cartridge's routine over a `DamageScenario`; `runAttackerEffect()`, `attackerEffectResult()`, `differences()`; `drawAttackerEffectScenario()` |

## Using the effects

A battle whose attacks should work out their special effects runs its commands with `BattleHooks`:

```cpp
#include "battle/battle_hooks.h"
#include "battle/command_executor.h"

battle::BattleHooks hooks;
battle::CommandExecutor executor(hooks, text, sprites);
```

The frame calls the hook once per attack, after the damage is split between the targets and halved
for the back row and after the hits in the back are found. What the effect leaves is read by the
rest of the frame: the damage and the multiplier, the targets hit (`attack.params.targetsHit`), a
spell to cast next (`scratch.followUpSpell`) and one more attack to make (`scratch.attacksLeft`), a
message to show after the attack (`scratch.message`), a certain critical hit
(`attack.automaticCritical`). A Launcher's or Super Ball's hits to come (`BattleState::extraHits`)
stop the attack right after the hook; the bounce makes them
([damage.md](damage.md#a-spell-bounced-off-reflect)).

The function can be called on its own, with the script the effect writes into:

```cpp
#include "battle/attacker_effects.h"

battle::BattleScriptBuilder script;
attack.effect = battle::SpecialEffect::of(AttackSpecialEffect::MANTRA);
battle::attackerEffect(state, attack, caster, script);
// attack.damage: the caster's HP over the other targets
// attack.params.targetsHit: the caster is no longer among them
```

An attack with no effect, or with one that acts only on its targets, is left as it is.

## What the effects do

| Carried by | What it does to the attack and its attacker |
|---|---|
| Thiefknife | steals instead of striking on half the draws; a Fight is shown as Capture |
| Atma Weapon, Drainer, Soul Sabre, Heal Rod | change the attack's flags: ignore defense, no critical, drain, work on MP, restore |
| Rune Edge, Illumina, Ragnarok, Punisher | a certain critical hit for 12 to 19 MP, when the attacker has them |
| Dice, Fixed Dice | the damage from two dice, or three — the third read from `BattleState::frameCount` — times twice the attacker's level, and the face again for three of a kind |
| Valiant Knife | the damage gains what the attacker has lost of its HP |
| Tempest | Wind Slash in place of the blow on half the draws |
| Ogre Nix | sometimes breaks: the hand's power goes to 0 and the battle's copy of the hand empties |
| Golem | the Golem's HP is the caster's |
| Sonic Dive | the turn ends with this action; the petrified, asleep, stopped, frozen and hidden are passed over |
| Mantra, Pearl Wind, Blow Fish, Revenge, Step Mine, Flare Star | damage from the caster's HP, a fixed 1,000, the HP lost, the party's steps, a target's level |
| Spiraler | the caster gives up its HP and MP, hides and dies |
| Warp, Warp Stone | the battle ends, unless the party cannot run |
| Exploder | the user's HP to everyone it hit and to itself |
| Reflect???, L? Pearl, N. Cross, Possess | who is hit: only those with Reflect, a level the gil's last digit divides, a random handful kept, two draws in five missing |
| Escape | a party member's is shown as the party running away |
| Mind Blast | picks one battler hit for each party position |
| Launcher, Super Ball | eight hits, or one to four, made from the attacker |
| Suplex, Bababreath, Discard | aim the attack again: a monster that can be thrown, a fallen party member, the battler held in Seize |
| Forcefield | nullifies one more element for the battle |
| Quadra Slam, Quadra Slice | four attacks at random targets |
| Empowerer | on, works on MP and is not shown; off, made again at a quarter of the power |
| Quick | two turns for the caster, unless someone already has them |
| Magicite | an esper drawn at random is cast next |
| Smoke Bomb | does nothing where the party cannot run |
| Elixir, Megalixir | work on HP first |
| GP Rain | throws 30 gil per level, from the party's gil or a monster's gold |

`hideAndDie()` sets a battler to hide and die once the attack's statuses take effect and marks it to
answer itself; Spiraler uses it on its caster. `takeGil()` takes gil from `BattleState::party`, all the
party has when it has less:

```cpp
const std::uint32_t thrown = battle::takeGil(state.party, 30 * level);   // 0 when the party has none
```

## Checking an effect against the cartridge

`DamageOracle::attackerEffect()` runs the cartridge's own effect routine over a `DamageScenario` and
reads back every cell the effects write; `runAttackerEffect()` runs the port's, and `differences()`
names each cell where they part:

```cpp
#include "damage_oracle.h"

trace::runOnLoop([&] {
    trace::DamageOracle oracle{image};
    oracle.start();

    trace::DamageScenario s;
    s.attacker = battle::BattlerSlot::character(0);
    s.state.at(s.attacker).present = true;
    s.state.at(s.attacker).hp = 1500;
    s.attack.effect = battle::SpecialEffect::of(AttackSpecialEffect::PEARL_WIND);

    const trace::AttackerEffectRun ours = trace::runAttackerEffect(s);
    const std::string apart =
        trace::differences(trace::attackerEffectResult(ours), oracle.attackerEffect(s));   // ""
});
```

`AttackerEffectResult` holds the cells by name: the attack's record, damage, animation values, flags,
params and aim; the command's spell to cast next, message, attacks to make and Mind Blast's picks; the
battle's hits to come, Golem, Forcefield, end-of-battle event, Quick, battlers leaving and the turn's
end; every battler's HP, MP, statuses to set, last-attacker mark and gold, a party member's hands and
whether its equipment is to be worked out again; the party's gil; the params blocks and animations the
effect added to the script; and the random-number position. `drawAttackerEffectScenario()` draws
scenarios from a seeded generator, so a sweep's draw can be run again alone by its index.

## Gotchas

- **The Fixed Dice read a clock.** The third die is `BattleState::frameCount`, which counts every
  frame shown since the battle started; two battles fought the same way roll the same.
- **Ogre Nix breaks by the last digit of the HP.** The chance is one in that digit plus one: at an HP
  ending in 0 it always breaks, at one ending in 9 one time in ten. It never breaks while its wielder's
  menu is open or during a counterattack.
- **Ogre Nix breaks the hand by the count of attacks left.** The weapon hand on an odd count, the
  shield hand on an even one — for a Fight that is the hand swinging.
- **A lone Mantra does 65,535**, and so does a Flare Star with no targets: the console's divide by 0.
- **Spiraler only removes a party member.** A monster's own bit is not in the byte it reads, so a
  monster casting it stays among its targets and is not marked as leaving; it still hides and dies.
- **Escape turns the swirl on.** The swirl before a spell plays for a party member's Escape.
- **Quick's miss is shown on the caster.** With someone already taking Quick's turns — even one whose
  count has just run out — the caster is shown "Miss".
- **Exploder hits its user.** The user is added to the targets hit and takes its own HP as damage.
- **GP Rain with no gil says "No money!!"** and hits no one; with less gil than it asks, it throws all
  there is and the party has none left.
- **Three effects have no carrier.** The 28-MP critical and the halved and quartered damage act as
  the original's table says, though nothing in the game uses them.

## Where to change

| To change | Edit |
|---|---|
| What one effect does | its function in `src/battle/attacker_effects.cpp` |
| Which function an effect index runs | its row in `kAttackerEffectRows` in `src/battle/attacker_effects.cpp` |
| The MP a critical costs, Ogre Nix's chance, the dice, Quick's turns, GP Rain's gil per level | the named constants beside each function in `src/battle/attacker_effects.cpp` |
| How gil is taken from the party | `takeGil()` in `src/battle/attacker_effects.cpp` |
| The messages the effects show | the `kMessage…` constants in `src/battle/battle_script.h` |
| How the frame counts frames | `tickBattleTime()` in `src/battle/battle_clock.cpp` |
| The cells the oracle writes and reads for the effect | `writeEffectCells()` and `readAttackerEffectResult()` in `tools/damage-trace/damage_oracle.cpp` |
