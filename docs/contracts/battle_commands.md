# Battle commands — behavioral contract

Behavioral specification for carrying out a battle command: the battle script a command writes,
how its attack is aimed, the attack frame that runs each attack, the hit check, and Fight. All
citations point into the pinned reference tree `original-src/` (everything8215/ff6 @ `1ea47b5`);
line numbers are into `src/battle/battle_main.asm` unless another file is named, and `btlgfx:`
names a line of `src/btlgfx/btlgfx_main.asm`. This document is the test authority for
`tests/test_targeting.cpp`, `tests/test_hit_check.cpp`, `tests/test_attack_frame.cpp` and
`tests/test_battle_script.cpp`.

The battle core (`docs/contracts/battle_core.md`) decides when a command runs; this contract starts
where it calls `ActionExecutor::execute`. Damage, statuses and the special effects are outside it:
the attack frame calls them through `AttackHooks` at the points named below.

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| A command's start | `ExecCmd` `:3089-3116`, `InitGfxScript` `:6338-6349` | `startAttack()`, `CommandExecutor::execute()` |
| Aiming a chosen command | `CalcCmdDelay` `:637-644` | `CommandExecutor::aim()` |
| The aiming setup | `InitTarget` `:6452-6568`, `InitItemTarget` `:6504-6519`, `CmdTargetTbl` `:6594-6596` | `initTargeting()` |
| Who an attack reaches | `ChooseTarget` `:14543-14603`, `_c258fa` `:14609-14626`, `_c25917` `:14632-14648`, `Retarget` `:14654-14713`, `CheckTargetsBattleType` `:14719-14801` | `chooseTargets()` |
| Targets off the field | `CheckTargetsPresent` `:14807-14833` | `removeAbsentTargets()` |
| Cover | `CoverEffect` `:2838-2888`, `SetCoverTarget` `:2894-2908`, `CheckCoverTarget` `:2914-2939` | `coverTarget()` |
| The attack loop | `ExecSelfAttack` `:7998-8009`, `ExecAttack` `:8015-8139` | `runAttack()`, `runSelfAttack()` |
| One attack | `CalcAttackEffect` `:8146-8371` | `resolveAttack()` |
| The hit check | `CheckHit` `:5796-5985`, `_c2239c` `:5991-6015`, `_c223bf` `:6021-6047` | `checkHit()` |
| The name shown | `ShowAttackName` `:8685-8773` | the `…NameShown` script commands |
| Runic, Air Anchor, a weapon's spell | `RunicEffect` `:8462-8520`, `AirAnchorEffect` `:9023-9031`, `CheckWeaponMagic` `:8658-8677`, `_c237eb` `:8983-9017` | inside `runAttack()` |
| What a hit target records | `_c235e3` `:8578-8613`, `_c2361b` `:8619-8631` | `recordLastHit()` |
| Hit in the back | `UpdateFacingDirection` `:3218-3271` | `hitInTheBack()` |
| The targets remembered | `_c23865` `:9068-9081` | `rememberTargets()` |
| Lores learned | `LearnLore` `:8793-8819` | inside `resolveAttack()` |
| Fight | `FightAttack` `:3445-3459`, `_c2299f` `:6915-6984`, `InitAttacker` `:6840-6849`, `InitAttackerLevel` `:7291-7299` | `fightAttack()`, `loadWeaponAttack()`, `initAttacker()` |
| A spell's command | `GetCmdForAI` `:5006-5020` | `commandForAttack()` |
| The battle script | `_c2629b` / `_c2629e` / `_c262bf` `:16128-16162`, `_c235ad` / `_c235be` `:8526-8556`, `CopyGfxParamsToBuf` `:16325-16346`, `ClearGfxParams` `:16352-16363`, `InitGfxParams` `:14428-14467` | `BattleScriptBuilder`, `AttackAnimationParams` |

## The battle script

A script is a list of commands. Every command a body writes is one value; a command that shows an
animation also takes a block of params — who attacked, whom it was aimed at, whom it hit, what
blocked it — and those blocks are consumed in the order they were written (`CopyGfxParamsToBuf`
appends one; the graphics program advances one per animation, `btlgfx:27069-27078`). An attack
writes its animation command before it knows whom it hit, and its block after
(`CalcAttackEffect :8326`, `:8371`). `BattleScriptBuilder::take()` gives each animation the block
written for it, in order.

`_c235ad` remembers where the last animation command went; `_c235be` writes a command there in place
of it. `replaceAnimation()` is the same.

