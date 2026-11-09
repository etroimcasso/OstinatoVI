# Battle AI — behavioral contract

Behavioral specification for a monster's AI script as the battle reads it: how a script is readied
when its monster takes a slot, how a turn and a counterattack read it, who it names as its targets,
what each of its conditions tests, and the three commands only a script queues. All citations point
into the pinned reference tree `original-src/` (everything8215/ff6 @ `1ea47b5`); line numbers are into
`src/battle/battle_main.asm` unless another file is named. This document is the test authority for
`tests/test_ai_targets.cpp`, `tests/test_ai_conditions.cpp`, `tests/test_ai_oracle.cpp`,
`tests/test_ai_run.cpp`, `tests/test_ai_commands.cpp` and `tests/test_ai_run_oracle.cpp`.

The scripts themselves are data: `src/data/ai_scripts.h`, described in
[`docs/engine/data-layer/monster-ai-scripts.md`](../engine/data-layer/monster-ai-scripts.md). The rules
here are `src/battle/ai.h`, `src/battle/ai_targets.h`, `src/battle/ai_conditions.h` and the three
commands in `src/battle/system_commands.h`. Every claim about the routines named below is also checked
against the cartridge itself: each routine is run on the cartridge, hosted on the engine's SNES
machine, over the same battle the port is given (`tools/damage-trace/damage_oracle.h`) — every one but
the two effects that make an attack (H).

