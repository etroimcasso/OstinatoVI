// The dialog preview: messages from the installed cartridge, typed by the field dialog engine into
// its dialog window and drawn by their components, with a scripted player supplying the input. A
// developer tool for watching the text and the box run on the real script — the box opens and
// closes around each message, at the top or the bottom of the screen, over a flat backdrop. There is
// no map behind it and no event that started the message. It is its own program and is not part of
// the game.
//
//     ostinato-preview-dialog 0 10 224                    # play them in a window, then close
//     ostinato-preview-dialog --capture shots 0 10 224    # no window: frames written to shots/
//
// main.cpp wires the program; this is its screen.
#pragma once

#include <cstdint>
#include <string_view>

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"
#include "text/dialog.h"
#include "text/dialog_window.h"

namespace ostinato::preview {

// The flat backdrop the preview draws behind the box, where the field's map would be: one layer at
// the bottom of the depth table, keyed "backdrop".
inline constexpr std::int32_t kBackdropZ = 0;
inline constexpr std::string_view kBackdropKey = "backdrop";

// The sheets and palettes the screen draws with, as uploaded to the renderer.
struct DialogSheets {
    retropp::AtlasManifest glyphs;    // the glyph sheet (text::buildGlyphSheet)
    retropp::AtlasManifest arrow;     // the choice arrow's sheet; empty when the cartridge has none
    retropp::PaletteId ink;           // the font color
    retropp::PaletteId shadow;        // the drop shadow's black
    retropp::AtlasManifest window;    // the wallpaper sheet (text::buildWindowSheet)
    retropp::PaletteId windowColors;  // the window's seven colors, after a see-through entry 0
    retropp::AtlasManifest backdrop;  // one solid tile
    retropp::PaletteId backdropColor;
};

// The preview's screen, back to front: the backdrop; the dialog box on its own tile layer at
// render::kDialogWindowZ, shaded, squashed while it opens and cropped while it closes; and the
// dialog's text, choice arrow and map title on one sprite layer at render::kDialogTextZ, with the
// page wipe and the box's clip as that layer's regions. Everything is placed where `window` puts it.
[[nodiscard]] render::Layers DialogPreviewScreen(const text::DialogState& state,
                                                 const text::DialogWindowState& window,
                                                 const DialogSheets& sheets);

}  // namespace ostinato::preview
