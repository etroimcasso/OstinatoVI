# Battle commands — behavioral contract

Behavioral specification for carrying out a battle command: the battle script a command writes,
how its attack is aimed, the attack frame that runs each attack, the hit check, every command the
player can choose, the attack an item makes, the command menu's refresh, the actions the battle
takes on its own, what a battler does when no one chooses for it, and the items handed over after
an action. All citations point
into the pinned reference tree `original-src/` (everything8215/ff6 @ `1ea47b5`); line numbers are
into `src/battle/battle_main.asm` unless another file is named, and `btlgfx:` names a line of
`src/btlgfx/btlgfx_main.asm`. This document is the test authority for `tests/test_targeting.cpp`,
`tests/test_hit_check.cpp`, `tests/test_attack_frame.cpp`, `tests/test_battle_script.cpp`,
`tests/test_battle_layout.cpp`, `tests/test_commands.cpp`, `tests/test_item_attack.cpp`,
`tests/test_command_menu.cpp`, `tests/test_system_commands.cpp`, `tests/test_automatic_actions.cpp`
and `tests/test_command_battle.cpp`.

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
| Where the monsters stand | `InitMonsterPos` `btlgfx:2838-2892`, `CalcMonsterPos_00`–`_03` `btlgfx:2689-2820`, the formation's x `btlgfx:2019-2022` | `placeMonster()`, `layOutMonsters()` |
| Where the party stands | `CharDirTbl` `btlgfx:45277`, `CharXAngleTbl` `btlgfx:42816`, `InitCharGfxMain` `btlgfx:8258-8270`, `:8457-8467` | `kPartyLayout`, `layOutParty()` |
| Turning to face a target | `_c19aa2` `btlgfx:23256-23370`, `GetTargetID` `btlgfx:28023-28035`, `SetAnimTargets` `btlgfx:25508-25514`, `CmdAnimTbl` `btlgfx:27082-27121` | `turnToFace()`, `turnToFaceTargets()` |
| The targets remembered | `_c23865` `:9068-9081` | `rememberTargets()` |
| Lores learned | `LearnLore` `:8793-8819` | inside `resolveAttack()` |
| Fight | `FightAttack` `:3445-3459`, `_c2299f` `:6915-6984`, `InitAttacker` `:6840-6849`, `InitAttackerLevel` `:7291-7299` | `fightAttack()`, `loadWeaponAttack()`, `initAttacker()` |
| A spell's command | `GetCmdForAI` `:5006-5020` | `commandForAttack()` |
| The battle script | `_c2629b` / `_c2629e` / `_c262bf` `:16128-16162`, `_c235ad` / `_c235be` `:8526-8556`, `CopyGfxParamsToBuf` `:16325-16346`, `ClearGfxParams` `:16352-16363`, `InitGfxParams` `:14428-14467` | `BattleScriptBuilder`, `AttackAnimationParams` |
| The command bodies | `CmdTbl` `:4157-4189` (commands $00-$1f), `Cmd_00`–`Cmd_1e` `:3299-4143` | `runCommand()` |
| The plain hit a command makes | `_c2298a` / `_c2298d` `:6892-6909` | `plainCommandAttack()` |
| Desperation attacks | `Cmd_00` `:3398-3431`, `DesperationAttack` `:3591-3594`, the table `:3596-3615` | inside `runCommand()`, `desperationAttack()` |
| Umaro | `_163b` `:3462-3489`, `UmaroAttackTbl` `:3493-3497`, `UmaroAttack_00`–`_02` `:3503-3585`, `InitUmaroAttack` `:3829-3841`, `RandBitWithRate` `:13525-13545`, `RandBitRateTbl` `:13549-13554`, `RandBit` `:13495-13515` | inside `runCommand()`, `pickWeighted()` |
| The attack an item makes | `CalcItemEffect` `:6992-7093`, `ToolsEffect_00`–`_07` `:7112-7170` | `itemAttack()` |
| Rage | `Cmd_10` `:3334-3354`, `SetRage` `:999-1015`, `LoadRageProp` `:7504-7550`, `FixImmuneStatus` `:6359-6380`, `UpdateImmuneStatus` `:6386-6433` | `setRage()`, `loadMonsterTraits()` |
| A spear | `SpearEffect` `:3279-3285` | `isSpear()` |
| The command menu | `UpdateCmdList` `:13560-13617`, `UpdateCmdIDTbl` / `UpdateCmd_00`–`_07` `:13621-13689`; its callers `:1360`, `:1721`, `:5625` | `refreshCommandMenu()` |
| The battle's own actions | `Cmd_20`–`Cmd_2d` `:13019-13406`, `Cmd_2a` `:5637-5696`, `Cmd_2b` `:16393-16401`, `MonsterDeathTbl` / `MonsterDeath_00`–`_05` `:13088-13147`, `:13411-13417` | `damageOverTime()`, `spellWithoutCaster()`, `statusTimersExpire()`, `partyEscapes()`, `seizeDrain()`, `formationChange()`, `setMonsterVisibility()`, `scan()`, `CommandExecutor::execute()` |
| A formation change | `Cmd_20` `:13206-13244`, `LoadBattleProp` `:7940-7992`, `InitMonsters` `:7672-7709`, `LoadMonsterProp` `:7307-7436` | `changeFormation()`, `loadMonster()` |
| Who queues them | `rigenetimer` `:14990-15035` (damage over time, Seize), `timer` `:14940-14963`, `CondemnDeath` `:15058-15062`, `CreateRunAwayAction` `:15083-15094`, the AI commands `AICmd_f2`/`f3`/`f5`/`f7`/`fa` `:4464-4549` | battle core; the AI's are its own unit |
| A party member acting on its own | `QueueAction` `:537-568`, `FixRoulette` `:574-582`, `RandCharAction` `:656-745`, `ConfusedCmdTbl` / `BerserkCmdTbl` / `RandCmdIDTbl` / `RandCmdTbl` `:754-777` | `chooseAutomaticAction()`, `chooseOwnCommand()` |
| The random attacks | `RandLore` `:783-801`, `RandMagic` `:807-837`, `RandMorph` `:854-859`, `RandBushido` `:865-875`, `RandBlitz` `:881-889`, `RandMagitek` `:895-900`, `RandTools` `:906-914`, `RandDance` `:920-947`, `RandRage` `:961-993`, `RandGenju` `:8968-8977`, `RandBit` `:13495-13515`, `GetBitID` `:13423-13429` | inside `chooseOwnCommand()`, `chooseDanceStep()`, `chooseRageAttack()`, `chooseRandomEsper()` |
| A monster picking its own attack | `ExecAction` `:213-224`, `RandMonsterAction` `:1021-1055` | `CommandExecutor::runAi()`, `choosesItsOwnAttack()`, `chooseMonsterAction()` |
| Items obtained | `_c262c7` `:16168-16187`, called at the end of `ExecCmd` `:3126` | `putAwayItemsObtained()` |

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
| `BattleMessage` | `ExecAttack :8111-8127`, `CalcAttackEffect :8177-8178`, `Cmd_25 :13250-13268`, `Cmd_27 :13274-13329`, `Cmd_2a :5642-5643` | the message index; the numbers its text shows travel with it |
| `PoseReset` | `ExecAttack :8088-8092`, `Cmd_2c :13373-13379` | the party member who goes back to standing (`btlgfx:25520-25534`) |
| `FormationChanges` | `Cmd_20 :13237-13244`, the final battle's next stage (`docs/contracts/battle_core.md`) | `GfxCmd_12 btlgfx:22118-22167`: byte 1's bit 7 scrolls the background on (`_c19275`); its low bits are the exit the departing monsters take (`DoMonsterEntryExit`). Byte 2 is overwritten by the reader with the monsters on screen. Byte 3 is the monsters still fighting (`$3a75`), which the reader also writes into the monsters standing (`$2f2f`). The new monsters enter by the new formation's own entrance (`$2f48`). |
| `MonsterDialogShown` | `Cmd_21 :13259-13268` | `GfxCmd_10 btlgfx:22863-22876`: a 16-bit index into the monster dialogs; `Cmd_21` zeroes byte 2, so the index is byte 1 |
| `BattleEventPlays` | `Cmd_23 :13019-13024` | `GfxCmd_0f btlgfx:38200-38238`: the battle event |
| `MonstersEnterOrLeave` | `Cmd_24 :13061-13083` | `GfxCmd_13 btlgfx:22177-22206`: byte 1 is the entrance type, the 18 scripts `DoMonsterEntryExit btlgfx:45529-45562` dispatches — `MonsterEntranceType`; with byte 3 (the monsters revived) set those monsters enter by it; otherwise byte 2's monsters (those hidden) that are still on screen leave by it |
| `MonsterAnimationPlays` | `Cmd_2b :16393-16401` | `GfxCmd_14 btlgfx:21917-21920` → `DoMonsterAnim btlgfx:45502-45523`: byte 1 is the animation (the AI script's `$fa` sub-command, named by `include/battle/ai_script.inc:259-327`); byte 2 is the monsters it plays on (a monster mask), or for the sound effect (9) the pan, with byte 3 the sound |

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
| `$b3.5` | `rowIgnored` | ignored | a weapon without the back-row flag lets the attacker's row halve the damage (`:6944-6947`); a Jump ignores it again (`:3872`) |
| `$b3.4` | `ignoreDefenseStopsMultiplier` | an attack that ignores defense is not multiplied | a Throw clears it (`:3947`). `ApplyDmgMult :8827-8836` reads it through a 16-bit load of `$b2`: shifted once and ANDed with `$11a1`/`$11a2`, three more shifts carry out `$b3.4` AND the ignore-defense trait, and a carry skips the multiplication |
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
the attack's. The targets it had are remembered before the absorbers replace them, and the attack is
then marked as aimed at random (`:8493-8494`), so the attack's own remembering takes them back.

### Air Anchor

`AirAnchorEffect :9023-9031`: a battler Air Anchor has marked is unmarked when it next attacks, and
that attack kills it (`_c23852 :9046-9052`: Dead is set to happen, and the attacker is marked to
answer itself, ready only when the attack affects MP — the carry from the record's bit 7).

### One attack

`CalcAttackEffect :8146-8371`:

1. Misses and hits in the back cleared; fresh params; the targets chosen and counted; cover; the
   params filled in. The attack is aimed at the other side only when a party member has no party
   targets or a monster has some (`:14447-14453`). The targets are remembered as the current ones
   and added to the command's earlier targets — or, while the attack is aimed at random, the
   current ones are taken back. `_c23865 :9068-9081` loads `$3414` with a 16-bit accumulator, so its
   `bmi` tests the high byte, `$3415` (randomize targets, cleared by a random weapon spell
   `:9001`, a sketched attack `:3314`, Umaro's Storm `:3582` and Runic `:8494`), not damage
   modification; Item and GP Rain, which turn damage modification off, remember their targets.
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

## The commands

`ExecCmd :3110-3113` dispatches on the command through `CmdTbl`; the port's `runCommand()` is a
`switch`. Mimic (`$12`) and `$1f` are `CmdNoEffect :3292`: the battle core replaces Mimic with the
command it mimics before running it.

### The plain hit a command makes

`_c2298a :6892` aims the queued command (`$3a7c`); `_c2298d :6901-6909` aims a command given in A.
Both load the command's attack (`InitTarget`), make it undodgeable (`$11a4.5`), take away its special
effect, and zero the level and the attack stat (`$11af`, `$11ae`). `plainCommandAttack()`.

### Fight and desperation attacks

`Cmd_00 :3398-3431`, for a party member, in this order: the turn counter's high byte (`$3a3f`, the
counter of time updates `$3a3e`) at least 3 — 768 time updates; Near Fatal (status-2 `$02`);
neither Image nor Confuse (status-2 `$24`); neither Zombie nor Vanish (status-1 `$12`); a monster
among the targets (`$b9`); a draw whose low four bits are clear, one in 16 (`:3414-3416`); and the
party member not yet in `$3f2f` — test-and-set, so it is marked used here even when the next check
turns it away. The actor (`$3ed8`) then decides: Gogo is `$fb`; any other actor from Gau on has
none; Terra..Mog are `$f0` plus the actor (`inc` / `dec` / `ora #$f0`). The shipped
`desperationAttack()` table holds exactly these. The attack runs as Magic (`DesperationAttack :3591`,
`$b5 = $02`) under its own name (kind 0), with the casting swirl turned on (`:3429-3430`). The Terra
check of `Cmd_02` is not on this path.

`Cmd_06 :3438-3443` (and Fight past the check): a party member whose actor is Umaro chooses his own
attack; anyone else runs `FightAttack`.

### Umaro

`_163b :3462-3489`: `$fe` gets `$04` when either relic (`$3cd0`/`$3cd1`) is a Rage Ring (`$c6`) and
the alive party members (`$3a74`) with Umaro's own bit flipped are not none, and `$08` when either
is a Blizzard Orb (`$c5`). `RandBitWithRate :13525-13545` with `X = $fe` and the offered bits
`($fe | $30) << 2`: for each of four choices, bits 7 to 4, an offered one adds its weight
(`RandBitRateTbl` row `$fe / 4`) plus the carry — one — to a running byte total; a draw below the
total (`RandA`) picks the first choice it falls short of. The choices, in order (`UmaroAttackTbl`
read backward): Fight, tackle, Storm (Blizzard Orb), throw (Rage Ring). `pickWeighted()` is this
routine, shared with `ChooseBattleType`.

- **Tackle** `UmaroAttack_02 :3503-3512`: `InitUmaroAttack :3829-3841` — the plain hit, power both
  hands' battle power capped at 255, Umaro's level, and his vigor as the attack stat — then ignore
  defense, criticals allowed, shown as `$23`.
- **Throw** `UmaroAttack_00 :3518-3575`: the candidates are the alive party members with Umaro's
  bit flipped. Slots 3 down to 0: a candidate in Hide (status-4 `$20`) is dropped; a candidate asleep
  or confused (status-2 `$a0`) **becomes the only candidate** (`:3537-3539` loads its bit alone into
  the saved set), so the highest-placed one is thrown. None left: the tackle. Otherwise one is picked
  (`RandBit`, the party counted first) and becomes X. Mog (`$0a`) makes the throw an automatic
  critical (`$b3.1` cleared). The power: the ally's two hands' battle power **plus the carry left by
  `cmp #$0a`** — one for Mog and any actor after him — capped at 255 (`$fe` plus the carry), plus
  Umaro's power, capped at 255. Shown as `$24`; ignore defense; criticals allowed; never the user
  (`$ba.0`). The ally's Sleep and Confuse are set to **clear** (`$3dfd`, status-2 to clear): being
  thrown wakes it and brings it to its senses.
- **Storm** `UmaroAttack_01 :3581-3585`: targets aimed at random (`$3415` cleared), attack `$54` cast
  as Magic under its own name.

### Item, Throw and Tools

`Cmd_01 :3954-4005`, entered at `_189e` by Throw (`:3943-3948`) and Tools (`:3933-3937`):

- Item only: damage modification off (`$3414`), Vanish does not apply (`$b3.7`).
- Throw only: extra multiplier 2, and `$b3.4` cleared (see the `$b3` table).
- Tools only: `$b6` becomes the item less `$a3` — the tool's index into the tools' animations,
  through a `sbc` with the dispatcher's carry clear. The port shows the tool itself
  (`AttackAnimation::item`).
- Named as the item (kind 1). The item (`$3a7d`) is aimed (`InitTarget`); an item whose spell byte's
  bit 7 is clear sets `$b1.4` (`InitTarget_01 :6524-6533`) and is kept; any other clears `$32f4`
  (`Battler::itemInHand`). The user joins `$3a8c` (`BattleState::holdingItem`): what it holds goes
  into the inventory after the action (`_c262c7`).
- An item that is the attack itself (`InitTarget`'s carry): `CalcItemEffect` with Throw and Tools
  as "thrown" (`cmp #$02` on the command). Unless the attack's status-1 byte holds Dead, Petrify or
  Zombie (`$c2`), its targets are cut to the alive (`$3a74`) and the enemy characters (`$3a42`), and
  an attack left with none is not aimed again (`$b3.2`). This applies to Throw and Tools as well.
- An item that casts a spell: Item becomes Magic and its attack the spell (`$3410`); Throw and Tools
  keep their command and show the item. No extra multiplier (`stz $bd` — a thrown edge is not
  doubled), the caster's stat and level (`InitAttacker`), ignore Reflect, undodgeable, never the
  fallen (`$ba.3`), no MP cost.

### The attack an item makes

`CalcItemEffect :6992-7093` clears the whole record (`$11a0-$11af`, level and attack stat with it),
then: physical and ignore defense (`$11a2 = $21`); re-target if invalid and ignore Reflect
(`$11a3 = $22`); undodgeable (`$11a4 = $20`). Thrown: the user's vigor (`$3b2c`) and level. Power
`+20`, element `+15`.

A consumable (not thrown): not physical; its use effect `+27` — bit 7 set, none; else
`effect + $48` into the effect space (`asl` then `adc #$90`); its statuses `+21`–`+24`; its `+19`
bits read by a shift chain: bit 7 fractional damage, bit 5 remove status, bit 4 affect MP **and**
restore (the chain sets bit 7 of the shifted byte, so the next bit tested — restore HP — reads set),
bit 3 restore, bit 1 invert on the undead; and a status-1 Dead in its statuses adds invert on the
undead and resurrection targeting.

Every item: Inviz Edge (`$ae`) adds Vanish, Shadow Edge (`$af`) Image (`:7064-7073`). A tool (type
0): not ignore defense, dodgeable, hit rate `+21`, and by `(item − $a3) & 7` (`ToolsEffectTbl`):
Noiseblaster's status-2 byte becomes Confuse; Chain Saw on a draw with its low two bits clear — one
in four — becomes the kill: tools animation 8, power 0, Dead, the stamina save alone (`$11a4 = $10`),
instant death; Chain Saw and Drill ignore defense; Debilitator `$56`; Air Anchor `$57`; Auto
Crossbow unsplit. Every type-0 item is one of the eight tools from `$a3` to `$aa`, so the `& 7` never
aliases.

### Magic and the commands that cast

- `_1765 :3691-3701`: the name kind, the spell aimed and loaded, the caster's stat (magic power, or
  vigor for a physical skill) and level; Slot's spells cost nothing (`$11a5`).
- Magic and X-Magic `:3658-3672`: Terra (actor 0) casting while battle switch 65 is set
  (`$3ebc.1`, "enables scene with LOCKE and EDGAR if TERRA uses magic") clears it and adds battle
  event 6 as an action that runs before anything else (`CreateImmediateAction`). It then sets the
  undodgeable bit, which the spell's record, loaded after it, replaces — no effect.
- Lore, Magitek, the Roulette command: kind 0. Summon: kind 2. Health (Cure 2, `$2e`) and Shock
  (`$82`): kind 5, the command's name.
- Slot `:3639-3652`: the swirl on; L.5 Doom is kind 7, Joker Doom; an attack below Fire Skean is an
  esper (Summon's path); Lagomorph shows message 7 ("Mugu mugu?") after it; anything else is Magic's
  path.
- The Roulette command `$1e` `:4136-4143`: cast as Lore (`$b5 = $0c`), then the roulette cursor
  animation (`$21`) with no params block of its own.

### Dance

`Cmd_13 :3707-3736`: Dance status set; no background change (`$b7 = $ff`); the dancer's dance
(`$32e1`, a `DanceId`), or with none the last one chosen (`$3a6f`, `state.lastDance`), which becomes
its own. Matching `BattleBGDance[$11e2]` → Magic's path. Otherwise a draw's low bit: set, the
background becomes `DanceBG[dance]` (`$b7`, `$11e2`) and Magic's path; clear, the stumble — Dance
cleared, message 6, shown as `$20`, the plain hit (`_c2298d` with `$20`, which has no aiming byte)
at the dancer itself. Every Dance carries a dance: the menu's choice, or the one chosen for a dancer
acting on its own.

### Morph, Revert, Row, Runic and Def

- Revert without Morph (status-4 `$08`) only puts the party member back to standing (`_c2192e`, the
  `$0e` script command) and makes no attack.
- Morph and Revert `:4046-4058`: the plain hit; its status-4 byte becomes Morph (`$11ad = $08`; the
  plain hit's record has no statuses); Revert adds remove-status (`$11a4.2`, from the carry the
  dispatcher's `asl` left clear); at the user (`ExecSelfAttack`).
- Row toggles the row (`$3aa1.5`), Runic sets Runic (`$3e4c.2`), Def sets defending (`$3aa1.1`);
  each then the plain hit at the user.

### Steal, Capture, Sketch, Control, Leap, Possess and GP Rain

- Steal `:3360-3365`: the plain hit with effect `$52`. Capture is a Fight whose blows carry `$52`.
- Leap `:4121-4130`: the plain hit with effect `$54`, not physical, targeting `$40`.
- Possess `:3847-3854`: aimed, undodgeable, effect `$50`. It never writes the level or the attack
  stat (`$11af`/`$11ae`, outside the record copy), so the original's are whatever the previous
  command left; the port's are 0.
- GP Rain `:4011-4024`: the plain hit, power one more, ignore defense and unsplit (`$11a2 |= $60`),
  damage modification off; a monster's is named by the command (kind 5); effect `$51`.
- Control `:4099-4115`: a party member in Chant (status-4 `$10`, the controller's status) goes back
  to standing, and the monster it controls (`$32b8`) runs the attack chosen for it as that attack's
  command (`_c21554 :3322-3328`). Otherwise the plain hit with effect `$53`, not physical,
  undodgeable.
- Sketch `:3299-3328`: the plain hit with effect `$55` (`$b7 = $ff`, which no Sketch reader reads).
  After it, if the effect recorded the monster it caught (`$3417`) and the attack (`$3400`): the
  sketcher takes the monster's special-attack animation and special-attack byte (`$3c81`, `$322d`),
  becomes the attack's maker (`$3417`), the next attacks are aimed at random (`$3415` cleared), the
  name may show again (`$b2.0`), and the attack runs as its own command **with the monster in Y**
  (`_c21554`). The body's stat load reads the monster's — `InitAttacker` with X from Y — while the
  level reads the sketcher's (`InitAttackerLevel` through `$3417`), and `ExecAttack :8067-8069` makes
  the sketcher the maker.

### Rage

`Cmd_10 :3334-3354`: no rage chosen (`$33a8` of `$ff`) takes the last rager's (`$3a93`, slot 0 if
none) — Mimic's Rage. The rager becomes the last; Rage status (status-4 `$01`) set; `SetRage
:999-1015`: the monster's special-attack animation, its attack graphic (`MonsterProp+26`) in both
hands' item, then `LoadRageProp :7504-7550` — special attack, weak elements, absorbed and nullified
elements, the blocked statuses of bytes 1-3, the record's statuses as statuses to set (status 1-2
whole, 3-4 masked `$84fe` with the flying flag as Float), status 2-3 kept on it, True Knight,
metamorph. `loadMonsterTraits()` is the shared part, as `LoadMonsterProp :7418` calls the same
routine. Then `FixImmuneStatus`, the attack chosen for it as its own command (`_c21554`), and
`UpdateImmuneStatus`.

### Bushido, Blitz and Jump

- Bushido `:3898-3927`: the technique aimed and loaded, `InitAttacker`. Retort (technique 1)
  toggles `$3e4c.0`. Now up: `ror $b6` makes the animation's operand `$80` — the stance with no
  technique — power 0, undodgeable, not physical, no name. Now down: the swirl on, then named (kind 4)
  like every other technique.
- Blitz `:3371-3392`: an operand with bit 7 set is a failed input — the intake brings one as Battle
  (`$ee`): hits no one (`$b3.0`), message `$43` (67), the attack becomes Pummel, no name. Any other is
  named (kind 8). The port takes a blitz as an attack from Pummel through the next seven, which is
  the same split over every value the intake and `GetCmdForAI` give.
- Jump `:3860-3892`: aimed; the shield hand when the weapon hand has no power, the weapon hand when
  the shield hand has none, else a draw's low bit (set, the shield hand); `_c2299f`; the record's
  `$11a4` becomes undodgeable alone (`sta`, not `tsb`); row ignored (`$b3.5`); extra multiplier one
  more (×1.5), or 2 with a spear — Mithril Pike through Imp Halberd, `$1d`-`$24` — in either hand
  (`SpearEffect`). A Dragon Horn (relic effects 2 bit 7, `$3c44`): the continuous jump (`$3a8e`), and
  one more landing, another on a draw below `$40`, another below `$10` — two to four in all. Hide
  (status-4 `$20`) cleared on the status itself.

## The command menu

`UpdateCmdList :13560-13617`, for a party member: the menu is marked to redraw (`$3a58`). For each
of the four rows (command `+0`, flags `+1`, targeting `+2`), the row is unusable — the carry rotated
into bit 7 of `+1` — when the command is empty (`bmi`), or when the party member is an Imp
(status-1 `$20`) and the command lacks the Imp flag (`BattleCommandProperties` flag `$04`); the
update handler is then not consulted. Otherwise `UpdateCmdIDTbl` finds the handler, or the row is
usable:

| Command | Handler | Unusable when |
|---|---|---|
| Morph | `_00 :13686-13689` | `$0f` ≥ the Morph supply (`$1cf6`): below 16 |
| Runic | `_01 :13677-13680` | neither hand's weapon flags (`$3ba4 \| $3ba5`) hold Runic (`$80`) |
| Bushido | `_02 :13668-13671` | neither holds Bushido (`$02`) |
| Lore, X-Magic, Magic | `_03`–`_05 :13655-13662` | Muted (status-2 `$08`) |
| Capture, Fight | `_06`/`_07 :13638-13649` | never; with an Offering (`$3c58.0`) the row's targeting becomes `$4e` |

Nothing writes a command's own targeting back while the battle lasts: `InitCmdList :13697-13807`
writes it once, at the start, and does not call `UpdateCmdList`, so a party member who starts the
battle wearing an Offering shows Fight's own targeting until the first refresh.

### What is not ported, and why

- `ExecSelfAttack :8004` clears `$341b`, which nothing reads.
- Sketch's `$b7 = $ff` has no reader on Sketch's path.

## The battle's own actions

`CommandExecutor::execute()` runs these when the battle core reaches them; each starts from the same
frame as a command (`ExecCmd :3093-3109`) and the ones that make an attack start from the plain hit
with no command's rules (`_c2298a` with a command past the menu's: `InitTarget :6460-6461` loads no
aiming byte). A battle's own action carries no command of its own; most are shown as Mimic, whose
animation is empty (`$b5 = $12`).

### Damage over time

`Cmd_22 :13153-13198`, queued by `rigenetimer :14990-15035` with the status that took effect —
`$04` Poison, `$02` Regen, `$40` Sap or Phantasm:

- No enemy left (`$3a77`): nothing, and the battler's beat stays pending (`$3aa1.4` is cleared only
  after this test).
- The beat is spent (`$3aa1.4` cleared); a battler held by a status (`$3aa0.4`) does nothing more.
- The plain hit, shown as Mimic; Vanish does not apply and the ignore-defense attack is multiplied
  (`$b3` loses `$90`); traits `$68`: ignore defense, no split, invert on the undead (replacing the
  plain hit's).
- `lsr $11a4 / lsr2 / rol $11a4`: bit 1 of the status (Regen) becomes the restore flag; the next
  bit, Poison, takes the battler's poison multiplier as the extra multiplier and the Poison element.
  The multiplier (`$3e24`) rises by two after each Poison beat, to 14, and nothing else writes it:
  it keeps growing for the whole battle, through a cure and a second poisoning.
- Power: max HP × stamina (`Mult24`) over 1024, plus 2, at most 255 (`:13185-13196`).
- An attack on the battler itself (`ExecSelfAttack`).

### A spell with no caster

`Cmd_26 :13030-13055`: the spell is `$b8`, the battler `$b6`. Doom also marks the condemned count
to stop (`$3204.4`) and does nothing more while no enemy is left; any other spell is cast regardless.
The spell is made as Magic, loaded and aimed (`InitTarget`), with the battler's magic power and
level (`InitAttacker`), the casting swirl on, flags 1 replaced by "ignore Reflect", undodgeable, no
MP, at the battler itself.

### Status timers running out

`Cmd_29 :13335-13367`, queued by `timer :14940-14963` with one bit per timer: the plain hit, shown
as Mimic, the swirl on; Stop (status-3 `$10`), Reflect (status-3 `$80`), Frozen (status-4 `$02`) to
take off. Sleep comes off only if the battler is still asleep (status-2 `$80`), and then the attack
is made as Magic with attack `$78`, Tapir; a sleep timer that ran out on an awake battler takes
nothing off and shows nothing. Remove-status; at the battler itself.

### The party's escape

`Cmd_2a :5637-5696`: nothing unless the party is running (`$2f45`). A party that cannot run
(`$b1.1`) is shown message 9, "can't run away". Otherwise, with no one just escaped (`$3a38`)
nothing; the escaped set is cleared, and each of them, last party slot first, leaves when its gauge
is full (`$3219` of 0), it is not held (`$3aa0` bits 4 and 6), it is present, and it is neither a
zombie nor hidden: it has left the battle (`$3a39`), it will be gone after the action (`$2f4c`), its
gauge empties after the action (`$3aa1.6`), all its actions are removed (`$3204.6`) and its links are
released (`_c207c8`). With anyone leaving, the run animation (`$22`) plays with the params of the
last one handled — the lowest slot — as attacker and everyone leaving as the targets; with no one,
nothing shows.

### Seize's drain

`Cmd_2d :13385-13406`, queued for the seizer with the seized as its target: the beat is spent; the
plain hit, attack stat 0, level 16, power 16, traits `$28` (ignore defense, invert on the undead),
flags 1 "ignore Reflect", drain; aimed exactly as given (`$3a46.1`); the drain not capped by the HP
the seizer is missing (`$b2.7` cleared); shown as Mimic; at the seized (`ExecAttack`).

### A formation change

`Cmd_20 :13206-13244`. The formation is `$b8`-`$b9` with bit 15 meaning "back to full HP and MP"
(`$3a47.7` is set when it is clear); bit 15 is stripped before `LoadBattleProp`, so the
random-of-four bit is never seen, while the conditional battles still replace the formation. Every
monster slot starts over (`:13219-13228`): its battle flags and triggers cleared, no monster in it,
the start's recalculations pending (`$ffbc`). Then `InitMonsters`: each slot's monster loaded; with
"keep" the HP and MP loads are skipped (`LoadMonsterProp :7370-7384`), so the new monster has the HP,
max HP, MP and max MP its slot held, and its stamina follows that max HP. A slot's statuses and the
monsters marked dead or fled (`$3a3a`) are not touched. `UpdateStatus`, `AfterAction1`,
`AfterAction2`, `UpdateDead`, `InitStatus`, `ChooseBattleType`; the monsters are placed for the new
battle type (`InitMonsterPos btlgfx:2838-2857`, from `GfxCmd_12`); the change is shown
(`FormationChanges`), and the monsters still fighting become the monsters standing.

### Monsters leaving and coming back

`Cmd_24 :13061-13083`: the mode is `$b8`, the monsters `$b9` (only slots that hold a monster,
`$3a73`), the entrance `$b6`. Per monster (`MonsterDeathTbl`):

| Mode | Handler | What happens |
|---|---|---|
| 0 | `_00 :13118-13125` | HP to max, then mode 2 |
| 1 | `_01 :13098-13099` | no longer a target (`$2f4d`), then mode 3 |
| 2 | `_02 :13130-13137` | revived: no longer marked fallen, standing, counted, joining after the action; the battle may end again |
| 3 | `_03 :13105-13113` | hidden: not counted, not standing, Hide set |
| 4 | `_04 :13088-13093` | the battle may not end, then mode 3 |
| 5 | `_05 :13142-13147` | mode 3, then Dead set |

The script shows the hidden and the revived (`MonstersEnterOrLeave`).

### Scan

`Cmd_27 :13274-13329`: four kinds of message, each played as a script of its own before the
action's (`ExecBtlGfx $04` per message): the level (52), HP and max HP (48), MP and max MP (49)
unless max MP is 0, and one "weak against" message from 21 per element bit, Fire first, for the
elements weak and neither halved, absorbed nor nullified. The action's own script is empty.

### The level a condemned count reads

`StartCondemn :1512-1525` reads `$11af`, the level the last attack was made at. Every writer of it is
an attack's setup: `InitAttackerLevel :7291-7299`, the plain hit's clear (`_c2298d :6907`), the
item attack's clear (`CalcItemEffect :6999`), Runic's (`_c2385e :9058-9062`), Umaro's
(`InitUmaroAttack :3838`) and Seize's 16 (`:13394`); the battle's start sets 20 (`:6131`). An action
that writes none — a dialog, a formation change, Possess, a damage-over-time beat with no enemy left —
leaves the last attack's level in place, and a count started after it (a formation change's new
monsters, say) is worked out from that. `CommandExecutor::execute()` starts each action's attack at
`BattleState::lastAttackLevel` and writes the attack's level back when the action ends.

### The rest

`Cmd_21` shows a monster dialog, `Cmd_25` a battle message, `Cmd_23` plays a battle event, `Cmd_2c`
resets a party member's pose and `Cmd_2b` plays a monster animation; each only writes its command.

## Automatic actions

### A party member acting on its own

`QueueAction :537-568`, for a party member with nothing pending: Dance status → the dance's step
(`RandDanceAction`); else Rage status → the rage's attack; else Magitek → a beam; else
`RandCharAction`. Each is given its advance wait and aimed (`CalcCmdDelay`) — except a command from
`$1e` up, which `CalcCmdDelay :621-622` skips entirely. An X-Magic pick becomes two actions: a Magic
with a second spell drawn by `RandMagic`, added first, then the X-Magic with the first.
`FixRoulette` turns Lore's Roulette into the Roulette command (`$1e`), which therefore takes no
advance wait and no aim.

`RandCharAction :656-745`:

- The candidates are the four menu commands and a Fight that is never cleared (`$fe`/`$ff`, zeroed at
  `:662-663` and counted in `$f5 = 5`).
- A row drops out when it is empty, when `ConfusedCmdTbl` lacks the command, or, under the berserk
  rules, when `BerserkCmdTbl` lacks it. The berserk rules apply when `$f4` is not negative: Berserk
  zeroes it; otherwise it is status-2 shifted left twice with the charm attacker's inverted top bit
  and the colosseum flag, negative when confused, charmed or in the colosseum. So a zombie that is
  none of those takes the berserk rules too.
- The surviving rows whose command has a chooser (`RandCmdIDTbl`/`RandCmdTbl`) draw their attack,
  last row first; a chooser with nothing to make drops the row (`RandCmdInvalid`). Any other command
  makes attack 0.
- One candidate is drawn (`RandA($f5)`), counting the extra Fight first and then the rows last
  first.

The choosers:

| Command | Chooser | Nothing to make when | Draw |
|---|---|---|---|
| Magic, X-Magic | `RandMagic` | Muted, or no spells (`$3cf8`) | `RandA(count)`, then the list's magic positions from the last back, counting the filled ones |
| Lore | `RandLore` | Muted, or no lores known (`$3a87`) | the same over the lores; a walk that runs out gives Condemned |
| Bushido | `RandBushido` | no Bushido weapon in either hand | `RandA(known)` from Dispatch |
| Blitz | `RandBlitz` | — | one of the known blitzes' bits (`RandBit`) from Pummel |
| Morph | `RandMorph` | Morph supply below 16 | — |
| Magitek | `RandMagitek` | — | `RandA(3)` from Fire Beam |
| Tools | `RandTools` | — | one of the owned tools' bits from Noiseblaster |
| Dance | `RandDance` | — | see below |
| Rage | `RandRage` | — | see below |

`RandBit` draws only when its mask has a bit set; with none it gives bit 0 without drawing.

### The dance step, the rage attack and the esper

- `RandDance :920-947`: a dancer with no dance (`$32e1` of `$ff`) takes one of the known dances'
  bits. Then one draw against `DanceRateTbl` (`$10`, `$30`, `$90`): below each, one step further —
  the first step 7/16, the second 3/8, the third 1/8, the fourth 1/16.
- `RandRage :961-993`: a rager with no monster takes the n-th of the known rages, n = `RandA(count)`;
  with none known it rages as monster 0 and reads its attacks at the list's end marker, monster 255.
  Then a coin between the monster's two Rage attacks.
- `RandGenju :8968-8977`: `RandA(25)`, two further from the eleventh — every esper but Odin and
  Raiden.

### A monster picking its own attack

`ExecAction :213-224`: a monster's AI turn in the colosseum, charmed (`$3395`), berserk or confused
(status-2 `$30`) goes to `RandMonsterAction :1021-1055` instead of its AI: from a slot drawn by
`Rand & 3`, the first filled Control attack counting down, round again; a berserk monster (status-2
`$10`) makes Battle; the command is `GetCmdForAI`'s; aimed and added with its advance wait.

## Items obtained

`_c262c7 :16168-16187`, at the end of every action (`ExecCmd :3126`, after the script and the
after-action checks): for each party member marked as holding (`$3a8c`), last slot first, the mark
is cleared, and an item still in hand (`$32f4`) goes into the inventory, one of it (`ExecBtlGfx $05`),
and the hand is emptied.

## What a hit target records

`_c235e3 :8578-8613`: who hit it with which command as chosen, with which spell, with which item
(when the command used one), and with which elements. `_c2361b`: a target not waiting to answer an
attack is marked to answer this one; one already waiting takes the new attacker on heads.

## Hit in the back

`UpdateFacingDirection :3218-3271`, for a blow only: in a side attack a party member hits in the
back every monster facing the way it faces; in a pincer a monster does the same to the party.
Both facings mean "facing right": the monster's is stored inverted from its sprite's flip
(`UpdateFacingDir btlgfx:39958-39972`).

## The battle layout

Where the battlers stand decides two things the battle reads: which side of a pincer each monster
is on (`$2eac`/`$2ead`, read by `CheckTargetsBattleType :14751-14753` and the run check
`:15184-15193`), and which way each battler faces (`$2f50`/`$2f51`, read by
`UpdateFacingDirection`). The port keeps, per battler, the part of the field it stands in — left,
center or right — and its facing.

### The monsters

Each monster's sprite stands with its left edge at its formation's x nibble × 8
(`btlgfx:2019-2022`) and its width in tiles measured from its stencil (`InitStencil`, see
`docs/engine/data-layer/monster-sprites.md`). `InitMonsterPos :2838-2857` clears every flip and
places them by battle type:

| Battle type | Placement | Part of the field | Facing |
|---|---|---|---|
| Front (`_00 :2689-2699`) | as the formation puts it | left | right |
| Back (`_01 :2705-2730`) | mirrored about x = 128: left edge 256 − (x + width) | right | left |
| Pincer (`_02 :2736-2778`) | right edge below 104 stays; otherwise mirrored about x = 160: left edge 320 − (x + width) | left / right | right / left |
| Side (`_03 :2784-2820`) | moved 48 right | center | left if its middle is below 128, else right |

The monsters on the left are the pincer's first side, those on the right its second. The colosseum's
one monster is centered (`:2858-2891`); in a front attack that changes no side. The layout runs
when the battle starts and when the final battle's formation changes (`GfxCmd_12
btlgfx:22118-22149`); neither ever sees an Imp's sprite, which only an animation command turns on.

### The party

`CharDirTbl` gives each party slot's facing by battle type (0 left, 1 right) and `CharXAngleTbl`
where it stands: an angle from a point above the screen, 256 straight down the middle. A party
member above 256 stands right of the middle, below it left, on it in the center; the parser proves
every member off the middle faces toward it, and that `CharFlipTbl` says the same as `CharDirTbl`.
A party member fighting against the party (a scripted battle's enemy) faces the other way
(`btlgfx:8457-8467`). At the next stage of the final battle the party keeps the way it faces.

### Turning to face a target

`_c19aa2` turns an attacker toward the target of the animation shown: the lowest-numbered monster
it is aimed at, or with none the lowest-numbered party member (`GetTargetID`). A party member turns
only for a monster, a monster only for a party member; an animation aimed at no one turns no one.
It faces right when the target's part of the field is further right than its own.

Every command's animation turns its attacker, and so does the ready stance (`GfxCmd_0c →
init_all_anim`), except:

- Mimic, Def and the empty command animations show nothing (`CmdAnim_12`/`15`/`1e`/`1f`); Row
  moves the party member without aiming (`CmdAnim_14`); the roulette cursor aims at no one
  (`CmdAnim_21`). None of them turns anyone or changes the targets remembered.
- Once the party runs (`CmdAnim_22`), no party member turns for the rest of that script
  (`w7e62a4`, cleared by `BtlGfx_04 btlgfx:22393`).
- A Jump's landing (`CmdAnim_16`) turns toward the targets of the last animation shown before it,
  not its own: it never sets the animation's targets.

The port turns attackers as each action's script is handed to the presentation, in script order;
the battle reads no facing while a script plays.

**The side rule is the original's comparison of screen positions.** The original compares the
middles of the two sprites (`_c10659 btlgfx:1030-1107`: a monster's left edge + width × 4; a party
member's place from `CalcCharXPos btlgfx:8171-8197` + 8). `tests/test_battle_layout.cpp` checks,
for every formation and every battle type it allows, every party member front row and back, on the
battle type's depths and the magitek train car's, against every monster both ways round, the
colosseum's fighter against every monster and the scripted battles' cast at their own places:
the part of the field gives the same answer and no two middles are equal. Gau's place on the Veldt
is never compared — he appears only once every monster is down (`:11933-11942`).

**Not followed: an attacker that has stepped forward.** A party member steps 24 pixels toward its
target to attack. In two side-attack formations, Cephaler ×3 and Orog + Power Demon ×2, that
step carries party member 0, front row, past a monster whose middle is at 184, and a second
animation in the same script turns it around. The side rule does not follow the step; the test
pins these twelve cases as the only ones.

## Lores

`LearnLore :8793-8819`: an attack that can be learned, seen by Strago when he is none of Dead,
Petrify, Blind, Zombie, Sleep, Confuse, Berserk, Stop, Frozen, Rage or Hide, is learned this battle
unless he knows it.

## Defects reproduced

Listed in [`docs/Bugs.md`](../Bugs.md): blows evaded by magic block, with Blind, Beads and the
status modifiers never applied; a vanished target hit by any spell before death protection is
checked; the hit-in-the-back steps counting for every later target; a pincer that cannot be run
from at its start even when every monster stands on one side; Umaro's throw adding one to the power
of Mog and every ally after him.
