// The damage to one target: the variance and the defenses, fractional damage, who gains, the
// elements, the Atma Weapon, what a blow does besides, drains and the ledger. Each case is worked
// out three ways — the port, by hand from the original, and the cartridge's own routine through the
// damage oracle where the run asks it (tests/with_cartridge.h) — and every other cell the pass writes
// is compared between the port and the cartridge as well. The seeded sweep, the port against the
// cartridge alone, is in tests/oracle/test_target_damage.cpp.
//
// Authority: docs/contracts/battle_damage.md.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

#include "battle/attack.h"
#include "battle/damage.h"
#include "oracle/damage_oracle.h"
#include "ostinato/attack_flags.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/element.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "target_damage_cases.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::DamageScenario;
using trace::TargetDamageResult;

// The three-way check: the port's answer, the value worked out by hand, and the cartridge's where the
// run asks it (an empty optional where it does not).
#define EXPECT_THREE_WAYS(port, hand, guest)                                                     \
    do {                                                                                        \
        const int port_ = (port);                                                               \
        const int hand_ = (hand);                                                               \
        const std::optional<int> guest_ = (guest);                                              \
        EXPECT_EQ(port_, hand_) << "the port disagrees with the hand trace";                    \
        if (guest_) {                                                                           \
            EXPECT_EQ(*guest_, hand_) << "the cartridge disagrees with the hand trace";          \
            EXPECT_EQ(port_, *guest_) << "the port disagrees with the cartridge";                \
        }                                                                                       \
    } while (0)

// The cartridge's answer, where the run asked it, is `hand`.
#define EXPECT_CARTRIDGE_EQ(guest, hand)                                                         \
    do {                                                                                        \
        const auto guest_ = (guest);                                                            \
        if (guest_) {                                                                           \
            EXPECT_EQ(*guest_, (hand)) << "the cartridge disagrees with the hand trace";         \
        }                                                                                       \
    } while (0)

constexpr BattlerSlot kHero = BattlerSlot::character(0);
constexpr BattlerSlot kAlly = BattlerSlot::character(1);
constexpr BattlerSlot kFoe = BattlerSlot::monster(0);

// No entry in the ledger.
constexpr int kNone = -1;

// Random-number positions whose next draw is known (the 256-entry table, rngByte):
constexpr std::uint8_t kDrawsVariance255 = 2;  // draws 31: a variance factor of 255, then 85
constexpr std::uint8_t kDrawsVariance224 = 15;  // a variance factor of 224
constexpr std::uint8_t kDrawsPoison = 36;       // draws 4
constexpr std::uint8_t kDrawsBlind = 2;         // draws 31
constexpr std::uint8_t kDrawsNothing = 0;       // draws 182

// A party member's blow of `damage` on a monster, with the damage taken as it is.
DamageScenario strike(int damage) {
    DamageScenario s;
    s.attacker = kHero;
    s.attack.record.traits.set(AttackTrait::PHYSICAL);
    s.attack.record.power = 10;
    s.attack.damage = damage;
    s.attack.scratch.damageModification = false;
    return s;
}

// A party member's spell of `damage` on a monster, with the damage taken as it is.
DamageScenario spell(int damage) {
    DamageScenario s = strike(damage);
    s.attack.record.traits.clear(AttackTrait::PHYSICAL);
    return s;
}

// The damage varied with a factor of 255 (997 from 1,000), the target's defenses then counting.
DamageScenario variedStrike() {
    DamageScenario s = strike(1000);
    s.attack.scratch.damageModification = true;
    s.state.rng = battle::BattleRng{kDrawsVariance255};
    return s;
}

int taken(const TargetDamageResult& result, BattlerSlot slot) {
    const auto& entry = result.ledger.taken[static_cast<std::size_t>(slot.index())];
    return entry ? *entry : kNone;
}

std::optional<int> taken(const std::optional<TargetDamageResult>& result, BattlerSlot slot) {
    return result ? std::optional<int>{taken(*result, slot)} : std::nullopt;
}

// The second value the attack's animation was rewritten with, or kNone.
int rewrittenWith(const TargetDamageResult& result) {
    return result.animationRewrittenWith ? *result.animationRewrittenWith : kNone;
}

std::optional<int> rewrittenWith(const std::optional<TargetDamageResult>& result) {
    return result ? std::optional<int>{rewrittenWith(*result)} : std::nullopt;
}

