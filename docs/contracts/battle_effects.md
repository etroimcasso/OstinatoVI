# Battle special effects — behavioral contract

Behavioral specification for what an attack's special effect does. An attack carries at most one
special effect; the battle works it out twice — once on the attack and its attacker before any target
is checked, and once on each target the attack hits. This contract specifies the first: every effect
that acts on the attack and its attacker. All citations point into the pinned reference tree
`original-src/` (everything8215/ff6 @ `1ea47b5`); line numbers are into `src/battle/battle_main.asm`
unless another file is named, and `ram:` names a line of `notes/battle-ram.txt`. This document is the
test authority for `tests/test_attacker_effects.cpp` and `tests/test_effects_oracle.cpp`.

The attack frame (`docs/contracts/battle_commands.md`) calls the effect at its `AttackHooks` point;
this contract starts at that call. The battle's own hooks are `BattleHooks`
(`src/battle/battle_hooks.h`), one call into `src/battle/attacker_effects.h`. The effect on each
target is still the default hook's, which changes nothing.

Every claim about `DoAttackerEffect` is also checked against the cartridge itself: the routine is run
on the cartridge, hosted on the engine's SNES machine, over the same inputs the port is given
(`tools/damage-trace/damage_oracle.h`).

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| The dispatch | `DoAttackerEffect` `:10118-10130`, `AttackerEffectTbl` `:10938-11026` | `attackerEffect()` and its table in `attacker_effects.cpp` |
| The forty-four effects | `AttackerEffect_01` … `_51` `:10134-10933` | one function each in `attacker_effects.cpp` |
| No one hit | `AttackerEffectMiss` `:10515-10518` | `hitsNoOne()` |
| Hide and die | `_c2384a` `:9037-9052`, `_c2362f` `:8641-8652` | `hideAndDie()` |
| Gil taken from the party | `TakeGil` `:8944-8962` | `takeGil()` |
| A random esper | `RandGenju` `:8968-8975` | `chooseRandomEsper()` (`src/battle/automatic_actions.h`) |
| The params aimed again | `InitGfxParams` `:14428-14466` | `initParams()` (`src/battle/attack.h`) |

## Where the battle calls it

`CalcAttackEffect :8275`, once per attack: after the damage is split between the targets and halved
for the back row, and after the hits in the back are found; before the hit check. The attacker is the
frame's X, moved into Y (`:10123`). `DoAttackerEffect` has no other caller. The port's seam is
`AttackHooks::attackerEffect`, which `BattleHooks` fills with
`attackerEffect(state, attack, attacker, script)`.

Right after it the frame reads the Launcher's or Super Ball's hits to come (`$3405`, `:8276-8279`):
with any to come, the attack stops there and the hits are made by the bounce
(`docs/contracts/battle_damage.md`, § The reflected attack).

## A. The dispatch

`DoAttackerEffect` saves X and the flags, makes both registers a byte wide, moves the attacker into Y
and jumps through `AttackerEffectTbl` by the effect (`$11a9`); the effect returns to it and it
restores. Nothing else.

The effect is held doubled in `$11a9`: an attack's own effect, a weapon's (1-14), an item's (`$49`-
`$4e`, the item's use effect plus `$48`) or a command's (`$50`-`$57`). The port's
`SpecialEffect::index()` is the undoubled value. The table has 88 rows, `$00`-`$57`; every row this
contract does not name is `AttackerEffectNone :10129`, a bare `rts`. Nothing indexes past the table:
the effect space ends at `$57`.

## B. What the effects read and write

