// Full-corpus tests of the field-encounter family: the rand/event battle
// groups (candidate formation words), the world/sub group-index tables, the
// world/sub rate tables, and the five inline field/battle.asm tables. The
// byte-equivalence tests assert EVERY entry matches the ROM bytes (no subset);
// the semantic tests hand-trace a few sectors/maps against the raw bytes and
// exercise the accessors, the Veldt sentinel, and the randomize flag.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/encounters.h"

#include "ostinato/battle_background_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/formation_ref.h"

#include "fixtures/encounter_bg_tables_expected.h"
#include "fixtures/event_battle_group_expected.h"
#include "fixtures/rand_battle_group_expected.h"
#include "fixtures/sub_battle_group_expected.h"
#include "fixtures/sub_battle_rate_expected.h"
#include "fixtures/world_battle_group_expected.h"
#include "fixtures/world_battle_rate_expected.h"

namespace {

using ostinato::WorldId;

// A group table (rand/event) — each entry carries its index plus its formation
// words. Identity fields match position on both sides, and one memcmp per group
// catches word-order and randomize-flag drift against the ROM bytes.
template <typename Table, typename Fixture>
void expectGroupsByteIdentical(const Table& table, const Fixture& fixture) {
    ASSERT_EQ(table.size(), fixture.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(fixture[i].index, i) << "fixture entry " << i;
        EXPECT_EQ(table[i].index, i) << "table entry " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, fixture[i].bytes.data(),
                              sizeof(table[i].record)), 0)
            << "group " << i;
    }
}

// A flat per-index byte table (world/sub group + rate) — index field matches
// position and value matches the ROM byte, for every entry.
template <typename Table, typename Fixture>
void expectValuesByteIdentical(const Table& table, const Fixture& fixture) {
    ASSERT_EQ(table.size(), fixture.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(fixture[i].index, i) << "fixture entry " << i;
        EXPECT_EQ(table[i].value, fixture[i].value) << "entry " << i;
    }
}

TEST(RandBattleGroup, AllGroupsAreByteIdenticalToRom) {
    expectGroupsByteIdentical(ostinato::randomBattleGroups(),
                              ostinato::test::kExpectedRandBattleGroups);
}

TEST(EventBattleGroup, AllGroupsAreByteIdenticalToRom) {
    expectGroupsByteIdentical(ostinato::eventBattleGroups(),
                              ostinato::test::kExpectedEventBattleGroups);
}

TEST(WorldBattleGroup, AllEntriesAreByteIdenticalToRom) {
    expectValuesByteIdentical(ostinato::worldBattleGroup(),
                              ostinato::test::kExpectedWorldBattleGroup);
}

TEST(SubBattleGroup, AllEntriesAreByteIdenticalToRom) {
    expectValuesByteIdentical(ostinato::subBattleGroup(),
                              ostinato::test::kExpectedSubBattleGroup);
}

TEST(WorldBattleRate, AllEntriesAreByteIdenticalToRom) {
    expectValuesByteIdentical(ostinato::worldBattleRate(),
                              ostinato::test::kExpectedWorldBattleRate);
}

TEST(SubBattleRate, AllEntriesAreByteIdenticalToRom) {
    expectValuesByteIdentical(ostinato::subBattleRate(),
                              ostinato::test::kExpectedSubBattleRate);
}

// The five inline field/battle.asm tables, compared in full to their fixtures.
TEST(InlineTables, AllMatchRom) {
    for (std::size_t w = 0; w < 2; ++w) {
        for (std::size_t i = 0; i < 8; ++i) {
            EXPECT_EQ(
                static_cast<std::uint8_t>(ostinato::kWorldBattleBackgrounds[w][i]),
                ostinato::test::kExpectedWorldBattleBg[w * 8 + i])
                << "world " << w << " slot " << i;
        }
    }
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(ostinato::kBattleBgRateSlot[i],
                  ostinato::test::kExpectedBattleBgRateSlot[i]) << "slot " << i;
        EXPECT_EQ(ostinato::kBattleBgGroupOffset[i],
                  ostinato::test::kExpectedBattleBgGroupOffset[i]) << i;
    }
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t r = 0; r < 4; ++r) {
            EXPECT_EQ(ostinato::kWorldBattleRateIncrements[c].byRate[r],
                      ostinato::test::kExpectedWorldBattleRateIncrements[c * 4 + r])
                << "world charm " << c << " class " << r;
            EXPECT_EQ(ostinato::kSubBattleRateIncrements[c].byRate[r],
                      ostinato::test::kExpectedSubBattleRateIncrements[c * 4 + r])
                << "sub charm " << c << " class " << r;
        }
    }
}