int healed(const TargetDamageResult& result, BattlerSlot slot) {
    const auto& entry = result.ledger.healed[static_cast<std::size_t>(slot.index())];
    return entry ? *entry : kNone;
}

std::optional<int> healed(const std::optional<TargetDamageResult>& result, BattlerSlot slot) {
    return result ? std::optional<int>{healed(*result, slot)} : std::nullopt;
}

std::size_t at(BattlerSlot slot) {
    return static_cast<std::size_t>(slot.index());
}

// One pass worked out by the port, and by the cartridge where the run asks it. Every cell either side
// writes must agree.
struct Both {
    TargetDamageResult port;
    std::optional<TargetDamageResult> guest;
};

// The cartridge's pass over `s` on `target`, where the run asks it.
std::optional<TargetDamageResult> cartridgeTargetDamage(const Cartridge& cartridge, const DamageScenario& s,
                                                        BattlerSlot target) {
    if (!cartridge) {
        return std::nullopt;
    }
    return cartridge.oracle().targetDamage(s, target);
}

Both pass(const Cartridge& cartridge, const DamageScenario& s, BattlerSlot target) {
    Both both{trace::portTargetDamage(s, target), cartridgeTargetDamage(cartridge, s, target)};
    if (both.guest) {
        EXPECT_EQ(trace::differences(both.port, *both.guest), "");
    }
    return both;
}

// A value of the cartridge's pass, read by `read`, where the run asked it.
template <typename Read>
auto fromTheCartridge(const Both& both, Read read) -> std::optional<std::decay_t<decltype(read(both.port))>> {
    if (!both.guest) {
        return std::nullopt;
    }
    return read(*both.guest);
}

// The cartridge's `member`, where the run asked it.
#define CARTRIDGE_VALUE(both, member) fromTheCartridge((both), [&](const TargetDamageResult& r) { return r.member; })

#define EXPECT_TAKEN(both, slot, hand) EXPECT_THREE_WAYS(taken((both).port, slot), hand, taken((both).guest, slot))
#define EXPECT_HEALED(both, slot, hand) \
    EXPECT_THREE_WAYS(healed((both).port, slot), hand, healed((both).guest, slot))

// ─── the damage before the elements ──────────────────────────────────────────────────────────────

TEST_F(TargetDamageTest, PowerZero_WritesNothing) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = strike(1000);
        s.attack.record.power = 0;
        const Both both = pass(cartridge, s, kFoe);
        EXPECT_TAKEN(both, kFoe, kNone);
        EXPECT_HEALED(both, kFoe, kNone);
    });
}

TEST_F(TargetDamageTest, NoModification_TakesTheDamageAsItIs) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        const Both both = pass(cartridge, strike(1000), kFoe);
        EXPECT_TAKEN(both, kFoe, 1000);
        EXPECT_HEALED(both, kFoe, kNone);
    });
}

TEST_F(TargetDamageTest, Variance_FromTwoHundredTwentyFourToTwoHundredFiftyFiveSixths) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // 1,000 × 224 / 256 + 1 and 1,000 × 255 / 256 + 1; defense ignored to see the variance alone.
        DamageScenario s = strike(1000);
        s.attack.scratch.damageModification = true;
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        s.state.rng = battle::BattleRng{kDrawsVariance224};
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 876);
        s.state.rng = battle::BattleRng{kDrawsVariance255};
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 997);
    });
}

TEST_F(TargetDamageTest, Defense_ABlowIsCutByDefense) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // 997 × 155 / 256 + 1.
        DamageScenario s = variedStrike();
        s.state.at(kFoe).defense = 100;
        s.state.at(kFoe).magicDefense = 50;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 604);
    });
}

TEST_F(TargetDamageTest, Defense_ASpellOrAnAttackOnMpIsCutByMagicDefense) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // 997 × 205 / 256 + 1.
        DamageScenario s = variedStrike();
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        s.state.at(kFoe).defense = 100;
        s.state.at(kFoe).magicDefense = 50;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 799);
        s.attack.record.traits.set(AttackTrait::PHYSICAL);
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 799);
    });
}

TEST_F(TargetDamageTest, Defense_TwoHundredFiftyFiveLeavesOne) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = variedStrike();
        s.state.at(kFoe).defense = 255;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 1);
        s.attack.scratch.golemBlocked = kAlly;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 1);
    });
}

