// What the rendering components return. A component is a function of the state it draws, and it
// returns the engine primitives it is: a leaf or a group of leaves returns its Sprites, Cells or Regions,
// a function that gathers components onto a layer returns a DrawLayer, and a screen returns its
// Layers. Nothing is drawn into anything — the frame is the list of what these return.
#pragma once

#include <vector>

#include "retropp/draw_state.h"

namespace ostinato::render {

using Sprites = std::vector<retropp::Sprite>;
using Cells = std::vector<retropp::TileCell>;
using Regions = std::vector<retropp::Region>;
using Layers = std::vector<retropp::DrawLayer>;

}  // namespace ostinato::render
