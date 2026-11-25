# Bugs in the original game

Defects that exist in the original Final Fantasy VI and are reproduced here on
purpose. The port matches the shipped game's behavior, including where that
behavior is wrong, so anything listed on this page is deliberate and must not
be "fixed" without a decision to diverge.

Each entry names what goes wrong, where the original does it, and where the port
keeps it. If you are changing code near one of these, read its entry first —
the surrounding code often looks like it has a mistake in it, because it does,
and that mistake is the point.

---

## World-map movement reads terrain from the wrong world

**What happens.** While the party walks the overworld, one of the two terrain
lookups per step always reads the World of Balance's terrain table, whatever
world the party is actually on. In the World of Ruin that lookup returns the
properties of the *same tile index in a different world* — a tile that may have
entirely different passability, encounter and background bits.

**Where the original does it.** The world map keeps two 256-entry terrain
tables, one per world, and reaches them two ways:

- `GetWorldTileProp` (`world/move.asm:1366-1393`) offsets the lookup by the map
  index before indexing, so it reads the table belonging to the current world.
  This is the routine most callers use, and it is correct.
- `MovePlayer` (`world/move.asm:823-828`) indexes the table directly —
  `asl; tax; lda f:WorldTileProp,x` — with no map offset. The index it computes
  is the tile byte alone, so the read always lands in the first table.

`MovePlayer` stores that result and calls `GetWorldTileProp` separately for the
checks that need the right world's answer, which is why the game is playable
despite this.

**Where the port keeps it.** `src/data/world_tiles.h` exposes both reads as
separate functions:

| Function | Behavior |
|---|---|
| `worldTileProperties(world, tile)` | The correct lookup — the world's own table. |
| `worldOfBalanceTileProperties(tile)` | The direct read, always the World of Balance table. |

Movement code ported from `MovePlayer` calls the second. Everything else calls
the first. `tests/test_world_tiles.cpp` pins both, and asserts that the two
tables really do differ, so the distinction cannot quietly collapse.

**If you want to change it.** Making movement use `worldTileProperties()` would
alter World of Ruin walking behavior, so it is a gameplay change rather than a
repair. Anything that depends on it — routing, encounter rates, where the party
can and cannot step in the second half of the game — shifts with it.

---

## World animation frames store sprite rows the game never draws

**What happens.** Eight of the overworld's 108 animation frames hold more sprite
rows than they say they do. The surplus rows are complete, plausible sprites —
more trench arrows, more rocks — and nothing ever puts them on screen.

**Where the original does it.** Each frame is a count byte followed by four bytes
per sprite. Eight frames disagree with their own count:

| Frames | Count byte says | Rows actually stored |
|---|---|---|
| The six serpent-trench arrow frames | 2 | 4 |
| Two of the trailing frames | 4 | 6 |

Both world draw routines — `DrawVehicleSprites` (`world/sprite.asm:423-489`) and
`DrawWorldSprites` (`world/sprite.asm:495-554`) — loop exactly `count` times, so
the extra rows are never read. They are real cartridge bytes all the same: they
sit inside the block the game copies whole, between one frame's rows and the next
frame's count byte.

**Where the port keeps it.** `WorldAnimFrame` reports the two numbers separately:

| Accessor | Meaning |
|---|---|
| `spriteCount()` | What the game draws — the record's own count byte. |
| `storedRows()` | What the record holds, surplus rows included. |

Drawing code uses `spriteCount()`. `storedRows()` exists so the extra rows are
visible rather than silently dropped, and so a record's real extent is known —
which matters, because a frame's length cannot be derived from the pointer table
alone. `tests/test_world_anim.cpp` pins all eight frames by name and asserts no
ninth appears.

**If you want to change it.** Drawing `storedRows()` sprites would put arrows and
rocks on screen that the original never shows, at positions nobody chose for that
purpose. Removing the rows instead would change the cartridge's own bytes, which
this port does not carry and does not rewrite.

---

## World-map strafing reads past the end of its bearing table

**What happens.** Holding Y on the overworld strafes the vehicle along a bearing
picked from the direction being held. The code masks all four direction bits and
looks the result up in a table that only covers eleven of the sixteen
combinations, so five of them read whatever bytes follow the table.

**Where the original does it.** `StrafeAngleTbl` (`world/ctrl.asm:194`) holds
eleven bearings. The strafe routine (`world/ctrl.asm:201-206`) masks the
direction bits with `and #$000f`, discards the empty mask, and indexes with
everything else:

```
lda     $05
and     #$000f
jeq     @6e38       ; nothing held — no strafe
asl
tax
lda     f:StrafeAngleTbl,x
```

That leaves masks 11 through 15 — up with both left and right, and any mask
holding both up and down — landing past the last entry, on the routine's own
instructions read as a bearing.

The gap looks like an oversight rather than a judgment about what a control pad
can do: masks 3 and 7 both hold left *and* right together, and both are present
in the table, carrying a bearing of zero. Masks 11 and 15 hold the same opposed
pair and are simply absent.

Whether it can be reached depends on the controller. A stock pad pivots, so up
with down and left with right cannot be held at once; a pad without that pivot,
or an input device that reports directions independently, can produce all
sixteen.

**Where the port keeps it.** `src/data/world_cutscenes.h` exposes the table as it
stands and reports the shortfall rather than papering over it:

| Function | Behavior |
|---|---|
| `strafeAngle(mask)` | The bearing, or nothing when the mask falls past the table. |
| `strafeMaskReadsPastTheTable(mask)` | Whether a mask is one of the five with no entry. |

No bearing is invented for the uncovered masks — the original does not produce
one either, it produces whatever those bytes happen to be. Callers decide.
`tests/test_world_cutscenes.cpp` walks all sixteen masks and pins which eleven
are covered.

**If you want to change it.** Extending the table to sixteen entries means
choosing bearings for direction combinations the game never assigned any, which
is new behavior rather than a repair. Refusing to strafe on an uncovered mask is
the smaller change and is what falling through `strafeAngle()` already gives you.

---

## Dialog drops a glyph that would cross the right margin

**What happens.** When the next glyph of a dialog message would reach the text
area's right edge, the line breaks — and the glyph is skipped. It is not carried
to the new line; the message continues with the glyph after it.

**Where the original does it.** `DrawDlgText` (`field/text.asm:926-934`) checks
the glyph's width against the margin and, when it does not fit, calls `NewLine`
and returns without drawing. Both of its callers then step past the glyph
regardless (`:459-460`, `:827-828`), so nothing ever draws it.

In English the word wrap (`CalcTextWidth`, `:189-318`) measures each word before
it starts and breaks the line early, so whole words never reach the margin. What
does reach it is the space after a word that ends near the edge: with everyone's
default name, the shipped English script drops **305** spaces this way and
**no other glyph** — every one of those drops is invisible. The only text the
word wrap does not measure is a tab's fill, a choice marker's space, a gold
amount and a spell name; none of those sits near a margin in the shipped script.
Japanese text has no word wrap at all, so there the check is the only thing
keeping a line inside the window, and a glyph lost at the margin is a real
character missing from the message.

**Where the port keeps it.** `tickDialog()` (`src/text/dialog.cpp`) breaks
the line and drops the glyph exactly as the original does, and records each one
in `DialogState::droppedAtMargin` with where it would have gone.
`tests/test_dialog.cpp` pins the behavior on a hand-built message and the
305 / 0 count over the whole English script.

**If you want to change it.** Carrying the glyph to the start of the new line is
the obvious repair. On the English script it changes nothing visible, since only
spaces are affected; on Japanese text, or on English text with a renamed
character long enough to reach the margin, it changes where words break.

---

## A guest's battle equipment permissions come from their name

**What happens.** During a battle, the Item menu lets a party member take a
weapon or shield into a hand only if the item's equippable-by mask shares a bit
with that character's own bits. Terra through Umaro each have a bit of their own.
For anyone else in the party — Banon, Leo, the ghosts and the other guests — the
battle uses the fifth and sixth bytes of the character's name in that place, so
which items a guest can hold in battle depends on how their name is spelled.

**Where the original does it.** `InitParty` (`battle/battle_main.asm:7891-7908`)
compares the actor with `$0E` and builds the mask `1 << actor` only below it. At
or above it the shift loop is skipped, and the value stored as the mask is the
word the routine last loaded: the name's fifth and sixth bytes, copied for the
battle menu a few lines earlier (`:7898`). `LoadItemProp` (`:14022-14033`) tests
items against that mask, and the battle menu reads the result when a hand is
swapped (`btlgfx/btlgfx_main.asm:20982-20987`).

**Where the port keeps it.** `equipBitsFor()` in `src/battle/party_setup.cpp`
gives a character past the fourteen the name's fifth and sixth bytes as
`Battler::equipsAs`. `tests/test_party_setup.cpp`
(`PlaceParty.AGuestsEquippableBitsAreTheLastTwoLettersOfTheirName`) pins it.

**If you want to change it.** Giving guests an empty mask would stop them taking
anything into a hand in battle; giving them the bit `EquipPermissions::canEquip()`
uses for their actor would match the field menus. Either changes which items a
guest can hold.

---

## A seated character with no record is loaded from memory past the battle's own

**What happens.** A scripted battle can seat a character the party has no record
for. The battle still counts them as present, but it finds no record holding
them, and everything it would have loaded from the record — HP, MP, statuses,
level, stats, equipment, hands, commands and esper — it reads from memory past
the battle's own, whatever another program left there.