TEST_F(TargetDamageTest, Defense_WhileAGolemOrInterceptorBlocksEveryTargetHasOneHundredNinetyTwo) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // 997 × 63 / 256 + 1, whatever the target's own defense.
        DamageScenario s = variedStrike();
        s.state.at(kFoe).defense = 10;
        s.attack.scratch.golemBlocked = kAlly;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 246);
        s.attack.scratch.golemBlocked.reset();
        s.attack.scratch.interceptorBlocked = kAlly;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 246);
    });
}

TEST_F(TargetDamageTest, SafeAndShell_SafeAgainstABlowShellAgainstASpell) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // Defense 0: 997 × 255 / 256 + 1 = 994; then × 170 / 256 + 1 = 661.
        DamageScenario s = variedStrike();
        s.state.at(kFoe).status.set(StatusId::PROTECT);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 661);
        s.state.at(kFoe).status = StatusSet::of(StatusId::SHELL);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 994);
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 661);
        s.state.at(kFoe).status = StatusSet::of(StatusId::PROTECT);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 994);
    });
}

TEST_F(TargetDamageTest, Stance_DefendingAndTheBackRowEachHalveABlow) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = variedStrike();
        s.state.at(kFoe).defending = true;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 497);
        s.state.at(kFoe).defending = false;
        s.state.at(kFoe).backRow = true;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 497);
        s.state.at(kFoe).defending = true;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 248);
        // A spell is not halved by either.
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 994);
    });
}

TEST_F(TargetDamageTest, Stance_MorphHalvesASpell) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = variedStrike();
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        s.state.at(kFoe).status.set(StatusId::MORPH);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 497);
        s.attack.record.traits.set(AttackTrait::PHYSICAL);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 994);
    });
}

TEST_F(TargetDamageTest, IgnoreDefense_SkipsDefenseSafeShellStanceAndMorph) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = variedStrike();
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        battle::Battler& foe = s.state.at(kFoe);
        foe.defense = 100;
        foe.status = StatusSet::of(StatusId::PROTECT, StatusId::SHELL, StatusId::MORPH);
        foe.defending = true;
        foe.backRow = true;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 997);
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 997);
    });
}

TEST_F(TargetDamageTest, FriendlyFire_APartyMembersAttackOnAPartyMemberIsHalved) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = variedStrike();
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        EXPECT_TAKEN(pass(cartridge, s, kAlly), kAlly, 498);
        // Not when it restores, and not a monster's.
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        EXPECT_HEALED(pass(cartridge, s, kAlly), kAlly, 997);
        s.attack.record.flags2.clear(AttackFlag2::RESTORE_HP_MP);
        s.attacker = kFoe;
        EXPECT_TAKEN(pass(cartridge, s, kAlly), kAlly, 997);
    });
}

TEST_F(TargetDamageTest, Modification_OffSkipsFriendlyFireAndKeepsTheSteps) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = strike(1000);
        s.attack.multiplier = 2;
        const Both both = pass(cartridge, s, kAlly);
        EXPECT_TAKEN(both, kAlly, 1000);
        EXPECT_THREE_WAYS(both.port.stepsLeft, 2, CARTRIDGE_VALUE(both, stepsLeft));
    });
}

TEST_F(TargetDamageTest, Multiplier_TheStepsAreAppliedAndUsedUp) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // 994 + 2 × 497.
        DamageScenario s = variedStrike();
        s.attack.multiplier = 2;
        const Both both = pass(cartridge, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 1988);
        EXPECT_THREE_WAYS(both.port.stepsLeft, 0, CARTRIDGE_VALUE(both, stepsLeft));
    });
}

TEST_F(TargetDamageTest, Multiplier_AnAttackThatIgnoresDefenseKeepsItsSteps) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = variedStrike();
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        s.attack.multiplier = 2;
        const Both both = pass(cartridge, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 997);
        EXPECT_THREE_WAYS(both.port.stepsLeft, 2, CARTRIDGE_VALUE(both, stepsLeft));
    });
}

