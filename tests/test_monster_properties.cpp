// Full-corpus tests of the monster-properties record table and the two
// metamorph tables. The byte-equivalence tests assert EVERY packed record is
// byte-identical to the ROM bytes (no subset) and that every entry's identity
// field matches its position; the semantic tests exercise the lookups and the
// builder round-trips the emitted rows depend on.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/metamorph.h"
#include "data/monster_properties.h"

#include "ostinato/blocked_status_set.h"
#include "ostinato/element.h"
#include "ostinato/element_set.h"
#include "ostinato/item_id.h"
#include "ostinato/metamorph_info.h"
#include "ostinato/monster_flags.h"
#include "ostinato/monster_id.h"
#include "ostinato/monster_special_attack.h"
#include "ostinato/status_id.h"

#include "fixtures/metamorph_expected.h"
#include "fixtures/monster_prop_expected.h"

namespace {

// Full corpus: identity fields on both sides match the position, and one
// memcmp per packed record catches field-order, padding, decomposition, and
// builder drift against the ROM bytes in a single comparison.
TEST(MonsterProperties, AllRecordsAreByteIdenticalToRom) {
    const auto table = ostinato::monsterProperties();
    ASSERT_EQ(table.size(), ostinato::test::kExpectedMonsterEntries.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& expected = ostinato::test::kExpectedMonsterEntries[i];
        EXPECT_EQ(expected.id, i) << "fixture entry " << i;
        EXPECT_EQ(static_cast<std::size_t>(table[i].id), i)
            << "table entry " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, &expected.record, 32), 0)
            << "monster index " << i;
    }
}

// The lookup indexes by MonsterId. Spot-check a named record's semantic
// surface against values hand-traced from the ROM record ($000 GUARD:
// 1E 10 64 00 00 64 8C 06 | 28 00 0F 00 30 00 30 00 | 05 00 10 00 |
// 00 00 00 | 00 00 08 | 00 | 00 00 00 00 | 20).
TEST(MonsterProperties, LookupSemanticSurface) {
    using ostinato::Element;
    using ostinato::getMonsterProperties;
    using ostinato::MonsterId;
    using ostinato::MonsterTraitFlag;

    const auto& guard = getMonsterProperties(MonsterId::GUARD);
    EXPECT_EQ(guard.speed, 30u);
    EXPECT_EQ(guard.attackPower, 16u);
    EXPECT_EQ(guard.hitRate, 100u);
    EXPECT_EQ(guard.evade, 0u);
    EXPECT_EQ(guard.magicBlock, 0u);
    EXPECT_EQ(guard.defense, 100u);
    EXPECT_EQ(guard.magicDefense, 140u);
    EXPECT_EQ(guard.magicPower, 6u);
    EXPECT_EQ(guard.hp, 40u);
    EXPECT_EQ(guard.mp, 15u);
    EXPECT_EQ(guard.experience, 48u);
    EXPECT_EQ(guard.gold, 48u);
    EXPECT_EQ(guard.level, 5u);
    EXPECT_TRUE(guard.traitFlags.has(MonsterTraitFlag::HUMAN));
    EXPECT_FALSE(guard.traitFlags.has(MonsterTraitFlag::UNDEAD));
    EXPECT_EQ(guard.battleFlags.bits, 0x00u);
    EXPECT_TRUE(guard.weakElements.has(Element::POISON));
    EXPECT_EQ(guard.attackGraphic, ostinato::ItemId::DIRK);
    EXPECT_EQ(guard.specialAttack.effectClass(), 0x20u);
    EXPECT_FALSE(guard.specialAttack.cantDodge());
    EXPECT_FALSE(guard.specialAttack.noDamage());
}

