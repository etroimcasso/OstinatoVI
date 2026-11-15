// The esper's spells learned and the level-up on the oracle (docs/contracts/battle_espers.md): both
// routines are where the US cartridges hold them, both calls return where the battle's end makes them,
// and an image without either is refused; then 500 drawn records through each routine (the cartridge's
// own routine, tools/damage-trace/src/oracle/damage_oracle.h, against the port), every call the oracle
// makes traced and replayed against the port, and a won battle recorded on the cartridge replayed the
// same way.
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <algorithm>
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

#include "data/esper_properties.h"
#include "espers_oracle_cases.h"
#include "game/character_record.h"
#include "oracle/damage_oracle.h"
#include "ostinato/attack_id.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using game::SpellProgress;
using trace::DamageOracle;
using trace::LevelUpResult;
using trace::RecordScenario;
using trace::SpellLearningResult;

constexpr std::uint32_t kLearnGenjuMagic = 0xC2602A;
constexpr std::uint32_t kDoLevelUp = 0xC260C2;
// Where the battle's end calls them from: the instruction after each call.
constexpr std::uint32_t kAfterLearnGenjuMagic = 0xC25E72;
constexpr std::uint32_t kAfterDoLevelUp = 0xC260B4;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

// --- where the routines are ----------------------------------------------------------------------

TEST_F(EspersOracleTest, BothRoutinesAreWhereTheUsCartridgesHoldThem) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const auto bytes = [&](std::uint32_t at) {
            return oracle.machine().read(retropp::MemoryRegion{.at = at, .size = 4});
        };
        EXPECT_EQ(bytes(kLearnGenjuMagic), (std::vector<std::uint8_t>{0xDA, 0x20, 0x93, 0x62}));  // phx; jsr $6293
        EXPECT_EQ(bytes(kDoLevelUp), (std::vector<std::uint8_t>{0x08, 0xFE, 0x08, 0x16}));        // php; inc $1608,x
    });
}

TEST_F(EspersOracleTest, BothCallsReturnWhereTheBattlesEndMakesThem) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const auto callBefore = [&](std::uint32_t at) {
            return oracle.machine().read(retropp::MemoryRegion{.at = at - 3, .size = 3});
        };
        EXPECT_EQ(callBefore(kAfterLearnGenjuMagic), (std::vector<std::uint8_t>{0x20, 0x2A, 0x60}));  // jsr $602A
        EXPECT_EQ(callBefore(kAfterDoLevelUp), (std::vector<std::uint8_t>{0x20, 0xC2, 0x60}));        // jsr $60C2
    });
    EXPECT_EQ(trace::kTracedRoutines[7].frameReturn, kAfterLearnGenjuMagic);
    EXPECT_EQ(trace::kTracedRoutines[8].frameReturn, kAfterDoLevelUp);
}

TEST_F(EspersOracleTest, AnImageWithoutEitherRoutineIsRefused) {
    OSTINATO_SKIP_WHEN_BRIEF();
    for (const std::uint32_t routine : {kLearnGenjuMagic, kDoLevelUp}) {
        trace::runOnLoop([&] {
            const std::span<const std::uint8_t> original = image();
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(routine)] ^= 0xFF;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << std::hex << routine;
        });
    }
}

// --- the sweeps and the replay -------------------------------------------------------------------

constexpr int kSweepDraws = 500;

TEST_F(EspersOracleTest, EveryDrawnLevelUpIsTheCartridges) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    int capped = 0;
    int statLimited = 0;
    int wrapped = 0;
    withStartedOracle(image(), [&](DamageOracle& oracle) {
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const RecordScenario s = trace::drawLevelUpScenario(rng);
            const LevelUpResult port = trace::portLevelUp(s);
            EXPECT_EQ(trace::differences(port, oracle.levelUp(s)), "") << "draw " << draw << ": " << trace::describe(s);
            capped += port.maxHp == game::kMaxHp || port.maxMp == game::kMaxMp;
            statLimited += port.vigor == 128 || port.speed == 128 || port.stamina == 128 || port.magicPower == 128;
            wrapped += (s.record.vigor >= 254 && port.vigor <= 1) || (s.record.speed >= 254 && port.speed <= 1) ||
                       (s.record.stamina >= 254 && port.stamina <= 1) ||
                       (s.record.magicPower >= 254 && port.magicPower <= 1);
        }
    });
    EXPECT_GT(capped, 0);
    EXPECT_GT(statLimited, 0);
    EXPECT_GT(wrapped, 0);
}

TEST_F(EspersOracleTest, EveryDrawnLearningIsTheCartridges) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    int justLearned = 0;
    int productsPastAByte = 0;
    withStartedOracle(image(), [&](DamageOracle& oracle) {
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const RecordScenario s = trace::drawLearningScenario(rng);
            for (const EsperSpell& slot : getEsperProperties(*s.record.esper).spells) {
                productsPastAByte += slot.learnRate * s.magicPoints > 255;
            }
            const SpellLearningResult port = trace::portLearnFromEsper(s);
            EXPECT_EQ(trace::differences(port, oracle.learnFromEsper(s)), "")
                << "draw " << draw << ": " << trace::describe(s);
            for (std::size_t spell = 0; spell < port.progress.size(); ++spell) {
                justLearned += port.progress[spell] == SpellProgress::kSpellJustLearned &&
                               s.spells.progress[spell] != SpellProgress::kSpellJustLearned;
            }
        }
    });
    EXPECT_GT(justLearned, 0);
    EXPECT_GT(productsPastAByte, 0);
}

std::vector<trace::TracedCall> parse(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line + "\n";
    }
    std::istringstream in{text};
    return trace::parseTrace(in);
}

