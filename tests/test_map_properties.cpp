// Full-corpus tests for the map-metadata core: the 415-row MapProperties record,
// the parallax satellite, and the initial-NPC-switch block. Every table is
// checked entry-by-entry against its parser-emitted fixture (the ground-truth
// ROM bytes) via memcmp, independent of the typed rows, so any decode or re-emit
// drift fails loudly. Plus hand-traced packed-group decodes and wrapper reads.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/map_properties.h"

#include "fixtures/init_npc_switch_expected.h"
#include "fixtures/map_parallax_expected.h"
#include "fixtures/map_properties_expected.h"

namespace {

using namespace ostinato;

// --- full-corpus byte-equivalence -------------------------------------------

TEST(MapProperties, MatchesRom) {
    const auto table = mapProperties();
    ASSERT_EQ(table.size(), test::kExpectedMapProperties.size());
    ASSERT_EQ(table.size(), kMapCount);
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& exp = test::kExpectedMapProperties[i];
        EXPECT_EQ(table[i].index, exp.index) << "index at " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, exp.bytes.data(),
                              sizeof(MapProperties)),
                  0)
            << "record bytes at " << i;
    }
}

TEST(MapProperties, ParallaxMatchesRom) {
    const auto table = mapParallax();
    ASSERT_EQ(table.size(), test::kExpectedMapParallax.size());
    ASSERT_EQ(table.size(), kParallaxCount);
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& exp = test::kExpectedMapParallax[i];
        EXPECT_EQ(table[i].index, exp.index) << "index at " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, exp.bytes.data(),
                              sizeof(MapParallax)),
                  0)
            << "record bytes at " << i;
    }
}

TEST(MapProperties, InitialNpcSwitchesMatchRom) {
    const auto sw = initialNpcSwitches();
    ASSERT_EQ(sw.size(), test::kExpectedInitialNpcSwitches.size());
    ASSERT_EQ(sw.size(), kInitialNpcSwitchBytes);
    EXPECT_EQ(std::memcmp(sw.data(), test::kExpectedInitialNpcSwitches.data(),
                          sw.size()),
              0);
}

// --- packed-group decode hand-traces ----------------------------------------
// Map 0 is an all-zero placeholder; map 414 (the last map) carries a full
// 48-bit graphics group and a 30-bit layout group — values hand-traced from
// map_prop.dat.

TEST(MapProperties, GraphicsIdsDecode) {
    const auto& g0 = mapProperties(0).graphics;
    EXPECT_EQ(g0.gfx1(), 0);
    EXPECT_EQ(g0.tileset2(), 0);

    const auto& g = mapProperties(414).graphics;
    EXPECT_EQ(g.gfx1(), 4);
    EXPECT_EQ(g.gfx2(), 5);
    EXPECT_EQ(g.gfx3(), 52);
    EXPECT_EQ(g.gfx4(), 7);
    EXPECT_EQ(g.bg3Gfx(), 8);
    EXPECT_EQ(g.tileset1(), 1);
    EXPECT_EQ(g.tileset2(), 2);
}

TEST(MapProperties, LayoutIdsDecode) {
    const auto& l = mapProperties(414).layouts;
    EXPECT_EQ(l.bg1Layout(), 272);
    EXPECT_EQ(l.bg2Layout(), 273);
    EXPECT_EQ(l.bg3Layout(), 0);  // no bg3 layer on this map
    EXPECT_EQ(l.spareBits(), 0);
}

TEST(MapProperties, BgSizesDecode) {
    const auto& s = mapProperties(414).bgSizes;  // +23 = 0x11, +24 = 0x07
    EXPECT_EQ(s.bg1WidthCode(), 0);
    EXPECT_EQ(s.bg1HeightCode(), 1);
    EXPECT_EQ(s.bg2WidthCode(), 0);
    EXPECT_EQ(s.bg2HeightCode(), 1);
    EXPECT_EQ(s.bg3WidthCode(), 0);
    EXPECT_EQ(s.bg3HeightCode(), 0);
    EXPECT_EQ(s.bg1WidthTiles(), 16);
    EXPECT_EQ(s.bg1HeightTiles(), 32);
    EXPECT_EQ(s.deadFlags(), 7);
    EXPECT_EQ(MapBgSizes::tilesForCode(4), 256);
}

TEST(MapProperties, AnimationIndexesDecode) {
    const auto& a = mapProperties(414).animation;  // +27 = 0x07
    EXPECT_EQ(a.bgAnimIndex(), 7);
    EXPECT_FALSE(a.hasBg3Animation());
}

// --- R-4: effect-flag warp/x-zone bit assignment ----------------------------
// field_menu.asm:2404-2413 tests bit 0 for x-zone and bit 1 for warp.

TEST(MapProperties, EffectFlagsWarpXZone) {
    const MapEffectFlags both{0x03};
    EXPECT_TRUE(both.xZoneEnabled());
    EXPECT_TRUE(both.warpEnabled());

    // Map 32 enables warp (bit 1). No map in the FF3 1.1 ROM sets bit 0, so
    // x-zone is verified only synthetically above.
    const auto& e = mapProperties(32).effectFlags;
    EXPECT_TRUE(e.warpEnabled());
    EXPECT_FALSE(e.xZoneEnabled());
}

TEST(MapProperties, BattleBackgroundDecode) {
    const auto& b4 = mapProperties(4).battleBackground;  // +2 = 0xA6
    EXPECT_EQ(static_cast<std::uint8_t>(b4.background()), 38);
    EXPECT_TRUE(b4.bg3Foreground());

    const auto& b = mapProperties(414).battleBackground;  // +2 = 0x80
    EXPECT_EQ(static_cast<std::uint8_t>(b.background()), 0);
    EXPECT_TRUE(b.bg3Foreground());
}

TEST(MapProperties, ParallaxSignedSpeeds) {
    // Parallax 3 carries a negative bg2 horizontal scroll speed.
    EXPECT_EQ(mapParallax(3).bg2SpeedX, -48);
}

TEST(MapProperties, Accessors) {
    EXPECT_EQ(mapProperties().size(), kMapCount);
    EXPECT_EQ(mapParallax().size(), kParallaxCount);
    EXPECT_EQ(initialNpcSwitches().size(), kInitialNpcSwitchBytes);
    EXPECT_EQ(mapProperties(0).titleIndex, 0);
}

}  // namespace
