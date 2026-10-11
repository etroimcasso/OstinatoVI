# Bugs in the original game

Defects that exist in the original Final Fantasy VI and are reproduced here on
purpose. The port matches the shipped game's behaviour, including where that
behaviour is wrong, so anything listed on this page is deliberate and must not
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

| Function | Behaviour |
|---|---|
| `worldTileProperties(world, tile)` | The correct lookup — the world's own table. |
| `worldOfBalanceTileProperties(tile)` | The direct read, always the World of Balance table. |

Movement code ported from `MovePlayer` calls the second. Everything else calls
the first. `tests/test_world_tiles.cpp` pins both, and asserts that the two
tables really do differ, so the distinction cannot quietly collapse.

**If you want to change it.** Making movement use `worldTileProperties()` would
alter World of Ruin walking behaviour, so it is a gameplay change rather than a
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

The gap looks like an oversight rather than a judgement about what a control pad
can do: masks 3 and 7 both hold left *and* right together, and both are present
in the table, carrying a bearing of zero. Masks 11 and 15 hold the same opposed
pair and are simply absent.

Whether it can be reached depends on the controller. A stock pad pivots, so up
with down and left with right cannot be held at once; a pad without that pivot,
or an input device that reports directions independently, can produce all
sixteen.

**Where the port keeps it.** `src/data/world_cutscenes.h` exposes the table as it
stands and reports the shortfall rather than papering over it:

| Function | Behaviour |
|---|---|
| `strafeAngle(mask)` | The bearing, or nothing when the mask falls past the table. |
| `strafeMaskReadsPastTheTable(mask)` | Whether a mask is one of the five with no entry. |

No bearing is invented for the uncovered masks — the original does not produce
one either, it produces whatever those bytes happen to be. Callers decide.
`tests/test_world_cutscenes.cpp` walks all sixteen masks and pins which eleven
are covered.

**If you want to change it.** Extending the table to sixteen entries means
choosing bearings for direction combinations the game never assigned any, which
is new behaviour rather than a repair. Refusing to strafe on an uncovered mask is
the smaller change and is what falling through `strafeAngle()` already gives you.

---

## Dialogue drops a glyph that would cross the right margin

**What happens.** When the next glyph of a dialogue message would reach the text
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

**Where the port keeps it.** `tickDialogue()` (`src/text/dialogue.cpp`) breaks
the line and drops the glyph exactly as the original does, and records each one
in `DialogueState::droppedAtMargin` with where it would have gone.
`tests/test_dialogue.cpp` pins the behaviour on a hand-built message and the
305 / 0 count over the whole English script.

**If you want to change it.** Carrying the glyph to the start of the new line is
the obvious repair. On the English script it changes nothing visible, since only
spaces are affected; on Japanese text, or on English text with a renamed
character long enough to reach the margin, it changes where words break.