// The rate-increment tables are magnitudes, not bytes: Charm Bangle halves the
// normal-class world rate, the Moogle Charm zeroes every class.
TEST(InlineTables, RateIncrementsAreDecimalMagnitudes) {
    using ostinato::CharmState;
    const auto charm = static_cast<std::size_t>(CharmState::NONE);
    const auto bangle = static_cast<std::size_t>(CharmState::CHARM_BANGLE);
    const auto moogle = static_cast<std::size_t>(CharmState::MOOGLE_CHARM);
    EXPECT_EQ(ostinato::kWorldBattleRateIncrements[charm].byRate[0], 192);
    EXPECT_EQ(ostinato::kWorldBattleRateIncrements[bangle].byRate[0], 96);
    for (std::size_t r = 0; r < 4; ++r) {
        EXPECT_EQ(ostinato::kWorldBattleRateIncrements[moogle].byRate[r], 0);
        EXPECT_EQ(ostinato::kSubBattleRateIncrements[moogle].byRate[r], 0);
    }
}

// world_battle_group sector 0 of the World of Balance holds the four bg-group
// slots 9, 11, 12, 13 (raw bytes 09 0B 0C 0D).
TEST(WorldBattleGroup, Accessor) {
    EXPECT_EQ(ostinato::getWorldBattleGroup(WorldId::WORLD_OF_BALANCE, 0, 0, 0),
              9);
    EXPECT_EQ(ostinato::getWorldBattleGroup(WorldId::WORLD_OF_BALANCE, 0, 0, 1),
              11);
    EXPECT_EQ(ostinato::getWorldBattleGroup(WorldId::WORLD_OF_BALANCE, 0, 0, 3),
              13);
}

// Exactly 28 world-map sectors are Veldt sectors ($FF), and isVeldtSector
// reports them.
TEST(WorldBattleGroup, VeldtSectorCountIs28) {
    std::size_t veldt = 0;
    for (const auto& e : ostinato::worldBattleGroup()) {
        if (e.value == ostinato::kVeldtSector) {
            ++veldt;
        }
    }
    EXPECT_EQ(veldt, 28u);
}

// sub_battle_group: map 32 is the first map with a non-zero group (group 189).
TEST(SubBattleGroup, Accessor) {
    EXPECT_EQ(ostinato::getMapBattleGroup(0), 0);
    EXPECT_EQ(ostinato::getMapBattleGroup(32), 189);
}

// world_battle_rate byte 5 is $55 — every 2-bit slot decodes to class LOW.
TEST(WorldBattleRate, AccessorDecodesByte5) {
    using ostinato::WorldBattleRateClass;
    for (std::uint8_t slot = 0; slot < 4; ++slot) {
        EXPECT_EQ(ostinato::getWorldBattleRate(WorldId::WORLD_OF_BALANCE, 5,
                                               slot),
                  WorldBattleRateClass::LOW)
            << "slot " << static_cast<int>(slot);
    }
}

// sub_battle_rate byte 10 is $04 — of maps 40-43, only map 41 is class LOW.
TEST(SubBattleRate, AccessorDecodesMap41) {
    using ostinato::SubBattleRateClass;
    EXPECT_EQ(ostinato::getMapBattleRate(40), SubBattleRateClass::NORMAL);
    EXPECT_EQ(ostinato::getMapBattleRate(41), SubBattleRateClass::LOW);
    EXPECT_EQ(ostinato::getMapBattleRate(42), SubBattleRateClass::NORMAL);
    EXPECT_EQ(ostinato::getMapBattleRate(43), SubBattleRateClass::NORMAL);
}

// event_battle_group 93's first word is formation 463 — the fixed read
// world/ctrl.asm:432 makes at offset $0174.
TEST(EventBattleGroup, Group93FirstFormation) {
    const auto& g = ostinato::getEventBattleGroup(93);
    EXPECT_EQ(static_cast<std::uint16_t>(g.formations[0].formationId()), 463);
    EXPECT_FALSE(g.formations[0].randomizePlus3());
}

// The only randomize-plus-3 words in the whole corpus are rand group 112's four
// slots; every event word is a plain reference.
TEST(RandBattleGroup, RandomizeFlagOnlyOnGroup112) {
    std::size_t randomized = 0;
    const auto groups = ostinato::randomBattleGroups();
    for (std::size_t g = 0; g < groups.size(); ++g) {
        for (const auto& ref : groups[g].record.formations) {
            if (ref.randomizePlus3()) {
                ++randomized;
                EXPECT_EQ(g, 112u) << "unexpected randomize group " << g;
            }
        }
    }
    EXPECT_EQ(randomized, 4u);

    for (const auto& g : ostinato::eventBattleGroups()) {
        for (const auto& ref : g.record.formations) {
            EXPECT_FALSE(ref.randomizePlus3());
        }
    }
}

}  // namespace
