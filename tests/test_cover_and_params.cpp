// The cover and the attack's animation set up, on the oracle (docs/contracts/battle_commands.md,
// "The attack frame" and "Cover"): both routines are where the US cartridges hold them and an image
// without either is refused; a Dragon Horn landing with another to come is marked for the jump's
// animation and aims the next landing at random, and the last landing is not; a bodyguard who is
// controlled or held in Seize does not step in, while one controlling a monster or holding someone
// does, and an ally held in Seize is not covered. Every case is worked out by hand from the original
// and also run on the cartridge's own routine (tools/damage-trace/damage_oracle.h); the port must
// match both. Then 500 drawn scenarios through each routine.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/attack.h"
#include "battle/targeting.h"
#include "damage_oracle.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::AttackInFlight;
using battle::BattlerSet;
using battle::BattlerSlot;
using trace::CoverResult;
using trace::DamageOracle;
using trace::DamageScenario;
using trace::ParamsInitResult;

constexpr BattlerSlot kTerra = BattlerSlot::character(0);
constexpr BattlerSlot kLocke = BattlerSlot::character(1);
constexpr BattlerSlot kEdgar = BattlerSlot::character(3);
constexpr BattlerSlot kGuard = BattlerSlot::monster(0);
constexpr BattlerSlot kSoldier = BattlerSlot::monster(1);

constexpr std::uint32_t kInitGfxParams = 0xC257C2;
constexpr std::uint32_t kCoverEffect = 0xC2123B;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

class CoverAndParamsTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    // The port's animation set up over `scenario`, checked against the cartridge's own answer.
    static AttackInFlight checkedParams(const DamageScenario& scenario) {
        ParamsInitResult guest;
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            guest = oracle.initParams(scenario);
        });
        EXPECT_EQ(trace::differences(trace::portInitParams(scenario), guest), "");
        DamageScenario after = scenario;
        battle::initParams(after.attack, after.attacker);
        return after.attack;
    }

    // The port's cover over `scenario`, checked against the cartridge's own answer.
    static AttackInFlight checkedCover(const DamageScenario& scenario) {
        CoverResult guest;
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            guest = oracle.coverEffect(scenario);
        });
        EXPECT_EQ(trace::differences(trace::portCoverTarget(scenario), guest), "");
        DamageScenario after = scenario;
        battle::coverTarget(after.state, after.attack, after.attacker);
        return after.attack;
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* CoverAndParamsTest::image_ = nullptr;

// --- where the routines are ----------------------------------------------------------------------

TEST_F(CoverAndParamsTest, BothRoutinesAreWhereTheUsCartridgesHoldThem) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const std::vector<std::uint8_t> initGfxParams =
            oracle.machine().read(retropp::MemoryRegion{.at = kInitGfxParams, .size = 4});
        const std::vector<std::uint8_t> coverEffect =
            oracle.machine().read(retropp::MemoryRegion{.at = kCoverEffect, .size = 4});
        EXPECT_EQ(initGfxParams, (std::vector<std::uint8_t>{0x08, 0xE2, 0x30, 0x64}));  // php; sep #$30; stz $a0
        EXPECT_EQ(coverEffect, (std::vector<std::uint8_t>{0xDA, 0x08, 0xA5, 0xB2}));    // phx; php; lda $b2
    });
}

TEST_F(CoverAndParamsTest, AnImageWithoutEitherRoutineIsRefused) {
    for (const std::uint32_t routine : {kInitGfxParams, kCoverEffect}) {
        trace::runOnLoop([&] {
            const std::span<const std::uint8_t> original = image();
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(routine)] ^= 0xFF;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << std::hex << routine;
        });
    }
}

// --- the attack's animation set up ---------------------------------------------------------------

// Locke jumps at a monster with `attacksLeft` landings still to come after this one.
DamageScenario jumpAt(int attacksLeft, bool dragonHorn) {
    DamageScenario s;
    s.attacker = kLocke;
    s.attack.command = BattleCommandId::JUMP;
    s.attack.targets = BattlerSet::of(kGuard);
    s.attack.scratch.attacksLeft = attacksLeft;
    s.attack.scratch.dragonHorn = dragonHorn;
    for (const BattlerSlot slot : {kLocke, kGuard}) {
        s.state.at(slot).present = true;
        s.state.at(slot).hp = 500;
        s.state.at(slot).maxHp = 500;
    }
    return s;
}

TEST_F(CoverAndParamsTest, ADragonHornLandingWithTwoMoreToComeAimsTheNextAtRandom) {
    const AttackInFlight attack = checkedParams(jumpAt(2, true));
    EXPECT_TRUE(attack.params.dragonHorn);
    EXPECT_TRUE(attack.rules.randomTarget);
    EXPECT_TRUE(attack.rules.fallBackToEarlierTargets);
}

