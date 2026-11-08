# Battle special effects — behavioral contract

Behavioral specification for what an attack's special effect does. An attack carries at most one
special effect; the battle works it out twice — once on the attack and its attacker before any target
is checked, and once on each target the attack hits. This contract specifies both; the control menu a
party member is given when Control takes hold; how the battle lets go of the links the effects make —
Control, Zinger, Love Token and Charm; and Morph's meter. All citations point into the pinned reference
tree `original-src/` (everything8215/ff6 @ `1ea47b5`); line numbers are into
`src/battle/battle_main.asm` unless another file is named, and `ram:` names a line of
`notes/battle-ram.txt`. This document is the test authority for `tests/test_attacker_effects.cpp`,
`tests/test_effects_oracle.cpp`, `tests/test_target_effects.cpp`, `tests/test_control_menu.cpp`,
`tests/test_links.cpp` and `tests/test_morph.cpp`.

The attack frame (`docs/contracts/battle_commands.md`) calls the effects at its `AttackHooks`
points; this contract starts at those calls. The battle's own hooks are `BattleHooks`
(`src/battle/battle_hooks.h`): one call into `src/battle/attacker_effects.h` for the effect on the
attacker, one into `src/battle/target_effects.h` for the effect on each target, and one into
`src/battle/links.h` for the control menu.

Every claim about `DoAttackerEffect` and `DoTargetEffect` is also checked against the cartridge
itself: the routine is run on the cartridge, hosted on the engine's SNES machine, over the same inputs
the port is given (`tools/damage-trace/damage_oracle.h`). `CheckSketchHit`, `TakeGil` and
`SetControlCmd` are checked through the effects that call them.

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
| The dispatch on each target | `DoTargetEffect` `:9087-9099`, `TargetEffectTbl` `:10014-10112` | `targetEffect()` and its table in `target_effects.cpp` |
| The forty-six effects on a target | `TargetEffect_03` … `_57` `:9103-10003` | one function each in `target_effects.cpp` |
| The effect missed the target | `_c23b1b` `:9512-9518` | `misses()` |
| The instant kill | `ScimitarEffect` `:9109-9154`, `_c223b2` `:6009-6015` | `instantKill()` |
| One status of the attack's | `_c23bd0` `:9634-9657` | `giveOneStatus()` |
| The level check of Sketch and Control | `CheckSketchHit` `:8914-8936` | `levelCheck()` |
| The control menu | `SetControlCmd` `:8855-8905` | `controlMenu()` (`src/battle/links.h`) |
| The possessor's mark | `_c2361b` `:8619-8631` | `markToRetaliate()` (`src/battle/attack.h`) |
| A Control link marked as ending | `ValidateControl` `:1231-1243` | `endControlIfUnable()` (`src/battle/links.h`) |
| The ending links let go | `RemoveControlTarget` `:1151-1169` | `releaseEndedControl()` |
| One Control link let go | `RemoveControlAttacker` `:1175-1194`, `UpdateSeize` `:1200-1211` | `releaseControl()` |
| Zinger, Love Token and Charm let go | `_c207c8` `:1249-1298`, `RemoveLoveToken` `:1304-1308`, `RemoveCharm` `:1314-1318` | `releaseLinks()` |
| The menu after Morph or Revert | `UpdateMorph` `:1679-1756` | `morphMenuChanged()` (`src/battle/morph.h`) |
| The meter running down | `DecMorphCounter` `:2815-2832` | `runDownMorph()` |
| The supply kept | `SaveMorphCounter` `:1762-1771` | `saveMorphSupply()` |

## Where the battle calls it

`CalcAttackEffect :8275`, once per attack: after the damage is split between the targets and halved
for the back row, and after the hits in the back are found; before the hit check. The attacker is the
frame's X, moved into Y (`:10123`). `DoAttackerEffect` has no other caller. The port's seam is
`AttackHooks::attackerEffect`, which `BattleHooks` fills with
`attackerEffect(state, attack, attacker, script)`.

Right after it the frame reads the Launcher's or Super Ball's hits to come (`$3405`, `:8276-8279`):
with any to come, the attack stops there and the hits are made by the bounce
(`docs/contracts/battle_damage.md`, § The reflected attack).

