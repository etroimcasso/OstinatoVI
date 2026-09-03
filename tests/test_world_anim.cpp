// World animation frames: the cartridge region, the decode views over it, and
// the frame-sequence tables that index it.
//
// The sprite arrangements are cartridge content, so there is no stored copy to
// compare against — the expectations here are structural, and the arrangements
// themselves come from the player's cartridge exactly as the game reads them.
// The port-time parser proves every byte of the region against a cartridge when
// it emits; these tests prove the port decodes what is there.

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <span>
#include <vector>

#include "data/rom_regions.h"
#include "data/world_anim.h"
#include "ostinato/rom_asset.h"
#include "ostinato/world_anim_frame_id.h"
#include "ostinato/world_anim_sprite.h"

#include "fixtures/world_anim_expected.h"
#include "vanilla_rom.h"

namespace ostinato {
namespace {

using test::kExpectedSmokingAirshipRowStride;
using test::kExpectedSmokingAirshipSecondLabelAt;
using test::kExpectedWorldAnimBlockBytes;
using test::kExpectedWorldAnimFrameCount;
using test::kExpectedWorldAnimPointerBytes;
using test::kExpectedWorldAnimRegionAt;
using test::kExpectedWorldAnimRegionSize;
using test::kExpectedWorldAnimSurplusFrames;

WorldAnimFrameId frameId(std::size_t index) {
    return static_cast<WorldAnimFrameId>(index);
}

// The cartridge, read once for the whole suite.
class WorldAnimTest : public ::testing::Test {
protected:
    inline static test::IngestedCartridge cartridge_;

    static void SetUpTestSuite() { cartridge_ = test::ingestVanilla(); }

    // The region's bytes, taken straight out of the image.
    static std::vector<std::uint8_t> region() {
        return test::romSlice(cartridge_.image, RomAsset::WORLD_ANIM_FRAMES);
    }
};

// --- the region row ------------------------------------------------------------

TEST_F(WorldAnimTest, RegionCoversBothBlocksAsOneExtent) {
    const auto place = romRegion(RomAsset::WORLD_ANIM_FRAMES, Language::EN);
    ASSERT_TRUE(place.has_value());
    EXPECT_EQ(place->at, kExpectedWorldAnimRegionAt);
    EXPECT_EQ(place->size, kExpectedWorldAnimRegionSize);
    EXPECT_EQ(place->count, 1u);

    // The pointer table and the records it addresses are contiguous, so one
    // extent serves both.
    EXPECT_EQ(kExpectedWorldAnimPointerBytes + kExpectedWorldAnimBlockBytes,
              kExpectedWorldAnimRegionSize);
    EXPECT_EQ(kExpectedWorldAnimPointerBytes,
              kExpectedWorldAnimFrameCount * kWorldAnimPointerBytes);
}

// The Japanese build shifts every address in this bank by an amount that only
// assembling the source would settle, and no Japanese cartridge is available to
// check a derived one against. The row is left out rather than guessed: a caller
// asking for it is told the family has no address in that language.
TEST_F(WorldAnimTest, NoJapaneseAddressIsClaimed) {
    EXPECT_FALSE(romRegion(RomAsset::WORLD_ANIM_FRAMES, Language::JP).has_value());
}

TEST_F(WorldAnimTest, RegionReadsFromTheCartridge) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const auto bytes = region();
    ASSERT_EQ(bytes.size(), kExpectedWorldAnimRegionSize);

    const WorldAnimFrames frames{bytes};
    ASSERT_TRUE(frames.valid());
    EXPECT_EQ(frames.pointerTable().size(), kExpectedWorldAnimPointerBytes);
    EXPECT_EQ(frames.records().size(), kExpectedWorldAnimBlockBytes);
}

TEST_F(WorldAnimTest, AnEmptyOrShortSpanYieldsNothing) {
    EXPECT_FALSE(WorldAnimFrames{}.valid());
    EXPECT_FALSE(WorldAnimFrames{std::span<const std::uint8_t>{}}.valid());

    const std::vector<std::uint8_t> tooShort(kExpectedWorldAnimPointerBytes, 0);
    EXPECT_FALSE(WorldAnimFrames{tooShort}.valid());

    EXPECT_FALSE(WorldAnimFrame{}.valid());
    EXPECT_EQ(WorldAnimFrame{}.spriteCount(), 0);
    EXPECT_EQ(WorldAnimFrame{}.storedRows(), 0u);
}

// --- every frame ---------------------------------------------------------------

TEST_F(WorldAnimTest, EveryFrameDecodesAsCountThenWholeSpriteRows) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const auto bytes = region();
    ASSERT_EQ(bytes.size(), kExpectedWorldAnimRegionSize);
    const WorldAnimFrames frames{bytes};
    ASSERT_TRUE(frames.valid());

