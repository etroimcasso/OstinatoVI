// TileLayer — gathers tile cells onto one screen-sized, finite tile layer.
//
//     frame.layers.push_back(TileLayer(kDialogWindowKey, kDialogWindowZ, DialogWindow(...),
//                                      text::kWindowVisibleColumns, rows,
//                                      {.scroll = {-8, -boxTop}, .regions = WindowShading(...)}));
//
// The cells are a row-major grid `widthInTiles` across and `heightInTiles` down. The layer covers
// the whole screen and the grid is finite: outside it the layer shows nothing, so the layers below
// show through. `options.scroll` places the grid — screen pixel (x, y) shows grid pixel
// (x + scroll.x, y + scroll.y), so a scroll of (-8, -16) puts the grid's top-left corner at (8, 16).
// Everything in `options` passes straight onto the layer.
//
// The layer's cells are a view the renderer reads while it draws, so TileLayer keeps its own copy
// under `key` until the next call with that key replaces it — the same lifetime rule as
// SpriteLayer.
#pragma once

#include <cstdint>
#include <string_view>

#include "retropp/draw_state.h"

#include "render/layer_options.h"
#include "render/types.h"

namespace ostinato::render {

[[nodiscard]] retropp::DrawLayer TileLayer(std::string_view key, std::int32_t z, Cells cells,
                                           int widthInTiles, int heightInTiles,
                                           LayerOptions options = {});

}  // namespace ostinato::render