// Two targets hit in the back, as the frame runs them: it adds one step before each target's damage.
// The steps each target's damage is worked out with, each pass worked out by `work`.
template <typename Work>
std::vector<int> stepsSeenByTwoBackHits(DamageScenario s, Work work) {
    std::vector<int> seen;
    for (const BattlerSlot target : {BattlerSlot::monster(1), BattlerSlot::monster(0)}) {
        ++s.attack.multiplier;
        seen.push_back(s.attack.multiplier);
        const TargetDamageResult result = work(s, target);
        s.attack.multiplier = result.stepsLeft;
        s.attack.scratch.ledger = result.ledger;
        s.state.rng = battle::BattleRng{result.rngPosition};
    }
    return seen;
}

// The steps the port's passes see.
std::vector<int> stepsThePortSees(const DamageScenario& s) {
    return stepsSeenByTwoBackHits(s, [](const DamageScenario& at, BattlerSlot target) {
        return trace::portTargetDamage(at, target);
    });
}

// The steps the cartridge's passes see are `hand`, where the run asks it.
::testing::AssertionResult theCartridgeSees(const Cartridge& cartridge, const DamageScenario& s,
                                            const std::vector<int>& hand) {
    if (!cartridge) {
        return ::testing::AssertionSuccess();
    }
    const std::vector<int> seen = stepsSeenByTwoBackHits(s, [&](const DamageScenario& at, BattlerSlot target) {
        return cartridge.oracle().targetDamage(at, target);
    });
    return seen == hand ? ::testing::AssertionSuccess() : ::testing::AssertionFailure() << "the cartridge sees other steps";
}

TEST_F(TargetDamageTest, BackHits_EachTargetUsesUpTheStepAddedForIt) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        const DamageScenario s = variedStrike();
        const std::vector<int> hand{1, 1};
        EXPECT_EQ(stepsThePortSees(s), hand);
        EXPECT_TRUE(theCartridgeSees(cartridge, s, hand));
    });
}

TEST_F(TargetDamageTest, BackHits_TheStepCarriesWhenTheDamageIsNotModified) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        const DamageScenario s = strike(1000);
        const std::vector<int> hand{1, 2};
        EXPECT_EQ(stepsThePortSees(s), hand);
        EXPECT_TRUE(theCartridgeSees(cartridge, s, hand));
    });
}

// ─── fractional damage ───────────────────────────────────────────────────────────────────────────

// A fraction of eight sixteenths, on a monster with 1,000 of 2,000 HP that the attack already takes
// 200 from.
DamageScenario halving() {
    DamageScenario s = spell(0);
    s.attack.record.flags2.set(AttackFlag2::FRACTIONAL_DAMAGE);
    s.attack.record.power = 8;
    s.state.at(kFoe).hp = 1000;
    s.state.at(kFoe).maxHp = 2000;
    s.attack.scratch.ledger.taken[at(kFoe)] = 200;
    return s;
}

TEST_F(TargetDamageTest, Fractional_OfWhatTheTargetHasLeft) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // (1,000 − 200) × 8 / 16 = 400, added to the 200.
        EXPECT_TAKEN(pass(cartridge, halving(), kFoe), kFoe, 600);
    });
}

TEST_F(TargetDamageTest, Fractional_OfTheMaximumForAnItemOrARevive) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // 2,000 × 8 / 16 = 1,000, added to the 200.
        DamageScenario s = halving();
        s.attack.command = BattleCommandId::ITEM;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 1200);
        s = halving();
        s.attack.record.traits.set(AttackTrait::RESURRECTION_TARGET);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 1200);
    });
}

TEST_F(TargetDamageTest, Fractional_OfMpForAnAttackOnMp) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // MP 100: (100 − 0) × 8 / 16 = 50.
        DamageScenario s = halving();
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        s.attack.scratch.ledger.taken[at(kFoe)].reset();
        s.state.at(kFoe).mp = 100;
        s.state.at(kFoe).maxMp = 300;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 50);
        // What the ledger takes is counted against MP too: 100 − 200 leaves 0, which does 1.
        s.attack.scratch.ledger.taken[at(kFoe)] = 200;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 201);
    });
}

TEST_F(TargetDamageTest, Fractional_ZeroDoesOne) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = halving();
        s.attack.scratch.ledger.taken[at(kFoe)].reset();
        s.attack.record.power = 1;
        s.state.at(kFoe).hp = 1;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 1);
    });
}

