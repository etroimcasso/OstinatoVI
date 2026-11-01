// The damage to one target: the variance and the defenses, fractional damage, who gains, the
// elements, the Atma Weapon, what a blow does besides, drains and the ledger. Each case is worked
// out three ways — the port, by hand from the original, and the cartridge's own routine through the
// damage oracle — and every other cell the pass writes is compared between the port and the
// cartridge as well. A seeded sweep then draws scenarios at random and checks the port against the
// cartridge alone.
//
// Authority: docs/contracts/battle_damage.md.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/attack.h"
#include "battle/damage.h"
#include "damage_oracle.h"
#include "ostinato/attack_flags.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/element.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::DamageScenario;
using trace::TargetDamageResult;

// The cartridge, read once for the binary; every test starts its own oracle on it.
class TargetDamageTest : public ::testing::Test {
public:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* TargetDamageTest::image_ = nullptr;

// The three-way check: the port's answer, the value worked out by hand, and the cartridge's.
#define EXPECT_THREE_WAYS(port, hand, guest)                                                     \
    do {                                                                                        \
        const int port_ = (port);                                                               \
        const int hand_ = (hand);                                                               \
        const int guest_ = (guest);                                                             \
        EXPECT_EQ(port_, hand_) << "the port disagrees with the hand trace";                    \
        EXPECT_EQ(guest_, hand_) << "the cartridge disagrees with the hand trace";               \
        EXPECT_EQ(port_, guest_) << "the port disagrees with the cartridge";                     \
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

// The second value the attack's animation was rewritten with, or kNone.
int rewrittenWith(const TargetDamageResult& result) {
    return result.animationRewrittenWith ? *result.animationRewrittenWith : kNone;
}

int healed(const TargetDamageResult& result, BattlerSlot slot) {
    const auto& entry = result.ledger.healed[static_cast<std::size_t>(slot.index())];
    return entry ? *entry : kNone;
}

std::size_t at(BattlerSlot slot) {
    return static_cast<std::size_t>(slot.index());
}

// One pass worked out by the port and by the cartridge. Every cell either side writes must agree.
struct Both {
    TargetDamageResult port;
    TargetDamageResult guest;
};

Both pass(trace::DamageOracle& oracle, const DamageScenario& s, BattlerSlot target) {
    Both both{trace::portTargetDamage(s, target), oracle.targetDamage(s, target)};
    EXPECT_EQ(trace::differences(both.port, both.guest), "");
    return both;
}

#define EXPECT_TAKEN(both, slot, hand) EXPECT_THREE_WAYS(taken((both).port, slot), hand, taken((both).guest, slot))
#define EXPECT_HEALED(both, slot, hand) \
    EXPECT_THREE_WAYS(healed((both).port, slot), hand, healed((both).guest, slot))

// Run `body` with a started oracle, from a tick of the run loop.
template <typename Body>
void withOracle(Body body) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{TargetDamageTest::image()};
        oracle.start();
        body(oracle);
    });
}

// ─── the damage before the elements ──────────────────────────────────────────────────────────────

TEST_F(TargetDamageTest, PowerZero_WritesNothing) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = strike(1000);
        s.attack.record.power = 0;
        const Both both = pass(oracle, s, kFoe);
        EXPECT_TAKEN(both, kFoe, kNone);
        EXPECT_HEALED(both, kFoe, kNone);
    });
}

TEST_F(TargetDamageTest, NoModification_TakesTheDamageAsItIs) {
    withOracle([](trace::DamageOracle& oracle) {
        const Both both = pass(oracle, strike(1000), kFoe);
        EXPECT_TAKEN(both, kFoe, 1000);
        EXPECT_HEALED(both, kFoe, kNone);
    });
}

TEST_F(TargetDamageTest, Variance_FromTwoHundredTwentyFourToTwoHundredFiftyFiveSixths) {
    withOracle([](trace::DamageOracle& oracle) {
        // 1,000 × 224 / 256 + 1 and 1,000 × 255 / 256 + 1; defense ignored to see the variance alone.
        DamageScenario s = strike(1000);
        s.attack.scratch.damageModification = true;
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        s.state.rng = battle::BattleRng{kDrawsVariance224};
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 876);
        s.state.rng = battle::BattleRng{kDrawsVariance255};
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 997);
    });
}