// Every call the oracle makes, traced: it reads nothing but the cells the oracle wrote — the record,
// the character's spells and the battle's-end cells — and it replays against the port with no difference.
TEST_F(EspersOracleTest, EveryTracedCallReadsOnlyWhatWasWrittenAndReplays) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 levelRng{trace::kSweepSeed};
    std::mt19937 learningRng{trace::kSweepSeed};
    int levelUps = 0;
    int learnings = 0;
    withStartedOracle(image(), [&](DamageOracle& oracle) {
        for (int draw = 0; draw < 100; ++draw) {
            const RecordScenario levelUp = trace::drawLevelUpScenario(levelRng);
            const RecordScenario learning = trace::drawLearningScenario(learningRng);
            oracle.trace(true);
            (void)oracle.levelUp(levelUp);
            (void)oracle.learnFromEsper(learning);
            oracle.trace(false);
            const std::vector<trace::TracedCall> calls = parse(oracle.takeTrace());
            ASSERT_EQ(calls.size(), 2u) << "draw " << draw;
            for (const trace::TracedCall& call : calls) {
                const bool isLevelUp = call.routine == "DoLevelUp";
                const RecordScenario& s = isLevelUp ? levelUp : learning;
                const std::uint32_t record = 0x7E1600 + 37 * static_cast<std::uint32_t>(s.recordIndex);
                const std::uint32_t spells = 0x7E1A6E + 54 * static_cast<std::uint32_t>(s.actor);
                for (const auto& [cell, value] : call.inputs) {
                    const bool inRecord = cell >= record && cell < record + 37;
                    const bool inSpells = cell >= spells && cell < spells + 54;
                    const bool battlesEnd = cell == 0x7E00F0 || cell == 0x7E00F1 || cell == 0x7E00F4 ||
                                            cell == 0x7E00F5 || cell == 0x7E00FB;
                    EXPECT_TRUE(inRecord || inSpells || battlesEnd)
                        << call.routine << " read $" << std::hex << cell << ", which the oracle did not write";
                }
                EXPECT_EQ(oracle.replay(call), "") << "draw " << draw << ": " << call.routine;
                ++(isLevelUp ? levelUps : learnings);
            }
        }
    });
    EXPECT_EQ(levelUps, 100);
    EXPECT_EQ(learnings, 100);
}

// --- the recorded win ----------------------------------------------------------------------------

// The first fight in Narshe won with Terra holding Terrato, at level 3 with the 208 experience level 4
// needs (tools/damage-trace/README.md has the run). Terrato's three spells are learned further with the
// battle's 0 magic points — read, stored unchanged, and counted as progress; Terra goes from level 3 to
// 4, her HP gain of 14 raised by 78/256 of itself to 18; Wedge and Vicks, records 14 and 15, go from
// level 1 to 2 with no esper. Every call replays against the port with no difference.
TEST_F(EspersOracleTest, TheRecordedEsperWinIsThePorts) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::filesystem::path file =
        std::filesystem::path{OSTINATO_DAMAGE_TRACE_RECORDINGS} / "esper-win.log";
    std::ifstream log{file};
    ASSERT_TRUE(log) << file.string() << " could not be read";
    const std::vector<trace::TracedCall> calls = trace::parseTrace(log);

    std::vector<const trace::TracedCall*> learning;
    std::vector<const trace::TracedCall*> levelUps;
    for (const trace::TracedCall& call : calls) {
        if (call.routine == "LearnGenjuMagic") {
            learning.push_back(&call);
        } else if (call.routine == "DoLevelUp") {
            levelUps.push_back(&call);
        }
    }
    ASSERT_EQ(learning.size(), 1u);
    ASSERT_EQ(levelUps.size(), 3u);

    // Terra's spell list starts at $1A6E; Quake, Quartr and W Wind are spells 21, 17 and 22.
    const trace::TracedCall& learned = *learning.front();
    EXPECT_EQ(learned.inputs.at(0x7E00FB), 0);
    for (const AttackId spell : {AttackId::QUAKE, AttackId::QUARTR, AttackId::W_WIND}) {
        const std::uint32_t cell = 0x7E1A6E + static_cast<std::uint32_t>(spell);
        EXPECT_EQ(learned.inputs.at(cell), learned.writes.at(cell)) << static_cast<int>(spell);
    }
    EXPECT_TRUE(learned.writes.contains(0x7E00F0));

    // Terra's record is the first: level $1608, maximum HP $160B.
    const auto terra = std::find_if(levelUps.begin(), levelUps.end(),
                                    [](const trace::TracedCall* call) { return call->inputs.contains(0x7E1608); });
    ASSERT_NE(terra, levelUps.end());
    EXPECT_EQ((*terra)->inputs.at(0x7E1608), 3);
    EXPECT_EQ((*terra)->writes.at(0x7E1608), 4);
    EXPECT_EQ((*terra)->inputs.at(0x7E161E), 4);  // Terrato, the fifth esper
    EXPECT_EQ((*terra)->writes.at(0x7E160B) - (*terra)->inputs.at(0x7E160B), 18);
    for (const std::uint32_t level : {0x7E180Eu, 0x7E1833u}) {  // records 14 and 15
        const auto other = std::find_if(levelUps.begin(), levelUps.end(),
                                        [&](const trace::TracedCall* call) { return call->inputs.contains(level); });
        ASSERT_NE(other, levelUps.end()) << std::hex << level;
        EXPECT_EQ((*other)->inputs.at(level), 1);
        EXPECT_EQ((*other)->writes.at(level), 2);
    }

    withStartedOracle(image(), [&](DamageOracle& oracle) {
        for (const trace::TracedCall& call : calls) {
            EXPECT_EQ(oracle.replay(call), "") << "frame " << call.frame << ": " << call.routine;
        }
    });
}

}  // namespace
}  // namespace ostinato::test
