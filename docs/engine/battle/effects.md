# Battle special effects

What an attack's special effect does. An attack carries at most one special effect — its own, its
weapon's, its item's or its command's (`SpecialEffect`, [commands.md](commands.md#the-attack-in-flight)) —
and the battle works it out at two points: once on the attack and its attacker, before any target is
checked, and once on each target the attack hits. Each point is a free function; `BattleHooks` calls
them from the attack frame's `attackerEffect` and `targetEffect` hooks
([commands.md](commands.md#hooks)). A party member that takes control of a monster is given the
monster's attacks as its menu, filled by a third function the same hooks call.

Every effect is checked against the cartridge's own routine, run on the engine's SNES machine over the
same inputs.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_effects.md`](../../contracts/battle_effects.md).

## Files

| File | Holds |
|---|---|
| `src/battle/attacker_effects.h` | `attackerEffect()`, `hideAndDie()`, `takeGil()` |
| `src/battle/attacker_effects.cpp` | one function per effect on the attacker, and the table that picks one by the effect's index |
| `src/battle/target_effects.h` | `targetEffect()` |
| `src/battle/target_effects.cpp` | one function per effect on a target, the table that picks one, the instant kill, the level check of Sketch and Control |
| `src/battle/links.h` | `controlMenu()` |
| `src/battle/battle_hooks.h` | `BattleHooks`: `attackerEffect`, `targetEffect` and `controlMenu`, one call each |
| `src/battle/attack.h` | `AttackInFlight`, `AttackScratch` (`mindBlastPicks`, `messageItem`, `messageAmount`, `sketch`), `initParams()`, `markToRetaliate()` |
| `src/battle/battle_script.h` | the battle messages the effects show, `kMessageDoesntHaveAnything` to `kMessageOgreNixBroken`; `BattleScriptBuilder::params()` |
| `src/battle/battle_state.h` | the links (`Battler::controlling`, `controlEnding`, `charming`, `guarding`, `targetingTarget`), `Battler::aiVariable` and `controlMenu`, `ControlMenuEntry`, `BattleState::swallowedByZinger`, `flippedByControl`, `frameCount`, `takenFromTheParty`, and the cells the effects write |
| `src/game/character_record.h` | `Party::gil`, `Party::steps` |
| `tools/damage-trace/damage_oracle.h` | `DamageOracle::attackerEffect()` and `targetEffect()`, the cartridge's routines over a `DamageScenario`; `runAttackerEffect()`, `runTargetEffect()`, the two result readers and `differences()`; `drawAttackerEffectScenario()`, `drawTargetEffectScenario()` |

## Using the effects

A battle whose attacks should work out their special effects runs its commands with `BattleHooks`:

```cpp
#include "battle/battle_hooks.h"
#include "battle/command_executor.h"

battle::BattleHooks hooks;
battle::CommandExecutor executor(hooks, text, sprites);
```

The frame calls `attackerEffect` once per attack, after the damage is split between the targets and
halved for the back row and after the hits in the back are found. What the effect leaves is read by
the rest of the frame: the damage and the multiplier, the targets hit (`attack.params.targetsHit`), a
spell to cast next (`scratch.followUpSpell`) and one more attack to make (`scratch.attacksLeft`), a
message to show after the attack (`scratch.message`), a certain critical hit
(`attack.automaticCritical`). A Launcher's or Super Ball's hits to come (`BattleState::extraHits`)
stop the attack right after the hook; the bounce makes them
([damage.md](damage.md#a-spell-bounced-off-reflect)).

The frame calls `targetEffect` for each target the attack hits, highest slot first, after the attack's
statuses on that target and before its damage; a spell bounced off Reflect calls it again for each
target it bounces to. An effect that misses its target returns `true`: the target is shown "Miss"
and takes no damage from the attack.

Both functions can be called on their own, with the script the effect writes into:

```cpp
#include "battle/attacker_effects.h"
#include "battle/target_effects.h"

battle::BattleScriptBuilder script;
attack.effect = battle::SpecialEffect::of(AttackSpecialEffect::MANTRA);
battle::attackerEffect(state, attack, caster, script);
// attack.damage: the caster's HP over the other targets

attack.effect = battle::SpecialEffect::of(battle::CommandEffect::Steal);
const bool missed = battle::targetEffect(state, attack, thief, monster, script);
// false: Steal never misses. attack.scratch.message is kMessageStole on a success, and
// attack.scratch.messageItem the item now in state.at(thief).itemInHand.
```

An attack with no effect, or with one that acts only at the other point, is left as it is.

A message the effects set is shown after the attack with the item and amount it names: the frame
writes `BattleMessage{.message = *scratch.message, .firstNumber = item, .secondNumber = amount}`
from `scratch.messageItem` and `scratch.messageAmount`.

## What the effects on the attacker do

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

## What the effects on each target do

| Carried by | What it does to the target |
|---|---|
| Scimitar, Zantetsuken | kill one time in four unless stamina saves the target; shown as a spell, and a monster hides as well |
| Striker, Wing Edge, Trump | the same kill, certain on the undead, then Doom's rule |
| Man Eater, Sniper, Hawkeye, Stone | more steps of the multiplier: a human, half the time (a Fight at a floating target thrown for four), a target of the caster's level |
| Scan | the target's level, HP, MP and weaknesses shown, or "Can't probe target!" |
| Metamorph | the monster becomes an item from its pack, at its odds |
| Palidor | carries the target off with a Jump at the front of its actions |
| Tapir | fills a sleeping target's HP and MP |
| Dischord | halves the level, rounding up |
| Pep Up | the caster dies so the target's MP is filled |
| Rippler | swaps the statuses only one of the two has |
| X-Zone, Odin, Raiden, Cleave, Snare | counterattacks off for the action |
| Quake, Magnitude8 and the other ground strikes | a floating target missed |
| Wallchange, Debilitator | one weakness drawn and the rest nullified; one more weakness |
| Escape, Sneeze, Smoke Bomb, Engulf, Bababreath | the target leaves the battle: a party member gone (and engulfed, or taken from the party), a monster fallen |
| Mind Blast, Evil Toot | the waiting statuses emptied, then one of the attack's statuses for each pick that named the target, or one |
| R. Polarity | turns the row over |
| Love Token, Charm, Seize, Discard, Targetting, Zinger | link two battlers: the target takes the caster's hits, is charmed, is held, is let go, is aimed at next, is swallowed |
| Doom | an undead party member is healed instead of killed; an undead monster fades out and comes back whole |
| Overcast, Phantasm | mark the target |
| Stunner | its statuses fail on a draw at or above its hit rate |
| Fallen One | HP 1 |
| Super Ball | 256 to 2,048 damage, drawn for each target |
| Elixir, Megalixir | fill the MP |
| Possess | the possessor and the possessed both hide and die |
| Steal | an item from the target's slots; a monster's steal takes gil |
| Control | takes hold of a monster: the controller chants and its menu offers the monster's attacks |
| Leap | Gau leaps and the battle ends |
| Sketch | catches one of the monster's two sketch attacks |
| Air Anchor | the target dies on its next action |

## The control menu

When Control takes hold, and each time the controlled monster acts, `controlMenu()` fills the
controller's `Battler::controlMenu` from the monster's four control attacks:

```cpp
#include "battle/links.h"

battle::controlMenu(state, controller, monster);
for (const battle::ControlMenuEntry& row : state.at(controller).controlMenu) {
    // row.attack: NONE for an empty slot; row.castable: the monster has the MP it costs;
    // row.targeting: the attack's own targeting
}
```

## Checking an effect against the cartridge

`DamageOracle::attackerEffect()` and `DamageOracle::targetEffect()` run the cartridge's own effect
routines over a `DamageScenario` and read back every cell the effects write; `runAttackerEffect()`
and `runTargetEffect()` run the port's, and `differences()` names each cell where they part:

```cpp
#include "damage_oracle.h"

trace::runOnLoop([&] {
    trace::DamageOracle oracle{image};
    oracle.start();

    trace::DamageScenario s;
    s.attacker = battle::BattlerSlot::character(0);
    s.state.at(s.attacker).present = true;
    s.attack.effect = battle::SpecialEffect::of(AttackSpecialEffect::FALLEN_ONE);
    const battle::BattlerSlot target = battle::BattlerSlot::monster(0);
    s.state.at(target).hp = 900;

    const trace::TargetEffectRun ours = trace::runTargetEffect(s, target);
    const std::string apart =
        trace::differences(trace::targetEffectResult(ours), oracle.targetEffect(s, target));   // ""
});
```

A target-effect scenario holds what the frame leaves for the effect: the target out of
`attack.params.targetsHit`, marked to answer the attacker (`lastAttacker`, `retaliationPending`), and
the command it was hit with in `lastHit.command`. `runTargetEffect()` starts the script with the
attack's own animation, as the frame has it there.

`AttackerEffectResult` and `TargetEffectResult` hold the cells by name: the attack's record, damage,
multiplier, animation values, flags, params and aim; the command's message and what it names, the
spell or sketch to make next, attacks to make and Mind Blast's picks; the battle's sets of battlers
leaving, gone, fallen, engulfed, taken from the party and holding an item, Zinger, the end-of-battle
event and the party's gil; every battler's HP, MP, statuses, links, items to steal, gold, last hit and
last-attacker mark, actions waiting and control menu; the animations, messages, params blocks and
interrupting actions the effect added; and the random-number position.
`drawAttackerEffectScenario()` and `drawTargetEffectScenario()` draw scenarios from a seeded
generator, so a sweep's draw can be run again alone by its index.

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
- **Escape turns the swirl on**, and so does the instant kill: the swirl before a spell plays.
- **Quick's miss is shown on the caster.** With someone already taking Quick's turns — even one whose
  count has just run out — the caster is shown "Miss".
- **Exploder hits its user.** The user is added to the targets hit and takes its own HP as damage.
- **GP Rain with no gil says "No money!!"** and hits no one; with less gil than it asks, it throws all
  there is and the party has none left.
- **Three effects on the attacker have no carrier.** The 28-MP critical and the halved and quartered
  damage act as the original's table says, though nothing in the game uses them.
- **Steal never misses.** A failed steal shows "Couldn't steal!!" or "Doesn't have anything!", and the
  target then remembers the command it was hit with as Fight ([Bugs.md](../../Bugs.md)).
- **A monster's Steal takes its level times 256, plus 20.** GP Rain beside it takes 30 per level
  ([Bugs.md](../../Bugs.md)).
- **Love Token protects the caster.** The target takes the hits aimed at the caster:
  `Battler::bodyguard` is on the caster, `Battler::guarding` on the target.
- **A failed Metamorph still says "Better off {item}!"** The message takes the place of the attack's
  animation before the odds are drawn.
- **Control's Chant is on the status word itself**, not among the statuses waiting to go on; a
  Control that fails its level check misses without a message.
- **A Control link that is ending does not hold the monster.** Another Control can take it.
- **The instant kill hides the attack when it has no one left** — for the attacks of the command that
  come after it (`scratch.hideWithoutTargets`).
- **The US cartridge's Scimitar never kills between a Dragon Horn's landings.** A Japanese cartridge
  (`GameVersion::JP_1_0`) has no such check, and only a US cartridge marks a controller as using the
  control menu.
- **Doom on an undead monster changes the attack's targets** to that monster.
- **A monster with 65,535 MP could cast none of its control attacks**, though none starts with that
  much; no formation forbids Leap either, so it is refused only when Gau fights alone.

## Where to change

| To change | Edit |
|---|---|
| What one effect does | its function in `src/battle/attacker_effects.cpp` or `src/battle/target_effects.cpp` |
| Which function an effect index runs | its row in `kAttackerEffectRows` or `kTargetEffectRows` |
| The MP a critical costs, Ogre Nix's chance, the dice, Quick's turns, GP Rain's gil per level | the named constants beside each function in `src/battle/attacker_effects.cpp` |
| The instant kill's odds, Steal's chance, a monster's steal, the level check | the named constants beside each function in `src/battle/target_effects.cpp` |
| How gil is taken from the party | `takeGil()` in `src/battle/attacker_effects.cpp` |
| The control menu's rows | `controlMenu()` in `src/battle/links.cpp` |
| The messages the effects show | the `kMessage…` constants in `src/battle/battle_script.h` |
| How the frame counts frames | `tickBattleTime()` in `src/battle/battle_clock.cpp` |
| The cells the oracle writes and reads for the effects | `writeEffectCells()`, `writeTargetEffectCells()`, `readAttackerEffectResult()` and `readTargetEffectResult()` in `tools/damage-trace/damage_oracle.cpp` |
