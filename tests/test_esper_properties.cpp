// Full-corpus test of the esper properties table (PLAN phase-1.B D5 +
// Amendment B1). The byte-equivalence test asserts EVERY one of the 27
// packed records is byte-identical to the ROM's 11-byte record (no subset)
// and that every entry's identity field matches its position in the
// $36-based GENJU index space; the semantic tests exercise the lookup, the
// rate-first pair order, and the blank-slot / missing-bonus sentinels.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/esper_properties.h"

#include "ostinato/attack_id.h"
#include "ostinato/esper_bonus.h"
#include "ostinato/esper_id.h"

#include "fixtures/genju_prop_expected.h"

namespace {

// Full corpus: identity fields on both sides match the position (offset by
// the parser-derived first esper id), and one memcmp per packed record
// catches field-order, pair-order, and sentinel drift in a single
// byte-for-byte comparison against the ROM.
TEST(EsperProperties, AllRecordsAreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kEsperProperties.size(),
              ostinato::test::kExpectedEsperEntries.size());
    for (std::size_t i = 0; i < ostinato::kEsperProperties.size(); ++i) {
        const auto& expected = ostinato::test::kExpectedEsperEntries[i];
        EXPECT_EQ(expected.id, i + ostinato::test::kExpectedEsperFirstId)
            << "fixture entry " << i;
        EXPECT_EQ(static_cast<std::size_t>(ostinato::kEsperProperties[i].id),
                  static_cast<std::size_t>(expected.id))
            << "table entry " << i;
        EXPECT_EQ(std::memcmp(&ostinato::kEsperProperties[i].record,
                              &expected.record, 11), 0)
            << "esper index " << i;
    }
}

// The lookup indexes by EsperId ($36..$50). Spot-check named records'
// semantic surfaces against values hand-traced from genju_prop.asm: Ramuh's
// rate-first pairs, and Starlet's bonus.
TEST(EsperProperties, LookupSemanticSurface) {
    using ostinato::AttackId;
    using ostinato::EsperBonus;
    using ostinato::EsperId;
    using ostinato::getEsperProperties;

    const auto& ramuh = getEsperProperties(EsperId::RAMUH);
    EXPECT_EQ(ramuh.spells[0].learnRate, 10u);
    EXPECT_EQ(ramuh.spells[0].spell, AttackId::BOLT);
    EXPECT_EQ(ramuh.spells[1].learnRate, 2u);
    EXPECT_EQ(ramuh.spells[1].spell, AttackId::BOLT_2);
    EXPECT_EQ(ramuh.spells[2].learnRate, 5u);
    EXPECT_EQ(ramuh.spells[2].spell, AttackId::POISON);
    EXPECT_EQ(ramuh.bonus, EsperBonus::STAMINA_1);

    const auto& starlet = getEsperProperties(EsperId::STARLET);
    EXPECT_EQ(starlet.spells[0].learnRate, 25u);
    EXPECT_EQ(starlet.spells[0].spell, AttackId::CURE);
    EXPECT_EQ(starlet.bonus, EsperBonus::STAMINA_2);
}

// Empty slots and missing bonuses come from the upstream macro's blank-arg
// semantics: Ragnarok holds one spell + four blank slots + no bonus; Shiva
// holds five spells + no bonus (bonus-less is independent of slot count).
TEST(EsperProperties, BlankSlotAndMissingBonusSentinels) {
    using ostinato::AttackId;
    using ostinato::EsperBonus;
    using ostinato::EsperId;
    using ostinato::getEsperProperties;

    const auto& ragnarok = getEsperProperties(EsperId::RAGNAROK);
    EXPECT_EQ(ragnarok.spells[0].learnRate, 1u);
    EXPECT_EQ(ragnarok.spells[0].spell, AttackId::ULTIMA);
    for (std::size_t slot = 1; slot < 5; ++slot) {
        EXPECT_EQ(ragnarok.spells[slot].learnRate, 0u) << "slot " << slot;
        EXPECT_EQ(ragnarok.spells[slot].spell, AttackId::NONE)
            << "slot " << slot;
    }
    EXPECT_EQ(ragnarok.bonus, EsperBonus::NONE);

    const auto& shiva = getEsperProperties(EsperId::SHIVA);
    EXPECT_EQ(shiva.spells[4].spell, AttackId::CURE);
    EXPECT_EQ(shiva.bonus, EsperBonus::NONE);
}

}  // namespace