TEST_F(TargetDamageTest, Defense_ABlowIsCutByDefense) {
    withOracle([](trace::DamageOracle& oracle) {
        // 997 × 155 / 256 + 1.
        DamageScenario s = variedStrike();
        s.state.at(kFoe).defense = 100;
        s.state.at(kFoe).magicDefense = 50;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 604);
    });
}

TEST_F(TargetDamageTest, Defense_ASpellOrAnAttackOnMpIsCutByMagicDefense) {
    withOracle([](trace::DamageOracle& oracle) {
        // 997 × 205 / 256 + 1.
        DamageScenario s = variedStrike();
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        s.state.at(kFoe).defense = 100;
        s.state.at(kFoe).magicDefense = 50;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 799);
        s.attack.record.traits.set(AttackTrait::PHYSICAL);
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 799);
    });
}

TEST_F(TargetDamageTest, Defense_TwoHundredFiftyFiveLeavesOne) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = variedStrike();
        s.state.at(kFoe).defense = 255;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 1);
        s.attack.scratch.golemBlocked = kAlly;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 1);
    });
}

TEST_F(TargetDamageTest, Defense_WhileAGolemOrInterceptorBlocksEveryTargetHasOneHundredNinetyTwo) {
    withOracle([](trace::DamageOracle& oracle) {
        // 997 × 63 / 256 + 1, whatever the target's own defense.
        DamageScenario s = variedStrike();
        s.state.at(kFoe).defense = 10;
        s.attack.scratch.golemBlocked = kAlly;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 246);
        s.attack.scratch.golemBlocked.reset();
        s.attack.scratch.interceptorBlocked = kAlly;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 246);
    });
}

TEST_F(TargetDamageTest, SafeAndShell_SafeAgainstABlowShellAgainstASpell) {
    withOracle([](trace::DamageOracle& oracle) {
        // Defense 0: 997 × 255 / 256 + 1 = 994; then × 170 / 256 + 1 = 661.
        DamageScenario s = variedStrike();
        s.state.at(kFoe).status.set(StatusId::PROTECT);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 661);
        s.state.at(kFoe).status = StatusSet::of(StatusId::SHELL);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 994);
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 661);
        s.state.at(kFoe).status = StatusSet::of(StatusId::PROTECT);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 994);
    });
}

TEST_F(TargetDamageTest, Stance_DefendingAndTheBackRowEachHalveABlow) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = variedStrike();
        s.state.at(kFoe).defending = true;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 497);
        s.state.at(kFoe).defending = false;
        s.state.at(kFoe).backRow = true;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 497);
        s.state.at(kFoe).defending = true;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 248);
        // A spell is not halved by either.
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 994);
    });
}

TEST_F(TargetDamageTest, Stance_MorphHalvesASpell) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = variedStrike();
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        s.state.at(kFoe).status.set(StatusId::MORPH);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 497);
        s.attack.record.traits.set(AttackTrait::PHYSICAL);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 994);
    });
}

TEST_F(TargetDamageTest, IgnoreDefense_SkipsDefenseSafeShellStanceAndMorph) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = variedStrike();
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        battle::Battler& foe = s.state.at(kFoe);
        foe.defense = 100;
        foe.status = StatusSet::of(StatusId::PROTECT, StatusId::SHELL, StatusId::MORPH);
        foe.defending = true;
        foe.backRow = true;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 997);
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 997);
    });
}

TEST_F(TargetDamageTest, FriendlyFire_APartyMembersAttackOnAPartyMemberIsHalved) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = variedStrike();
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        EXPECT_TAKEN(pass(oracle, s, kAlly), kAlly, 498);
        // Not when it restores, and not a monster's.
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        EXPECT_HEALED(pass(oracle, s, kAlly), kAlly, 997);
        s.attack.record.flags2.clear(AttackFlag2::RESTORE_HP_MP);
        s.attacker = kFoe;
        EXPECT_TAKEN(pass(oracle, s, kAlly), kAlly, 997);
    });
}

