# Battle rewards — behavioral contract

Behavioral specification for what a battle's end gives the party once the battle has an outcome:
the experience and gold a win gives, each living party member's share, the levels it reaches and what
they teach, the spells its equipment teaches, the lores Strago saw, Mog's dance, Terra's Morph supply
and the rages. All citations point into the pinned reference tree `original-src/` (everything8215/ff6 @
`1ea47b5`); line numbers are into `src/battle/battle_main.asm` unless another file is named. A
character record's cells are named from `notes/field-ram.txt:885-927`: record *n* starts at `$1600 +
37·n`, with the actor at `+$00`, the level at `+$08`, the experience at `+$11` (three bytes), the esper
at `+$1E` and the six pieces of equipment at `+$1F` to `+$24`. `$f0`-`$ff` are the win's direct-page
cells and `$2f35`-`$2f40` the four message variables, three bytes each. This document is the test
authority for `tests/test_rewards.cpp` and `tests/test_rewards_oracle.cpp`.

The rules are functions over the party (`game::Party`, `src/game/character_record.h`) and the battle
(`battle::BattleState`) in `src/battle/rewards.h`. Every rule with a routine of its own is also checked
against the cartridge itself: the routine is run on the cartridge, hosted on the engine's SNES machine,
over the same party the port is given (`tools/damage-trace/damage_oracle.h`).

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| The experience and gold the fallen give | `WinBattle :15326-15345` | `spoilsOf()` |
| Each ally's share | `:15346-15359`, `Div :11818-11833` | `experienceShare()` |
| Experience added | `AddExp :16043-16073`, `MaxExp :16078` | `addExperience()` |
| The next level reached | `CheckLevelUp :15711-15743` | `earnedNextLevel()` |
| What a level teaches | `LearnAbilities :15958-16016`, `GetAbilityBit :16025-16035` | `learnAbilitiesAtLevel()` |
| The spells equipment teaches | `LearnItemMagic :15623-15655` | `learnFromEquipment()` |
| The lores | `:15437-15458` | `learnLores()` |
| The dance | `:15459-15468` | `learnDance()` |
| The Morph supply | `:15377-15388` | `gainMorphSupply()` |
| The rages | `LearnRage :12185-12198` | `learnRages()` |
| A spell learned a little further | `IncLearnMagic :15687-15705` | `addSpellProgress()` (`docs/contracts/battle_espers.md` § B) |
| A level-up | `DoLevelUp :15761-15816` | `levelUp()` (`docs/contracts/battle_espers.md` § C) |
| The switch the win reads | `:15321-15323` | `game::kMagicPointsEarnedSwitch` (67) |
| The Cursed Shield's count | `$3ec0`, battle variable 12 | `game::BattleSwitches::cursedShieldBattles()` |
| The tables | `BattleMagicPoints`, `LevelUpExp`, `NaturalMagic`, `BushidoLevelTbl`, `BattleBGDance`, `ItemProp` | `magicPointsForBattle()`, `levelUpExp()`, `kNaturalMagicTerra`/`kNaturalMagicCeles`, `abilityLearnLevels()`, `backgroundDance()`, `getItemProperties()` |

## A. What each ending runs

The battle hands its outcome to `ActionExecutor::playBattleEnd()` and then writes the party back
(`BattleSim::finish()`, `src/battle/battle_sim.cpp`). In the cartridge each ending runs its body, then
`UpdateSRAM` (`:11961`), then `TerminateBattle :150` — except the final battle's next stage
(`:12204-12239`).

| Outcome | Cartridge path | Runs, in order |
|---|---|---|
| `BattleWon` | `@488c :11960` → `WinBattle :15282` | the win (§ F–§ N), which runs the wrap-up `_c24903 :12062-12086` inside it |
| `BattleLost` | `@482e :11921`, `BattleEnd_03 :12042-12045` → `LoseBattle :15588-15593` | the defeat switch, the wrap-up, then the message (`$ff`: none) |
| `BattleEnded{RanOutOfTime}` | `BattleEnd_01 :11972-11976` | the wager kept, the switch, the wrap-up |
| `BattleEnded{EndNow}`, `{TimerExpired}`, `{EatenByZoneEater}` | `BattleEnd_04 :11983-11985` | the wrap-up |
| `BattleEnded{GauLeapt}` | `BattleEnd_02 :12023-12027` | Gau out of the party (§ Q), then the rages — **no wrap-up** |
| `BattleEnded{RagesLearned}` | `BattleEnd_05 :12034-12036` | the rages — **no wrap-up** |
| `BattleEnded{FinalBattleNextStage}` | `BattleEnd_06 :12204-12206` | the step back (`EndAction`), the wrap-up |

A lost battle gives nothing beyond what the wrap-up puts away. A win never runs `LearnRage`: the rages
are learned only by the two endings that name them.

## F. Magic points, experience and gold

- **Magic points** (`$fb`, `:15313-15319`): `BattleMagicPoints` at the battle's index (`$3ed4`, set
  once from the formation the battle starts with, `:7966-7970`), or 0 for an index of 512 or more.
- **The party has magicite** (`$f1`, `:15321-15323`): `$3ebc & $08`, battle switch 67
  (`game::kMagicPointsEarnedSwitch`). The event script sets it as the party is given its first
  magicite (`src/event/event_main.asm:25838`, `:26464`, `:26476`, `:26488`). The Morph command reads a
  different switch, 66 (`$3ebc & $04`, `InitCmd_00 :13814-13816`). `$f0`, the win's flags, starts at 0
  (`:15320`): bit 0 the curse dispelled, bit 3 magic points earned, bit 4 a Cat Hood counted, bit 6 a
  technique told of, bit 7 a blitz told of.
- **The fallen** (`:15325-15345`): for monster slots 5 → 0, a monster whose first status byte has
  Dead, Petrify or Zombie (`$3eec,x & $c2`, `:15326-15328`) adds its experience (`$3d8c,x`) to a total
  in the first message variable — **unless the battle is on the Veldt** (`$11e4 & $02`,
  `:15329-15331`) — and its gold (`$3da0,x`) to a total in the fourth, either way. A monster that fled
  or was sent away has none of the three statuses and gives neither.
- **Each ally's share** (`:15346-15359`): the total divided by the allies alive (`$3a76`), the
  remainder dropped. The three-byte total is divided in two steps through `Div`, the console's 16-by-8
  divider: the top two bytes (`$2f36-$2f37`) first, then the first step's remainder above the low byte.
  The share replaces the total in the first variable.
- A share that is not 0 is told of with message `$27` (39), "Got *n* Exp. point(s)" (`:15360-15363`).

## G. Each living party member, last slot first

For party slots 3 → 0, a member alive (`$3018,y & $3a74`, `:15366-15368`):

1. **A Cat Hood** (`$3c59,y & $10`, `:15369-15376`): the first member wearing one doubles the gold
   total; a second does nothing (`tsb $f0; bne`).
2. **Terra** (the slot's actor `$3ed8,y` is Terra) with the party holding magicite (`:15377-15388`):
   magic points count as earned, and the Morph supply (`$1cf6`) gains twice the magic points (§ S).
3. **Experience** (`:15389-15394`): `AddExp` (§ H) with the share; a member with an Exp. Egg
   (`$3c59,y & $08`) gets `AddExp` a second time.
4. **A member whose actor is before Gogo** (`cmp #CHAR::GOGO; bcs`, `:15395-15397`): the spells its
   equipment teaches (§ I), then, with an esper equipped, the esper's (`learnFromEsper()`). Gogo, Umaro
   and every guest learn neither.

