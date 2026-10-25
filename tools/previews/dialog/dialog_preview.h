// The dialog preview: messages from the installed cartridge, typed by the field dialog engine and
// drawn by its components, with a scripted player supplying the input. A developer tool for
// watching the text engine run on the real script. A dialog window opens and closes around each
// message, so the text sits where the box puts it and its lines are spaced as the box spaces them,
// but the box itself is not drawn yet, and there is no map behind it and no event that started it.
// It is its own program and is not part of the game.
//
//     ostinato-preview-dialog 0 10 224                    # play them in a window, then close
//     ostinato-preview-dialog --capture shots 0 10 224    # no window: frames written to shots/
//
// main.cpp wires the program; this is its screen.
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"
#include "text/dialog.h"
#include "text/dialog_window.h"

namespace ostinato::preview {

// The dialog font's two sheets and two palettes, as uploaded to the renderer.
struct DialogSheets {
    retropp::AtlasManifest glyphs;  // the glyph sheet (text::buildGlyphSheet)
    retropp::AtlasManifest arrow;   // the choice arrow's sheet; empty when the cartridge has none
    retropp::PaletteId ink;         // the font color
    retropp::PaletteId shadow;      // the drop shadow's black
};

// The preview's screen: the dialog's text, choice arrow and map title on one layer at
// render::kDialogTextZ, with the page wipe as that layer's regions. The text area and its line
// spacing are the ones `window` gives; the title's text area is the title box's.
[[nodiscard]] render::Layers DialogPreviewScreen(const text::DialogState& state,
                                                 const text::DialogWindowState& window,
                                                 const DialogSheets& sheets);

}  // namespace ostinato::preview
