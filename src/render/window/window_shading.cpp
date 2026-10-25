#include "render/window/window_shading.h"

#include <span>
#include <string>

#include "render/layers.h"
#include "text/layout.h"
#include "text/window_layout.h"

namespace ostinato::render {

namespace {

// A shading level in 8-bit gray: level n of 31.
std::uint8_t levelGray(int level) {
    constexpr int kFullLevel = 31;
    return static_cast<std::uint8_t>((level * 255 + kFullLevel / 2) / kFullLevel);
}

// The rows [first, end) of `table`, the table's row 0 at screen y `tableTop`, as runs.
void appendRuns(Regions& regions, std::span<const text::WindowShadingRow> table, int tableHeightPx,
                int first, int end, retropp::Point boxTopLeft, float tableTop) {
    int row = first;
    while (row < end) {
        const int level = table[static_cast<std::size_t>(row)].level;
        const bool lightens = text::shadingLightens(row, tableHeightPx);
        int runEnd = row + 1;
        while (runEnd < end && table[static_cast<std::size_t>(runEnd)].level == level &&
               text::shadingLightens(runEnd, tableHeightPx) == lightens) {
            ++runEnd;
        }
        if (level > 0) {
            const std::uint8_t gray = levelGray(level);
            regions.push_back(retropp::Region{
                .key = std::string{kWindowShadingKey} + ":" + std::to_string(row),
                .shape = retropp::ShapePoints::rectangle(
                    {.x = boxTopLeft.x, .y = tableTop + static_cast<float>(row)},
                    static_cast<float>(text::kWindowVisibleColumns * text::kWindowCellPx),
                    static_cast<float>(runEnd - row)),
                .effects = {retropp::ScreenSpaceEffect{
                    .kind = retropp::ScreenSpaceEffectKind::ColorFill,
                    .fill = retropp::Rgba8{.r = gray, .g = gray, .b = gray}}},
                .blend = lightens ? retropp::BlendMode::Add : retropp::BlendMode::Subtract,
            });
        }
        row = runEnd;
    }
}

}  // namespace

Regions WindowShading(const text::DialogWindowState& state, retropp::Point boxTopLeft) {
    Regions regions;
    if (state.titleShowing) {
        appendRuns(regions, text::windowShading(text::kMapTitleShadingTablePx),
                   text::kMapTitleShadingTablePx, 0, text::kMapTitleWindowPx, boxTopLeft,
                   boxTopLeft.y);
        return regions;
    }
    const int rows = text::rowsShown(state);
    if (state.textOnly || rows == 0) {
        return regions;
    }
    const int shownTop = text::firstRowShown(state) * text::kWindowCellPx;
    const int shownPx = rows * text::kWindowCellPx;
    if (state.phase == text::WindowPhase::Opening) {
        // Each opening step shades the rows it shows with its own height's table.
        appendRuns(regions, text::windowShading(shownPx), shownPx, 0, shownPx, boxTopLeft,
                   boxTopLeft.y + static_cast<float>(shownTop));
        return regions;
    }
    // Open, or closing: the open box's table, on the rows that remain.
    const int boxPx = text::kDialogWindowRows * text::kWindowCellPx;
    appendRuns(regions, text::windowShading(boxPx), boxPx, shownTop, shownTop + shownPx,
               boxTopLeft, boxTopLeft.y);
    return regions;
}

}  // namespace ostinato::render
