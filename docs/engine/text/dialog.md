# The dialog engine

The field dialog engine types a message into the dialog window one glyph per frame, breaks
its lines, pauses where the script says, waits on the player between pages, runs the
multiple-choice cursor, and shows the map title when a map opens. It is a state machine over a
plain struct, advanced by one function once per frame. It holds no picture: it records where
every glyph went and what the player is looking at, and the rendering components turn that
into sprites.

The behavior it reproduces is specified in [`docs/contracts/text_engine.md`](../../contracts/text_engine.md);
this page is about using and changing the code.

## Public surface

```cpp
#include "text/dialog.h"

using namespace ostinato::text;

DialogContext context;
context.metrics = content.dialogFont.metrics;  // the font's widths, from the cartridge
context.corpus = &corpus;                  // the TextCorpus the message comes from
context.names[0] = terraName;              // each character's current name

DialogState state;
initDialog(state);                       // at map load

startDialog(state, 1203, context);       // an event shows message 1203
while (running) {
    DialogInput input{.confirmHeld = a, .up = up, .down = down, .left = left, .right = right};
    tickDialog(state, input, context);   // once per field frame
    if (windowCloseRequested(state)) { /* the message is done; close the window */ }
}
```

| Function | What it does |
|---|---|
| `initDialog(state)` | Resets everything, as entering a map does: no message, an empty text area, the pen at the top left. |
| `startDialog(state, index, context)` | Begins typing dialog `index` from the corpus. Reads the record the way the original follows its pointer: an empty record shows the next one's text, and a record without an `END` runs into the next. |
| `startDialog(state, tokens)` | The same with an already-decoded message, for callers (and tests) that build one. |
| `tickDialog(state, input, context)` | One field frame. Nothing about dialog changes anywhere else. |
| `showMapTitle(state, record, context)` | Types a map title, centered on line 0, all at once; it stays up for `kTitleFrames`. |
| `closeMapTitle(state)` | Takes the title down early, as any event starting does. |
| `awaitingKeypress(state)` | The page is fully shown and the engine is waiting for A. An event that waits on a message polls this. |
| `windowCloseRequested(state)` | The message finished this frame and wants its window closed. Reading it clears it. |
| `selection(state)` / `clearSelection(state)` | The choice the player picked, kept until the event that reads it clears it. |
| `halfLinesCleared(state)` / `isWiped(state, line, x)` | How far a dismissed page's text has gone, and whether a given spot has gone yet. |

### Input

`DialogInput` is five held levels: A after the player's button mapping, and the four
directions. They are levels, not presses. The engine does its own edge detection: a page is
dismissed by A being *released* and then *pressed*, so A held down through a page end does
not skip it, and the cursor moves once per direction press however long it is held.

### Context

`DialogContext` is everything a message reads from outside the engine. Fill it once and pass
it every frame:

| Field | Used by |
|---|---|
| `language` | Everything that differs between English and Japanese: the word wrap, the tab fill glyph, the digit glyphs, name conversion, title centering. |
| `metrics` | A `FontMetrics` view over the font's width table — how far the pen moves per glyph, and what the word wrap adds up. The cartridge's is `DialogFont::metrics` ([font.md](font.md)). |
| `corpus` | `startDialog(index)`, and the item-name and spell-name splices. |
| `mte` | The Japanese multi-tile table; unused in English. |
| `names` | The character-name splice: character *n*'s current name in the menu font's encoding, ended early by `kNameEnd`. |
| `item`, `spellIndex`, `goldAmount` | The item, spell and gold splices — the thing just found in a chest. |

The engine never looks these up itself. Whoever starts a message that splices a name or an
amount sets the field first.

## What the rendering reads

The state is the picture's data. The fields a rendering component needs:

| Field | Meaning |
|---|---|
| `placed` | Every glyph on the current page, in drawing order: `{page, line, x, glyph}`. `x` is the pen position the glyph was drawn at — the left edge of its ink. Its artwork starts `kGlyphArtLeftOfPen` pixels left of that, `kGlyphRowOffset` rows below its line's top. How far apart the lines are is the dialog window's to say ([window.md](window.md)). |
| `page` | Counts every page the state has shown. It is part of a glyph's identity: a glyph on page 7 and a glyph at the same spot on page 8 are different things, and a key built from `{page, line, ordinal}` keeps them apart. |
| `cursor` | Where the choice arrow is, if it is on screen: a line and the pen x of its 16-pixel column. The arrow draws at the column's left edge. |
| `titleShowing` | Whether the glyphs in `placed` are a map title that is up. |
| `halfLinesCleared()` / `isWiped()` | A dismissed page leaves half a line at a time over eight frames, top line first, left half before right. A glyph or cursor on a half-line that has gone is not drawn. |

