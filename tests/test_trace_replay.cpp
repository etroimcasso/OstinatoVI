// A damage trace replayed against the port: the trace's lines read back into calls, and each call
// rebuilt as a battle and worked out by the port, the port's answer compared with every cell the
// cartridge's routine wrote. The cartridge's own calls, traced through the oracle, prove the replay;
// every recording under tests/fixtures/damage-trace/ is replayed the same way.
//
// Authority: docs/contracts/battle_damage.md.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/damage.h"
#include "damage_oracle.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using trace::TracedCall;

// The cartridge, read once for the binary; every test starts its own oracle on it.
class TraceReplayTest : public ::testing::Test {
public:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* TraceReplayTest::image_ = nullptr;

template <typename Body>
void withOracle(Body body) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{TraceReplayTest::image()};
        oracle.start();
        body(oracle);
    });
}

std::vector<TracedCall> parse(const std::string& text) {
    std::istringstream in{text};
    return trace::parseTrace(in);
}

std::vector<TracedCall> parse(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line + "\n";
    }
    return parse(text);
}

// ─── reading a trace ────────────────────────────────────────────────────────────────────────────

TEST(TraceParse, ACallIsItsEntryItsContextItsAccessesAndItsReturn) {
    const std::vector<TracedCall> calls = parse(
        "f7 enter CalcTargetDmg $C2:0B83\n"
        "f7 params 00 04 0F 00 01 00 00 00 00 00 00 00 00 00 00 00 hits FF rules 02 previous --\n"
        "f7 r $7E:11A6 0A\n"
        "f7 r $7E:33D8 FF\n"
        "f7 w $7E:33D8 2C\n"
        "f7 r $7E:33D8 2C\n"
        "f7 w $7E:33D8 2D\n"
        "f7 return CalcTargetDmg $C2:346C\n");
    ASSERT_EQ(calls.size(), 1u);
    const TracedCall& call = calls[0];
    EXPECT_EQ(call.routine, "CalcTargetDmg");
    EXPECT_EQ(call.frame, 7u);
    EXPECT_EQ(call.returnedTo, 0xC2346Cu);
    EXPECT_EQ(call.params[1], 0x04);
    EXPECT_EQ(call.hitsToCome, 0xFF);
    EXPECT_EQ(call.aimingRules, 0x02);
    EXPECT_FALSE(call.previousAttacker.has_value());
    // A cell read before any write is an input, its first value; a write keeps its last value.
    EXPECT_EQ(call.inputs.at(0x7E11A6), 0x0A);
    EXPECT_EQ(call.inputs.at(0x7E33D8), 0xFF);
    EXPECT_EQ(call.writes.at(0x7E33D8), 0x2D);
    EXPECT_EQ(call.inputs.size(), 2u);
}

TEST(TraceParse, ACallThatNeverReturnsIsLeftOutAndAStrayReturnIsSkipped) {
    const std::vector<TracedCall> calls = parse(
        "f1 return CalcTargetDmg $C2:346C\n"
        "f2 enter CalcDmg $C2:2B9D\n"
        "f2 r $7E:11A2 01\n"
        "f3 enter CalcDmg $C2:2B9D\n"
        "f3 params 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 hits FF rules 00 previous 04\n"
        "f3 return CalcDmg $C2:322E\n"
        "window closed at frame 4\n");
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].frame, 3u);
    EXPECT_TRUE(calls[0].inputs.empty());
    EXPECT_EQ(calls[0].previousAttacker, std::optional<std::uint8_t>{0x04});
}

// The frame works out an attack's statuses on a target and then its special effect on the same one:
// the effect's call takes the target whose cells the status call touched, and only straight after one.
TEST(TraceParse, ATargetEffectAfterTheStatusesTakesTheirTarget) {
    const std::vector<TracedCall> calls = parse(
        "f5 enter MagicStatusEffect $C2:4406\n"
        "f5 r $7E:3EEE 00\n"
        "f5 w $7E:3DDE 04\n"
        "f5 return MagicStatusEffect $C2:345C\n"
        "f5 enter DoTargetEffect $C2:387E\n"
        "f5 return DoTargetEffect $C2:345F\n"
        "f6 enter DoTargetEffect $C2:387E\n"
        "f6 return DoTargetEffect $C2:345F\n");
    ASSERT_EQ(calls.size(), 3u);
    EXPECT_FALSE(calls[0].target.has_value());
    // $3EEE and $3DDE are battler 5's statuses and statuses to set.
    EXPECT_EQ(calls[1].target, battle::BattlerSlot::fromIndex(5));
    EXPECT_FALSE(calls[2].target.has_value());
}

TEST(TraceParse, ALineThatIsNotAnEventFailsNamingIt) {
    EXPECT_THROW(parse("f1 enter CalcDmg C22B9D\n"), std::runtime_error);
    EXPECT_THROW(parse("f1 r $7E:11A2 1G\n"), std::runtime_error);
    EXPECT_THROW(parse("f1 jump CalcDmg $C2:2B9D\n"), std::runtime_error);
}

