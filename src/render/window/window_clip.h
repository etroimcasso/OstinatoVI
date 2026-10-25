// WindowClip — hides what lies outside the dialog box's rows while it opens and closes.
//
//     SpriteLayer("dialog", kDialogTextZ, {DialogText(...), ...},
//                 {.regions = concat(DialogWipe(...), WindowClip(window, boxTopLeft))});
//
// While the window opens or closes only the box's middle rows show, and the text shows only within
// them too. This returns a see-through region over everything outside those rows, the full width of
// the screen, for the layer it is given to: the text's layer, and the box's own layer, where it is
// what crops the closing box. With no rows shown the whole layer is see-through. Returns nothing
// once the box is open, for a closed window, and for the map-title box.
//
// `boxTopLeft` is the screen position of the box's top-left visible corner.
#pragma once

#include "retropp/draw_state.h"

#include "render/types.h"
#include "text/dialog_window.h"

namespace ostinato::render {

[[nodiscard]] Regions WindowClip(const text::DialogWindowState& state, retropp::Point boxTopLeft);

}  // namespace ostinato::render
