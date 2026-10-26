# Aiming

Every attack is aimed twice: once when its command is chosen, so the battle knows whom it is for,
and again on every attack the command makes, because a target may have fallen, vanished or moved
since. Aiming decides who an attack can reach and, when it may reach only one, which one.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_commands.md`](../../contracts/battle_commands.md) § Aiming.

## Files

| File | Holds |
|---|---|
| `include/ostinato/targeting.h` | `Targeting`, `InitialSelection`: the targeting byte of an attack, an item or a command |
| `include/ostinato/command_targeting_init.h` | `CommandTargetingInit`, `CommandAttackLoad`: a command's aiming byte |
| `src/battle/attack.h` | `AimingRules`, and the attack in flight that carries them |
| `src/battle/targeting.h` | `initTargeting()`, `chooseTargets()`, `removeAbsentTargets()`, `coverTarget()`, `kFirstItemUsedAsItself` |

## The two bytes

**The targeting byte** (`Targeting`) is what an attack, an item or a command's record says about its
targets: whether the cursor moves, whether it stays on one side, whom it starts on, whether it can
widen to a whole side, whether it starts on the enemy.

```cpp
Targeting t = getAttackProperties(AttackId::FIRE).targeting;
t.manual();             // the player may move the cursor
t.enemyByDefault();     // it starts on the other side
t.initialSelection();   // InitialSelection::Single, All, Group or Half
t.multiTarget();        // it may be widened to the whole side
t.keepsEveryTarget();   // more than one target stays chosen
t.isSelfOnly();         // the byte is SELF alone: the user and no one else
t.usesAttackDefault();  // a command's MENU byte: the attack's own byte applies
```

**The aiming byte** (`CommandTargetingInit`) belongs to a command and holds rules the targeting byte
cannot say, plus which way the command loads its attack.

```cpp
CommandTargetingInit item = commandTargetingInit(BattleCommandId::ITEM);
item.mayTargetTheFallen();        // true: a Fenix Down reaches the dead
item.keepsTargetsThatAreGone();   // true: an item is never re-aimed
item.attackLoad();                // CommandAttackLoad::Item
```

## Aiming an attack

`initTargeting()` starts the attack's rules and targeting byte from the command and loads the attack
the command makes into the attack in flight. `chooseTargets()` then works from the targets the
attack was given.

```cpp
AttackInFlight attack = startAttack(CommandAction{.command = BattleCommandId::MAGIC,
                                                  .attack = AttackId::FIRE,
                                                  .targets = BattlerSet::of(BattlerSlot::monster(0))});
initTargeting(attack, BattleCommandId::MAGIC, AttackId::FIRE);
chooseTargets(state, attack, BattlerSlot::character(0));
attack.targets;   // whom Fire reaches
```

For Item, Throw and Tools the attack passed in is the item. `initTargeting()` returns `true` when
the item itself is the attack — a consumable, a thrown weapon, a tool — and loads no record;
otherwise the item's spell or the thrown edge's spell is loaded.

`chooseTargets()`, in short:

- drops the targets it cannot reach: off the field, seized, hidden by a monster's AI, away from the
  field while counterattacks are on, and anyone the rules exclude;
- with targets left, keeps them all when the attack may hit them all and picks one otherwise;
- with none left, aims at a whole side — the attacker's own, turned by the enemy-first flag, Charm,
  Confuse or fighting against the party, or both sides for a Zombie or an initial "all" — unless the
  rules keep it aimed at no one;
- keeps a pincer or side attack's attack on one side of it, and an attack aimed at party members and
  monsters at once on one of the two;
- falls back to the command's earlier targets when the rules allow and no one is left;
- then keeps them all or picks one.

`coverTarget()` runs after it, inside the attack frame: on a blow that can be critical, a Love
Token's bodyguard or the True Knight wearer with the most HP steps in front of a Near Fatal ally and
becomes the only target. The params record the ally and every battler who stepped in.

## The aiming rules

`AimingRules` is one bool per rule. The command's aiming byte sets most of them; the attack's own
traits, the command body and the attack frame add the rest.

| Rule | Set by |
|---|---|
| `noCharacterTargets` | the aiming byte, an attack that cannot target the party |
| `randomTarget` | the aiming byte, a random-target attack, an Offering, the randomize cell |
| `fallBackToEarlierTargets` | the aiming byte, the last landing of a continuous jump |
| `reflected` | a spell bounced off Reflect |
| `mayTargetTheFallen` | the aiming byte, a resurrection attack, a reviving item |
| `keepTargetsThatAreGone` | the aiming byte, every attack after a command's first |
| `opposingSideOnly` | the params: every target is on the other side from the attacker |
| `excludeAttacker` | the aiming byte |

## Gotchas

- **A pick draws.** Picking one target draws from the random numbers even when there is one to pick
  from; tests that pin later draws count it.
- **Given targets skip the layout.** Targets that are still reachable are kept or picked from as
  they are; the pincer and side rules apply only when the attack is aimed afresh.
- **The pincer sides and facing are the layout's.** `BattleState::pincerSides` and `facingRight`
  come from where the battlers stand on screen; aiming reads them, it does not set them.
- **An exact aim lasts one aim.** `AttackScratch::aimExactly` skips the reach check once and is
  cleared by it.
- **A target off the field is kept only by the rules.** With `mayTargetTheFallen` set, the dead and
  the absent stay targets; otherwise a dead party member, a dead, petrified or zombie monster, and
  anyone hidden or absent is dropped.

## Where to change

| To change | Edit |
|---|---|
| A command's aiming rules | its row of `kCommandTargetingInit` (`data/battle_commands.h`) |
| Which items cast their spell when used | `kFirstItemUsedAsItself` in `src/battle/targeting.h` |
| What the thrown edges become | `kThrowToolsConversions` (`data/battle_tables.h`) |
| Who can be reached | `maskTargets()` in `src/battle/targeting.cpp` |
| Which side a re-aim goes to | `aimAtSide()` in `src/battle/targeting.cpp` |
| The pincer and side rules | `keepToOneSide()` in `src/battle/targeting.cpp` |
| Who covers whom | `coverTarget()` in `src/battle/targeting.cpp` |
