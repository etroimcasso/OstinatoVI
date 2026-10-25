#include "render/window/window_squash.h"

#include "text/layout.h"

namespace ostinato::render {

retropp::Transform WindowSquash(const text::DialogWindowState& state, retropp::Point boxTopLeft) {
    if (state.titleShowing || state.textOnly || state.phase != text::WindowPhase::Opening ||
        text::rowsShown(state) == 0) {
        return retropp::Transform::identity();
    }
    const float boxPx = static_cast<float>(text::kDialogWindowRows * text::kWindowCellPx);
    const float shownPx = static_cast<float>(text::rowsShown(state) * text::kWindowCellPx);
    return retropp::Transform::scale(1.0f, shownPx / boxPx, boxTopLeft.x,
                                     boxTopLeft.y + boxPx / 2.0f);
}

}  // namespace ostinato::render
