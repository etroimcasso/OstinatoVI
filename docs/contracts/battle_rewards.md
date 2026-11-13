# Battle rewards — behavioral contract

Behavioral specification for what a battle's end gives the party once the battle has an outcome:
the experience and gold a win gives, each living party member's share, the levels it reaches and what
they teach, the spells its equipment teaches, the lores Strago saw, Mog's dance, Terra's Morph supply
and the rages. All citations point into the pinned reference tree `original-src/` (everything8215/ff6 @
`1ea47b5`); line numbers are into `src/battle/battle_main.asm` unless another file is named. A
character record's cells are named from `notes/field-ram.txt:885-927`: record *n* starts at `$1600 +
37·n`, with the actor at `+$00`, the level at `+$08`, the experience at `+$11` (three bytes), the esper
at `+$1E` and the six pieces of equipment at `+$1F` to `+$24`. `$f0`-`$ff` are the win's direct-page
cells and `$2f35`-`$2f40` the four message variables, three bytes each. Lines marked `G:` are into
`src/btlgfx/btlgfx_main.asm`, the graphics program, which keeps the list of items obtained. This
document is the test authority for `tests/test_rewards.cpp`, `tests/test_rewards_oracle.cpp`,
`tests/test_items_obtained.cpp`, `tests/test_battle_end.cpp` and `tests/test_battle_end_oracle.cpp`.

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
| What each ending runs | `CheckBattleEnd :11891-11964`, `BattleEndTbl :12050-12056` | `playBattleEnd()` (`src/battle/battle_end.h`), `CommandExecutor::playBattleEnd()` |
| The win, in its order | `WinBattle :15282-15563` | `playBattleEnd()` with `BattleWon` |
| The loss | `LoseBattle :15588-15593` | `playBattleEnd()` with `BattleLost` |
| What the game keeps | `UpdateSRAM :12092-12179`, `RemoveChar :11879-11885` | `writeBack()` (`src/battle/battle_start.h`) |
| The wrap-up | `_c24903 :12062-12086` | `wrapUpBattle()` (`src/battle/battle_end.h`) |
| An item joining the list | `BtlGfx_05 G:357-373`, `LoadItemProp :13981-14037` | `obtainItem()` (`src/battle/items_obtained.h`) |
| The list merged | `_c14445 G:9448-9494`, `CheckInventoryFull G:21262-21273` | `mergeItemsObtained()` |
| The list as a battle starts | `G:43456-43460`, `G:2475-2480`, `InitBattle :6180-6193` | `itemsObtainedAtBattleStart()`, `BattleStart::itemsObtainedTail` |
| The list's last three places at power-on | `DecompCutsceneProg` (`src/cutscene/cutscene_main.asm:81-122`), `src/cutscene/opening.asm:1302-1314` | `itemsObtainedTailAtPowerOn()` |
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

## B. Messages and the step back — `ShowMsg :15602-15615`, `EndAction :374-380`

`ShowMsg` puts the message behind battle script command `$02` and ends the script (`$2d6e-$2d72`), then
plays it (graphics command `$04`); a message of `$ff` shows nothing. In the port each message is its own
`RunBattleScript` holding one `BattleMessage`, with the numbers its text reads — the first from the first
message variable (`$2f35`), the second from the second (`$2f38`); a name, an item, a spell or a party slot
is the variable's low byte, an amount its three bytes — and none where the text reads none.

| Message | Text | Numbers |
|---|---|---|
| 32 (`$20`) | got *item* × 1 | first: the item |
| 33 (`$21`) | got *item* × *n* | first: the item; second: *n* |
| 38 (`$26`) | got *n* GP, shown without waiting for a button | second: the gold |
| 39 (`$27`) | got *n* Exp. point(s) | first: the experience each ally gets |
| 41 (`$29`) | annihilated | — |
| 42 (`$2a`) | dispelled curse on shield | — |
| 45 (`$2d`) | learned *lore* | first: the lore's attack |
| 46 (`$2e`) | *name* gained a level | second: the party slot |
| 50 (`$32`) | *name* learned *spell* | first: the spell; second: the party slot |
| 51 (`$33`) | devised a new Blitz | — |
| 53 (`$35`) | got *n* magic point(s) | first: the magic points |
| 54 (`$36`) | Banon fell | — |
| 64 (`$40`) | mastered a new dance | — |
| 66 (`$42`) | mastered a new technique | — |

`EndAction` plays battle script command `$0d` — the acting battler steps back — as a `RunBattleScript`
holding `StepBack`.

## C. The wrap-up — `_c24903 :12062-12086`

`wrapUpBattle()`, in order:

1. `SaveMorphCounter :1762-1771`: with someone morphed (`$3ee2` not negative), the Morph supply
   (`$1cf6`) becomes the supply times the meter's high byte (`$3f31`), over 256 (`MultAB`, then `xba`).
   With no one morphed the supply is left as it is.
2. For party slots 3 to 0 (`ldx #$06 … dex2; bpl`, `:12065-12073`): the Morph meter shown (`$3b04,x`) is
   emptied and the slot's battle menu closes (graphics command `$03` with the slot in `$10`) — a
   `CloseBattleMenu` request per slot, slot 3's first.
