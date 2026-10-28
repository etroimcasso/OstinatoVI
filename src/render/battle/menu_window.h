// MenuWindow — one of the battle's windows: a box of the wallpaper's tiles, border included.
//
//     const BattleWindowEntry& box = battleWindow(BattleWindowId::MONSTER_NAMES, language);
//     Cells cells = MenuWindow(box.width, box.height, wallpaper, colors);
//
// `columns` by `rows` tile cells, row by row, every one drawn from `wallpaper` (the uploaded
// wallpaper sheet, text::buildWindowSheet) through `colors`, the battle's window colors. The border
// runs around the outside: the corners, the top and bottom edges alternating their two tiles from
// the left, the left and right edges alternating theirs from the top. Inside, the wallpaper's 4 x 4
// pattern repeats, its first column at the window's second.
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"

namespace ostinato::render {

// PRECONDITION (asserted): at least two columns and two rows, so the border fits.
[[nodiscard]] Cells MenuWindow(int columns, int rows, const retropp::AtlasManifest& wallpaper,
                               retropp::PaletteId colors);

}  // namespace ostinato::render