After the loop (`:15408-15417`): with magic points earned and the award not 0, message `$35` (53), "Got
*n* Magic Point(s)", with the magic points.

## H. Experience added — `AddExp`

The share (three bytes) is added to the record's experience (`$1611-$1613`) in three bytes, the carry
out of the third byte lost (`:16045-16052`). A sum above 15,000,000 (`MaxExp`, compared from the top
byte down, `:16054-16066`) becomes 15,000,000 (`kMaxExperience`). The level is not looked at: a
character at 99 still gains experience.

The port: `addExperience(record, share)` keeps the sum's low 24 bits, then holds it to
`kMaxExperience`.

## I. The spells equipment teaches — `LearnItemMagic`

For the six equipment slots in record order — weapon, shield, helmet, armor, relic 1, relic 2
(`$161f,x` on, `:15626-15653`); an empty slot (`$ff`) is passed over:

1. **The Cursed Shield** (item `$66`, `:15630-15637`): the battles won holding it (`$3ec0`, battle
   variable 12) go up by one. When the count comes round to 0 — the 256th — the curse is dispelled
   (`$f0` bit 0) and the slot's item becomes the Paladin Shield (`$67`). The count goes up once per
   slot holding the shield, whichever slot that is.
2. The item in the slot — **the Paladin Shield, when the curse was just dispelled** (A holds `$67` at
   `@600c`) — teaches its spell (`ItemProp+4`) at its learn rate (`ItemProp+3`) through
   `IncLearnMagic` (`:15638-15648`); a rate of 0 does nothing.
3. A spell stored counts magic points as earned when the party holds magicite (`lda $f1; tsb $f0`,
   `:15703-15704`).

Every item with a learn rate teaches one of the 54 learnable spells.

The port: `learnFromEquipment(record, spells, switches, magicPoints)` returns whether a spell was
learned further and whether the curse was dispelled; the caller counts magic points as earned when the
party has magicite.

## J. The second pass, last slot first

For party slots 3 → 0, a member alive (`:15418-15436`): the second variable holds the slot's number
(`tya; lsr`, `:15425-15427`); "gained a level" is armed (`$f2 ← $2e`); `CheckLevelUp` (§ K); then,
for an actor before Gogo, `ShowLearnedMagicMsg :15822-15837` turns each spell at `$80` into `$ff`, last
spell first, with message `$32` (50), "*name* learned *spell*". **Every** living member is checked for
a level — Gogo, Umaro and guests included.