3. No battle menu may open again (`lda #$80; tsb $b1`, `:12074-12075`) — `BattleState::menusDisabled`.
4. 32 frames pass (graphics command `$01`, 32 times, `:12076-12080`) — one `Wait` script command of 32
   frames. The presentation holds the script for that many frames, the frame it starts counting as the
   first.
5. **Every party member's hand is put away**, not only the ones marked as holding something: `lda #$0f;
   tsb $3a8c` (`:12081-12082`) marks all four, then `_c262c7 :16168-16187`, slots 3 to 0, takes each
   marked slot's mark off and, where its item in hand (`$32f4,x`) is not `$ff`, adds one of it to the list
   (§ D.1, graphics command `$05`) and empties the hand. So an item chosen for Item or Throw and never
   used comes back. Each one put away is also an `AddItemObtained` request.
6. The list is merged into the inventory (graphics command `$0a`, `:12084-12085`; § D.3).
7. `UpdateCharProp :5595-5631`: for party slots 3 to 0 whose equipment changed in the battle's Item menu
   (`$2f30,x`), the record takes the two hands' items and its battle stats are worked out again
   (`applyEquipmentChanges()`).

The same steps run after every action but 5: `_c262c7` alone runs once an action is over, for the party
members marked as holding an item (`putAwayItemsObtained()`), and nothing is merged then.

## D. The items obtained

The battle program hands an item to the graphics program as a five-byte record at `$2e72` built by
`LoadItemProp :13981-14037`: the item; its flags — `$80` set when it cannot be used in battle, `$20`
throwable, then the item type's bits from `ItemTypeMaskTbl` shifted once, `$40` a tool, `$10` a weapon,
`$08` a shield; its targeting (`ItemProp+14`); its quantity (`$2e75`, written by the caller); and one bit
per party slot, set where that party member cannot hold it (`:14021-14033`: `sec` on no match, `rol`), or
`$ff` for a type that is never held. The port's `BattleItem` holds each of these
(`battleItemFromRecord()`, `recordOf()`); the flags' low three bits, which `LoadItemProp` never sets, and
the fifth byte's high four are not held. An inventory slot (`$2686`, 256 records) has the same form.

1. **Adding — `BtlGfx_05 G:357-373`.** The record is copied to place `added & 15` of a sixteen-place list
   (`w7e602d`, five bytes a place, `btlgfx_ram.inc:97`) and `added` (`w7e64da`) goes up by one, wrapping
   past 255. Neither count is brought back down within the battle: the list is a ring.
2. **A battle's start.** The graphics program's start (`BtlGfx_00 G:379-381`) zeroes `$7E:6178-$7E:8D12`
   (`G:43456-43460`), which holds both counts, then writes `$ff` over the list's first **64** bytes
   (`G:2475-2480`, `cpx #$0040`; the disassembly marks it "should be #$0050 *** bug ***"): places 0-11
   whole and place 12's first four bytes, which reads as empty. **Places 13-15 are not reached**: they keep
   whatever was in them. `itemsObtainedAtBattleStart()` empties places 0-12 and both counts, and lays
   `BattleStart::itemsObtainedTail` into places 13-15; `writeBack()` hands places 13-15 back on
   `BattleResult::itemsObtainedTail` as the battle leaves them.
3. **A later stage of the final battle** plays script command `$12` in place of the graphics program's
   start (`InitBattle :6180-6193`): the whole list and both counts carry on into the stage.
