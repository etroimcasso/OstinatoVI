// The esper's spells learned and the level-up on the oracle (docs/contracts/battle_espers.md): both
// routines are where the US cartridges hold them, both calls return where the battle's end makes them,
// and an image without either is refused; every esper's level-up and every esper's spells, the caps,
// the stat limit and the learning edges, each worked out by hand from the original and also run on the
// cartridge's own routine (tools/damage-trace/damage_oracle.h), the port matching both; then 500 drawn
// records through each routine, every call the oracle makes traced and replayed against the port, and
// a won battle recorded on the cartridge replayed the same way.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "damage_oracle.h"
#include "data/esper_properties.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/esper_id.h"
#include "vanilla_rom.h"

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

class EspersOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    template <typename Body>
    static void withOracle(Body body) {
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            body(oracle);
        });
    }

    // The port's level-up over `scenario`, checked against the cartridge's own answer.
    static LevelUpResult checkedLevelUp(const RecordScenario& scenario) {
        LevelUpResult guest;
        withOracle([&](DamageOracle& oracle) { guest = oracle.levelUp(scenario); });
        const LevelUpResult port = trace::portLevelUp(scenario);
        EXPECT_EQ(trace::differences(port, guest), "") << trace::describe(scenario);
        return port;
    }

    // The port's spells learned over `scenario`, checked against the cartridge's own answer.
    static SpellLearningResult checkedLearning(const RecordScenario& scenario) {
        SpellLearningResult guest;
        withOracle([&](DamageOracle& oracle) { guest = oracle.learnFromEsper(scenario); });
        const SpellLearningResult port = trace::portLearnFromEsper(scenario);
        EXPECT_EQ(trace::differences(port, guest), "") << trace::describe(scenario);
        return port;
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* EspersOracleTest::image_ = nullptr;

// --- where the routines are ----------------------------------------------------------------------

TEST_F(EspersOracleTest, BothRoutinesAreWhereTheUsCartridgesHoldThem) {
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
    for (const std::uint32_t routine : {kLearnGenjuMagic, kDoLevelUp}) {
        trace::runOnLoop([&] {
            const std::span<const std::uint8_t> original = image();
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(routine)] ^= 0xFF;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << std::hex << routine;
        });
    }
}

// --- the level-up --------------------------------------------------------------------------------

// Record 3 at level 69 with 1,000 HP and 100 MP, every stat at 50, the HP boost 3 and the MP boost 1.
RecordScenario level69(std::optional<EsperId> esper) {
    RecordScenario s;
    s.recordIndex = 3;
    s.record.actor = CharacterPropId::SHADOW;
    s.record.level = 69;
    s.record.maxHp = 1000;
    s.record.maxMp = 100;
    s.record.vigor = 50;
    s.record.speed = 50;
    s.record.stamina = 50;
    s.record.magicPower = 50;
    s.record.esper = esper;
    s.hpBoostBits = 3;
    s.mpBoostBits = 1;
    return s;
}

TEST_F(EspersOracleTest, ALevelUpWithNoEsperGainsTheCurvesAndKeepsTheBoosts) {
    const LevelUpResult result = checkedLevelUp(level69(std::nullopt));
    EXPECT_EQ(result.level, 70);
    EXPECT_EQ(result.maxHp, 1162);
    EXPECT_EQ(result.maxMp, 108);
    EXPECT_EQ(result.hpBoostBits, 3);
    EXPECT_EQ(result.mpBoostBits, 1);
}

TEST_F(EspersOracleTest, EveryEspersLevelUpIsTheCartridges) {
    withOracle([&](DamageOracle& oracle) {
        for (const EsperPropertiesEntry& entry : kEsperProperties) {
            const RecordScenario s = level69(entry.id);
            EXPECT_EQ(trace::differences(trace::portLevelUp(s), oracle.levelUp(s)), "") << static_cast<int>(entry.id);
        }
    });
    // Two by hand: Terrato's +30% (162 + 49) and Bismark's +2 vigor.
    EXPECT_EQ(checkedLevelUp(level69(EsperId::TERRATO)).maxHp, 1211);
    EXPECT_EQ(checkedLevelUp(level69(EsperId::BISMARK)).vigor, 52);
}

