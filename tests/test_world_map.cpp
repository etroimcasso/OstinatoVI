// Full-corpus tests for the world-map data: the story-gated tilemap
// modification chunks, their per-world offset table, the seven vehicle event
// references, and the sine table. Every chunk and every event reference is
// memcmp'd against its generated fixture (the ROM-assembled bytes), independent
// of the typed rows, so any decode or re-emit drift fails loudly. The offset
// table and the sine table are checked entry-by-entry (typed identity + value),
// the per-world slice accessors are exercised, and the EventBitRef /
// WorldTilePatchRef surfaces are round-tripped against traced source values.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/world_map.h"
#include "ostinato/event_script_ref.h"
#include "ostinato/world_map_id.h"
#include "ostinato/world_map_modification.h"
#include "ostinato/world_vehicle_event.h"

#include "fixtures/world_data_expected.h"

namespace {

using namespace ostinato;

// --- full-corpus byte-equivalence -------------------------------------------

TEST(WorldMap, ModificationsMatchRom) {
    const auto records = worldModificationRecords();
    ASSERT_EQ(records.size(), test::kExpectedWorldMapModifications.size());
    ASSERT_EQ(records.size(), kWorldModificationCount);
    for (std::size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(std::memcmp(&records[i],
                              test::kExpectedWorldMapModifications[i].bytes.data(),
                              sizeof(WorldMapModification)),
                  0)
            << "modification chunk bytes at " << i;
    }
}

TEST(WorldMap, OffsetTableMatchesRom) {
    const auto offsets = worldModificationOffsets();
    ASSERT_EQ(offsets.size(), test::kExpectedWorldModOffsets.size());
    ASSERT_EQ(offsets.size(), kWorldModifiedWorldCount + 1);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        EXPECT_EQ(offsets[i].index, test::kExpectedWorldModOffsets[i].index)
            << "offset index at " << i;
        EXPECT_EQ(offsets[i].firstChunk,
                  test::kExpectedWorldModOffsets[i].firstChunk)
            << "offset value at " << i;
    }
    // End marker: the last entry's chunk index is the chunk count.
    EXPECT_EQ(offsets.back().firstChunk, kWorldModificationCount);
}

TEST(WorldMap, VehicleEventsMatchRom) {
    const auto events = worldVehicleEvents();
    ASSERT_EQ(events.size(), test::kExpectedWorldVehicleEvents.size());
    ASSERT_EQ(events.size(), kWorldVehicleEventCount);
    for (std::size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(static_cast<std::size_t>(events[i].event), i)
            << "vehicle event position at " << i;
        EXPECT_EQ(std::memcmp(events[i].script.bytes.data(),
                              test::kExpectedWorldVehicleEvents[i].bytes.data(),
                              sizeof(EventScriptRef)),
                  0)
            << "vehicle event reference bytes at " << i;
    }
}

TEST(WorldMap, SineTableMatchesRom) {
    const auto sine = worldSineTable();
    ASSERT_EQ(sine.size(), test::kExpectedWorldSine.size());
    ASSERT_EQ(sine.size(), kWorldSineLength);
    for (std::size_t i = 0; i < sine.size(); ++i) {
        EXPECT_EQ(sine[i].index, test::kExpectedWorldSine[i].index)
            << "sine degree at " << i;
        EXPECT_EQ(sine[i].amplitude, test::kExpectedWorldSine[i].amplitude)
            << "sine amplitude at " << i;
    }
}

// The table is the generator the source documents (world/world_data.asm:47-50),
// checked here against the shipped rows rather than only at emit time.
TEST(WorldMap, SineTableMatchesItsGenerator) {
    const auto sine = worldSineTable();
    for (std::size_t degree = 0; degree < sine.size(); ++degree) {
        const auto expected = static_cast<std::uint8_t>(std::floor(
            std::fabs(std::sin(2.0 * M_PI * static_cast<double>(degree) / 360.0)
                      * 255.0)));
        EXPECT_EQ(sine[degree].amplitude, expected)
            << "sine amplitude at degree " << degree;
    }
}