4. **Merging — `_c14445 G:9448-9494`.** From place `merged & 15` (`w7e64db`): a place whose item is `$ff`
   ends the merge. Otherwise the inventory's 256 slots are searched for the item, first slot first
   (`G:9461-9466`):
   - **held**: the place's quantity is added to the slot's, eight bits wide with the carry unread, and a
     result of 100 or more becomes 99 (`cmp #$64; bcc; lda #$63`, `G:9482-9488`) — 98 + 5 is 99, but
     200 + 100 is 44;
   - **not held**: `CheckInventoryFull G:21262-21273` finds the first slot whose item is `$ff`, and the
     place's five bytes are copied into it — the quantity as it is, past 99 too. With no empty slot the
     merge ends there: the place keeps its item and `merged` does not move (`bcs @44a1`, `G:9469-9470`).

   Then the place's item becomes `$ff`, `merged` goes up by one, and the next place is tried. A place whose
   item is `$ff` is empty: nothing else in it is read.
5. **Who merges**: the wrap-up (§ C.6), each drop and the colosseum's prize (`_c26279 :16085-16091`, which
   adds and merges at once), and a party member's battle menu opening (`_c144a2 G:9500-9512`, from
   `_c102ca G:548-568`).
6. **What follows from the clear stopping short.** Merged after each item (as drops are), the first
   twelve fill and merge places 0-11. The thirteenth fills and merges place 12, and the merge walks on
   into places 13-15, merging whatever they hold, then to place 0, which it emptied, and stops: `merged`
   is 16. The fourteenth to sixteenth then go into places 13-15 and wait, since the merge looks at place 0;
   the seventeenth to twenty-eighth fill and merge places 0-11; the twenty-ninth fills place 12 and the
   walk merges the waiting three behind it. Whatever places 13-15 hold when the battle ends is handed
   back and is not in the inventory. An empty place 13 stops the walk at the thirteenth, and the next three
   merge as they come.
7. **A full inventory stops the merge for good.** A place whose item the party does not hold, with no
   empty slot, stays; every item after it waits behind it, held items too, in this battle and, for places
   13-15, the next.