| Command | Written by | What it carries, as its reader reads it |
|---|---|---|
| `AttackNameShown` | `ShowAttackName` kinds 0, 7 and 8 | the attack's index into the attack names (`btlgfx:22784-22829`). Kind 7, Joker Doom, shows the entry at index `$55` (`:8762`), which is the attack-name table's "Joker Doom": Bushido names come from their own list, so the attack-name slot at Dispatch's index holds it. Kind 8, a Blitz, shows the blitz as the player chose it (`$3a7d`). |
| `BushidoNameShown` | kind 4 | the technique, shown from the Bushido names (`_c197a1` style `$81`) |
| `ItemNameShown` | kind 1 | the item as chosen (`$3a7d`) |
| `EsperNameShown` | kind 2 | the esper summoned (`$b6 − $36`, `:8724-8728`) |
| `CommandNameShown` | kind 5 | the command (`$b5`) |
| `MonsterSpecialAttackNameShown` | kind 6 | the monster whose special-attack name is shown — a 16-bit index into `MonsterSpecialName` (`btlgfx:22712-22769`) |
| `AttackAnimation` | `_c235ad` | the command the animation is shown as, the attack, the second value, and the params. A Fight swing's second byte carries the hand in bit 7 (`btlgfx:28323-28326`, "left-hand weapon flag") and its third the weapon's animation, the item index plus one (`_c2299f :6958-6960`). A command value of `$20` and up is one of the special animations of `CmdAnimTbl` (`btlgfx:27082-27121`). |
| `BattleMessage` | `ExecAttack :8111-8127`, `CalcAttackEffect :8177-8178` | the message index; the numbers its text shows travel with it |
| `PoseReset` | `ExecAttack :8088-8092` | the party member who goes back to standing (`btlgfx:25520-25534`) |

Kind 3 of the name kinds has no writer: nothing sets `$3412` to 3.

The special animation at `$25` (`CmdAnim_25`, `btlgfx:27658-27665`; the upstream comment calls it a
long dialog) plays the caster animation `$37` when the attack has party-member targets. It is
written by `CalcAttackEffect :8237-8246` only when Runic has drawn the attack in this pass — the
branch is taken when `$b2.6` was already set, and only `RunicEffect :8513` clears it. It is
`SpecialAnimation::RunicAbsorbsSpell`.

### The params

