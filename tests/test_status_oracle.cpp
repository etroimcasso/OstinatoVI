// The status routines on the oracle: each is where the US cartridges hold it and is refused where it
// is not, the attack frame, the battle's start and a formation change call them where the trace's
// return sites sit, a scenario written for the status pass reads back from the cells it was written
// to, a petrified battler is marked as having died on every pass the cartridge makes, not only the
// first, and the cartridge's own calls of both routines, traced, replay against the port.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/status.h"
#include "damage_oracle.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using trace::DamageOracle;
using trace::DamageScenario;
using trace::StatusChangesResult;

class StatusOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* StatusOracleTest::image_ = nullptr;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

TEST_F(StatusOracleTest, AnImageWithoutUpdateStatusIsRefused) {
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        std::vector<std::uint8_t> other(original.begin(), original.end());
        other[offsetOf(0xC24391)] ^= 0xFF;
        EXPECT_THROW(DamageOracle{other}, std::runtime_error);
    });
}

TEST_F(StatusOracleTest, AnImageWithoutMagicStatusEffectIsRefused) {
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        std::vector<std::uint8_t> other(original.begin(), original.end());
        other[offsetOf(0xC24406)] ^= 0xFF;
        EXPECT_THROW(DamageOracle{other}, std::runtime_error);
    });
}

// The calls the attack frame makes: `jsr UpdateStatus` returning to $C2:324D (battle_main.asm:8109),
// and `jsr MagicStatusEffect` returning to $C2:345C in the frame (:8356) and $C2:34F9 in the bounce
// (:8434). Each return site is the byte after a three-byte `jsr` to the routine.
TEST_F(StatusOracleTest, TheFramesCallsReturnWhereTheTraceExpects) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const auto callBefore = [&](std::uint32_t returnSite) {
            return oracle.machine().read(retropp::MemoryRegion{.at = returnSite - 3, .size = 3});
        };
        const std::vector<std::uint8_t> toUpdateStatus{0x20, 0x91, 0x43};
        const std::vector<std::uint8_t> toMagicStatusEffect{0x20, 0x06, 0x44};
        EXPECT_EQ(callBefore(0xC2324D), toUpdateStatus);
        EXPECT_EQ(callBefore(0xC2345C), toMagicStatusEffect);
        EXPECT_EQ(callBefore(0xC234F9), toMagicStatusEffect);
    });
}

// The two other calls of UpdateStatus the trace follows: the battle's start, returning to $C2:24A7
// (:6128), and a formation change, returning to $C2:50AA (:13231).
TEST_F(StatusOracleTest, TheStartsAndAFormationChangesCallsReturnWhereTheTraceExpects) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const auto callBefore = [&](std::uint32_t returnSite) {
            return oracle.machine().read(retropp::MemoryRegion{.at = returnSite - 3, .size = 3});
        };
        const std::vector<std::uint8_t> toUpdateStatus{0x20, 0x91, 0x43};
        const trace::TracedRoutine& updateStatus = trace::kTracedRoutines[3];
        ASSERT_STREQ(updateStatus.name, "UpdateStatus");
        EXPECT_EQ(updateStatus.otherReturns[0], 0xC224A7u);
        EXPECT_EQ(updateStatus.otherReturns[1], 0xC250AAu);
        for (const std::uint32_t site : updateStatus.otherReturns) {
            EXPECT_EQ(callBefore(site), toUpdateStatus);
        }
    });
}

// --- the cartridge's own calls, traced and replayed -----------------------------------------------

constexpr int kReplayDraws = 200;

std::vector<trace::TracedCall> parse(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line + "\n";
    }
    std::istringstream in{text};
    return trace::parseTrace(in);
}

TEST_F(StatusOracleTest, TheStatusesTakingEffectReplayAsTheCartridgeWorkedThemOut) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        std::mt19937 rng{trace::kSweepSeed};
        int calls = 0;
        for (int index = 0; index < kReplayDraws; ++index) {
            const DamageScenario drawn = trace::drawStatusChangesScenario(rng);
            oracle.trace(true);
            (void)oracle.statusChanges(drawn);
            oracle.trace(false);
            for (const trace::TracedCall& call : parse(oracle.takeTrace())) {
                EXPECT_EQ(call.routine, "UpdateStatus");
                EXPECT_EQ(oracle.replay(call), "") << "draw " << index;
                ++calls;
            }
        }
        EXPECT_EQ(calls, kReplayDraws);
    });
}

TEST_F(StatusOracleTest, AnAttacksStatusesOnATargetReplayAsTheCartridgeWorkedThemOut) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        std::mt19937 rng{trace::kSweepSeed};
        int calls = 0;
        for (int index = 0; index < kReplayDraws; ++index) {
            const trace::TargetScenario drawn = trace::drawStatusEffectScenario(rng);
            oracle.trace(true);
            (void)oracle.statusEffect(drawn.scenario, drawn.target);
            oracle.trace(false);
            for (const trace::TracedCall& call : parse(oracle.takeTrace())) {
                EXPECT_EQ(call.routine, "MagicStatusEffect");
                EXPECT_EQ(oracle.replay(call), "") << "draw " << index;
                ++calls;
            }
        }
        EXPECT_EQ(calls, kReplayDraws);
    });
}

