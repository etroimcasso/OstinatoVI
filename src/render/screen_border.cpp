#include "render/screen_border.h"

#include <cassert>

#include "retropp/draw_state.h"

#include "render/layers.h"

namespace ostinato::render {

Regions ScreenBorder(int topBlankRows, int bottomBlankRows) {
    assert(topBlankRows >= 0 && bottomBlankRows >= 0 && "the blank line counts are not negative");
    assert(topBlankRows + bottomBlankRows < kScreenSize.height && "the border leaves lines to show");
    Regions border = retropp::stencil(
        retropp::ShapePoints::rectangle(
            {.x = static_cast<float>(kScreenBorderPx), .y = static_cast<float>(topBlankRows)},
            static_cast<float>(kScreenSize.width - 2 * kScreenBorderPx),
            static_cast<float>(kScreenSize.height - topBlankRows - bottomBlankRows)),
        retropp::StencilMode::TransparentOutside);
    for (retropp::Region& region : border) {
        region.key = kScreenBorderKey;
    }
    return border;
}

}  // namespace ostinato::render
