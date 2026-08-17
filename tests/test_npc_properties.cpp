// Full-corpus tests for the per-map NPC-properties table: all 2,193 nine-byte
// records memcmp'd against the ROM-assembled fixture (independent of the typed
// builder rows, so any pack/emit drift fails loudly), the per-map offset table
// checked entry-by-entry, the runtime variant discrimination validated over the
// whole corpus, and the three builders + accessors round-tripped. The memcmp
// proves the builders pack the exact ROM bytes; the encode->decode round-trips
// prove the accessors invert the builders; together they pin the decode to the
// ROM.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/npc_properties.h"
#include "ostinato/event_dir.h"
#include "ostinato/event_trigger.h"  // kEventReturnScript

#include "fixtures/npc_prop_expected.h"

namespace {

using namespace ostinato;

// --- full-corpus byte-equivalence -------------------------------------------

TEST(NpcProperties, RecordsMatchRom) {
    const auto records = npcRecords();
    ASSERT_EQ(records.size(), test::kExpectedNpcRecords.size());
    ASSERT_EQ(records.size(), kNpcRecordCount);
    for (std::size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(std::memcmp(&records[i],
                              test::kExpectedNpcRecords[i].bytes.data(),
                              sizeof(NpcProperties)),
                  0)
            << "npc record bytes at " << i;
    }
}

TEST(NpcProperties, OffsetTableMatchesRom) {
    const auto offsets = npcOffsets();
    ASSERT_EQ(offsets.size(), test::kExpectedNpcOffsets.size());
    ASSERT_EQ(offsets.size(), kNpcMapSlots + 1);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        EXPECT_EQ(offsets[i].index, i) << "offset index at " << i;
        EXPECT_EQ(offsets[i].offset, test::kExpectedNpcOffsets[i])
            << "offset value at " << i;
    }
    EXPECT_EQ(offsets.back().offset, kNpcRecordCount);
}

// --- runtime variant discrimination over the whole corpus -------------------

TEST(NpcProperties, VariantDiscriminationCounts) {
    // The byte-level predicates reproduce the source's variant split exactly:
    // 240 special (make_special_npc), 451 animated (make_npc + set_npc_anim),
    // 1,502 plain normal.
    std::size_t special = 0;
    std::size_t animated = 0;
    std::size_t normal = 0;
    for (const auto& r : npcRecords()) {
        if (r.isSpecial()) {
            ++special;
        } else if (r.isAnimated()) {
            ++animated;
        } else {
            ++normal;
        }
    }
    EXPECT_EQ(special, 240u);
    EXPECT_EQ(animated, 451u);
    EXPECT_EQ(normal, 1502u);
    EXPECT_EQ(special + animated + normal, kNpcRecordCount);
}

// --- builder encode -> decode round-trips (one per variant) -----------------

TEST(NpcProperties, NormalBuilderRoundTrip) {
    // A plain normal NPC (matches record 0's map, the first CLYDE guard on map 3:
    // npc_prop.asm — pos (8,11), switch $043f, CLYDE/LOCKE, SLOW, facing UP, no
    // reaction). No explicit event -> kEventReturnScript.
    constexpr auto r = NpcProperties::npc({.pos = {8, 11},
                                           .switchId = 0x043F,
                                           .gfx = MapSpriteGfx::CLYDE,
                                           .pal = MapSpritePal::LOCKE,
                                           .speed = ObjectSpeed::SLOW,
                                           .dir = EventDir::UP,
                                           .react = NpcReact::NONE});
    static_assert(!r.isSpecial() && !r.isAnimated());
    static_assert(r.posX() == 8 && r.posY() == 11);
    static_assert(r.switchId() == 0x043F);
    static_assert(r.gfx() == MapSpriteGfx::CLYDE);
    static_assert(r.pal() == MapSpritePal::LOCKE);
    static_assert(r.speed() == ObjectSpeed::SLOW);
    static_assert(r.dir() == EventDir::UP);
    static_assert(r.react() == NpcReact::NONE);
    static_assert(!r.showRider());
    static_assert(r.eventRef().offset() == kEventReturnScript.offset());
    SUCCEED();
}

TEST(NpcProperties, AnimatedBuilderRoundTrip) {
    // An animated NPC with an explicit event and a four-frame animation.
    constexpr auto r =
        NpcProperties::animated({.pos = {5, 31},
                                 .switchId = 0x048C,
                                 .event = EventScriptRef::at(0x0224B),
                                 .gfx = MapSpriteGfx::BIG_SPARKLE,
                                 .pal = MapSpritePal::RAINBOW,
                                 .speed = ObjectSpeed::SLOWER,
                                 .layerPriority = NpcLayerPriority::FOREGROUND,
                                 .react = NpcReact::NONE,
                                 .animType = NpcAnimType::FOUR_FRAMES,
                                 .animFrame = NpcAnimFrame::SPECIAL,
                                 .animSpeed = NpcAnimSpeed::MEDIUM});
    static_assert(r.isAnimated() && !r.isSpecial());
    static_assert(r.posX() == 5 && r.posY() == 31);
    static_assert(r.switchId() == 0x048C);
    static_assert(r.eventRef().offset() == 0x0224Bu);
    static_assert(r.animType() == NpcAnimType::FOUR_FRAMES);
    static_assert(r.animFrame() == NpcAnimFrame::SPECIAL);
    static_assert(r.animSpeed() == NpcAnimSpeed::MEDIUM);
    static_assert(r.layerPriority() == NpcLayerPriority::FOREGROUND);
    SUCCEED();
}

