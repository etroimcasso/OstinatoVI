# Field dialog text engine — behavioral contract

Behavioral specification for the field dialog text engine: the per-frame state machine
that types a message one glyph at a time, its control codes, the English word-wrap law, the
name / item / spell / gold splices, the multiple-choice cursor, the page wipe, and the map
title. All citations point into the pinned reference tree `original-src/`
(everything8215/ff6 @ `1ea47b5`); line numbers are into `src/field/text.asm` unless another
file is named. This document is the test authority for `tests/test_dialog.cpp`.

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| Reset at map load | `InitDlgText` `:30-70` | `initDialog()` |
| Start a message | `GetDlgPtr` `:76-94`; event commands `$48` / `$4b` (`field/event.asm:1699-1784`) | `startDialog()` |
| The per-frame step | `UpdateDlgText` `:326-436`, called once per field frame (`field/reset.asm:131`) | `tickDialog()` |
| One glyph or control | `UpdateDlgTextOneLine` `:438-878` | (inside `tickDialog()`) |
| Word width | `CalcTextWidth` `:189-318` (English only) | (inside `tickDialog()`) |
| Margin check per glyph | `DrawDlgText` `:926-960` | (inside `tickDialog()`) |
| Line / page end | `NewLine` `:966-992`, `NewPage` `:998-1018` | (inside `tickDialog()`) |
| Map title | `ShowMapTitle` `:100-179`; `CloseMapTitleWindow` (`field/window.asm:160-195`) | `showMapTitle()`, `closeMapTitle()` |
| Page wipe | `ClearDlgTextRegion` `:1050-1083` | `DialogState::pageClearFrames`, `halfLinesCleared()`, `isWiped()` |
| Choice cursor | `UpdateDlgText` `:368-413`, `DrawDlgCursor` `:1161-1197`, `TfrDlgCursorGfx` `:1203-1244` | `DialogChoices` |
| Font widths | `FontWidth` (`gfx/font_gfx.asm:14-36`) | `FontMetrics` |

## Timing

One call of `tickDialog()` is one field frame. Nothing about dialog advances anywhere
else. Every cost below is in those frames.

## The per-frame order

Each frame runs these steps in order; the first that returns ends the frame
(`:326-436`).

1. **Title timer.** While the map title is showing, its timer counts down; reaching zero
   closes the title (see *Map title*).
2. **Idle.** With no message showing and none just finished, nothing else happens.
3. **Pause.** A pending pause (`WAIT`, `WAIT_FRAMES`) counts down one frame and returns.
4. **Key timer.** An armed key timer counts down one frame and falls through. When its count
   is already zero, it disarms instead, sets the key state to typing and clears the choice
   count, then falls through (`:350-365`).
5. **Key state.** If the engine is waiting on the player:
   - with two or more choices, the choice cursor moves (see *Choices*);
   - waiting for **release**: while A is held, return; otherwise switch to waiting for a
     press and return;
   - waiting for a **press**: while A is not held, return; otherwise switch to typing, clear
     the choice count, disarm the key timer, and return (`:414-426`).

   A is read as a **held** level, not an edge. A press that is still held when a page ends
   does not advance it: the release must be seen first.
6. **Page wipe.** While a dismissed page is still going away, the wipe advances one frame and
   the frame returns (`:428-431`).
7. **Completion.** A finished message requests the dialog window to close, returns to
   idle, and the frame returns (`:432-436`). The request is read once.
8. **One step.** One glyph or one control is consumed (below).

## One step

**English** (`:440-477`). The width of the word at the cursor is measured first (see *Word
wrap*). If the pen position plus that width reaches the right margin, the line breaks and
the step ends — nothing is drawn and the cursor does not move. Otherwise, a queued splice
glyph is drawn if one is waiting; if not, the next glyph or control at the cursor is
consumed.

**Japanese** (`:479-528`). The same, without the width measurement. Japanese text never
word-wraps; only the per-glyph margin check applies.

| Control | Effect | Frames |
|---|---|---|
| `END` | the page ends (as `PAGE`) and the message is finished; the cursor stays | 1 |
| `NEWLINE` | the line breaks | 1 |
| `CHARACTER_NAME n` | queue character *n*'s name; its first glyph draws this frame | 1 per glyph |
| `WAIT` | pause 60 frames | 1 + 60 |
| `WAIT_FRAMES n` | pause 15·*n* frames | 1 + 15*n* |
| `KEY` | arm the key timer at 1 frame; **does not wait** — typing continues, and two frames later the timer disarms, resetting the key state and the choice count | 1 |
| `PAGE` | the page ends | 1 |
| `TAB n` | queue *n* fill glyphs (`$7F` English, `$FF` Japanese); none draws this frame | 1 + *n* |
| `CHOICE` | record the current 16-pixel column as the next choice, then draw the 6-pixel wide space `$FF` as an ordinary glyph | 1 |
| `KEY_TIMED n` | arm the key timer at 15·*n* frames and wait for a press; the timer running out advances without one | 1 + wait |
| `GP` | queue the gold digits; the first draws this frame | 1 per glyph |
| `ITEM_NAME` | queue the item's name; the first draws this frame | 1 per glyph |
| `SPELL_NAME` | queue the spell's name; the first draws this frame | 1 per glyph |

