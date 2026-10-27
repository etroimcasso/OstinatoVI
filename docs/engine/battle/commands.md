# Battle commands

`CommandExecutor` is the battle's `ActionExecutor`: when the battle core runs a command, it carries
the command out and returns the battle script that shows it. Each command body loads the attack its
command makes into an `AttackInFlight` and hands it to the attack frame, which aims it, checks it
against every target, works out its effects and writes it into the script.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_commands.md`](../../contracts/battle_commands.md).

## Files

| File | Holds |
|---|---|
| `src/battle/command_executor.h` | `CommandExecutor` |
| `src/battle/commands.h` | `fightAttack()`, `loadWeaponAttack()`, `initAttacker()`, `attackerLevel()`, `commandForAttack()`, `itemStatuses()` |
| `src/battle/attack.h` | `AttackInFlight`, `AttackScratch`, `AimingRules`, `SpecialEffect`, `CommandEffect`, `AttackNameKind`, `AttackHooks`, `startAttack()`, `loadAttackRecord()`, `rememberTargets()`, `runAttack()`, `runSelfAttack()`, `resolveAttack()`, `checkHit()`, `HitResult`, `hitInTheBack()`, `recordLastHit()` |
| `src/battle/battle_script.h` | `BattleScriptCommand` and its alternatives, `AttackAnimationParams`, `SpecialAnimation`, `BlockShown`, `BattleScriptBuilder` |
| `src/battle/targeting.h` | aiming ([targeting.md](targeting.md)) |

## Running commands

```cpp
AttackHooks hooks;                 // the damage, status and effect work plugs in here
CommandExecutor executor(hooks);
BattleSim battle(start, party, switches, cartridge.text, cartridge.monsterSprites, executor);
```

`execute()` runs Fight and Capture; a command with no body yet runs as no action and shows nothing.
After a Fight it reports to the battle core whether the attack was physical and who it ended up
hitting (`state.scratch.physical`, `state.scratch.finalTargets`). `aim()` aims a command chosen with
no targets: Fight chosen by a berserk party member, say, gets one monster.

## The attack in flight

`startAttack()` turns a chosen command into an `AttackInFlight`: the command and attack as chosen,
the targets, the MP cost. The body then loads the attack itself — `initTargeting()` loads the
command's record, `loadWeaponAttack()` puts a hand's weapon on it — and edits the copy as the command
requires. The shipped record is never changed.

| Field | Is |
|---|---|
| `command`, `attack` | what the attack is shown as and what it is; a body may change both |
| `queuedCommand`, `queuedAttack` | the command and attack as the player or the AI chose them |
| `record` | the attack's record, as this attack has changed it |
| `effect` | the special effect it carries, whatever it came from |
| `attackerStat`, `attackerLevel` | the stat and level the damage is worked out from |
| `rules`, `targeting`, `targets` | how it is aimed and whom it reaches |
| `multiplier`, `extraMultiplier` | damage steps, each adding half the damage again; the command's own steps start every attack's count |
| `damage` | the damage before it is split and multiplied |
| the flags | whether it can be critical, Vanish applies, the row counts, a target that is gone is replaced, and so on |
| `params` | what the animation is told |
| `scratch` | what the command keeps between its attacks |

`SpecialEffect` puts an attack's, a weapon's, an item's and a command's effects in one space:

```cpp
attack.effect = SpecialEffect::of(CommandEffect::Steal);
attack.effect = SpecialEffect::of(hand.blockEffect.weaponEffect());
attack.effect.isNone();
```

## The attack frame

`runAttack()` runs the command's attacks — `scratch.attacksLeft` more after the first — and
`resolveAttack()` one of them. A Fight is two attacks, one per hand; with an Offering it is eight. A
hand with no battle power makes no attack.

```cpp
AttackInFlight attack = startAttack(command);
BattleScriptBuilder script;
fightAttack(state, hooks, script, attack, actor);
std::vector<BattleScriptCommand> commands = script.take();
```

On the way an attack can:

- **fizzle** — a spell with an MP cost cast by a Muted caster, or by an Imp casting anything but Imp,
  shows the caster going back to standing and nothing else;
- **run short of MP** — shown as nothing, with the "not enough MP" message;
- **be drawn in by Runic** — every battler with Runic up takes it as an MP restore of its cost shared
  between them, after the Runic animation;
- **kill its maker** — a battler Air Anchor marked dies after its next attack;
- **be a critical hit** — one blow in 32 aimed at the other side, an automatic critical, or an Imp
  with the Imp critical flag; two damage steps and the screen flash;
- **cast the weapon's spell** — one time in four, as one more attack after the hit;
- **be learned** — Strago learns a learnable lore he sees used.

`checkHit()` is the hit check for one target. It returns `HitResult{hit, block, reflected}` and does
the rest itself: it sets Vanish or Image to clear, notes the Golem or Interceptor stepping in, takes
away criticals and the weapon's spell after a miss and records the miss.

```cpp
HitResult result = checkHit(state, attack, attacker, target);
if (!result.hit && result.block != BlockShown::None) { /* the block animation shows */ }
```

`recordLastHit()` writes what a hit target remembers — who hit it with which command, spell, item
and elements (`Battler::lastHit`) — and marks it to answer the attacker (`lastAttacker`,
`retaliationPending`).

## Hooks

`AttackHooks` is where damage, statuses and the special effects plug in. Each member is called at
its point in the attack and does nothing by default, so an attack run with plain `AttackHooks`
reaches, hits and misses its targets and is shown, but changes no HP or status.

| Member | Called |
|---|---|
| `baseDamage` | before the attack is aimed; set `attack.damage` |
| `attackerEffect` | after the damage is split, before the hit check |
| `scaleDamage` | after the critical hit is decided; multiply by `attack.multiplier` |
| `controlMenu` | after a controlled monster attacks, for its controller |
| `statusEffect` | for each target hit |
| `targetEffect` | for each target hit, after `statusEffect`; return `true` if the effect missed it |
| `targetDamage` | for each target hit that the effect did not miss |
| `applyDamage` | once, after every target; write the damage numbers into the script |
| `reflectedAttack` | after a spell bounced off anyone |
| `applyPendingStatuses` | after each attack |

```cpp
struct TenEach : AttackHooks {
    void targetDamage(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target) override {
        Battler& hit = state.at(target);
        hit.hp = hit.hp > 10 ? hit.hp - 10 : 0;
    }
};
```

## The battle script

A body writes its script into a `BattleScriptBuilder`. Animations take params — attacker, targets,
targets hit, blocks, the covered ally — written separately, in the order the attack works them out;
`take()` pairs each animation with its params.

```cpp
script.add(AttackNameShown{.attack = AttackId::FIRE});
script.addAnimation(AttackAnimation{.what = BattleCommandId::MAGIC, .attack = AttackId::FIRE});
script.addParams(attack.params);
script.replaceAnimation(changed);   // writes over the last animation added
```

The commands:

| Command | Shows |
|---|---|
| `AttackNameShown`, `BushidoNameShown`, `ItemNameShown`, `EsperNameShown`, `CommandNameShown`, `MonsterSpecialAttackNameShown` | the name in the window at the top of the screen |
| `AttackAnimation` | the command's animation (`what` a `BattleCommandId`) or a special one (`SpecialAnimation`), with its params; a Fight swing says its hand and its weapon's animation |
| `BattleMessage` | a battle message, with the numbers it shows |
| `PoseReset` | a party member going back to standing |
| `ReadyStance`, `StepBack`, `FinalBattleStageArrives` | written by the battle core |

An attack shown as `kNothingShown` (Mimic's command, whose animation is empty) shows no animation.
`kJokerDoomName` is the attack-name entry that reads "Joker Doom".

## Fight

`fightAttack()` makes the two attacks (eight with an Offering) at the targets the command was aimed
at, criticals allowed. Each attack loads one hand with `loadWeaponAttack()`: the weapon hand first,
then the shield hand. A monster fights with one hand.

## Gotchas

- **The record is a copy.** Change `attack.record`, never the shipped table; the next command loads
  it fresh.
- **Params belong to the animation written before them.** An attack writes its animation before it
  knows whom it hit; add the params afterward and `take()` pairs them.
- **Every miss turns criticals off.** A miss on one target takes away the critical hit and the
  weapon's spell for the rest of the attack, so the order targets are checked in matters: last slot
  first.
- **Hit-in-the-back steps add up.** Each target hit in the back raises `attack.multiplier` for itself
  and every target after it ([`docs/Bugs.md`](../../Bugs.md)).
- **Blows are evaded by Magic Block.** `checkHit()` reads `magicBlockFactor` for every attack, and
  Blind changes nothing ([`docs/Bugs.md`](../../Bugs.md)).
- **The Golem and Interceptor make a hit.** When one of them steps in, the target counts as hit and
  the attack's statuses are removed.

## Where to change

| To change | Edit |
|---|---|
| Which commands run | the `switch` in `CommandExecutor::execute()` |
| How many attacks a Fight makes | `fightAttack()` in `src/battle/commands.cpp` |
| What a hand's weapon gives its attack | `loadWeaponAttack()` in `src/battle/commands.cpp` |
| The attack loop: fizzling, Runic, Air Anchor, the weapon's spell | `src/battle/attack.cpp` |
| The critical-hit chance | `kCriticalBelow` in `src/battle/attack.cpp` |
| The hit check | `checkHit()` in `src/battle/attack.cpp` |
| Damage, statuses and effects | your `AttackHooks` |