**Where the original does it.** `InitParty` (`battle/battle_main.asm:7848-7917`)
marks each seated slot present (`:7853`), then walks the sixteen records for the
slot's character (`GetCharID`, `:7868-7870`) and stores the record offset only on
a match (`:7910`). With no match the offset keeps the `$FFFF` the start filled in
(`:6063-6067`). `LoadCharProp` (`:6602-6661`), `UpdateEquipBattle`
(`:6745-6828`), `InitInventory` (`:13919-13929`), `InitCmdList`
(`:13701-13708`) and `ValidateSpellList` (`:14217-14218`) then read through it:
`$7E:1600` plus `$FFFF` and up is `$7F:15FF` and up.

**Where the port keeps it.** It does not: there is no memory past the battle's
own in the port for the reads to find. `seat()` in `src/battle/party_setup.cpp`
finds no record for the character and leaves `Battler::record` empty, and
`loadParty()` passes the slot by, so the character is present with nothing
loaded. In play the case does not arise: a record gets its character only from
the event command that gives a character its properties
(`field/event.asm:1079`), so a setup's character has a record whenever the story
has created them before the battle. `tests/oracle/test_battle_start_oracle.cpp`
(`BattleStartOracleTest.ASeatedCharacterWithNoRecordIsLeftOutOfTheComparison`)
starts such a battle on the cartridge and holds every cell not loaded through the
record to the port's.

**If you want to change it.** Giving such a character a record's worth of values
means choosing them, since the original has none to give; refusing to seat a
character with no record changes who stands in the battle.

---

## Monsters with the same English name are listed on separate lines

**What happens.** The battle's list of monsters' names counts monsters that
share a name on one line. In the English versions, two different monsters with
the same name usually get a line each, because the comparison does not read
their names.

**Where the original does it.** `LoadMonsterProp`
(`battle/battle_main.asm:7319-7359`) indexes the monster name table at the
monster number times eight and compares eight bytes. The Japanese table's
records are eight bytes long, so it compares names. The English table's records
are ten bytes long (`text/text_main.asm:180-187`), so for monster *n* it
compares the eight bytes at offset 8*n* — part of one name and part of the next —
and two monsters are grouped only when those windows match.
`UpdateMonsterGfxBuf` (`:15218-15234`) builds the list from the result.

**Where the port keeps it.** `loadMonster()` in `src/battle/battle_start.cpp`
compares the same eight-byte windows of the cartridge's name table to set
`Battler::nameGroup`. `tests/test_battle_start.cpp`
(`StartMonstersOnTheCartridge.EnglishNamesAreComparedEightBytesApartInTenByteRecords`)
pins it against the cartridge's names.

**If you want to change it.** Comparing `TextCorpus::monsterName()` records
would group the English names the way the Japanese ones are grouped; the list
of names would then show fewer lines in some battles.

## Blows are evaded by Magic Block, and Blind changes nothing

**What happens.** A physical attack is evaded with the target's Magic Block,
not its Evade, so Evade does nothing in battle. The adjustments meant for a
blow's chance to hit never apply either: an attacker's Blind, the target's
Beads, and the target's statuses that should raise or lower the chance (Blind,
Zombie, Confuse, Slow and Reraise; Poison, Sap, Near Fatal and Haste).

**Where the original does it.** `CheckHit` (`battle/battle_main.asm:5914-5985`)
picks the rating with `lda $3b54,y; bcs @2347; lda $3b55,y` — Evade if the carry
is set, Magic Block otherwise — and then skips the adjustments with
`bcc @2388`. Both read a carry the code means to hold "this is a blow". On every
path that reaches them it is clear: a spell arrives from `lsr` of a clear
physical bit, a blow from `lsr` of the Retort bit, which the branch before has
just found clear. So every attack uses Magic Block and none is adjusted.

**Where the port keeps it.** `checkHit()` in `src/battle/attack.cpp` works the
chance out from `Battler::magicBlockFactor` alone. `tests/test_hit_check.cpp`
pins both halves: `CheckHit.BlowsAreEvadedByMagicBlockNotEvade` and
`CheckHit.TheAttackersBlindnessAndTheTargetsStatusesDoNotChangeTheChance`.

**If you want to change it.** Reading `evadeFactor` for blows and applying the
adjustments would make Evade, Blind, Beads and those statuses matter, and would
change how often every physical attack in the game lands.

## A vanished target is hit by any spell, whatever protects it

**What happens.** A spell aimed at a vanished target hits it before anything
else is checked — before Reflect, before protection from instant death, before
evasion. An instant-death spell therefore kills a vanished target that is
otherwise immune to it.

**Where the original does it.** `CheckHit` (`battle/battle_main.asm:5803-5816`)
tests Vanish first; for an attack that is not physical and not level-divisible
it returns a hit at once, skipping every check after it. Vanish itself is not
taken off there.

**Where the port keeps it.** `checkHit()` in `src/battle/attack.cpp` returns a
hit at the same point. `tests/test_hit_check.cpp`
(`CheckHit.AVanishedTargetIsHitBySpellsAtOnce`) pins it with a Reflect-protected
target.

**If you want to change it.** Moving the Vanish test after the protection checks
would make Vanish a defense against instant death rather than a way past it.

## A back-hit step carries to the next target when its own target's damage leaves it

**What happens.** A blow adds one damage step for each target it hits in the
back, just before that target's damage is worked out, and working the damage out
uses the step up. So each target hit in the back normally gets one step. But
when a target's damage does not use the multiplier — the attack has no power,
its special effect missed that target, Zinger is holding that target, the damage
is a fraction of the target's
HP, the damage does not vary (as for GP Rain), or the attack ignores defense and
is not multiplied — the step is left over, and the next target hit gets it too:
two steps if it was hit in the back as well, one if it was not.

**Where the original does it.** `CalcAttackEffect`
(`battle/battle_main.asm:8349-8351`) adds to the damage multiplier `$bc` inside
the loop over the targets hit, before it skips the target Zinger holds
(`:8353-8354`) or one the effect missed (`:8358-8359`). `CalcDmgMod` (`:2034-2036`) runs the multiplier on
the target's damage, and `ApplyDmgMult` (`:8847`) sets `$bc` to 0 only when it
applies it. `CalcTargetDmg` returns before it for an attack of power 0
(`:1812-1813`), and does not call `CalcDmgMod` at all for fractional damage
(`:1814-1818`); `CalcDmgMod` returns before it when damage modification is off
(`:1967-1968`).

**Where the port keeps it.** `resolveAttack()` in `src/battle/attack.cpp` adds
to `AttackInFlight::multiplier` in the same loop, and `targetDamage()` in
`src/battle/damage.cpp` uses the steps up only where the original does.
`tests/test_attack_frame.cpp` (`AttackFrame.EachTargetHitInTheBackAddsAStepForTheRest`)
pins the frame's additions; `tests/test_target_damage.cpp`
(`TargetDamageTest.BackHits_EachTargetUsesUpTheStepAddedForIt`,
`TargetDamageTest.BackHits_TheStepCarriesWhenTheDamageIsNotModified`) pins one
step for each of two back hits, and one then two when the damage does not vary,
against the cartridge's routine as well.

**If you want to change it.** Taking the step off after each target, whether or
not its damage used it, keeps every step with its own target.

## A pincer cannot be run from at its start, even with every monster on one side

**What happens.** When a pincer attack opens, the party cannot run, even when
every monster stands on the same side of the field. In 33 formations that can
be fought as a pincer, a lone dragon among them, nothing stands on one of the
two sides, yet running stays blocked until something else refreshes the check,
such as a monster being defeated.

**Where the original does it.** `InitBattle` fills `$2000-$341F` with `$FF`
(`battle/battle_main.asm:6062-6066`) and checks running at `@24f2` (`:6162`, the
pincer test at `:15184-15193`) before it starts the battle graphics (`@253f`),
whose setup writes the two pincer sides `$2EAC`/`$2EAD`
(`btlgfx/btlgfx_main.asm:2854-2857`). The check reads both sides as `$FF`: every
monster on each side.

**Where the port keeps it.** `assembleBattle()` in `src/battle/battle_start.cpp`
sets both of `BattleState::pincerSides` to every monster before
`refreshRunDifficulty()` and lays the monsters out after it.
`tests/test_battle_layout.cpp`
(`BattleLayoutCartridgeTest.APincerWithEveryMonsterOnOneSideStillCannotBeRunFromAtFirst`)
pins it with the Latimeria.

**If you want to change it.** Laying the battle out before the check lets the
party run at once from a one-sided pincer; a pincer with monsters on both sides
is unchanged.

## Umaro's throw adds one to the power of Mog and every ally after him

**What happens.** When Umaro throws an ally, the ally's battle power joins his.
For Mog, Gau, Gogo and the guests the throw is one power stronger than their two
hands' battle power: a thrown Mog with 40 and 20 adds 61, a thrown Locke with the
same hands adds 60.

**Where the original does it.** `UmaroAttack_00`
(`battle/battle_main.asm:3550-3562`) compares the thrown ally's actor with Mog
(`cmp #$0a`) to make Mog's throw a critical hit, and then adds the ally's two
hands' battle power with `adc` without clearing the carry, so the compare's
carry — set for Mog and every actor after him — is added in.

