// WindowShading — the light and dark bands across the dialog box.
//
//     TileLayer(kDialogWindowKey, kDialogWindowZ, DialogWindow(...), ...,
//               {.regions = WindowShading(window, boxTopLeft)});
//
// The box is lighter toward its top and darker toward its bottom. This returns one region per run
// of box rows that share a shading level and a side of the box's middle: a ColorFill of that level
// in gray, added to the box above the middle and subtracted from it below. Level 0 rows get no
// region. Give the result to the box's own layer, so only the box is shaded and the text above it
// is not.
//
// `boxTopLeft` is the screen position of the box's top-left visible corner. While the box opens each
// step draws its own height's shading over the rows it shows; while it closes, the open box's
// shading stays on the rows that remain. Returns nothing for a closed window or a text-only message.
#pragma once

#include "retropp/draw_state.h"

#include "render/types.h"
#include "text/dialog_window.h"

namespace ostinato::render {

[[nodiscard]] Regions WindowShading(const text::DialogWindowState& state, retropp::Point boxTopLeft);

}  // namespace ostinato::render