TEST_F(TargetDamageTest, Fractional_IgnoresTheVarianceTheDefensesAndTheMultiplier) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = halving();
        s.attack.scratch.damageModification = true;
        s.state.at(kFoe).magicDefense = 100;
        s.attack.multiplier = 2;
        const Both both = pass(cartridge, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 600);
        EXPECT_THREE_WAYS(both.port.stepsLeft, 2, CARTRIDGE_VALUE(both, stepsLeft));
        EXPECT_THREE_WAYS(both.port.rngPosition, 0, CARTRIDGE_VALUE(both, rngPosition));
    });
}

// ─── who gains ───────────────────────────────────────────────────────────────────────────────────

TEST_F(TargetDamageTest, Petrified_TakesZeroEvenFromACure) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = spell(1000);
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        s.state.at(kFoe).status.set(StatusId::PETRIFY);
        const Both both = pass(cartridge, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 0);
        EXPECT_HEALED(both, kFoe, kNone);
    });
}

TEST_F(TargetDamageTest, Undead_AnAttackThatKillsOrZombifiesDoesNothingAtAll) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        for (const StatusId status : {StatusId::DEAD, StatusId::ZOMBIE}) {
            DamageScenario s = strike(1000);
            s.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
            s.attack.record.statuses.set(status);
            s.state.at(kHero).status.set(StatusId::ZOMBIE);
            s.state.at(kFoe).undead = true;
            const Both both = pass(cartridge, s, kFoe);
            EXPECT_TAKEN(both, kFoe, kNone);
            EXPECT_HEALED(both, kFoe, kNone);
            // Nothing past it runs: no Zombie draw.
            EXPECT_THREE_WAYS(both.port.rngPosition, 0, CARTRIDGE_VALUE(both, rngPosition));
        }
    });
}

TEST_F(TargetDamageTest, Undead_ACureHurtsIt) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = spell(1000);
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        s.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
        s.state.at(kFoe).undead = true;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 1000);
        // Without the trait the cure heals it.
        s.attack.record.traits.clear(AttackTrait::INVERT_ON_UNDEAD);
        EXPECT_HEALED(pass(cartridge, s, kFoe), kFoe, 1000);
    });
}

TEST_F(TargetDamageTest, Undead_ADrainRunsBackward) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = spell(1000);
        s.attack.record.flags2.set(AttackFlag2::DRAIN);
        s.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
        s.state.at(kFoe).undead = true;
        const Both both = pass(cartridge, s, kFoe);
        EXPECT_HEALED(both, kFoe, 1000);
        EXPECT_TAKEN(both, kHero, 1000);
    });
}

TEST_F(TargetDamageTest, Zombie_ADrainOnAZombieRunsBackwardWithTheTrait) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = spell(1000);
        s.attack.record.flags2.set(AttackFlag2::DRAIN);
        s.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
        s.state.at(kFoe).status.set(StatusId::ZOMBIE);
        Both both = pass(cartridge, s, kFoe);
        EXPECT_HEALED(both, kFoe, 1000);
        EXPECT_TAKEN(both, kHero, 1000);
        s.attack.record.traits.clear(AttackTrait::INVERT_ON_UNDEAD);
        both = pass(cartridge, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 1000);
        EXPECT_HEALED(both, kHero, 1000);
    });
}

// ─── the elements ────────────────────────────────────────────────────────────────────────────────

DamageScenario fire(int damage) {
    DamageScenario s = spell(damage);
    s.attack.record.element = ElementSet::of(Element::FIRE);
    return s;
}

TEST_F(TargetDamageTest, Forcefield_AnAttackWhoseEveryElementIsNullifiedDoesZero) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = fire(1000);
        s.state.elementsNullifiedByForcefield = ElementSet::of(Element::FIRE);
        // Ahead of absorbing.
        s.state.at(kFoe).elementsAbsorbed = ElementSet::of(Element::FIRE);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 0);
        // Not when another of its elements is left.
        s.attack.record.element = ElementSet::of(Element::FIRE, Element::ICE);
        s.state.at(kFoe).elementsAbsorbed = {};
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 1000);
    });
}

TEST_F(TargetDamageTest, Elements_AbsorbingHealsAndComesFirst) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = fire(1000);
        s.state.at(kFoe).elementsAbsorbed = ElementSet::of(Element::FIRE);
        s.state.at(kFoe).elementsNullified = ElementSet::of(Element::FIRE);
        s.state.at(kFoe).elementsWeak = ElementSet::of(Element::FIRE);
        EXPECT_HEALED(pass(cartridge, s, kFoe), kFoe, 1000);
        // A cure it absorbs hurts it.
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 1000);
    });
}