**Where the port keeps it.** The throw in `src/battle/commands.cpp` adds one for
any ally whose actor is Mog or later. `tests/test_commands.cpp`
(`Commands.ThrowingMogIsACriticalHitAndAddsOne`,
`Commands.AnAllyAfterMogAlsoAddsOne`) pins it.

**If you want to change it.** Dropping the extra one makes every thrown ally add
exactly its hands' battle power.

---

## A condemned count starts one higher than its formula

**What happens.** A condemned count starts at 20 plus 60 less a roll of the
level plus a random part below it, or at 20 when the roll passes 60. Whenever the
roll is 60 or less the count is one higher: a level 1 roll of 1 starts at 80, not
79, and every count a battle's start gives (at level 20) is one higher too.

**Where the original does it.** `StartCondemn`
(`battle/battle_main.asm:1512-1525`) subtracts the roll from 60 with `sbc` after
`sec`, branches past the clear to 0 on the carry the subtraction left, and then adds
20 with `adc`, so that carry — set whenever nothing was borrowed — is added in.

**Where the port keeps it.** `startCondemnedCount()` in `src/battle/battle_loop.cpp`
gives 81 less the roll while the roll is 60 or less. `tests/test_battle_loop.cpp`
(`AfterAction.TheCondemnedCountStartsFromTheAttackersLevel`) and
`tests/test_system_commands.cpp`
(`LastAttackLevel.ACountStartedAfterAnActionIsWorkedOutFromItsAttacksLevel`) pin it.

**If you want to change it.** Dropping the one makes every count that starts at 20
or more start at 20 plus 60 less the roll.

---

## An equipment change sets the level the next condemned count starts from

**What happens.** A condemned count is worked out from the level of the last
attack made, and an action that sets no level of its own keeps that one.
Changing a party member's equipment during a battle rewrites it: until the next
attack sets a level, it is the hit rate of what the member holds in the shield
hand — a second weapon's, 200 for an empty hand, 0 for a shield or anything
else. A count started then is worked out from that number and not from anyone's
level, so 0 starts it at 81 and anything over 60 at 20. When two members change
equipment at once, the one in the lower slot decides it. The battle's start
refreshes every member the same way and then sets the level to 20, before
anything reads it.

**Where the original does it.** `UpdateEquip` clears `$11a0-$11de`
(`battle/battle_main.asm:2301-2304`), and `CalcEquipEffect` stores the hit rate
of an item of the weapon type at `$11ae,y`, Y being 1 for the shield hand
(`:2611-2641`); a shield's branch writes no hit rate (`:2546-2573`). `$11af` is
the byte an attack's setup leaves the attacker's level in, and `StartCondemn`
reads it (`:1512-1525`). `UpdateCharProp` runs the refresh for each member whose
equipment changed, slot 3 first (`:5595-5631`); `InitChars` refreshes each
member at the start (`:6204-6221`) before the 20 is written (`:6130-6131`).

**Where the port keeps it.** `refreshTheLastAttackLevel()` in
`src/battle/party_setup.cpp` sets `BattleState::lastAttackLevel` to the shield
hand's `Hand::hitRate`, from `loadParty()` and `applyEquipmentChanges()`.
`tests/test_party_setup.cpp`
(`EquipmentChanges.TheRefreshLeavesASecondWeaponsHitRateAsTheLastAttacksLevel`,
`EquipmentChanges.TheRefreshLeavesNoLevelWithAShieldInTheShieldHand`,
`EquipmentChanges.TheRefreshLeavesTheBareHandsHitRateWithNothingInTheShieldHand`,
`EquipmentChanges.OfTwoMembersRefreshedTheLowerSlotLeavesTheLevel`),
`tests/test_system_commands.cpp`
(`LastAttackLevel.ACountStartedAfterAnEquipmentRefreshIsWorkedOutFromTheRefreshsLevel`)
and `tests/test_battle_start.cpp`
(`StartBattle.APartyHoldingASecondWeaponStillStartsItsCountsFromTwenty`) pin it;
the cartridge checks compare the level with `$11af` at every loop step of a whole
battle.

**If you want to change it.** Leaving `lastAttackLevel` alone in the equipment
refresh keeps the last attack's level through an equipment change.

---

## A strong enough spell does almost nothing at level 99

**What happens.** A spell's base damage is four times its power plus the power
times the caster's magic stat times the level, divided by 32 — and that sum is
kept as a 16-bit number. At level 99 it passes 65,535 for every spell of power 84
or more once the magic stat is high enough, and starts over from zero. Ultima
(power 150) cast by a party member with a magic stat of 140 — a magic power of 94
and up, the stat being one and a half times it — comes to 65,568 and does 32
before the target's defense; at a stat of 139 it does 65,104. Goner (power 220),
a monster's spell, wraps from a magic stat of 95 and does 3. Atomic Ray (80) and
everything weaker never reach it.

**Where the original does it.** `CalcMagicDmg` (`battle/battle_main.asm:7188-7210`)
shifts the 24-bit product right five times keeping 16 bits (`LsrA`, `:2150-2158`)
and adds the base with a 16-bit `adc`.

**Where the port keeps it.** `magicDamage()` in `src/battle/damage.cpp` keeps
the low 16 bits of the sum. `tests/test_damage_formulas.cpp`
(`DamageFormulaTest.MagicDamage_WrapsPastSixtyFiveThousand`) pins (150, 140, 99)
at 32 and (150, 139, 99) at 65,104, against the cartridge's routine as well.

**If you want to change it.** Working the sum out in `int` and stopping it at
65,535 keeps the strongest spells at the cap instead of wrapping; every spell
under the wrap is unchanged.

---

## A strong enough blow loses most of its damage at a high level

**What happens.** A blow's base damage multiplies the attack (the power, four
times it for a monster, plus the vigor stat) by the level twice, and the first
product is kept as a 16-bit number before the second multiplication. When it
passes 65,535 the blow does a fraction of what it should. The Guardian at Vector,
power 250 and level 71, has an attack of 1,056 to 1,063 (its vigor is drawn from
56 to 63), and 1,056 × 71 = 74,976 is kept as 9,440: its blows come to 2,618 to
2,756 before the target's defense where 20,795 to 20,933 was meant. For a party
member it takes a weapon of power 255 held in both hands (counted as 446) with a
vigor stat of 255 from level 94: at level 99 the blow comes to 2,494 where 40,506
was meant.

**Where the original does it.** `CalcDmg` (`battle/battle_main.asm:7253-7259`)
reassembles the low two bytes of `Mult24`'s 24-bit product and drops the third
before multiplying by the level again.

**Where the port keeps it.** `physicalDamage()` in `src/battle/damage.cpp` keeps
the low 16 bits of the first product. `tests/test_damage_formulas.cpp`
(`DamageFormulaTest.PhysicalDamage_TheFirstProductByTheLevelKeepsSixteenBits`)
pins a party member's blow at 2,494 at level 99 and 2,070 at level 98, against
the cartridge's routine as well.

**If you want to change it.** Keeping the whole product makes the Guardian's
blows, and a level-94-plus party member's strongest blows, land at their full
strength; every blow under the cut is unchanged.

---

## A hard enough Atma Weapon hit does almost nothing

**What happens.** The Atma Weapon multiplies each hit's damage by its strength —
the wielder's level, times its HP left counted in 256s, divided by its maximum
HP counted the same way — divides by 64 and adds 1, and keeps the result as a
16-bit number. When the damage times the strength reaches 4,194,304 the result
starts over from zero. At level 99 and full HP the strength is 99, so any hit of
42,367 or more before the weapon's own step wraps: a hit of 45,000 does 4,074
instead of the 9,999 a hit can do at most, and one from 42,367 to 48,830 always
shows less than 9,999. A level-99 wielder's critical hit can be that strong. The
beam the hit shows is chosen from the wrapped number, so such a hit can also show
a shorter beam.

**Where the original does it.** `UltimaEffect` (`battle/battle_main.asm:2248-2281`)
multiplies with `Mult24`, shifts the 24-bit product right six times keeping 16
bits (`LsrA`, `:2150-2158`), adds 1 with a 16-bit `inc`, and compares that with
501 and 1001 for the beam.

**Where the port keeps it.** The Atma Weapon's step in `targetDamage()` in
`src/battle/damage.cpp` keeps the low 16 bits. `tests/test_target_damage.cpp`
(`TargetDamageTest.Atma_TheDamageWrapsPastSixtyFiveThousand`) pins 45,000 at level
99 at 4,074 with the longest beam, against the cartridge's routine as well.

**If you want to change it.** Working the product out in `int` and stopping it at
65,535 lets the strongest Atma Weapon hits reach the 9,999 cap; every hit under
the wrap is unchanged.

---

## A monster that drains with its special attack answers its own drain

**What happens.** A drain that is a blow marks the drainer as having been
attacked by its own target, waiting to answer it. A monster whose special attack
drains is then counted as attacked, and once anything has damaged it during the
battle its counter script's "if hit" condition holds, so it answers its own
drain at once. Number 128, whose special attack drains HP, and Soul Saver, whose
special attack drains MP, both use their special attack in their main script and
both counter "if hit": each can follow its own drain with a counterattack. A
drain that is a spell marks the drainer without the waiting, and nothing follows
it.