A compressed pair costs two frames, one per glyph. A line break forced by the word wrap
costs one frame and draws nothing.

When the last queued glyph of a splice is drawn, the cursor moves past the control that
queued it. A `TAB` therefore holds the cursor on itself while its fill glyphs drain.

**A splice that produces no glyphs** — a name whose first byte is the end marker, a gold
amount of zero, a `TAB` count of zero — draws nothing in the port; the frame is spent and the
message goes on. The original mishandles this case: it reads its buffer's end marker as a
glyph index and draws from 704 bytes before the font, which is program code, not a glyph
(`:450-465`, `:1350`). Nothing in the shipped game reaches it: every gold chest holds at
least 500 (`field/player.asm:800-824`) and the amount is the one just found, and the naming
screen refuses a name whose six bytes are all blank (`menu/name_change.asm:127-136`).

### Records that run on

The engine reads a message from its record's first byte until it meets `END`; the record
boundary does not stop it. Three records are empty — their pointer equals the next record's
— and so show the following record's text, and one record has no terminator and continues
into the next. `startDialog()` reads the same way.

## Word wrap (English only)

Measured every frame before anything is drawn, starting at the **cursor**:

- With a splice draining, the cursor sits on the control that queued it, and the width is
  0. (For `TAB` the original measures from the count byte, which reads as a control for every
  count below 32 and so gives 0 too. Every shipped count is between 5 and 22; the port
  measures every draining tab as 0.)
- Otherwise the width sums `FontWidth` over the glyphs from the next undrawn one onwards,
  across following runs, and stops at the space glyph `$7F` (not added) or at any control.
  `CHARACTER_NAME` and `ITEM_NAME` add their name's width before stopping (up to 6 and 12
  glyphs, ending at the name's end marker); every other control adds nothing. **Gold
  amounts, spell names and tab fill are never measured.**
- A compressed pair whose first glyph has already drawn contributes only its second glyph.

If `x + width ≥ 224` the line breaks instead of drawing. The measure stops after a name, so
letters glued to it (`'s`) are measured separately when the cursor reaches them.

The original keeps the width in one byte. The widest word in the shipped English script is
170 pixels, so no measurement ever exceeds it, and the port measures in plain integers.

## The margin

A glyph is drawn only if `x + FontWidth[glyph] < 224` (`:926-934`). Otherwise the line breaks
**and the glyph is dropped**: both callers move past it regardless (`:459-460`, `:827-828`).
In English the word wrap keeps whole words off the margin, so what reaches it is the space
after a word that ends near it — dropped 305 times across the shipped script, each time
invisibly — and could otherwise only be unmeasured text: tab fill, the choice marker's space,
a gold amount or a spell name at the end of a line. With everyone's default name, no visible
glyph in the English script is ever lost. Japanese has no word wrap. See `docs/Bugs.md`.

## Lines, pages and the wipe

- The pen starts at **x = 4** (`:54-55`). The right margin is **224**.
- A page holds **4 lines**. A line break resets x to 4 and steps the line. Breaking past
  the fourth line ends the page (`NewLine` `:985-990`).
- A page end sets the key state to *waiting for release* and arms a **9-frame wipe**
  (`NewLine` `:987-990`, `NewPage` `:1013-1016`). `PAGE` and `END` also reset the line to 0.
- Once the player has released and pressed A, the wipe counts 9 → 0, one per frame. The
  window stays open, and the page's text leaves half a line at a time over the eight frames
  from 8 to 1: line 0's left half (pen x below 112), then its right half, then line 1's left,
  and so on to line 3's right (`:1051-1082`). Whatever is on a half-line — glyphs and the
  choice cursor alike — is no longer shown once the wipe has passed it. The next page types
  from line 0 on the frame after the count reaches 0.

## Choices

`CHOICE` records where its cursor arrow will go: the current line, and the left edge of the
16-pixel column the pen is in (`:682-694`). The arrow snaps to the column, never to the
marker's own pixel. The shipped script records at most 6 in one message.

While the engine waits on the player and two or more choices exist, the cursor moves
(`:368-413`):

- With no direction held, the selection is not changing; the cursor is drawn at the
  selected choice.
- With a direction held while the selection is already changing, nothing happens — one
  move per press.
- Otherwise up or left steps back one choice, unless already on the first; down or right
  steps forward one, unless already on the last. Up/left is tested before down/right. A
  step that moves sets the selection to changing. A blocked step does not, so the next
  frame tries again. The cursor is drawn at the (possibly new) selection.

