// Full-corpus tests for the map trigger family: treasures and long/short
// entrances. Every record is compared against its generated fixture (the
// original ROM bytes) via memcmp, independent of the typed rows, so any decode
// or re-emit drift fails loudly. The per-map offset tables are checked
// entry-by-entry (typed identity + record offset), and the record wrappers are
// decoded against independent re-derivations from the raw bytes.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/map_properties.h"  // kMapCount, kMapAddressSpace
#include "data/map_triggers.h"

#include "fixtures/long_entrance_expected.h"
#include "fixtures/short_entrance_expected.h"
#include "fixtures/treasure_expected.h"

namespace {

using namespace ostinato;

// --- full-corpus byte-equivalence -------------------------------------------

TEST(MapTriggers, TreasureRecordsMatchRom) {
    const auto records = treasureRecords();
    ASSERT_EQ(records.size(), test::kExpectedTreasureRecords.size());
    ASSERT_EQ(records.size(), kTreasureRecordCount);
    for (std::size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(std::memcmp(&records[i],
                              test::kExpectedTreasureRecords[i].bytes.data(),
                              sizeof(TreasureProperty)),
                  0)
            << "treasure record bytes at " << i;
    }
}

TEST(MapTriggers, LongEntranceRecordsMatchRom) {
    const auto records = longEntranceRecords();
    ASSERT_EQ(records.size(), test::kExpectedLongEntranceRecords.size());
    ASSERT_EQ(records.size(), kLongEntranceRecordCount);
    for (std::size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(std::memcmp(&records[i],
                              test::kExpectedLongEntranceRecords[i].bytes.data(),
                              sizeof(LongEntrance)),
                  0)
            << "long entrance record bytes at " << i;
    }
}

TEST(MapTriggers, ShortEntranceRecordsMatchRom) {
    const auto records = shortEntranceRecords();
    ASSERT_EQ(records.size(), test::kExpectedShortEntranceRecords.size());
    ASSERT_EQ(records.size(), kShortEntranceRecordCount);
    for (std::size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(std::memcmp(&records[i],
                              test::kExpectedShortEntranceRecords[i].bytes.data(),
                              sizeof(ShortEntrance)),
                  0)
            << "short entrance record bytes at " << i;
    }
}

// --- offset tables: typed identity + record offset --------------------------

TEST(MapTriggers, TreasureOffsetsMatchRom) {
    const auto offsets = treasureOffsets();
    ASSERT_EQ(offsets.size(), test::kExpectedTreasureOffsets.size());
    ASSERT_EQ(offsets.size(), kMapCount + 1);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        EXPECT_EQ(offsets[i].index, i) << "treasure offset index at " << i;
        EXPECT_EQ(offsets[i].offset, test::kExpectedTreasureOffsets[i])
            << "treasure offset value at " << i;
    }
    EXPECT_EQ(offsets.back().offset, kTreasureRecordCount);
}

TEST(MapTriggers, LongEntranceOffsetsMatchRom) {
    const auto offsets = longEntranceOffsets();
    ASSERT_EQ(offsets.size(), test::kExpectedLongEntranceOffsets.size());
    ASSERT_EQ(offsets.size(), kMapAddressSpace + 1);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        EXPECT_EQ(offsets[i].index, i) << "long entrance offset index at " << i;
        EXPECT_EQ(offsets[i].offset, test::kExpectedLongEntranceOffsets[i])
            << "long entrance offset value at " << i;
    }
    EXPECT_EQ(offsets.back().offset, kLongEntranceRecordCount);
}

TEST(MapTriggers, ShortEntranceOffsetsMatchRom) {
    const auto offsets = shortEntranceOffsets();
    ASSERT_EQ(offsets.size(), test::kExpectedShortEntranceOffsets.size());
    ASSERT_EQ(offsets.size(), kMapAddressSpace + 1);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        EXPECT_EQ(offsets[i].index, i) << "short entrance offset index at " << i;
        EXPECT_EQ(offsets[i].offset, test::kExpectedShortEntranceOffsets[i])
            << "short entrance offset value at " << i;
    }
    EXPECT_EQ(offsets.back().offset, kShortEntranceRecordCount);
}

// --- per-map slice behaviour ------------------------------------------------

TEST(MapTriggers, TreasuresForMapSlices) {
    // Every map's slice matches its offset-table window, and the accessor never
    // reads out of range.
    for (std::uint16_t m = 0; m < kMapCount; ++m) {
        const auto span = treasuresForMap(m);
        const std::size_t begin = treasureOffsets()[m].offset;
        const std::size_t end = treasureOffsets()[m + 1].offset;
        EXPECT_EQ(span.size(), end - begin) << "treasure slice size at map " << m;
        if (!span.empty()) {
            EXPECT_EQ(&span.front(), &treasureRecords()[begin])
                << "treasure slice base at map " << m;
        }
    }
    // Map 75 carries eight treasures (treasure_prop.inc _75=$00d7.._76=$00ff,
    // 8 records of 5 bytes); most early maps carry none.
    EXPECT_EQ(treasuresForMap(75).size(), 8u);
    EXPECT_TRUE(treasuresForMap(0).empty());
}

