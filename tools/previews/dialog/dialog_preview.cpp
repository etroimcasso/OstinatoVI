#include "dialog_preview.h"

#include <string_view>
#include <utility>

#include "render/layers.h"
#include "render/sprite_layer.h"
#include "render/text/dialog_cursor.h"
#include "render/text/dialog_text.h"
#include "render/text/dialog_wipe.h"
#include "render/text/map_title.h"
#include "render/tile_layer.h"
#include "render/window/dialog_window.h"
#include "render/window/window_clip.h"
#include "render/window/window_shading.h"
#include "render/window/window_squash.h"
#include "text/layout.h"
#include "text/window_layout.h"

namespace ostinato::preview {

namespace {

// The text's layer.
constexpr std::string_view kDialogLayerKey = "dialog";

// The backdrop covers the screen in tiles of the window's cell size.
constexpr int kBackdropColumns = render::kScreenSize.width / text::kWindowCellPx;
constexpr int kBackdropRows = render::kScreenSize.height / text::kWindowCellPx;

render::Cells Backdrop(const retropp::AtlasManifest& sheet, retropp::PaletteId color) {
    return render::Cells(static_cast<std::size_t>(kBackdropColumns * kBackdropRows),
                         retropp::TileCell{.atlas = sheet.atlasId,
                                           .tile = sheet[0].tile,
                                           .palette = color});
}

render::Regions joined(render::Regions first, const render::Regions& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

}  // namespace

render::Layers DialogPreviewScreen(const text::DialogState& state,
                                   const text::DialogWindowState& window,
                                   const DialogSheets& sheets) {
    const int x = text::kTextAreaLeft;
    const int y = text::textAreaTop(window);
    const int pitch = text::linePitch(window);
    const retropp::Point boxTopLeft{
        .x = static_cast<float>(text::kWindowFirstVisibleColumn * text::kWindowCellPx),
        .y = static_cast<float>(text::boxTopRow(window) * text::kWindowCellPx)};
    const render::Regions clip = render::WindowClip(window, boxTopLeft);

    render::Cells box = render::DialogWindow(window, sheets.window, sheets.windowColors);
    const int boxRows = static_cast<int>(box.size()) / text::kWindowVisibleColumns;

    return {
        render::TileLayer(kBackdropKey, kBackdropZ,
                          Backdrop(sheets.backdrop, sheets.backdropColor), kBackdropColumns,
                          kBackdropRows),
        render::TileLayer(
            render::kDialogWindowKey, render::kDialogWindowZ, std::move(box),
            text::kWindowVisibleColumns, boxRows,
            {.scroll = {.x = -static_cast<int>(boxTopLeft.x), .y = -static_cast<int>(boxTopLeft.y)},
             .regions = joined(render::WindowShading(window, boxTopLeft), clip),
             .transform = render::WindowSquash(window, boxTopLeft)}),
        render::SpriteLayer(
            kDialogLayerKey, render::kDialogTextZ,
            {render::DialogText(state, x, y, pitch, sheets.glyphs, sheets.ink, sheets.shadow),
             render::DialogCursor(state, x, y, pitch, sheets.arrow, sheets.ink, sheets.shadow),
             render::MapTitle(state, x, text::titleTextAreaTop(), sheets.glyphs, sheets.ink,
                              sheets.shadow)},
            {.regions = joined(render::DialogWipe(state, x, y, pitch), clip)}),
    };
}

}  // namespace ostinato::preview