TEST(NpcProperties, SpecialBuilderRoundTrip) {
    // A special NPC with a master reference (a slave shifted right one tile).
    constexpr auto r = NpcProperties::special(
        {.pos = {2, 9},
         .switchId = 0x03FF,
         .gfx = MapSpriteGfx::FLYING_TERRA_3,
         .pal = MapSpritePal::RAINBOW,
         .speed = ObjectSpeed::SLOWER,
         .layerPriority = NpcLayerPriority::FOREGROUND,
         .animType = NpcAnimType::TWO_FRAMES,
         .animFrame = NpcAnimFrame::SPECIAL,
         .vramPos = {2, 0},
         .master = {.id = 0, .offset = 1, .dir = NpcMasterOffsetDir::RIGHT,
                    .isSlave = true}});
    static_assert(r.isSpecial());
    static_assert(r.posX() == 2 && r.posY() == 9);
    static_assert(r.vramX() == 2 && r.vramY() == 0);
    static_assert(r.masterId() == 0 && r.masterOffset() == 1);
    static_assert(r.masterDir() == NpcMasterOffsetDir::RIGHT);
    static_assert(r.isSlave());
    static_assert(r.animType() == NpcAnimType::TWO_FRAMES);
    static_assert(r.animFrame() == NpcAnimFrame::SPECIAL);
    static_assert(r.layerPriority() == NpcLayerPriority::FOREGROUND);
    SUCCEED();
}

TEST(NpcProperties, MasterWithoutSlaveBit) {
    // 105 records reference a master but clear the slave bit (a master reference
    // that is not itself a slave). isSlave defaults false; the byte reflects it.
    constexpr auto r = NpcProperties::special(
        {.pos = {8, 8},
         .switchId = 0x039F,
         .gfx = MapSpriteGfx::ENDING_TERRA_3,
         .pal = MapSpritePal::TERRA,
         .speed = ObjectSpeed::NORMAL,
         .layerPriority = NpcLayerPriority::BACKGROUND,
         .vramPos = {4, 0},
         .master = {.id = 0, .offset = 4, .dir = NpcMasterOffsetDir::DOWN}});
    static_assert(r.masterOffset() == 4);
    static_assert(r.masterDir() == NpcMasterOffsetDir::DOWN);
    static_assert(!r.isSlave());
    SUCCEED();
}

// --- real-record decode spot-check ------------------------------------------

TEST(NpcProperties, MapThreeFirstSpecial) {
    // World maps 0-2 have no NPCs; map 3 is the first populated map. Its first
    // NPC is a 32x32 vehicle in the "special graphics" class (the airship deck):
    // switch $03a0, SLOWER, NOTHING/VEHICLE, drawn behind the background layer.
    const auto npcs = npcsForMap(3);
    ASSERT_FALSE(npcs.empty());
    const NpcProperties& r = npcs[0];
    EXPECT_TRUE(r.isSpecial());
    EXPECT_EQ(r.posX(), 4);
    EXPECT_EQ(r.posY(), 4);
    EXPECT_TRUE(r.is32x32());
    EXPECT_EQ(r.switchId(), 0x03A0u);
    EXPECT_EQ(r.gfx(), MapSpriteGfx::NOTHING);
    EXPECT_EQ(r.pal(), MapSpritePal::VEHICLE);
    EXPECT_EQ(r.speed(), ObjectSpeed::SLOWER);
    EXPECT_EQ(r.layerPriority(), NpcLayerPriority::BACKGROUND);
    EXPECT_EQ(r.vramX(), 0);
    EXPECT_EQ(r.vramY(), 0);
}

// --- value-space enum pins ---------------------------------------------------

TEST(NpcProperties, EnumPins) {
    static_assert(static_cast<std::uint8_t>(MapSpriteGfx::TERRA) == 0);
    static_assert(static_cast<std::uint8_t>(MapSpriteGfx::SMALL_BIRD_LEFT) == 164);
    // The palette alias set: a palette-less set_npc_gfx uses the sprite's own
    // name as its palette (LOCKE = MERCHANT = BROWN_SOLDIER = 1).
    static_assert(MapSpritePal::LOCKE == MapSpritePal::MERCHANT);
    static_assert(MapSpritePal::LOCKE == MapSpritePal::BROWN_SOLDIER);
    static_assert(static_cast<std::uint8_t>(MapSpritePal::VEHICLE) == 7);
    // EventVehicle keeps the upstream $00/$20/$40/$60 spacing (packed <<1 &$c0).
    static_assert(static_cast<std::uint8_t>(EventVehicle::CHOCOBO) == 0x20);
    static_assert(static_cast<std::uint8_t>(EventVehicle::RAFT) == 0x60);
    SUCCEED();
}

// --- per-map slice invariants ------------------------------------------------

TEST(NpcProperties, WorldMapsHaveNoNpcs) {
    // "no npcs on world maps": slots 0-2 are empty, as is the extra slot 415.
    EXPECT_TRUE(npcsForMap(0).empty());
    EXPECT_TRUE(npcsForMap(1).empty());
    EXPECT_TRUE(npcsForMap(2).empty());
    EXPECT_TRUE(npcsForMap(415).empty());
}

TEST(NpcProperties, PerMapSlicesReconstructTheCorpus) {
    std::size_t total = 0;
    for (std::uint16_t m = 0; m < kNpcMapSlots; ++m) {
        total += npcsForMap(m).size();
    }
    EXPECT_EQ(total, kNpcRecordCount);
}

}  // namespace