| Cell | Port |
|---|---|
| `$11a2` traits, `$11a3` flags 1, `$11a4` flags 2, `$11a6` power, `$11a8` hit rate, `$11a9` the effect | `attack.record`, `attack.effect` |
| `$11af` the attacker's level as the attack loaded it | `attack.attackerLevel` |
| `$11b0-$11b1` the damage | `attack.damage` |
| `$b0.4` the swirl before a spell suppressed | `attack.castingSwirlSuppressed` |
| `$b1.0` a counterattack running, `$b1.2` the party cannot run | `state.counterattackRunning`, `state.cannotRun` |
| `$b2.1` clear: the attack may be a critical hit | `attack.canBeCritical` |
| `$b3.1` clear: the attack is a critical hit | `attack.automaticCritical` |
| `$b5`, `$b6`, `$b7` the animation's command, attack and second value | `attack.command`, `attack.attack`, `attack.secondary` |
| `$b8-$b9` the targets being aimed at | `attack.targets` |
| `$ba.6` a random target | `attack.rules.randomTarget` |
| `$bc` the multiplier, in halves | `attack.multiplier` |
| `$a0.4` the swirl, `$a2-$a3` the targets, `$a4-$a5` the targets hit, `$a6-$a7` the battlers the hits come from | `attack.params` |
| `$3400` the spell cast next, `$3401` the message shown | `scratch.followUpSpell`, `scratch.message` |
| `$3405` the hits to come, less one | `state.extraHits` |
| `$3414` zero: no damage modification | `scratch.damageModification` false |
| `$3a70` the attacks still to make | `scratch.attacksLeft` |
| `$3ec9` the targets the attack was aimed at | `scratch.targetCount` |
| `$3a5a` the battlers shown "Miss" | `scratch.missed` |
| `$3a5c-$3a63` Mind Blast's four picks | `scratch.mindBlastPicks` |
| `$3402`, `$3404` Quick's turns left and its target | `state.quickTurnsLeft`, `state.quickTarget` |
| `$3a36` the Golem's HP | `state.golemHp` |
| `$3a46.4` the turn ends with this action | `state.endTurnAfterAction` |
| `$3a6e` the end-of-battle event, its index doubled | `state.endOfBattleEvent` |
| `$3a76` the party members fighting | `state.allyCount` |
| `$3ec8` the elements Forcefield has nullified | `state.elementsNullifiedByForcefield` |
| `$3f2c` the battlers away from the field | `state.awayFromTheField` |
| `$2f4c-$2f4d` the battlers gone once the action is over | `state.leavingAfterAction` |
| `$2f30` + party slot: the equipment to work out again | `Battler::equipmentChanged` |
| `$2b86` / `$2b9a` + party slot × 5: the battle's copy of each hand | `state.inventory.weaponHands`, `shieldHands` |
| `$3b68` / `$3b69`: each hand's power | `Battler::weaponHand.battlePower`, `shieldHand.battlePower` |
| `$3aa0.0` on the field, `$3aa0.2` its menu open | `Battler::present`, `inBattleMenu` |
| `$3358` the battler held in Seize | `Battler::seizing` |
| `$3c80` a monster's battle flags | `Battler::monsterFlags` |
| `$3d98` a monster's gold | `Battler::gold` |
| `$3bf4`, `$3c1c`, `$3c08` HP, maximum HP, MP; `$3b18` level | `Battler::hp`, `maxHp`, `mp`, `level` |
| `$1860-$1862` the party's gil, `$1866-$1868` its steps (`field-ram:934-936`) | `state.party.gil`, `state.party.steps` |
| `$021e` the console's frame count, low byte | `state.frameCount` |

The console's divide (`Div :11818-11839`) gives 65,535 for a divisor of 0; every effect below that
divides says so where it can happen. Every draw is the battle's random number (`Rand`, `RandA`,
`RandCarry`, `RandBit` `:12367-12401`, `:13495-13519`): `RandA(n)` is a draw scaled by *n* / 256,
`RandCarry` a draw's low bit, and `RandBit` picks one set bit of a mask with `RandA` of the count,
counting from the lowest — no bits set draws nothing.

## C. The effects on the attacker

Y is the attacker throughout.

### The weapons