8. **What a console just switched on holds there.** Every power-on runs the title screen, which lays the
   title and cutscene program over `$7E:5000-$7E:7C62` (`DecompCutsceneProg`,
   `src/cutscene/cutscene_main.asm:81-122`, `:105`); `OpeningCredits`, `FloatingContScene` and
   `WorldOfRuinScene` lay it again (`:55-75`). In it, `$7E:606E` is `ArmorBpdySprite_01`
   (`src/cutscene/opening.asm:1309-1314`, the opening's Magitek armor, body, second frame), so places
   13-15 read as item `$04` (Thief Knife) × 108, flags `$98`; item `$88` (Silk Robe) × 112, flags `$08`;
   item `$18` (Excalibur) × 136, flags `$80`. `itemsObtainedTailAtPowerOn()` reads them out of the
   decoded program at `$106E` (`RomAsset::CUTSCENE`, decoded by the cartridge's own decompressor). Other
   programs write over the same memory between battles; what each leaves there belongs to that program.

## E. The win's opening and the colosseum — `WinBattle :15282-15310`

1. The step back (§ B, `:15285`).
2. The graphics program's second status byte is cleared for the four party members (`stz $2e99,x`,
   `:15286-15289`): the party celebrates without the look of Condemned, Near Fatal, Image, Mute, Berserk,
   Muddled, Seizure or Sleep. Only the picture changes; the statuses stay.
3. The twelve message-variable bytes are zeroed (`:15290-15293`).
4. The celebration (graphics command `$08`, `:15294-15295`; `BtlGfx_08 G:243-274`): every battle menu
   closes (`_c10105 G:229-237`) — a `CloseBattleMenu` for slots 0 to 3 — then, unless the fanfare is off
   (`$2f49 & $02`: the formation's flag, `FormationAux::fanfareDisabled()`, or Shadow having just left,
   `BattleState::noVictoryFanfare`), the fanfare (song `$2f`) starts — a `VictoryFanfare` request, not
   asked for when the field's music plays on (`$11e4 & $08`, `BattleState::keepFieldMusic`) — and battle
   event 1 plays (script `$0f $01`, `G:279-280`) — a `RunBattleScript` holding `BattleEventPlays` of
   `kVictoryEvent`.
5. The wrap-up (§ C, `:15296`).
6. **In the colosseum** (`$3a97` not 0, `:15297-15310`): the prize (`$0207`, `colosseumPrize`) joins the
   items obtained, one of it, and is merged (`_c26279 :16085-16091`); message 32 tells of it; and the win
   is over — no experience, gold, learning or drops.

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

1. **A Cat Hood** (`$3c59,y & $10`, the fifth relic effects, `RelicEffect5::DOUBLE_GP`, `:15369-15376`):
   the first member wearing one doubles the gold total; a second does nothing (`tsb $f0; bne`).
2. **Terra** (the slot's actor `$3ed8,y` is Terra) with the party holding magicite (`:15377-15388`):
   magic points count as earned, and the Morph supply (`$1cf6`) gains twice the magic points (§ S).
3. **Experience** (`:15389-15394`): `AddExp` (§ H) with the share; a member with an Exp. Egg
   (`$3c59,y & $08`, `RelicEffect5::DOUBLE_EXPERIENCE`) gets `AddExp` a second time.
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

## M. The spells just learned — `ShowLearnedMagicMsg :15822-15837`

After a living member's levels (§ J), for an actor before Gogo: spells 53 to 0 of its list (`ldy #$0035`),
each at `$80` (`SpellProgress::kSpellJustLearned`) becomes `$ff` (`kSpellLearned`) and message 50 tells
of it, with the spell and the member's party slot.

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

## O. The drops — `:15474-15530`

Six places, empty, each with a count of 0 (`$f0-$fb`).

1. For monster slots 5 to 0 with Dead, Petrify or Zombie (`$3eec,x & $c2`, `:15482-15484`), **one random
   number is drawn** (`Rand`, `:15485`): below 32 (`kRareDropBelow`) the monster's rare drop, otherwise
   its common one (`MonsterItems+2` or `+3`, `:15486-15494`, the index built from the compare's carry). A
   drop of `$ff` is nothing. An empty slot is never fallen: the battle's start leaves its status 0 and its
   monster `$ffff` (measured on the first fight in Narshe).
2. The drop goes to the first place, 5 to 0, holding the same item or empty (`:15498-15511`), and that
   place's count goes up.
3. Places 5 to 0, each not empty (`:15514-15530`): the item joins the items obtained with that count and
   is merged (`_c26279`), then message 32 for a count of 1, message 33 for more.

So the first drop told of is the highest-numbered fallen monster's. The drops are the win's only random
numbers.

## P. The gold — `:15531-15563`

A gold total of 0 ends the win untold. Otherwise message 38 tells of it, and it is added to the party's
gil (`$1860-$1862`) in three bytes, the carry out lost, then held to 9,999,999 (`MaxGil`, `kMaxGil`).

## Q. The rages — `LearnRage`, and Gau after a Leap

- `LearnRage`: for each of the six monster slots whose monster index is below 256 (`$2002,x` is 0), that
  monster's rage joins the rages known (`$1d2c`, rage *n* is bit *n* mod 8 of byte *n* / 8) — **whether
  the monster is alive, fallen or gone**. An empty slot's index is `$ffff` and is passed over.
- `BattleEnd_02`: Gau's record leaves the party (`RemoveChar :11879-11885`) and Gau is no longer among
  the characters the party can field (`trb $1edf`, bit 3); then `LearnRage`.
  In the port, Gau's slot joins `BattleState::takenFromTheParty`, which `writeBack()` takes out of the
  party (§ U), and `BattleResult::gauLeftParty` is set. Leap is Gau's command, so the battler that leapt
  is Gau.

## R. The loss — `LoseBattle :15588-15593`

The defeat switch (`$3ebc` bit 0, battle switch 64, set by `writeBack()`), the wrap-up, then the message
(§ B), none for `$ff`. A lost battle gives nothing beyond what the wrap-up puts away.

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

Every rule here is reachable: the port owns the party, the switches, the monsters' slots, the
inventory and the list. The arithmetic play cannot reach — an award of 128 magic points or more, an
experience sum past three bytes, a share for no allies, a Cursed Shield in more than one slot, a quantity
sum past 255 — is reproduced all the same and checked by hand and against
the cartridge.

## U. What the game keeps — `UpdateSRAM :12092-12179`

`writeBack()` does what `UpdateSRAM` does: each party member's statuses (the first byte, with Float and
Interceptor of the fourth), HP and — unless its maximum MP is 0 — MP into its record; a fallen ghost, and
anyone taken from the party, out of it (`RemoveChar :11879-11885`: `stz $1850,x`, **the record's whole
party byte cleared** — party, slot, row, enabled (bit 6) and visible (bit 7): a default
`PartyMembership`); the battle inventory copied back; the colosseum's wager given up — one taken from each
slot holding it, last slot first, until a slot runs out, after which the search looks for empty slots
(`:12135-12150`: the emptied slot's `lda #$ff` replaces the wager the loop compares with); Doom Gaze's HP; the
twenty battle variables; and the battle found on the Veldt. Shadow walking off (`ShadowLeaves
:11990-12007`) clears his party byte the same way.