**Where the original does it.** `CalcMaxDmg` (`battle/battle_main.asm:1911-1924`)
marks the drainer with `_c2362f` (`:8641-8652`), whose waiting bit is the carry
the attack's physical bit left at `:1892-1893` — clear after a Zombie drainer's
draw gave Poison or Blind (`ZombieEffect`, `:2220-2233`). `CheckRetal`
(`:12534-12563`) starts the counter script of a battler waiting to answer
someone else, and the "if hit" condition `AICond_05` (`:4704-4712`) holds once
`$327c` names a battler that has damaged it (`:2964`).

**Where the port keeps it.** `targetDamage()` in `src/battle/damage.cpp` marks
the drainer's `Battler::lastAttacker` and `Battler::retaliationPending`, and
`checkCounterattacks()` in `src/battle/battle_loop.cpp` reads them.
`tests/test_target_damage.cpp`
(`TargetDamageTest.Drain_TheDrainerWaitsToAnswerAfterABlowNotASpell`,
`TargetDamageTest.Drain_AZombieDrainersBlowWaitsOnlyWhenItsDrawGaveNothing`) pins
the mark, against the cartridge's routine as well.

**If you want to change it.** Marking a blow's drainer without the waiting, as a
spell drain does, stops it answering its own drain; a battler a drain hits still
answers it.

---

## An attacker that hides and dies waits to answer itself after some blows

**What happens.** An attacker effect that hides its attacker and sets it to die
marks the attacker as attacked by itself. Whether that mark waits to be answered
is decided by the processor's carry as the attack left it, not by the attack: it
waits after a blow by a party member in a side attack, or by a monster in a
pincer or a side attack, and not otherwise. Spiraler, the only attack with that
effect, is a spell, so in play its mark never waits.

**Where the original does it.** `AttackerEffect_16` (`battle/battle_main.asm:10804-10814`)
reaches `_c2362f` (`:8641-8652`) through `_c2384a` and `_c23852` with the carry
`UpdateFacingDirection` (`:3218-3271`) left: its `lsr` of the physical bit, then
`cmp #$03` for a party member (`:3227`) or `cmp #$02` for a monster (`:3251`).
`DoAttackerEffect` (`:10118-10129`) passes the carry through.

**Where the port keeps it.** `facingStepLeavesTheMarkReady()` in
`src/battle/attacker_effects.cpp`, read by `spiraler()`. `tests/oracle/test_attack_oracle.cpp`
compares the whole attack with the cartridge's frame.

**If you want to change it.** Passing `false` to `hideAndDie()` leaves the mark
unready for every record, as it is for Spiraler's own.

---

## An attack after a bounce can take two lore list entries for its targets

**What happens.** A spell that bounces off Reflect leaves the bounce's aiming rule on for the rest of
the command. Each later attack of the command, aimed at the other side only, goes half the time to
the battler the last params block names in its attacker's place — and after a bounce that block is
the bounce's, which holds there where the bounce off the second party slot went. When nothing
bounced off that slot the place holds `$FF`, and the aim reads past the end of the battlers' target
bits: it takes the lore list's entries for Revenge and Pearl Wind as the party's bits and the
monsters' bits. A known lore's entry is `$3E` or `$3F`, an unknown one `$FF`; either overlaps almost
any target set, so the attack is aimed at that word, bits that name no battler included. A pick of
one target among them can land on such a bit, and the attack then reaches no one. It needs a command
that makes more than one attack, one of them a spell that bounces.

**Where the original does it.** `CalcReflectDmg` (`battle/battle_main.asm:8387-8388`) sets `$ba`
bit 4, which only its Launcher path clears (`:8393-8394`), and writes the bounce's block with
`$a0` + the battler bounced off ← the battler it went to (`:8424-8427`). `CheckTargetsBattleType`
(`:14727-14739`) reads byte 1 of the block before the next one (`$2c5f,x` with X = `$3a32`), doubles
it and loads `$3018,x` as a word: X = `$FE` loads `$3116-$3117`, the lore list `InitSpellList`
(`:14062-14079`) fills over the `$FF` `InitBattle` (`:6061-6067`) leaves. `RandBit` (`:13495-13515`)
picks among all sixteen bits.

**Where the port keeps it.** `BattleScriptBuilder::lastParamsAttacker()` in
`src/battle/battle_script.cpp` tracks the place; `loreListReadAsTargets()` and `pickOne()` in
`src/battle/targeting.cpp` take the word and pick from every bit. `tests/test_attack_oracle.cpp`
(`TheAttackAfterABounceTakesTheLoreListForItsTargets`) compares it with the cartridge's frame, and
`tests/test_targeting.cpp` pins the word.

**If you want to change it.** Passing the attack's own attacker to `chooseTargets()` in
`resolveAttack()` (`src/battle/attack.cpp`) aims a later attack back at the caster instead.

---

## The Phantom Train's background names two tiles nothing loaded

**What happens.** Two cells of the Phantom Train battle background's tilemap
name tiles 652 and 653. No graphics block of any background covers those
numbers: block 1 holds tiles 0 to 127 (0 to 255 when it is double width),
block 2 128 to 255 and block 3 896 to 1023. On the hardware the two cells show
whatever that part of video memory happened to hold from before the battle,
which is not part of the background's art.

**Where the original does it.** `LoadBattleBG` (`btlgfx/btlgfx_main.asm:4090-4144`)
transfers the three blocks to fixed places in video memory and the tilemap as
it is; nothing checks that a cell's tile number lands in a block. The tilemap
is the compressed stream `BattleBGTiles::GHOST_TRAIN` (`gfx/battle_bg.asm`),
background `BATTLE_BG::GHOST_TRAIN` (50). The other background whose cells
reach outside its blocks' art, `BATTLE_BG_31` (49), upstream marks unused and
no map or event names it.

**Where the port keeps it.** `BattleBackgroundPicture::sheetTile()`
(`src/data/battle_background_art.h`) returns nothing for a number no block
loads, and `BattleBackground` (`src/render/battle/battle_background.h`) draws
such a cell as a blank tile. `tests/test_battle_background_art.cpp` decodes all
56 backgrounds and pins that only this one has such cells, and that they are
exactly these two.

**If you want to change it.** Drawing something there means choosing art the
game never assigned; blank is the one choice that invents nothing.

---

## "Miss" sits right of the battler it is over

**What happens.** A damage numeral is centered on its battler, but the word
"Miss" is not: shown on its own it starts at the battler's center and runs 16
pixels to the right, and shown with every other battler's numeral at once it
starts 8 pixels further right still.

**Where the original does it.** A numeral's digits are copied into its graphics
from the first tile, and its offset — how far left of the battler it starts —
is half its width, 4 a digit (`btlgfx/btlgfx_main.asm:24733-24749`,
`:24894-24911`). "Miss" is copied one tile in (`+$20`, `:24712`, `:24864`), and
given an offset of 8 when shown on its own (`GfxCmd_0b :24717-24718`) and of 0
when shown together (`_c1a5fa :24841`, where the offset was cleared and never
set for a miss). The two tiles of "Miss" therefore start 8 right of where the
offset puts the numeral: at the center on its own, 8 past it together.

**Where the port keeps it.** `battle::ShownNumeral::offset` carries the two
offsets (`kSingleMissOffset`, `kTogetherMissOffset` in
`src/battle/presentation.cpp`), and `render::DamageNumbers` draws "Miss" one
cell further right than its offset. `tests/test_battle_presentation.cpp` and
`tests/test_battle_hud_components.cpp` pin both.

**If you want to change it.** Centering "Miss" means an offset of 8 and no
extra cell in both cases.

---

## A counterattack is decided on statuses the battler never gets

**What happens.** Whether a monster answers an attack is decided on the
statuses it is about to have. If they include Sleep, Confuse, Berserk, Frozen or
Stop, its counter script runs without doing any of its actions, so it does not
answer. Those statuses are taken at the start of the statuses taking effect,
which is too early. The statuses it is immune to are not yet held back from coming
off, the statuses going on and coming off have not yet done what they do, and a
Zombie's statuses have not yet been taken away. A monster that is already a
Zombie and is hit by something that would confuse it therefore does not answer.
A Zombie cannot be confused, and the confusion never shows.

**Where the original does it.** `InitStatusVars`
(`battle/battle_main.asm:11298-11312`) runs `CalcStatus` into `$3e60`/`$3e74`
for a battler whose last attacker is marked waiting (`$32e0` bit 7). It does so
before `UpdateStatus` masks the clears (`:11059-11061`, `:11070-11072`), before
the status handlers run (`:11041-11080`), and before `_c24585` takes a Zombie's
statuses off (`:11319-11328`). When those cells hold Sleep, Confuse, Berserk,
Frozen or Stop, `ExecAIRetal` (`:12481-12493`) sets the counter script to run
without its actions. The interpreter then treats every command below `$fc` as a
failed condition (`:4370-4374`) and skips to the next "end if" or the script's
end (`:4314-4325`).

**Where the port keeps it.** `applyStatusChanges()` in `src/battle/status.cpp`
sets `Battler::statusForCounter` at the same point, for a monster's answer to
read. `tests/test_status_changes.cpp`
(`StatusChangesTest.TheStatusesForACounterattackAreTakenBeforeTheMask`,
`StatusChangesTest.TheStatusesForACounterattackAreTakenBeforeTheHandlers`) pins
both against the cartridge's own routine.

**If you want to change it.** Taking the statuses for a counterattack from the
battler's statuses once the pass is over makes whether it answers follow what
the player sees.

---

## A party member answers an action that made no attack as if it were the attack before