`DoTargetEffect` is called for each target hit, highest slot first: by the frame at `:8357`, after the
target's statuses (`MagicStatusEffect`), and by the bounce at `:8435` for each battler a spell bounces
to. Y is the target; the attacker is the frame's X, which the dispatch pushes and the effects read
back from the stack (`$05,s`) — in the bounce, the battler the spell bounced off. Before the call the
frame has taken the target out of the targets hit (`:8347`), marked it to answer the attacker and
recorded what hit it (`_c235e3 :8578-8613`), and cleared `$3a48`. After it, `$3a48` still clear puts
the target back among the targets hit and works out its damage (`:8358-8362`); set, the target takes
nothing more from the attack. The port's seam is `AttackHooks::targetEffect`, which `BattleHooks`
fills with `targetEffect(state, attack, attacker, target, script)`; its `true` is `$3a48` set.

When the attacker is controlled (`$32b9,x` holds a live link), the frame fills its controller's menu
again with `SetControlCmd` before the targets (`:8337-8343`): `AttackHooks::controlMenu`, filled by
`controlMenu(state, controller, monster)`.

After every action the battle checks the links and the meter (`docs/contracts/battle_core.md`), through
the `ActionExecutor` members `CommandExecutor` fills:

| Original | Where it runs | Seam | Filled with |
|---|---|---|---|
| `ValidateControl` | `AfterAction1 :1078`, for each battler on the field that can be targeted | `checkControl` | `endControlIfUnable()` |
| `_c207c8` | `AfterAction1 :1089` for a battler Dead, Petrified or a Zombie; `_c2144f :3175` for one leaving the field; `Cmd_2a :5681` for one who ran | `releaseLinks` | `releaseLinks()` |
| `RemoveControlTarget` | `AfterAction1 :1113`, for every battler, the last first | `clearControl` | `releaseEndedControl()` |
| `UpdateMorph` | `AfterAction2 :1366`, for a battler whose menu after Morph or Revert is to be worked out again (`$3204` bit 1) | `refreshMorph` | `morphMenuChanged()` |
| `DecMorphCounter` | every time update, for the morphed battler (`:2687-2689`) | `tickMorph` | `runDownMorph()` |

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
| `$11aa-$11ad` the attack's statuses | `attack.record.statuses` |
| `$3a48` the effect missed the target; `$3a4e` the command's earlier targets | `targetEffect()`'s result; `scratch.earlierTargets` |
| `$341a` negative: counterattacks on; `$341d` negative: an attack with no one left is shown | `state.scratch.countersEnabled`; `scratch.hideWithoutTargets` false |
| `$2f35` the message's item, `$2f38-$2f3a` its amount | `scratch.messageItem`, `scratch.messageAmount` |
| `$3417` the monster a sketch caught; the sketched attack in `$3400` | `scratch.sketch` |
| `$3a39-$3a3a` the party members gone from the battle, then the monsters fallen | `state.partyLeft`, `state.monstersFallen` |
| `$3a88` taken from the party, `$3a8a` engulfed, `$3a8c` holding an item | `state.takenFromTheParty`, `state.engulfed`, `state.holdingItem` |
| `$2f54` the monsters drawn flipped | `state.flippedByControl` |
| `$33f8`, `$33f9` Zinger's monster and the party member it swallowed | `state.heldByZinger`, `state.swallowedByZinger` |
| `$3403` clear: the party member is held in Seize | `Battler::seizedBy` |
| `$2f49.3` the formation forbids Leap | `getFormationAux(state.formation).leapDisabled()` |
| `$3184`, `$3420`, `$3520`, `$3620` the battle's list of actions; `$32cc` each battler's first; `$340a` the first interrupting one | `Battler::actions`, `state.immediateActions` |
| `$32b8`, `$32b9` Control's two links, the top bit set while ending | `Battler::controlling`, `controlledBy`, `controlEnding` |
| `$3359` held in Seize by; `$3394`, `$3395` Charm's two links; `$336c`, `$336d` Love Token's | `Battler::seizedBy`, `charming`, `charmedBy`, `bodyguard`, `guarding` |
| `$32f4` the item it holds, `$32f5` Targetting's target, `$3dac` its AI variable | `Battler::itemInHand`, `targetingTarget`, `aiVariable` |
| `$3308`, `$3309` the rare and common items to steal; `$3c94` the metamorph byte | `Battler::rareSteal`, `commonSteal`, `metamorph` |
| `$1ff9` the monster, a word | `Battler::monster` |
| `$3204` what is to be worked out again; `$3205.2` clear: Air Anchor is on it | `Battler::pendingUpdates`, `airAnchored` |
| `$3aa0.7` its menu may open; `$3aa1.2` instant death cannot reach it, `.5` the back row, `.6` its gauge empties after the action | `Battler::menuMayOpen`, `instantDeathProtected`, `backRow`, `emptyGaugeAfterAction` |
| `$3e4d.0` the control menu is used, `.1` Overcast, `.6` Phantasm | `Battler::triggers` |
| `$3b40` stamina; `$3c95.4` human, `.7` undead | `Battler::stamina`, `human`, `undead` |
| `$3bcc`, `$3bcd`, `$3be0`, `$3be1` absorbed, nullified, weak and halved elements | `Battler::elementsAbsorbed`, `elementsNullified`, `elementsWeak`, `elementsHalved` |
| `$3dd4`, `$3de8`, `$3dfc`, `$3e10` the statuses waiting to go on and come off | `Battler::statusesToSet`, `statusesToClear` |
| `$3d48` the command it was last hit with; `$32e0` its last-attacker mark | `Battler::lastHit.command`, `lastAttacker` and `retaliationPending` |
| `$202e + 12 × position + $30`: a party member's control menu, three bytes a row | `Battler::controlMenu` |

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