| Index | Effect | Lines | Rule |
|---|---|---|---|
| `$01` | Thiefknife | `:10136-10146` | On a draw with its low bit clear the effect becomes Steal (`$52`), and an attack shown as Fight is shown as Capture. |
| `$02` | Atma Weapon | `:10451-10457` | Ignores defense; may not be a critical hit. |
| `$05` | Drainer | `:10299-10304` | Inverts on the undead; drains. |
| `$06` | Soul Sabre | `:10289-10292` | Works on MP, then the Drainer's rule. |
| `$07` | Rune Edge, Illumina, Ragnarok, Punisher | `:10229-10251` | Only an attack that may be a critical hit and was aimed at someone: the cost is 12 plus a draw's low three bits. With that much MP the attacker pays it and the attack is a critical hit; without, nothing — the one-in-32 critical stands. |
| `$09` | Dice, Fixed Dice | `:10673-10744` | No damage modification; cannot be dodged. One draw *r*: the first die is (*r* & 15) mod 6 + 1, the second (*r* mod 96) / 16 + 1. A hit rate of 3 or more (Fixed Dice) rolls a third die from the console's frame count, its low byte mod 6, plus 1. The damage is the faces multiplied, by twice the attacker's level, and — with three dice all showing one face — by that face again, each addition stopping at 65,535. The animation's attack is the third face less one (15 for two dice); its second value holds the second face less one in the high four bits and the first face less one in the low four. An attack shown as Fight is shown as the dice roll. |
| `$0a` | Valiant Knife | `:10323-10333` | Ignores defense; the damage gains what the attacker has lost of its HP, its maximum less its HP. The damage word wraps past 65,535. |
| `$0b` | Tempest | `:10339-10346` | On a draw below 128: power 0, Wind Slash cast next, one more attack. |
| `$0c` | Heal Rod | `:10310-10315` | Ignores defense; restores. |
| `$0e` | Ogre Nix | `:10177-10220` | Not during a counterattack and not while the wielder's menu is open: `RandA` of the last digit of the wielder's HP plus one (`Div` by 10 gives its remainder, `:10187-10191`) — a draw of 0 breaks it. The wielder's equipment is to be worked out again; the hand swinging, the weapon hand with an odd count of attacks still to make and the shield hand with an even one, has its power cleared; and if that hand's item in the battle's copy is Ogre Nix it becomes empty with quantity 0 and the message is 68, "Ogre Nix was broken!". Then the `$07` rule, either way. |

### The attacks