TEST_F(TargetDamageTest, Elements_NullifyingHalvingAndWeakness) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = fire(1000);
        s.state.at(kFoe).elementsNullified = ElementSet::of(Element::FIRE);
        s.state.at(kFoe).elementsHalved = ElementSet::of(Element::FIRE);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 0);
        s.state.at(kFoe).elementsNullified = {};
        s.state.at(kFoe).elementsWeak = ElementSet::of(Element::FIRE);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 500);
        s.state.at(kFoe).elementsHalved = {};
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 2000);
        // One shared element is enough.
        s.attack.record.element = ElementSet::of(Element::FIRE, Element::ICE);
        s.state.at(kFoe).elementsWeak = ElementSet::of(Element::ICE);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 2000);
    });
}

// ─── the Atma Weapon ─────────────────────────────────────────────────────────────────────────────

// An Atma Weapon blow by a party member of `level` with `hp` of `maxHp`.
DamageScenario atma(int damage, std::uint8_t level, std::uint16_t hp, std::uint16_t maxHp) {
    DamageScenario s = strike(damage);
    s.attack.effect = battle::SpecialEffect::of(WeaponSpecialEffect::ATMA_WEAPON);
    s.attack.secondary = 29;
    s.state.at(kHero).level = level;
    s.state.at(kHero).hp = hp;
    s.state.at(kHero).maxHp = maxHp;
    return s;
}

TEST_F(TargetDamageTest, Atma_ScaledByTheLevelAndTheHpLeft) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // (4,999 / 256 + 1) × 50 / (9,999 / 256 + 1) = 20 × 50 / 40 = 25; 1,000 × 25 / 64 + 1.
        const Both both = pass(cartridge, atma(1000, 50, 4999, 9999), kFoe);
        EXPECT_TAKEN(both, kFoe, 391);
        EXPECT_THREE_WAYS(both.port.secondary, 29, CARTRIDGE_VALUE(both, secondary));
        EXPECT_FALSE(both.port.animationRewrittenWith.has_value());
    });
}

TEST_F(TargetDamageTest, Atma_TheBeamLengthensFromFiveHundredOneAndAThousandOne) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // At full HP the strength is the level: 64 makes the damage itself plus 1.
        struct Row {
            int damage;
            int shown;
            int beam;
        };
        for (const Row row : {Row{499, 500, kNone}, Row{500, 501, 91}, Row{999, 1000, 91}, Row{1000, 1001, 92}}) {
            const Both both = pass(cartridge, atma(row.damage, 64, 3000, 3000), kFoe);
            EXPECT_TAKEN(both, kFoe, row.shown);
            EXPECT_THREE_WAYS(rewrittenWith(both.port), row.beam, rewrittenWith(both.guest));
        }
    });
}

TEST_F(TargetDamageTest, Atma_APetrifiedTargetTakesOne) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = atma(1000, 64, 3000, 3000);
        s.state.at(kFoe).status.set(StatusId::PETRIFY);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 1);
    });
}

TEST_F(TargetDamageTest, Atma_TheDamageWrapsPastSixtyFiveThousand) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // Level 99 at full HP: 45,000 × 99 / 64 = 69,609, kept as 4,073, plus 1 (docs/Bugs.md).
        const Both both = pass(cartridge, atma(45000, 99, 9999, 9999), kFoe);
        EXPECT_TAKEN(both, kFoe, 4074);
        EXPECT_THREE_WAYS(rewrittenWith(both.port), 92, rewrittenWith(both.guest));
    });
}

TEST_F(TargetDamageTest, Atma_WeaknessDoublesOnlyBelowThirtyTwoThousandSevenHundredSixtyEight) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // At level 1 and full HP the strength is 1: weak, 32,767 doubles to 65,534 → 65,534 / 64 + 1;
        // 32,768 stays → 32,768 / 64 + 1.
        DamageScenario s = atma(32767, 1, 3000, 3000);
        s.attack.record.element = ElementSet::of(Element::FIRE);
        s.state.at(kFoe).elementsWeak = ElementSet::of(Element::FIRE);
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 1024);
        s.attack.damage = 32768;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 513);
    });
}

// ─── what a blow does besides ────────────────────────────────────────────────────────────────────

