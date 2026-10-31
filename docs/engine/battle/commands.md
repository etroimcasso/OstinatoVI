# Battle commands

`CommandExecutor` is the battle's `ActionExecutor`: when the battle core runs an action, it carries
the action out and returns the battle script that shows it. Each command body loads the attack its
command makes into an `AttackInFlight` and hands it to the attack frame, which aims it, checks it
against every target, works out its effects and writes it into the script. The executor also runs
the actions the battle takes on its own — damage over time, the party's escape, a formation change
and the rest — and chooses for the battlers no one chooses for.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_commands.md`](../../contracts/battle_commands.md).

## Files

| File | Holds |
|---|---|
| `src/battle/command_executor.h` | `CommandExecutor` |
| `src/battle/system_commands.h` | the battle's own actions: `damageOverTime()`, `spellWithoutCaster()`, `statusTimersExpire()`, `partyEscapes()`, `seizeDrain()`, `formationChange()`, `setMonsterVisibility()`, `scan()` |
| `src/battle/automatic_actions.h` | `chooseAutomaticAction()`, `chooseOwnCommand()`, `choosesItsOwnAttack()`, `chooseMonsterAction()`, `chooseDanceStep()`, `chooseRageAttack()`, `chooseRandomEsper()` |
| `src/battle/action_queue.h` | the `Action` kinds, among them `FormationChange`, `MonsterDialogAction`, `MonsterVisibilityAction` with `MonsterVisibilityMode`, `ScanAction` and `MonsterAnimationAction` |
| `src/battle/commands.h` | `runCommand()`, the bodies' loaders — `initAttacker()`, `attackerLevel()`, `loadWeaponAttack()`, `plainCommandAttack()`, `fightAttack()`, `setRage()` — and `commandForAttack()`, `isSpear()`, `itemStatuses()`, `kBattleEventTerraMagicScene`, `kTimeUpdatesBeforeDesperation` |
| `src/battle/item_attack.h` | `itemAttack()`, `ItemUse` |
| `src/battle/command_menu.h` | `refreshCommandMenu()`, `kMorphSupplyToMorph`, `kOfferingTargeting` |
| `src/battle/attack.h` | `AttackInFlight`, `AttackScratch`, `SketchDrawn`, `AimingRules`, `SpecialEffect`, `CommandEffect`, `AttackNameKind`, `AttackHooks`, `startAttack()`, `loadAttackRecord()`, `rememberTargets()`, `runAttack()`, `runSelfAttack()`, `resolveAttack()`, `checkHit()`, `HitResult`, `hitInTheBack()`, `recordLastHit()` |
| `src/battle/battle_script.h` | `BattleScriptCommand` and its alternatives, `AttackAnimationParams`, `SpecialAnimation`, `MonsterAnimation`, `BlockShown`, `BattleScriptBuilder`, the battle messages the commands show |
| `src/battle/battle_inventory.h` | `putAwayItemsObtained()` ([below](#items-obtained)) |
| `src/battle/targeting.h` | aiming ([targeting.md](targeting.md)) |

## Running commands

```cpp
AttackHooks hooks;                 // the damage, status and effect work plugs in here
CommandExecutor executor(hooks, cartridge.text, cartridge.monsterSprites);
BattleSim battle(start, party, switches, cartridge.text, cartridge.monsterSprites, executor);
```

The text and the monster sprites are what a formation change needs to load the new monsters and
place them; they must outlive the executor.

`execute()` runs every command a party member or a monster can choose, and every action the battle
takes on its own ([below](#the-battles-own-actions)). Mimic, which the battle core replaces with the
command it mimics before running it, shows nothing. After an action it reports to the battle core
whether the attack was physical and who it ended up hitting (`state.scratch.physical`,
`state.scratch.finalTargets`). `aim()` aims a command chosen with no targets: Fight chosen by a
berserk party member, say, gets one monster. `chooseAutomaticAction()` and `runAi()` choose for the
battlers no one chooses for ([below](#automatic-actions)), and `chooseDanceStep()`,
`chooseRageAttack()` and `chooseRandomEsper()` settle the menu choices that come down to chance.
`refreshCommandList()` decides which of a party member's commands can be chosen
([below](#the-command-menu)).

Without the executor, `runCommand()` runs one command into a script:

```cpp
AttackInFlight attack = startAttack(CommandAction{.command = BattleCommandId::STEAL,
                                                  .targets = BattlerSet::of(BattlerSlot::monster(0))});
