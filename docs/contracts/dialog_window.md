# Field dialog window — behavioral contract

Behavioral specification for the field dialog window: the box the dialog text types into, its
five-frame opening and closing, the text-only form that shows no box, the map-title box, where the
text sits inside the box, and what the box looks like. All citations point into the pinned
reference tree `original-src/` (everything8215/ff6 @ `1ea47b5`); line numbers are into
`src/field/window.asm` unless another file is named. This document is the test authority for
`tests/test_dialog_window.cpp`.

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| Reset at map load | `field/init.asm:432-433`, `InitDlgWindow` `:526-594` | `initDialogWindow()` |
| Open a message's window | event commands `$48` / `$4b` (`field/event.asm:1699-1719`, `:1755-1784`) | `openDialogWindow()` |
| Wait for it to close | event command `$49` (`field/event.asm:1725-1730`) | `isClosed()` |
| Close it | the text engine at a message's end (`field/text.asm:432-436`) | `closeDialogWindow()` |
| The per-frame step | `UpdateDlgWindow` `:238-439`, called once per field frame (`field/reset.asm:132`) | `tickDialogWindow()` |
| Rows added and removed per frame | `DlgOpenMaskTbl` `:442-447` | `rowsShown()` |
| Map-title box | `OpenMapTitleWindow` `:65-154`, `CloseMapTitleWindow` `:160-195` | `openMapTitleWindow()`, `closeMapTitleWindow()` |
| Where the text sits | the text's vertical offsets (`field/hdma.asm:885-906`, `:947-963`) and pointers (`:458-463`) | `linePitch()`, the text-area constants |
| The box's tiles | `DlgWindowTilesTop/Mid/Btm` `:626-648`, `InitDlgWindow` `:543-565` | (the box component) |
| Art and colors | `TfrWindowGfx` `:24-58`, `InitDlgWindow` `:527-541` | (the box component) |

## Timing

One call of `tickDialogWindow()` is one field frame. Each frame the dialog text advances first and
the window second (`field/reset.asm:131-132`); the event that opens a window runs earlier in the
same frame (`field/reset.asm:99`). So the frame a message starts is also the frame its window takes
its first opening step and its first glyph is typed.

## States

The window is **closed**, **opening**, **open** or **closing**, and counts the opening steps it has
taken, 0 to 5 (`$ba`, `$bb`). Each opening step shows two more of the box's nine rows, growing out
from the middle row; each closing step takes two away.

| Rows shown | Opening steps taken |
|---|---|
| 0 | 0 |
| 1 | 1 |
| 3 | 2 |
| 5 | 3 |
| 7 | 4 |
| 9 | 5 |

The rows shown are always the middle ones: with *r* shown, rows (9 − *r*) / 2 through
(9 + *r*) / 2 − 1 of the box (`DlgOpenMaskTbl` `:442-447`).

At map load the window is closed with no steps taken (`field/init.asm:432-433`).

## Opening

`openDialogWindow()` reproduces the event commands: it records the placement (top or bottom) and
whether the message is text-only, and sets the window opening (`$ba` = 1). It does not reset the
step count.

Each frame while opening (`:249-309`): if five steps have been taken the window is open and nothing
changes; otherwise one more step is taken. So after a message starts on frame *N* the box shows
**1, 3, 5, 7 and 9 rows on frames *N* to *N* + 4**, and is open from then on.

An open window asked to open again stays open. A window asked to open while closing takes up from
the step it has reached: it grows again from the rows it is showing.

## Closing

`closeDialogWindow()` sets the window closing (`$ba` = `$80`). The text engine asks for it once a
message's last page has been dismissed and wiped, which is never sooner than ten frames after the
message started, so through the text engine the box is always fully open when it starts to close.

Each frame while closing (`:362-439`): if no steps remain, the window is **closed**; otherwise one
step is undone, taking away the outermost two of the rows shown. So after a close asked for on frame
*N* the box shows **7, 5, 3, 1 and 0 rows on frames *N* to *N* + 4**, and is reported closed on
frame ***N* + 5** — one frame after the last row has gone. `isClosed()` is the event system's wait.

**The close crops; the opening squashes.** A closing box keeps the picture it had when open and
simply stops showing its outer rows (`:381-396` restores only the rows it removes and rewrites no
others). An opening box shows the whole box squashed into the rows shown (below).

A close asked for while the window is already closed spends one frame closing before it is closed
again.

## Text-only

A text-only message (`$0564`, bit 6 of the event operand) runs exactly the same steps on the same
frames (`:312-359`, `:408-435`) but shows **no box**: the map and every sprite stay in view under the
text, and the text sits where it would in a box. The original also switches the map's own color
effects off in the text's rows (`hdma.asm:566-572`, written at `:341-342`); that belongs to the
field's color effects, not to this window.

## Placement

The box's top is at **tile row 1** for a message shown at the top of the screen and **tile row 18**
for one shown at the bottom (`field/event.asm:1705-1710`): screen y 8 or 144. The box is nine rows
of 8-pixel tiles, 72 pixels tall.

## The map-title box

`openMapTitleWindow()` reproduces `OpenMapTitleWindow`, which runs once a map's title has been typed
(`field/text.asm:171-178`). It is **instant** — no steps — always at **tile row 1**, and **four rows
tall**: the box's top border, two rows of its wallpaper and its bottom border (`:101-106`: rows 1-3
unscrolled, row 4 showing the box's ninth row; `hdma.asm:907-921`).