// --- per-world slices --------------------------------------------------------

TEST(WorldMap, ModificationSlicesPartitionTheChunkList) {
    const auto balance = worldModifications(WorldMapId::WORLD_OF_BALANCE);
    const auto ruin = worldModifications(WorldMapId::WORLD_OF_RUIN);
    // The two lists are 60 and 12 bytes of 4-byte chunks.
    EXPECT_EQ(balance.size(), 15u);
    EXPECT_EQ(ruin.size(), 3u);
    EXPECT_EQ(balance.size() + ruin.size(), kWorldModificationCount);
    // The slices are contiguous and cover the whole array in order.
    const auto records = worldModificationRecords();
    EXPECT_EQ(balance.data(), records.data());
    EXPECT_EQ(ruin.data(), records.data() + balance.size());
}

// The first chunk of the World of Balance, traced through the source: event bit
// 267 gates the patch at the very start of the pool (offset $48 == the pool's
// own base, 60 + 12 bytes of chunk lists).
TEST(WorldMap, FirstBalanceChunkMatchesTracedSource) {
    const auto balance = worldModifications(WorldMapId::WORLD_OF_BALANCE);
    ASSERT_FALSE(balance.empty());
    EXPECT_EQ(balance.front().bit.index(), 267u);
    EXPECT_EQ(balance.front().patch.offsetFromBlockBase(), 0x0048u);
}

TEST(WorldMap, FirstRuinChunkMatchesTracedSource) {
    const auto ruin = worldModifications(WorldMapId::WORLD_OF_RUIN);
    ASSERT_FALSE(ruin.empty());
    EXPECT_EQ(ruin.front().bit.index(), 262u);
    EXPECT_EQ(ruin.front().patch.offsetFromBlockBase(), 0x04D3u);
}

// Every chunk points into the patch pool, which begins where the two chunk
// lists end. The pool is 1,182 bytes, so the block runs to $04E6.
TEST(WorldMap, EveryPatchReferenceLandsInThePool) {
    constexpr std::uint16_t poolBegin = 0x0048;
    constexpr std::uint16_t poolEnd = poolBegin + 1182;
    for (const auto& chunk : worldModificationRecords()) {
        EXPECT_GE(chunk.patch.offsetFromBlockBase(), poolBegin);
        EXPECT_LT(chunk.patch.offsetFromBlockBase(), poolEnd);
    }
}

// Bit 15 is masked off by the consumer before the byte/mask split, and no
// shipped row sets it — so index() and raw() agree across the whole corpus.
TEST(WorldMap, NoEventBitSetsTheMaskedHighBit) {
    for (const auto& chunk : worldModificationRecords()) {
        EXPECT_EQ(chunk.bit.raw() & 0x8000u, 0u);
        EXPECT_EQ(chunk.bit.raw(), chunk.bit.index());
    }
}

// --- vehicle events ----------------------------------------------------------

TEST(WorldMap, VehicleEventsResolveToTheirScripts) {
    EXPECT_EQ(worldVehicleEvent(WorldVehicleEvent::AIRSHIP_DECK).offset(),
              0x00068u);
    EXPECT_EQ(worldVehicleEvent(WorldVehicleEvent::WORLD_TENT).offset(),
              0x0004Fu);
    EXPECT_EQ(worldVehicleEvent(WorldVehicleEvent::AIRSHIP_GROUND).offset(),
              0x00059u);
    EXPECT_EQ(worldVehicleEvent(WorldVehicleEvent::ENTER_PHOENIX_CAVE).offset(),
              0x00088u);
    EXPECT_EQ(worldVehicleEvent(WorldVehicleEvent::ENTER_KEFKAS_TOWER).offset(),
              0x0007Fu);
    EXPECT_EQ(worldVehicleEvent(WorldVehicleEvent::ENTER_GOGOS_LAIR).offset(),
              0x0008Fu);
    EXPECT_EQ(worldVehicleEvent(WorldVehicleEvent::DOOM_GAZE_DEFEATED).offset(),
              0x00096u);
}