TEST_F(TargetDamageTest, Zombie_AZombiesBlowPoisonsOrBlindsOneTimeInSixteenEach) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        struct Row {
            std::uint8_t position;
            StatusSet given;
        };
        for (const Row row : {Row{kDrawsPoison, StatusSet::of(StatusId::POISON)},
                              Row{kDrawsBlind, StatusSet::of(StatusId::BLIND)}, Row{kDrawsNothing, StatusSet{}}}) {
            DamageScenario s = strike(1000);
            s.state.at(kHero).status.set(StatusId::ZOMBIE);
            s.state.rng = battle::BattleRng{row.position};
            const Both both = pass(cartridge, s, kFoe);
            EXPECT_EQ(both.port.statusesToSet[at(kFoe)].bytes, row.given.bytes);
            EXPECT_CARTRIDGE_EQ(CARTRIDGE_VALUE(both, statusesToSet[at(kFoe)].bytes), row.given.bytes);
        }
    });
}

TEST_F(TargetDamageTest, Blow_ABlockedBlowDoesNothingBesides) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = strike(1000);
        s.attack.scratch.golemBlocked = kAlly;
        s.state.at(kHero).status.set(StatusId::ZOMBIE);
        s.state.at(kFoe).status = StatusSet::of(StatusId::SLEEP);
        s.state.at(kFoe).controlledBy = kAlly;
        const Both both = pass(cartridge, s, kFoe);
        EXPECT_THREE_WAYS(both.port.rngPosition, 0, CARTRIDGE_VALUE(both, rngPosition));
        EXPECT_TRUE(both.port.controlled[at(kFoe)]);
        EXPECT_EQ(both.port.statusesToClear[at(kFoe)].bytes, StatusSet{}.bytes);
    });
}

TEST_F(TargetDamageTest, Blow_WakesClearsMuddleAndFreesFromControl) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = strike(1000);
        s.state.at(kFoe).status = StatusSet::of(StatusId::SLEEP, StatusId::CONFUSE);
        s.state.at(kFoe).controlledBy = kAlly;
        Both both = pass(cartridge, s, kFoe);
        EXPECT_EQ(both.port.statusesToClear[at(kFoe)].bytes, StatusSet::of(StatusId::SLEEP, StatusId::CONFUSE).bytes);
        EXPECT_FALSE(both.port.controlled[at(kFoe)]);
        // A blow that puts Sleep on leaves it.
        s.attack.record.statuses.set(StatusId::SLEEP);
        both = pass(cartridge, s, kFoe);
        EXPECT_EQ(both.port.statusesToClear[at(kFoe)].bytes, StatusSet::of(StatusId::CONFUSE).bytes);
        // A spell does none of it.
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        both = pass(cartridge, s, kFoe);
        EXPECT_EQ(both.port.statusesToClear[at(kFoe)].bytes, StatusSet{}.bytes);
        EXPECT_TRUE(both.port.controlled[at(kFoe)]);
    });
}

// ─── drains ──────────────────────────────────────────────────────────────────────────────────────

// A spell drain of 997 (1,000 varied by 255, defense 0 and so ignored) by a party member with 1,000
// of 2,000 HP on a monster with `foeHp`.
DamageScenario drain(std::uint16_t foeHp) {
    DamageScenario s = variedStrike();
    s.attack.record.traits.clear(AttackTrait::PHYSICAL);
    s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
    s.attack.record.flags2.set(AttackFlag2::DRAIN);
    s.state.at(kHero).hp = 1000;
    s.state.at(kHero).maxHp = 2000;
    s.state.at(kFoe).hp = foeHp;
    s.state.at(kFoe).maxHp = 9000;
    return s;
}

TEST_F(TargetDamageTest, Drain_NoMoreThanTheTargetHas) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        const Both both = pass(cartridge, drain(300), kFoe);
        EXPECT_TAKEN(both, kFoe, 300);
        EXPECT_HEALED(both, kHero, 300);
    });
}

TEST_F(TargetDamageTest, Drain_NoMoreThanTheDrainerIsMissingUnlessUncapped) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = drain(5000);
        s.state.at(kHero).hp = 1800;
        Both both = pass(cartridge, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 200);
        EXPECT_HEALED(both, kHero, 200);
        s.attack.drainCapped = false;
        both = pass(cartridge, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 997);
        EXPECT_HEALED(both, kHero, 997);
    });
}

