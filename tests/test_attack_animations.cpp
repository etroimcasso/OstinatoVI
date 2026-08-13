// Full-corpus tests for the btlgfx attack/animation data layer. Every table is
// checked entry-by-entry against its parser-emitted fixture (the ground-truth
// ROM bytes), independent of the typed rows, so any decode or re-emit drift in
// either artifact fails loudly. Plus typed-wrapper round-trips, hand-traced
// spot-checks, and accessor traces.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/attack_animations.h"

#include "fixtures/attack_anim_prop_expected.h"
#include "fixtures/attack_gfx_prop_expected.h"
#include "fixtures/item_anim_ptrs_expected.h"
#include "fixtures/item_jump_throw_anim_expected.h"
#include "fixtures/monster_attack_anim_prop_expected.h"
#include "fixtures/monster_overlap_expected.h"
#include "fixtures/weapon_anim_prop_expected.h"

namespace {

using namespace ostinato;

// --- full-corpus byte-equivalence -------------------------------------------

TEST(AttackAnimations, AttackAnimPropMatchesRom) {
    const auto table = attackAnimationProperties();
    ASSERT_EQ(table.size(), test::kExpectedAttackAnimProp.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& exp = test::kExpectedAttackAnimProp[i];
        EXPECT_EQ(table[i].index, exp.index) << "index at " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, exp.bytes.data(),
                              sizeof(AttackAnimationProperties)),
                  0)
            << "record bytes at " << i;
    }
}

TEST(AttackAnimations, AttackGfxPropMatchesRom) {
    const auto table = animationGraphicsProperties();
    ASSERT_EQ(table.size(), test::kExpectedAnimationGfxProp.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& exp = test::kExpectedAnimationGfxProp[i];
        EXPECT_EQ(table[i].index, exp.index) << "index at " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, exp.bytes.data(),
                              sizeof(AnimationGraphicsProperties)),
                  0)
            << "record bytes at " << i;
    }
}

TEST(AttackAnimations, WeaponAnimPropMatchesRom) {
    const auto table = weaponAnimationProperties();
    ASSERT_EQ(table.size(), test::kExpectedWeaponAnimProp.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& exp = test::kExpectedWeaponAnimProp[i];
        EXPECT_EQ(static_cast<std::uint16_t>(table[i].item), exp.index)
            << "item at " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, exp.bytes.data(),
                              sizeof(WeaponAnimationProperties)),
                  0)
            << "record bytes at " << i;
    }
}

TEST(AttackAnimations, MonsterAttackAnimPropMatchesRom) {
    const auto table = monsterAttackAnimationProperties();
    ASSERT_EQ(table.size(), test::kExpectedMonsterAttackAnimProp.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& exp = test::kExpectedMonsterAttackAnimProp[i];
        EXPECT_EQ(static_cast<std::uint16_t>(table[i].animation), exp.index)
            << "animation at " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, exp.bytes.data(),
                              sizeof(WeaponAnimationProperties)),
                  0)
            << "record bytes at " << i;
    }
}

TEST(AttackAnimations, ItemJumpThrowMatchesRom) {
    // Reassemble the 257 raw bytes: row 0 is the unarmed slot, rows 1-256 the
    // items, then memcmp against the fixture.
    std::array<std::uint8_t, 257> got{};
    got[0] = kUnarmedItemThrowAnimation.bits;
    const auto table = itemThrowAnimations();
    ASSERT_EQ(table.size(), 256u);
    for (std::size_t i = 0; i < table.size(); ++i) {
        got[i + 1] = table[i].animation.bits;
    }
    EXPECT_EQ(std::memcmp(got.data(), test::kExpectedItemJumpThrowAnim.data(),
                          got.size()),
              0);
}

TEST(AttackAnimations, ItemAnimPtrsReMultiplyMatchesRom) {
    // The port stores de-multiplied row indices; re-multiply by 14 (or 0xFFFF
    // for NONE) to recover the raw ROM words.
    const auto table = usableItemAnimations();
    ASSERT_EQ(table.size(), test::kExpectedItemAnimPtrs.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto anim = table[i].animation;
        const std::uint16_t word =
            anim.isNone() ? 0xFFFF
                          : static_cast<std::uint16_t>(anim.index() * 14);
        EXPECT_EQ(word, test::kExpectedItemAnimPtrs[i]) << "ptr at " << i;
    }
}

