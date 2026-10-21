// DialogueWipe — the half-lines a dismissed page has already lost.
//
//     SpriteLayer("dialogue", kDialogueTextZ, {DialogueText(...), DialogueCursor(...)},
//                 {.regions = DialogueWipe(state, textX, textY)});
//
// A page the player dismisses does not vanish at once: its text goes half a line per frame, left
// half then right half, top line first, before the next page types (text::halfLinesCleared). This
// returns one region per half-line gone so far — the half-line's rectangle, made see-through on the
// layer it is given to — so a glyph straddling the middle of a line loses exactly the half inside
// the gone half-line. (x, y) is the top-left corner of the text area on screen; each half-line is
// kHalfLinePx wide and kLineHeight tall. Returns nothing while no page is being wiped.
#pragma once

#include "render/types.h"
#include "text/dialogue.h"

namespace ostinato::render {

[[nodiscard]] Regions DialogueWipe(const text::DialogueState& state, int x, int y);

}  // namespace ostinato::render
