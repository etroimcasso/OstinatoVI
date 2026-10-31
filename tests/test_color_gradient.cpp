// ColorGradient read back from the regions it returns: where each band lands, its fill and blend,
// the runs neighboring bands merge into, and the keys those runs carry.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "retropp/draw_state.h"
#include "retropp/palette.h"

#include "render/color_gradient.h"

namespace ostinato::render {
namespace {

using retropp::BlendMode;

// The rectangle a region covers: its top-left corner and its size.
struct Rect {
    float x;
    float y;
    float width;
    float height;
};

Rect rectOf(const retropp::Region& region) {
    EXPECT_EQ(region.shape.points.size(), 4u);
    const retropp::Point topLeft = region.shape.points[0];
    const retropp::Point bottomRight = region.shape.points[2];
    return {.x = topLeft.x,
            .y = topLeft.y,
            .width = bottomRight.x - topLeft.x,
            .height = bottomRight.y - topLeft.y};
}

retropp::Rgba8 fillOf(const retropp::Region& region) {
    EXPECT_EQ(region.effects.size(), 1u);
    EXPECT_EQ(region.effects[0].kind, retropp::ScreenSpaceEffectKind::ColorFill);
    return region.effects[0].fill;
}

TEST(ColorGradient, OneBandIsOneRectangleOfItsRowsAtTheTopLeft) {
    const std::array<GradientBand, 1> bands{{{.rows = 6, .level = 7, .blend = BlendMode::Add}}};
    const Regions regions = ColorGradient("g", {.x = 8.0f, .y = 20.0f}, 240.0f, bands);
    ASSERT_EQ(regions.size(), 1u);
    const Rect rect = rectOf(regions[0]);
    EXPECT_EQ(rect.x, 8.0f);
    EXPECT_EQ(rect.y, 20.0f);
    EXPECT_EQ(rect.width, 240.0f);
    EXPECT_EQ(rect.height, 6.0f);
    EXPECT_EQ(regions[0].blend, BlendMode::Add);
}

// A level is that many 31sts of full strength on each channel, rounded to the nearest value.
TEST(ColorGradient, ALevelIsAGrayIn31sts) {
    const std::array<GradientBand, 4> bands{{
        {.rows = 1, .level = 1, .blend = BlendMode::Add},
        {.rows = 1, .level = 7, .blend = BlendMode::Add},
        {.rows = 1, .level = 16, .blend = BlendMode::Add},
        {.rows = 1, .level = 31, .blend = BlendMode::Add},
    }};
    const Regions regions = ColorGradient("g", {}, 16.0f, bands);
    ASSERT_EQ(regions.size(), 4u);
    const std::array<std::uint8_t, 4> expected{8, 58, 132, 255};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const retropp::Rgba8 fill = fillOf(regions[i]);
        EXPECT_EQ(fill.r, expected[i]) << i;
        EXPECT_EQ(fill.g, expected[i]) << i;
        EXPECT_EQ(fill.b, expected[i]) << i;
    }
}

TEST(ColorGradient, AColoredBandScalesEachChannelOfItsColor) {
    const std::array<GradientBand, 1> bands{{{.rows = 2,
                                              .level = 16,
                                              .blend = BlendMode::Subtract,
                                              .color = {.r = 255, .g = 0, .b = 128}}}};
    const Regions regions = ColorGradient("g", {}, 16.0f, bands);
    ASSERT_EQ(regions.size(), 1u);
    const retropp::Rgba8 fill = fillOf(regions[0]);
    EXPECT_EQ(fill.r, 132);
    EXPECT_EQ(fill.g, 0);
    EXPECT_EQ(fill.b, 66);
    EXPECT_EQ(regions[0].blend, BlendMode::Subtract);
}

// Neighbors with the same level, blend and color are one region as tall as all of them.
TEST(ColorGradient, NeighborsThatMatchMergeIntoOneRun) {
    const std::array<GradientBand, 4> bands{{
        {.rows = 1, .level = 5, .blend = BlendMode::Add},
        {.rows = 2, .level = 5, .blend = BlendMode::Add},
        {.rows = 3, .level = 5, .blend = BlendMode::Add},
        {.rows = 1, .level = 4, .blend = BlendMode::Add},
    }};
    const Regions regions = ColorGradient("g", {.x = 0.0f, .y = 10.0f}, 16.0f, bands);
    ASSERT_EQ(regions.size(), 2u);
    EXPECT_EQ(rectOf(regions[0]).y, 10.0f);
    EXPECT_EQ(rectOf(regions[0]).height, 6.0f);
    EXPECT_EQ(rectOf(regions[1]).y, 16.0f);
    EXPECT_EQ(rectOf(regions[1]).height, 1.0f);
}

// The same level under a different blend or in a different color starts a new run.
TEST(ColorGradient, ADifferentBlendOrColorSplitsARun) {
    const std::array<GradientBand, 3> bands{{
        {.rows = 2, .level = 5, .blend = BlendMode::Add},
        {.rows = 2, .level = 5, .blend = BlendMode::Subtract},
        {.rows = 2, .level = 5, .blend = BlendMode::Subtract, .color = {.r = 255}},
    }};
    const Regions regions = ColorGradient("g", {}, 16.0f, bands);
    ASSERT_EQ(regions.size(), 3u);
    EXPECT_EQ(regions[0].blend, BlendMode::Add);
    EXPECT_EQ(regions[1].blend, BlendMode::Subtract);
    EXPECT_EQ(fillOf(regions[1]).g, fillOf(regions[1]).r);
    EXPECT_EQ(fillOf(regions[2]).g, 0);
}

// A level-0 band gets no region, but its rows still push the bands below it down.
TEST(ColorGradient, ALevelZeroBandIsNotShownButTakesItsRows) {
    const std::array<GradientBand, 3> bands{{
        {.rows = 16, .level = 0, .blend = BlendMode::Add},
        {.rows = 4, .level = 3, .blend = BlendMode::Add},
        {.rows = 4, .level = 0, .blend = BlendMode::Subtract},
    }};
    const Regions regions = ColorGradient("g", {.x = 0.0f, .y = 100.0f}, 16.0f, bands);
    ASSERT_EQ(regions.size(), 1u);
    EXPECT_EQ(rectOf(regions[0]).y, 116.0f);
    EXPECT_EQ(rectOf(regions[0]).height, 4.0f);
}

// A run is keyed by its prefix and the first row it covers, counted from the top-left.
TEST(ColorGradient, ARunIsKeyedByItsFirstRow) {
    const std::array<GradientBand, 4> bands{{
        {.rows = 3, .level = 0, .blend = BlendMode::Add},
        {.rows = 2, .level = 6, .blend = BlendMode::Add},
        {.rows = 1, .level = 6, .blend = BlendMode::Add},
        {.rows = 4, .level = 2, .blend = BlendMode::Subtract},
    }};
    const Regions regions = ColorGradient("shade", {.x = 0.0f, .y = 50.0f}, 16.0f, bands);
    ASSERT_EQ(regions.size(), 2u);
    EXPECT_EQ(regions[0].key, retropp::ObjectKey{"shade:3"});
    EXPECT_EQ(regions[1].key, retropp::ObjectKey{"shade:6"});
}

TEST(ColorGradient, NoBandsOrNoRowsGiveNothing) {
    EXPECT_TRUE(ColorGradient("g", {}, 16.0f, std::span<const GradientBand>{}).empty());
    const std::array<GradientBand, 1> empty{{{.rows = 0, .level = 7, .blend = BlendMode::Add}}};
    EXPECT_TRUE(ColorGradient("g", {}, 16.0f, empty).empty());
}

}  // namespace
}  // namespace ostinato::render
