// ScreenBorder — the black border around the picture.
//
//     frame.regions = ScreenBorder(kBattleTopBlankRows, kBattleBottomBlankRows);
//
// The picture shows inside a rectangle: kScreenBorderPx columns in from the left and the right edges
// of the screen, `topBlankRows` lines down from the top and `bottomBlankRows` up from the bottom.
// Outside it every layer and every sprite is see-through, so the engine's black backdrop shows
// there. One region, keyed kScreenBorderKey; give it to the frame's `regions`, so it cuts every layer
// at the same edges.
#pragma once

#include "render/types.h"

namespace ostinato::render {

// PRECONDITION (asserted): neither count is negative and together they leave lines to show.
[[nodiscard]] Regions ScreenBorder(int topBlankRows, int bottomBlankRows);

}  // namespace ostinato::render