TEST(WorldMap, EveryVehicleEventPointsIntoTheEventScriptBlock) {
    for (const auto& entry : worldVehicleEvents()) {
        EXPECT_LT(entry.script.offset(), kEventScriptBlockSize);
    }
}

// --- packed-wrapper surfaces -------------------------------------------------

TEST(WorldMap, EventBitRefSplitsABitNumberLikeTheConsumer) {
    // Event bit 267 = byte 33, mask $08 — the shift-and-mask the consumer does
    // (world/init.asm:1940-1949).
    constexpr auto ref = EventBitRef::of(267);
    static_assert(ref.raw() == 267);
    static_assert(ref.index() == 267);
    static_assert(ref.byteIndex() == 33);
    static_assert(ref.bitMask() == 0x08);
    EXPECT_EQ(ref.bytes[0], 0x0B);
    EXPECT_EQ(ref.bytes[1], 0x01);

    // Every bit position in a byte maps to its own mask.
    for (std::uint16_t bit = 0; bit < 8; ++bit) {
        EXPECT_EQ(EventBitRef::of(bit).bitMask(), 1u << bit);
        EXPECT_EQ(EventBitRef::of(bit).byteIndex(), 0u);
    }
    EXPECT_EQ(EventBitRef::of(8).byteIndex(), 1u);
    EXPECT_EQ(EventBitRef::of(8).bitMask(), 0x01);
}

TEST(WorldMap, WorldTilePatchRefRoundTrip) {
    constexpr auto ref = WorldTilePatchRef::at(0x04D3);
    static_assert(ref.offsetFromBlockBase() == 0x04D3);
    EXPECT_EQ(ref.bytes[0], 0xD3);
    EXPECT_EQ(ref.bytes[1], 0x04);
}

// The packed record is exactly the ROM's four bytes, so an array of them is
// byte-identical to the contiguous chunk list.
TEST(WorldMap, ModificationRecordIsPacked) {
    static_assert(sizeof(WorldMapModification) == 4);
    static_assert(alignof(WorldMapModification) == 1);
    static_assert(sizeof(EventBitRef) == 2);
    static_assert(sizeof(WorldTilePatchRef) == 2);
    EXPECT_EQ(sizeof(WorldMapModification) * kWorldModificationCount, 72u);
}

// --- sine / cosine accessors -------------------------------------------------

TEST(WorldMap, SineAndCosineReadTheTableAtTheConsumersIndices) {
    const auto sine = worldSineTable();
    // Below 180 the angle indexes the table directly; cosine reads a quarter
    // turn along (world/move.asm:39-41).
    EXPECT_EQ(worldSine(0), sine[0].amplitude);
    EXPECT_EQ(worldCosine(0), sine[90].amplitude);
    EXPECT_EQ(worldSine(45), sine[45].amplitude);
    EXPECT_EQ(worldCosine(45), sine[135].amplitude);
    EXPECT_EQ(worldSine(179), sine[179].amplitude);
    EXPECT_EQ(worldCosine(179), sine[269].amplitude);
}

TEST(WorldMap, AnglesAtOrAbove180ReduceByASingleSubtraction) {
    // The consumer subtracts 180 once (world/move.asm:35-37), so the second
    // half turn mirrors the first — the table holds magnitudes only and the
    // sign comes from the quadrant test, not from the table.
    for (std::uint16_t degrees = 180; degrees < 360; ++degrees) {
        EXPECT_EQ(worldSine(degrees), worldSine(degrees - 180))
            << "sine at " << degrees;
        EXPECT_EQ(worldCosine(degrees), worldCosine(degrees - 180))
            << "cosine at " << degrees;
    }
}

TEST(WorldMap, SineQuadrantEndpoints) {
    EXPECT_EQ(worldSine(0), 0);
    EXPECT_EQ(worldSine(90), 255);
    EXPECT_EQ(worldCosine(0), 255);
    EXPECT_EQ(worldCosine(90), 0);
}

}  // namespace
