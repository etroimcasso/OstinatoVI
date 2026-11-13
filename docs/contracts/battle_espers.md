# Battle espers — behavioral contract

Behavioral specification for what an equipped esper does for a party member when a battle is won:
the spells it teaches are learned a little further, and each level-up the battle's experience brings
carries the esper's level-up bonus. All citations point into the pinned reference tree `original-src/`
(everything8215/ff6 @ `1ea47b5`); line numbers are into `src/battle/battle_main.asm` unless another file
is named. A character record's cells are named from `notes/field-ram.txt:885-927`: record *n* starts at
`$1600 + 37·n`, with the level at `+$08`, the maximum HP at `+$0B` (two bytes), the maximum MP at
`+$0F`, the experience at `+$11` (three bytes), vigor, speed, stamina and magic power at `+$1A` to `+$1D`
and the esper at `+$1E`. This document is the test authority for `tests/test_spell_learning.cpp`,
`tests/test_leveling.cpp` and `tests/test_espers_oracle.cpp`.

Both rules are pure functions over the party's records (`game::CharacterRecord`, `game::SpellProgress`
in `src/game/character_record.h`): `src/battle/spell_learning.h` and `src/battle/leveling.h`. Every claim
about `LearnGenjuMagic` and `DoLevelUp` is also checked against the cartridge itself: the routine is run
on the cartridge, hosted on the engine's SNES machine, over the same records the port is given
(`tools/damage-trace/damage_oracle.h`). `IncLearnMagic` and the seventeen bonus handlers are checked
through the two routines that call them.

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| The esper's spells | `LearnGenjuMagic` `:15661-15678` | `learnFromEsper()` |
| A spell learned a little further | `IncLearnMagic` `:15687-15705` | `addSpellProgress()` |
| The level-up | `DoLevelUp` `:15761-15816` | `levelUp()` |
| The esper's bonus | `GenjuBonusTbl` `:15842-15859`, `GenjuBonus_00`-`_10` `:15865-15949` | `applyEsperBonus()` and its table in `leveling.cpp` |
| The esper's record | `GetGenjuPropPtr` `:16116-16122`, `GenjuProp` (`src/menu/genju_prop.asm`) | `getEsperProperties()` (`src/data/esper_properties.h`) |
| The gains | `LevelUpHP`, `LevelUpMP` (`src/field/event.asm`) | `levelUpHp()`, `levelUpMp()` (`src/data/level_up.h`) |
| The product | `MultAB` `:11801-11809` | `int` arithmetic |

## Where the battle calls them

The win (`WinBattle`, normal victory `:15313-15436`) calls these; the win itself is ported with the rest
of the battle's end, and this contract states what it hands over.

- **The spells.** For each party member alive at the win (`$3a74`, `:15366-15368`) and before Gogo in
  the actor order (`cmp #CHAR::GOGO; bcs`, `:15395-15397` — Gogo and Umaro learn nothing): the spell
  list is the character's (`GetSpellListPtr :16099-16108`: `$1a6e + 54·actor`, into `$f4`); the items
  equipped teach first (`LearnItemMagic :15623-15655`, each through `IncLearnMagic` at `:15648`); then a
  member with an esper (`$161e` not negative, `:15402-15403`) calls `LearnGenjuMagic` with the esper's
  place among the 27 in A (`:15404`). The battle's magic points are `$fb`: `BattleMagicPoints` at the
  battle's index, or 0 for an index of 512 or more (`:15313-15319`); `magicPointsForBattle()` holds the
  table. `$f1` is whether the party has magicite (`$3ebc & $08`, `:15321-15323`: battle switch 67,
  `game::kMagicPointsEarnedSwitch`, which the event script sets with the party's first magicite) and
  `$f0` the win's flags, cleared (`:15320`). The Morph command tests a different bit, `$3ebc & $04`,
  switch 66 (`InitCmd_00 :13814-13816`).
- **The level-ups.** For each party member alive, `CheckLevelUp :15711-15755` returns at level 99; it
  sums the experience steps below the level, times 8, and when the record's experience reaches that it
  calls `DoLevelUp` (`:15748`) with X the record, then `LearnAbilities`, and goes round again.

Port seams: `learnFromEsper(progress, esper, magicPoints)`, `addSpellProgress(progress, spell, rate,
magicPoints)` for the items, and `levelUp(record, version)` once per level the experience reaches. The
caller keeps the rules above, the magic-points guard, the message "got *n* magic point(s)" and the
"learned" messages.

## A. The esper's spells

`LearnGenjuMagic`: X = the esper's place × 11, its 11-byte record (`GetGenjuPropPtr`; the record is five
`{rate, spell}` pairs and then the bonus byte). For each of the five pairs in order (`ldy #5`,
`:15664`, `:15674-15676`): a spell byte of `$ff` is an empty pair and is passed over (`:15667-15668`);
otherwise `IncLearnMagic` runs with A the rate and Y the spell (`:15669-15673`). X, the record, is kept
across the call (`phx`/`plx`) and is not read.

