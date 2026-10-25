#include "render/window/dialog_window.h"

#include <cstddef>
#include <span>

#include "text/window_layout.h"

namespace ostinato::render {

namespace {

void appendRow(Cells& cells, const text::WindowFormationRow& row,
               const retropp::AtlasManifest& sheet, retropp::PaletteId palette) {
    for (int c = 0; c < text::kWindowVisibleColumns; ++c) {
        const auto tile = row.tiles[static_cast<std::size_t>(text::kWindowFirstVisibleColumn + c)];
        cells.push_back(retropp::TileCell{
            .atlas = sheet.atlasId,
            .tile = sheet[static_cast<std::size_t>(tile)].tile,
            .palette = palette,
        });
    }
}

}  // namespace

Cells DialogWindow(const text::DialogWindowState& state, const retropp::AtlasManifest& sheet,
                   retropp::PaletteId palette) {
    Cells cells;
    if (state.titleShowing) {
        for (const int row : text::kMapTitleFormationRows) {
            appendRow(cells, text::kDialogWindowFormation[static_cast<std::size_t>(row)], sheet,
                      palette);
        }
        return cells;
    }
    if (state.textOnly || text::rowsShown(state) == 0) {
        return cells;
    }
    for (const text::WindowFormationRow& row : text::kDialogWindowFormation) {
        appendRow(cells, row, sheet, palette);
    }
    return cells;
}

}  // namespace ostinato::render