BattleScriptBuilder script;
runCommand(state, hooks, script, attack, BattlerSlot::character(0));
std::vector<BattleScriptCommand> commands = script.take();
```

## The commands

Every body follows the same few steps: name the attack, load what the command makes, change what the
command changes about it, run it. The name shown is `scratch.nameKind`; a body that sets none shows
no name.

| Command | Makes | Name |
|---|---|---|
| Fight, Capture | two blows, one per hand (eight with an Offering); Capture's carry Steal. A party member late in the battle may make a desperation attack instead; Umaro picks his own attack | — |
| Item | the item: a consumable's own effect, or the spell it casts as Magic | the item |
| Throw | the thrown weapon at twice its damage, or the spell an edge casts | the item |
| Tools | the tool, or the spell the Bio Blaster or Flash casts | the item |
| Magic, X-Magic, Lore, Magitek | the spell, with the caster's magic power and level | the spell |
| Summon | the esper's attack | the esper |
| Slot | the reels' outcome, free; the swirl plays | the attack, the esper, or Joker Doom |
| Health, Shock | Cure 2, Shock | the command |
| Dance | the step of the dance chosen at intake | the step |
| Bushido | the technique | the technique |
| Blitz | the blitz, or nothing for buttons that made none | the blitz |
| Rage | the attack chosen for the monster raged as | as that attack's command |
| Morph, Revert | Morph put on, or taken off, the user | — |
| Row, Runic, Def | a change to the user, shown on the user | — |
| Steal, Sketch, Control, Leap, Possess | the plain hit carrying the command's effect | — |
| GP Rain | the plain hit, one power stronger, unsplit, ignoring defense | the command, for a monster |
| Jump | a blow with one hand's weapon | — |
| the Roulette command | Lore's Roulette at no one, then the roulette cursor | Roulette |

The commands that only act on a target or the user start from `plainCommandAttack()`: the plain hit
aimed by the command's own rules, impossible to dodge, with no level, no attack stat and no effect of
its own. The special effects (`CommandEffect::Steal`, `Control`, `Leap`, `Sketch`, `Possess`,
`GpRain`) are worked out by `AttackHooks::targetEffect`.

### Fight and desperation attacks

A party member's Fight becomes its desperation attack — Riot Blade for Terra, X-Meteo for Gogo, the
shipped `desperationAttack()` table — once per battle, one time in sixteen, when all of these hold:

- the battle has run `kTimeUpdatesBeforeDesperation` (768) time updates;
- it is Near Fatal, and neither seeing double, confused, a zombie nor vanished;
- the Fight is aimed at a monster.

The attack is cast as Magic, with the swirl. Gau, Umaro and the guests have none, but spend their
chance all the same (`state.desperationUsed`).

### Umaro

Umaro chooses between a Fight and a tackle, his Storm with a Blizzard Orb, and throwing an ally with a
Rage Ring while another party member is alive. Each relic selects a different row of
`kRandomBitRates`, so wearing one changes the odds of the others too.

- **Tackle**: the power of both his hands, his level and vigor, ignoring defense; may be critical.
- **Storm**: the Storm attack cast as a spell, aimed at random.
- **Throw**: an ally is thrown — the highest-placed one asleep or confused, else one at random, never
  one in hiding; with no one to throw he tackles. The thrown ally's power joins his, one more for Mog
  and any ally after him, at most 255. Throwing Mog is a critical hit. The ally wakes and comes to its
  senses.

### Item, Throw and Tools

The item command names the item and aims it by the item's own targeting. An item that casts a spell —
a rod, an elemental shield, a thrown Fire Skean, the Bio Blaster — casts it free, unreflected and
undodgeable, never at the fallen; Item shows it as Magic. Any other item is the attack itself,
built by `itemAttack()`:

```cpp
itemAttack(state, attack, user, ItemId::POTION, ItemUse::Consumed);
itemAttack(state, attack, user, ItemId::DIRK, ItemUse::ThrownOrWorked);
```

| Use | The attack |
|---|---|
| a consumable | not physical; its record's healing (restore HP, restore MP, remove status, fractional damage, harm the undead), statuses and use effect. One that works on the dead raises them |
| a thrown weapon | a physical blow at the item's battle power, with the thrower's vigor and level |
| a tool | as a thrown weapon, but dodgeable, at the tool's hit rate, taking defense into account, plus its own effect: Noiseblaster confuses, Chain Saw kills one time in four, Drill ignores defense, Debilitator and Air Anchor carry their effect, Auto Crossbow is unsplit |

Every item attack ignores Reflect; all but a tool also ignore defense and cannot be dodged. Unless
it raises the dead or turns its target to stone or a zombie, it reaches only the living, and is not
aimed again when they are gone. Item's attack is not modified for damage, and Vanish does not change
whether it lands; Throw doubles its weapon's damage and multiplies it even though it ignores defense
(`ignoreDefenseStopsMultiplier`).

An item used up leaves `Battler::itemInHand`; one that is not — whose spell is not cast on use — stays
there. Either way the user joins `state.holdingItem`, and what it holds goes back into the inventory
after the action.

### Dance

A dancer dances the dance it chose, or the last one chosen (`state.lastDance`). A dance that matches
the battlefield (`backgroundDance()`) is danced. Otherwise a coin decides: heads, the battlefield
changes to the dance's (`danceBackground()`, shown as the animation's `newBackground`) and it is
danced; tails, the dancer stumbles — no more dancing, the "stumbled" message, and only the stumble
shown.

### Sketch, Rage and Control

These three make a second command inside the first, into the same script.

- **Sketch** draws the sketch. If its effect caught the monster (`scratch.sketch`), the sketcher takes
  the monster's special attack and its animation and makes the attack it caught, as the monster
  would: with the monster's vigor or magic power, the sketcher's level, aimed at random, named again.
- **Rage** rages as the monster chosen, or as the last party member to rage did (`state.lastRager`).
  `setRage()` takes on the monster: its special attack and animation, its attack's look in both hands,
  and what `loadMonsterTraits()` gives — elements, immunities and statuses, the statuses put on when
  the action's statuses take effect. Then the attack chosen for it is made.
- **Control** tries to take hold of the target. A party member already controlling a monster (its
  Chant status) goes back to standing and has the monster make the attack picked for it.

### Bushido, Blitz and Jump

- **Bushido**: Retort's first use readies it — the stance shows with no technique
  (`AttackAnimation::showsAttack`), no harm, no name. Its second answers it, with the swirl and the
  name. Every other technique is named and made.
- **Blitz**: buttons that made no blitz hit no one, with Pummel's record, no name and the "incorrect
  blitz input" message (`kMessageBlitzFailed`).
- **Jump**: the jumper comes down with the hand that has power — either, on a coin — undodgeable, its
  row ignored, for half again the damage, twice with a spear in either hand (`isSpear()`). A Dragon
  Horn brings it down two to four times. Landing ends its hiding.

## The battle's own actions

The battle core queues these itself, or a monster's AI asks for them; `execute()` runs each one.
Most make an attack from the plain hit with no command's rules, shown as `kNothingShown`, so the
hooks work out their damage and statuses like any other attack's.

| Action | What happens |
|---|---|
| `DamageOverTime` | `damageOverTime()`: an attack on the battler itself, ignoring defense, unsplit, turning around on the undead, of power max HP × stamina / 1024 + 2 (at most 255). Regen restores. Poison carries the Poison element and `Battler::poisonMultiplier`, which rises by two steps after every Poison beat, to 14, and stays up for the rest of the battle. Nothing happens while no enemy is left |
| `SpellWithoutCaster` | `spellWithoutCaster()`: the spell as Magic, cast by the battler on itself — free, unreflected, undodgeable, with the swirl. Doom also stops the condemned count, and stops there while no enemy is left |
| `StatusTimersExpired` | `statusTimersExpire()`: Stop, Reflect and Frozen come off; Sleep comes off a battler still asleep, which wakes with Tapir's animation |
| `PartyEscape` | `partyEscapes()`: each party member who got away, with a full gauge, free to act, on the field and neither a zombie nor hidden, leaves; they run off together. A party that cannot run is told so |
| `SeizeDrain` | `seizeDrain()`: the seizer drains its captive — level 16, power 16, unreflected, not capped by the HP the seizer is missing (`AttackInFlight::drainCapped`) |
| `FormationChange` | `formationChange()`: the battle moves to another formation (`changeFormation()`, [start.md](start.md)) and shows it (`FormationChanges`) |
| `MonsterVisibilityAction` | `setMonsterVisibility()`: monsters leave or come back by `MonsterVisibilityMode` — revived at full or current HP, hidden as a target or not, hidden while the battle may not end, hidden and killed |
| `ScanAction` | `scan()`: the level, HP, MP (unless the target has none) and each weakness nothing cancels, one message each, every message its own `RunBattleScript` played before the action's own, which is empty |
| `MonsterDialogAction`, `BattleMessageAction`, `BattleEventAction`, `ResetBattlePose`, `MonsterAnimationAction` | only their command: `MonsterDialogShown`, `BattleMessage`, `BattleEventPlays`, `PoseReset`, `MonsterAnimationPlays` |

```cpp
AttackInFlight attack;
BattleScriptBuilder script;
damageOverTime(state, hooks, script, attack, slot, DamageOverTimeKind::Poison);
state.at(slot).poisonMultiplier;   // 2 after the first beat
```

## Automatic actions

A party member whose turn comes with nothing chosen acts on its own (`chooseAutomaticAction()`):

1. A dancer dances its dance's step (`chooseDanceStep()`): the first step 7 times in 16, the second 3
   in 8, the third 1 in 8, the fourth 1 in 16 (`kDanceStepThresholds`). A dancer with no dance takes
   one of the dances the party knows.
2. A raging party member makes one of its monster's two Rage attacks, on a coin
   (`chooseRageAttack()`). One with no monster takes one of the known rages.
3. A party member in Magitek armor fires one of the first three beams.
4. Anyone else — berserk, confused, charmed, a zombie, in the colosseum — picks one of its own
   commands (`chooseOwnCommand()`).

`chooseOwnCommand()` offers each of the four commands unless the row is empty, a confused party
member could not use it (`kConfusedAllowedCommands`), or the berserk rules forbid it
(`kBerserkAllowedCommands`, for the berserk and for a zombie who is neither confused, charmed nor in
the colosseum). A command that picks its attack at random drops out when it has none: Magic or Lore
while Muted or with nothing to cast, Morph below 16 supply, Bushido without a Bushido weapon. One
more Fight is always offered, so Fight comes up more often than the others.

An X-Magic pick casts two spells: a Magic with a second spell, then the X-Magic. A Lore that comes out
as Roulette is made as the Roulette command (`kRouletteCommand`), which takes no advance wait and is
not aimed.

A monster that is berserk, confused, charmed or in the colosseum picks its own attack instead of
following its AI: `runAi()` checks `choosesItsOwnAttack()` and calls `chooseMonsterAction()`, which
takes one of its four Control attacks — from a slot drawn at random, the first filled one counting
down and round again — or, when berserk, the plain hit. Any other monster's AI turn adds nothing.

`chooseRandomEsper()` is the esper a slot reel's summon calls: every esper but Odin and Raiden,
equally likely.

## Items obtained

After every action, each party member marked in `state.holdingItem` with an item still in
`Battler::itemInHand` puts it into the party's inventory — an `AddItemObtained` request, last party
member first — and its hand is emptied (`putAwayItemsObtained()`). Item marks the user, so a rod or
shield whose spell was cast without using it up goes back. Anything that puts an item in a party
member's hand and marks it in `state.holdingItem` hands it over the same way.

## The command menu

`refreshCommandMenu(state, slot)` decides which of a party member's four commands can be chosen
(`MenuCommand::usable`) and marks its menu to be drawn again (`state.menusToRefresh`). The battle
calls it, through the executor, after the Item menu changes equipment and whenever a pending update
asks for it.

| Rule | Unusable when |
|---|---|
| every command | its row is empty, or the party member is an Imp and the command is not one an Imp may use |
| Morph | the party's Morph supply is below `kMorphSupplyToMorph` (16) |
| Runic, Bushido | neither hand holds a Runic, or a Bushido, weapon |
| Magic, X-Magic, Lore | the party member is Muted |

With an Offering worn, Fight and Capture take `kOfferingTargeting` and keep it after the Offering is
taken off; nothing sets the command's own targeting back during the battle.

```cpp
state.at(slot).status.set(StatusId::SILENCE);
refreshCommandMenu(state, slot);
state.at(slot).commands[1].usable;   // false for Magic
```

## The attack in flight

`startAttack()` turns a chosen command into an `AttackInFlight`: the command and attack as chosen,
the targets, the MP cost. The body then loads the attack itself — `initTargeting()` loads the
command's record, `loadWeaponAttack()` puts a hand's weapon on it — and edits the copy as the command
requires. The shipped record is never changed.

| Field | Is |
|---|---|
| `command`, `attack` | what the attack is shown as and what it is; a body may change both |
| `queuedCommand`, `queuedAttack` | the command and attack (or item) as the player or the AI chose them |
| `record` | the attack's record, as this attack has changed it |
| `effect` | the special effect it carries, whatever it came from |
| `attackerStat`, `attackerLevel` | the stat and level the damage is worked out from |
| `rules`, `targeting`, `targets` | how it is aimed and whom it reaches |
| `multiplier`, `extraMultiplier` | damage steps, each adding half the damage again; the command's own steps start every attack's count |
| `damage` | the damage before it is split and multiplied |
| the flags | whether it can be critical, Vanish applies, the row counts, a target that is gone is replaced, an attack that ignores defense is multiplied, and so on |
| `showsAttack`, `chainSawKill`, `newBackground`, `secondary`, `offHand` | what its animation shows |
| `params` | what the animation is told about who attacked and who was hit |
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
hand with no battle power makes no attack. `runSelfAttack()` aims at the attacker itself.

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

`rememberTargets()` notes each attack's targets as the command's current ones and adds them to its
earlier targets. While an attack is aimed at random (`scratch.randomizeTargets`), it takes the
current targets back instead.

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
| `targetEffect` | for each target hit, after `statusEffect`; return `true` if the effect missed it. Sketch's effect sets `attack.scratch.sketch` when it catches |
| `targetDamage` | for each target hit that the effect did not miss |
| `applyDamage` | once, after every target; write the damage numbers into the script |
| `reflectedAttack` | after a spell bounced off anyone |
| `applyPendingStatuses` | after each attack |

`baseDamage` and `scaleDamage` are the formulas in [damage.md](damage.md): `battle::baseDamage()`
and `battle::scaleDamage()`, which a hooks class calls from those members.

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
| `AttackAnimation` | the command's animation (`what` a `BattleCommandId`) or a special one (`SpecialAnimation`), with its params; a Fight swing says its hand and its weapon's animation; Item, Throw and Tools show `item`; a Dance that changes the battlefield, `newBackground` |
| `BattleMessage` | a battle message, with the numbers it shows |
| `PoseReset` | a party member going back to standing |
| `FormationChanges` | the formation changing: the departing monsters' exit, whether the background scrolls on, and the monsters fighting once it has arrived; `FormationChanges::from()` reads the one-byte form the final battle's stages carry |
| `MonsterDialogShown` | a monster's dialog |
| `BattleEventPlays` | a battle event |
| `MonstersEnterOrLeave` | monsters coming back by an entrance type, or else leaving by it |
| `MonsterAnimationPlays` | a monster animation, or a sound effect with its pan |
| `DamageNumbersShown` | damage numerals over the battlers an attack reached — an amount, or "Miss", green for healing — every one at once with the script waiting (`DamageNumberWait::UntilGone`) or one at a time with the script going on (`CarryOn`) |
| `ReadyStance`, `StepBack` | written by the battle core |

How long each command takes on screen is [presentation.md](presentation.md).

An attack shown as `kNothingShown` (Mimic's command, whose animation is empty) shows no animation.
`kJokerDoomName` is the attack-name entry that reads "Joker Doom".

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
- **A nested command shares the attack.** Sketch, Rage and Control run the second command on the same
  `AttackInFlight`, so what the first changed — the targets remembered, the name already shown, the
  swirl — carries into it.
- **Umaro throws Mog's power plus one.** So does any ally from Mog on ([`docs/Bugs.md`](../../Bugs.md)).
- **Poison's multiplier never comes down.** `Battler::poisonMultiplier` keeps its value through a cure,
  so a battler poisoned again later in the battle takes the higher damage at once.
- **A formation change keeps more than HP.** Without `restoreMonsters`, each new monster takes the
  HP and MP its slot held, and its stamina follows that max HP. Either way the slots keep their
  statuses, and a slot whose monster had fallen stays fallen.
- **Scan's messages arrive as their own scripts.** Each is a `RunBattleScript` request pushed during
  `execute()`; the battle waits only on the action's own script, which comes last.
- **The Roulette command skips the advance wait.** It is added with `addAction()`, not
  `addAutomaticAction()`.

## Where to change

| To change | Edit |
|---|---|
| Which body a command runs | the `switch` in `runCommand()`, `src/battle/commands.cpp` |
| What one command does | its body in `src/battle/commands.cpp` |
| The attack an item, a thrown weapon or a tool makes | `itemAttack()` in `src/battle/item_attack.cpp` |
| When a desperation attack comes | `desperationAttackFor()` in `src/battle/commands.cpp` |
| Which commands can be chosen | `refreshCommandMenu()` in `src/battle/command_menu.cpp` |
| How many attacks a Fight makes | `fightAttack()` in `src/battle/commands.cpp` |
| What a hand's weapon gives its attack | `loadWeaponAttack()` in `src/battle/commands.cpp` |
| The attack loop: fizzling, Runic, Air Anchor, the weapon's spell | `src/battle/attack.cpp` |
| The critical-hit chance | `kCriticalBelow` in `src/battle/attack.cpp` |
| The hit check | `checkHit()` in `src/battle/attack.cpp` |
| Damage, statuses and effects | your `AttackHooks` |
| What a battle's own action does | its function in `src/battle/system_commands.cpp` |
| Which action kind runs which function | `CommandExecutor::execute()` in `src/battle/command_executor.cpp` |
| What a party member picks on its own | `chooseOwnCommand()` and its choosers in `src/battle/automatic_actions.cpp` |
| The dance step odds | `kDanceStepThresholds` (shipped data); the ladder is `chooseDanceStep()` |
| When a monster picks its own attack | `choosesItsOwnAttack()` in `src/battle/automatic_actions.cpp` |
| What happens to an item left in hand after an action | `putAwayItemsObtained()` in `src/battle/battle_inventory.cpp` |
