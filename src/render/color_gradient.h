// ColorGradient — horizontal bands of color laid over a rectangle, top to bottom.
//
//     const std::array<GradientBand, 3> bands{{
//         {.rows = 8, .level = 7, .blend = retropp::BlendMode::Add},
//         {.rows = 4, .level = 0, .blend = retropp::BlendMode::Add},
//         {.rows = 8, .level = 7, .blend = retropp::BlendMode::Subtract},
//     }};
//     TileLayer(key, z, cells, columns, rows, {.regions = ColorGradient("glow", {8, 16}, 240, bands)});
//
// Each band covers `rows` pixel rows, the first band starting at `topLeft` and each next band
// directly below the one before. A band's fill is its `color` at `level` 31sts of full strength on
// each channel — white by default, so a level is a gray — combined with what lies beneath under the
// band's `blend`: Add lightens, Subtract darkens. A band at level 0 adds or takes away nothing and
// gets no region.
//
// Neighboring bands with the same level, color and blend come back as one region, keyed
// `<keyPrefix>:<row>` where `row` is the region's first pixel row counted from `topLeft`. A band
// that is not shown still counts its rows, so leading level-0 bands push the rest down and keep
// their keys. Give the result to a layer's `regions` to shade that layer alone, or to the frame's to
// shade everything beneath it.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "retropp/draw_state.h"
#include "retropp/palette.h"

#include "render/types.h"

namespace ostinato::render {

// The strongest level a band can have: its color at full strength.
inline constexpr int kGradientFullLevel = 31;

// One band of a gradient: how many pixel rows it covers, how strongly it is filled, how the fill
// combines with what lies beneath, and the fill's color at full strength.
struct GradientBand {
    int rows;
    std::uint8_t level;
    retropp::BlendMode blend;
    retropp::Rgba8 color = {.r = 255, .g = 255, .b = 255};
};

[[nodiscard]] Regions ColorGradient(std::string_view keyPrefix, retropp::Point topLeft, float width,
                                    std::span<const GradientBand> bands);

}  // namespace ostinato::render