TEST_F(CoverAndParamsTest, ADragonHornLandingWithOneMoreToComeAimsTheNextAtRandom) {
    const AttackInFlight attack = checkedParams(jumpAt(1, true));
    EXPECT_TRUE(attack.params.dragonHorn);
    EXPECT_TRUE(attack.rules.randomTarget);
    EXPECT_TRUE(attack.rules.fallBackToEarlierTargets);
}

TEST_F(CoverAndParamsTest, TheLastDragonHornLandingIsNotMarked) {
    const AttackInFlight attack = checkedParams(jumpAt(0, true));
    EXPECT_FALSE(attack.params.dragonHorn);
    EXPECT_FALSE(attack.rules.randomTarget);
    EXPECT_FALSE(attack.rules.fallBackToEarlierTargets);
}

TEST_F(CoverAndParamsTest, WithoutTheDragonHornNoLandingIsMarked) {
    const AttackInFlight attack = checkedParams(jumpAt(2, false));
    EXPECT_FALSE(attack.params.dragonHorn);
    EXPECT_FALSE(attack.rules.randomTarget);
    EXPECT_FALSE(attack.rules.fallBackToEarlierTargets);
}

// A monster's spell on a party member: it keeps to the other side, and a suppressed swirl is not
// shown and is suppressed no longer.
TEST_F(CoverAndParamsTest, TheOtherSideRuleAndTheSwirlAreSetUpWithTheParams) {
    DamageScenario s = jumpAt(0, false);
    s.attacker = kGuard;
    s.attack.command = BattleCommandId::MAGIC;
    s.attack.targets = BattlerSet::of(kLocke);
    s.attack.castingSwirlSuppressed = true;
    const AttackInFlight attack = checkedParams(s);
    EXPECT_TRUE(attack.params.attackerIsMonster);
    EXPECT_FALSE(attack.params.noCharacterTargets);
    EXPECT_TRUE(attack.rules.opposingSideOnly);
    EXPECT_FALSE(attack.params.castingSwirl);
    EXPECT_FALSE(attack.castingSwirlSuppressed);
}

constexpr int kSweepDraws = 500;

TEST_F(CoverAndParamsTest, EveryDrawnScenarioSetsUpTheParamsAsTheCartridgeDoes) {
    std::mt19937 rng{trace::kSweepSeed};
    int dragonHornLandings = 0;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const DamageScenario scenario = trace::drawAttackerEffectScenario(rng);
            if (scenario.attack.scratch.dragonHorn && scenario.attack.scratch.attacksLeft != 0) {
                ++dragonHornLandings;
            }
            EXPECT_EQ(trace::differences(trace::portInitParams(scenario), oracle.initParams(scenario)), "")
                << "draw " << draw;
        }
    });
    EXPECT_GT(dragonHornLandings, 0);
}

// --- the cover -----------------------------------------------------------------------------------

// The guard's blow at Terra, Near Fatal, with Locke wearing the True Knight's relic beside her.
DamageScenario blowAtTerra() {
    DamageScenario s;
    s.attacker = kGuard;
    s.attack.canBeCritical = true;
    s.attack.targets = BattlerSet::of(kTerra);
    for (const BattlerSlot slot : {kTerra, kLocke, kEdgar, kGuard}) {
        s.state.at(slot).present = true;
        s.state.at(slot).hp = 100;
        s.state.at(slot).maxHp = 400;
    }
    s.state.at(kTerra).status.set(StatusId::NEAR_FATAL);
    s.state.at(kLocke).relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
    return s;
}

void expectCovered(const AttackInFlight& attack, BattlerSlot bodyguard) {
    EXPECT_EQ(attack.targets, BattlerSet::of(bodyguard));
    EXPECT_EQ(attack.params.coveredAlly, kTerra);
}

void expectUncovered(const AttackInFlight& attack) {
    EXPECT_EQ(attack.targets, BattlerSet::of(kTerra));
    EXPECT_FALSE(attack.params.coveredAlly);
}

TEST_F(CoverAndParamsTest, AControlledBodyguardDoesNotStepIn) {
    DamageScenario s = blowAtTerra();
    s.state.at(kLocke).controlledBy = kGuard;
    s.state.at(kGuard).controlling = kLocke;
    expectUncovered(checkedCover(s));
}

TEST_F(CoverAndParamsTest, ABodyguardControllingAMonsterStepsIn) {
    DamageScenario s = blowAtTerra();
    s.state.at(kLocke).controlling = kSoldier;
    s.state.at(kSoldier).controlledBy = kLocke;
    expectCovered(checkedCover(s), kLocke);
}

TEST_F(CoverAndParamsTest, ABodyguardWhoseControlIsEndingStepsIn) {
    DamageScenario s = blowAtTerra();
    s.state.at(kLocke).controlledBy = kGuard;
    s.state.at(kLocke).controlEnding = true;
    s.state.at(kGuard).controlling = kLocke;
    s.state.at(kGuard).controlEnding = true;
    expectCovered(checkedCover(s), kLocke);
}