**What happens.** After every action the battle decides who answers it, and a
party member's Retort, Interceptor and Black Belt answer only a physical hit.
Whether the hit was physical is read from the last record loaded, not from the
action just run. An action that makes no attack (a pose reset, a battle message,
Scan, a story event) leaves the flag as the attack before it set it, so a party
member it reaches is judged on that attack. Queuing a command loads its record
too: a battler that queues one with no player choosing — a Muddled or Berserk
party member, a monster, an AI's attack — sets the flag to that command's,
and each action already waiting for the battler is aimed again in turn, the last
one's left. The flag shares its byte with the
start's spell lists and an equipment sum, so at the battle's start, and after an
equipment change, it says whether the refreshed party member's stamina is odd
until the next attack, and a start that refreshes no member leaves it set. The
same refresh rewrites the level the next condemned count starts from (*An
equipment change sets the level the next condemned count starts from*).

**Where the original does it.** `CheckRetal` reads bit 0 of `$11a2`
(`battle/battle_main.asm:12565-12569`). The attack's record lands there when an
action loads one (`InitTarget`, `:6452-6480`), and when a command below `$1E` is
queued: `CalcCmdDelay` runs `InitTarget` for it before it looks at the targets
(`:621-622`, `:637`, `:640-642`), for an automatic command (`:546-568`), a
monster's own choice (`:1053-1054`), an AI's attack (`:4441-4442`) and each
action already waiting (`:517-533`). An item used as itself loads none
(`InitTarget_01`, `:6525-6527`). `ExecCmd` resets the action's other
cells (`:3103-3109`) but not this one. `InitSpellList` fills `$11a0-$11d5` with
`$ff` (`:14055-14059`) before the party is refreshed; `UpdateEquip` clears `$11a0-$11de` and
leaves the member's stamina in `$11a2` (`:2302-2304`, `:2334-2339`), for each party
member at the start, slot 3 first (`InitChars`, `:6204-6221`), and after an
equipment change (`UpdateCharProp`, `:5621-5624`).

**Where the port keeps it.** `BattleState::lastAttackPhysical`, set in
`CommandExecutor::execute()` and `CommandExecutor::aim()` only when a record was
loaded (`AttackInFlight::recordLoaded`, set by `loadAttackRecord()` and
`itemAttack()`), and by the equipment refresh and `loadParty()` in
`src/battle/party_setup.cpp`. `tests/test_attack_frame.cpp`
(`CommandExecutor.AnActionThatMakesNoAttackKeepsTheLastAttacksPhysicalFlag`,
`CommandExecutor.AQueuedFightLoadsTheBattleRecordsPhysicalFlag`,
`CommandExecutor.AQueuedSpellLoadsTheSpellsRecord`,
`CommandExecutor.TheLastOfTwoPendingActionsLeavesItsRecord`,
`CommandExecutor.AQueuedItemUsedAsItselfLoadsNoRecord`) and
`tests/test_battle_start.cpp`
(`StartBattle.TheLastAttacksPhysicalFlagStartsAsWhetherTheLowestMembersStaminaIsOdd`)
and `tests/test_party_setup.cpp` (`EquipmentChanges.TheRefreshLeavesTheMembersStaminaAsTheLastAttacksPhysicalFlag`,
`EquipmentChanges.AStartThatRefreshesNoMemberLeavesTheLastAttacksPhysicalFlagSet`) pin it; the cartridge checks compare the flag with `$11a2` at every loop step of
a whole battle and at every start.

**If you want to change it.** Setting `lastAttackPhysical` to false for an action
that makes no attack, and leaving it alone at the start and in the equipment
refresh, judges each action on its own hit.

---

## A failed Steal is remembered as a Fight

**What happens.** A battler records the command it was last hit with, and a
monster's script can answer a command used against it ("if hit by Steal").
When Steal fails — the target has nothing, the chance or the draw is against
the thief, the slot drawn is empty, or a monster's steal finds no gil — the
target's record is written over with Fight. A monster scripted to answer Steal
therefore answers only the steals that succeed, and one scripted to answer a
Fight answers a failed Steal as well.

**Where the original does it.** `TargetEffect_52`
(`battle/battle_main.asm:9307-9379`) writes 0 into `$3d48,y` on every failure
(`:9353-9356`), after `_c235e3` (`:8586-8588`) has recorded the command used.
`AICond_01` (`:4667-4682`) compares `$3d48` with the script's commands.

**Where the port keeps it.** `steal`'s failures in
`src/battle/target_effects.cpp` set `Battler::lastHit.command` to Fight.
`tests/test_target_effects.cpp`
(`TargetEffectTest.StealingFromATargetWithNothingSaysSoAndIsRememberedAsAFight`,
`TargetEffectTest.AMonstersStealFailsOnADrawOf192OrMore`) pins it, against the
cartridge's routine as well.

**If you want to change it.** Leaving the record alone on a failure lets a
monster answer every Steal used against it.

---

## A monster's Steal takes its level times 256 gil

**What happens.** A monster that steals takes gil from the party: its level
times 256, plus 20. A level-12 monster takes 3,092; a level-40 one 10,260. GP
Rain, worked out right beside it, takes the level times 30 — 360 and 1,200 for
the same two — which is the scale the steal looks meant to have.

**Where the original does it.** `TargetEffect_52`
(`battle/battle_main.asm:9363-9367`) builds the amount as `lda $3b18,x; xba;
lda #$14` — the level as the high byte, 20 as the low — and hands it to
`TakeGil`. `AttackerEffect_51` (`:10362-10366`) multiplies the level by 30 with
`MultAB`.

**Where the port keeps it.** `monsterSteals()` in
`src/battle/target_effects.cpp`. `tests/test_target_effects.cpp`
(`TargetEffectTest.AMonsterStealsItsLevelTimes256Plus20Gil`) pins the amount,
against the cartridge's routine as well.

**If you want to change it.** Multiplying the level by 30, as GP Rain does,
gives an amount on GP Rain's scale; whatever the party loses still goes to the
monster's gold.

---

## A failed Metamorph says the monster became an item

**What happens.** Metamorph draws an item from the monster's pack, then draws
against the monster's odds. The message "Better off {item}!" naming the drawn
item is put in the place of the attack's animation before the odds are drawn,
so it shows whether the monster turns or not. On a failure the monster stays
in the fight and the party gets nothing, under a message saying it became the
item. In the first fight, a guard given odds that never succeed shows "Better
off Green Cherry!" for 65 frames and stays standing.

**Where the original does it.** `TargetEffect_12`
(`battle/battle_main.asm:9385-9418`) stores the item in `$2f35` and writes
animation command `$02` with message `$1d` into `$3a28`/`$3a29`, adding it to
the script (`:9396-9403`), and only then compares a draw with
`MetamorphRateTbl` (`:9407-9409`), branching past the item and the monster's
death on a failure.

**Where the port keeps it.** `metamorph()` in
`src/battle/target_effects.cpp`. `tests/test_target_effects.cpp`
(`TargetEffectTest.AFailedMetamorphStillWritesBetterOffInPlaceOfTheAnimation`)
pins it, against the cartridge's routine as well; `tools/damage-trace/README.md`
shows the failed Metamorph set up on the running cartridge.

**If you want to change it.** Writing the message after a successful draw only
leaves a failed Metamorph with the attack's own animation.

---

## One Mag Roader answers attacks with Wild Cat's turn script

**What happens.** The fourth Mag Roader's script ends its turn part and is
missing the marker that ends its counterattack part. The game therefore reads
the next script in the table — Wild Cat's turn part — as the Mag Roader's
counterattacks. When it is attacked and is the only monster left, it may answer
with Fire Ball (one time in three, nothing otherwise); with others still
standing it answers with Wild Cat's blows, alternating between its two turns.

**Where the original does it.** `battle/ai_script.asm:2530-2537` (script 243)
has no `end_retal`. `InitAI` (`battle/battle_main.asm:7442-7466`) takes the byte
after the turn part's end as the counterattack part's start unless it is
`$ff`: that byte is the first of script 119 (`:2542`), 3410 bytes into the
script block.

**Where the port keeps it.** `src/data/generated/ai_scripts_data.inc` gives
script 243 the counterattack part `kWildCatTurn`, and `readyAi()`
(`src/battle/ai.cpp`) sets it as the slot's. `tests/test_ai_run.cpp`
(`AiReadyTest.TheMagRoaderAnswersWithWildCatsTurnPart`) and
`tests/test_ai_run_oracle.cpp`
(`AiRunOracleTest.TheMagRoadersCounterattackPartIsWildCatsTurnPart`) pin it,
against the cartridge's routine as well.

**If you want to change it.** Giving the row an empty counterattack part makes
the Mag Roader answer nothing, as its own script reads.

---

## A condition on the monster itself leaves the monster as the target

**What happens.** A script condition that names a target also makes it the
target of the attacks that follow it in the block. For a condition on the
monster itself the game means to clear that target afterward, and clears only
the party's half of it: the monster's own place stays. A monster's next attack
in such a block is aimed at itself unless the script sets a target first. A
party member a scripted battle gives a script is left with no target.

**Where the original does it.** `AICond_17`
(`battle/battle_main.asm:4920-4939`), the target test six other conditions also
use, ends with `stz $fc; stz $fc` (`:4936-4937`) when the operand is `SELF`: the
same byte twice, the party's half of the targets word, with the accumulator one
byte wide.

