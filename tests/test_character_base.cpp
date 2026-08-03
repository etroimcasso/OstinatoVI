// Full-corpus test of the character base-stats table (PLAN phase-1.A D6/D7 + A1).
// The byte-equivalence test asserts EVERY one of the 64 records is byte-identical
// to the ROM's 22-byte record (no subset); the semantic test exercises the lookup
// and the packed-trait accessors the port depends on.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/character.h"

#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/item_id.h"
#include "ostinato/level_mod.h"
#include "ostinato/run_factor.h"

#include "fixtures/char_prop_expected.h"

namespace {

// Full corpus: one memcmp per record catches field-order, padding, enum-value,
// and trait-packing drift in a single byte-for-byte comparison against the ROM.
TEST(CharacterBaseStats, AllRecordsAreByteIdenticalToRom) {
    const auto table = ostinato::characterBaseStats();
    ASSERT_EQ(table.size(), ostinato::test::kExpectedCharacterRecords.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(std::memcmp(&table[i],
                              &ostinato::test::kExpectedCharacterRecords[i], 22),
                  0)
            << "record index " << i;
    }
}

// The lookup indexes by CharacterPropId (== the raw 0..63 record index). Spot-check
// two named records' semantic surface, including the D6 packed-trait accessors.
TEST(CharacterBaseStats, LookupAndTraitAccessors) {
    using ostinato::BattleCommandId;
    using ostinato::CharacterPropId;
    using ostinato::getCharacterBaseStats;
    using ostinato::LevelMod;
    using ostinato::RunFactor;

    const auto& terra = getCharacterBaseStats(CharacterPropId::TERRA);
    EXPECT_EQ(terra.hp, 40u);
    EXPECT_EQ(terra.mp, 16u);
    EXPECT_EQ(terra.commands[0], BattleCommandId::FIGHT);
    EXPECT_EQ(terra.traits.runFactor(), RunFactor::NORMAL);
    EXPECT_EQ(terra.traits.levelMod(), LevelMod::NORMAL);
    EXPECT_FALSE(terra.traits.fixedEquip());

    // Banon ($0e): run VERY_LOW, level LOW, fixed-equip set (packed 0x1f).
    const auto& banon = getCharacterBaseStats(CharacterPropId::BANON);
    EXPECT_EQ(banon.traits.runFactor(), RunFactor::VERY_LOW);
    EXPECT_EQ(banon.traits.levelMod(), LevelMod::LOW);
    EXPECT_TRUE(banon.traits.fixedEquip());
}

}  // namespace
