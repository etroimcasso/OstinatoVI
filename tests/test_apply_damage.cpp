// The damage taken and shown: HP and MP changed by each battler's ledger entries, a battler brought
// down, what a battler that lost anything remembers, the Golem's HP, and the numerals in the order
// and the form the script shows them. Each case is worked out three ways — the port, by hand from the
// original, and the cartridge's own routine through the damage oracle — and every other cell the
// routine writes is compared between the port and the cartridge as well. A seeded sweep then draws
// scenarios at random and checks the port against the cartridge alone.
//
// Authority: docs/contracts/battle_damage.md.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/attack.h"
#include "battle/damage.h"
#include "damage_oracle.h"
#include "ostinato/attack_flags.h"
#include "ostinato/status_id.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using battle::DamageNumber;
using battle::DamageNumbersShown;
using battle::DamageNumberWait;
using trace::ApplyDamageResult;
using trace::DamageScenario;

// The cartridge, read once for the binary; every test starts its own oracle on it.
class ApplyDamageTest : public ::testing::Test {
public:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* ApplyDamageTest::image_ = nullptr;

// The three-way check: the port's answer, the value worked out by hand, and the cartridge's.
#define EXPECT_THREE_WAYS(port, hand, guest)                                                     \
    do {                                                                                        \
        const auto port_ = (port);                                                              \
        const auto hand_ = (hand);                                                              \
        const auto guest_ = (guest);                                                            \
        EXPECT_EQ(port_, hand_) << "the port disagrees with the hand trace";                    \
        EXPECT_EQ(guest_, hand_) << "the cartridge disagrees with the hand trace";               \
        EXPECT_EQ(port_, guest_) << "the port disagrees with the cartridge";                     \
    } while (0)

// Run `body` with a started oracle, from a tick of the run loop.
template <typename Body>
void withOracle(Body body) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{ApplyDamageTest::image()};
        oracle.start();
        body(oracle);
    });
}

constexpr BattlerSlot kHero = BattlerSlot::character(0);
constexpr BattlerSlot kAlly = BattlerSlot::character(1);
constexpr BattlerSlot kFoe = BattlerSlot::monster(0);
constexpr BattlerSlot kOtherFoe = BattlerSlot::monster(1);

std::size_t at(BattlerSlot slot) {
    return static_cast<std::size_t>(slot.index());
}

// A party member's attack with nothing in the ledger yet; every battler at 1,000 of 2,000 HP and
// 100 of 200 MP.
DamageScenario attackBy(BattlerSlot attacker) {
    DamageScenario s;
    s.attacker = attacker;
    for (battle::Battler& battler : s.state.battlers) {
        battler.hp = 1000;
        battler.maxHp = 2000;
        battler.mp = 100;
        battler.maxMp = 200;
    }
    return s;
}

void take(DamageScenario& s, BattlerSlot slot, std::uint16_t amount) {
    s.attack.scratch.ledger.taken[at(slot)] = amount;
}

void heal(DamageScenario& s, BattlerSlot slot, std::uint16_t amount) {
    s.attack.scratch.ledger.healed[at(slot)] = amount;
}

// The numerals as one line: each DamageNumbersShown in brackets, "together" or "one at a time",
// then each battler's index and amount, "miss", and "+" for a gain.
std::string numerals(const std::vector<DamageNumbersShown>& shown) {
    std::string text;
    for (const DamageNumbersShown& one : shown) {
        text += one.wait == DamageNumberWait::UntilGone ? "[together" : "[one at a time";
        for (const DamageNumber& number : one.numbers) {
            text += " " + std::to_string(number.target.index()) + ":" +
                    (number.miss ? std::string{"miss"} : std::to_string(number.amount)) + (number.heals ? "+" : "");
        }
        text += "]";
    }
    return text;
}

// The damage taken and shown by the port and by the cartridge. Every cell either side writes must
// agree.
struct Both {
    ApplyDamageResult port;
    ApplyDamageResult guest;
};

Both apply(trace::DamageOracle& oracle, const DamageScenario& s) {
    Both both{trace::portApplyDamage(s), oracle.applyDamage(s)};
    EXPECT_EQ(trace::differences(both.port, both.guest), "");
    return both;
}

#define EXPECT_HP(both, slot, hand) EXPECT_THREE_WAYS((both).port.hp[at(slot)], hand, (both).guest.hp[at(slot)])
#define EXPECT_MP(both, slot, hand) EXPECT_THREE_WAYS((both).port.mp[at(slot)], hand, (both).guest.mp[at(slot)])
#define EXPECT_NUMERALS(both, hand) EXPECT_THREE_WAYS(numerals((both).port.shown), std::string{hand}, numerals((both).guest.shown))
#define EXPECT_SET_TO_DIE(both, slot, hand)                                                   \
    EXPECT_THREE_WAYS((both).port.statusesToSet[at(slot)].has(StatusId::DEAD), hand,         \
                      (both).guest.statusesToSet[at(slot)].has(StatusId::DEAD))