| Byte | Written | Read | Field |
|---|---|---|---|
| `$a0.7` | `InitGfxParams :14435-14437` | | `attackerIsMonster`: the attacker's slot is a monster slot |
| `$a0.6` | `:14444-14446` | | `noCharacterTargets` |
| `$a0.5` | `CalcAttackEffect :8324-8325` | `btlgfx:28311-28313` | `criticalFlash` |
| `$a0.4` | `:14454-14457` | | `castingSwirl`. The command starts with the swirl suppressed; the first attack's setup turns the suppression off, so a later attack of the same command has the swirl on. |
| `$a0.1` | `:14458-14465` | | `dragonHorn`: the last landing of a continuous jump |
| `$a0.0` | `CalcAttackEffect :8189-8192` | `btlgfx:28304-28306` "monster weapon attack" | `monsterAttack`: set only for a monster's special attack |
| `$a1` | `:14432-14434` | | `attacker` |
| `$a2-a3` | `:14438-14443` | | `targets` |
| `$a4-a5` | `:14438-14443`, then the hit check and the per-target pass | | `targetsHit` |
| `$a6-a7` | `CheckHit :5823-5824` | `btlgfx:25697-25701` | `reflectedOff` |
| `$a6-a7` | `CheckCoverTarget :2930-2931` | `btlgfx:28175-28228` (the Fight animation's bodyguard step) | `steppingIn`: every battler eligible to cover moves in front of the covered ally |
| `$a8` | `SetCoverTarget :2900-2901` | `btlgfx:28116-28119` | `coveredAlly` |
| `$aa-$ad` | `_c223bf :6039-6045` | `btlgfx:28445-28451` | `blocks`, per party slot |

The Fight animation reads byte 6 as the battlers stepping in; the spell animations read it as the
targets reflected off. Cover happens only on an attack that can be critical, which only Fight and
Umaro's attacks are, and the plain-hit record ignores Reflect, so in a Fight the byte is only ever
the bodyguards.

## A command's start

`ExecCmd :3093-3109`: a fresh script, one attack, no targets remembered; the casting swirl
suppressed, no extra multiplier, no weapon spellcast, no continuous jump; every flag of `$b2` and
`$b3` set and the per-command cells `$3410-$341f` cleared. `startAttack()` holds the command, its
attack and targets as they were chosen, and its MP cost. After the body the script ends
(`_c2629b :3116`); the port's script ends where its list ends.

`$b2` and `$b3`, read from their writers and readers:

| Bit | Port | Default | Changed by |
|---|---|---|---|
| `$b2.6` | `runicAbsorbing` | not absorbing | set by Runic (`:8513`), cleared when its animation is written (`:8237`) |
| `$b2.5` | `firstPass` | first pass | cleared on every pass (`:8041`) |
| `$b2.4` | `followUpSameTargets` | other targets | the weapon's spell goes after the hit's targets (`:8674-8675`) |
| `$b2.1` | `canBeCritical` | cannot | Fight allows it (`:3455`); an Offering, a follow-up spell, a monster special attack and a miss take it away (`:6969`, `:8996`, `:8189`, `:5892`) |
| `$b2.0` | `nameShown` | not shown | the first name shown (`:8686`) |
| `$b3.7` | `vanishApplies` | applies | `ExecSelfAttack :8000-8001` turns it off |
| `$b3.6` | `twoHanded` | not two-handed | the hand's weapon held in both hands (`:6942-6947`) |
| `$b3.5` | `rowIgnored` | ignored | a weapon without the back-row flag lets the attacker's row halve the damage (`:6944-6947`) |
| `$b3.2` | `emptyTargetsReaim` | re-aims | |
| `$b3.1` | `automaticCritical` | no | reset by every weapon load (`:6942-6943`) |
| `$b3.0` | `hitsNoOne` | hits | |

## Aiming

### The targeting byte

`TARGET` bits (`const.inc`) with a two-bit initial-selection field. The byte `$02` alone aims at the
attacker and nothing else (`ChooseTarget :14548-14555`); the byte `$FF` on a command defers to the
attack's own (`InitTarget :6551-6555`). `& $2c` — multi-target or an initial selection other than
one — keeps every target given (`:14566-14569`).

### The command's aiming byte

`CmdTargetTbl` (`:6594-6596`, shipped as `kCommandTargetingInit`). `InitTarget :6463-6478` splits it:

- `& $E1` into the aiming rules: no party targets (bit 7), a random target (bit 6), fall back to the
  command's earlier targets (bit 5), never the user (bit 0);
- `& $18` shifted right one into two more: the fallen may be aimed at (bit 4 → rule bit 3), and a
  target that is gone is not replaced (bit 3 → rule bit 2);
- `& $06` picks how the attack is loaded: `CommandAttackLoad`.

A command at `$1e` or past it has no aiming byte: its rules are empty, its targeting byte is `$40`
(the other side), and it loads the plain hit (`:6456-6461`).

### Loading the attack

| Load | Source | Behavior |
|---|---|---|
| Plain hit | `InitTarget_00 :6548-6549` | the record of attack `$ee`, Battle |
| Spell | `InitTarget_02 :6540-6542` | the attack is remembered as this command's spell and its record loaded |
| Item | `InitTarget_01 :6524-6533` | the item is remembered and aimed by its own targeting byte (`+14`); unless its statuses (`+21`) hold Dead, Petrify or Zombie, the fallen may not be aimed at. An item at Sprint Shoes or past it is the attack itself: nothing more is loaded. Before it, the item casts its spell (`+18 & $3f`), and an item whose cast-on-use bit is clear is not used up. |
| Thrown item | `InitTarget_03 :6489-6498` | Bio Blaster, Flash, Fire Skean, Water Edge and Bolt Edge become their spells (`kThrowToolsConversions`); any other item is aimed as an item and is the attack itself |

After a record is loaded (`:6551-6567`): a `$FF` targeting byte takes the record's; the
resurrection-targeting trait lets the fallen be aimed at, the random-target trait re-aims at random,
the no-party trait keeps party members out. An item that is the attack itself skips these.

### Who an attack reaches

`ChooseTarget` in order:

1. A `$02` targeting byte: the attacker alone.
2. The reach check (`_c258fa`, `_c25917`): unless the next aim is exact — Joker Doom and Seize's
   drain set it, and the check clears it — the targets keep only battlers on the field
   (`targetableOrPresent`) and counted in the battle, party members not seized, monsters their AI
   has not hidden, and while counterattacks are on (`$341a` negative, the default) not those away
   from the field. Then the no-party and never-the-user rules.
3. A random-target rule re-aims at once (step 5). Otherwise, unless the fallen may be aimed at, the
   targets off the field are dropped.
4. Targets left: with `& $2c` they are all kept, otherwise one is picked at random; no battle-layout
   step follows. None left: if a target that is gone is not replaced, step 6 runs on no targets;
   otherwise the attack is re-aimed.
5. **Re-aiming** (`Retarget`): the side starts as the attacker's own. A spell bounced off Reflect
   turns to the other side and nothing else applies. Otherwise a Charm turns it, a party member
   fighting against the party turns it, an initial selection of "all" takes both sides, the
   enemy-first flag turns it, a Zombie attacker takes both sides, and Confuse turns it
   (`:14686-14693`: status-1 bit 1 is Zombie, status-2 bit 5 Confuse). The monsters' side is all six
   monster slots and the party members fighting against the party; the party's side is the other
   party members. Then the reach check again, and the fallen dropped unless they may be aimed at.
6. **The battle layout** (`CheckTargetsBattleType`). A spell bounced off Reflect, aimed at the
   other side only, by an attacker not Confused: on tails it goes to the battler that cast it, if
   that battler is a target (`:14727-14739`); nothing else in this step applies. Otherwise, for an
   initial selection other than "all" or "half": in a pincer, a monster reaches only the monsters
   on its own side, and a party member whose targets are on both sides keeps one of them by a coin
   (heads, the second side); in a side attack, a party member reaches only its own half of the
   party (slots 0-1 or 2-3), and a monster whose targets are on both halves keeps one by a coin
   (heads, slots 2-3). Then, unless the initial selection is "all", an attack aimed at party members
   and monsters at once keeps one of them: the low bit of a draw, set for the party members.
7. With no target left and the fall-back rule, the command's earlier targets.
8. An initial selection other than one keeps them all; a multi-target attack keeps them all on
   heads; otherwise one at random.

A pick draws from the random numbers even when there is one target to pick from. The pincer sides
and the battlers' facing are set by the battle's layout on screen (`btlgfx:2855-2857`,
`btlgfx:39958-39976`); the battle reads them from `BattleState::pincerSides` and `facingRight`.

### Targets off the field

`CheckTargetsPresent :14807-14833`: a target is dropped when it is not on the field, when it is a
party member who is dead or a monster that is dead, petrified or a zombie (`:14818-14821`; the
upstream comments name the two masks the other way round), or when it is hidden (status-4 bit 5,
`:14824-14826`).

### Cover

`CoverEffect :2838-2888`, after the targets are chosen, for an attack that can be critical with a
target. The covered target is the highest-numbered target (`BitToTargetID :13438-13452` scans from
the last monster down).

1. The battler the target's Love Token gives it as a bodyguard steps in if it is free to.
2. Then, if the covered target is Near Fatal, not vanished (status-1 bit 4, `:2861`) and not holding
   anyone in Seize, every True Knight wearer on its side other than the target and the attacker who
   is free to is a candidate; the one with the most HP steps in, and among equal HP the lowest slot
   (`cmp; bcc` keeps the later, lower one). A Love Token bodyguard already in place is not replaced.

Free to cover (`CheckCoverTarget :2914-2939`): on the field, not controlling a monster, not holding
anyone in Seize, and none of Dead, Petrify, Vanish, Zombie, Sleep, Confuse, Stop, Frozen, Chant or
Hide. Every battler found free to cover is marked as stepping in, and the one chosen becomes the only
target, with the covered ally recorded.

## The attack frame

### The attack loop

`ExecAttack :8015-8139` runs once per attack of the command; `attacksLeft` counts those after the
current one, and a weapon's spell adds one. Each attack:

1. The multiplier starts at the command's extra steps; no Golem or Interceptor block yet.
2. Fight or Capture with no follow-up spell pending: the plain hit is set up again and one hand's
   weapon loaded onto it (`_c2299f`) — the weapon hand when `attacksLeft + 1` is even, the shield
   hand when it is odd, so a two-attack Fight swings the weapon hand first. A hand with no battle
   power makes no attack at all (`:8033-8034`). Capture's swing carries the Steal effect.
3. A follow-up spell pending replaces the attack (`_c237eb`): its command is the spell's, it is set
   up afresh, it costs no MP and cannot be critical, and ignores Reflect. Going after the hit's
   targets, it cannot be learned as a lore or drawn in by Runic, may reach the fallen, keeps its
   targets if they are gone, aims at one of them, and either cannot be dodged or — for an attack
   using the stamina save — misses quietly. Otherwise it is re-aimed at random and cannot be dodged.
4. The first attack keeps its targets if they are gone unless its record re-targets when they are;
   every later attack keeps them (`:8041-8047`). An attack with no targets at this point is aimed
   again unless the command said not to (`:8048-8054`). The randomize-targets cell re-aims at
   random; a Blitz with the wrong buttons hits no one.
5. The name, once per command (`ShowAttackName`). Joker Doom's name also makes the next aim exact,
   removes the instant-death trait and makes the attack impossible to dodge (`:8754-8763`).
6. While a Sketch runs, the sketcher makes the rest of the attack.
7. An action chosen as the Roulette command (`$1e`) has no targets and keeps none (`:8070-8076`).
8. **The MP gate** (`:8077-8093`): an attack with an MP cost made by a Muted battler, or by an Imp
   not casting Imp, shows only the maker going back to standing and does nothing else.
9. Runic, Air Anchor, the base damage (`AttackHooks::baseDamage`), and the attack itself
   (`resolveAttack`).
10. A spell, not a blow, that bounced off anyone goes on to its new targets
    (`AttackHooks::reflectedAttack`). The targets become the attack's current ones; the statuses
    set and cleared take effect (`AttackHooks::applyPendingStatuses`); the weapon's spell is cast one
    time in four (`CheckWeaponMagic`), not between the landings of a continuous jump.
11. A battle message set by the attack is shown; a monster's attack whose record asks shows the
    message numbered like the attack.
12. Damage modification on again, no randomized targets, misses shown again; a battler Interceptor
    stepped in for is the one it guards (`state.scratch.interceptorGuarding`).

`ExecSelfAttack :7998-8009` aims at the attacker itself, with Vanish not applying, the fallen
reachable and the target kept even if gone.

### Runic

`RunicEffect :8462-8520`, for an attack Runic may draw in: every battler on the field with Runic up
(the party member's, or a monster's own) is checked; a party member's Runic ends whether or not it
can absorb, and a monster's stays. Those not Dead, Petrified, asleep, stopped, frozen or hidden draw
the attack in: they become its targets, and it restores MP equal to its cost divided among them,
with no statuses, ignoring defense and Reflect, unsplit, undodgeable, without damage modification,
level or attack stat, message or special effect. Its element stays. Its own animation plays before
the attack's.

### Air Anchor

`AirAnchorEffect :9023-9031`: a battler Air Anchor has marked is unmarked when it next attacks, and
that attack kills it (`_c23852 :9046-9052`: Dead is set to happen, and the attacker is marked to
answer itself, ready only when the attack affects MP — the carry from the record's bit 7).

### One attack

`CalcAttackEffect :8146-8371`:

1. Misses and hits in the back cleared; fresh params; the targets chosen and counted; cover; the
   params filled in. The attack is aimed at the other side only when a party member has no party
   targets or a monster has some (`:14447-14453`). The targets are remembered as the current ones
   and added to the command's earlier targets — or, with damage modification off, the current ones
   are taken back.
2. **MP**: an attack with a cost takes it from the attacker. Short of it, a monster's attack is
   shown as Mimic's (which shows nothing), the animation is written with no targets, the "not
   enough MP" message (40) follows, and the attack ends there (`:8161-8179`). Paid, the attacker's
   castable spells are worked out again.
3. **A monster's special attack** (name kind 6): it cannot be critical and is shown as the monster's
   attack, with its special-attack animation. Its byte: no-damage → power 0 and miss if immune;
   can't-dodge → undodgeable; `$30`/`$31` drain HP / MP; `$32` and up remove Reflect; `$20-$2f` add
   `(value − $20) + 1` steps — the compare leaves the carry set into the add (`:8225-8230`); below
   `$20` the status of that number is added.
4. Runic's animation, if Runic drew the attack in.
5. A morphed attacker adds two steps; a Berserk attacker's blow one.
6. The damage is halved across two or more targets unless unsplit, and halved again for a
   back-row attacker whose row counts (`:8259-8273`).
7. The targets hit in the back; the special effect on the attacker
   (`AttackHooks::attackerEffect`). A Launcher or Super Ball with hits still to come stops the
   attack here (`$3405`, `:8277-8279`): its hits are made by the reflected-attack step.
8. The hit check for each target, last slot first; a miss leaves the targets hit.
9. With misses quiet and no one hit, or with the animation hidden for an attack aimed at no one,
   it is shown as Mimic's.
10. **Critical** (`:8304-8325`): an attacker with the Imp critical flag who is an Imp; or an
    automatic critical; or, for an attack that can be critical aimed at the other side only, a draw
    below 8 — one in 32 — with at least one target. Two steps and the flash.
11. The animation is written; the damage multiplied (`AttackHooks::scaleDamage`); an attack that
    kills its attacker sets it to die; a controlled monster's controller has its menu redrawn
    (`AttackHooks::controlMenu`).
12. **Each target hit**, last slot first (`:8344-8364`): one more step for a target hit in the back
    — cumulative, so it counts for every target after it too; the target records the hit; a target
    Zinger holds is skipped; the statuses (`AttackHooks::statusEffect`) and the special effect
    (`AttackHooks::targetEffect`) on it; a target the effect missed is no longer hit, the rest take
    their damage (`AttackHooks::targetDamage`).
13. The damage is taken and shown (`AttackHooks::applyDamage`); Strago learns the lore; with no one
    hit the attack shows no message; the params block is written.

### The hit check

`CheckHit :5796-5985`, for one target:

1. **Vanish**, unless the attack ignores it (`:5803-5816`): a vanished target is hit at once by an
   attack that is not physical — no other check runs — and missed by a blow. A level-divisible
   attack instead sets Vanish to clear and goes on.
2. **Reflect**, unless the attack ignores it: the attack bounces off the target, which is recorded,
   and misses it; no block shows and no miss is recorded (`:5817-5825`).
3. Instant death against protection from it misses. A resurrection-targeting attack misses a target
   that is neither dead nor undead, or both (`:5834-5839`: Dead XOR undead).
4. A Fight or Capture with no special effect aimed at one party member: Interceptor steps in on
   heads; otherwise the Golem, while it has HP (`:5840-5867`).
5. An undodgeable attack hits. A level-divisible one hits when the target's level is a multiple of
   its hit rate. One using the stamina save misses when a percentage roll is at or above
   `magic block factor × hit rate ÷ 256`, or when a 0-127 roll is at or below the target's stamina.
6. A target petrified, asleep, stopped or frozen, hit in the back, or an attack with hit rate 255:
   hit. Against a blow, a target with Retort up takes it, and an Image takes it every time and is
   used up when a draw is below `$40` (one in four).
7. Otherwise a percentage roll below `magic block factor × hit rate ÷ 256` hits.
8. **The miss** (`:5879-5899`): a party member who is not a zombie, in Magitek armor or vanished
   shows a block picked at random among its equipment's block animations for the kind of attack,
   with Interceptor or the Golem among them when one stepped in. When the Golem or Interceptor is
   picked, the attack lands with its statuses removed instead of missing. Any other miss takes away
   criticals and the weapon's spell for the rest of the attack and is shown "Miss" unless misses are
   quiet.

## Fight

`FightAttack :3445-3459`: two attacks, eight with an Offering; the targets off the field dropped;
the targets remembered; criticals allowed; the command remembered so every attack loads a hand.

`_c2299f :6915-6984` loads a hand onto the plain hit: the attacker's vigor and level (the sketcher's
level while a Sketch runs); Sniper Sight makes it undodgeable; the attack `$ef` by a battler not an
Imp is named as a monster's special attack; the hand's battle power, element and hit rate, its
weapon's two-handed and back-row flags, its spell and special effect, its swing. An Offering makes
every attack undodgeable, aimed at random, never critical and without a spell. An Imp monster
without the Imp critical flag hits with power 1.

`GetCmdForAI :5006-5020`: an attack's command is the command of the highest attack base at or below
it (`kAiCommandsForAttack`), or Magic below them all.

## What a hit target records

`_c235e3 :8578-8613`: who hit it with which command as chosen, with which spell, with which item
(when the command used one), and with which elements. `_c2361b`: a target not waiting to answer an
attack is marked to answer this one; one already waiting takes the new attacker on heads.

## Hit in the back

`UpdateFacingDirection :3218-3271`, for a blow only: in a side attack a party member hits in the
back every monster facing the way it faces; in a pincer a monster does the same to the party.

## Lores

`LearnLore :8793-8819`: an attack that can be learned, seen by Strago when he is none of Dead,
Petrify, Blind, Zombie, Sleep, Confuse, Berserk, Stop, Frozen, Rage or Hide, is learned this battle
unless he knows it.

## Defects reproduced

Listed in [`docs/Bugs.md`](../Bugs.md): blows evaded by magic block, with Blind, Beads and the
status modifiers never applied; a vanished target hit by any spell before death protection is
checked; the hit-in-the-back steps counting for every later target.