TEST_F(TargetDamageTest, Modification_OffSkipsFriendlyFireAndKeepsTheSteps) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = strike(1000);
        s.attack.multiplier = 2;
        const Both both = pass(oracle, s, kAlly);
        EXPECT_TAKEN(both, kAlly, 1000);
        EXPECT_THREE_WAYS(both.port.stepsLeft, 2, both.guest.stepsLeft);
    });
}

TEST_F(TargetDamageTest, Multiplier_TheStepsAreAppliedAndUsedUp) {
    withOracle([](trace::DamageOracle& oracle) {
        // 994 + 2 × 497.
        DamageScenario s = variedStrike();
        s.attack.multiplier = 2;
        const Both both = pass(oracle, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 1988);
        EXPECT_THREE_WAYS(both.port.stepsLeft, 0, both.guest.stepsLeft);
    });
}

TEST_F(TargetDamageTest, Multiplier_AnAttackThatIgnoresDefenseKeepsItsSteps) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = variedStrike();
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        s.attack.multiplier = 2;
        const Both both = pass(oracle, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 997);
        EXPECT_THREE_WAYS(both.port.stepsLeft, 2, both.guest.stepsLeft);
    });
}

// Two targets hit in the back, as the frame runs them: it adds one step before each target's damage.
// The steps each target's damage is worked out with, on both sides.
std::vector<int> stepsSeenByTwoBackHits(trace::DamageOracle& oracle, DamageScenario s, bool port) {
    std::vector<int> seen;
    for (const BattlerSlot target : {BattlerSlot::monster(1), BattlerSlot::monster(0)}) {
        ++s.attack.multiplier;
        seen.push_back(s.attack.multiplier);
        const TargetDamageResult result = port ? trace::portTargetDamage(s, target) : oracle.targetDamage(s, target);
        s.attack.multiplier = result.stepsLeft;
        s.attack.scratch.ledger = result.ledger;
        s.state.rng = battle::BattleRng{result.rngPosition};
    }
    return seen;
}

TEST_F(TargetDamageTest, BackHits_EachTargetUsesUpTheStepAddedForIt) {
    withOracle([](trace::DamageOracle& oracle) {
        const DamageScenario s = variedStrike();
        const std::vector<int> hand{1, 1};
        EXPECT_EQ(stepsSeenByTwoBackHits(oracle, s, true), hand);
        EXPECT_EQ(stepsSeenByTwoBackHits(oracle, s, false), hand);
    });
}

TEST_F(TargetDamageTest, BackHits_TheStepCarriesWhenTheDamageIsNotModified) {
    withOracle([](trace::DamageOracle& oracle) {
        const DamageScenario s = strike(1000);
        const std::vector<int> hand{1, 2};
        EXPECT_EQ(stepsSeenByTwoBackHits(oracle, s, true), hand);
        EXPECT_EQ(stepsSeenByTwoBackHits(oracle, s, false), hand);
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
    withOracle([](trace::DamageOracle& oracle) {
        // (1,000 − 200) × 8 / 16 = 400, added to the 200.
        EXPECT_TAKEN(pass(oracle, halving(), kFoe), kFoe, 600);
    });
}

TEST_F(TargetDamageTest, Fractional_OfTheMaximumForAnItemOrARevive) {
    withOracle([](trace::DamageOracle& oracle) {
        // 2,000 × 8 / 16 = 1,000, added to the 200.
        DamageScenario s = halving();
        s.attack.command = BattleCommandId::ITEM;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 1200);
        s = halving();
        s.attack.record.traits.set(AttackTrait::RESURRECTION_TARGET);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 1200);
    });
}

TEST_F(TargetDamageTest, Fractional_OfMpForAnAttackOnMp) {
    withOracle([](trace::DamageOracle& oracle) {
        // MP 100: (100 − 0) × 8 / 16 = 50.
        DamageScenario s = halving();
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        s.attack.scratch.ledger.taken[at(kFoe)].reset();
        s.state.at(kFoe).mp = 100;
        s.state.at(kFoe).maxMp = 300;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 50);
        // What the ledger takes is counted against MP too: 100 − 200 leaves 0, which does 1.
        s.attack.scratch.ledger.taken[at(kFoe)] = 200;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 201);
    });
}