// ─── HP and MP ───────────────────────────────────────────────────────────────────────────────────

TEST_F(ApplyDamageTest, Loss_ComesOffHpAndTheLedgerEmpties) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        take(s, kFoe, 120);
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kFoe, 880);
        EXPECT_MP(both, kFoe, 100);
        EXPECT_NUMERALS(both, "[one at a time 4:120]");
        EXPECT_THREE_WAYS(both.port.ledger.taken[at(kFoe)].has_value(), false,
                          both.guest.ledger.taken[at(kFoe)].has_value());
    });
}

TEST_F(ApplyDamageTest, Gain_StopsAtTheMaximum) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        heal(s, kAlly, 300);
        s.state.at(kAlly).hp = 1900;
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kAlly, 2000);
        EXPECT_NUMERALS(both, "[one at a time 1:300+]");
    });
}

TEST_F(ApplyDamageTest, GainPastSixtyFiveThousand_StopsAtTheMaximum) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        battle::Battler& foe = s.state.at(kFoe);
        foe.hp = 65000;
        foe.maxHp = 65535;
        heal(s, kFoe, 9999);
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kFoe, 65535);
    });
}

TEST_F(ApplyDamageTest, LossToExactlyZero_SetsTheBattlerToDieAndCancelsTheWeaponSpell) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        s.state.at(kFoe).hp = 50;
        s.attack.scratch.weaponSpellcast.raw = 0x9F;
        take(s, kFoe, 50);
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kFoe, 0);
        EXPECT_SET_TO_DIE(both, kFoe, true);
        EXPECT_THREE_WAYS(both.port.weaponSpellcast, std::uint8_t{0}, both.guest.weaponSpellcast);
    });
}

TEST_F(ApplyDamageTest, LossPastZero_StopsAtZero) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        s.state.at(kFoe).hp = 30;
        take(s, kFoe, 9999);
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kFoe, 0);
        EXPECT_SET_TO_DIE(both, kFoe, true);
        EXPECT_NUMERALS(both, "[one at a time 4:9999]");
    });
}

TEST_F(ApplyDamageTest, LossLeavingOneHp_KeepsTheWeaponSpell) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        s.state.at(kFoe).hp = 51;
        s.attack.scratch.weaponSpellcast.raw = 0x9F;
        take(s, kFoe, 50);
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kFoe, 1);
        EXPECT_SET_TO_DIE(both, kFoe, false);
        EXPECT_THREE_WAYS(both.port.weaponSpellcast, std::uint8_t{0x9F}, both.guest.weaponSpellcast);
    });
}

TEST_F(ApplyDamageTest, Zombie_AtZeroHpIsNotSetToDie) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        s.state.at(kAlly).hp = 10;
        s.state.at(kAlly).status.set(StatusId::ZOMBIE);
        s.attack.scratch.weaponSpellcast.raw = 0x9F;
        take(s, kAlly, 40);
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kAlly, 0);
        EXPECT_SET_TO_DIE(both, kAlly, false);
        EXPECT_THREE_WAYS(both.port.weaponSpellcast, std::uint8_t{0}, both.guest.weaponSpellcast);
    });
}

TEST_F(ApplyDamageTest, LossAndGain_TheDifferenceIsTakenAndBothAreShownTheGainSecond) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        take(s, kFoe, 100);
        heal(s, kFoe, 30);
        take(s, kOtherFoe, 30);
        heal(s, kOtherFoe, 100);
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kFoe, 930);
        EXPECT_HP(both, kOtherFoe, 1070);
        EXPECT_NUMERALS(both, "[one at a time 5:30 4:100][one at a time 5:100+ 4:30+]");
    });
}

TEST_F(ApplyDamageTest, AttackOnMp_ChangesMpAndMarksTheSpellsToBeWorkedOut) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        take(s, kFoe, 20);
        heal(s, kAlly, 500);
        const Both both = apply(oracle, s);
        EXPECT_MP(both, kFoe, 80);
        EXPECT_HP(both, kFoe, 1000);
        EXPECT_MP(both, kAlly, 200);
        EXPECT_THREE_WAYS(both.port.enabledMagic[at(kFoe)], true, both.guest.enabledMagic[at(kFoe)]);
        EXPECT_THREE_WAYS(both.port.enabledMagic[at(kAlly)], true, both.guest.enabledMagic[at(kAlly)]);
        EXPECT_THREE_WAYS(both.port.enabledMagic[at(kOtherFoe)], false, both.guest.enabledMagic[at(kOtherFoe)]);
    });
}

