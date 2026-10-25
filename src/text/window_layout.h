// The dialog box's tile formation and its shading tables. The rows are generated
// (src/data/generated/dialog_window_formation_data.inc, dialog_window_shading_data.inc); this header
// owns the row types, the arrays and the lookups the box's components read.
//
//     const WindowFormationRow& top = kDialogWindowFormation[0];       // the top border
//     for (const WindowShadingRow& r : windowShading(40)) { ... }       // a 40-row box's shading
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/window_tile.h"
#include "text/layout.h"

namespace ostinato::text {

// --- the formation ------------------------------------------------------------------------------

// The formation is 32 tiles across. Columns 0 and 31 repeat columns 1 and 30 and are never seen:
// the box shows kWindowVisibleColumns columns starting at kWindowFirstVisibleColumn.
inline constexpr std::size_t kWindowFormationColumns = 32;
inline constexpr int kWindowFirstVisibleColumn = 1;
inline constexpr int kWindowVisibleColumns = 30;

// One row of the box: its position, top border first, and its tiles left to right.
struct WindowFormationRow {
    std::uint8_t row;
    std::array<WindowTile, kWindowFormationColumns> tiles;
};

inline constexpr std::array<WindowFormationRow, kDialogWindowRows> kDialogWindowFormation = {{
#include "data/generated/dialog_window_formation_data.inc"
}};

static_assert([] {
    for (std::size_t i = 0; i < kDialogWindowFormation.size(); ++i) {
        if (kDialogWindowFormation[i].row != i) {
            return false;
        }
    }
    return true;
}(), "kDialogWindowFormation rows must match their positions");

// The rows of the formation the map-title box shows, top to bottom: the top border, the first two
// wallpaper rows and the bottom border.
inline constexpr std::array<int, kMapTitleWindowRows> kMapTitleFormationRows = {0, 1, 2, 8};

// --- the shading --------------------------------------------------------------------------------

// One row of a shading table: its position from the top of the box as shown, and its level, 0 to
// 7, in 31sts of full brightness on each color channel.
struct WindowShadingRow {
    std::uint8_t row;
    std::uint8_t level;
};

#include "data/generated/dialog_window_shading_data.inc"

template <std::size_t N>
constexpr bool shadingRowsInOrder(const std::array<WindowShadingRow, N>& table) {
    for (std::size_t i = 0; i < N; ++i) {
        if (table[i].row != i) {
            return false;
        }
    }
    return true;
}
static_assert(shadingRowsInOrder(kWindowShading72) && shadingRowsInOrder(kWindowShading56) &&
                  shadingRowsInOrder(kWindowShading40) && shadingRowsInOrder(kWindowShading24) &&
                  shadingRowsInOrder(kWindowShading8),
              "shading rows must match their positions");

// The brightest shading level.
inline constexpr int kWindowShadingMaxLevel = 7;

// The shading table for a box shown `heightPx` pixels tall: 72, 56, 40, 24 or 8. Any other height
// has no table and returns an empty span.
constexpr std::span<const WindowShadingRow> windowShading(int heightPx) {
    switch (heightPx) {
    case 72: return kWindowShading72;
    case 56: return kWindowShading56;
    case 40: return kWindowShading40;
    case 24: return kWindowShading24;
    case 8: return kWindowShading8;
    default: return {};
    }
}

// Whether row `row` of a `heightPx`-tall shading table lightens the box (true, above the table's
// middle) or darkens it (false, from the middle down).
constexpr bool shadingLightens(int row, int heightPx) {
    return row < heightPx / 2;
}

// The table the map-title box draws through, and how many of its rows it uses: the 40-row table's
// first 32, changing from lightening to darkening at that table's middle.
inline constexpr int kMapTitleShadingTablePx = 40;
inline constexpr int kMapTitleWindowPx = kMapTitleWindowRows * kWindowCellPx;

}  // namespace ostinato::text