TEST(MapTriggers, EntrancesForMapSlices) {
    for (std::uint16_t m = 0; m < kMapAddressSpace; ++m) {
        const auto lspan = longEntrancesForMap(m);
        EXPECT_EQ(lspan.size(),
                  longEntranceOffsets()[m + 1].offset
                      - longEntranceOffsets()[m].offset)
            << "long entrance slice size at map " << m;
        const auto sspan = shortEntrancesForMap(m);
        EXPECT_EQ(sspan.size(),
                  shortEntranceOffsets()[m + 1].offset
                      - shortEntranceOffsets()[m].offset)
            << "short entrance slice size at map " << m;
    }
}

// --- wrapper decode vs independent re-derivation ----------------------------

TEST(MapTriggers, TreasureSwitchDecode) {
    const auto records = treasureRecords();
    bool sawGil = false, sawItem = false, sawMonster = false;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& bytes = test::kExpectedTreasureRecords[i].bytes;
        const std::uint16_t word = bytes[2] | (bytes[3] << 8);
        const auto& t = records[i].trigger;
        EXPECT_EQ(t.raw(), word) << "switch raw at " << i;
        EXPECT_EQ(t.eventBit(), word & 0x01FF) << "eventBit at " << i;
        EXPECT_EQ(t.isGil(), (word & 0x8000) != 0) << "isGil at " << i;
        EXPECT_EQ(t.isItem(), (word & 0x4000) != 0) << "isItem at " << i;
        EXPECT_EQ(t.isMonsterInABox(), (word & 0x2000) != 0) << "monster at " << i;
        EXPECT_EQ(t.isEmpty(), (word & 0x1000) != 0) << "isEmpty at " << i;
        sawGil = sawGil || t.isGil();
        sawItem = sawItem || t.isItem();
        sawMonster = sawMonster || t.isMonsterInABox();
    }
    // The corpus exercises all three real content types.
    EXPECT_TRUE(sawGil);
    EXPECT_TRUE(sawItem);
    EXPECT_TRUE(sawMonster);

    // Treasure record 0 is item 230 at (55, 8), event bit 1.
    const auto& r0 = records[0];
    EXPECT_EQ(r0.posX, 55);
    EXPECT_EQ(r0.posY, 8);
    EXPECT_TRUE(r0.trigger.isItem());
    EXPECT_FALSE(r0.trigger.isGil());
    EXPECT_EQ(r0.trigger.eventBit(), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(r0.item()), 230);
}

TEST(MapTriggers, EntranceDestinationDecode) {
    bool sawParent = false, sawFacing = false;
    const auto longs = longEntranceRecords();
    for (std::size_t i = 0; i < longs.size(); ++i) {
        const auto& bytes = test::kExpectedLongEntranceRecords[i].bytes;
        const std::uint16_t word = bytes[3] | (bytes[4] << 8);
        const auto& d = longs[i].destination;
        EXPECT_EQ(d.raw(), word) << "long dest raw at " << i;
        EXPECT_EQ(d.destMap(), word & 0x01FF) << "destMap at " << i;
        EXPECT_EQ(d.setsParentMap(), (word & 0x0200) != 0);
        EXPECT_EQ(d.zLevelLower(), (word & 0x0400) != 0);
        EXPECT_EQ(d.showMapName(), (word & 0x0800) != 0);
        EXPECT_EQ(static_cast<std::uint8_t>(d.facing()), (word >> 12) & 0x03);
        // The run byte: bit 7 vertical, bits 0-6 length.
        const std::uint8_t run = bytes[2];
        EXPECT_EQ(longs[i].run.isVertical(), (run & 0x80) != 0);
        EXPECT_EQ(longs[i].run.length(), run & 0x7F);
        sawParent = sawParent || d.isParentReturn();
        sawFacing = sawFacing ||
                    static_cast<std::uint8_t>(d.facing()) != 0;
    }
    for (std::size_t i = 0; i < shortEntranceRecords().size(); ++i) {
        const auto& bytes = test::kExpectedShortEntranceRecords[i].bytes;
        const std::uint16_t word = bytes[2] | (bytes[3] << 8);
        const auto& d = shortEntranceRecords()[i].destination;
        EXPECT_EQ(d.raw(), word) << "short dest raw at " << i;
        EXPECT_EQ(d.destMap(), word & 0x01FF) << "short destMap at " << i;
        sawParent = sawParent || d.isParentReturn();
    }
    // The parent-return sentinel and non-UP facings both occur in the corpus.
    EXPECT_TRUE(sawParent);
    EXPECT_TRUE(sawFacing);
    EXPECT_EQ(kParentMapSentinel, 0x01FFu);

    // Long entrance record 0: horizontal run of length 2 to map 15 at (88, 46).
    const auto& e0 = longEntranceRecords()[0];
    EXPECT_FALSE(e0.run.isVertical());
    EXPECT_EQ(e0.run.length(), 2u);
    EXPECT_EQ(e0.destination.destMap(), 15u);
    EXPECT_EQ(e0.destX, 88);
    EXPECT_EQ(e0.destY, 46);
}

}  // namespace