TEST_F(EspersOracleTest, TheCapsAreTheCartridges) {
    RecordScenario s = level69(EsperId::BAHAMUT);
    s.record.maxHp = 9900;
    s.record.maxMp = 995;
    const LevelUpResult capped = checkedLevelUp(s);
    EXPECT_EQ(capped.maxHp, 9999);
    EXPECT_EQ(capped.maxMp, 999);
    EXPECT_EQ(capped.hpBoostBits, 3);
    s.record.maxHp = 9999;
    s.record.maxMp = 999;
    EXPECT_EQ(checkedLevelUp(s).maxHp, 9999);
}

TEST_F(EspersOracleTest, TheStatLimitIsTheCartridges) {
    RecordScenario s = level69(EsperId::BISMARK);  // +2 vigor
    s.record.vigor = 127;
    EXPECT_EQ(checkedLevelUp(s).vigor, 128);
    s.record.vigor = 128;
    EXPECT_EQ(checkedLevelUp(s).vigor, 128);
    s = level69(EsperId::RAMUH);  // +1 stamina
    s.record.stamina = 127;
    EXPECT_EQ(checkedLevelUp(s).stamina, 128);
    s.record.stamina = 128;
    EXPECT_EQ(checkedLevelUp(s).stamina, 128);
}

// The bonus adds within the stat's byte: 255 + 1 and 254 + 2 come round to 0, 255 + 2 to 1, and
// 254 + 1 is 255, held to 128.
TEST_F(EspersOracleTest, TheStatsByteWrapsAsTheCartridgesDoes) {
    RecordScenario s = level69(EsperId::RAMUH);  // +1 stamina
    s.record.stamina = 255;
    EXPECT_EQ(checkedLevelUp(s).stamina, 0);
    s.record.stamina = 254;
    EXPECT_EQ(checkedLevelUp(s).stamina, 128);
    s = level69(EsperId::BISMARK);  // +2 vigor
    s.record.vigor = 254;
    EXPECT_EQ(checkedLevelUp(s).vigor, 0);
    s.record.vigor = 255;
    EXPECT_EQ(checkedLevelUp(s).vigor, 1);
}

// --- the esper's spells --------------------------------------------------------------------------

// Locke (record 1) with `esper` after a battle of `magicPoints`, every spell unlearned.
RecordScenario lockeWith(EsperId esper, std::uint8_t magicPoints) {
    RecordScenario s;
    s.recordIndex = 1;
    s.actor = CharacterPropId::LOCKE;
    s.record.actor = CharacterPropId::LOCKE;
    s.record.esper = esper;
    s.magicPoints = magicPoints;
    return s;
}

std::uint8_t& percent(SpellProgress& progress, AttackId spell) {
    return progress.progress[static_cast<std::size_t>(spell)];
}

std::uint8_t percent(const SpellLearningResult& result, AttackId spell) {
    return result.progress[static_cast<std::size_t>(spell)];
}

TEST_F(EspersOracleTest, EveryEspersSpellsAreTheCartridges) {
    withOracle([&](DamageOracle& oracle) {
        for (const EsperPropertiesEntry& entry : kEsperProperties) {
            const RecordScenario s = lockeWith(entry.id, 10);
            EXPECT_EQ(trace::differences(trace::portLearnFromEsper(s), oracle.learnFromEsper(s)), "")
                << static_cast<int>(entry.id);
        }
    });
    // By hand: Ramuh with 10 magic points, Bolt at 10 × 10 = just learned, Bolt 2 at 20, Poison at 50.
    const SpellLearningResult ramuh = checkedLearning(lockeWith(EsperId::RAMUH, 10));
    EXPECT_EQ(percent(ramuh, AttackId::BOLT), SpellProgress::kSpellJustLearned);
    EXPECT_EQ(percent(ramuh, AttackId::BOLT_2), 20);
    EXPECT_EQ(percent(ramuh, AttackId::POISON), 50);
    EXPECT_TRUE(ramuh.progressed);
}

