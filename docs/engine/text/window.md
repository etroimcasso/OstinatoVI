# The dialog window

The dialog window is the box a field message types into. It opens over five frames, growing out
from its middle row, stays open while the message runs, and closes over five more, taking its
outer rows away first. It sits at the top or the bottom of the screen, it can show no box at all
for a text-only message, and the same state carries the map title's smaller box. It also decides
where the text area is and how far apart the text's lines are.

Like the dialog engine ([dialog.md](dialog.md)), the window is logic only: a plain struct
advanced by one function once per frame. It holds no picture and includes no rendering type.

The behavior it reproduces is specified in
[`docs/contracts/dialog_window.md`](../../contracts/dialog_window.md); this page is about using
and changing the code.

## Public surface

```cpp
#include "text/dialog.h"
#include "text/dialog_window.h"

using namespace ostinato::text;

DialogState text;
DialogWindowState window;
initDialog(text);                                         // at map load
initDialogWindow(window);

startDialog(text, 1203, context);                         // an event shows message 1203...
openDialogWindow(window, WindowPlacement::Bottom, false); // ...in a box at the bottom

while (running) {
    tickDialog(text, input, context);                     // the text first
    if (windowCloseRequested(text)) {
        closeDialogWindow(window);                        // the message's end reaches the window
    }
    tickDialogWindow(window);                             // then the window
    if (isClosed(window)) { /* the event moves on */ }
}
```

| Function | What it does |
|---|---|
| `initDialogWindow(state)` | Resets the window, as entering a map does: closed, no steps taken, no title box. |
| `openDialogWindow(state, placement, textOnly)` | Opens the window for a message, at the top or the bottom, with no box behind the text when `textOnly`. The steps already taken carry on (see *Gotchas*). |
| `closeDialogWindow(state)` | Starts the close. The text engine asks for it once a message's last page is gone. |
| `tickDialogWindow(state)` | One field frame. Nothing about the window changes anywhere else. |
| `isClosed(state)` | The window is closed. An event that shows a message and waits for it waits on this. |
| `openMapTitleWindow(state)` / `closeMapTitleWindow(state)` | Puts the map-title box up or takes it down, both at once. Taking it down does nothing unless it is up. |
| `rowsShown(state)` | How many of the box's rows are showing, always the middle ones. |
| `boxTopRow(state)` | The cell row the box's top is on: `kDialogWindowTopRow` or `kDialogWindowBottomRow`. |
| `textAreaTop(state)` | The screen y of the top of the text area in this window's box. |
| `titleTextAreaTop()` | The screen y of the top of the map title's text area. |
| `linePitch(state)` | How far apart the text's lines are right now: `kOpeningLinePitch` (16) while the window opens, `kLinePitch` (15) otherwise. |

### The state

| Field | Meaning |
|---|---|
| `phase` | `Closed`, `Opening`, `Open` or `Closing`. |
| `step` | The opening steps taken, 0 to `kWindowOpenFrames`. Each opening frame takes one and each closing frame undoes one. |
| `placement` | `WindowPlacement::Top` or `Bottom`, from the last `openDialogWindow`. |
| `textOnly` | The message shows no box: the text sits where it would, over whatever is behind it. |
| `titleShowing` | The map-title box is up. |

## A message's window, frame by frame

The frame a message starts is the frame its window takes its first step, and the frame its first
glyph is typed.

| Frame | Rows shown | Phase | Line pitch |
|---|---|---|---|
| start | 1 | `Opening` | 16 |
| +1 | 3 | `Opening` | 16 |
| +2 | 5 | `Opening` | 16 |
| +3 | 7 | `Opening` | 16 |
| +4 | 9 | `Open` | 15 |
| close asked for | 7 | `Closing` | 15 |
| +1 | 5 | `Closing` | 15 |
| +2 | 3 | `Closing` | 15 |
| +3 | 1 | `Closing` | 15 |
| +4 | 0 | `Closing` | 15 |
| +5 | 0 | `Closed` | 15 |

The window reports closed one frame after its last row has gone. The text engine never asks for
a close sooner than ten frames into a message, so through it the box is always fully open when it
starts to close.

A text-only message runs the same frames and the same rows. Only the picture differs: there is no
box, and what is behind the text stays in view.

## Where the text sits

```cpp
const int x = kTextAreaLeft;            // 16: the pen's first ink is at x 20
const int y = textAreaTop(window);      // 12 for a top box, 148 for a bottom one
const int pitch = linePitch(window);    // line n's top is y + n * pitch
```

The box's top is cell row 1 or 18, at screen y 8 or 144, and the box is nine 8-pixel rows tall.
The text area starts `kTextAreaTopInBox` rows into the box, so line 0's ink is on the first row
under the top border. Once the box is open the lines are 15 pixels apart and the fourth line's
shadow lands on the bottom border's first row. While it opens they are 16 apart, so on the frame
the box finishes opening lines 1, 2 and 3 move up by 1, 2 and 3 pixels; the rendering components
key each glyph by its line and place, so they move rather than being replaced.

The map title has its own text area, `titleTextAreaTop()`, which sits lower in the title box than
a message's does in its box. The title box is four rows tall and always at the top.

These positions are what the rendering components take as their `(x, y)` and `linePitch`
([../rendering/components.md](../rendering/components.md)).

## Gotchas

- **Order matters within a frame.** Tick the text, pass its close request on, then tick the
  window. Ticking the window first makes every close one frame late.
- **Opening does not restart.** `openDialogWindow` keeps the steps already taken. An open window
  asked to open again stays open; a closing window asked to open grows again from the rows it is
  showing. Call `initDialogWindow` for a fresh closed window.
- **A close takes a frame even when closed.** `closeDialogWindow` on a closed window leaves it
  closing for one frame, so `isClosed` is false until the next tick.
- **The title box does not arbitrate.** While it is up, `rowsShown` reports its four rows and
  `boxTopRow` the top, whatever the message window's own state is. The game never asks for both
  at once.

## Where to change things

| To change | Edit |
|---|---|
| The box's size and position, how long it takes to open, where the text area sits in it, the line spacing | `src/text/layout.h` — every constant is named there and used nowhere by value. |
| What a frame does to the window | `tickDialogWindow()` in `src/text/dialog_window.cpp`. |
| How the rows shown follow from the steps | `rowsShown()` in `src/text/dialog_window.cpp`. |
| When the lines are 16 or 15 apart | `linePitch()` in `src/text/dialog_window.cpp`. |
| The expected behavior | `docs/contracts/dialog_window.md`, then `tests/test_dialog_window.cpp`, which is written from it. |

`tests/test_dialog_window.cpp` drives the window frame by frame, runs a short message through the
text engine beside it in the field's order, and runs the opening frames of every English message
in the cartridge to pin how many type below their first line while the box is still opening.