// ─── the cartridge's own calls, traced and replayed ─────────────────────────────────────────────

constexpr int kReplayDraws = 200;

// Every call `lines` holds replays with no difference; returns how many there were, by return site.
struct Replayed {
    int frameCalls = 0;
    int bounceCalls = 0;
};

Replayed replayAll(trace::DamageOracle& oracle, const std::vector<std::string>& lines, const std::string& draw) {
    Replayed replayed;
    for (const TracedCall& call : parse(lines)) {
        EXPECT_EQ(oracle.replay(call), "") << draw << ": " << call.routine << " returning to " << call.returnedTo;
        bool bounce = false;
        for (const trace::TracedRoutine& routine : trace::kTracedRoutines) {
            bounce = bounce || (routine.bounceReturn != 0 && call.returnedTo == routine.bounceReturn);
        }
        ++(bounce ? replayed.bounceCalls : replayed.frameCalls);
    }
    return replayed;
}

TEST_F(TraceReplayTest, TheBaseDamageReplaysAsTheCartridgeWorkedItOut) {
    withOracle([](trace::DamageOracle& oracle) {
        std::mt19937 rng{trace::kSweepSeed};
        int calls = 0;
        for (int index = 0; index < kReplayDraws; ++index) {
            const trace::DamageScenario drawn = trace::drawScenario(rng);
            oracle.trace(true);
            (void)oracle.calcDamage(drawn);
            oracle.trace(false);
            calls += replayAll(oracle, oracle.takeTrace(), "draw " + std::to_string(index)).frameCalls;
        }
        EXPECT_EQ(calls, kReplayDraws);
    });
}

TEST_F(TraceReplayTest, TheDamageToOneTargetReplaysAsTheCartridgeWorkedItOut) {
    withOracle([](trace::DamageOracle& oracle) {
        std::mt19937 rng{trace::kSweepSeed};
        int calls = 0;
        for (int index = 0; index < kReplayDraws; ++index) {
            const trace::TargetScenario drawn = trace::drawTargetScenario(rng);
            oracle.trace(true);
            (void)oracle.targetDamage(drawn.scenario, drawn.target);
            oracle.trace(false);
            calls += replayAll(oracle, oracle.takeTrace(), "draw " + std::to_string(index)).frameCalls;
        }
        EXPECT_EQ(calls, kReplayDraws);
    });
}

TEST_F(TraceReplayTest, TheDamageTakenAndShownReplaysAsTheCartridgeWorkedItOut) {
    withOracle([](trace::DamageOracle& oracle) {
        std::mt19937 rng{trace::kSweepSeed};
        int calls = 0;
        for (int index = 0; index < kReplayDraws; ++index) {
            const trace::DamageScenario drawn = trace::drawApplyScenario(rng);
            oracle.trace(true);
            (void)oracle.applyDamage(drawn);
            oracle.trace(false);
            calls += replayAll(oracle, oracle.takeTrace(), "draw " + std::to_string(index)).frameCalls;
        }
        EXPECT_EQ(calls, kReplayDraws);
    });
}

TEST_F(TraceReplayTest, ABouncesCallsReplayAsTheCartridgeWorkedThemOut) {
    withOracle([](trace::DamageOracle& oracle) {
        std::mt19937 rng{trace::kSweepSeed};
        int calls = 0;
        for (int index = 0; index < kReplayDraws; ++index) {
            const trace::DamageScenario drawn = trace::drawReflectScenario(rng);
            oracle.trace(true);
            (void)oracle.reflectedAttack(drawn);
            oracle.trace(false);
            const Replayed replayed = replayAll(oracle, oracle.takeTrace(), "draw " + std::to_string(index));
            EXPECT_EQ(replayed.frameCalls, 0) << "draw " << index;
            calls += replayed.bounceCalls;
        }
        // Each bounce shows its damage once, and most bounces hit someone.
        EXPECT_GT(calls, kReplayDraws);
        RecordProperty("bounce calls", calls);
    });
}

// ─── recordings ─────────────────────────────────────────────────────────────────────────────────

// Every damage-trace log under tests/fixtures/damage-trace/ replays with no difference. With no
// recordings there is nothing to replay.
TEST_F(TraceReplayTest, EveryRecordingReplaysAsTheGameWorkedItOut) {
    const std::filesystem::path recordings{OSTINATO_DAMAGE_TRACE_RECORDINGS};
    std::vector<std::filesystem::path> logs;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(recordings, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            logs.push_back(entry.path());
        }
    }
    int calls = 0;
    withOracle([&](trace::DamageOracle& oracle) {
        for (const std::filesystem::path& log : logs) {
            std::ifstream in{log};
            for (const TracedCall& call : trace::parseTrace(in)) {
                EXPECT_EQ(oracle.replay(call), "")
                    << log.filename().string() << " frame " << call.frame << ": " << call.routine;
                ++calls;
            }
        }
    });
    RecordProperty("recordings", static_cast<int>(logs.size()));
    RecordProperty("calls", calls);
}

}  // namespace
}  // namespace ostinato::test