The port: `learnFromEsper()` walks `getEsperProperties(esper).spells` in order, passes over a slot whose
spell is `AttackId::NONE`, and runs `addSpellProgress()` on the rest. It returns whether any of them
returned true — the bit `IncLearnMagic` leaves in `$f0` (B step 5).

## B. A spell learned a little further

`IncLearnMagic`, with A the rate, Y the spell and `$f4` the character's spell list:

1. A rate of 0 returns at once (`beq`, `:15688`, on the flags of the load before the call).
2. The amount is rate × magic points (`xba; lda $fb; jsr MultAB`, `:15689-15691`), its **low byte** kept
   (`sta $ee`, A a byte wide, `:15692`).
3. The spell's learned percentage (`lda ($f4),y`, `:15693`); `$ff`, learned, returns (`:15694-15695`).
4. The percentage plus the amount (`clc; adc $ee`, `:15696-15697`). A carry out of the byte, or a sum of
   **100 or more** (`cmp #100; bcc`, `:15699-15700`), stores **`$80`**: learned, its message still to
   show (`:15701`). Otherwise the sum is stored (`:15702`).
5. `lda $f1; tsb $f0` (`:15703-15704`): the magicite bit is OR-ed into the win's flags, as magic points
   earned, whenever step 4 stores — with 0 magic points too, when the amount is 0 and the percentage is
   stored unchanged. Without magicite the store sets nothing.

The port: `addSpellProgress()` returns false for a rate of 0 and for a spell at
`SpellProgress::kSpellLearned` (255); otherwise it adds the product's low byte, stores the sum, or
`SpellProgress::kSpellJustLearned` (128) for a sum of 100 or more, and returns true. A spell already at 128 — learned from an item and again
from the esper in the same win — sums past 100 and stays 128.