`closeMapTitleWindow()` reproduces `CloseMapTitleWindow`: instant, and it does nothing unless the
title box is showing (`:161-162`). The rest of what that routine resets is the text engine's
(`closeMapTitle()`, `docs/contracts/text_engine.md` § Map title).

The dialog window and the title box are never asked for at the same time in the shipped game; the
port does not arbitrate between them.

## Where the text sits

The text area's left edge is at **screen x 16** (the text's tiles start two tiles in,
`DlgTextTiles` `:619-623`, unscrolled), so the pen's first ink is at x 20.

The text area's top is **4 rows below the box's top** (the text is offset by 4 rows across the box's
first 20 rows, `hdma.asm:947-949`). A line's glyphs sit `kGlyphRowOffset` rows below its line's top
(`docs/contracts/text_engine.md` § The picture), so line 0's ink starts on box row 8, the first row
under the top border.

**The lines are 15 pixels apart while the box is open and while it closes, and 16 pixels apart while
it opens.** Once open, the text's vertical offset steps from −4 to −3, −2, −1 and 0 at box rows 20,
35, 50 and 65 (`hdma.asm:885-895`, `:947-955`), each step falling on one of the blank rows between two
lines, so each line after the first sits one pixel higher than the last: ink at box rows 8-18, 23-33,
38-48 and 53-63. While the box opens, every row it shows carries the first offset, −4 (`:458-462`: all
nine pointers on steps 0-3 name the first eight offsets), so the lines keep their 16-pixel spacing
until the fifth step: on the frame the box finishes opening, lines 1, 2 and 3 rise by 1, 2 and 3
pixels. The close leaves the open offsets in the rows it keeps (`:381-396`). A text-only message
does the same. In the English script 205 messages type below their first line within the four
frames the box is opening.

The fourth line's shadow row is box row 64, which is the first row of the bottom border.

**The map title's text area** is 6 rows below the title box's top (`hdma.asm:896-906`,
`:958-963`), so its ink is on box rows 10-20 and its shadow on row 21. A title is one line.

## The picture

Recorded here as the contract the box's components are built against.

- **The box's tiles.** 32 columns by 9 rows (`:626-648`): row 0 the top border (corner, corner,
  14 pairs of edge tiles, corner, corner), rows 1-7 the wallpaper — a 4 × 4 pattern repeated across
  columns 2-29, with left and right edge tiles that alternate between even and odd rows — and row 8
  the bottom border. All 28 of its tiles come from the chosen wallpaper's art.
- **Only columns 1 to 30 are seen.** The field hides x < 8 and x > 247 on every layer
  (`field/reset.asm:846-859`), which the tile formation's own comment notes (`:625`): the box is
  seen 240 pixels wide, from x 8 to x 247. That edge mask is the field's, not the box's.
- **Art and colors.** Eight wallpapers, 28 tiles each, are in the cartridge (`DlgWindowGfxPtrs`
  `:55-58`; the cartridge region `WINDOW_GFX`). Each pixel is one of **seven** colors: the art's
  values are 1 to 7 and never 0, so the box has no see-through pixel (`:40-43`). The seven
  colors are the player's: the configuration holds seven colors for each wallpaper and the chosen
  wallpaper's are loaded at map load (`:527-541`, from `$1d57` + 14 × the wallpaper). Their
  defaults are colors 1-7 of each wallpaper's 16-color palette in the cartridge (`WINDOW_PAL`;
  `menu/menu_gfx.asm:189-210`, `field/init.asm:170-175`). The wallpaper is the configuration's
  choice, 0 to 7 (`field/reset.asm:584-586`).
- **What the box covers.** In the box's rows the screen shows only the box and the text
  (`hdma.asm:281-290`): the map's other layers and every sprite are hidden within the box, and a
  sprite crossing its edge is cut at the edge. The box is opaque and above the map and its sprites.
- **Shading.** The box is lighter toward its top and darker toward its bottom. Per row a level
  *n* from 0 to 7 is **added** to all three color channels (in 31sts) above the box's middle and
  **subtracted** from its middle row down (`hdma.asm:505-534` the levels, `:605-659` add or
  subtract); only the box is shaded, never the text. For the full 72-row box the levels run 7, 7, 7,
  then five rows each of 6 down to 0, five each of 1 up to 6, and 7, 7, 7, 7; the sign changes
  between rows 35 and 36, inside the level-0 rows. Each opening step draws its own height's levels
  over the rows it shows (heights 8, 24, 40, 56, 72), each changing sign at its own middle. The title
  box uses the first 32 rows of the 40-row levels, adding for rows 0-19 and subtracting for 20-31
  (`:123-143`).
- **The opening squash.** On the first four opening steps the whole 72-row box is drawn squashed
  into the 8, 24, 40 or 56 rows shown, centered on the box's middle (`hdma.asm:965-985`). The
  original picks which of the box's rows to show on each screen row; the port scales the box
  vertically about its middle instead. The two differ by at most one row on the 24-, 40- and
  56-row steps; on the 8-row step the original skips the box's middle pair of rows and shows rows
  27 and 44 where the scale shows 31 and 40.

## Language variance

None. The window's code and tables carry no language or revision conditional, and the art and
colors are at the same place in both languages.

## Not in this window

The field's side masks and the field's vertical placement of its tile rows (the field's
rendering); the field's spotlight and color effects, which the box's rows are exempt from
(`hdma.asm:349-351`, `:376-378`); the event commands that start a message and wait on its window;
the configuration menu that edits the window colors and resets them (`menu/config.asm:1280-1301`);
the debug menu's dialog viewer; the ending's fade bars (`:338-349`), which show only the text.
