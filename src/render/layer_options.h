// Everything a layer carries beyond its name, depth, size and content. A function that gathers
// components onto a layer (SpriteLayer) owns those four and passes these through untouched, so a
// caller can scroll, fade, blend, transform, or confine an effect to part of a layer without the
// layer's own function knowing what any of it means. Every default is the engine's own: a layer
// given no options draws exactly as a plain retropp::DrawLayer would.
//
//     SpriteLayer("dialog", kDialogTextZ, {DialogText(...)},
//                 {.alpha = 0.5f, .regions = DialogWipe(...)});
#pragma once

#include <vector>

#include "retropp/draw_state.h"
#include "retropp/transform.h"

namespace ostinato::render {

struct LayerOptions {
    retropp::LayerScroll scroll{};
    float alpha = 1.0f;
    retropp::BlendMode blend = retropp::BlendMode::Normal;
    std::vector<retropp::ScreenSpaceEffect> effects{};
    std::vector<retropp::Region> regions{};
    retropp::Transform transform{};
    retropp::DisplacementEdge transformEdge = retropp::DisplacementEdge::Blank;
};

}  // namespace ostinato::render