TEST_F(TargetDamageTest, Fractional_ZeroDoesOne) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = halving();
        s.attack.scratch.ledger.taken[at(kFoe)].reset();
        s.attack.record.power = 1;
        s.state.at(kFoe).hp = 1;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 1);
    });
}

TEST_F(TargetDamageTest, Fractional_IgnoresTheVarianceTheDefensesAndTheMultiplier) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = halving();
        s.attack.scratch.damageModification = true;
        s.state.at(kFoe).magicDefense = 100;
        s.attack.multiplier = 2;
        const Both both = pass(oracle, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 600);
        EXPECT_THREE_WAYS(both.port.stepsLeft, 2, both.guest.stepsLeft);
        EXPECT_THREE_WAYS(both.port.rngPosition, 0, both.guest.rngPosition);
    });
}

// ─── who gains ───────────────────────────────────────────────────────────────────────────────────

TEST_F(TargetDamageTest, Petrified_TakesZeroEvenFromACure) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = spell(1000);
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        s.state.at(kFoe).status.set(StatusId::PETRIFY);
        const Both both = pass(oracle, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 0);
        EXPECT_HEALED(both, kFoe, kNone);
    });
}

TEST_F(TargetDamageTest, Undead_AnAttackThatKillsOrZombifiesDoesNothingAtAll) {
    withOracle([](trace::DamageOracle& oracle) {
        for (const StatusId status : {StatusId::DEAD, StatusId::ZOMBIE}) {
            DamageScenario s = strike(1000);
            s.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
            s.attack.record.statuses.set(status);
            s.state.at(kHero).status.set(StatusId::ZOMBIE);
            s.state.at(kFoe).undead = true;
            const Both both = pass(oracle, s, kFoe);
            EXPECT_TAKEN(both, kFoe, kNone);
            EXPECT_HEALED(both, kFoe, kNone);
            // Nothing past it runs: no Zombie draw.
            EXPECT_THREE_WAYS(both.port.rngPosition, 0, both.guest.rngPosition);
        }
    });
}

TEST_F(TargetDamageTest, Undead_ACureHurtsIt) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = spell(1000);
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        s.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
        s.state.at(kFoe).undead = true;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 1000);
        // Without the trait the cure heals it.
        s.attack.record.traits.clear(AttackTrait::INVERT_ON_UNDEAD);
        EXPECT_HEALED(pass(oracle, s, kFoe), kFoe, 1000);
    });
}

TEST_F(TargetDamageTest, Undead_ADrainRunsBackward) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = spell(1000);
        s.attack.record.flags2.set(AttackFlag2::DRAIN);
        s.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
        s.state.at(kFoe).undead = true;
        const Both both = pass(oracle, s, kFoe);
        EXPECT_HEALED(both, kFoe, 1000);
        EXPECT_TAKEN(both, kHero, 1000);
    });
}

TEST_F(TargetDamageTest, Zombie_ADrainOnAZombieRunsBackwardWithTheTrait) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = spell(1000);
        s.attack.record.flags2.set(AttackFlag2::DRAIN);
        s.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
        s.state.at(kFoe).status.set(StatusId::ZOMBIE);
        Both both = pass(oracle, s, kFoe);
        EXPECT_HEALED(both, kFoe, 1000);
        EXPECT_TAKEN(both, kHero, 1000);
        s.attack.record.traits.clear(AttackTrait::INVERT_ON_UNDEAD);
        both = pass(oracle, s, kFoe);
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
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = fire(1000);
        s.state.elementsNullifiedByForcefield = ElementSet::of(Element::FIRE);
        // Ahead of absorbing.
        s.state.at(kFoe).elementsAbsorbed = ElementSet::of(Element::FIRE);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 0);
        // Not when another of its elements is left.
        s.attack.record.element = ElementSet::of(Element::FIRE, Element::ICE);
        s.state.at(kFoe).elementsAbsorbed = {};
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 1000);
    });
}

TEST_F(TargetDamageTest, Elements_AbsorbingHealsAndComesFirst) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = fire(1000);
        s.state.at(kFoe).elementsAbsorbed = ElementSet::of(Element::FIRE);
        s.state.at(kFoe).elementsNullified = ElementSet::of(Element::FIRE);
        s.state.at(kFoe).elementsWeak = ElementSet::of(Element::FIRE);
        EXPECT_HEALED(pass(oracle, s, kFoe), kFoe, 1000);
        // A cure it absorbs hurts it.
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 1000);
    });
}

