#include "render/text/dialogue_wipe.h"

#include <string>

#include "render/layers.h"
#include "text/layout.h"

namespace ostinato::render {

namespace {

// Half-lines in a line: the left one, then the right one.
constexpr int kHalvesPerLine = text::kWipeSteps / text::kLinesPerPage;

}  // namespace

Regions DialogueWipe(const text::DialogueState& state, int x, int y) {
    Regions regions;
    const int gone = text::halfLinesCleared(state);
    regions.reserve(static_cast<std::size_t>(gone));
    for (int halfLine = 0; halfLine < gone; ++halfLine) {
        const int line = halfLine / kHalvesPerLine;
        const int half = halfLine % kHalvesPerLine;
        const retropp::Point topLeft{
            .x = static_cast<float>(x + half * text::kHalfLinePx),
            .y = static_cast<float>(y + line * text::kLineHeight)};
        regions.push_back(retropp::Region{
            .key = std::string{kDialogueWipeKey} + ":" + std::to_string(line) + ":" +
                   std::to_string(half),
            .shape = retropp::ShapePoints::rectangle(topLeft,
                                                     static_cast<float>(text::kHalfLinePx),
                                                     static_cast<float>(text::kLineHeight)),
            .effects = {retropp::ScreenSpaceEffect{
                .kind = retropp::ScreenSpaceEffectKind::Transparency}},
        });
    }
    return regions;
}

}  // namespace ostinato::render