A frame's dialog picture is therefore: each glyph in `placed`, drawn twice — in the font
color with its artwork at `(x − kGlyphArtLeftOfPen, line · pitch + kGlyphRowOffset)` in the text
area, where `pitch` is the window's line spacing, and
in black one pixel right and one down (`kShadowDx`, `kShadowDy`) beneath every letter — plus
the arrow if present, its shadow one pixel to the right only (`kCursorShadowDx`,
`kCursorShadowDy`), with every half-line that has gone left out. Nothing else is needed and
nothing is remembered between frames.

## Drawing it

The rendering components do exactly that, one per piece of the picture
([../rendering/components.md](../rendering/components.md)):

```cpp
#include "render/layers.h"
#include "render/sprite_layer.h"
#include "render/text/dialog_cursor.h"
#include "render/text/dialog_text.h"
#include "render/text/dialog_wipe.h"
#include "render/text/map_title.h"
#include "text/dialog_window.h"

using namespace ostinato::render;

const int x = text::kTextAreaLeft;
const int y = text::textAreaTop(window);      // the message's box, top or bottom
const int pitch = text::linePitch(window);    // 16 while the box opens, 15 after
frame.layers.push_back(SpriteLayer(
    "dialog", kDialogTextZ,
    {DialogText(state, x, y, pitch, glyphSheet, ink, shadow),
     DialogCursor(state, x, y, pitch, arrowSheet, ink, shadow),
     MapTitle(state, x, text::titleTextAreaTop(), glyphSheet, ink, shadow)},
    {.regions = DialogWipe(state, x, y, pitch)}));
```

| Component | Draws |
|---|---|
| `DialogText` | The page's glyphs and their shadows; nothing while a map title is up. |
| `MapTitle` | The map title's glyphs while it is up. |
| `DialogCursor` | The choice arrow and its shadow, when there is one. |
| `DialogWipe` | One see-through region per half-line a dismissed page has lost, for the layer's `regions`. |

`glyphSheet` and `arrowSheet` are the font's two sheets, uploaded once
([font.md](font.md)); `ink` and `shadow` are the palettes for the font color and the shadow
black. `window` is the message's dialog window ([window.md](window.md)), which places the text
area and spaces its lines. The engine is advanced in the frame's update and these are called in
its draw, so the picture always matches the state the frame ended on.

## Preview

`ostinato-preview-dialog` plays messages from the installed cartridge through the text
engine, the dialog window and their components — the whole chain from the cartridge to the
screen. It is a developer tool (`tools/previews/dialog/`), not part of the game. Each message
types into its box over a flat backdrop that stands in for the map; there is no event that started
the message.

```sh
ostinato-vi --install-rom <path-to-rom>                  # once, if no cartridge is installed
ostinato-preview-dialog 0 10 224                         # play dialogs 0, 10 and 224 (of 0 to 3083) in a window
ostinato-preview-dialog --capture shots 0 10 224         # the same with no window, frames written to shots/
ostinato-preview-dialog --title 1 32 224 --text-only 10  # map 1's title first; dialog 10 with no box
```

Nobody drives it. A scripted player plays each message to its end: on every page that waits
for A it holds the page, presses down once halfway through the hold on the first page that
offers two or more choices, then presses A. The messages' boxes alternate between the top and
the bottom of the screen, starting at the top, and every message after `--text-only` shows no
box. `--title <map>` shows that map's title first, for as long as the game shows one. In the
window the hold is two seconds, the messages play one after another on the field tick
(`kFieldTickPeriod`) at the SNES's 256×224 with a second's pause between them, and the window
closes itself after the last. A message that has not ended after ten minutes' worth of frames is
reported and the exit status is nonzero. A message is finished once its window has closed, five
frames after the text asks for it.

With `--capture <dir>` nothing opens. The title and each message play straight through, and the
screen is composed off screen and written to `<dir>` as binary PPM:

| File | The screen when |
|---|---|
| `title-<map>.ppm` | the map title is up |
| `dialog-<index>-open<rows>.ppm` | the box shows 1, 3, 5, 7 and then all 9 of its rows, on the five frames it opens over |
| `dialog-<index>-page<n>.ppm` | page *n* is fully typed and waits for A |
| `dialog-<index>-page<n>-moved.ppm` | on the first page with two or more choices, the selection has moved down one |
| `dialog-<index>-wipe.ppm` | the first page wipe has taken its first half-line |
| `dialog-<index>-close.ppm` | the box has started to close |

A page that moves on by itself — a timed wait, as in the opening narration — never waits
for A, so it has no page file.

Either way the splices insert fixed values: every character's default name, the Mithril
Knife, and 500 gil, and the box uses the first wallpaper in the cartridge's colors.

