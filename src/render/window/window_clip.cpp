#include "render/window/window_clip.h"

#include "render/layers.h"
#include "text/layout.h"

namespace ostinato::render {

Regions WindowClip(const text::DialogWindowState& state, retropp::Point boxTopLeft) {
    const bool moving = state.phase == text::WindowPhase::Opening ||
                        state.phase == text::WindowPhase::Closing;
    if (state.titleShowing || !moving) {
        return {};
    }
    const auto screenWidth = static_cast<float>(kScreenSize.width);
    const int rows = text::rowsShown(state);
    if (rows == 0) {
        return {retropp::Region{
            .key = kWindowClipKey,
            .shape = retropp::ShapePoints::rectangle(
                {}, screenWidth, static_cast<float>(kScreenSize.height)),
            .effects = {retropp::ScreenSpaceEffect{
                .kind = retropp::ScreenSpaceEffectKind::Transparency}},
        }};
    }
    const float top =
        boxTopLeft.y + static_cast<float>(text::firstRowShown(state) * text::kWindowCellPx);
    Regions regions = retropp::stencil(
        retropp::ShapePoints::rectangle({.x = 0.0f, .y = top}, screenWidth,
                                        static_cast<float>(rows * text::kWindowCellPx)),
        retropp::StencilMode::TransparentOutside);
    for (retropp::Region& region : regions) {
        region.key = kWindowClipKey;
    }
    return regions;
}

}  // namespace ostinato::render