TEST_F(TargetDamageTest, Elements_NullifyingHalvingAndWeakness) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = fire(1000);
        s.state.at(kFoe).elementsNullified = ElementSet::of(Element::FIRE);
        s.state.at(kFoe).elementsHalved = ElementSet::of(Element::FIRE);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 0);
        s.state.at(kFoe).elementsNullified = {};
        s.state.at(kFoe).elementsWeak = ElementSet::of(Element::FIRE);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 500);
        s.state.at(kFoe).elementsHalved = {};
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 2000);
        // One shared element is enough.
        s.attack.record.element = ElementSet::of(Element::FIRE, Element::ICE);
        s.state.at(kFoe).elementsWeak = ElementSet::of(Element::ICE);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 2000);
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
    withOracle([](trace::DamageOracle& oracle) {
        // (4,999 / 256 + 1) × 50 / (9,999 / 256 + 1) = 20 × 50 / 40 = 25; 1,000 × 25 / 64 + 1.
        const Both both = pass(oracle, atma(1000, 50, 4999, 9999), kFoe);
        EXPECT_TAKEN(both, kFoe, 391);
        EXPECT_THREE_WAYS(both.port.secondary, 29, both.guest.secondary);
        EXPECT_FALSE(both.port.animationRewrittenWith.has_value());
    });
}

TEST_F(TargetDamageTest, Atma_TheBeamLengthensFromFiveHundredOneAndAThousandOne) {
    withOracle([](trace::DamageOracle& oracle) {
        // At full HP the strength is the level: 64 makes the damage itself plus 1.
        struct Row {
            int damage;
            int shown;
            int beam;
        };
        for (const Row row : {Row{499, 500, kNone}, Row{500, 501, 91}, Row{999, 1000, 91}, Row{1000, 1001, 92}}) {
            const Both both = pass(oracle, atma(row.damage, 64, 3000, 3000), kFoe);
            EXPECT_TAKEN(both, kFoe, row.shown);
            EXPECT_THREE_WAYS(rewrittenWith(both.port), row.beam, rewrittenWith(both.guest));
        }
    });
}

TEST_F(TargetDamageTest, Atma_APetrifiedTargetTakesOne) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = atma(1000, 64, 3000, 3000);
        s.state.at(kFoe).status.set(StatusId::PETRIFY);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 1);
    });
}

TEST_F(TargetDamageTest, Atma_TheDamageWrapsPastSixtyFiveThousand) {
    withOracle([](trace::DamageOracle& oracle) {
        // Level 99 at full HP: 45,000 × 99 / 64 = 69,609, kept as 4,073, plus 1 (docs/Bugs.md).
        const Both both = pass(oracle, atma(45000, 99, 9999, 9999), kFoe);
        EXPECT_TAKEN(both, kFoe, 4074);
        EXPECT_THREE_WAYS(rewrittenWith(both.port), 92, rewrittenWith(both.guest));
    });
}

TEST_F(TargetDamageTest, Atma_WeaknessDoublesOnlyBelowThirtyTwoThousandSevenHundredSixtyEight) {
    withOracle([](trace::DamageOracle& oracle) {
        // At level 1 and full HP the strength is 1: weak, 32,767 doubles to 65,534 → 65,534 / 64 + 1;
        // 32,768 stays → 32,768 / 64 + 1.
        DamageScenario s = atma(32767, 1, 3000, 3000);
        s.attack.record.element = ElementSet::of(Element::FIRE);
        s.state.at(kFoe).elementsWeak = ElementSet::of(Element::FIRE);
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 1024);
        s.attack.damage = 32768;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 513);
    });
}

// ─── what a blow does besides ────────────────────────────────────────────────────────────────────