The cells the script reader keeps: `$f0` the place it is at, `$f2` the place to go on from after a
wait, `$f4` the blocks skipped when the part last stopped at a wait, `$f5` the blocks skipped this
run, `$f6` the battler, `$f8` restricted (one byte, 0 when not), `$fc` the targets now, `$fe` the
targets the run started with, `$3a2c`–`$3a2f` the four bytes of the step it is at (a condition's are
`$fc`, its number, its two operands). A place is a byte offset into the cartridge's script block
(`AIScript`, `$CF8700`). A battler is named in the cartridge's cells by twice its slot index; a set of
battlers is a word with the party's bits in its low byte and the monsters' in its high byte (`$3018`, a
battler's bit).

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| A script readied | `InitAI :7442-7466`, `FindAIScriptEnd :4233-4249` | `readyAi()` in `ai.cpp` |
| A turn's run | `ExecMonsterAction :447-463` | `runAiTurn()` |
| A counterattack's run | `ExecAIRetal :12468-12508` | `runAiCounter()` |
| One step | `ExecAI :4215-4227`, `NextAICmd :4341-4382`, `ExecAICmd :4332-4334`, `conditionmiss :4314-4325` | `PartRun::read()` in `ai.cpp` |
| The steps that act | `AICmd_f0`–`ff :4271-4549`, `AIRand3 :4292-4301` | `PartRun::act()` |
| An attack's command | `GetCmdForAI :5006-5032` | `commandForAttack()` (`battle/commands.h`) |
| The AI's Roulette | `FixRoulette :574-582` | `kRouletteCommand` (`battle/attack.h`) |
| A command's wait and aim | `CalcCmdDelay :614-647` | `addAdvanceWait()` (`battle/actions.h`) |
| Where the action waits | `CreateDefaultAction :12900-12964` | `addActionOrCounter()` |
| A variable changed | `Cmd_2e :5049-5065`, `_1e38 :5088-5093` | `changeBattleVariable()` (`battle/system_commands.h`) |
| A switch changed | `Cmd_2f :5071-5085`, `_c21e57 :5119-5126` | `changeBattleSwitch()` |
| An effect | `Cmd_30 :5132-5137`, `MiscAIEffect_00`–`_0d :5142-5309` | `runAiEffect()` and its table |
| Who a script names | `CheckAITarget :5318-5366`, `AITargetTbl :5565-5589`, the handlers `:5372-5561` | `aiTargets()` and its table in `ai_targets.cpp` |
| A pick of one | `RandBit :13495-13515`, `RandA :12390-12404` | `BattleRng::pickFrom()` |
| A coin | `RandCarry :12367-12372` | `BattleRng::coin()` |
| Those on the field | `CheckTargetsPresent :14807-14833` | `removeAbsentTargets()` (`battle/targeting.h`) |
| Those in reach | `_c25917 :14632-14648` | `reachableTargets()` (`battle/targeting.h`) |
| What a script tests | `AICondTbl :4944-4973`, `AICond_00`–`_1c :4555-4939` | `aiCondition()` and its rules in `ai_conditions.cpp` |
| The target test's battler | `BitToTargetID :13438-13452` | `lastIn()` in `ai_conditions.cpp` |
| A variable | `GetBattleVar :5103-5113` | `BattleState::battleVariables`, `Battler::aiVariable`, `game::BattleSwitches` |
| The names listed | `UpdateMonsterGfxBuf :15169-15235` | `refreshRunDifficulty()` writes `BattleState::monsterNamesListed` |
| The reach flag a command leaves | `ExecCmd :3105-3108`, `ExecSelfAttack :8004` | `BattleState::awayFromTheFieldUnreachable`, written at the end of `CommandExecutor::execute()` |

## A. Where the battle calls a script

| Caller | Line | What it hands over |
|---|---|---|
| A monster's turn: `ExecAction` finds the AI action at the head of the battler's list, removes it, and unless the battle is the colosseum's or the battler is charmed or holds Berserk or Confuse, runs `ExecMonsterAction` | `:211-224` | the battler; no targets (`QueueAction :489-490` cleared them) |
| A party member a scripted battle gave a script, when its advance wait starts | `QueueAction :509-512` | the battler; no targets; Confuse, Berserk and Charm do not divert it |
| A counterattack: `ExecRetal` finds the AI action at the head of the counter list, removes it and runs `ExecAIRetal` | `:12416-12425` | the battler; the battler it answers, or none |
| A monster loaded: the battle's start, a formation change, a colosseum opponent | `LoadMonsterProp :7313` | the monster, its slot |

A run starts with its targets (`$fc`) and the targets it started with (`$fe`) both the targets handed
over (`ExecAI :4218-4223`), and restricted as its entry sets it (`ExecAICmd :4332-4334`). A target step
(`AICmd_f1 :4450-4457`) sets the targets to `CheckAITarget` of its operand with the cut (E); a condition
(`AICmd_fc :4307-4312`) runs its routine with the carry clear (`asl` of the number, `:4309`) and the
battler in Y, and goes on to the next step when the routine returns with the carry set.

The port's seams: `readyAi(state, slot)`, `runAiTurn(state, executor, actor)`, `runAiCounter(state,
executor, actor, answering)`, `aiTargets(state, actor, target, reachableOnly)` and `aiCondition(state,
actor, condition, run)`, with `AiRun` holding `$fc`, `$fe` and `$f8`. The battle's own seams —
`CommandExecutor::initAi()`, `runAi()` and `runCounterAi()` — do not call the first three; a battle
fought through `CommandExecutor` runs no script.

## B. A script readied — `InitAI`

1. The turn part's place (`$3254,y`) ← `AIScriptPtrs[index]` (`:7447-7450`).
2. `FindAIScriptEnd` is called until it returns the part's end (`:7453-7455`): past every end of a
   block to the end of the part.
3. The byte after that end: `$ff` → nothing more is written (`:7456-7458`); anything else → the
   counterattack part's place (`$3268,y`) ← there (`:7459-7461`). Script 243 (the fourth Mag Roader,
   `ai_script.asm:2530-2537`) has no end of its counterattack part: the byte after its end is the first
   byte of script 119 (Wild Cat) at offset 3410 of the block, so **its counterattack part is Wild Cat's
   turn part** and ends where that ends.
4. **Nothing else is written.** A battle starts (`InitBattle :6056-6067`) with every place `$ffff` (no
   script), both counts `$ff`, both places to go on from 0, the battler's own variable (`$3dac`) and
   timer (`$3dc0`) 0. A formation change (`Cmd_20 :13219-13230`) clears only `$3aa8`, `$3e54`, `$2001`
   and `$320c` before it loads the new monsters, so **a new monster in a slot keeps the old one's
   counts and places to go on from, its own variable and timer, and — when the new script has no
   counterattack part — the old one's counterattack part.**

Port: `readyAi()` sets `Battler::turnScript` and, only when the script has one, `counterScript`, and
`hasCounterScript` to whether a counterattack part is set; nothing else. `changeFormation()` leaves the
fields alone. A battler with no script (`Battler::aiScript` empty) is left as it is.

## C. A run — `ExecMonsterAction`, `ExecAIRetal`, `ExecAI`, `NextAICmd`

**Entry.** A turn: `$3a98` ← 0 (not restricted); place ← the turn part, place to go on from ←
`$3d0c,x`, saved count ← `$3240,x` (`:449-455`). A counterattack: G sets `$3a98`, then place ← the
counterattack part, place to go on from ← `$3d20,x`, saved count ← `$3241,x` (`:12494-12499`). `ExecAI`:
`$fc` and `$fe` ← `$b8` (a word: the targets handed over), `$f5` ← 0, `$f6` ← the battler
(`:4218-4223`); `ExecAICmd` copies `$3a98` to `$f8`. **Exit:** the place to go on from and `$f5` are
stored back (`:458-462`, `:12502-12506`). **Every run starts from the top of its part.**

**One step** (`NextAICmd`):

1. The four bytes at the place are loaded (`GetNextAICmd :4255-4265`).
2. **The resume rule** (`:4345-4354`): if the step is not a condition, a wait, an end of a block or the
   end (its byte below `$fc`) **and** `$f5` equals the saved count `$f4`: the saved count becomes `$ff`,
   the place becomes the place to go on from, and the step there is loaded instead — whatever kind of
   step it is. The rule is tested at every step that acts, not only the first: a run that skips a
   block after acting (a condition after acting steps) can reach the saved count later. It fires once
   a run at most.
3. The place moves past the step (`:4355-4368`).
4. **Restricted** (`:4370-4374`): if `$f8` is not 0 and the step acts (byte below `$fc`), the block is
   skipped (step 6) as if a condition had failed.
5. The step (`:4375-4382`):
   - a condition (`AICmd_fc :4307-4312`): its routine (F); the carry set → the next step, with `$f8`
     as the routine left it; clear → the block is skipped.
   - a wait (`AICmd_fd :4271-4275`): the place to go on from ← the place (already past the wait);
     **the run ends** with `$f5` as it stands.
   - an end of a block or the end (`AICmd_fe :4282-4286`): `$f5` ← `$ff`; **the run ends**.
   - anything else: D, then the next step.
6. **The block is skipped** (`conditionmiss :4314-4325`): `$fc` ← `$fe`; from the place, the next end
   of a block or end is found (`FindAIScriptEnd`); the end → `$f5` ← `$ff` and the run ends; an end of a
   block → the place is past it, **`$f5` += 1**, `$f8` ← `$3a98` again, the next step.

In the game's terms: each run reads the part from its top; a block whose condition fails is skipped and
counted; the end of a block or the part's end ends the run, and the next starts fresh; a wait ends the
run and remembers the step after it and how many blocks had been skipped; the next run goes on from
that step once it reaches a step that acts having skipped the same number. A condition may stand after
acting steps in a block (for example `ai_script.asm:4264-4266`, a variable added to and then tested):
it tests the battle as it is then — an action queued earlier in the run has not happened yet, so the
variable has not changed.

Port: `PartRun::read()` in `ai.cpp`, over the part's `AiStep`s. The count is a `std::uint8_t`
(`AiResume::blocksSkipped`, `kNoResume` = 255) and the place to go on from a pointer to a step
(`AiResume::at`), into the script the part was read from — after a formation change, the old
monster's (B.4). The cartridge's place 0 is `at == nullptr`; no run resumes to it, since a count other
than `$ff` is stored only by a wait, which also stores the place.

## D. The steps that act

**D.1 — an attack** (a byte below `$f0`, `_1b25 :4428`): the command is `GetCmdForAI` of the attack. No
random number is drawn. → D.5.

**D.2 — one of three attacks** (`$f0`, `AICmd_f0 :4426-4427`, `AIRand3 :4292-4301`): `RandA(3)` picks one
of the three operands; the value `$fe` is **nothing**: no action, and the run goes on to the next step.
Otherwise as D.1. The source writes a lone attack numbered `$f0` or above as three equal operands
(`ai_script.inc:83-91`), so that form draws where D.1 does not.

**D.3 — one of three commands** (`$f4`, `AICmd_f4 :4413-4416`): the same draw and the same nothing; the
action is that command with attack 0.

**D.4 — an item used or thrown** (`$f6`, `AICmd_f6 :4392-4403`): Item for an operand of 0, Throw
otherwise; the first item is copied over the operand byte, so the draw is among *first, first,
second*: the first two times in three. The value `$fe` is nothing, as above.

**D.5 — the action** (`_1b28 :4431-4444`): the targets are `$fc`; while the battler's status holds
**Confuse** (`bit #STATUS12::CONFUSE` on the status now, `:4436-4439`) they are empty. Lore's Roulette
becomes the AI's Roulette command (`FixRoulette`). `CalcCmdDelay`: a command below `$1e` adds its
advance wait and is aimed — targets given stand, none are chosen (`ChooseTarget`); the AI's Roulette
(`$1e`) takes neither. `CreateDefaultAction` puts it on the counterattack list while a counterattack
runs (`$b1` bit 0), else on the battler's own list. **`$fc` is not cleared**: the targets stay for every
later action of the run until a skipped block puts back `$fe` or a step changes them.

