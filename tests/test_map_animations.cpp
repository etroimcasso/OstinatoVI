// Full-corpus tests for the map animation tables: palette animation, the bg1/bg2
// pointer-table stream, and the bg3 records. Each table is checked against its
// parser-emitted fixture via memcmp, plus decode hand-traces and the two
// preserved quirks: the 11/12/17/19 alias (Q1) and index 7's short body that the
// consumer over-reads into the next one (Q2).
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/map_animations.h"

#include "fixtures/map_bg3_anim_expected.h"
#include "fixtures/map_bg_anim_expected.h"
#include "fixtures/map_pal_anim_expected.h"

namespace {

using namespace ostinato;

// --- palette animation ------------------------------------------------------

TEST(MapAnimations, PaletteAnimationMatchesRom) {
    const auto table = mapPaletteAnimations();
    ASSERT_EQ(table.size(), test::kExpectedMapPaletteAnimation.size());
    ASSERT_EQ(table.size(), kPaletteAnimationCount);
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& exp = test::kExpectedMapPaletteAnimation[i];
        EXPECT_EQ(table[i].index, exp.index) << "index at " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, exp.bytes.data(),
                              sizeof(MapPaletteAnimation)),
                  0)
            << "record bytes at " << i;
    }
}

TEST(MapAnimations, PaletteAnimationControlDecode) {
    const auto& slot = mapPaletteAnimation(0).slots[0];  // control 0x11
    EXPECT_EQ(slot.control.type(), PaletteAnimationType::CYCLE);
    EXPECT_EQ(slot.control.stepCount(), 1);
    EXPECT_FALSE(slot.control.disabled());
    EXPECT_EQ(slot.frameDuration, 4);
    EXPECT_EQ(slot.colorOffset, 136);
    EXPECT_EQ(slot.colorByteCount, 4);
    EXPECT_EQ(slot.romColorOffset, 0);
}

// --- bg1/bg2 animation stream + offsets -------------------------------------

TEST(MapAnimations, BgAnimationStreamMatchesRom) {
    const auto stream = bgAnimationStream();
    ASSERT_EQ(stream.size(), test::kExpectedBgAnimationStream.size());
    EXPECT_EQ(std::memcmp(stream.data(),
                          test::kExpectedBgAnimationStream.data(), stream.size()),
              0);
}

TEST(MapAnimations, BgAnimationOffsetsMatchRom) {
    const auto offs = bgAnimationOffsets();
    ASSERT_EQ(offs.size(), test::kExpectedBgAnimationOffsets.size());
    ASSERT_EQ(offs.size(), kBgAnimationCount + 1);
    for (std::size_t i = 0; i < offs.size(); ++i) {
        EXPECT_EQ(offs[i].index, i) << "index at " << i;
        EXPECT_EQ(offs[i].offset, test::kExpectedBgAnimationOffsets[i])
            << "offset at " << i;
    }
}

TEST(MapAnimations, BgAnimationAliasQuirk) {
    // Q1: animation indexes 11/12/17/19 alias one body -> identical offsets.
    const auto offs = bgAnimationOffsets();
    EXPECT_EQ(offs[11].offset, offs[12].offset);
    EXPECT_EQ(offs[11].offset, offs[17].offset);
    EXPECT_EQ(offs[11].offset, offs[19].offset);
}

TEST(MapAnimations, BgAnimationOverReadQuirk) {
    // Q2: index 7's body is one sub-record shorter than the fixed 8 the consumer
    // reads, so the read spills into index 8's body. Storage stays contiguous so
    // that over-read is reproducible.
    const auto offs = bgAnimationOffsets();
    EXPECT_EQ(offs[8].offset - offs[7].offset, 2u * sizeof(BgAnimationFrameSet));
    EXPECT_LT(offs[8].offset - offs[7].offset,
              kBgAnimationSubRecordsRead * sizeof(BgAnimationFrameSet));
}

TEST(MapAnimations, BgAnimationFrameSetDecode) {
    // First sub-record of index 0: stream bytes 00 01 00 00 80 00 00 01 80 01.
    const auto fs = bgAnimationFrameSet(bgAnimationOffsets()[0].offset);
    EXPECT_EQ(fs.animSpeed, 0x0100);
    EXPECT_EQ(fs.frames[0], 0x0000);
    EXPECT_EQ(fs.frames[1], 0x0080);
    EXPECT_EQ(fs.frames[2], 0x0100);
    EXPECT_EQ(fs.frames[3], 0x0180);
}

// --- bg3 animation ----------------------------------------------------------

TEST(MapAnimations, Bg3AnimationMatchesRom) {
    const auto table = bg3Animations();
    ASSERT_EQ(table.size(), test::kExpectedBg3Animation.size());
    ASSERT_EQ(table.size(), kBg3AnimationCount);
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& exp = test::kExpectedBg3Animation[i];
        EXPECT_EQ(table[i].index, exp.index) << "index at " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, exp.bytes.data(),
                              sizeof(Bg3AnimationRecord)),
                  0)
            << "record bytes at " << i;
    }
}

TEST(MapAnimations, Bg3AnimationDecode) {
    const auto& r = bg3Animation(0);
    EXPECT_EQ(r.animSpeed, 0x0100);
    EXPECT_EQ(r.gfxSize, 0x0380);
    EXPECT_EQ(r.frames[0], 0x0000);
    EXPECT_EQ(r.frames[1], 0x0380);
}

}  // namespace