TEST(AttackAnimations, MonsterOverlapMatchesRom) {
    const auto table = monsterOverlaps();
    ASSERT_EQ(table.size(), test::kExpectedMonsterOverlapEntries.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& exp = test::kExpectedMonsterOverlapEntries[i];
        EXPECT_EQ(static_cast<std::uint16_t>(table[i].monster), exp.id)
            << "monster at " << i;
        EXPECT_EQ(table[i].yShift, exp.yShift) << "yShift at " << i;
    }
}

// --- typed-wrapper round-trips ----------------------------------------------

TEST(AttackAnimations, AnimationRefRoundTrips) {
    EXPECT_TRUE(AnimationRef::NONE.isNone());
    EXPECT_EQ(AnimationRef::of(193).index(), 193);
    EXPECT_FALSE(AnimationRef::of(193).hasHighBit());
    EXPECT_TRUE(AnimationRef::withHighBit(221).hasHighBit());
    EXPECT_EQ(AnimationRef::withHighBit(221).index(), 221);
    EXPECT_EQ(AnimationRef::withHighBit(221).raw(), 0x80DD);
}

TEST(AttackAnimations, AnimationTileOffsetRoundTrips) {
    EXPECT_FALSE(AnimationTileOffset::of(100).is2bpp());
    EXPECT_EQ(AnimationTileOffset::of(100).offset(), 100);
    EXPECT_TRUE(AnimationTileOffset::of2bpp(25601).is2bpp());
    EXPECT_EQ(AnimationTileOffset::of2bpp(25601).offset(), 25601);
}

TEST(AttackAnimations, AnimationInitFunctionRoundTrips) {
    EXPECT_EQ(AnimationInitFunction::of(37).index(), 37);
    EXPECT_FALSE(AnimationInitFunction::of(37).hasHighBit());
    EXPECT_TRUE(AnimationInitFunction::withHighBit(2).hasHighBit());
    EXPECT_EQ(AnimationInitFunction::withHighBit(2).index(), 2);
}

TEST(AttackAnimations, ThrownAnimationFlagsRoundTrips) {
    const auto a = ThrownAnimationFlags::of(WeaponAnimationType::UNKNOWN_0);
    EXPECT_FALSE(a.isThrown());
    EXPECT_EQ(a.animationType(), WeaponAnimationType::UNKNOWN_0);
    const auto b = ThrownAnimationFlags::thrown(WeaponAnimationType::STAR_OR_GAMBLER);
    EXPECT_TRUE(b.isThrown());
    EXPECT_EQ(b.animationType(), WeaponAnimationType::STAR_OR_GAMBLER);
    EXPECT_EQ(b.bits, 0x81);
}

TEST(AttackAnimations, ItemThrowAnimationRoundTrips) {
    const auto a = ItemThrowAnimation::of(JumpAnimationClass::SPEAR,
                                          ThrowAnimationClass::BOOMERANG);
    EXPECT_FALSE(a.usesFightAnimation());
    EXPECT_EQ(a.jumpAnimation(), JumpAnimationClass::SPEAR);
    EXPECT_EQ(a.throwAnimation(), ThrowAnimationClass::BOOMERANG);
    const auto b = ItemThrowAnimation::fightAnimation(
        JumpAnimationClass::UNARMED, ThrowAnimationClass::FIRE_SKEAN);
    EXPECT_TRUE(b.usesFightAnimation());
    EXPECT_EQ(b.throwAnimation(), ThrowAnimationClass::FIRE_SKEAN);
}

TEST(AttackAnimations, AttackAnimationIndexRoundTrips) {
    EXPECT_TRUE(AttackAnimationIndex::NONE.isNone());
    EXPECT_FALSE(AttackAnimationIndex::of(402).isNone());
    EXPECT_EQ(AttackAnimationIndex::of(402).index(), 402);
}

// --- hand-traced spot-checks ------------------------------------------------

TEST(AttackAnimations, SpotCheckBitFifteenAnimation) {
    // AttackAnimProp row 1 carries a high-bit bg1 animation (withHighBit(221)).
    const auto& row = attackAnimationProperties(1);
    EXPECT_TRUE(row.bg1Animation.hasHighBit());
    EXPECT_EQ(row.bg1Animation.index(), 221);
    // Row 0's sprite animation is a plain reference; its bg animations are NONE.
    const auto& row0 = attackAnimationProperties(0);
    EXPECT_EQ(row0.spriteAnimation.index(), 193);
    EXPECT_TRUE(row0.bg1Animation.isNone());
    EXPECT_EQ(row0.defaultSoundEffect, 22);
}