## K. The levels reached — `CheckLevelUp`

Round and round until one of the two returns:

1. A level of 99 or more returns (`:15714-15716`).
2. The experience the next level needs is the sum of the first *level* steps of `LevelUpExp` — the
   steps to levels 2 to *level* + 1 — a 16-bit sum whose carries count into a third byte, times 8
   (`:15717-15735`). Every total is below 2²⁴.
3. The record's experience below that returns (`:15736-15743`: the top two bytes compared, then the low
   byte).
4. The first time for this member, message `$2e` (46), "*name* gained a level", with the slot, and the
   message is disarmed (`:15744-15747`): one message per member however many levels it gains.
5. `DoLevelUp` (`levelUp()`), then `LearnAbilities` (§ L) with the record's actor and its new level
   (`:15748-15754`).

The port: `earnedNextLevel(record)` is steps 1-3. The win runs the loop.

## L. What a level teaches — `LearnAbilities`

By the record's actor:

- **Terra, Celes** (`:15959-15964`, `:15995-16015`): each of the actor's 16 natural-magic rows
  (`NaturalMagic`, Terra's then Celes's, `{spell, level}`) whose level **equals** the new level marks
  its spell `$80` (`SpellProgress::kSpellJustLearned`) unless the spell is learned (`$ff`). Nothing is
  told here; § J tells of it.
- **Cyan** (`:15967-15978`): the first of the eight SwdTech levels (`BushidoLevelTbl`) equal to the new
  level gives its bit; none returns. The bit joins the SwdTechs known (`$1cf7`); one already known
  returns. Otherwise, the first time in this win (`$f0` bit 6), message `$42` (66), "Mastered a new
  technique!" — a second technique learned in the same win is learned untold.
- **Sabin** (`:15981-15992`): the same over `BlitzLevelTbl` (the eight bytes after Cyan's), `$1d28`,
  `$f0` bit 7, message `$33` (51), "Devised a new Blitz!".
- Anyone else: nothing.

The port: `learnAbilitiesAtLevel(party, actor, level)` marks the natural spells and returns whether a
SwdTech or a Blitz was newly learned; the caller tells of the first of each in a win.

## N. Lores, the dance, the shield

- **Lores** (`:15438-15458`): for lores 23 → 0, one Strago saw this battle (`$3a84`, lore *n* is bit
  *n* mod 8 of byte *n* / 8) joins the lores known (`$1d29`) and message `$2d` (45), "Learned *lore*",
  tells of it with the attack (`ATTACK::CONDEMNED` + its number).
- **The dance** (`:15459-15468`): with Mog in one of the battle's party slots (`$300a` not negative —
  alive or not), the dance of the battle's background as it now stands (`$11e2`; a dance changes it)
  joins the dances known (`$1d4c`); one not known before is told of with message `$40` (64), "Mastered
  a new dance!". Every one of the 56 backgrounds has a dance, so the table's "none" (`bmi`) is never
  read.
- **The shield** (`:15469-15473`): the curse dispelled this win shows message `$2a` (42), "Dispelled
  curse on shield".

## Q. The rages — `LearnRage`, and Gau after a Leap

- `LearnRage`: for each of the six monster slots whose monster index is below 256 (`$2002,x` is 0), that
  monster's rage joins the rages known (`$1d2c`, rage *n* is bit *n* mod 8 of byte *n* / 8) — **whether
  the monster is alive, fallen or gone**. An empty slot's index is `$ffff` and is passed over.
- `BattleEnd_02`: Gau's record leaves the party (`RemoveChar :11879-11885`) and Gau is no longer among
  the characters the party can field (`trb $1edf`, bit 3); then `LearnRage`.

## S. Widths

- The totals' `inc $2f37` and `inc $2f40` (`:15337`, `:15343`) run 16 bits wide, but six monsters give
  at most 393,210 and the third byte never carries.
- The allies alive are loaded 16 bits wide (`ldx $3a76`, `:15349`) and `Div` divides by the low byte.
  Over 0 the divider answers 65,535 for both steps, so the share is 16,777,215; a win always has an ally
  alive.
- The Cat Hood's doubling is three bytes wide: the bit shifted out of the third is lost.
- **The Morph supply**: `lda $fb; asl; adc $1cf6` — the doubling's low byte, plus the carry the doubling
  leaves, plus the supply; a carry out of that makes it 255. An award of 128 adds 1; 200 adds 145.
- `AddExp` and the gil: three bytes, the last carry unread.
- `CheckLevelUp`'s compare is 16 bits wide on the top two bytes; the low bytes are compared only when
  those are equal.
- `LearnItemMagic` clears the accumulator before loading the spell, so the spell is a byte.

## T. Reachability

Every rule here is reachable: the port owns the party, the switches and the monsters' slots. The
arithmetic play cannot reach — an award of 128 magic points or more, an experience sum past three
bytes, a share for no allies, a Cursed Shield in more than one slot — is reproduced all the same and
checked by hand and against the cartridge.