The cursor is a right-pointing arrow at the selected choice's position. Exactly one is ever
shown. After A is pressed it stops following the selection, but it stays on screen until the
wipe reaches its half-line. The selection persists after the choice count is cleared, until
the event that reads it clears it (`field/event.asm:4784-4803`).

## Key timer

`KEY_TIMED n` arms the timer at 15·*n* frames and waits for a press. Each frame the timer
counts down one (step 4); at zero it disarms, returns the key state to typing and clears the
choice count — the message continues without a press. A press first disarms it.

`KEY` arms the timer at 1 frame without waiting, so it only resets the key state and the
choice count two frames later. The shipped script has three records where a `KEY` follows
choices in the same message.

## Splices

| Splice | Source | English | Japanese |
|---|---|---|---|
| Character name | character *n*'s name, menu encoding, ≤ 6 bytes (`:560-584`) | each byte − `$60`; stops at `$FF` | raw bytes, as two-byte glyphs (`:588-601`) |
| Item name | the item's name record, **skipping byte 0** (the icon), ≤ 12 (`:742-776`) | − `$60`; stops at `$FF` | raw; stops at `$FF` |
| Spell name | the spell-name table at **4 × index + 1**, 4 bytes (`:779-812`) | − `$60`; stops at `$FF` | raw; stops at `$FF` |
| Gold | the decimal digits of the amount found, 7 places, leading zeros suppressed (`:714-739`) | digit + `$54` | digit + `$53` |

The spell splice uses a stride of 4 and copies 4 bytes; the upstream comment says both
should be 7 (`:783`, `:804`). It is unreachable: nothing writes the spell index it reads,
and the one message that uses it is never shown (`field/player.asm:824-853`). The port keeps
the stride and count as written.

The gold amount is the amount just found, not the party's total (`field/player.asm:800-824`).

## Map title

`showMapTitle()` reproduces `ShowMapTitle` once the caller has decided a title is shown
(`:100-121` — the map-change flag is set and party switching is off):

- The title timer is set to **100 frames**.
- **Centering.** English: width = the sum of `FontWidth` over the record's bytes up to its
  terminator (`:123-137`). Japanese: 12 × the glyph count, where a byte below `$20` takes
  the byte after it into the same glyph (`:139-156`). `x = (224 − width) / 2`.
- The whole title is typed **at once**, onto line 0 (`:171-174`), through the same step as
  dialog, until its `END`. The key state and the wipe count are then cleared, so the title
  stays on screen.
- The title's `END` does **not** request a window close. The original leaves the finished
  flag set and absorbs it the next frame with no observable effect (`UpdateDlgWindow`).

`closeMapTitle()` reproduces `CloseMapTitleWindow`. It does nothing unless the title is
showing. Otherwise it clears the timer, sets the message state to idle and arms the 9-frame
wipe. The wipe does not run while the engine is idle (step 2 precedes step 6). The title's
glyphs therefore stay in the text area, and the next message starts typing **9 frames
late**, after the wipe clears line 0.

## Language variance

| | English | Japanese |
|---|---|---|
| Word wrap | measured every frame | none |
| Glyphs | `$20-$FF`, one byte | `$20-$4FF`; a byte below `$20` and the byte after it form one glyph |
| Tab fill | `$7F` | `$FF` |
| Digit base | `$54` | `$53` |
| Title centering | sum of widths | 12 × glyph count |

Everything else is identical. The Japanese branches are built and their tests are registered.
They skip until a Japanese cartridge is available.

## The picture (what the components draw)

Recorded here as the contract the rendering components are built against.

- The text area's pixel x is the pen's x. Below a line's top, rows 0-3 are blank, a glyph's
  11 rows are rows 4-14, and its shadow is rows 5-15 (`CopyDlgTextToBuf` `:1125-1155`). How
  far apart the lines are on screen, and where the text area sits, is the dialog window's: 15
  pixels once its box is open, 16 while it opens (`docs/contracts/dialog_window.md` § Where the
  text sits).
- A glyph's artwork is a 16-pixel-wide cell whose ink begins 4 pixels in. The cell of a glyph
  at pen x starts at pixel **x − 4** (`LoadLetterGfx` `:1380-1453`), so the pen is the ink's
  left edge.
- The shadow is the glyph itself one pixel right and one row down, in black; the letter wins
  wherever the two overlap, across neighboring glyphs too.
- The choice cursor is a 6-pixel-wide, 11-row arrow at the left of its column, in rows 4-14,
  with a one-pixel shadow to its right only (`DrawDlgCursor` `:1161-1197`).

## Not in this engine

The dialog window itself, its open/close animation and the map title's window
(`field/window.asm`; `docs/contracts/dialog_window.md`); the event commands that start messages, wait on them and read the
selection; the battle and menu text renderers; the debug menu; the font color setting.
