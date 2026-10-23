// The dialog preview: messages from the installed cartridge, typed by the field dialog engine and
// drawn by its components, with a scripted player supplying the input. A developer tool for
// watching the text engine run on the real script — there is no dialog window around the text, no
// map behind it, and no event that started it. It is its own program and is not part of the game.
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

namespace ostinato::preview {

// Where the text area's top-left corner sits on the preview's screen. Placing the text is the
// dialog window's job; the preview has no window, so it centers the text area across the screen's
// width, near the top.
inline constexpr int kPreviewTextX = 16;
inline constexpr int kPreviewTextY = 16;

// The dialog font's two sheets and two palettes, as uploaded to the renderer.
struct DialogSheets {
    retropp::AtlasManifest glyphs;  // the glyph sheet (text::buildGlyphSheet)
    retropp::AtlasManifest arrow;   // the choice arrow's sheet; empty when the cartridge has none
    retropp::PaletteId ink;         // the font color
    retropp::PaletteId shadow;      // the drop shadow's black
};

// The preview's screen: the dialog's text, choice arrow and map title on one layer at
// render::kDialogTextZ, with the page wipe as that layer's regions and the text area at
// (kPreviewTextX, kPreviewTextY).
[[nodiscard]] render::Layers DialogPreviewScreen(const text::DialogState& state,
                                                 const DialogSheets& sheets);

}  // namespace ostinato::preview
