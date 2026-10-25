// DialogWipe — the half-lines a dismissed page has already lost.
//
//     SpriteLayer("dialog", kDialogTextZ, {DialogText(...), DialogCursor(...)},
//                 {.regions = DialogWipe(state, x, y, pitch)});
//
// A page the player dismisses does not vanish at once: its text goes half a line per frame, left
// half then right half, top line first, before the next page types (text::halfLinesCleared). This
// returns one region per half-line gone so far — the half-line's rectangle, made see-through on the
// layer it is given to — so a glyph straddling the middle of a line loses exactly the half inside
// the gone half-line. (x, y) is the top-left corner of the text area on screen and `linePitch` how
// far apart its lines are, as for DialogText. Each half-line is kHalfLinePx wide and covers the
// line's kLineInkRows rows: its glyphs, their shadows and the choice arrow, and nothing of the lines
// above or below. Returns nothing while no page is being wiped.
#pragma once

#include "render/types.h"
#include "text/dialog.h"

namespace ostinato::render {

[[nodiscard]] Regions DialogWipe(const text::DialogState& state, int x, int y, int linePitch);

}  // namespace ostinato::render