`DialogPreviewScreen(state, window, sheets)` (`tools/previews/dialog/dialog_preview.h`) is the
screen, back to front: the backdrop as a `TileLayer`; the box as a `TileLayer` at
`kDialogWindowZ` with `WindowShading` and `WindowClip` as its regions and `WindowSquash` as its
transform; and one `SpriteLayer` at `kDialogTextZ` gathering `DialogText`, `DialogCursor` and
`MapTitle`, with `DialogWipe` and `WindowClip` as its regions — all placed where `window` puts
them. `main.cpp` is the wiring — it uploads the sheets and palettes once, advances the engine and
its window one field frame at a time in the field's order with the scripted player's input, and
assembles the screen for each picture. `tests/test_dialog_preview.cpp` reads back what the screen
returns.

## How a message types

Each frame does, in order: count down the title timer; return if idle; count down a pause;
count down the key timer; if waiting on the player, read the buttons and return; if a page is
being wiped, advance the wipe and return; if the message has finished, request the window
close and return; otherwise consume one glyph or one control.

A plain glyph costs one frame. The control codes (`FieldTextCommand`, from the token stream):

| Control | Effect |
|---|---|
| `END` | Ends the page and the message. |
| `NEWLINE` | Pen to the left, next line. The fourth line's end is a page end. |
| `PAGE` | Page end: the player must dismiss it, then the text wipes. |
| `WAIT`, `WAIT_FRAMES n` | Pause `kSecondFrames`, or `kQuarterSecondFrames · n`. |
| `KEY_TIMED n` | Wait for A, or `kQuarterSecondFrames · n` frames, whichever comes first. |
| `KEY` | Does not wait. It arms the key timer for one frame, whose running out resets the choice count — a choice reset, not a pause. |
| `TAB n` | Queues `n` space glyphs; one draws per frame after this one. |
| `CHOICE` | Records a choice position and draws the wide space. |
| `CHARACTER_NAME n`, `ITEM_NAME`, `SPELL_NAME`, `GP` | Queue a splice from the context; its first glyph draws this frame, the rest one per frame. |

A queued splice is `DialogState::splice`; while it drains, the cursor stays on the control
that queued it. A splice that turns out empty draws nothing and spends the frame.

### The English word wrap

Before an English frame draws anything, the engine measures the word at the cursor — from the
next undrawn glyph to the next space or control — and breaks the line first if the word would
reach the margin. Character and item names are measured with the control that splices them;
gold amounts, spell names and tab fill are never measured, so they can reach the margin a glyph
at a time. Japanese text is never measured: only the per-glyph margin check applies.

A glyph that would itself cross the margin is dropped, not carried — the line breaks and the
message continues with the next glyph. `DialogState::droppedAtMargin` records each one; see
[`docs/Bugs.md`](../../Bugs.md) for what that amounts to on the shipped script (305 trailing
spaces, nothing visible).

## Gotchas

- **Lifetimes.** `DialogContext` holds spans and a pointer — the width table, the names, the
  corpus must outlive it. The engine copies nothing out of them.
- **The pen carries over.** `startDialog` resets only the message. A finished message has
  already put the pen at the top left, but starting a message while another is mid-page types
  from wherever the pen is. Call `initDialog` to reset the text area.
- **A title delays the next message.** After the title closes, the engine is idle, and the
  wipe that clears the title's glyphs does not run until a message starts. That message types
  nine frames late. This is the original's behavior.
- **`KEY` is not a pause.** See the table above. If a script needs the message to wait, it uses
  `KEY_TIMED` or a page end.
- **The spell splice reads 4 bytes at a stride of 4** through a 7-byte-record table. That is
  what the original does, and nothing in the shipped game reaches it. Do not "fix" it without
  reading `docs/contracts/text_engine.md § Splices`.

## Where to change things

| To change | Edit |
|---|---|
| The text area's geometry or any timing (margins, lines per page, the line spacing, pause lengths, the title's duration) | `src/text/layout.h` — every constant is named there and used nowhere by value. |
| Where the text area sits on screen | The dialog window ([window.md](window.md)). |
| What a control code does | `runControl()` in `src/text/dialog.cpp`. |
| How a word is measured | `measureWord()` in `src/text/dialog.cpp`. |
| How a splice is built | The `*Glyphs()` functions in `src/text/dialog.cpp`. |
| How the cursor moves | `kChoiceMoves` and `moveChoiceCursor()` in `src/text/dialog.cpp`. |
| How any of it is drawn | The components under `src/render/text/` ([../rendering/components.md](../rendering/components.md)). |
| The preview's scripted player, pacing, splice values or window placement | `tools/previews/dialog/main.cpp` and `dialog_preview.h`. |
| The expected behavior | `docs/contracts/text_engine.md`, then `tests/test_dialog.cpp`, which is written from it. |

The tests in `tests/test_dialog.cpp` drive the engine frame by frame over hand-built
messages with a synthetic font, then run every English message in the cartridge to its end
with the real font widths. A change that alters any observable behavior shows up there.
`tests/test_dialog_components.cpp` reads back what each component returns, including over a
running message as its page is dismissed and wiped.