TEST_F(EspersOracleTest, TheLearningEdgesAreTheCartridges) {
    // Bolt learned, Bolt 2 at 50 (+ 2 × 25 = 100), Poison just learned already, with 25 magic points
    // as no battle gives — the rule takes any byte.
    RecordScenario s = lockeWith(EsperId::RAMUH, 25);
    percent(s.spells, AttackId::BOLT) = SpellProgress::kSpellLearned;
    percent(s.spells, AttackId::BOLT_2) = 50;
    percent(s.spells, AttackId::POISON) = SpellProgress::kSpellJustLearned;
    const SpellLearningResult edges = checkedLearning(s);
    EXPECT_EQ(percent(edges, AttackId::BOLT), SpellProgress::kSpellLearned);
    EXPECT_EQ(percent(edges, AttackId::BOLT_2), SpellProgress::kSpellJustLearned);
    EXPECT_EQ(percent(edges, AttackId::POISON), SpellProgress::kSpellJustLearned);
    EXPECT_TRUE(edges.progressed);

    // Starlet's Cure from 95 by 25 × 10: past a byte, just learned.
    RecordScenario past = lockeWith(EsperId::STARLET, 10);
    percent(past.spells, AttackId::CURE) = 95;
    EXPECT_EQ(percent(checkedLearning(past), AttackId::CURE), SpellProgress::kSpellJustLearned);

    // No magic points: nothing moves, and a spell stored still counts.
    RecordScenario none = lockeWith(EsperId::RAMUH, 0);
    percent(none.spells, AttackId::BOLT) = 40;
    const SpellLearningResult unchanged = checkedLearning(none);
    EXPECT_EQ(percent(unchanged, AttackId::BOLT), 40);
    EXPECT_TRUE(unchanged.progressed);

    // Starlet's Cure from 50 by 25 × 12 = 300, of which the low byte, 44, is added: 94.
    RecordScenario wide = lockeWith(EsperId::STARLET, 12);
    percent(wide.spells, AttackId::CURE) = 50;
    EXPECT_EQ(percent(checkedLearning(wide), AttackId::CURE), 94);

    // Every spell learned: nothing is stored.
    RecordScenario learned = lockeWith(EsperId::RAMUH, 10);
    for (const AttackId spell : {AttackId::BOLT, AttackId::BOLT_2, AttackId::POISON}) {
        percent(learned.spells, spell) = SpellProgress::kSpellLearned;
    }
    EXPECT_FALSE(checkedLearning(learned).progressed);
}

// --- the sweeps and the replay -------------------------------------------------------------------

constexpr int kSweepDraws = 500;

TEST_F(EspersOracleTest, EveryDrawnLevelUpIsTheCartridges) {
    std::mt19937 rng{trace::kSweepSeed};
    int capped = 0;
    int statLimited = 0;
    int wrapped = 0;
    withOracle([&](DamageOracle& oracle) {
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
    std::mt19937 rng{trace::kSweepSeed};
    int justLearned = 0;
    int productsPastAByte = 0;
    withOracle([&](DamageOracle& oracle) {
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

// The replay tells the esper by the spells it reads, so no two espers may teach the same spells.
TEST(EspersReplay, NoTwoEspersTeachTheSameSpells) {
    std::set<std::set<AttackId>> seen;
    for (const EsperPropertiesEntry& entry : kEsperProperties) {
        std::set<AttackId> spells;
        for (const EsperSpell& slot : entry.record.spells) {
            if (slot.spell != AttackId::NONE) {
                spells.insert(slot.spell);
            }
        }
        EXPECT_TRUE(seen.insert(spells).second) << static_cast<int>(entry.id);
    }
}

// Every call the oracle makes, traced: it reads nothing but the cells the oracle wrote — the record,
// the character's spells and the battle's-end cells — and it replays against the port with no difference.
TEST_F(EspersOracleTest, EveryTracedCallReadsOnlyWhatWasWrittenAndReplays) {
    std::mt19937 levelRng{trace::kSweepSeed};
    std::mt19937 learningRng{trace::kSweepSeed};
    int levelUps = 0;
    int learnings = 0;
    withOracle([&](DamageOracle& oracle) {
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

    withOracle([&](DamageOracle& oracle) {
        for (const trace::TracedCall& call : calls) {
            EXPECT_EQ(oracle.replay(call), "") << "frame " << call.frame << ": " << call.routine;
        }
    });
}

}  // namespace
}  // namespace ostinato::test
