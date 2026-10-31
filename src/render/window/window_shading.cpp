#include "render/window/window_shading.h"

#include <cstddef>
#include <span>
#include <vector>

#include "render/color_gradient.h"
#include "render/layers.h"
#include "text/layout.h"
#include "text/window_layout.h"

namespace ostinato::render {

namespace {

// The rows [first, end) of `table` as a gradient over the box's width, the table's row 0 at screen
// y `tableTop`. The rows above `first` are one blank band, so each region's key counts its rows
// from the table's top.
Regions shadingRows(std::span<const text::WindowShadingRow> table, int tableHeightPx, int first,
                    int end, retropp::Point boxTopLeft, float tableTop) {
    std::vector<GradientBand> bands;
    if (first > 0) {
        bands.push_back({.rows = first, .level = 0, .blend = retropp::BlendMode::Add});
    }
    for (int row = first; row < end; ++row) {
        bands.push_back({.rows = 1,
                         .level = table[static_cast<std::size_t>(row)].level,
                         .blend = text::shadingLightens(row, tableHeightPx)
                                      ? retropp::BlendMode::Add
                                      : retropp::BlendMode::Subtract});
    }
    return ColorGradient(kWindowShadingKey, {.x = boxTopLeft.x, .y = tableTop},
                         static_cast<float>(text::kWindowVisibleColumns * text::kWindowCellPx),
                         bands);
}

}  // namespace

Regions WindowShading(const text::DialogWindowState& state, retropp::Point boxTopLeft) {
    if (state.titleShowing) {
        return shadingRows(text::windowShading(text::kMapTitleShadingTablePx),
                           text::kMapTitleShadingTablePx, 0, text::kMapTitleWindowPx, boxTopLeft,
                           boxTopLeft.y);
    }
    const int rows = text::rowsShown(state);
    if (state.textOnly || rows == 0) {
        return {};
    }
    const int shownTop = text::firstRowShown(state) * text::kWindowCellPx;
    const int shownPx = rows * text::kWindowCellPx;
    if (state.phase == text::WindowPhase::Opening) {
        // Each opening step shades the rows it shows with its own height's table.
        return shadingRows(text::windowShading(shownPx), shownPx, 0, shownPx, boxTopLeft,
                           boxTopLeft.y + static_cast<float>(shownTop));
    }
    // Open, or closing: the open box's table, on the rows that remain.
    const int boxPx = text::kDialogWindowRows * text::kWindowCellPx;
    return shadingRows(text::windowShading(boxPx), boxPx, shownTop, shownTop + shownPx, boxTopLeft,
                       boxTopLeft.y);
}

}  // namespace ostinato::render
