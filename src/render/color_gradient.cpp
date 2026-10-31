#include "render/color_gradient.h"

#include <cstddef>
#include <string>

namespace ostinato::render {

namespace {

// One channel at `level` 31sts of its full strength, rounded to the nearest value.
std::uint8_t channelAtLevel(std::uint8_t full, int level) {
    return static_cast<std::uint8_t>((full * level + kGradientFullLevel / 2) / kGradientFullLevel);
}

bool sameFill(const GradientBand& a, const GradientBand& b) {
    return a.level == b.level && a.blend == b.blend && a.color == b.color;
}

}  // namespace

Regions ColorGradient(std::string_view keyPrefix, retropp::Point topLeft, float width,
                      std::span<const GradientBand> bands) {
    Regions regions;
    int row = 0;
    std::size_t band = 0;
    while (band < bands.size()) {
        const GradientBand& first = bands[band];
        int rows = first.rows;
        std::size_t next = band + 1;
        while (next < bands.size() && sameFill(bands[next], first)) {
            rows += bands[next].rows;
            ++next;
        }
        if (first.level > 0 && rows > 0) {
            regions.push_back(retropp::Region{
                .key = std::string{keyPrefix} + ":" + std::to_string(row),
                .shape = retropp::ShapePoints::rectangle(
                    {.x = topLeft.x, .y = topLeft.y + static_cast<float>(row)}, width,
                    static_cast<float>(rows)),
                .effects = {retropp::ScreenSpaceEffect{
                    .kind = retropp::ScreenSpaceEffectKind::ColorFill,
                    .fill = retropp::Rgba8{.r = channelAtLevel(first.color.r, first.level),
                                           .g = channelAtLevel(first.color.g, first.level),
                                           .b = channelAtLevel(first.color.b, first.level)}}},
                .blend = first.blend,
            });
        }
        row += rows;
        band = next;
    }
    return regions;
}

}  // namespace ostinato::render