**Where the port keeps it.** `ConditionRules::testTarget()` in
`src/battle/ai_conditions.cpp` keeps the targets' monsters only.
`tests/test_ai_conditions.cpp` pins it for a monster and for a party member,
against the cartridge's routine as well.

**If you want to change it.** Clearing the whole targets word leaves the attacks
after the condition aimed as the run started.

---

## A new monster keeps the script memory of the one before it in its slot

**What happens.** When a battle changes formation, each monster slot keeps
what its last monster's script had: its own variable and timer, where each part
of its script had stopped at a wait and how many blocks it had skipped then,
and — if the new monster's script has no counterattack part — the old
monster's counterattack part. The new monster's first turn can therefore go on
from the middle of the old monster's script, and a new monster can answer
attacks as the old one did.

**Where the original does it.** `Cmd_20` (`battle/battle_main.asm:13219-13230`)
clears only `$3aa8`, `$3e54`, `$2001` and `$320c` before loading the new
monsters; `InitAI` (`:7442-7466`) writes the turn part's start and, only for a
script with one, the counterattack part's.

**Where the port keeps it.** `readyAi()` in `src/battle/ai.cpp` sets the turn
part and, only when the script has one, the counterattack part, and nothing
else; `changeFormation()` leaves the slot's fields alone. `tests/test_ai_run.cpp`
(`AiReadyTest.ANewMonsterWithNoCounterattackPartKeepsTheSlotsAndAllItsScriptMemory`)
and `tests/test_ai_run_oracle.cpp`
(`AiRunOracleTest.ReadyingEveryScriptIntoASlotThatHeldAnotherKeepsWhatTheCartridgeKeeps`)
pin it, against the cartridge's routine as well.

**If you want to change it.** Clearing `Battler::turnResume`, `counterResume`,
`counterScript`, `aiVariable` and `turnTimer` when a monster is loaded starts
each new monster's script afresh.

---

## Thirteen items obtained bring three more that were never obtained

**What happens.** A battle's start empties the first 13 of the 16 places for items the party comes
away with. The last three keep whatever was in them. When a thirteenth item is obtained in one battle,
whatever those three places hold follows it into the inventory. On a console just switched on they hold
part of the opening's Magitek armor animation, which reads as 108 Thief Knives, 112 Silk Robes and 136
Excaliburs; an item the party does not already hold arrives in that number, past the usual 99. The
fourteenth, fifteenth and sixteenth items obtained in that battle then wait in those places and stay out
of the inventory until a twenty-ninth is obtained, or until a later battle's thirteenth.

**Where the original does it.** The graphics program's start writes `$FF` over the list's first 64
bytes, where 80 are the list (`btlgfx/btlgfx_main.asm:2475-2480`, marked "should be #$0050" in the
disassembly), and zeroes the two counts (`:43456-43460`). The merge (`_c14445`, `:9448-9494`) runs on
until it meets an empty place. The title and cutscene program, laid over `$7E:5000` at every power-on
(`cutscene/cutscene_main.asm:81-122`), leaves `cutscene/opening.asm:1309-1314` in those places.

**Where the port keeps it.** `itemsObtainedAtBattleStart()` in `src/battle/items_obtained.cpp` empties
places 0-12 and lays `BattleStart::itemsObtainedTail` into places 13-15; `writeBack()` hands them back
on `BattleResult::itemsObtainedTail`. `itemsObtainedTailAtPowerOn()` reads the power-on bytes out of
the cartridge's own title program. `tests/test_items_obtained.cpp`
(`ItemsObtained.TheThirteenthItemBringsTheLastThreePlacesIntoTheInventory`,
`ItemsObtained.TheFourteenthToSixteenthWaitUntilTheTwentyNinth`) and `tests/test_battle_end_oracle.cpp`
(`BattleEndOracleTest.TheLastThreePlacesAtPowerOnAreThreeItemsInNumbersPastTheMost`) pin it, against the
cartridge as well.

**If you want to change it.** Emptying all sixteen places in `itemsObtainedAtBattleStart()` ends both.

---

## A full inventory holds back every item obtained after the one that would not fit

**What happens.** With no empty inventory slot, an obtained item the party does not already hold stays
where it is, and every item obtained after it waits behind it, even ones the party holds.

**Where the original does it.** `_c14445` (`btlgfx/btlgfx_main.asm:9448-9494`) returns at a full
inventory (`bcs @44a1`, `:9469-9470`) without moving its count, so every later merge starts at the same
place.

**Where the port keeps it.** `mergeItemsObtained()` in `src/battle/items_obtained.cpp`.
`tests/test_items_obtained.cpp` (`ItemsObtained.AFullInventoryStopsTheMergeAndEverythingBehindItWaits`) and
`tests/oracle/test_battle_end_oracle.cpp` (`BattleEndOracleTest.TheListByHand`) pin it, against the cartridge as
well.

**If you want to change it.** Passing over a place that does not fit, instead of returning, lets the
items behind it in.

---

## A Leap loses the items still on their way to the inventory

**What happens.** When Gau leaps, or a battle ends by teaching its rages, the Morph supply is not saved,
items still in a party member's hand are not put away and obtained items not yet in the inventory are
lost.

**Where the original does it.** `BattleEnd_02` and `BattleEnd_05` (`battle/battle_main.asm:12023-12036`)
learn the rages and go straight to `UpdateSRAM` (`:11961`); every other ending runs `_c24903`
(`:12062-12086`) first.

**Where the port keeps it.** `playBattleEnd()` in `src/battle/battle_end.cpp` runs no wrap-up for
`BattleEndReason::GauLeapt` and `RagesLearned`. `tests/test_battle_end.cpp`
(`BattleEnd.ALeapAndTheRagesLearnedRunNoWrapUp`) pins it.

**If you want to change it.** Calling `wrapUpBattle()` before the rages in those two cases saves the
supply and puts the items away.

---

## Two SwdTechs learned in one win are told of once

**What happens.** Cyan learning two SwdTechs, or Sabin two Blitzes, in one win is told of once: both are
learned, and "Mastered a new technique!" or "Devised a new Blitz!" shows for the first only.

**Where the original does it.** `LearnAbilities` (`battle/battle_main.asm:15967-15992`) shows its message
only when its flag in `$f0` was not yet set (`tsb $f0; bne`), and the win sets `$f0` to 0 only once
(`:15320`).

**Where the port keeps it.** `levelEachMember()` in `src/battle/battle_end.cpp` keeps one flag for the
technique message and one for the Blitz message per win. `tests/test_rewards_oracle.cpp`
(`RewardsOracleTest.TwoTechniquesInOneWinAreToldOfOnce`) pins it against the cartridge.

**If you want to change it.** Telling of every `AbilitiesLearned::swdtech` and `blitz` returned tells of
each.

---

## A wager held in two slots is taken from one of them

**What happens.** A colosseum match takes one of the wagered item from each inventory slot holding it,
last slot first — until one slot runs out. From then on the search looks for empty slots instead, so a
slot before it holding the same item keeps all of its count. Only an inventory holding one item in two
slots shows it.

**Where the original does it.** `UpdateSRAM` (`battle/battle_main.asm:12135-12150`) compares each slot
with the accumulator, and empties a slot that runs out with `lda #$ff`, which replaces the wager in the
accumulator.

**Where the port keeps it.** `writeBack()` in `src/battle/battle_start.cpp`. `tests/test_battle_end.cpp`
(`WriteBack.AWagerHeldInTwoSlotsIsTakenFromTheLastUntilItRunsOut`) and `tests/oracle/test_battle_end_oracle.cpp`
(`BattleEndOracleTest.WriteBackSweep`) pin it, against the cartridge as well.

**If you want to change it.** Keeping the wager as the item looked for takes one from every slot holding it.

---

## A Rage takes the monster's nature over the party member's own

**What happens.** A party member who rages takes on the monster's whole nature: whether it dies at
0 MP, whether its name is hidden, whether it is human, the Imp critical, and whether it is undead.
That replaces what the member's equipment gave — human always, undead with a Relic Ring — so a Relic
Ring wearer raging as a living monster is no longer undead, and one raging as a monster that is not
human is no longer human, until the equipment is next refreshed.

**Where the original does it.** `LoadRageProp` stores the monster's metamorph byte with the
accumulator sixteen bits wide (`battle/battle_main.asm:7518`, the `plp` at `:7548`), so
`lda f:MonsterProp+17,x` / `sta $3c94,y` (`:7546-7547`) lays the record's bytes 17 and 18 into
`$3c94-$3c95`; byte 18 is the nature. `SetRage` runs it for the member raging (`:1013`), over what
`UpdateEquipBattle` left in `$3c95` (`:6779-6782`), and the next refresh writes the whole byte again.
Its readers: `:1828`, `:3027`, `:5838`, `:6980`, `:8305`, `:9115`, `:9161`, `:9914`, `:11107`.

**Where the port keeps it.** `loadMonsterTraits()` in `src/battle/battle_start.cpp` loads the nature
with the rest of what a Rage takes (`loadMonsterNature()`), and `applyDerivedStats()` in
`src/battle/party_setup.cpp` writes all five again. `tests/test_commands.cpp`
(`Commands.ARageTakesTheMonstersWholeNature`,
`Commands.ARelicRingsUndeadIsLostToALivingMonstersRageUntilTheNextRefresh`) pins it.

**If you want to change it.** Leaving `human` and `undead` out of what `loadMonsterTraits()` takes for a
party member keeps the equipment's through a Rage.

