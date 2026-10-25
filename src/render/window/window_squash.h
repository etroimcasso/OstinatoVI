// WindowSquash — the dialog box squashed into the rows it shows while it opens.
//
//     TileLayer(kDialogWindowKey, kDialogWindowZ, DialogWindow(...), ...,
//               {.transform = WindowSquash(window, boxTopLeft)});
//
// On each opening step before the last, the whole box is drawn squashed vertically into the rows
// shown, about its middle row: a vertical scale of (rows shown) / 9. The box's own layer carries it.
// Returns the identity once the box is open, while it closes — the close crops the box rather than
// squashing it (WindowClip) — and for a closed window, the map-title box and a text-only message.
//
// `boxTopLeft` is the screen position of the box's top-left visible corner.
#pragma once

#include "retropp/draw_state.h"
#include "retropp/transform.h"

#include "text/dialog_window.h"

namespace ostinato::render {

[[nodiscard]] retropp::Transform WindowSquash(const text::DialogWindowState& state,
                                              retropp::Point boxTopLeft);

}  // namespace ostinato::render