TEST_F(CoverAndParamsTest, ABodyguardHeldInSeizeDoesNotStepIn) {
    DamageScenario s = blowAtTerra();
    s.state.at(kLocke).seizedBy = kGuard;
    s.state.at(kGuard).seizing = kLocke;
    expectUncovered(checkedCover(s));
}

TEST_F(CoverAndParamsTest, ABodyguardHoldingSomeoneStepsIn) {
    DamageScenario s = blowAtTerra();
    s.state.at(kLocke).seizing = kEdgar;
    s.state.at(kEdgar).seizedBy = kLocke;
    expectCovered(checkedCover(s), kLocke);
}

TEST_F(CoverAndParamsTest, AnAllyHeldInSeizeIsNotCovered) {
    DamageScenario s = blowAtTerra();
    s.state.at(kTerra).seizedBy = kGuard;
    s.state.at(kGuard).seizing = kTerra;
    expectUncovered(checkedCover(s));
}

// A Love Token's bodyguard steps in whatever the ally's HP, before any True Knight is considered.
TEST_F(CoverAndParamsTest, ALoveTokenBodyguardCoversFirst) {
    DamageScenario s = blowAtTerra();
    s.state.at(kTerra).status = {};
    s.state.at(kTerra).bodyguard = kEdgar;
    s.state.at(kEdgar).guarding = kTerra;
    const AttackInFlight attack = checkedCover(s);
    expectCovered(attack, kEdgar);
    EXPECT_EQ(attack.params.steppingIn, BattlerSet::of(kEdgar));
}

// Whether the cover would have gone otherwise for the battler at `slot` with its link cleared:
// the draw reached the rule that keeps a battler with that link out.
bool linkMattered(const DamageScenario& scenario, BattlerSlot slot, bool held) {
    DamageScenario as = scenario;
    battle::coverTarget(as.state, as.attack, as.attacker);
    DamageScenario freed = scenario;
    if (held) {
        freed.state.at(slot).seizedBy.reset();
    } else {
        freed.state.at(slot).controlledBy.reset();
    }
    battle::coverTarget(freed.state, freed.attack, freed.attacker);
    return !(as.attack.params.steppingIn == freed.attack.params.steppingIn);
}

// The target a blow at `targets` covers: the highest-numbered one.
std::optional<BattlerSlot> coveredTarget(BattlerSet targets) {
    for (int index = battle::kBattlerSlots - 1; index >= 0; --index) {
        if (targets.has(BattlerSlot::fromIndex(index))) {
            return BattlerSlot::fromIndex(index);
        }
    }
    return std::nullopt;
}

// Over 500 drawn scenarios the cover leaves the targets, the covered ally and the battlers stepping
// in as the cartridge's routine does, and the draws reach both bodyguards and both exclusions.
TEST_F(CoverAndParamsTest, EveryDrawnScenarioIsCoveredAsTheCartridgeCoversIt) {
    std::mt19937 rng{trace::kSweepSeed};
    int loveTokens = 0;
    int trueKnights = 0;
    int controlledKeptOut = 0;
    int heldKeptOut = 0;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const DamageScenario scenario = trace::drawCoverScenario(rng);
            EXPECT_EQ(trace::differences(trace::portCoverTarget(scenario), oracle.coverEffect(scenario)), "")
                << "draw " << draw;

            DamageScenario after = scenario;
            battle::coverTarget(after.state, after.attack, after.attacker);
            if (after.attack.params.coveredAlly) {
                const auto& bodyguard = scenario.state.at(*after.attack.params.coveredAlly).bodyguard;
                const bool byLoveToken = bodyguard && after.attack.targets == BattlerSet::of(*bodyguard);
                ++(byLoveToken ? loveTokens : trueKnights);
            }
            const std::optional<BattlerSlot> covered = coveredTarget(scenario.attack.targets);
            for (int index = 0; index < battle::kBattlerSlots; ++index) {
                const BattlerSlot slot = BattlerSlot::fromIndex(index);
                if (slot == covered) {
                    continue;
                }
                const battle::Battler& battler = scenario.state.at(slot);
                if (battler.controlledBy && !battler.controlEnding && linkMattered(scenario, slot, false)) {
                    ++controlledKeptOut;
                }
                if (battler.seizedBy && linkMattered(scenario, slot, true)) {
                    ++heldKeptOut;
                }
            }
        }
    });
    EXPECT_GT(loveTokens, 0);
    EXPECT_GT(trueKnights, 0);
    EXPECT_GT(controlledKeptOut, 0);
    EXPECT_GT(heldKeptOut, 0);
    std::cout << "love tokens " << loveTokens << ", true knights " << trueKnights << ", controlled kept out "
              << controlledKeptOut << ", held kept out " << heldKeptOut << '\n';
}

}  // namespace
}  // namespace ostinato::test