TEST_F(TargetDamageTest, Zombie_AZombiesBlowPoisonsOrBlindsOneTimeInSixteenEach) {
    withOracle([](trace::DamageOracle& oracle) {
        struct Row {
            std::uint8_t position;
            StatusSet given;
        };
        for (const Row row : {Row{kDrawsPoison, StatusSet::of(StatusId::POISON)},
                              Row{kDrawsBlind, StatusSet::of(StatusId::BLIND)}, Row{kDrawsNothing, StatusSet{}}}) {
            DamageScenario s = strike(1000);
            s.state.at(kHero).status.set(StatusId::ZOMBIE);
            s.state.rng = battle::BattleRng{row.position};
            const Both both = pass(oracle, s, kFoe);
            EXPECT_EQ(both.port.statusesToSet[at(kFoe)].bytes, row.given.bytes);
            EXPECT_EQ(both.guest.statusesToSet[at(kFoe)].bytes, row.given.bytes);
        }
    });
}

TEST_F(TargetDamageTest, Blow_ABlockedBlowDoesNothingBesides) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = strike(1000);
        s.attack.scratch.golemBlocked = kAlly;
        s.state.at(kHero).status.set(StatusId::ZOMBIE);
        s.state.at(kFoe).status = StatusSet::of(StatusId::SLEEP);
        s.state.at(kFoe).controlledBy = kAlly;
        const Both both = pass(oracle, s, kFoe);
        EXPECT_THREE_WAYS(both.port.rngPosition, 0, both.guest.rngPosition);
        EXPECT_TRUE(both.port.controlled[at(kFoe)]);
        EXPECT_EQ(both.port.statusesToClear[at(kFoe)].bytes, StatusSet{}.bytes);
    });
}

TEST_F(TargetDamageTest, Blow_WakesClearsMuddleAndFreesFromControl) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = strike(1000);
        s.state.at(kFoe).status = StatusSet::of(StatusId::SLEEP, StatusId::CONFUSE);
        s.state.at(kFoe).controlledBy = kAlly;
        Both both = pass(oracle, s, kFoe);
        EXPECT_EQ(both.port.statusesToClear[at(kFoe)].bytes, StatusSet::of(StatusId::SLEEP, StatusId::CONFUSE).bytes);
        EXPECT_FALSE(both.port.controlled[at(kFoe)]);
        // A blow that puts Sleep on leaves it.
        s.attack.record.statuses.set(StatusId::SLEEP);
        both = pass(oracle, s, kFoe);
        EXPECT_EQ(both.port.statusesToClear[at(kFoe)].bytes, StatusSet::of(StatusId::CONFUSE).bytes);
        // A spell does none of it.
        s.attack.record.traits.clear(AttackTrait::PHYSICAL);
        both = pass(oracle, s, kFoe);
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
    withOracle([](trace::DamageOracle& oracle) {
        const Both both = pass(oracle, drain(300), kFoe);
        EXPECT_TAKEN(both, kFoe, 300);
        EXPECT_HEALED(both, kHero, 300);
    });
}

TEST_F(TargetDamageTest, Drain_NoMoreThanTheDrainerIsMissingUnlessUncapped) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = drain(5000);
        s.state.at(kHero).hp = 1800;
        Both both = pass(oracle, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 200);
        EXPECT_HEALED(both, kHero, 200);
        s.attack.drainCapped = false;
        both = pass(oracle, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 997);
        EXPECT_HEALED(both, kHero, 997);
    });
}

TEST_F(TargetDamageTest, Drain_NoCapWithoutDamageModification) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = drain(300);
        s.attack.scratch.damageModification = false;
        const Both both = pass(oracle, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 1000);
        EXPECT_HEALED(both, kHero, 1000);
    });
}

TEST_F(TargetDamageTest, Drain_OnMpCapsByMp) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = drain(5000);
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        s.state.at(kFoe).mp = 50;
        s.state.at(kFoe).maxMp = 100;
        s.state.at(kHero).mp = 0;
        s.state.at(kHero).maxMp = 500;
        const Both both = pass(oracle, s, kFoe);
        EXPECT_TAKEN(both, kFoe, 50);
        EXPECT_HEALED(both, kHero, 50);
    });
}