TEST_F(ApplyDamageTest, MpToZero_KillsOnlyABattlerThatDiesAtZeroMpAndTakesItsHpToo) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        s.state.at(kFoe).diesAtZeroMp = true;
        take(s, kFoe, 150);
        take(s, kOtherFoe, 150);
        const Both both = apply(oracle, s);
        EXPECT_MP(both, kFoe, 0);
        EXPECT_HP(both, kFoe, 0);
        EXPECT_SET_TO_DIE(both, kFoe, true);
        EXPECT_MP(both, kOtherFoe, 0);
        EXPECT_HP(both, kOtherFoe, 1000);
        EXPECT_SET_TO_DIE(both, kOtherFoe, false);
        // Both lost MP, so both remember who attacked them.
        EXPECT_THREE_WAYS(both.port.lastDamagedBy[at(kOtherFoe)], std::optional<BattlerSlot>{kHero},
                          both.guest.lastDamagedBy[at(kOtherFoe)]);
    });
}

// ─── who loses nothing ──────────────────────────────────────────────────────────────────────────

TEST_F(ApplyDamageTest, Invincible_LosesNothingAndShowsZero) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        s.state.invincible.add(kFoe);
        take(s, kFoe, 500);
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kFoe, 1000);
        EXPECT_NUMERALS(both, "[one at a time 4:0]");
        EXPECT_THREE_WAYS(both.port.lastDamagedBy[at(kFoe)], std::optional<BattlerSlot>{},
                          both.guest.lastDamagedBy[at(kFoe)]);
    });
}

TEST_F(ApplyDamageTest, GolemBlock_NobodyLosesAnythingAndTheGolemTakesTheBlockedHit) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kFoe);
        s.attack.scratch.golemBlocked = kAlly;
        s.state.golemHp = 1000;
        take(s, kAlly, 300);
        take(s, kHero, 200);
        heal(s, kFoe, 50);
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kAlly, 1000);
        EXPECT_HP(both, kHero, 1000);
        // A gain still counts while the block is on.
        EXPECT_HP(both, kFoe, 1050);
        EXPECT_THREE_WAYS(both.port.golemHp, std::uint16_t{700}, both.guest.golemHp);
        EXPECT_NUMERALS(both, "[one at a time 4:50+ 1:300 0:200]");
    });
}

TEST_F(ApplyDamageTest, GolemHp_StopsAtZero) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kFoe);
        s.attack.scratch.golemBlocked = kAlly;
        s.state.golemHp = 100;
        take(s, kAlly, 300);
        const Both both = apply(oracle, s);
        EXPECT_THREE_WAYS(both.port.golemHp, std::uint16_t{0}, both.guest.golemHp);
    });
}

TEST_F(ApplyDamageTest, InterceptorBlock_NobodyLosesAnythingAndTheGolemIsUntouched) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kFoe);
        s.attack.scratch.interceptorBlocked = kAlly;
        s.state.golemHp = 1000;
        take(s, kAlly, 300);
        const Both both = apply(oracle, s);
        EXPECT_HP(both, kAlly, 1000);
        EXPECT_THREE_WAYS(both.port.golemHp, std::uint16_t{1000}, both.guest.golemHp);
    });
}

// ─── who remembers it ───────────────────────────────────────────────────────────────────────────

TEST_F(ApplyDamageTest, Loss_MarksTheAttackerToBeAnsweredAndRemembersIt) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kFoe);
        take(s, kHero, 10);
        take(s, kAlly, 10);
        // The ally is already waiting to answer the other monster.
        s.state.at(kAlly).lastAttacker = kOtherFoe;
        s.state.at(kAlly).retaliationPending = true;
        const Both both = apply(oracle, s);
        constexpr std::uint8_t kWaiting = 0x80;
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], static_cast<std::uint8_t>(kWaiting | kFoe.index()),
                          both.guest.lastAttackerMark[at(kHero)]);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kAlly)],
                          static_cast<std::uint8_t>(kWaiting | kOtherFoe.index()),
                          both.guest.lastAttackerMark[at(kAlly)]);
        EXPECT_THREE_WAYS(both.port.lastDamagedBy[at(kHero)], std::optional<BattlerSlot>{kFoe},
                          both.guest.lastDamagedBy[at(kHero)]);
        EXPECT_THREE_WAYS(both.port.lastDamagedBy[at(kAlly)], std::optional<BattlerSlot>{kFoe},
                          both.guest.lastDamagedBy[at(kAlly)]);
    });
}