`SpellProgress::knows()` stays true only at 255, as the original's `cmp #$ff` tests. `ShowLearnedMagicMsg
:15822-15837` turns each 128 into 255 with the message "*name* learned *spell*" (message `$32`);
`LearnAbilities`'s natural magic writes 128 too (`:16008-16009`).

The spell is one of the 54 learnable spells, Fire (`$00`) to Life 3 (`$35`); the esper and item tables
hold no other.

## C. The level-up

`DoLevelUp`, with X the record (`php`/`plp` around):

1. The level goes up by one (`inc $1608,x`, `:15763`) **before** the gains are read.
2. The gains at the **new** level: `$fc` = `LevelUpHP[level − 2]` and `$fe` = `LevelUpMP[level − 2]`
   (`:15768-15773`), each a byte with its high byte (`$fd`, `$ff`) cleared first (`:15764-15765`). The MP
   curve is the release's: the Japanese cartridge's differs. The port reads `levelUpHp(level)` and
   `levelUpMp(version, level)`, "gained on reaching `level`".
3. The esper's bonus (`:15775-15788`): no esper (`$161e` negative) skips it; otherwise the bonus byte at
   `GenjuProp + 10`; a negative bonus (`$ff`, `EsperBonus::NONE`) skips it; otherwise `GenjuBonusTbl` at
   the bonus × 2, with Y the record (`txy`, `:15779`). § D.
4. The maximum HP (`:15789-15801`): the word at `$160b` keeps its top two bits (`$c000`, the HP boost)
   aside; the low 14 bits plus the 16-bit gain, and **10,000 or more becomes 9,999**; the boost bits go
   back on.
5. The maximum MP (`:15802-15814`): the same on `$160f` with the MP gain; **1,000 or more becomes 999**.

`CheckLevelUp` never calls it at level 99 (`:15714-15716`); `levelUp()` asserts the level is below
`kMaxLevel`.

The two boost bits are the record's HP and MP boost from equipment (`notes/field-ram.txt:891-897`). The
port works the boosts out from the equipment as a battle starts (`docs/contracts/battle_core.md`) and
keeps no field for them: `CharacterRecord::maxHp` and `maxMp` are the character's own maximums, and
`levelUp()` adds to them and caps them. The oracle writes boost bits into the record's words and checks
that the cartridge leaves them as they were.

## D. The esper's bonus

`GenjuBonusTbl` holds seventeen rows, one per bonus `$00`-`$10`; each handler is entered with X = the
bonus × 2, Y = the record, `$fc/$fd` the HP gain and `$fe/$ff` the MP gain.

| Bonus (`EsperBonus`) | Handler | Rule |
|---|---|---|
| `$00` HP_10, `$03` MP_10 | `GenjuBonus_00/_03 :15865-15868` | 26/256 (10.16 %) |
| `$01` HP_30, `$04` MP_30 | `_01/_04 :15874-15877` | 78/256 (30.47 %) |
| `$02` HP_50, `$05` MP_50 | `_02/_05 :15883-15885` | 128/256 (50 %) |
| the shared tail | `_617a :15886-15900` | X below 6 is the HP gain, otherwise the MP gain (`cpx #$0006`); the extra is (gain × fraction) >> 8 (`xba; lda $fc,x; jsr MultAB; xba`), **at least 1** (`bne; inc`, `:15894-15895`), added to the 16-bit gain |
| `$06` HP_100 | `_06 :15946-15949` | the HP gain's low byte added to the HP gain: doubled, with **no least amount** — it joins the tail after the `inc` |
| `$07` LV_30, `$08` LV_50 | `_07/_08 :15907-15909` | `rts`: nothing |
| `$09` STRENGTH_1, `$0a` STRENGTH_2 | `_09/_0a :15927-15940` | vigor, `$161a` |
| `$0b` SPEED_1, `$0c` SPEED_2 | `_0b/_0c :15923-15924`, into the stat tail | speed, `$161b` |
| `$0d` STAMINA_1, `$0e` STAMINA_2 | `_0d/_0e :15919-15920` | stamina, `$161c` |
| `$0f` MAGPWR_1, `$10` MAGPWR_2 | `_0f/_10 :15915-15916` | magic power, `$161d` |
| the stat tail | `:15929-15940` | `txa; lsr2` leaves bit 0 of the bonus in the carry: **odd (the `_1`s) adds 1, even (the `_2`s) adds 2** (`inc; bcs; inc`); **129 or more becomes 128** (`cmp #$81; bcc; lda #$80`) |

The stat handlers fall through one into the next, each `iny` moving Y to the next stat.

The port: `applyEsperBonus()` dispatches through a 17-row table indexed by the bonus, one rule per row,
each named for what it does. The fractions are the named constants `kTenPercentOf256`,
`kThirtyPercentOf256` and `kFiftyPercentOf256`; the stat limit is `kStatBonusMax` (128).

The esper table carries HP_10 twice, HP_30, HP_50, MP_10, MP_30, MP_50, STRENGTH_1, STRENGTH_2 twice,
SPEED_1, STAMINA_1, STAMINA_2 twice, MAGPWR_1 twice, MAGPWR_2 twice and NONE nine times. **HP_100, LV_30
and LV_50 have no esper carrying them**; the port has their rows from the listing all the same, and their
tests are worked out by hand only — the oracle reads the bonus from the cartridge's own table.

## E. Widths