---

## A party member's Special attack deals no damage, cannot be dodged and lifts Reflect

**What happens.** A party member that makes the Special attack with no monster and no rage behind it
deals no damage, hits whatever the target's evasion, takes Reflect off a target that has it, and is
named for no monster. The one character that does it in the shipped game is Kefka at the Sealed Gate:
the scripted battle there (`CharAiId::SEALED_GATE_1`) seats him as an enemy
character running the script of the monster Kefka (Esper Gate), whose turns choose Special among their
attacks. No other scripted battle's cast runs a script that names Special.

**Where the original does it.** The battle's start fills `$2000-$341f` with `$ff`
(`battle/battle_main.asm:6061-6067`), the special attack at `$322d,x` and the monster it is named for
at `$33a8-$33a9,x` among them. A monster's record (`LoadRageProp :7506-7507`), a Rage (`SetRage :1013`)
and a caught Sketch (`Cmd_0d :3312-3313`) write the special attack; a party member seated by a scripted
battle has none of them. `_c2299f` names the attack `$ef` as a special attack for any battler that is
not an Imp (`:6927-6934`), and `CalcAttackEffect` reads `$ff` as no damage, cannot be dodged and Reflect
removed (`:8195-8224`). `ShowAttackName_06` names it for `$ffff` (`:8742-8748`).

**Where the port keeps it.** `kNoMonsterSpecialAttack` in `src/battle/battle_state.h` is every battler's
`specialAttack` until one of those writes it, and the attack frame's name command holds no monster
(`MonsterSpecialAttackNameShown::monster`, `src/battle/attack.cpp`). `tests/test_attack_frame.cpp`
(`AttackFrame.ASpecialWithNoMonsterDealsNoDamageAndCannotBeDodged`,
`AttackFrame.ASpecialWithNoMonsterLiftsReflect`, `AttackFrame.ASpecialWithNoMonsterIsNamedForNone`) pins
it, and the whole battles against the cartridge compare `$322d,x` and `$33a8-$33a9,x` at every step.

**If you want to change it.** Giving `Battler::specialAttack` another starting value in
`src/battle/battle_state.h` changes what such a party member's Special does.

---

## Umaro's tackle and throw under a Rage are made with Fire

**What happens.** When Umaro rages, his rage's Battle or Special attack sends him to his own attacks,
and a tackle or a throw he makes then is made with the spell Fire's record: a magic hit,
fire-elemental, still undodgeable and ignoring defense, with the power of his hands, his level and his
vigor behind it. The target is recorded as last hit by Fire, which an enemy's counterattack script can
read. A tackle or throw under a Fight is the plain hit, as it should be. Umaro's own commands hold no
Rage, so the shipped game reaches this only where a battle gives him one.

**Where the original does it.** `InitUmaroAttack` (`battle/battle_main.asm:3829-3841`) aims his attack
with `_c2298a` (`:6892-6909`), which hands the command as chosen to `InitTarget` with the attack in B
(`:6479`). The roll that picked the tackle or the throw left B at 0 (`RandBitWithRate :13525-13545`
shifts the offered bits out of it; `RandA :12390-12404` gives it back unchanged). A command that uses no
spell loads the Battle attack whatever B holds (`InitTarget_00 :6548-6550`); Rage is one that uses a
spell (`:6538-6539`), so `InitTarget_02` (`:6540-6542`) loads attack 0, Fire, and writes it into
`$3410` as the last spell, which `_c235e3` (`:8585-8594`) records on the target.

**Where the port keeps it.** `kUmaroAttackRecord` in `src/battle/commands.cpp` is the attack Umaro's
attacks are aimed with, passed to `plainCommandAttack()`. `tests/test_commands.cpp`
(`Commands.UmarosTackleUnderARageIsMadeWithFiresRecord`,
`Commands.UmarosThrowUnderARageIsMadeWithFiresRecord`,
`Commands.UmarosTackleUnderAFightKeepsTheBattleRecord`) pins it, and the whole battles against the
cartridge compare the record and the target's last hit at every step.

**If you want to change it.** Passing `attack.attack` instead of `kUmaroAttackRecord` in
`loadUmaroAttack()` makes a raging Umaro's tackle and throw the rage's own Battle or Special attack.

---

## A morph that runs out while a battle's end plays leaves the Morph supply at nothing

**What happens.** A battle that ends with a party member in Morph keeps the Morph supply as the meter
stands — but the meter keeps running down through the frames the end plays, and if it runs out in them
the supply is saved again by a meter of nothing: it drops to 0. In a win the magic points Terra earns are
added to the supply partway through, so a meter running out after them loses those too. The longer the
end — the wrap-up's wait, then every message of a win — the more of the supply is at risk.

**Where the original does it.** The wrap-up saves the supply first (`_c24903`,
`battle/battle_main.asm:12064`, `SaveMorphCounter :1762-1771`), then waits 32 frames with battle time
running (`:12076-12080`; `BtlGfx_01`, `btlgfx/btlgfx_main.asm:392-409`, calls `UpdateBattleTime`).
`DecMorphCounter` (`:2815-2829`) runs on every second frame of those and, when the meter runs out,
empties it and calls `SaveMorphCounter` again, which multiplies the supply by the emptied meter.
Nothing stops the morph before the end begins.

**Where the port keeps it.** `BattleSim::tick()` in `src/battle/battle_sim.cpp` plays the end a part at a
time through `continueBattleEnd()` (`src/battle/battle_end.h`), with `tickBattleTime()` running on every
frame between the parts, so `runDownMorph()` (`src/battle/morph.cpp`) saves the supply when the meter runs
out there. `tests/test_battle_sim.cpp` (`BattleSim.AMorphRunningOutInTheWrapUpsWaitLeavesTheSupplyAtNothing`,
`BattleSim.AMorphRunningOutAfterTheMagicPointsAreAddedLeavesTheSupplyAtNothing`) pins it, and
`tests/oracle/test_battle_whole_oracle.cpp`
(`BattleWholeOracleTest.AMorphRunningOutInTheWrapUpsWaitLeavesTheSupplyAtNothing`) pins it against the
cartridge.

**If you want to change it.** Ending the morph in the wrap-up's first half (`closeTheMenus()` in
`src/battle/battle_end.cpp`), after the supply is saved, keeps the saved supply whatever the end's frames
do.

---

## A party member put back in Imp at a later stage of the final battle keeps their equipment unrefreshed

**What happens.** Each stage of the final battle after the first is meant to start by working out every
seated party member's equipment again. A member still in Imp when the stage starts is not refreshed at
the stage's first step, and neither is the member in the next party slot. Two members in Imp side by
side in the third and fourth slots leave the third unrefreshed and the fourth refreshed. A member in Imp
in the fourth slot also moves the windows' wallpaper byte on by one, which the stage's own start then
lays again from the configuration, so nothing on screen changes.

**Where the original does it.** `RestartBattle` runs `InitBattle` again (`battle/battle_main.asm:63-64`),
whose fill leaves `$FF` in the four equipment-change flags at `$2F30-$2F33` (`:6061-6067`); only the
opening's graphics init clears them (`btlgfx/btlgfx_main.asm:2488-2490`), and a later stage's
(`_c10076`, `:155-174`) does not. The stage puts each member's statuses on again from their record
(`LoadCharProp :6640-6641`, `UpdateStatus :6128`), and Imp's handler falls into Rage's coming-off
(`SetStatus_05`, `RemoveStatus_18`, `:11413-11432`), whose `inc $2f30,x` runs with the sixteen-bit
accumulator `UpdateStatus` set (`:11034-11035`): it adds one to the flag and the byte above it, so a
flag at `$FF` goes to 0 and carries one into the next slot's `$FF`, which goes to 0 too. The first
loop step's `UpdateCharProp` (`:5597-5631`) refreshes only the slots whose flag is not zero, from the
fourth slot down. A carry out of the fourth slot lands in `$2F34`, which `_c10fe0`
(`btlgfx/btlgfx_main.asm:2193-2195`) lays from the configuration before anything reads it.

**Where the port keeps it.** `statusChangeAsksForEquipmentRefresh()` in `src/battle/party_setup.cpp` adds
one to `BattleState::equipmentChangeFlags` as a sixteen-bit number, and `assembleBattle()` in
`src/battle/battle_start.cpp` starts a later stage's flags at `kEquipmentChangeFlagFilled`.
`tests/test_battle_start.cpp` (`StartFinalBattle.TheNextStageStartsEveryEquipmentFlagAtTheFill`,
`StartFinalBattle.AnImpAtTheNextStageZeroesItsFlagAndTheNextSlots`,
`StartFinalBattle.TwoImpsSideBySideAtTheNextStageLeaveTheHigherSlotsFlagAtOne`,
`StartFinalBattle.TheFourthSlotsCarryGoesIntoTheWallpaperTheStageThenLaysAgain`) pins it, and the whole
battles against the cartridge compare the four flags and the wallpaper byte at every step.

**If you want to change it.** Filling a later stage's flags with `kEquipmentChangeFlagFilled` after the
stage's statuses are put on, instead of before, in `assembleBattle()`, refreshes every seated member at
the stage's first step.

---

## An empty party slot at a later stage of the final battle is refreshed from the field program's stack

