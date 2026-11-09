# Battle AI — behavioral contract

Behavioral specification for a monster's AI script as the battle reads it: who a script names as its
targets and what each of its conditions tests. All citations point into the pinned reference tree
`original-src/` (everything8215/ff6 @ `1ea47b5`); line numbers are into `src/battle/battle_main.asm`
unless another file is named. This document is the test authority for `tests/test_ai_targets.cpp`,
`tests/test_ai_conditions.cpp` and `tests/test_ai_oracle.cpp`.

The scripts themselves are data: `src/data/ai_scripts.h`, described in
[`docs/engine/data-layer/monster-ai-scripts.md`](../engine/data-layer/monster-ai-scripts.md). The rules
here are `src/battle/ai_targets.h` and `src/battle/ai_conditions.h`. Every claim about `CheckAITarget`
and the condition routines is also checked against the cartridge itself: each routine is run on the
cartridge, hosted on the engine's SNES machine, over the same battle the port is given
(`tools/damage-trace/damage_oracle.h`).

The cells the script reader keeps: `$f8` restricted (one byte, 0 when not), `$fc` the targets now,
`$fe` the targets the run started with, `$3a2c`–`$3a2f` the four bytes of the step it is at (a
condition's are `$fc`, its number, its two operands). A battler is named in the cartridge's cells by
twice its slot index; a set of battlers is a word with the party's bits in its low byte and the
monsters' in its high byte (`$3018`, a battler's bit).

## Sources

| Behavior | Source | Port surface |
|---|---|---|
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

The port's seams: `aiTargets(state, actor, target, reachableOnly)` and `aiCondition(state, actor,
condition, run)`, with `AiRun` holding `$fc`, `$fe` and `$f8`.

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

## Rules read through other surfaces

- `RandBit`'s pick, `RandCarry`'s coin, `CheckTargetsPresent` and the reach check are the port's shipped
  `pickFrom()`, `coin()`, `removeAbsentTargets()` and `reachableTargets()`.
- `AICmdSizeTbl` and the step bytes are the cartridge's way of stepping through a script; the port reads
  typed steps.