**D.6 — targets set** (`$f1`, `AICmd_f1 :4450-4457`): `CheckAITarget` with the operand and the carry
clear (E — the cut applies); `$fc` ← the set, empty included.

**D.7 — the steps that queue an action of their own** (`_1b78 :4510-4519`, `AICmd_f7 :4526-4533`,
`AICmd_fa :4539-4549`): the command with the step's first operand as its attack byte and the next two
as its targets word; `CreateDefaultAction`; no wait and no aim. The targets word is whatever the step's
next two bytes are: for a step three bytes long (a dialog, a variable change, an effect) its high byte
is the first byte of the step after it, and the battle event step (`AICmd_f7`) writes no targets word
at all — its action carries the targets the run last set. No command reads those bytes: a dialog and a
battle event read only their attack byte, a variable change and an effect only the targets word's low
byte.

| Step | Action | What the command reads |
|---|---|---|
| `$f2` change battle | `FormationChange` | the exit and the scroll flag (attack byte), the formation and restore flag (targets word) |
| `$f3` monster dialog | `MonsterDialogAction` | the dialog's low byte (attack byte); 117 uses, 89 dialogs, the highest 140 |
| `$f5` show or hide monsters | `MonsterVisibilityAction` | the entrance (attack byte), the mode and the monsters (targets word); **a mask of 0 becomes the battler's own monster bit** (`AICmd_f5 :4464-4470`, `$3019,y`: none for a party member) |
| `$f7` battle event | `BattleEventAction` | the event (attack byte) |
| `$f8` change a variable | `BattleVariableAction` (H.1) | the variable (attack byte), the change and amount (targets word's low byte) |
| `$f9` change a switch | `BattleSwitchAction` (H.2) | the change (attack byte), the variable and the bit (targets word) |
| `$fa` monster animation | `MonsterAnimationAction` | the animation (attack byte), the monsters — or for the sound effect its pan and number (targets word) |
| `$fb` an effect | `AiEffectAction` (H.3) | the effect (attack byte), its operand (targets word's low byte) |

Port: `PartRun::act()` in `ai.cpp`, one overload per kind of step.

## E. Who a script names — `CheckAITarget`

On entry: A the target operand; **carry clear = cut the sides to those in reach, carry set = do not**
(`:5338-5340`); Y the battler whose script it is. It returns the set in `$b8` and the carry set when the
set is not empty (`AITarget_46 :5379-5387`).

**The sides** (`:5322-5337`): every party slot holding a party member (`$3ed8,x` not negative —
`Battler::actor`) and every monster slot holding a monster (`$2002,x` not negative —
`Battler::monster`), on the field or not. With the cut they are then cut to those on the field
(`CheckTargetsPresent`: present, `$3aa0` bit 0; not Dead, and for a monster not Petrify or Zombie; not
Hide — `removeAbsentTargets()`) and to those in reach (`_c25917`: counted in the battle and targetable
or present, `$3a78` and `$3408`; for a party member not held in Seize, `$3403`; for a monster not made
untargetable by its AI, `$2f46`; and, while the word at `$341a` is negative — **`$341b` as the last
command left it** — not away from the field, `$3f2c`).

| Operand | Name | Line | The set |
|---|---|---|---|
| below 48 | a party member, by `CharacterPropId` | `:5342-5352` | the party slot whose `$3ed8` is the operand, slots searched last to first; none → empty. The sides are not read |
| 48–53 | `MONSTER_SLOT_1`–`_6` | `:5353-5362` | that slot if it holds a monster, on the field or not, alive or not; else empty |
| 54 | `SELF` | `AITarget_36 :5506-5510` | the battler |
| 55 | `ALL_OTHER_MONSTERS` | `_37 :5403-5411` | the sides less the battler, monsters only |
| 56 | `ALL_MONSTERS` | `_38 :5409-5411` | the sides' monsters |
| 57 | `RAND_OTHER_MONSTER` | `_39 :5413-5418` | one of 55's, picked |
| 58 | `RAND_MONSTER` | `_3a :5416-5418` | one of 56's, picked |
| 59 / 60 | `ALL_DEAD_CHARS` / `RAND_DEAD_CHAR` | `_3b`/`_3c :5420-5426`, `_c22037 :5529-5542` | the party slots whose status holds Dead (`$3ee4` bit 7, read as the high byte of a word at `$3ee3`), all or one picked; the ten slots' statuses are read and the sides are not |
| 61 / 62 | `ALL_DEAD_MONSTERS` / `RAND_DEAD_MONSTER` | `_3d`/`_3e :5428-5434` | the same for the monster slots |
| 63 / 64 | `ALL_REFLECT_CHARS` / `RAND_REFLECT_CHAR` | `_3f`/`_40 :5436-5442`, `_c2204e :5548-5561` | the party slots whose status holds Reflect (`$3ef8` bit 7) |
| 65 / 66 | `ALL_REFLECT_MONSTERS` / `RAND_REFLECT_MONSTER` | `_41`/`_42 :5444-5450` | the same for the monster slots |
| 67 | `ALL_CHARS` | `_43 :5396-5398` | the sides' party members |
| 68 | `RAND_CHAR` | `_44 :5372-5377` | one of them, picked |
| 69 | `PREV_ATTACKER` | `_45 :5470-5481` | the battler in `$32e0,y` when that byte is below 10: `Battler::lastAttacker` unless `retaliationPending` (the byte's top bit); else empty |
| 70 | `ALL_TARGETS` | `_46` | the sides |
| 71 | `NOTHING` | `_47 :5393-5398` | empty |
| 72–75 | `CHAR_SLOT_1`–`_4` | `_48`–`_4b :5488-5501` | that party slot if it is present (`$3aa0` bit 0); else empty |
| 76 | `RAND_TARGET` | `_4c :5452-5456` | the sides less the battler; then a coin (`RandCarry`): **heads** (the carry set) → all of them, **tails** → one of them picked |
| 77 | `TARGETTING_TARGET` | `_4d :5458-5467` | the battler Targetting marked (`$32f5,y`, `Battler::targetingTarget`): **the mark is cleared as it is read**, then cut to those on the field (`CheckTargetsPresent`, whatever the carry); no mark → empty |

A pick (`RandBit`) draws one random number when the set is not empty and none when it is; the number
scaled by the count (`RandA`) is the index of the member taken, counted from the first party member
up. `RAND_TARGET` draws its coin before its pick.

## F. What a script tests — `AICondTbl`

Entered with the condition's two operands in `$3a2e`/`$3a2f`, Y the battler, the carry clear. Carry set
on return = the condition holds. "→ targets" is what it leaves in `$fc`.

**The target test** (`AICond_17 :4920-4939`, a condition of its own and the first step of six others):
`CheckAITarget` with the first operand and **the carry set** (no cut); a set that is not empty → `$fc` ←
it, Y ← **its battler in the highest slot** (`BitToTargetID` searches from the sixth monster down to the
first party member) and it holds. Then, **when the operand is `SELF`**, the low byte of `$fc` is cleared
— twice (`stz $fc; stz $fc`, `:4936-4937`, bytes `64 FC 64 FC` at `$C21D4F`), whether the set was found
or not. The party's half goes and **a monster's own bit, in the high byte, stays**: after a test on
`SELF` a monster's targets are itself, and a scripted party member's are empty.

| # | Port | Line | Holds when | → targets |
|---|---|---|---|---|
| `$00`, `$0a` | `ai::Never` | `:4900-4902` | never (the same routine) | — |
| `$01` | `ai::HitByCommand` | `:4667-4682` | it was last hit with a command (`$3290,x` not negative) and that command (`$3d48,x`) is one of the two | who did it |
| `$02` | `ai::HitByAttack` | `:4659-4661` → `$01` at `$3291`, `$3d49` | the same for the last spell or skill | who did it |
| `$03` | `ai::HitByItem` | `:4650-4653` → `$01` at `$32a4`, `$3d5c` | the same for the last item | who did it |
| `$04` | `ai::HitByElement` | `:4689-4698` | it was last hit (`$32a5,x`) and that attack's elements (`$3d5d,x`) share one with the operand | who did it |
| `$05` | `ai::Damaged` | `:4704-4712` | an attack has taken HP or MP from it (`$327c,x` not negative — `lastDamagedBy`) | who did it |
| `$06` | `ai::HpAtMost` | `:4555-4564` | the target test, and `operand × 128 ≥` Y's HP — **at most**, the operand an HP in steps of 128 | the target test's |
| `$07` | `ai::MpAtMost` | `:4571-4578` | the target test, and `operand ≥` Y's MP | the target test's |
| `$08` | `ai::HasStatus` | `:4585-4622` | the target test; for a status numbered 16 or more (from Dance on) the set is first cut to the alive (`$3a74`); every member without the status is dropped; **not empty → one of them is picked**, a random number drawn | the one picked; empty when none has it |
| `$09` | `ai::NoStatus` | `:4629-4631` | `$08` does not hold: no member of the set has the status — or the target test found no one | as `$08` left them: the one picked, empty, or untouched when the target test failed |
| `$0b` | `ai::HalfOwnTimerAtLeast` | `:4723-4728` | **half** the battler's own timer (`$3dc0,y`, a word, `lsr`) `≥` the operand | — |
| `$0c` | `ai::VariableBelow` | `:4747-4749` | `$0d` does not hold | — |
| `$0d` | `ai::VariableAtLeast` | `:4735-4741` | the variable `≥` the operand — **at least** | — |
| `$0e` | `ai::LevelBelow` | `:4790-4792` | `$0f` does not hold — **true also when the target test fails** | as `$0f` left them |
| `$0f` | `ai::LevelAtLeast` | `:4778-4784` | the target test, and Y's level `≥` the operand | the target test's |
| `$10` | `ai::OneKindOfMonster` | `:4798-4801` | the names listed (`$3eca`) number 1 or 0 | — |
| `$11` | `ai::MonstersAlive` | `:4818-4821`, `_1cef :4832-4837` | every monster of the mask is alive (`$3a75`); a mask of 0 is the battler's own monster bit (`GetAITargetMask :5038-5043`, `$3019,y`), which for a party member is 0 — every one of no monsters is alive | — |
| `$12` | `ai::MonstersDead` | `:4827-4837` | every monster of the mask is in `$3a73` (its slot holds a monster) exclusive-or `$3a75` (alive); a mask of 0 as `$11`. **Whatever the answer, the restriction is lifted first** (`stz $f8`, `:4828`) | — |
| `$13` | `ai::PartyAtLeast` / `ai::MonstersAtMost` | `:4843-4851` | first operand 0: the party members alive (`$3a76`) `≥` the second; otherwise the second `≥` the enemies alive (`$3a77`) | — |
| `$14` | `ai::SwitchSet` | `:4755-4764` | the variable's bit is set | — |
| `$15` | `ai::SwitchClear` | `:4770-4772` | `$14` does not hold | — |
| `$16` | `ai::HalfBattleTimerAtLeast` | `:4718-4721` | **half** the battle's timer (`$3a44`, a word) `≥` the operand | — |
| `$17` | `ai::TargetValid` | `:4920-4939` | the target test | its own |
| `$18` | `ai::GauNotRecruited` | `:4857-4862` | `$1edf` bit 3 is **clear**: Gau has not been recruited | — |
| `$19` | `ai::InSlot` | `:4807-4812` | the battler's monster bit (`$3019,y`) is in the mask; never for a party member | — |
| `$1a` | `ai::WeakTo` | `:4637-4644` | the target test, and Y's weaknesses (`$3be0`) share an element with the operand | the target test's |
| `$1b` | `ai::FormationIs` | `:4868-4874` | the battle's formation (`$11e0`, a word) is the operand | — |
| `$1c` | `ai::Always` | `:4880-4883` | always; **lifts the restriction** | — |

A condition that does not hold leaves `$fc` as its routine left it; the script reader then puts back
the targets the run started with (`conditionmiss :4314-4318`).

**A variable** (`GetBattleVar`): an index below 36 is the byte `$3eb0 + index`, 36 or above the battler's
own (`$3dac,y`). Indices 0–3 are the battle's four variables, which a battle clears as it starts
(`InitBattle :6056-6067`) and nothing but the scripts touches (`BattleState::battleVariables`); indices
4–23 are the bytes of the game's battle switches (`$3eb4`), so index *v*, bit *b* is battle switch
`(v − 4) × 8 + b` (`game::BattleSwitches`, `AiSwitch::battleSwitch()`). The scripts hold the indices 0–3,
9, 10, 12, 13 and 36, and no other; no typed step names another.

**The names listed** (`$3eca`): `UpdateMonsterGfxBuf` counts, for each monster slot in order that is
present, not fallen or fled (`$3a3a`), counted in the battle (`$3409`), not Dead, Petrify or Zombie and
whose name is not hidden (`$3c9d` bit 2), a name group (`$3388`) not yet among the at most four listed
(`:15192-15235`). `refreshRunDifficulty()` writes the same count beside the run difficulty, from the same
loop.

## G. A counterattack's guard — `ExecAIRetal`

1. The battler's bit is taken out of `$33fc`; **it was already out → nothing runs** (`:12472-12474`):
   one run a battle step. Port: `BattleState::counterAiRanThisStep` holds the battlers whose bit is
   out.
2. The battler's bit is taken out of `$3a56` (`diedAwaitingCounter`):
   - **it was in** (`@4c28 :12490-12492`): its bit in `$33fe` is set. It was clear — another battler
     attacked it this step (`CheckRetal :12544-12545` clears it; `attackedThisStep`) → step 3; it was
     set → **restricted**. Port: `attackedThisStep.has()` is "the bit was clear", and setting it is
     `remove()`.
   - **it was not** (`:12477-12480`): Quick is running for another battler (`$3404` a battler and not
     this one — the word at `$3403` read for its sign) → **restricted**; else step 3.
3. The statuses it answers with (`$3e60`/`$3e74`, `Battler::statusForCounter`) hold Sleep, Confuse or
   Berserk (`bit #$b000`) or Stop or Frozen (`bit #$0210`) → **restricted**; it is charmed (`$3395,x`
   a battler — the word at `$3394` read for its sign) → **restricted**; else not.
4. Restricted is `$3a98` = `$ffff`. The counterattack part runs (C) either way, with `$b8` — the
   battler the counter action answers, or none for a battler answering its own death — as its targets.

**What restricted does** (C step 4 with F): only a block that reaches `ifMonstersDead` or `always`
before its first step that acts runs; every other block is skipped, one after another. A skipped
block restricts the run again. It is how a monster asleep, confused, stopped or charmed still makes
the attack its script gives it as it dies.

A battler whose turn part does not run — charmed, berserk, confused, in the colosseum (`:214-222`) —
still runs its counterattack part, restricted or not as above. A scripted party member's turn part
runs whatever its status (A).

## H. The three commands — the battler that queued them in Y

**H.1 — `Cmd_2e :5049-5065`, a variable set, raised or lowered.** `$b6` the variable, `$b8` the amount
with the change in its top two bits. Top bit clear → **set**: bit 6 is dropped and the variable
becomes the low six bits. `$80` → **add**; `$c0` → **subtract** (`eor #$bf; inc` makes the six-bit
amount's negative, `:5061-5062`). The sum is a byte: **it wraps** (`adc $ee`, `:5063`, the carry clear
from `GetBattleVar`'s `clc` and the set path's `lsr` of `$80`). Stored by `_1e38 :5088-5093`. The
scripts' amounts are 0, 1 and 4. Port: `changeBattleVariable()`, the sum in the variable's
`std::uint8_t`.

**H.2 — `Cmd_2f :5071-5085`, a switch toggled, set or cleared.** `$b9` the bit (a 1 rotated in through
the carry: bit *n* for *n* up to 7), `$b8` the variable, `$b6` the change: 0 toggle (`eor`), 1 set
(`ora`), 2 clear (`and` with the complement). Stored the same way. Port: `changeBattleSwitch()`; a
variable index from 4 is a game battle switch (`AiSwitch::battleSwitch()`, `game::BattleSwitches`).

**H.3 — `Cmd_30 :5132-5137` and `MiscAIEffectTbl :5295-5309`.** `$b6` the effect, the accumulator `$b8`
its operand. A target is found by `CheckAITarget` with the carry set (no cut).

| # | `AiEffect` | Line | What it does |
|---|---|---|---|
| 0 | `ResetOwnTimer` | `:5142-5146` | the battler's timer ← 0 |
| 1 | `MakeInvincible` | `:5166-5174` | the target joins `$3a3c` (`BattleState::invincible`) |
| 2 | `EndBattle` | `:5158-5161` | `$3a6e` ← 8 (`EndOfBattleEvent::EndNow`) |
| 3 | `RecruitGau` | `:5213-5227` | `$1edf` bit 3 set (`gauRecruited`); the battler's character record (`$3ed9,y`) joins the party played (`$1a6d`) at the battler's slot, its row kept, its enabled bit set and its shown bit kept (`$1850`: `ora #$40; and #$e0`, then the party and the slot) |
| 4 | `ResetBattleTimer` | `:5232-5235` | `$3a44` ← 0 |
| 5 | `EndInvincible` | `:5179-5187` | the target leaves `$3a3c` |
| 6 | `MakeTargetable` | `:5193-5198` | the target's monsters (`$b9`) join `$2f46` — they leave `aiUntargetable` |
| 7 | `MakeUntargetable` | `:5203-5208` | the target's monsters leave `$2f46` — they join `aiUntargetable` |
| 8 | `FillGauge` | `:5240-5246` | if the target names anyone, the high byte of **the battler's own** ATB rate (`$3ac9,y`) ← `$ff`: its gauge fills at the next time update; the target only decides whether |
| 9 | `EndBattleLearningRages` | `:5151-5153` | `$3a6e` ← 10 (`EndOfBattleEvent::RagesLearned`) |
| 10 | `Nothing` | `:5289-5290` | nothing; no script holds it |
| 11 | `SetStatus` | `:5260-5262`, `_c21eeb :5268-5279` | an attack by the battler on itself: the plain attack set up again (`_c2298a :6892-6909` — the battle's own attack, undodgeable, no effect, level 0), the status's bit put in the attack's statuses (`GetBitPtr`), nothing shown (`$b5` ← `$12`), `ExecSelfAttack` |
| 12 | `ClearStatus` | `:5251-5255` | the same with the attack's remove-status flag (`$11a4` bit 2) |
| 13 | `HideSelf` | `:5284-5287` | Hide set on the battler's status **directly** (`$3ef9,y`), outside the statuses taking effect |

Port: `runAiEffect()` behind `kAiEffectRules`, one row per effect in this order. `SetStatus` and
`ClearStatus` are the attack `statusTimersExpire()` makes (`Cmd_29 :13335-13367`) less the casting
swirl's reset: `Cmd_29` turns the swirl off (`trb $b0`), `Cmd_30` does not. They are checked against the
listing, not against the cartridge: the oracle does not set up the cells the cartridge's whole attack
reads. `RecruitGau` reads the battler's record index for any battler; the scripts that hold it are
party members' (`$3ed9,y` of a monster is not a record), and the port changes a record only for a
battler that has one. The port keeps no enabled or shown bit; the oracle writes both and checks that
the enabled bit is set and the shown bit kept.

## I. Widths and carries

- `AICond_06`: `lda $3a2f; xba; longa; lsr` — the operand becomes the high byte, then is halved: × 128.
  The compare is a word against the HP word.
- `AICond_0b`/`_16`: the timers are words; the compare is against the word `$3a2e`, the operand with
  `$3a2f` above it, which every script holds as 0 (`ai_script.inc`).
- `AICond_08`: the status number is tested against 16 **before** bit 4 is dropped (`cmp #$10`, then
  `lda #$10; trb $3a2f`); the mask (`AIGetBit :4908-4914`) is built with A a word wide, so bits 8–15
  reach a status word's second byte; the loop reads each battler's status word.
- `AICond_17`: `pla; cmp #$36; stz $fc; stz $fc` with A a byte wide — the targets' party byte only.
- `AICond_03`/`_04`: `tya; adc #$13` / `adc #$15` with the carry clear from the reader's `asl`; `_03`
  falls through `_02`'s `iny` — offset 20; `_04` is 21.
- `AITarget_45`: `cmp #$0a` on the whole byte — the waiting bit makes it "not below 10".
- `_c25917`: `lda $341a` with A a word wide tests `$341b`'s top bit.
- `GetAITargetMask` and `AICond_19` read `$3019,y`: a monster's bit; 0 for a party slot.
- The reader tests `$f8` one byte wide (`NextAICmd :4370`); `stz $f8` lifts the restriction.
- `ExecMonsterAction` loads the saved count with A a word wide (`lda $3240,x`): `$f4` ← the turn's
  count and `$f5` ← the counterattack's; `ExecAI` then clears `$f5`.
- `ExecAIRetal`: `lda $3403; bmi` with A a word wide tests `$3404`; `lda $3394,x; bpl` tests `$3395,x`.
- `Cmd_2e`: the `lsr` between the two `trb`s turns `$80` into `$40` for the second; its carry out is 0,
  so the add that follows has no carry in on either path.
- `Cmd_2f`'s `_c21e57` rotates a 1 in through the carry: bit *n* for *n* up to 7 with A a byte wide.
- `AICmd_f5`'s own-bit rule reads `$3019,y`: a monster's bit; 0 for a party slot.
- `_1b28`'s Confuse test is on the status now (`$3ee4`); G's are on the statuses it answers with.
- The count `$f5` is a byte and is compared as one.

## J. What the scripts hold

Every step and every branch of every routine above is the port's. What the scripts do and do not
hold is recorded here as fact; it changes no rule:

- No script reaches the conditions `$00`/`$0a`, `$0e`, `$18`, `$1c`, `$11` with a mask of 0; the effects
  5, 8, 10 and 12; the monster animations 5 and 8; the show-or-hide mode 5; a toggle of a monster's own
  switch. Each is ported; the conditions, the toggle and the effects 5, 8 and 10 are checked on the
  cartridge.
- The typed steps have no form for an operand no script holds (`data/ai_scripts.h`): a byte-wise
  variable from 4 to 35, a bit above 7, an amount above 63, a switch change above 2, an effect above
  13, a condition above `$1c`, the value `$fe` as an attack, command or item, a dialog above 255.
- A party member a scripted battle gives a script reads scripts 256 to 383 (`kCharacterAiScripts`).

## K. State

| Cell | Port |
|---|---|
| `$3254` / `$3268` the parts | `Battler::turnScript`, `counterScript` (spans into `kAiScripts`); `hasCounterScript` |
| `$3240` / `$3d0c`, `$3241` / `$3d20` the counts and places to go on from | `Battler::turnResume`, `counterResume` (`AiResume`) |
| `$3eb0`–`$3eb3` | `BattleState::battleVariables` |
| `$3dac`, `$3dc0` | `Battler::aiVariable`, `turnTimer` |
| `$3a3c`, `$2f46` | `BattleState::invincible`, `aiUntargetable` |
| `$3a44`, `$3a6e` | `BattleState::battleCounter`, `endOfBattleEvent` |
| `$33fc`, `$3a56`, `$33fe` | `BattleState::counterAiRanThisStep`, `diedAwaitingCounter`, `attackedThisStep` |
| `$3e60`/`$3e74` | `Battler::statusForCounter` |
| `$3ac8` | `Battler::atbRate` |
| `$1edf` bit 3, `$1850` | `BattleState::gauRecruited`, `game::Party::membership` |

## L. What the original never does

- A script shows nothing itself: every picture is the queued actions' (`battle/battle_script.h`).
- A script draws random numbers only through D.2–D.4's draw, E's picks and coin, and F's status pick;
  a command's aim draws as the aim's own rules say.
- `AICmdSizeTbl` and the step bytes are the cartridge's way of stepping through a script; the port reads
  typed steps.
- A charmed, berserk or confused monster, and one in the colosseum, chooses its own attack on its turn
  (`chooseMonsterAction()`).

## M. Behavior kept as the cartridge has it

- **The fourth Mag Roader answers attacks with Wild Cat's turn part** (B.3).
- **A condition on the monster itself leaves the monster as its target** (F, the target test).
- **A formation change keeps the slot's script memory** (B.4).
- "Less than" in the source's macro names is at most, "greater" at least, the timers are compared at
  half their reading, and the source's `if_gau_present` holds when Gau is not recruited (F). The
  port's names say what happens.
- `ifStatus` and `ifNoStatus` draw a random number whenever a target has the status, holding or not.
- `ifLevelBelow` holds when its target names no one.
- Reading the Targetting mark uses it up, in a condition as in a target step.
- `FillGauge` fills the gauge of the monster that asks, not the target's (H.3).
- A lone attack numbered `$f0` or above still draws (D.2).
- A set amount is six bits (H.1).
- A monster's script reads the reach flag the last command left (E).

## Rules read through other surfaces

- `RandBit`'s pick, `RandCarry`'s coin, `CheckTargetsPresent` and the reach check are the port's shipped
  `pickFrom()`, `coin()`, `removeAbsentTargets()` and `reachableTargets()`.
- `GetCmdForAI`, `CalcCmdDelay`, `ChooseTarget` and `CreateDefaultAction` are `commandForAttack()`,
  `addAdvanceWait()`, `CommandExecutor::aim()` and `addActionOrCounter()`; the five shipped actions a
  script queues are run by `CommandExecutor::execute()` ([`battle_commands.md`](battle_commands.md)).