TEST_F(ApplyDamageTest, PartyMemberThatLosesAnything_MayBeCounteredForByABlackBelt) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        take(s, kAlly, 10);
        heal(s, BattlerSlot::character(2), 10);
        take(s, kFoe, 10);
        const Both both = apply(oracle, s);
        battle::BattlerSet expected = battle::BattlerSet::of(kAlly);
        EXPECT_THREE_WAYS(both.port.blackBeltEligible, expected, both.guest.blackBeltEligible);
    });
}

TEST_F(ApplyDamageTest, LossByItsOwnAttack_IsMarkedButNotRemembered) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        take(s, kHero, 10);
        const Both both = apply(oracle, s);
        constexpr std::uint8_t kWaiting = 0x80;
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], static_cast<std::uint8_t>(kWaiting | kHero.index()),
                          both.guest.lastAttackerMark[at(kHero)]);
        EXPECT_THREE_WAYS(both.port.lastDamagedBy[at(kHero)], std::optional<BattlerSlot>{},
                          both.guest.lastDamagedBy[at(kHero)]);
        EXPECT_THREE_WAYS(both.port.blackBeltEligible, battle::BattlerSet{}, both.guest.blackBeltEligible);
    });
}

TEST_F(ApplyDamageTest, GainOrZero_MarksNoOne) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kFoe);
        heal(s, kHero, 10);
        take(s, kAlly, 0);
        const Both both = apply(oracle, s);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kHero)], std::uint8_t{0}, both.guest.lastAttackerMark[at(kHero)]);
        EXPECT_THREE_WAYS(both.port.lastAttackerMark[at(kAlly)], std::uint8_t{0}, both.guest.lastAttackerMark[at(kAlly)]);
        EXPECT_NUMERALS(both, "[one at a time 1:0 0:10+]");
    });
}

// ─── the numerals ───────────────────────────────────────────────────────────────────────────────

TEST_F(ApplyDamageTest, Miss_ShownOnlyOverABattlerWithNoEntry) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        s.attack.scratch.missed.add(kFoe);
        s.attack.scratch.missed.add(kOtherFoe);
        take(s, kOtherFoe, 70);
        const Both both = apply(oracle, s);
        EXPECT_NUMERALS(both, "[one at a time 5:70 4:miss]");
        EXPECT_THREE_WAYS(both.port.missed, battle::BattlerSet::of(kFoe), both.guest.missed);
    });
}

TEST_F(ApplyDamageTest, NoEntryAndNoMiss_ShowsNothing) {
    withOracle([](trace::DamageOracle& oracle) {
        const Both both = apply(oracle, attackBy(kHero));
        EXPECT_NUMERALS(both, "");
    });
}

TEST_F(ApplyDamageTest, FourNumerals_OneAtATime_FiveAllAtOnce) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario four = attackBy(kHero);
        for (int position = 0; position < 4; ++position) {
            take(four, BattlerSlot::monster(position), static_cast<std::uint16_t>(10 + position));
        }
        EXPECT_NUMERALS(apply(oracle, four), "[one at a time 7:13 6:12 5:11 4:10]");

        DamageScenario five = four;
        five.attack.scratch.missed.add(BattlerSlot::monster(4));
        EXPECT_NUMERALS(apply(oracle, five), "[together 8:miss 7:13 6:12 5:11 4:10]");
    });
}

TEST_F(ApplyDamageTest, SecondPass_WaitsByItsOwnCount) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = attackBy(kHero);
        for (int position = 0; position < 5; ++position) {
            take(s, BattlerSlot::monster(position), 1);
        }
        heal(s, kFoe, 2);
        EXPECT_NUMERALS(apply(oracle, s), "[together 8:1 7:1 6:1 5:1 4:1][one at a time 4:2+]");
    });
}

// ─── the sweep ───────────────────────────────────────────────────────────────────────────────────

constexpr int kSweepDraws = 500;

TEST_F(ApplyDamageTest, ApplyDamageSweep_EveryCellMatchesTheCartridge) {
    withOracle([](trace::DamageOracle& oracle) {
        std::mt19937 rng{trace::kSweepSeed};
        for (int index = 0; index < kSweepDraws; ++index) {
            const DamageScenario drawn = trace::drawApplyScenario(rng);
            const ApplyDamageResult port = trace::portApplyDamage(drawn);
            const ApplyDamageResult guest = oracle.applyDamage(drawn);
            ASSERT_EQ(trace::differences(port, guest), "") << "draw " << index;
        }
    });
}

}  // namespace
}  // namespace ostinato::test