    for (std::size_t index = 0; index < kWorldAnimFrameCount; ++index) {
        const auto frame = frames.frameAt(frameId(index));
        ASSERT_TRUE(frame.valid()) << "frame " << index << " decoded to nothing";

        // A record is a count byte plus whole sprite rows, and it never claims
        // more sprites than it stores — the consumer would read past it.
        EXPECT_EQ((frame.bytes().size() - 1) % sizeof(WorldAnimSprite), 0u)
            << "frame " << index << " is not whole sprite rows";
        EXPECT_LE(frame.spriteCount(), frame.storedRows())
            << "frame " << index << " declares more sprites than it stores";

        // The record lies wholly inside the block.
        EXPECT_LE(frames.offsetOf(frameId(index)) + frame.bytes().size(),
                  frames.records().size())
            << "frame " << index << " runs past the records";
    }
}

TEST_F(WorldAnimTest, RecordsTileTheBlockWithNoGap) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const auto bytes = region();
    ASSERT_EQ(bytes.size(), kExpectedWorldAnimRegionSize);
    const WorldAnimFrames frames{bytes};
    ASSERT_TRUE(frames.valid());

    // Walking the distinct offsets in order, each record ends where the next
    // begins and the last ends at the block's end — so every byte of the block
    // belongs to exactly one record.
    std::set<std::uint16_t> offsets;
    for (std::size_t index = 0; index < kWorldAnimFrameCount; ++index) {
        offsets.insert(frames.offsetOf(frameId(index)));
    }

    std::size_t walked = 0;
    for (const std::uint16_t offset : offsets) {
        EXPECT_EQ(offset, walked) << "a gap or overlap before offset " << offset;
        for (std::size_t index = 0; index < kWorldAnimFrameCount; ++index) {
            if (frames.offsetOf(frameId(index)) == offset) {
                walked += frames.frameAt(frameId(index)).bytes().size();
                break;
            }
        }
    }
    EXPECT_EQ(walked, kExpectedWorldAnimBlockBytes);
}

// --- the quirks the cartridge carries ------------------------------------------

// The blank frame is a label with no bytes of its own sitting at the start of
// the records, so it and the first airship frame share an offset of zero. Both
// rows are kept: the pointer table has one entry per frame either way, and the
// draw routines never reach the table for the blank frame because they skip an
// object showing it.
TEST_F(WorldAnimTest, TheBlankFrameSharesTheFirstRecordsOffset) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const auto bytes = region();
    ASSERT_EQ(bytes.size(), kExpectedWorldAnimRegionSize);
    const WorldAnimFrames frames{bytes};
    ASSERT_TRUE(frames.valid());

    EXPECT_EQ(frames.offsetOf(WorldAnimFrameId::FRAME_0), 0);
    EXPECT_EQ(frames.offsetOf(WorldAnimFrameId::FRAME_1), 0);
}

// Two of the esper frames are one record under two names, so their offsets are
// equal and neither is deduplicated.
TEST_F(WorldAnimTest, TheAliasedFramesShareOneRecord) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const auto bytes = region();
    ASSERT_EQ(bytes.size(), kExpectedWorldAnimRegionSize);
    const WorldAnimFrames frames{bytes};
    ASSERT_TRUE(frames.valid());

    const auto first = frames.frameAt(WorldAnimFrameId::FRAME_90);
    const auto second = frames.frameAt(WorldAnimFrameId::FRAME_91);
    EXPECT_EQ(frames.offsetOf(WorldAnimFrameId::FRAME_90),
              frames.offsetOf(WorldAnimFrameId::FRAME_91));
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(first.bytes().data(), second.bytes().data());
    EXPECT_EQ(first.bytes().size(), second.bytes().size());
    EXPECT_EQ(first.spriteCount(), 1);
}