// A call of UpdateStatus from the battle's start replays as one from the attack frame does; a call
// that returned anywhere the trace does not follow is refused.
TEST_F(StatusOracleTest, AStartsStatusPassReplaysAndAnUnknownCallerIsRefused) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        std::mt19937 rng{trace::kSweepSeed};
        const DamageScenario drawn = trace::drawStatusChangesScenario(rng);
        oracle.trace(true);
        (void)oracle.statusChanges(drawn);
        oracle.trace(false);
        std::vector<std::string> lines = oracle.takeTrace();
        const auto returnedTo = [&](const std::string& site) {
            std::vector<std::string> moved = lines;
            for (std::string& line : moved) {
                if (const std::size_t at = line.find(" return UpdateStatus $C2:324D"); at != std::string::npos) {
                    line.replace(at, std::string{" return UpdateStatus $C2:324D"}.size(), " return UpdateStatus " + site);
                }
            }
            return parse(moved);
        };
        const std::vector<trace::TracedCall> fromStart = returnedTo("$C2:24A7");
        ASSERT_EQ(fromStart.size(), 1u);
        EXPECT_EQ(fromStart[0].returnedTo, 0xC224A7u);
        EXPECT_EQ(oracle.replay(fromStart[0]), "");

        const std::vector<trace::TracedCall> fromElsewhere = returnedTo("$C2:1234");
        ASSERT_EQ(fromElsewhere.size(), 1u);
        EXPECT_THROW((void)oracle.replay(fromElsewhere[0]), std::runtime_error);
    });
}

// With no battler on the field the pass changes nothing, so every cell the oracle wrote reads back
// as the scenario had it.
TEST_F(StatusOracleTest, AScenarioReadsBackFromItsCells) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        DamageScenario scenario;
        battle::Battler& monster = scenario.state.at(BattlerSlot::monster(4));
        monster.status = StatusSet::of(StatusId::SLOW, StatusId::IMAGE);
        monster.statusesToSet = StatusSet::of(StatusId::DEAD);
        monster.statusesToClear = StatusSet::of(StatusId::FLOAT);
        monster.statusForCounter = StatusSet::of(StatusId::SLEEP);
        monster.pendingUpdates.speed = true;
        monster.menuMayOpen = true;
        monster.emptyGaugeAfterAction = true;
        monster.triggers.phantasm = true;
        monster.sleepTimer = 1;
        monster.stopTimer = 2;
        monster.reflectTimer = 3;
        monster.freezeTimer = 4;
        scenario.state.at(BattlerSlot::character(3)).equipmentChanged = true;
        scenario.state.diedAwaitingCounter = BattlerSet::of(BattlerSlot::monster(0));
        scenario.state.statusChanged = BattlerSet::of(BattlerSlot::character(2));
        scenario.state.monstersStanding = BattlerSet::of(BattlerSlot::monster(5));
        scenario.state.monstersFallen = BattlerSet::of(BattlerSlot::monster(1));
        const StatusChangesResult guest = oracle.statusChanges(scenario);
        EXPECT_EQ(trace::differences(trace::portStatusChanges(scenario), guest), "");
        const std::size_t at = static_cast<std::size_t>(BattlerSlot::monster(4).index());
        EXPECT_EQ(guest.status[at].bytes, monster.status.bytes);
        EXPECT_EQ(guest.statusesToSet[at].bytes, monster.statusesToSet.bytes);
        EXPECT_EQ(guest.statusesToClear[at].bytes, monster.statusesToClear.bytes);
        EXPECT_EQ(guest.statusForCounter[at].bytes, monster.statusForCounter.bytes);
        EXPECT_EQ(guest.pendingUpdates[at], 0x04);
        EXPECT_TRUE(guest.menuMayOpen[at]);
        EXPECT_TRUE(guest.emptyGaugeAfterAction[at]);
        EXPECT_TRUE(guest.phantasm[at]);
        EXPECT_EQ(guest.sleepTimer[at], 1);
        EXPECT_EQ(guest.stopTimer[at], 2);
        EXPECT_EQ(guest.reflectTimer[at], 3);
        EXPECT_EQ(guest.freezeTimer[at], 4);
        EXPECT_TRUE(guest.equipmentChanged[static_cast<std::size_t>(BattlerSlot::character(3).index())]);
        EXPECT_EQ(guest.diedAwaitingCounter, scenario.state.diedAwaitingCounter);
        EXPECT_EQ(guest.statusChanged, scenario.state.statusChanged);
        EXPECT_EQ(guest.monstersStanding, scenario.state.monstersStanding);
        EXPECT_EQ(guest.monstersFallen, scenario.state.monstersFallen);
    });
}

// A petrified monster hit by two attacks in a row: the battle's counterattack step takes it out of
// "died this action" in between, and the second pass puts it back, on the cartridge as in the port.
TEST_F(StatusOracleTest, APetrifiedMonsterIsMarkedAsDyingOnEveryPass) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        const BattlerSlot monster = BattlerSlot::monster(0);
        DamageScenario scenario;
        battle::Battler& battler = scenario.state.at(monster);
        battler.present = true;
        battler.maxHp = 2000;
        battler.hp = 2000;
        battler.statusesToSet = StatusSet::of(StatusId::PETRIFY);

        const StatusChangesResult first = oracle.statusChanges(scenario);
        battle::applyStatusChanges(scenario.state);
        EXPECT_EQ(first.diedAwaitingCounter, BattlerSet::of(monster));
        EXPECT_EQ(scenario.state.diedAwaitingCounter, BattlerSet::of(monster));

        scenario.state.diedAwaitingCounter = {};
        const StatusChangesResult second = oracle.statusChanges(scenario);
        EXPECT_EQ(trace::differences(trace::portStatusChanges(scenario), second), "");
        EXPECT_EQ(second.status[static_cast<std::size_t>(monster.index())].bytes, StatusSet::of(StatusId::PETRIFY).bytes);
        EXPECT_EQ(second.diedAwaitingCounter, BattlerSet::of(monster));
    });
}

}  // namespace
}  // namespace ostinato::test