**What happens.** At each stage of the final battle after the first, a party slot nobody fills is given an
equipment refresh as if someone were in it. The refresh builds that nobody's stats and equipment out of
the thirty-seven bytes below the first character record. Those bytes are the top of the field program's
stack, where the field's call into the battle and the battle's own calls keep their return addresses and
saved flags. The shield and helmet are item 0. The armor is the item the field's saved processor flags
number, and the relics are items 21 and 1. Magic power, stamina, speed and vigor are bytes of two return
addresses and the battle's saved flags, and the first status byte is a return address's low byte. The
actor is a byte a deeper call left, and the weapon is a byte of bank `$7F`.

Almost all of it lands in cells nothing reads: the empty slot's own stats, immunities and command rows,
and two bytes of bank `$7F`. The slot's menu is drawn again. When the empty slot is the last slot the
refresh takes — the slots below it seated by nobody, or by members whose flags an Imp's carry zeroed —
its sums also stand as the last attack's level and physical flag. The level is item 0's hit rate, 180,
and the physical flag is the low bit of the stamina sum: `$61` plus the six items' stamina modifiers.
Both stand until the next refresh or the next action writes them, and nothing reads either before then.

**Where the original does it.** `UpdateCharProp` (`battle/battle_main.asm:5597-5631`) refreshes every slot
whose equipment-change flag is not zero, the fourth first, and at a later stage an empty slot's flag is the
start's `$FF` (`:6061-6067`). It reads the slot's record offset, the fill's `$FFFF` (`:5615`), so the hands
and status it stores land in bank `$7F` (`:5616-5620`). `UpdateEquip` masks the slot's `$FF` and pulls the
unmasked byte back (`:2295-2300`); one more, times a record's thirty-seven bytes (`:2305-2310`), is the
record at `$15DB-$15FF`. It clears `$11A0-$11DE` (`:2301-2304`) and sums into it the actor at `$15DB`
(`:2311`), the stats at `$15F5-$15F8` (`:2334-2339`), the status at `$15EF` (`:2340`), and the equipment at
`$15FB-$15FF` and, with the index wrapped to `$FFFF`, `$7F:15FA` (`:2342-2348`). Absolute indexed
addressing carries the index into the bank, so its two boost writes land at `$7F:15EB` and `$7F:15E7`
(`:2349-2362`). The shield hand's hit rate is the last attack's level at `$11AF` (`CalcEquipEffect`
`:2640-2641`), and the stamina sum sits at `$11A2`. `StartCondemn` reads the level (`:1513-1516`) and
`CheckRetal` the flag's bit 0 (`:12567-12569`), each after an action, which writes the level first
(`:6999`, `:7297`). `UpdateEquipBattle` (`:6733-6834`), `FixImmuneStatus` and `UpdateImmuneStatus`
(`:6359-6433`) and `UpdateCmdList` (`:13560-13617`) then write the slot's cells, its command rows and the
party's relic byte `$3A6D` from the sums, and mark its menu to be drawn again (`:13568-13569`).

The stack is the field's. `Reset` sets the stack pointer to `$15FF` (`field/reset.asm:17-18`), and the
field calls `ExecBattle` from its loop at that depth (`:124-128`), pushing the return `$0115`: relic 2 and
relic 1. `ExecBattle` saves the flags, data bank 0 and direct page 0 (`field/battle.asm:77-79`): the armor,
the helmet and the shield. It then calls `Battle_ext` (`:80`), pushing the return `$C0:C161`, which holds
magic power `$C1` and stamina `$61`. `BattleMain` saves the flags, which hold the speed
(`battle_main.asm:55-61`). The loop calls `UpdateCharProp` (`:74`), whose return's high byte is the vigor,
0. `UpdateCharProp` saves its two index bytes and calls `UpdateEquip` (`:5601-5602`, `:5623`), pushing the
return `$C2:20CE`, whose low byte `$CE` is the first status byte. The weapon place, `$7F:15FA`, holds 0 on
a machine parked at the reset code.

**Where the port keeps it.** `applyEquipmentChanges()` in `src/battle/party_setup.cpp` zeroes an empty
slot's flag and marks its menu to be drawn again (`BattleState::menusToRefresh`), and runs none of the
sums: the bytes are the processor's call frames, which the port has no counterpart for. Where the empty
slot is refreshed last, `BattleState::lastAttackLevel` and `lastAttackPhysical` stay as the refresh before
it left them, where the game holds 180 and the stamina sum's low bit, until the next refresh or action
writes them. `tests/test_battle_start.cpp`
(`StartFinalBattle.AnEmptySlotAtTheNextStageHasItsMenuDrawnAgainAtTheFirstStep`) pins the menu, and
`tests/oracle/test_battle_whole_oracle.cpp`
(`BattleWholeOracleTest.WhatAnEmptySlotsRefreshWritesReachesNothing`,
`BattleWholeOracleTest.TheCommandRowsAnEmptySlotsRefreshWritesReachNothing`,
`BattleWholeOracleTest.TheHandsAndRowsAnEmptySlotsRefreshReadsReachNothing`,
`BattleWholeOracleTest.TheBoostsAnEmptySlotsRefreshWritesInBank7FReachNothing`,
`BattleWholeOracleTest.TheBlockAnEmptySlotsRefreshLeavesReachesNothing`) shows against the cartridge that
what the sums write reaches nothing the battle reads, the block at `$11A0-$11DE` included. Those tests run
the battle program on the power-on stack page, where the thirty-seven bytes are zero, so there the sums
are item 0's in every place.

**If you want to change it.** Passing over a slot with no one in it before its flag is read, in
`applyEquipmentChanges()`, leaves an empty slot's flag set and draws its menu no more than any other
step does.

---

## The Chain Saw takes its kill roll from past the random number table

**What happens.** The Chain Saw is meant to kill outright one time in four: on a random number whose low
two bits are clear. It takes that number from 256 × N bytes past the start of the random number table,
where N is the byte the field's dialog keeps its text's x position in. That byte is 0 when the console is
switched on. Each dialog, each new line and each new page set it to 4, and each letter adds its width. A
map's title sets it to the title's centering. So a battle the field starts after any dialog has been shown rolls
the Chain Saw from 256 other bytes of the cartridge: at 4, the battle graphics program's code from
`$C1:0100`. The chance of a kill is whatever share of those bytes has its low two bits clear. A battle
the world program starts — an encounter on the world map, a battle on a vehicle, one on the Magitek
train ride — runs on the world program's own direct page, which that program clears each time it starts,
so there the roll comes from the table. In the shipped game the byte holds 4 as the first fight in
Narshe starts, and 4 again as the battle after it starts.

**Where the original does it.** `Rand` (`battle/battle_main.asm:12378-12384`) steps the position at `$BE`
with `inc $be` and loads it with `ldx $be`, and sets no register width. `CalcItemEffect` widens the index
registers (`:7015`) and calls the tool's effect before it narrows them (`jsr (ToolsEffectTbl,x)` `:7089`,
the `plp` at `:7091`). The Chain Saw's effect is `jsr Rand` (`ToolsEffect_03` `:7127`), so there
`ldx $be` loads `$BE` and `$BF` together and `lda f:RNGTbl,x` reads `RNGTbl + $BF × 256 + $BE`. The
position still steps as a byte. Nothing in the battle program writes `$BF`. The field writes it as its
dialog's x position (`InitDlgText`, `field/text.asm:54-55`; `ShowMapTitle` `:135-158`; `DrawDlgText`
`:955-958`; `NewLine` `:973-974`; `NewPage` `:1005-1006`), and `ExecBattle` calls the battle on the field's
direct page (`field/battle.asm:69-80`). The world program clears its direct page from `$19` up
(`InitHWRegs`, `world/init.asm:1403-1407`) and calls the battle on it from three places: an encounter
(`world/move.asm:882-908`), a vehicle's battle (`world/world_start.asm:156-164`) and the Magitek train
ride's (`:569-577`).

**Where the port keeps it.** `addToolEffect()` in `src/battle/item_attack.cpp` rolls the Chain Saw with
`BattleRng::nextFrom(state.chainSawRollEntries)` (`src/battle/battle_rng.h`), which steps the position as
`next()` does and reads the entries it is given. `BattleStart::chainSawRollEntries` hands the battle its
entries, and a later stage of the final battle keeps them. They are the cartridge's 256 bytes at the
table's place plus 256 × the byte, read with `assets::HiRomImage::readWithIndexHighByte()`
(`src/assets/rom_reader.h`); left as they are, they are the table's own (`kRngTableEntries`), as a battle
the world program starts has them. `tests/test_item_attack.cpp`
(`ItemAttack.TheChainSawRollsFromTheBattlesChainSawEntries`), `tests/test_battle_state.cpp`
(`BattleRng.NextFromReadsTheEntriesItIsGivenAtTheNextPosition`), `tests/test_battle_start.cpp`
(`StartBattle.TheChainSawRollsFromTheEntriesTheBattleStartsWith`,
`StartFinalBattle.TheNextStageKeepsTheChainSawsEntries`) and `tests/test_cartridge.cpp`
(`HiRomImage.TheRngTableReadWithAHighByteOfZeroIsTheTable`) pin it, and
`tests/oracle/test_battle_whole_oracle.cpp`
(`BattleWholeOracleTest.AChainSawsRollWithTheByteAboveThePositionAtOneReadsPastTheTable`) pins it against
the cartridge.

**If you want to change it.** Rolling with `state.rng.next()` in `addToolEffect()` takes the Chain Saw's
number from the table in every battle, one kill in four.