| Index | Effect | Lines | Rule |
|---|---|---|---|
| `$0f` | the MP critical at 28 MP | `:10257-10260` | The `$07` rule with a base cost of 28 (`lda #$1c`; the listing's comment says 26). Nothing carries it (§ K). |
| `$11` | Golem | `:10279-10283` | The Golem's HP is the caster's HP. The listing labels it "scan". |
| `$13` | Sonic Dive | `:10763-10780` | The turn ends with this action. Every battler petrified or asleep (a 16-bit read of statuses 1-2, `$8040`) or stopped, frozen or hidden (statuses 3-4, `$2210`) leaves the targets and the targets hit. The index is Palidor's too; Palidor's rule on its targets is the other table's. |
| `$15` | Mantra | `:10837-10850` | Ignores defense, no split, no damage modification; the attacker leaves the targets hit; the damage is the attacker's HP over the targets less one — 65,535 when the attacker was the only one. |
| `$16` | Spiraler | `:10804-10814` | The attacker's bit is read a byte wide, so only a party member's is there: a party member leaves the targets and the targets hit and is gone once the action is over; a monster stays. Either way it is hidden and set to die (§ C.1), its HP and MP 0. |
| `$18`, `$4d` | Warp, Warp Stone | `:10463-10473` | Where the party cannot run: message 10, "Can't escape!!", and no one is hit. Otherwise the end-of-battle event `$02`: the battle ends as when time runs out. |
| `$19` | Exploder | `:10399-10421` | Multiplier 0; the swirl suppressed; no damage modification. The params are aimed at the attacker alone and copied to the script; then aimed at the targets it hit, with the attacker added to them. The damage is the attacker's HP, and the attack's animation is added to the script. The first aim takes the suppression, so the second has the swirl. |
| `$1a` | Blow Fish | `:10602-10609` | Ignores defense, no split, no damage modification; damage 1,000. |
| `$1b` | Pearl Wind | `:10266-10273` | Ignores defense, no split, no damage modification; the damage is the attacker's HP. |
| `$1c` | Reflect??? | `:10907-10916` | Every battler without Reflect leaves the targets hit. |
| `$1d` | L? Pearl | `:10526-10538` | The hit rate is the last digit of the party's gil (two divides by 10 over the three bytes). |
| `$1e` | Step Mine | `:10152-10171` | No damage modification. The party's steps over the power, or 65,535 when the quotient does not fit the damage word (a power of 0 included). |
| `$27` | Escape | `:10544-10551` | A party member: shown as the party running away (`$22`), and the swirl turned **on** (`$a0.4` set — the listing's comment says "disable"). A monster: nothing. |
| `$28` | Mind Blast | `:10647-10655` | For each party position, 3 first: one battler drawn from the targets hit, party members and monsters together. |
| `$29` | N. Cross | `:10661-10667` | One draw's bits leave the party members hit, a second draw's the monsters hit. |
| `$2a` | Flare Star | `:10615-10631` | No damage modification; one target drawn; the damage is the power times that target's level, over the number of targets — 65,535 with none. |
| `$2c` | Launcher | `:10439-10445` | Eight hits to come; the hits come from the attacker. |
| `$30` | Suplex | `:10878-10899` | The swirl suppressed. From the targets, the monsters that cannot be suplexed are dropped — with none left, all of them; one drawn; the params aimed at it. |
| `$31` | Forcefield | `:10573-10584` | With every element nullified, no one is hit. Otherwise one of the rest drawn is nullified for the battle, and the message is 55 plus its bit, "…-element ineffectual", Fire's first. |
| `$32` | Quadra Slam, Quadra Slice | `:10590-10596` | Three more attacks, at random targets; the effect is cleared so they do not run it again. |
| `$33` | Bababreath | `:10479-10503` | Party positions 3 to 0: the last one on the field, dead, petrified or a zombie and not away from it — the lowest — has the params aimed at it alone. None: with two or more party members fighting the attack goes on as aimed; otherwise no one is hit. |
| `$36` | Empowerer | `:10786-10798` | Turns working on MP over. Now on: shown as Mimic, which shows nothing. Now off: one more attack, power a quarter. |
| `$3d` | Revenge | `:10750-10757` | No damage modification; the damage is the HP the attacker has lost. |
| `$41`, `$42` | halve, quarter the damage | `:10869-10872`, `:10858-10861` | `$42` halves and runs into `$41`, which halves again. Nothing carries either (§ K). |
| `$43` | Quick | `:10922-10933` | With someone already taking Quick's turns — a count that has run out to 0 included — the attacker is shown "Miss" and stays among the targets hit. Otherwise it takes two turns. |
| `$44` | Discard | `:10820-10831` | No one is hit; cannot be dodged. If the attacker holds someone in Seize, the params are aimed at that battler alone, so the effect on targets reaches it. |

### The items

| Index | Effect | Lines | Rule |
|---|---|---|---|
| `$49` | Magicite | `:10352-10356` | An esper drawn at random — 25 to choose from, Odin and Raiden passed over — is cast next; one more attack. |
| `$4a` | Super Ball | `:10427-10433` | The animation's attack is `$7d` (the attack animation at Bio Blast's index); one to four hits to come, a draw's low two bits plus one; the hits come from the attacker. |
| `$4b` | Smoke Bomb | `:10559-10567` | Where the party cannot run: no one is hit, the effect is cleared and the message is 9, "Can't run away!!". |
| `$4c` | Elixir, Megalixir | `:10637-10641` | Works on HP; the effect on each target restores the MP. |

### The commands

| Index | Effect | Lines | Rule |
|---|---|---|---|
| `$50` | Possess | `:10509-10512` | A draw of 150 or more hits no one: two draws in five. |
| `$51` | GP Rain | `:10362-10393` | 30 gil for each of the attacker's levels (`$3b18`). A party member takes it from the party's gil (§ C.2); a monster from its own gold, all of it when it has less, nothing with none. Nothing taken: no one is hit and the message is 8, "No money!!". Otherwise the damage is twice what was taken, over the number of targets. |

### C.1 Hide and die

`_c2384a`: Hide is set to go on, then `_c23852` marks the battler to answer itself — unless it already
waits to answer someone — and sets Dead to go on. The mark is left ready by the processor's carry as
the frame leaves it; `UpdateFacingDirection` (`:3218-3271`, called just before the effect) clears it for any attack
that is not physical, and Spiraler is a spell, so its mark is left unready.

### C.2 Gil from the party

`TakeGil`: the gil's low word less the amount, its high byte less the borrow. Without a borrow the
amount is taken. With one, the party had less than the amount: the old low word is what is taken and
`stz $1860; stz $1861`, both two bytes wide, clear all three bytes — the party is left with nothing.
The amount is at most 65,535.

## K. Who carries each effect

Every attack's own effect, every weapon's, every consumable's use effect and every command's, read
from the shipped tables, carry these rows of the table: `$01`, `$02`, `$05`, `$06`, `$07`, `$09`,
`$0a`, `$0b`, `$0c`, `$0e`, `$11`, `$13`, `$15`, `$16`, `$18`, `$19`, `$1a`, `$1b`, `$1c`, `$1d`, `$1e`,
`$27`, `$28`, `$29`, `$2a`, `$2c`, `$30`, `$31`, `$32`, `$33`, `$36`, `$3d`, `$43`, `$44`, `$49`, `$4a`,
`$4b`, `$4c`, `$4d`, `$50`, `$51`. Nothing carries `$0f`, `$41` or `$42`; they act as the table says
all the same.

A weapon's effect comes from a party member's hand, so a weapon row's attacker is always a party
member.
