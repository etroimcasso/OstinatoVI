// SpriteLayer — gathers sprite components onto one screen-sized layer.
//
//     frame.layers.push_back(SpriteLayer("dialogue", kDialogueTextZ,
//                                        {DialogueText(...), DialogueCursor(...)},
//                                        {.regions = DialogueWipe(...)}));
//
// The children are declared as a list and drawn in that order; a sprite's own z orders it within
// the layer before that. Everything in `options` passes straight onto the layer.
//
// The layer's sprites are a view the renderer reads while it draws, so SpriteLayer keeps its own
// copy under `key` until the next call with that key replaces it. Two calls with the same key in
// one frame leave only the second one's sprites behind both layers — and the engine already
// requires a layer's key to be unique within a frame.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "retropp/draw_state.h"

#include "render/layer_options.h"
#include "render/types.h"

namespace ostinato::render {

[[nodiscard]] retropp::DrawLayer SpriteLayer(std::string_view key, std::int32_t z,
                                             std::initializer_list<Sprites> children,
                                             LayerOptions options = {});

}  // namespace ostinato::render