// Eight records store sprite rows past the count they declare. The port keeps
// them and does not draw them, which is what the original does.
TEST_F(WorldAnimTest, TheSurplusRowFramesAreCarriedAndNotDrawn) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const auto bytes = region();
    ASSERT_EQ(bytes.size(), kExpectedWorldAnimRegionSize);
    const WorldAnimFrames frames{bytes};
    ASSERT_TRUE(frames.valid());

    for (const auto& expected : kExpectedWorldAnimSurplusFrames) {
        const auto frame = frames.frameAt(frameId(expected.frame));
        ASSERT_TRUE(frame.valid()) << "frame " << int{expected.frame};
        EXPECT_EQ(frame.spriteCount(), expected.declaredSprites)
            << "frame " << int{expected.frame};
        EXPECT_EQ(frame.storedRows(), expected.storedRows)
            << "frame " << int{expected.frame};
        EXPECT_GT(frame.storedRows(), frame.spriteCount())
            << "frame " << int{expected.frame};
    }

    // No other frame stores rows it does not declare.
    std::size_t surplus = 0;
    for (std::size_t index = 0; index < kWorldAnimFrameCount; ++index) {
        const auto frame = frames.frameAt(frameId(index));
        if (frame.storedRows() > frame.spriteCount()) {
            ++surplus;
        }
    }
    EXPECT_EQ(surplus, kExpectedWorldAnimSurplusFrames.size());
}

// --- one record decoded by hand ------------------------------------------------

// The airship's first frame, traced against the cartridge: twelve sprites in
// three columns of four rows, the right-hand column mirrored.
TEST_F(WorldAnimTest, TheFirstAirshipFrameDecodesAsTraced) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const auto bytes = region();
    ASSERT_EQ(bytes.size(), kExpectedWorldAnimRegionSize);
    const WorldAnimFrames frames{bytes};
    ASSERT_TRUE(frames.valid());

    const auto frame = frames.frameAt(WorldAnimFrameId::FRAME_1);
    ASSERT_TRUE(frame.valid());
    EXPECT_EQ(frame.spriteCount(), 12);
    EXPECT_EQ(frame.storedRows(), 12u);

    const auto first = frame.sprite(0);
    EXPECT_EQ(first.offsetX(), -12);
    EXPECT_EQ(first.offsetY(), -16);
    EXPECT_EQ(first.tileIndex(), 0x40);
    EXPECT_EQ(first.paletteIndex(), 0);
    EXPECT_EQ(first.layerPriority(), 1);
    EXPECT_FALSE(first.flippedHorizontally());
    EXPECT_FALSE(first.flippedVertically());

    // The third sprite of each row mirrors the first: same tile, flipped.
    const auto mirrored = frame.sprite(2);
    EXPECT_EQ(mirrored.tileIndex(), first.tileIndex());
    EXPECT_EQ(mirrored.offsetY(), first.offsetY());
    EXPECT_TRUE(mirrored.flippedHorizontally());
    EXPECT_FALSE(mirrored.flippedVertically());

    // Reading past the stored rows yields a zeroed sprite rather than the next
    // record's bytes.
    const auto past = frame.sprite(frame.storedRows());
    EXPECT_EQ(past.tileIndex(), 0);
    EXPECT_EQ(past.attributes, 0);
}

// The attribute byte's fields are packed as vhoopppm, so every bit has to come
// back out of a byte with all of them set.
TEST_F(WorldAnimTest, TheAttributeByteUnpacksEveryField) {
    const WorldAnimSprite sprite{
        .x = 0x80, .y = 0x7F, .tileLow = 0xAB, .attributes = 0xFF};
    EXPECT_EQ(sprite.offsetX(), -128);
    EXPECT_EQ(sprite.offsetY(), 127);
    EXPECT_EQ(sprite.tileIndex(), 0x1AB);
    EXPECT_EQ(sprite.paletteIndex(), 7);
    EXPECT_EQ(sprite.layerPriority(), 3);
    EXPECT_TRUE(sprite.flippedHorizontally());
    EXPECT_TRUE(sprite.flippedVertically());

    const WorldAnimSprite clear{
        .x = 0, .y = 0, .tileLow = 0xAB, .attributes = 0x00};
    EXPECT_EQ(clear.tileIndex(), 0xAB);
    EXPECT_EQ(clear.paletteIndex(), 0);
    EXPECT_EQ(clear.layerPriority(), 0);
    EXPECT_FALSE(clear.flippedHorizontally());
    EXPECT_FALSE(clear.flippedVertically());
}

