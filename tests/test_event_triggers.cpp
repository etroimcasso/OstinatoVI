// Full-corpus tests for the event-trigger family: the per-map event triggers,
// their per-map offset table, and the per-map startup events. Every record is
// memcmp'd against its generated fixture (the ROM-assembled bytes), independent
// of the typed rows, so any decode or re-emit drift fails loudly. The offset
// table is checked entry-by-entry (typed identity + record offset), the per-map
// slice accessors are exercised, and the EventScriptRef surface is round-tripped.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/event_triggers.h"
#include "ostinato/event_script_ref.h"
#include "ostinato/event_trigger.h"

#include "fixtures/event_trigger_expected.h"

namespace {

using namespace ostinato;

// --- full-corpus byte-equivalence -------------------------------------------

TEST(EventTriggers, RecordsMatchRom) {
    const auto records = eventTriggerRecords();
    ASSERT_EQ(records.size(), test::kExpectedEventTriggerRecords.size());
    ASSERT_EQ(records.size(), kEventTriggerRecordCount);
    for (std::size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(std::memcmp(&records[i],
                              test::kExpectedEventTriggerRecords[i].bytes.data(),
                              sizeof(EventTrigger)),
                  0)
            << "event trigger record bytes at " << i;
    }
}

TEST(EventTriggers, OffsetTableMatchesRom) {
    const auto offsets = eventTriggerOffsets();
    ASSERT_EQ(offsets.size(), test::kExpectedEventTriggerOffsets.size());
    ASSERT_EQ(offsets.size(), kEventTriggerMapSlots + 1);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        EXPECT_EQ(offsets[i].index, i) << "offset index at " << i;
        EXPECT_EQ(offsets[i].offset, test::kExpectedEventTriggerOffsets[i])
            << "offset value at " << i;
    }
    // End marker: the last slot's offset is the record count.
    EXPECT_EQ(offsets.back().offset, kEventTriggerRecordCount);
}

TEST(EventTriggers, MapInitEventsMatchRom) {
    const auto events = mapInitEvents();
    ASSERT_EQ(events.size(), test::kExpectedMapInitEvents.size());
    ASSERT_EQ(events.size(), 512u);
    for (std::size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(events[i].index, i) << "map-init index at " << i;
        EXPECT_EQ(std::memcmp(events[i].record.bytes.data(),
                              test::kExpectedMapInitEvents[i].bytes.data(),
                              sizeof(EventScriptRef)),
                  0)
            << "map-init record bytes at " << i;
    }
}

// --- EventScriptRef surface --------------------------------------------------

TEST(EventTriggers, EventScriptRefRoundTrip) {
    // Little-endian byte order and offset() round-trip.
    constexpr auto ref = EventScriptRef::at(0x010BB7);
    static_assert(ref.offset() == 0x010BB7);
    static_assert(ref.bytes[0] == 0xB7 && ref.bytes[1] == 0x0B
                  && ref.bytes[2] == 0x01);
    EXPECT_EQ(ref.offset(), 0x010BB7u);
    // The block-size bound is the ceiling every offset stays below.
    EXPECT_LT(EventScriptRef::at(0x029AEB).offset(), kEventScriptBlockSize);
}

TEST(EventTriggers, EventReturnScriptPin) {
    // The generated kEventReturnScript matches the ROM-resolved offset.
    EXPECT_EQ(kEventReturnScript.offset(), test::kExpectedEventReturnOffset);
    EXPECT_EQ(kEventReturnScript.offset(), 0x05EB3u);
}

// --- per-map slice accessors -------------------------------------------------

TEST(EventTriggers, MapZeroTriggers) {
    // Map 0 (a world map) carries 9 triggers; the first is at tile (179, 71) and
    // fires event-script offset 0x10bb7 (_cb0bb7).
    const auto triggers = eventTriggersForMap(0);
    ASSERT_EQ(triggers.size(), 9u);
    EXPECT_EQ(triggers[0].posX, 179);
    EXPECT_EQ(triggers[0].posY, 71);
    EXPECT_EQ(triggers[0].event.offset(), 0x010BB7u);
}

TEST(EventTriggers, MapOneTriggers) {
    // Map 1 (world map) has 5 triggers — both share the single event table.
    EXPECT_EQ(eventTriggersForMap(1).size(), 5u);
}

TEST(EventTriggers, MapNineSavePoint) {
    // Map 9 has one trigger: the SavePoint at tile (8, 6). SavePoint is a named
    // code label; its offset (0x29aeb) is resolved from the ROM.
    const auto triggers = eventTriggersForMap(9);
    ASSERT_EQ(triggers.size(), 1u);
    EXPECT_EQ(triggers[0].posX, 8);
    EXPECT_EQ(triggers[0].posY, 6);
    EXPECT_EQ(triggers[0].event.offset(), 0x029AEBu);
}

TEST(EventTriggers, EmptyMaps) {
    // Maps 2, 4, 5 carry no triggers; slot 415 (the extra slot beyond the 415
    // defined maps) is empty too.
    EXPECT_TRUE(eventTriggersForMap(2).empty());
    EXPECT_TRUE(eventTriggersForMap(4).empty());
    EXPECT_TRUE(eventTriggersForMap(5).empty());
    EXPECT_TRUE(eventTriggersForMap(415).empty());
}

TEST(EventTriggers, PerMapSlicesReconstructTheCorpus) {
    // The per-map slices cover the flat record array exactly once.
    std::size_t total = 0;
    for (std::uint16_t m = 0; m < kEventTriggerMapSlots; ++m) {
        total += eventTriggersForMap(m).size();
    }
    EXPECT_EQ(total, kEventTriggerRecordCount);
}

// --- map-init startup events -------------------------------------------------

TEST(EventTriggers, MapInitEventAnchors) {
    // Map 0 has no startup event (EventReturn); map 3 runs _cae8f4 (offset
    // 0xe8f4); map 6 runs _cb29f3 (offset 0x129f3).
    EXPECT_EQ(mapInitEvent(0).offset(), kEventReturnScript.offset());
    EXPECT_EQ(mapInitEvent(3).offset(), 0x00E8F4u);
    EXPECT_EQ(mapInitEvent(6).offset(), 0x0129F3u);
}

}  // namespace