- `IncLearnMagic` keeps the low byte of rate × magic points, and so does the port: a rate of 30 with 10
  magic points adds 44, and 16 × 16 adds nothing. In play the product never passes a byte — the
  largest learn rate in the esper table is 25 (Starlet's Cure) and the largest award in
  `BattleMagicPoints` is 10, at most 250 — but the rule holds for any rate and magic points the
  routine is given, and the oracle's draws pass a byte to show it.
- The sum in B step 4 is a byte with its carry tested: a sum past 255 counts as 100 or more, as the
  port's `int` sum compared with 100 does.
- The gains are 16-bit: +100 % on the largest HP gain (162 on reaching level 70) is 324, added whole.
- The percentage handlers multiply the gain's low byte, and a gain is one byte when they run: one bonus
  runs per level-up.
- **The stat tail adds within the stat's byte, and so does the port.** `lda $161a,x; inc; (inc)` comes
  round past 255 before the `cmp #$81` limit: a stat of 255 + 1 or 254 + 2 becomes 0 and 255 + 2
  becomes 1, each stored because it is below 129; 254 + 1 and 253 + 2 are 255, held to 128. In play no
  stat gets there — a record's four stats (`$161a`-`$161d`) are written only by this tail, which stores
  at most 128, and by event command `$40` (`src/field/event.asm` `EventCmd_40 :1047-1054`), which copies
  them from one of the 64 `char_prop` records, the largest of whose stats is 80 — but the rule holds for
  any stat the routine is given. `tests/test_leveling.cpp` pins both the wrap and the `char_prop`
  stats.
- The caps compare the 14-bit maximum plus the gain, before the boost bits go back on.

## F. Shipped and verified, not ported here

- **`Cmd_19 :3688-3701`**, Summon: `runCommand()`'s SUMMON case, `castSpell(…, AttackNameKind::Esper)`
  (`src/battle/commands.cpp`). The name kind 2 (`:3689`), the targeting with `$b6` (`:3693-3695`),
  `InitAttacker` (`:3696`), the MP cost zeroed for the **Slot** command's esper only (`$b5 == $0f`,
  `:3697-3700`, reached from `slotCommand()`) and `jmp ExecAttack` (`:3701`) each match.
- **`RandGenju :8968-8977`**: `chooseRandomEsper()` (`src/battle/automatic_actions.cpp`). `RandA(25)`;
  an esper from the eleventh on moves two further (`cmp #$0b; bcc; inc2`: Odin and Raiden are never
  drawn); `+ $36`. Its callers — the Magicite item's effect and the Slot reel's esper — use it.

## G. Not here

- A level the story gives a character (`src/field/event.asm` `UpdateExp :1291-1322`, `InitMaxHP
  :1349-1377`, `InitMaxMP :1405-1433`; `src/field/menu.asm` `UpdateMaxHP :103-130`, `UpdateMaxMP
  :139-166`) sums the curves with **no esper bonus** and no `DoLevelUp`: a character set to a level by an
  event has exactly the curves' HP and MP.
- The menu reads `GenjuProp` only to show it (`src/menu/skills.asm` `LoadGenjuProp :2769-2798`); the
  esper equipped is written by the skills menu.
- `give_genju` and `take_genju` are event commands.
- The battle's graphics name the espers and run the summon animations; neither changes a record.
- A party member not alive at the win learns nothing and gains no experience (`:15366-15368`); the dead
  and the petrified still count toward the experience divided (`:15326-15328`).
- Nothing in either routine draws a random number.

## H. Observable rules kept

- **The percentages are 256ths.** "+10%" adds 26/256 of the gain, "+30%" 78/256 and "+50%" exactly
  half; a +30 % on an HP gain of 162 adds 49 where 30 % would be 48.6. The bonus's name is "+30%"; the
  arithmetic is the bonus.
- **At least 1.** A percentage bonus never adds 0; +100 % has no least amount and needs none (every HP
  gain is 11 or more).
- **A stat bonus stops at 128**: 127 + 2 is 128, 128 + 1 is 128.
- **A stat bonus adds within the stat's byte**: 255 + 1 is 0, 254 + 2 is 0, 255 + 2 is 1.
- **The amount learned is the product's low byte**: a rate of 30 with 10 magic points adds 44.
- **Progress with 0 magic points.** `IncLearnMagic` stores an unchanged percentage and sets the flag
  when the battle gives no magic points; the message it feeds is passed over when `$fb` is 0
  (`:15411-15412`), so nothing shows.
- **A just-learned spell learned again** in the same win stays just learned.