TEST_F(TargetDamageTest, Drain_NoCapWithoutDamageModification) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = drain(300);
        s.attack.scratch.damageModification = false;
        const Both both = pass(cartridge, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 1000);
        EXPECT_HEALED(both, kHero, 1000);
    });
}

TEST_F(TargetDamageTest, Drain_OnMpCapsByMp) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = drain(5000);
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        s.state.at(kFoe).mp = 50;
        s.state.at(kFoe).maxMp = 100;
        s.state.at(kHero).mp = 0;
        s.state.at(kHero).maxMp = 500;
        const Both both = pass(cartridge, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 50);
        EXPECT_HEALED(both, kHero, 50);
    });
}

TEST_F(TargetDamageTest, Drain_BackwardCapsByTheDrainerAndTheTargetsRoom) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        // An undead target gains: no more than the drainer's 400 HP, nor than the target's 100 missing.
        DamageScenario s = drain(8900);
        s.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
        s.state.at(kFoe).undead = true;
        s.state.at(kHero).hp = 400;
        Both both = pass(cartridge, s, kFoe);
        EXPECT_HEALED(both, kFoe, 100);
        EXPECT_TAKEN(both, kHero, 100);
        s.state.at(kFoe).hp = 5000;
        both = pass(cartridge, s, kFoe);
        EXPECT_HEALED(both, kFoe, 400);
        EXPECT_TAKEN(both, kHero, 400);
    });
}

TEST_F(TargetDamageTest, Drain_TheDrainerWaitsToAnswerAfterABlowNotASpell) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        const int foe = kFoe.index();
        constexpr int kWaiting = 0x80;
        DamageScenario s = drain(5000);
        Both both = pass(cartridge, s, kFoe);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], foe, CARTRIDGE_VALUE(both, lastAttackerMark[at(kHero)]));
        s.attack.record.traits.set(AttackTrait::PHYSICAL);
        both = pass(cartridge, s, kFoe);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], foe | kWaiting,
                          CARTRIDGE_VALUE(both, lastAttackerMark[at(kHero)]));
        // Already waiting to answer someone: kept.
        s.state.at(kHero).lastAttacker = kAlly;
        s.state.at(kHero).retaliationPending = true;
        both = pass(cartridge, s, kFoe);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], kAlly.index() | kWaiting,
                          CARTRIDGE_VALUE(both, lastAttackerMark[at(kHero)]));
    });
}

TEST_F(TargetDamageTest, Drain_AZombieDrainersBlowWaitsOnlyWhenItsDrawGaveNothing) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        const int foe = kFoe.index();
        constexpr int kWaiting = 0x80;
        DamageScenario s = drain(5000);
        s.attack.record.traits.set(AttackTrait::PHYSICAL);
        s.attack.scratch.damageModification = false;
        s.state.at(kHero).status.set(StatusId::ZOMBIE);
        s.state.rng = battle::BattleRng{kDrawsBlind};
        Both both = pass(cartridge, s, kFoe);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], foe, CARTRIDGE_VALUE(both, lastAttackerMark[at(kHero)]));
        s.state.rng = battle::BattleRng{kDrawsNothing};
        both = pass(cartridge, s, kFoe);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], foe | kWaiting,
                          CARTRIDGE_VALUE(both, lastAttackerMark[at(kHero)]));
    });
}

// ─── the ledger ──────────────────────────────────────────────────────────────────────────────────

TEST_F(TargetDamageTest, Ledger_AddsToWhatIsThereAndStopsAtNineThousandNineHundredNinetyNine) {
    withCartridge(TargetDamageTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = strike(1000);
        s.attack.scratch.ledger.taken[at(kFoe)] = 5000;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 6000);
        s.attack.scratch.ledger.taken[at(kFoe)] = 9500;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 9999);
        s.attack.damage = 60000;
        s.attack.scratch.ledger.taken[at(kFoe)] = 9999;
        EXPECT_TAKEN(pass(cartridge, s, kFoe), kFoe, 9999);
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        s.attack.damage = 1000;
        s.attack.scratch.ledger.healed[at(kFoe)] = 9500;
        EXPECT_HEALED(pass(cartridge, s, kFoe), kFoe, 9999);
    });
}

}  // namespace
}  // namespace ostinato::test
