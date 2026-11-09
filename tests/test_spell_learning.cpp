// The spells learned further at a won battle's end (docs/contracts/battle_espers.md, "A spell
// learned a little further" and "The esper's spells"): a rate of 0 and a learned spell change nothing;
// otherwise the percentage goes up by rate × magic points and becomes "just learned" at 100 or more,
// a store counting as progress with 0 magic points too; an esper's five slots in order, the empty ones
// passed over; every esper's spells over the whole table; the product kept to its low byte, and in
// play never passing one.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "battle/spell_learning.h"
#include "data/battle_magic_points.h"
#include "data/esper_properties.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/esper_id.h"

namespace ostinato::test {
namespace {

using battle::addSpellProgress;
using battle::learnFromEsper;
using game::SpellProgress;

std::uint8_t& percent(SpellProgress& progress, AttackId spell) {
    return progress.progress[static_cast<std::size_t>(spell)];
}

// --- a spell learned a little further ------------------------------------------------------------

TEST(SpellLearning, TheJustLearnedMarkIsNotYetKnown) {
    SpellProgress progress;
    percent(progress, AttackId::FIRE) = SpellProgress::kSpellJustLearned;
    EXPECT_EQ(SpellProgress::kSpellJustLearned, 128);
    EXPECT_FALSE(progress.knows(AttackId::FIRE));
    percent(progress, AttackId::FIRE) = SpellProgress::kSpellLearned;
    EXPECT_TRUE(progress.knows(AttackId::FIRE));
}

TEST(SpellLearning, ARateOfZeroChangesNothing) {
    SpellProgress progress;
    percent(progress, AttackId::FIRE) = 40;
    EXPECT_FALSE(addSpellProgress(progress, AttackId::FIRE, 0, 10));
    EXPECT_EQ(percent(progress, AttackId::FIRE), 40);
}

TEST(SpellLearning, ALearnedSpellChangesNothing) {
    SpellProgress progress;
    percent(progress, AttackId::FIRE) = SpellProgress::kSpellLearned;
    EXPECT_FALSE(addSpellProgress(progress, AttackId::FIRE, 10, 10));
    EXPECT_EQ(percent(progress, AttackId::FIRE), SpellProgress::kSpellLearned);
}

TEST(SpellLearning, NoMagicPointsLeaveThePercentageButCountAsProgress) {
    SpellProgress progress;
    percent(progress, AttackId::FIRE) = 40;
    EXPECT_TRUE(addSpellProgress(progress, AttackId::FIRE, 10, 0));
    EXPECT_EQ(percent(progress, AttackId::FIRE), 40);
}

TEST(SpellLearning, ASumBelowOneHundredIsStored) {
    SpellProgress progress;
    percent(progress, AttackId::FIRE) = 40;
    EXPECT_TRUE(addSpellProgress(progress, AttackId::FIRE, 10, 5));
    EXPECT_EQ(percent(progress, AttackId::FIRE), 90);
    EXPECT_TRUE(addSpellProgress(progress, AttackId::FIRE, 3, 3));
    EXPECT_EQ(percent(progress, AttackId::FIRE), 99);
}

TEST(SpellLearning, ASumOfExactlyOneHundredIsJustLearned) {
    SpellProgress progress;
    percent(progress, AttackId::FIRE) = 50;
    EXPECT_TRUE(addSpellProgress(progress, AttackId::FIRE, 10, 5));
    EXPECT_EQ(percent(progress, AttackId::FIRE), SpellProgress::kSpellJustLearned);
}

TEST(SpellLearning, ASumPastAByteIsJustLearned) {
    SpellProgress progress;
    percent(progress, AttackId::CURE) = 95;
    EXPECT_TRUE(addSpellProgress(progress, AttackId::CURE, 25, 10));
    EXPECT_EQ(percent(progress, AttackId::CURE), SpellProgress::kSpellJustLearned);
}

// A spell learned from an item and again from the esper in the same win.
TEST(SpellLearning, AJustLearnedSpellStaysJustLearned) {
    SpellProgress progress;
    percent(progress, AttackId::FIRE) = SpellProgress::kSpellJustLearned;
    EXPECT_TRUE(addSpellProgress(progress, AttackId::FIRE, 3, 2));
    EXPECT_EQ(percent(progress, AttackId::FIRE), SpellProgress::kSpellJustLearned);
}

// --- the esper's spells --------------------------------------------------------------------------

// Ramuh teaches Bolt at 10, Bolt 2 at 2 and Poison at 5, and has two empty slots.
TEST(SpellLearning, RamuhTeachesHisThreeSpellsAndNothingElse) {
    SpellProgress progress;
    EXPECT_TRUE(learnFromEsper(progress, EsperId::RAMUH, 5));
    EXPECT_EQ(percent(progress, AttackId::BOLT), 50);
    EXPECT_EQ(percent(progress, AttackId::BOLT_2), 10);
    EXPECT_EQ(percent(progress, AttackId::POISON), 25);
    const auto taught = std::count_if(progress.progress.begin(), progress.progress.end(),
                                      [](std::uint8_t learned) { return learned != 0; });
    EXPECT_EQ(taught, 3);
}

TEST(SpellLearning, AnEsperWhoseSpellsAreAllLearnedTeachesNothing) {
    SpellProgress progress;
    for (const AttackId spell : {AttackId::BOLT, AttackId::BOLT_2, AttackId::POISON}) {
        percent(progress, spell) = SpellProgress::kSpellLearned;
    }
    EXPECT_FALSE(learnFromEsper(progress, EsperId::RAMUH, 10));
}

TEST(SpellLearning, OneSpellStillToLearnIsProgress) {
    SpellProgress progress;
    percent(progress, AttackId::BOLT) = SpellProgress::kSpellLearned;
    percent(progress, AttackId::BOLT_2) = SpellProgress::kSpellLearned;
    EXPECT_TRUE(learnFromEsper(progress, EsperId::RAMUH, 1));
    EXPECT_EQ(percent(progress, AttackId::POISON), 5);
}

// Starlet teaches Cure at 25 and Cure 2 at 16: with 10 magic points both pass 100 from nothing.
TEST(SpellLearning, StarletTeachesCureAndCure2InOneBattleOfTenMagicPoints) {
    SpellProgress progress;
    EXPECT_TRUE(learnFromEsper(progress, EsperId::STARLET, 10));
    EXPECT_EQ(percent(progress, AttackId::CURE), SpellProgress::kSpellJustLearned);
    EXPECT_EQ(percent(progress, AttackId::CURE_2), SpellProgress::kSpellJustLearned);
    EXPECT_EQ(percent(progress, AttackId::CURE_3), 10);
    EXPECT_EQ(percent(progress, AttackId::REGEN), SpellProgress::kSpellJustLearned);
    EXPECT_EQ(percent(progress, AttackId::REMEDY), SpellProgress::kSpellJustLearned);
}

// Every esper from nothing with 10 magic points: each spell it teaches stands at its rate × 10, or
// just learned at 100 or more, and every other spell is untouched.
TEST(SpellLearning, EveryEsperTeachesItsOwnSpellsAtItsOwnRates) {
    for (const EsperPropertiesEntry& entry : kEsperProperties) {
        SpellProgress progress;
        EXPECT_TRUE(learnFromEsper(progress, entry.id, 10)) << static_cast<int>(entry.id);
        SpellProgress expected;
        for (const EsperSpell& slot : entry.record.spells) {
            if (slot.spell == AttackId::NONE) {
                continue;
            }
            const int sum = slot.learnRate * 10;
            percent(expected, slot.spell) =
                sum >= 100 ? SpellProgress::kSpellJustLearned : static_cast<std::uint8_t>(sum);
        }
        EXPECT_EQ(progress.progress, expected.progress) << static_cast<int>(entry.id);
    }
}

// The amount added is the low byte of rate × magic points: 30 × 10 = 300 adds 44, and 25 × 12 = 300
// from 50 reaches 94, short of learned.
TEST(SpellLearning, TheAmountIsTheProductsLowByte) {
    SpellProgress progress;
    EXPECT_TRUE(addSpellProgress(progress, AttackId::FIRE, 30, 10));
    EXPECT_EQ(percent(progress, AttackId::FIRE), 44);
    percent(progress, AttackId::CURE) = 50;
    EXPECT_TRUE(addSpellProgress(progress, AttackId::CURE, 25, 12));
    EXPECT_EQ(percent(progress, AttackId::CURE), 94);
    // 16 × 16 = 256 adds nothing at all.
    percent(progress, AttackId::ICE) = 10;
    EXPECT_TRUE(addSpellProgress(progress, AttackId::ICE, 16, 16));
    EXPECT_EQ(percent(progress, AttackId::ICE), 10);
}

// No esper's rate times any battle's magic points passes a byte, so in play the low byte is the
// whole product.
TEST(SpellLearning, NoEsperRateTimesABattlesMagicPointsPassesAByte) {
    int largestRate = 0;
    for (const EsperPropertiesEntry& entry : kEsperProperties) {
        for (const EsperSpell& slot : entry.record.spells) {
            largestRate = std::max<int>(largestRate, slot.learnRate);
        }
    }
    int largestAward = 0;
    for (const BattleMagicPointsEntry& entry : kBattleMagicPoints) {
        largestAward = std::max<int>(largestAward, entry.magicPoints);
    }
    EXPECT_EQ(largestRate, 25);
    EXPECT_EQ(largestAward, 10);
    EXPECT_LE(largestRate * largestAward, 255);
}

}  // namespace
}  // namespace ostinato::test