TEST_F(TargetDamageTest, Drain_BackwardCapsByTheDrainerAndTheTargetsRoom) {
    withOracle([](trace::DamageOracle& oracle) {
        // An undead target gains: no more than the drainer's 400 HP, nor than the target's 100 missing.
        DamageScenario s = drain(8900);
        s.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
        s.state.at(kFoe).undead = true;
        s.state.at(kHero).hp = 400;
        Both both = pass(oracle, s, kFoe);
        EXPECT_HEALED(both, kFoe, 100);
        EXPECT_TAKEN(both, kHero, 100);
        s.state.at(kFoe).hp = 5000;
        both = pass(oracle, s, kFoe);
        EXPECT_HEALED(both, kFoe, 400);
        EXPECT_TAKEN(both, kHero, 400);
    });
}

TEST_F(TargetDamageTest, Drain_TheDrainerWaitsToAnswerAfterABlowNotASpell) {
    withOracle([](trace::DamageOracle& oracle) {
        const int foe = kFoe.index();
        constexpr int kWaiting = 0x80;
        DamageScenario s = drain(5000);
        Both both = pass(oracle, s, kFoe);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], foe, both.guest.lastAttackerMark[at(kHero)]);
        s.attack.record.traits.set(AttackTrait::PHYSICAL);
        both = pass(oracle, s, kFoe);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], foe | kWaiting,
                          both.guest.lastAttackerMark[at(kHero)]);
        // Already waiting to answer someone: kept.
        s.state.at(kHero).lastAttacker = kAlly;
        s.state.at(kHero).retaliationPending = true;
        both = pass(oracle, s, kFoe);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], kAlly.index() | kWaiting,
                          both.guest.lastAttackerMark[at(kHero)]);
    });
}

TEST_F(TargetDamageTest, Drain_AZombieDrainersBlowWaitsOnlyWhenItsDrawGaveNothing) {
    withOracle([](trace::DamageOracle& oracle) {
        const int foe = kFoe.index();
        constexpr int kWaiting = 0x80;
        DamageScenario s = drain(5000);
        s.attack.record.traits.set(AttackTrait::PHYSICAL);
        s.attack.scratch.damageModification = false;
        s.state.at(kHero).status.set(StatusId::ZOMBIE);
        s.state.rng = battle::BattleRng{kDrawsBlind};
        Both both = pass(oracle, s, kFoe);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], foe, both.guest.lastAttackerMark[at(kHero)]);
        s.state.rng = battle::BattleRng{kDrawsNothing};
        both = pass(oracle, s, kFoe);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], foe | kWaiting,
                          both.guest.lastAttackerMark[at(kHero)]);
    });
}

// ─── the ledger ──────────────────────────────────────────────────────────────────────────────────

TEST_F(TargetDamageTest, Ledger_AddsToWhatIsThereAndStopsAtNineThousandNineHundredNinetyNine) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = strike(1000);
        s.attack.scratch.ledger.taken[at(kFoe)] = 5000;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 6000);
        s.attack.scratch.ledger.taken[at(kFoe)] = 9500;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 9999);
        s.attack.damage = 60000;
        s.attack.scratch.ledger.taken[at(kFoe)] = 9999;
        EXPECT_TAKEN(pass(oracle, s, kFoe), kFoe, 9999);
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        s.attack.damage = 1000;
        s.attack.scratch.ledger.healed[at(kFoe)] = 9500;
        EXPECT_HEALED(pass(oracle, s, kFoe), kFoe, 9999);
    });
}

// ─── the seeded sweep ────────────────────────────────────────────────────────────────────────────

constexpr int kSweepDraws = 500;

TEST_F(TargetDamageTest, TargetDamageSweep_EveryCellMatchesTheCartridge) {
    withOracle([](trace::DamageOracle& oracle) {
        std::mt19937 rng{trace::kSweepSeed};
        for (int index = 0; index < kSweepDraws; ++index) {
            const trace::TargetScenario drawn = trace::drawTargetScenario(rng);
            const TargetDamageResult port = trace::portTargetDamage(drawn.scenario, drawn.target);
            const TargetDamageResult guest = oracle.targetDamage(drawn.scenario, drawn.target);
            ASSERT_EQ(trace::differences(port, guest), "")
                << "draw " << index << ": " << trace::describe(drawn);
        }
    });
}

}  // namespace
}  // namespace ostinato::test
