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

## Each target hit in the back raises the damage for every target after it

**What happens.** A blow that hits several targets in the back adds one damage
step for each of them, and keeps the steps: the first target it reaches gets
one step, the second two, and so on, whether or not the later targets were hit
in the back themselves.

**Where the original does it.** `CalcAttackEffect`
(`battle/battle_main.asm:8344-8351`) adds to the damage multiplier `$bc` inside
the loop over the targets hit and never takes the step off again before the
next target.

**Where the port keeps it.** `resolveAttack()` in `src/battle/attack.cpp` adds
to `AttackInFlight::multiplier` in the same loop. `tests/test_attack_frame.cpp`
(`AttackFrame.EachTargetHitInTheBackAddsAStepForTheRest`) pins the steps.

**If you want to change it.** Counting the step for its own target only would
lower the damage of spread blows in side and pincer attacks.

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
