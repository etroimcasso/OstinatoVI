// Full-corpus test of the dance attack table (PLAN phase-1.B D4 + Amendment
// B1). The byte-equivalence test asserts EVERY one of the 8 packed records is
// byte-identical to the ROM's 4-byte record (no subset) and that every
// entry's identity field matches its position; the semantic tests exercise
// the lookup.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/dance_properties.h"

#include "ostinato/attack_id.h"
#include "ostinato/dance_id.h"

#include "fixtures/dance_prop_expected.h"

namespace {

// Full corpus: identity fields on both sides match the position, and one
// memcmp per packed record catches field-order and symbol-resolution drift
// in a single byte-for-byte comparison against the ROM.
TEST(DanceProperties, AllRecordsAreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kDanceProperties.size(),
              ostinato::test::kExpectedDanceEntries.size());
    for (std::size_t i = 0; i < ostinato::kDanceProperties.size(); ++i) {
        const auto& expected = ostinato::test::kExpectedDanceEntries[i];
        EXPECT_EQ(expected.id, i) << "fixture entry " << i;
        EXPECT_EQ(static_cast<std::size_t>(ostinato::kDanceProperties[i].id), i)
            << "table entry " << i;
        EXPECT_EQ(std::memcmp(&ostinato::kDanceProperties[i].record,
                              &expected.record, 4), 0)
            << "dance index " << i;
    }
}

// The lookup indexes by DanceId. Spot-check a named record's semantic
// surface against the upstream rows (water rondo: EL_NINO, PLASMA, SPECTER,
// WILD_BEAR in slot/probability-tier order).
TEST(DanceProperties, LookupSemanticSurface) {
    using ostinato::AttackId;
    using ostinato::DanceId;

    const auto& waterRondo =
        ostinato::getDanceProperties(DanceId::WATER_RONDO);
    EXPECT_EQ(waterRondo.attacks[0], AttackId::EL_NINO);
    EXPECT_EQ(waterRondo.attacks[1], AttackId::PLASMA);
    EXPECT_EQ(waterRondo.attacks[2], AttackId::SPECTER);
    EXPECT_EQ(waterRondo.attacks[3], AttackId::WILD_BEAR);

    const auto& windSong = ostinato::getDanceProperties(DanceId::WIND_SONG);
    EXPECT_EQ(windSong.attacks[0], AttackId::WIND_SLASH);
    EXPECT_EQ(windSong.attacks[3], AttackId::COKATRICE);
}

}  // namespace
