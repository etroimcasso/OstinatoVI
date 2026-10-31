#include "render/battle/menu_window.h"

#include <cassert>
#include <cstddef>

#include "data/battle_gfx_tables.h"
#include "ostinato/window_tile.h"

namespace ostinato::render {

namespace {

// Which of the wallpaper's four corners.
enum Corner : std::size_t { TopLeft, TopRight, BottomLeft, BottomRight };

// The tile at window cell (row, column), border included.
WindowTile windowTile(int row, int column, int columns, int rows) {
    const BattleWindowBorder& border = battleWindowBorder();
    const auto pattern = [](int n) { return static_cast<std::size_t>(n % kBattleWindowPattern); };
    const bool top = row == 0;
    const bool bottom = row == rows - 1;
    const bool left = column == 0;
    const bool right = column == columns - 1;
    if (top && left) return border.corners[TopLeft];
    if (top && right) return border.corners[TopRight];
    if (bottom && left) return border.corners[BottomLeft];
    if (bottom && right) return border.corners[BottomRight];
    if (top) return border.top[pattern(column)];
    if (bottom) return border.bottom[pattern(column)];
    if (left) return border.left[pattern(row)];
    if (right) return border.right[pattern(row)];
    // The pattern's rows are counted from the first row inside the border; its columns are counted
    // from the border itself, as the table stores them.
    return battleWindowWallpaper()[pattern(row - 1)].tiles[pattern(column)];
}

}  // namespace

Cells MenuWindow(int columns, int rows, const retropp::AtlasManifest& wallpaper, retropp::PaletteId colors) {
    assert(columns >= 2 && rows >= 2 && "a window is at least its border");
    Cells cells;
    cells.reserve(static_cast<std::size_t>(columns * rows));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const WindowTile tile = windowTile(row, column, columns, rows);
            cells.push_back(retropp::TileCell{
                .atlas = wallpaper.atlasId,
                .tile = wallpaper[static_cast<std::size_t>(tile)].tile,
                .palette = colors,
            });
        }
    }
    return cells;
}

}  // namespace ostinato::render