TEST(AttackAnimations, SpotCheck2bppGraphicsRowExists) {
    // 2bpp rows exist and non-2bpp rows exist; a 2bpp row's raw word carries
    // bit 15.
    bool saw2bpp = false;
    bool saw4bpp = false;
    for (const auto& entry : animationGraphicsProperties()) {
        if (entry.record.tileOffset.is2bpp()) {
            saw2bpp = true;
            EXPECT_NE(entry.record.tileOffset.raw & 0x8000, 0);
        } else {
            saw4bpp = true;
        }
    }
    EXPECT_TRUE(saw2bpp);
    EXPECT_TRUE(saw4bpp);
}

TEST(AttackAnimations, SpotCheckUsableItemAnimation) {
    // ItemAnimPtrs entry 7 is RENAME_CARD ($e7) -> AttackAnimProp row 402.
    const auto table = usableItemAnimations();
    EXPECT_EQ(table[7].item, ItemId::RENAME_CARD);
    EXPECT_FALSE(table[7].animation.isNone());
    EXPECT_EQ(table[7].animation.index(), 402);
    // The first entries ($e0..$e6) and the last ($ff) have no animation.
    EXPECT_TRUE(table[0].animation.isNone());
    EXPECT_TRUE(table[31].animation.isNone());
}

TEST(AttackAnimations, SpotCheckItemJumpThrowDecode) {
    // ItemJumpThrowAnim [$00] DIRK: jump + throw are both the thin-knife class.
    const auto dirk = itemThrowAnimation(ItemId::DIRK);
    EXPECT_FALSE(dirk.usesFightAnimation());
    EXPECT_EQ(dirk.jumpAnimation(), JumpAnimationClass::THIN_KNIFE);
    EXPECT_EQ(dirk.throwAnimation(), ThrowAnimationClass::THIN_KNIFE);
}

TEST(AttackAnimations, SpotCheckMonsterOverlap) {
    // TENTACLE carries a large sprite-priority y shift; a zero-shift monster
    // stays zero.
    EXPECT_EQ(monsterOverlap(MonsterId::TENTACLE), 72);
    EXPECT_EQ(monsterOverlap(MonsterId::GUARD), 0);
}

TEST(AttackAnimations, SpotCheckThrownWeaponExists) {
    // At least one weapon is thrown with the star/gambler animation type.
    bool sawThrownStar = false;
    for (const auto& entry : weaponAnimationProperties()) {
        if (entry.record.thrown.isThrown() &&
            entry.record.thrown.animationType() ==
                WeaponAnimationType::STAR_OR_GAMBLER) {
            sawThrownStar = true;
        }
    }
    EXPECT_TRUE(sawThrownStar);
}

// --- accessor traces --------------------------------------------------------

TEST(AttackAnimations, AccessorsMatchTableRows) {
    EXPECT_EQ(&attackAnimationProperties(5),
              &attackAnimationProperties()[5].record);
    EXPECT_EQ(&animationGraphicsProperties(9),
              &animationGraphicsProperties()[9].record);
    // The weapon/monster shared record keyed two ways.
    const auto& weapon0 = weaponAnimationProperties(ItemId::DIRK);
    EXPECT_EQ(&weapon0, &weaponAnimationProperties()[0].record);
    const auto& hit = monsterAttackAnimationProperties(MonsterAttackAnimation::HIT);
    EXPECT_EQ(&hit, &monsterAttackAnimationProperties()[0].record);
}

// --- JP-variant deferrals (R3) ----------------------------------------------

TEST(AttackAnimations, AttackAnimPropJpVariant) {
    GTEST_SKIP() << "JP attack_anim_prop deferred until the JP ROM is rippable "
                    "(R3); the EN table is the sole backing store for now";
}

TEST(AttackAnimations, AttackGfxPropJpVariant) {
    GTEST_SKIP() << "JP attack_gfx_prop deferred until the JP ROM is rippable "
                    "(R3); the EN table is the sole backing store for now";
}

}  // namespace
