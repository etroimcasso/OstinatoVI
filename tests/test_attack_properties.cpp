// Full-corpus test of the attack-properties table. The byte-equivalence test
// asserts EVERY one of the 256 packed records is byte-identical to the ROM's
// 14-byte record (no subset) and that every entry's identity field matches its
// position; the semantic tests exercise the lookup and the builder round-trips
// the emitted rows depend on. The JP language variant is pending a J-ROM rip —
// visible skip below, registered on every platform.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/attack_properties.h"

#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/element.h"
#include "ostinato/element_set.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "ostinato/target_flags.h"
#include "ostinato/targeting.h"

#include "fixtures/magic_prop_expected.h"

namespace {

// Full corpus: identity fields on both sides match the position, and one
// memcmp per packed record catches field-order, padding, decomposition, and
// builder drift in a single byte-for-byte comparison against the ROM.
TEST(AttackProperties, AllRecordsAreByteIdenticalToRom) {
    const auto table = ostinato::attackPropertiesEn();
    ASSERT_EQ(table.size(), ostinato::test::kExpectedAttackEntries.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& expected = ostinato::test::kExpectedAttackEntries[i];
        EXPECT_EQ(expected.id, i) << "fixture entry " << i;
        EXPECT_EQ(static_cast<std::size_t>(table[i].id), i)
            << "table entry " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, &expected.record, 14), 0)
            << "attack index " << i;
    }
}

// The lookup indexes by AttackId. Spot-check a named record's semantic
// surface against values hand-traced from the ROM record ($00 FIRE:
// 61 01 00 28 00 04 15 00 96 FF 00*4).
TEST(AttackProperties, LookupSemanticSurface) {
    using ostinato::AttackFlag1;
    using ostinato::AttackId;
    using ostinato::Element;
    using ostinato::getAttackProperties;

    const auto& fire = getAttackProperties(AttackId::FIRE);
    EXPECT_EQ(fire.targeting.bits, 0x61u);
    EXPECT_TRUE(fire.element.has(Element::FIRE));
    EXPECT_FALSE(fire.element.has(Element::ICE));
    EXPECT_EQ(fire.traits.bits, 0x00u);
    EXPECT_TRUE(fire.flags1.has(AttackFlag1::ENABLE_RUNIC));
    EXPECT_TRUE(fire.flags1.has(AttackFlag1::RETARGET_IF_INVALID));
    EXPECT_FALSE(fire.flags1.has(AttackFlag1::AFFECT_MP));
    EXPECT_EQ(fire.mpCost, 4u);
    EXPECT_EQ(fire.power, 21u);
    EXPECT_EQ(fire.hitRate, 150u);
    EXPECT_EQ(fire.specialEffect, ostinato::kNoSpecialEffect);
}

// Builder round-trips: every of(...) builder re-packs to the raw ROM byte the
// parser decomposed. One case per builder family, values chosen so each bit
// path is exercised.
TEST(AttackProperties, BuilderRoundTrips) {
    using ostinato::AttackFlag1;
    using ostinato::AttackFlags1;
    using ostinato::AttackMiscFlag;
    using ostinato::AttackMiscFlags;
    using ostinato::Element;
    using ostinato::ElementSet;
    using ostinato::StatusId;
    using ostinato::StatusSet;
    using ostinato::TargetFlags;
    using ostinato::Targeting;

    EXPECT_EQ(Targeting::of(TargetFlags::MANUAL, TargetFlags::MULTI_TARGET,
                            TargetFlags::ENEMY).bits,
              0x61u);
    EXPECT_EQ(Targeting::of(TargetFlags::MENU).bits, 0xFFu);
    EXPECT_EQ(Targeting{}.bits, 0x00u);

    EXPECT_EQ(ElementSet::of(Element::FIRE, Element::ICE).bits, 0x03u);
    EXPECT_EQ(ElementSet{}.bits, 0x00u);

    EXPECT_EQ(AttackFlags1::of(AttackFlag1::ENABLE_RUNIC,
                               AttackFlag1::RETARGET_IF_INVALID).bits,
              0x28u);
    EXPECT_EQ(AttackMiscFlags::of(AttackMiscFlag::MISS_IF_STATUS_IMMUNE,
                                  AttackMiscFlag::SHOW_ATTACK_MESSAGE).bits,
              0x03u);

    // StatusId packs id -> (byte id/8, bit id%8): BLIND=0 -> byte0 bit0,
    // DANCE=0x10 -> byte2 bit0, FLOAT=0x1F -> byte3 bit7.
    const auto statuses =
        StatusSet::of(StatusId::BLIND, StatusId::DANCE, StatusId::FLOAT);
    EXPECT_EQ(statuses.bytes[0], 0x01u);
    EXPECT_EQ(statuses.bytes[1], 0x00u);
    EXPECT_EQ(statuses.bytes[2], 0x01u);
    EXPECT_EQ(statuses.bytes[3], 0x80u);
}

// The JP table (magic_prop_jp.dat) is language-variant upstream and pending a
// J-ROM rip — the skip stays visible on every platform until it lands.
TEST(AttackProperties, JpVariantTable) {
    GTEST_SKIP() << "magic_prop JP variant pending J-ROM rip";
}

}  // namespace