// The flag bytes surface their RAM-map bits (battle-ram.txt:952-970) through
// named enumerators. OROG carries HUMAN + UNDEAD (trait byte $90); SOLDIER
// carries CANT_RUN (battle-flag byte $08); SKULL_DRGN carries
// DIES_AT_ZERO_MP and five battle flags (byte $CD) — all hand-traced from
// their ROM records.
TEST(MonsterProperties, FlagByteSurfaces) {
    using ostinato::getMonsterProperties;
    using ostinato::MonsterBattleFlag;
    using ostinato::MonsterId;
    using ostinato::MonsterTraitFlag;

    const auto& orog = getMonsterProperties(MonsterId::OROG);
    EXPECT_TRUE(orog.traitFlags.has(MonsterTraitFlag::HUMAN));
    EXPECT_TRUE(orog.traitFlags.has(MonsterTraitFlag::UNDEAD));
    EXPECT_FALSE(orog.traitFlags.has(MonsterTraitFlag::IMP_CRITICAL));

    const auto& soldier = getMonsterProperties(MonsterId::SOLDIER);
    EXPECT_TRUE(soldier.battleFlags.has(MonsterBattleFlag::CANT_RUN));
    EXPECT_FALSE(soldier.battleFlags.has(MonsterBattleFlag::FIRST_STRIKE));

    const auto& skullDragon = getMonsterProperties(MonsterId::SKULL_DRGN);
    EXPECT_TRUE(skullDragon.traitFlags.has(MonsterTraitFlag::DIES_AT_ZERO_MP));
    EXPECT_EQ(skullDragon.battleFlags.bits, 0xCDu);
    EXPECT_TRUE(skullDragon.battleFlags.has(MonsterBattleFlag::CANT_CONTROL));
    EXPECT_TRUE(skullDragon.battleFlags.has(MonsterBattleFlag::SPECIAL_EVENT));
    EXPECT_FALSE(skullDragon.battleFlags.has(MonsterBattleFlag::CANT_SCAN));
}

// MetamorphInfo round-trips the effect's pack/rate decode
// (battle_main.asm:9385-9409): low 5 bits pack, high 3 bits rate. SKULL_DRGN
// carries metamorph byte $86 (pack 6, rate ODDS_1_8), hand-traced.
TEST(MonsterProperties, MetamorphInfoSurface) {
    using ostinato::getMonsterProperties;
    using ostinato::MetamorphInfo;
    using ostinato::MetamorphRate;
    using ostinato::MonsterId;

    const auto info =
        MetamorphInfo::of({.packIndex = 6, .rate = MetamorphRate::ODDS_1_8});
    EXPECT_EQ(info.packed, 0x86u);
    EXPECT_EQ(info.packIndex(), 6u);
    EXPECT_EQ(info.rate(), MetamorphRate::ODDS_1_8);

    const auto& skullDragon = getMonsterProperties(MonsterId::SKULL_DRGN);
    EXPECT_EQ(skullDragon.metamorph.packed, 0x86u);
    EXPECT_EQ(skullDragon.metamorph.rate(), MetamorphRate::ODDS_1_8);
}

// The per-band special-attack builders round-trip the dispatch's decode
// (battle_main.asm:8195-8235). SKULL_DRGN carries the full byte $FF
// (remove-reflect band with 13 dead residual bits + both modifiers),
// hand-traced.
TEST(MonsterProperties, SpecialAttackSurface) {
    using ostinato::getMonsterProperties;
    using ostinato::MonsterId;
    using ostinato::MonsterSpecialAttack;
    using ostinato::StatusId;

    EXPECT_EQ(MonsterSpecialAttack::inflictStatus(StatusId::CONDEMNED).packed,
              0x08u);
    EXPECT_EQ(MonsterSpecialAttack::damageBoost(0).packed, 0x20u);
    EXPECT_EQ(MonsterSpecialAttack::damageBoost(13).packed, 0x2Du);
    EXPECT_EQ(MonsterSpecialAttack::drainHp().packed, 0x30u);
    EXPECT_EQ(MonsterSpecialAttack::drainMp().packed, 0x31u);
    EXPECT_EQ(MonsterSpecialAttack::removeReflect().packed, 0x32u);
    EXPECT_EQ(MonsterSpecialAttack::inflictStatus(StatusId::SLEEP)
                  .withNoDamage().packed,
              0x4Fu);

    const auto& skullDragon = getMonsterProperties(MonsterId::SKULL_DRGN);
    EXPECT_EQ(skullDragon.specialAttack.packed, 0xFFu);
    EXPECT_EQ(MonsterSpecialAttack::removeReflect(13)
                  .withCantDodge().withNoDamage().packed,
              0xFFu);
    EXPECT_TRUE(skullDragon.specialAttack.cantDodge());
    EXPECT_TRUE(skullDragon.specialAttack.noDamage());
}

