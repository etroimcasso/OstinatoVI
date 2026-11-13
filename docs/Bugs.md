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
`src/battle/attacker_effects.cpp`, read by `spiraler()`. `tests/test_attack_oracle.cpp`
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
`tests/test_battle_end_oracle.cpp` (`BattleEndOracleTest.TheListByHand`) pin it, against the cartridge as
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
(`WriteBack.AWagerHeldInTwoSlotsIsTakenFromTheLastUntilItRunsOut`) and `tests/test_battle_end_oracle.cpp`
(`BattleEndOracleTest.WriteBackSweep`) pin it, against the cartridge as well.

**If you want to change it.** Keeping the wager as the item looked for takes one from every slot holding it.