## D. The effects on each target

Y is the target and X the attacker throughout. Rows of `TargetEffectTbl` the tables below do not name
are `TargetEffectNone`, a bare `rts`. "Misses" is § D.1; "hides and dies" puts Hide and Dead on to go
on.

### D.1 The miss

`_c23b1b`: the target into `$3a48` (the effect missed it), into `$3a5a` (shown "Miss") and out of the
targets hit. In the port the effect returns `true` and the target is added to `scratch.missed`; the
frame adds it to `scratch.missedByEffect`. Steal never misses: a failed steal shows its message.

### D.2 The weapons

| Index | Effect | Lines | Rule |
|---|---|---|---|
| `$03` | Striker, Wing Edge, Trump | `:9903-9906` | The instant kill (§ D.6) with the X animation (`$7e`) and the undead killed without a draw; then Doom's rule (`$35`), whether or not the kill came. |
| `$04` | Man Eater | `:9160-9166` | A human target (`$3c95.4`): two more steps of the multiplier. |
| `$08` | Sniper, Hawkeye | `:9174-9191` | On a draw with its low bit set: one more step. Then, at a floating target with an attack shown as Fight: three more steps, shown as Throw with the weapon thrown — the attack is the second value less one (a Fight's second value is the weapon plus one) — and the attack's animation is rewritten with it (`_c235bb`). |
| `$0d` | Scimitar, Zantetsuken | `:9105-9154` | The instant kill (§ D.6) with the Scimitar animation (`$ee`), always drawn for. |

### D.3 The attacks

| Index | Effect | Lines | Rule |
|---|---|---|---|
| `$10` | Scan | `:9712-9721` | A monster that cannot be scanned (`$3c80.4`): message 44, "Can't probe target!". Otherwise an interrupting Scan of the target (command `$27`, the target as its attack). |
| `$12` | Metamorph | `:9385-9418` | A party member: misses. Otherwise two draws' low bits pick one of four items in the pack the metamorph byte's low five bits name (the first draw the high bit); the item is the message's item, and message 29, "Better off {item}!", is written in place of the attack's animation (`_c235be`), which is then added again (`_c235ad`) — before the odds are drawn. A draw below the threshold the byte's top three bits name (`MetamorphRateTbl :10008-10009`): the attacker holds the item, a party member joins those holding one, and the target dies. Otherwise: misses. |
| `$13` | Palidor | `:9212-9232` | The target leaves the field once the action is over (`$3204.0`); a Jump with no targets and no MP is put in front of its actions, the first it had after it. |
| `$17` | Tapir | `:9782-9791` | A target that is not asleep: misses. Otherwise its HP and MP are its maximum. |
| `$19` | Exploder | `:9697-9706` | The attacker as its own target is taken out of the targets hit — by a byte-wide read, so a monster's own bit is not there. The frame puts back every target the effect did not miss (§ Where the battle calls it), so the user takes its own damage. |
| `$1c` | Reflect??? | `:9760-9761` | A row of its own that does nothing; the Lore acts on its attacker. |
| `$1f` | Dischord | `:9270-9274` | The level is one more, halved: (level + 1) / 2 in a byte, so 255 gives 0. |
| `$20` | Pep Up | `:9797-9806` | The caster hides and dies, is gone once the action is over (`$2f4c`, a party member or a monster), and its HP and MP are 0; the target's MP is its maximum. |
| `$21` | Rippler | `:9663-9691` | For the statuses only one of the two has: the caster's go onto the target's to-set and the caster's to-clear, the target's onto the caster's to-set and the target's to-clear. The four waiting words are written over, not added to. |
| `$22` | Stone | `:9197-9206` | The attacker's level equal to the target's: 13 plus the compare's carry, fourteen more steps. |
| `$23` | turns counterattacks off | `:9752-9754` | Counterattacks are off for this action (`$341a`). |
| `$25` | misses floating targets | `:9561-9564` | A floating target: misses. |
| `$26` | Wallchange | `:9291-9301` | One element drawn from all eight is the only one the target is weak to; every other is nullified; one of those others, drawn, is absorbed too. Halved elements are left. |
| `$27`, `$38` | Escape, Sneeze | `:9257-9264` | The target is gone once the action is over, and has left the battle: a word-wide write, so a party member joins those gone from the battle (`$3a39`) and a monster those fallen (`$3a3a`). |
| `$28` | Mind Blast | `:9610-9624` | The target's four waiting status words are emptied; then, for each party position 3 to 0 whose pick (§ C `$28`) is the target, one of the attack's statuses (§ D.5). |
| `$2b` | R. Polarity | `:9280-9285` | The target's row turns over. |
| `$2d` | Love Token | `:9888-9895` | `$336c[attacker]` = the target, `$336d[target]` = the attacker. The cover reads `$336c` by the battler being covered (`CoverEffect :2853`), so the target takes the hits aimed at the caster. |
| `$2e` | Seize | `:9812-9833` | The attacker already holds someone, or the target is a monster: misses. Otherwise the two links; the attacker's AI variable gains its top bit; the target is held (`$3403`), its menu may not open, and its actions are dropped (`$3204.6`). |
| `$2f` | Targetting | `:9973-9979` | `$32f5[attacker]` = the target: the monster's script aims its next attack there. |
| `$30` | Suplex | `:9727-9731` | A monster that cannot be suplexed (`$3c80.2`): misses. |
| `$33` | Bababreath | `:9248-9250` | A party member is taken from the party (a byte-wide write); then Escape's rule. |
| `$34` | Charm | `:9767-9776` | The attacker already charms someone: misses. Otherwise `$3394[attacker]` = the target, `$3395[target]` = the attacker. |
| `$35` | Doom | `:9913-9937` | A target that is not undead: nothing. An undead party member: Dead no longer goes on, and its HP is its maximum. An undead monster: Hide goes on, the attack's targets are the monster (`$b8`), and an interrupting exit and entrance (command `$24`) brings it back at full HP (mode 0, from the targets' low byte) faded in checkered (`$0a`). |
| `$37` | Overcast | `:9865-9869` | `$3e4d.1`. |
| `$39` | Engulf | `:9238-9242` | A party member is engulfed (a byte-wide write); then Escape's rule. |
| `$3a` | Zinger | `:9875-9882` | `$33f8` = the attacker, `$33f9` = the target; a monster attacker is gone once the action is over (a byte-wide read of its bit in the monsters' half). |
| `$3b` | Evil Toot | `:9630-9657` | The target's four waiting status words are emptied; one of the attack's statuses (§ D.5). |
| `$3e` | Phantasm | `:9943-9948` | `$3e4d.6`. |
| `$3f` | Stunner | `:9954-9967` | A draw below the hit rate: nothing. Otherwise the attack's statuses are taken out of the target's to-set words. |
| `$40` | Fallen One | `:9985-9990` | HP 1. |
| `$44` | Discard | `:9839-9850` | The attacker's AI variable loses its top bit; both Seize links are cleared, and the target is no longer held. |

### D.4 The items and the commands

| Index | Effect | Lines | Rule |
|---|---|---|---|
| `$4a` | Super Ball | `:9996-10003` | The damage is a draw's low three bits plus one, times 256 — drawn for each target. |
| `$4b` | Smoke Bomb | `:9257-9264` | Escape's rule. |
| `$4c` | Elixir, Megalixir | `:9856-9859` | The target's castable spells are worked out again (`$3204.7`); its MP is its maximum. |
| `$50` | Possess | `:9593-9604` | A party member attacker is gone once the action is over and taken from the party (byte-wide writes). The attacker hides and dies, then the target does. Then `_c2361b` marks the attacker to answer the target — always if it is not waiting to answer someone, on a draw's low bit if it is. |
| `$52` | Steal | `:9307-9379` | Message 1, "Doesn't have anything!", to start. **A party member:** nothing in either slot fails; otherwise message 2, "Couldn't steal!!". The chance is 50 plus the attacker's level less the target's: a thief whose level and the 50 pass a byte, or a chance of 128 or more, steals for certain; below 0 fails without a draw; a Sneak Ring (`$3c45.0`) doubles it, and `RandA(100)` at or above it fails. A draw below 32 takes the rare slot, any other the common one; an empty slot fails. Success: the item is the message's item, the attacker holds it and joins those holding one, both slots are emptied, message 3, "Stole {item}". **A monster:** message 2; a draw of 192 or more fails; message 1; the amount is the monster's level times 256 plus 20 (`lda $3b18,x; xba; lda #$14`), taken from the party's gil (§ C.2); nothing taken fails; otherwise it is the message's amount, the monster's gold gains it (stopping at 65,535), message 63, "{amount} GP was stolen!!". **Every failure** writes Fight over the command the target was last hit with (`$3d48`, `:9353-9356`). |
| `$53` | Control | `:9470-9506` | A party member, a monster that cannot be controlled (`$3c80.7`), one that is Dead, Petrified, Invisible, a Zombie, asleep, confused, berserk, raging, hidden or morphed, or one held by a live link (`$32b9` with its top bit clear): message 4, "Can't control!!", and misses. A link that is ending does not refuse it. Then the level check (§ E.1) with the Coronet (`$3c45.3`); failing it misses with no message. Success: the two links (both live); the target is drawn flipped (`$2f54`); a US cartridge marks the attacker as using the control menu (`$3e4d.0`); the attacker is put in Chant **on its status word itself** (`$3ef9,x`), not through the statuses waiting to go on; the target's gauge empties after the action; the control menu is filled (§ E.2). |
| `$54` | Leap | `:9570-9587` | The formation forbids Leap (`$2f49.3`), or fewer than two party members fight: message 5, "Can't dive!!", and misses. Otherwise the attacker hides, and the battle ends with Gau leaping (`$3a6e` = 4). No formation in the game forbids it (§ K). |
| `$55` | Sketch | `:9524-9555` | A party member: misses, no message. A monster that cannot be sketched (`$3c80.5`): message 31, "Can't sketch!!", and misses. The level check (§ E.1) with the Beret (`$3c45.2`); failing it misses. Success: the monster is the one sketched (`$3417`); the animation's second value is its position, and the attack's animation is rewritten (`_c235bb`); a draw of 64 or more picks its second sketch attack, any other its first, into `$3400`. |
| `$56` | Debilitator | `:9433-9464` | The elements the target is not weak to and Forcefield has not nullified; none: misses. One drawn: message 11 plus its bit, "Now weak against fire!" first; the target is weak to it and no longer halves, absorbs or nullifies it. |
| `$57` | Air Anchor | `:9737-9746` | A target instant death cannot reach: misses, no message. Otherwise message 19, "Move, and you're dust!", Air Anchor is on it, and counterattacks are off for this action. |

### D.5 One of the attack's statuses

`_c23bd0`: the attack's statuses 1-2 and 3-4 are counted; `RandA` of the total at or above the count
of 1-2 picks the 3-4 word, otherwise the 1-2 word; `RandBit` picks one status of that word, which goes
on the target's to-set word. An attack with no statuses draws once and gives none.

### D.6 The instant kill

`ScimitarEffect`, with a carry and an animation: a target instant death cannot reach (`$3aa1.2`) —
nothing, and nothing is drawn. The X weapons (carry clear) kill the undead without a draw. Otherwise a
draw below 64 is needed, and the stamina save (`_c223b2`: a draw's low seven bits at or below the
target's stamina) saves it. A US cartridge then refuses during a Dragon Horn's later landings (attacks
still to make while shown as Jump, `:9123-9129`); a Japanese cartridge has no such check. The kill:
the target back among the targets hit and out of the command's earlier targets; the swirl before a
spell turned **on** (`$a0.4` set); Dead goes on; the attack is hidden when it has no one left
(`stz $341d`, read by the frame for the attacks after this one, `:8297-8303`); power 0; shown as Magic
casting the animation's attack; a monster struck by the Scimitar animation hides as well; the
animation is added to the script and the params copied after it.

### D.7 The deaths the effects set

Pep Up's and Possess's dying battlers are set to hide and die with `_c2384a`, which also marks Y — the
target — to answer the dying one unless it already waits to answer someone. Every target hit was
marked by the frame before its effect (`_c235e3`), so it always waits, and that mark is left as it is.

## E. The helpers

### E.1 The level check

`CheckSketchHit`: the target's level, times 170 / 256 with the relic; the attacker's level times 256
divided by that (the console's divide: 65,535 for 0). A quotient of 256 or more succeeds without a
draw; below, a draw under the quotient succeeds.

### E.2 The control menu

`SetControlCmd`, into the controller's menu (`CmdPropPtrs :13873-13874` + `$30`: four rows of three
bytes), from the monster's four control attacks (`MonsterControl`): an empty slot (`$ff`) is a row
that cannot be chosen, with no targeting. Otherwise the attack, a byte whose top bit is set when it
cannot be cast, and the attack's targeting byte (`MagicProp + 0`). The comparison adds one to the
monster's MP first: an attack is castable when its MP cost (`MagicProp + 5`) is at most the MP, except
that a monster with 65,535 MP wraps to 0 and can cast nothing. No monster starts with 65,535 MP
(§ K). The byte's low bits are not read by the menu.

## F. Control's release

A Control link is held in two cells per battler: `$32b8` names the monster a party member controls and
`$32b9` the party member controlling a monster, `$ff` for none. The top bit of either marks the link as
ending. The port holds the link as `Battler::controlling` and `Battler::controlledBy` and the mark as
`Battler::controlEnding`; the mark is set and acted on within one pass after an action, so one flag
stands for both of a battler's cells.

### F.1 A link marked as ending

`ValidateControl`, for each battler on the field that can be targeted, after an action: a battler with
any of Dead, Petrify, Zombie, Berserk, Confuse, Sleep (statuses 1-2), Dance, Stop, Rage or Frozen
(statuses 3-4) has the top bit set in both its cells (`asl / sec / ror`, `:1237-1242`). `CheckStatus
:14523-14537` sets the carry when none of the statuses is set, and the routine returns on it. A cell
holding `$ff` stays `$ff`, so a battler with no link is left as it is. Any other status leaves the link
live.

### F.2 The ending links let go

`RemoveControlTarget`, for every battler from slot 9 down to slot 0, after the pass above: a cell that is
not `$ff` and has its top bit set names a link to let go — `$32b9` (the battler is the monster) with the
controller it names, then `$32b8` (the battler is the controller) with the monster it names. The test is
`cmp #$ff; beq; bpl` (`:1153-1155`, `:1160-1162`): after the compare the sign is that of the cell less
`$ff`, which for a slot index is negative exactly when the top bit is set — the listing's comment
"branch if msb set" has it the wrong way round. A live link is left alone.

### F.3 One link let go

`RemoveControlAttacker`, with Y the controller and X the monster:

1. A US cartridge takes the controller off the control menu (`$3e4d.0`, `:1176-1180`); its menu offers
   its own commands again. A Japanese cartridge has no such flag: its menu tests Chant instead
   (`:2795-2801`).
2. The controller stops chanting, straight off its status word (`$3ef9,y`, `:1181-1183`), not through
   the statuses waiting to come off.
3. Both cells of the link are `$ff` (`:1184-1186`); the monster is no longer drawn flipped (`$2f54`).
4. `UpdateSeize` for the monster and then the controller (`:1200-1211`): each one's gauge empties once
   the action is over (`$3aa1.6`), every action it had waiting is dropped (`$3204.6`), and its menu may
   not open (`$3aa0.7`). The routine is named for Seize, but Control's release is its only caller.

## G. The links let go

`_c207c8`, for a battler that died, was petrified or turned into a zombie (`AfterAction1 :1084-1089`),
left the field (`:3175`) or ran (`:5681`):

- **Zinger.** When the battler is the party member Zinger swallowed (`$33f9`, `:1251-1252`), two actions
  that interrupt the line are added for the monster that swallowed it (`$33f8`), both command `$24`: the
  monster leaves the field without letting the battle end (mode `$04`, entrance `$00`, at once), then comes
  back with the HP it had (mode `$02`, entrance `$08`, fading in from the top). Both cells are `$ff`.
- **Love Token.** `$336c` names the battler the battler's hits go to and `$336d` the battler whose hits
  it takes. `RemoveLoveToken` with X the battler and Y its `$336c`: X's `$336c` and Y's `$336d` are `$ff`
  (`:1271-1274`, `:1304-1308`); then with X its `$336d` and Y the battler (`:1275-1281`). The far end's
  cell is cleared whoever it names now: a bodyguard that has since taken another Love Token stops
  guarding that one too.
- **Charm.** The same for `$3394` (the battler it charms) and `$3395` (the battler charming it), through
  `RemoveCharm` (`:1282-1292`, `:1314-1318`).
- **Quick** is let go here as well (`:1293-1297`); the battle core does that (`docs/contracts/battle_core.md`).

## H. Morph

The meter is `$3f30`, a 16-bit counter, full at 65,535; `$3f32` is the speed it runs down by; `$3ee2` is
the morphed battler (`$ff`: none); `$3b04` is the meter shown, its high byte; `$1cf6` is the party's
Morph supply, kept between battles. The port holds them as `BattleState::morphCounter`,
`morphCounterSpeed` and `morphedBattler`, `Battler::morphGauge` and `game::Party::morphSupply`.

### H.1 The menu after Morph or Revert

`UpdateMorph`, for a battler whose menu after Morph or Revert is to be worked out again:

1. Unless a counterattack is running (`$b1.0`), a battler whose gauge is full (`$3219` = 0) stops its gauge
   and may open its menu (`$3aa0.3` and `.7`, `:1680-1686`).
2. Morph and Revert trade places in its four menu rows (`$202e`, three bytes a row, `:1688-1708`):
   now morphed (status 4, `$08`), a Morph row becomes Revert; otherwise a Revert row becomes Morph.
3. **Now reverted** (`:1714-1726`): the supply is kept (H.3), no one is morphed, the meter shown is 0, a
   party member's commands are worked out again (`UpdateCmdList`), and the meter is 0.
4. **Now morphed** (`:1729-1756`): nothing more when someone is morphed already (`$3ee2` valid) or the
   battle keeps Terra morphed throughout (`$3ebb.2`, battle switch 58). Otherwise the battler is the
   morphed one; the meter is full; and its speed is 65,535 divided by the supply (`Div`), shifted right 3
   bits — 4 when Morph lasts twice as long (`$3ebb.1`, battle switch 57) — at most `$7ff`, plus 1.
   `Div` saves and restores the flags around the divide, so the carry it is tested on (`:1745`) is
   still `$3ebb.1`, moved there by the `ror`/`asl` before it (`:1733-1737`): set gives the fourth shift
   and the slower meter. The listing's comment on the branch has it the wrong way round.

The supply is at least 16 whenever the meter starts: the Morph command is offered only then (§ "The
command menu" of `docs/contracts/battle_commands.md`), no attack puts Morph on, and a battle that keeps
Terra morphed fills the supply and starts no meter. The fastest meter, at a supply of 16, runs down by
512, so the `$7ff` cap is never reached.

Neither switch changes during a battle: no monster script sets or clears battle switches 56-63, so the
port reads them as the battle starts (`BattleState::permanentMorph`, `morphLastsTwiceAsLong`).

### H.2 The meter running down

`DecMorphCounter`, on every time update, for the morphed battler: the meter less the speed, 16 bits
(`:2816-2820`). With no borrow, the meter shown is its high byte. A borrow runs it out (`:2823-2829`): its
high byte is cleared; the supply is kept (H.3) — reading that cleared byte, so the supply is 0; no one is
morphed; and a Revert (command `$04`) is added as the battler's counterattack (`CreateRetalAction`). The
meter shown is then 0. The meter's low byte keeps its wrap, but nothing reads the meter again before
Morph sets it full or a revert clears it, so the port leaves it 0. The counterattack's attack and targets
are whatever an earlier action left in `$3a7b` and `$b8`; Revert reads neither and acts on its user, so
the port gives it the battler as its target.

### H.3 The supply kept

`SaveMorphCounter`: with someone morphed, the supply becomes the supply times the meter's high byte, over
256 (`MultAB`, the product's high byte, `:1763-1770`). With no one morphed, nothing changes. The battle's
end calls it too (`:12064`).

### H.4 The final battle's stages

A stage of the final battle starts without clearing the battle's cells, so the morphed battler, the meter
and its speed carry into the next stage, as the statuses do (`:6097-6106` asks for the morphed battler's
menu to be worked out again, which finds it morphed already and changes nothing).

## I. Shipped with the commands

Umaro's three attacks, a desperation attack and the cover were ported with the commands
(`docs/contracts/battle_commands.md`); read again here against `src/battle/commands.cpp` and
`src/battle/targeting.cpp`:

- **`UmaroAttack_02 :3503-3512`**, the tackle (`umaroTackle()`): `InitUmaroAttack :3829-3841` loads the
  plain hit with the power of both hands, at most 255, Umaro's level and his vigor (the doubled value);
  ignores defense; may be critical; shown as animation `$23`. Matches.
- **`UmaroAttack_00 :3518-3575`**, the throw (`umaroThrow()`): the living party members but Umaro; a
  hidden one is dropped; the highest slot asleep or confused becomes the only one (the loop runs from slot
  3 down and skips slots no longer in the set); none left: the tackle. Mog (actor `$0a`) is thrown for an
  automatic critical; the thrown member's hands' power plus the compare's carry — one more for Mog and
  every actor after him (`:3550-3556`) — at most 255, plus Umaro's, at most 255 (an overflow of the first
  sum gives `$fe` with the carry set, which the second add carries to 255); shown as animation `$24`;
  ignores defense; may be critical; never aimed at Umaro (`$ba.0`); the member's Sleep and Confuse are to
  come off (`:3571-3574`). Matches.
- **`UmaroAttack_01 :3581-3584`** and **`DesperationAttack :3591-3594`**: Storm (`$54`) with its targets
  drawn at random (`$3415` = 0), and a desperation attack, each cast as Magic (`$b5` = 2) under the
  attack's own name: `castAsSpell()`. Matches.
- **`CoverEffect :2838-2888`** and **`CheckCoverTarget :2914-2939`** (`coverTarget()`): the cover's
  checks read `$32b8,x` and `$3358,x` (`:2920-2923`), and the covered target's `$3358,y` (`:2863-2864`),
  with the accumulator 16 bits wide (`longa :2916`; `CoverEffect` is entered wide), so each `bpl` tests the
  byte after: `$32b9` (the battler is controlled) and `$3359` (the battler is held in Seize). A battler
  steps in only when no one controls it and no one holds it; the target is covered only when no one holds
  it. `coverTarget()` reads the other cell of each pair — whether the battler controls a monster, and
  whether it holds someone in Seize. This part does not match, and is recorded here for its own change.

## K. Who carries each effect

Every attack's own effect, every weapon's, every consumable's use effect and every command's, read
from the shipped tables, carry these rows of the table: `$01`, `$02`, `$05`, `$06`, `$07`, `$09`,
`$0a`, `$0b`, `$0c`, `$0e`, `$11`, `$13`, `$15`, `$16`, `$18`, `$19`, `$1a`, `$1b`, `$1c`, `$1d`, `$1e`,
`$27`, `$28`, `$29`, `$2a`, `$2c`, `$30`, `$31`, `$32`, `$33`, `$36`, `$3d`, `$43`, `$44`, `$49`, `$4a`,
`$4b`, `$4c`, `$4d`, `$50`, `$51`. Nothing carries `$0f`, `$41` or `$42`; they act as the table says
all the same.

A weapon's effect comes from a party member's hand, so a weapon row's attacker is always a party
member.

Every row of the table on each target has a carrier: `$03`, `$04`, `$08`, `$0d`, `$10`, `$12`, `$13`,
`$17`, `$19`, `$1c`, `$1f`, `$20`, `$21`, `$22`, `$23`, `$25`, `$26`, `$27`, `$28`, `$2b`, `$2d`, `$2e`,
`$2f`, `$30`, `$33`, `$34`, `$35`, `$37`, `$38`, `$39`, `$3a`, `$3b`, `$3e`, `$3f`, `$40`, `$44`, `$4a`,
`$4b`, `$4c`, `$50`, `$52`, `$53`, `$54`, `$55`, `$56`, `$57`. Two branches are never reached with the
game's own data: no formation of the 576 forbids Leap, and none of the 384 monsters starts with
65,535 MP. Control is a party member's command, so its attacker is always a party member — the menu
it fills is a party member's.