// --- the frame-sequence tables -------------------------------------------------

TEST_F(WorldAnimTest, EverySequenceStepMatchesTheFixture) {
    const auto compare = [](std::span<const WorldAnimFrameStep> table,
                            auto&& expected, const char* name) {
        ASSERT_EQ(table.size(), expected.size()) << name;
        for (std::size_t i = 0; i < table.size(); ++i) {
            EXPECT_EQ(table[i].index, expected[i].index) << name << '[' << i << ']';
            EXPECT_EQ(static_cast<std::uint8_t>(table[i].frame), expected[i].frame)
                << name << '[' << i << ']';
        }
    };

    compare(dismountChocoboFrames(), test::kExpectedWorldAnimDismountChocoboFrames,
            "dismount chocobo");
    compare(smokingAirshipFrames(), test::kExpectedWorldAnimSmokingAirshipFrames,
            "smoking airship");
    compare(birdFrames(), test::kExpectedWorldAnimBirdFrames, "bird");
}

TEST_F(WorldAnimTest, EverySequenceStepNamesAFrameThatExists) {
    for (const auto table : {dismountChocoboFrames(), smokingAirshipFrames(),
                             birdFrames()}) {
        for (const auto& step : table) {
            EXPECT_LT(static_cast<std::size_t>(step.frame), kWorldAnimFrameCount);
        }
    }
}

// The airship's altitude picks a row of six, but the step within a row runs one
// wider than the row — so each row's overrun lands on the next row's leading
// blank frame, and the table's last entry serves the final row's.
TEST_F(WorldAnimTest, EverySmokingAirshipRowBeginsAndOverrunsOnTheBlankFrame) {
    const auto table = smokingAirshipFrames();
    ASSERT_EQ(table.size() % kSmokingAirshipFrameRowStride, 1u);
    EXPECT_EQ(kSmokingAirshipFrameRowStride, kExpectedSmokingAirshipRowStride);
    EXPECT_EQ(kSmokingAirshipSecondLabelStep, kExpectedSmokingAirshipSecondLabelAt);

    for (std::size_t step = 0; step < table.size();
         step += kSmokingAirshipFrameRowStride) {
        EXPECT_EQ(table[step].frame, WorldAnimFrameId::FRAME_0)
            << "row starting at step " << step << " does not open blank";
    }
    EXPECT_EQ(table.back().frame, WorldAnimFrameId::FRAME_0);

    // The second cartridge label falls on a row boundary, which is what makes
    // the two labels one table rather than two.
    EXPECT_EQ(kSmokingAirshipSecondLabelStep % kSmokingAirshipFrameRowStride, 0u);
}

// The sequences step through frames that belong to the group they animate: the
// dismount cycle stays inside the dismounting-chocobo frames, the bird inside
// the bird frames, and the airship rows inside the smoking-airship frames.
TEST_F(WorldAnimTest, EachSequenceStaysInsideItsOwnFrameGroup) {
    for (const auto& step : dismountChocoboFrames()) {
        EXPECT_GE(static_cast<std::size_t>(step.frame), 73u);
        EXPECT_LE(static_cast<std::size_t>(step.frame), 76u);
    }
    for (const auto& step : birdFrames()) {
        EXPECT_GE(static_cast<std::size_t>(step.frame), 95u);
        EXPECT_LE(static_cast<std::size_t>(step.frame), 97u);
    }
    for (const auto& step : smokingAirshipFrames()) {
        if (step.frame == WorldAnimFrameId::FRAME_0) {
            continue;
        }
        EXPECT_GE(static_cast<std::size_t>(step.frame), 86u);
        EXPECT_LE(static_cast<std::size_t>(step.frame), 89u);
    }
}

}  // namespace
}  // namespace ostinato
