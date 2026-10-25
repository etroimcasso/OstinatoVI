// DialogWindow — the dialog box's tiles.
//
//     const Cells cells = DialogWindow(window, wallpaperSheet, windowPalette);
//     frame.layers.push_back(TileLayer(kDialogWindowKey, kDialogWindowZ, cells,
//                                      text::kWindowVisibleColumns,
//                                      static_cast<int>(cells.size()) / text::kWindowVisibleColumns,
//                                      {.scroll = {-8, -boxTop}, ...}));
//
// The box's formation as tile cells, kWindowVisibleColumns across — the columns the field shows —
// with every cell drawn from `sheet` (the uploaded wallpaper sheet, text::buildWindowSheet) through
// `palette`, the window's colors. A message's box is all nine rows whenever any of it shows: while
// it opens the layer's transform squashes it into the rows shown (WindowSquash), and while it closes
// a clip crops it to them (WindowClip). The map-title box is its four rows. Returns nothing while
// the window is closed, before its first opening step, and for a text-only message.
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"
#include "text/dialog_window.h"

namespace ostinato::render {

[[nodiscard]] Cells DialogWindow(const text::DialogWindowState& state,
                                 const retropp::AtlasManifest& sheet, retropp::PaletteId palette);

}  // namespace ostinato::render