// BlockedStatusSet packs id -> (byte id/8, bit id%8) over the three
// blocked-status bytes, mirroring StatusSet's rule: BLIND=0 -> byte0 bit0,
// DANCE=$10 -> byte2 bit0, REFLECT=$17 -> byte2 bit7.
TEST(MonsterProperties, BlockedStatusSetRoundTrip) {
    using ostinato::BlockedStatusSet;
    using ostinato::StatusId;

    const auto blocked = BlockedStatusSet::of(StatusId::BLIND, StatusId::DANCE,
                                              StatusId::REFLECT);
    EXPECT_EQ(blocked.bytes[0], 0x01u);
    EXPECT_EQ(blocked.bytes[1], 0x00u);
    EXPECT_EQ(blocked.bytes[2], 0x81u);
    EXPECT_TRUE(blocked.has(StatusId::BLIND));
    EXPECT_TRUE(blocked.has(StatusId::REFLECT));
    EXPECT_FALSE(blocked.has(StatusId::SLEEP));
    EXPECT_EQ(BlockedStatusSet{}.bytes[0], 0x00u);
}

// Full corpus: every metamorph pack is byte-identical to the ROM pack and
// every entry's index field matches its position.
TEST(Metamorph, AllPacksAreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kMetamorphPacks.size(),
              ostinato::test::kExpectedMetamorphPacks.size());
    for (std::size_t i = 0; i < ostinato::kMetamorphPacks.size(); ++i) {
        const auto& expected = ostinato::test::kExpectedMetamorphPacks[i];
        EXPECT_EQ(expected.index, i) << "fixture pack " << i;
        EXPECT_EQ(ostinato::kMetamorphPacks[i].index, i) << "table pack " << i;
        EXPECT_EQ(std::memcmp(&ostinato::kMetamorphPacks[i].record,
                              &expected.record, 4), 0)
            << "pack index " << i;
    }
}

// Full corpus: the eight rate thresholds match the ROM row and every entry's
// identity enumerator matches its position.
TEST(Metamorph, AllRatesMatchRom) {
    ASSERT_EQ(ostinato::kMetamorphRates.size(),
              ostinato::test::kExpectedMetamorphRates.size());
    for (std::size_t i = 0; i < ostinato::kMetamorphRates.size(); ++i) {
        const auto& expected = ostinato::test::kExpectedMetamorphRates[i];
        EXPECT_EQ(expected.index, i) << "fixture rate " << i;
        EXPECT_EQ(static_cast<std::size_t>(ostinato::kMetamorphRates[i].id), i)
            << "table rate " << i;
        EXPECT_EQ(ostinato::kMetamorphRates[i].value, expected.value)
            << "rate index " << i;
    }
}

// The accessors key off the packed MetamorphInfo byte exactly as the effect
// does: pack 0's first item is hand-traced from the .dat ($F2); the ODDS_1_8
// threshold is $20 from the MetamorphRateTbl row.
TEST(Metamorph, AccessorsKeyedByMetamorphInfo) {
    using ostinato::getMetamorphPack;
    using ostinato::MetamorphInfo;
    using ostinato::MetamorphRate;
    using ostinato::metamorphRate;

    const auto packZero = MetamorphInfo::of(
        {.packIndex = 0, .rate = MetamorphRate::ODDS_255_256});
    EXPECT_EQ(getMetamorphPack(packZero).items[0],
              static_cast<ostinato::ItemId>(0xF2));
    EXPECT_EQ(metamorphRate(packZero), 0xFFu);

    const auto skullDragonInfo =
        MetamorphInfo::of({.packIndex = 6, .rate = MetamorphRate::ODDS_1_8});
    EXPECT_EQ(metamorphRate(skullDragonInfo), 0x20u);
    EXPECT_EQ(metamorphRate(MetamorphInfo::of(
                  {.packIndex = 0, .rate = MetamorphRate::NEVER})),
              0x00u);
}

}  // namespace
