// HudWindows — the HUD's two windows side by side, and the shading that darkens them toward the bottom.
//
//     TileLayer(kHudWindowKey, kHudWindowZ, HudWindows(Language::EN, wallpaper, colors),
//               hudWindowsColumns(Language::EN), kHudWindowRows,
//               {.scroll = hudWindowsScroll(Language::EN), .regions = HudShading(Language::EN)});
//
// The monster names' window and the party's window, each a MenuWindow at the size the battle's
// window table gives it, laid side by side into one grid of cells that starts at the first window's
// column and ends at the second's last. The grid sits at the HUD's top line.
//
// HudShading darkens the windows a 31st more every three lines from line 151, as the battle's own
// color math does: lines 151 to 153 are untouched, the windows' last lines lose 21 31sts. It covers
// the windows' own rectangle.
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/draw_state.h"
#include "retropp/palette.h"

#include "ostinato/game_version.h"
#include "render/types.h"

namespace ostinato::render {

[[nodiscard]] Cells HudWindows(Language language, const retropp::AtlasManifest& wallpaper, retropp::PaletteId colors);

// How many columns the grid HudWindows returns is, and the scroll that puts it in place.
int hudWindowsColumns(Language language);
retropp::LayerScroll hudWindowsScroll(Language language);

[[nodiscard]] Regions HudShading(Language language);

}  // namespace ostinato::render
