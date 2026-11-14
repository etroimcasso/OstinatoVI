// A battle's start on the oracle (docs/contracts/battle_core.md § A battle's start): the battle
// program's entry is where the US cartridges hold it and an image without it is refused; the program is
// called whole, as the field calls it, from the first fight in Narshe as first-fight.inputs leaves the
// machine and from that fight's scenario written into a machine fresh from its reset code, and in both
// it opens, reaches the loop's first frame wait and returns; and a start scenario written reads back
// the same.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "assets/cartridge.h"
#include "battle/battle_state.h"
#include "data/char_ai.h"
#include "data/formations.h"
#include "damage_oracle.h"
#include "machine_state.h"
#include "retropp/guest_escape.h"
#include "retropp/snes.h"
#include "retropp/vm.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

namespace fs = std::filesystem;
using trace::DamageOracle;
using trace::StartResult;

// The battle program's entry, as the field's jump to it lands (battle/battle_main.asm:39-40).
constexpr std::uint32_t kBattleEntry = 0xC20000;
// How far past first-fight.inputs's last line a run looks for its battle.
constexpr std::uint64_t kRunOutFrames = 600;
// The formations, and a scripted setup's reach byte that leaves monster slot 0 out.
constexpr int kFormationCount = 576;
constexpr std::uint8_t kSlotZeroOutOfReach = 0xFE;
// Places in a StartResult's cells: the play clock's frames, the monsters standing, and the monsters
// the AI leaves in reach.
constexpr std::size_t kPlayClockFramesCell = 0x021E;
constexpr std::size_t kMonstersStandingCell = 0x2F2F;
constexpr std::size_t kMonsterReachCell = 0x2F46;
// Cells for the gates: one the comparison holds whole (the party slot 0's field flags, $3AA0), one
// the start's fill writes that no field determines ($2F55), and one the comparison leaves out (which
// way the battlers face, $2F50).
constexpr std::uint32_t kWorkRam = 0x7E0000;
constexpr std::uint32_t kHeldCell = kWorkRam + 0x3AA0;
constexpr std::uint32_t kUnheldFillCell = kWorkRam + 0x2F55;
constexpr std::uint32_t kLeftOutCell = kWorkRam + 0x2F50;
// The byte after the last inventory slot's quantity, which the start reads sixteen bits wide.
constexpr std::uint32_t kQuantityHighByte = kWorkRam + 0x1A69;

class BattleStartOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        image_ = new std::vector<std::uint8_t>{vanillaRom()};
        content_ = new assets::IngestedContent{assets::ingestCartridge(image())};
    }
    static void TearDownTestSuite() {
        delete content_;
        content_ = nullptr;
        delete image_;
        image_ = nullptr;
        delete firstFight_;
        firstFight_ = nullptr;
    }

    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    // The machine on the tick before the first fight in Narshe's battle program is entered, played from
    // power-on by first-fight.inputs.
    static const std::vector<std::uint8_t>& firstFight() {
        if (firstFight_ == nullptr) {
            std::ifstream inputs{fs::path{OSTINATO_DAMAGE_TRACE_RECORDINGS} / "../../../tools/damage-trace/first-fight.inputs"};
            if (!inputs) {
                throw std::runtime_error("first-fight.inputs could not be read");
            }
            const std::vector<trace::InputStep> script = trace::readInputScript(inputs);
            const std::uint64_t lastFrame = script.back().frame + kRunOutFrames;
            firstFight_ = new std::vector<std::uint8_t>{};
            trace::runOnLoop([&] {
                retropp::Vm::SNES machine{retropp::VmConfig{}};
                machine.hostRom(image());
                bool entered = false;
                machine.registerEscapes(retropp::escapes(retropp::GuestEscape{
                    .key = "battle entry", .at = kBattleEntry, .handler = [&](retropp::Vm&, std::uint32_t) { entered = true; }}));
                machine.run(retropp::Vm::Advance::OnTick);
                std::vector<std::uint8_t> kept;
                for (std::uint64_t frame = 0; !entered && frame <= lastFrame; ++frame) {
                    machine.buttons(retropp::snes::Ports{.one = trace::buttonsAt(script, frame), .two = std::nullopt});
                    machine.advanceTick();
                    if (!entered) {
                        kept = machine.state();
                    }
                }
                machine.stop();
                if (!entered) {
                    throw std::runtime_error("first-fight.inputs reached no battle");
                }
                *firstFight_ = std::move(kept);
            });
        }
        return *firstFight_;
    }

    static const assets::IngestedContent& content() { return *content_; }

    static std::vector<std::uint8_t>* image_;
    static assets::IngestedContent* content_;
    static std::vector<std::uint8_t>* firstFight_;
};

std::vector<std::uint8_t>* BattleStartOracleTest::image_ = nullptr;
assets::IngestedContent* BattleStartOracleTest::content_ = nullptr;
std::vector<std::uint8_t>* BattleStartOracleTest::firstFight_ = nullptr;

// The battle program's entry for the field and BattleMain are where the US cartridges hold them, and
// an image without either is refused.
TEST_F(BattleStartOracleTest, TheBattleProgramsEntryIsWhereTheCartridgeHoldsIt) {
    const std::span<const std::uint8_t> cartridge = image();
    EXPECT_EQ(cartridge[0x020000], 0x4C);  // Battle_ext: jmp BattleMain
    EXPECT_EQ(cartridge[0x020001], 0x0C);
    EXPECT_EQ(cartridge[0x020002], 0x00);
    EXPECT_EQ(cartridge[0x02000C], 0x08);  // BattleMain: php; sep #$30
    EXPECT_EQ(cartridge[0x02000D], 0xE2);
    EXPECT_EQ(cartridge[0x02000E], 0x30);
    for (const std::size_t at : {std::size_t{0x020001}, std::size_t{0x02000D}}) {
        std::vector<std::uint8_t> other{cartridge.begin(), cartridge.end()};
        other[at] ^= 0xFF;
        trace::runOnLoop([&] {
            try {
                DamageOracle oracle{other};
                ADD_FAILURE() << "an image without the battle program's entry was taken";
            } catch (const std::runtime_error& refused) {
                const std::string why = refused.what();
                EXPECT_NE(why.find(at == 0x020001 ? "Battle_ext" : "BattleMain"), std::string::npos) << why;
            }
        });
    }
}

// The first fight's start, from the machine as the field left it: the battle opens (graphics command
// $00), reaches the loop's first frame wait ($01), and, ended there, closes ($09) and returns.
TEST_F(BattleStartOracleTest, TheFirstFightReachesTheLoopAndReturns) {
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useMonsterSprites(content().monsterSprites);
        ASSERT_TRUE(oracle.machine().restore(state));
        const StartResult result = oracle.battleStartAsItStands();
        EXPECT_TRUE(result.reachedTheLoop);
        EXPECT_TRUE(result.returned);
        ASSERT_GE(result.calls.size(), 3u);
        EXPECT_EQ(result.calls[0].command, 0x00);
        EXPECT_EQ(result.calls[1].command, 0x01);
        EXPECT_EQ(result.calls.back().command, 0x09);
    });
}

// The first fight's scenario, read off the machine as the field left it, written into another
// machine and read back: the same scenario.
TEST_F(BattleStartOracleTest, AStartScenarioWrittenReadsBackTheSame) {
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario read = oracle.readStartScenario();
        EXPECT_EQ(read.start.formation.raw, FormationRef::of(FormationId::GUARD_X2).raw);

        DamageOracle fresh{image()};
        fresh.start();
        fresh.writeStartScenario(read);
        EXPECT_EQ(trace::differences(read, fresh.readStartScenario()), "");
    });
}

// The first fight's scenario written into a machine fresh from its reset code, with nothing else of
// the game run: the battle program opens, reaches the loop's first frame wait and returns.
TEST_F(BattleStartOracleTest, AWrittenScenarioReachesTheLoopAndReturns) {
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();

        DamageOracle fresh{image()};
        fresh.start();
        fresh.useMonsterSprites(content().monsterSprites);
        const StartResult result = fresh.battleStart(scenario);
        EXPECT_TRUE(result.reachedTheLoop);
        EXPECT_TRUE(result.returned);
        ASSERT_GE(result.calls.size(), 2u);
        EXPECT_EQ(result.calls[0].command, 0x00);
        EXPECT_EQ(result.calls[1].command, 0x01);
    });
}

// The first fight's start, from the machine as the field left it, against the port's start over the
// same scenario: every cell a BattleState field determines the same, the opening's monsters standing
// and the play clock's frames among them, and nothing read beyond the battle's cells that is not
// listed.
TEST_F(BattleStartOracleTest, TheFirstFightsStartIsThePorts) {
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useMonsterSprites(content().monsterSprites);
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();
        const StartResult result = oracle.battleStartAsItStands();
        ASSERT_TRUE(result.reachedTheLoop);
        const battle::BattleState port = trace::portStart(scenario, content().text, content().monsterSprites);
        EXPECT_EQ(oracle.compareBattleState(port, scenario.cells, result.cells), "");
        EXPECT_EQ(port.monstersStanding.monsters.bits, result.cells[kMonstersStandingCell]) << "the monsters standing";
        EXPECT_EQ(port.playClockFrames, result.cells[kPlayClockFramesCell]) << "the play clock's frames";
        for (const trace::UnwrittenRead& read : result.readsBeyondTheBattlesCells) {
            ADD_FAILURE() << "read beyond the battle's cells: " << std::hex << read.at;
        }
    });
}

// The same start from the first fight's scenario written into a machine fresh from its reset code:
// the port's start again.
TEST_F(BattleStartOracleTest, TheFirstFightsWrittenScenarioStartsAsThePortDoes) {
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();

        DamageOracle fresh{image()};
        fresh.start();
        fresh.useMonsterSprites(content().monsterSprites);
        const StartResult result = fresh.battleStart(scenario);
        ASSERT_TRUE(result.reachedTheLoop);
        const battle::BattleState port = trace::portStart(scenario, content().text, content().monsterSprites);
        EXPECT_EQ(fresh.compareBattleState(port, scenario.cells, result.cells), "");
    });
}

// The first fight's start reads, before anything writes them, only cells left as the machine has
// them; and the quantity's high byte it reads at $1A69 reaches nothing: written $00 and $FF, the
// cells at the loop's first frame wait are the same.
TEST_F(BattleStartOracleTest, TheFirstFightsStartReadsNothingItWasNotHanded) {
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();

        std::optional<trace::BattleCells> first;
        for (const std::uint8_t quantityHighByte : {std::uint8_t{0x00}, std::uint8_t{0xFF}}) {
            DamageOracle fresh{image()};
            fresh.start();
            fresh.useMonsterSprites(content().monsterSprites);
            fresh.writeStartScenario(scenario);
            const std::array<std::uint8_t, 1> byte{quantityHighByte};
            fresh.machine().write(retropp::MemoryRegion{.at = kQuantityHighByte, .size = 1}, byte);
            const StartResult result = fresh.battleStartAsItStands();
            ASSERT_TRUE(result.reachedTheLoop);
            for (const trace::UnwrittenRead& read : result.unwrittenReads) {
                if (read.at != kQuantityHighByte) {
                    EXPECT_TRUE(trace::leftAsTheMachineHasIt(read.at)) << std::hex << read.at;
                }
            }
            if (first) {
                for (std::size_t cell = 0; cell < first->size(); ++cell) {
                    if (kWorkRam + cell != kQuantityHighByte) {
                        EXPECT_EQ((*first)[cell], result.cells[cell]) << "$7E:" << std::hex << cell;
                    }
                }
            } else {
                first = result.cells;
            }
        }
    });
}

// The two gates on the first fight's start, each on cells whose answer is known: the write-set gate
// names a cell the start's fill wrote that no field determines and passes one the comparison holds; the
// carried-read gate names a read of a cell the comparison left out and passes a read of one it holds.
TEST_F(BattleStartOracleTest, TheGatesNameWhatTheComparisonDoesNotHold) {
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useMonsterSprites(content().monsterSprites);
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();
        const StartResult result = oracle.battleStartAsItStands();
        ASSERT_TRUE(result.reachedTheLoop);
        ASSERT_TRUE(result.battleWrites.contains(kUnheldFillCell));
        ASSERT_TRUE(result.battleWrites.contains(kHeldCell));
        const battle::BattleState port = trace::portStart(scenario, content().text, content().monsterSprites);

        EXPECT_EQ(oracle.uncoveredWrites(port, scenario.cells, {kHeldCell}), "");
        EXPECT_EQ(oracle.uncoveredWrites(port, scenario.cells, {kUnheldFillCell}),
                  "$7E:2F55 written, bits FF not compared\n");
        // The fill's bits of $3205 are listed; its write is held whole between them.
        EXPECT_EQ(oracle.uncoveredWrites(port, scenario.cells, {kWorkRam + 0x3205}), "");

        EXPECT_EQ(oracle.carriedReads(port, scenario.cells, {trace::UnwrittenRead{.at = kHeldCell, .value = 0}}), "");
        EXPECT_EQ(oracle.carriedReads(port, scenario.cells,
                                      {trace::UnwrittenRead{.at = kLeftOutCell, .value = 0xFF},
                                       trace::UnwrittenRead{.at = kLeftOutCell, .value = 0xFF}}),
                  "$7E:2F50 read after the comparison, FF, not compared\n");
    });
}

// A formation whose scripted setup keeps monster slot 0 out of reach (its validMonsterTargets $FE),
// started from the first fight's scenario: the opening lays the setup's byte into $2F46 and the port
// starts with that monster out of the AI's reach.
TEST_F(BattleStartOracleTest, AScriptedSetupKeepsItsMonstersOutOfReach) {
    std::optional<FormationId> chosen;
    for (int id = 0; id < kFormationCount && !chosen; ++id) {
        const FormationAux& aux = getFormationAux(static_cast<FormationId>(id));
        if (aux.characterAiEnabled() &&
            charAiSetup(static_cast<CharAiId>(aux.characterAiIndex())).validMonsterTargets.bits == kSlotZeroOutOfReach) {
            chosen = static_cast<FormationId>(id);
        }
    }
    ASSERT_TRUE(chosen) << "no scripted setup keeps a monster out of reach";
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useMonsterSprites(content().monsterSprites);
        ASSERT_TRUE(oracle.machine().restore(state));
        trace::StartScenario scenario = oracle.readStartScenario();
        scenario.start.formation = FormationRef::of(*chosen);
        // Each character the setup seats has a record, taken from one outside the party played.
        game::Party& party = scenario.cells.party;
        for (const CharAiSlot& cast : charAiSetup(static_cast<CharAiId>(getFormationAux(*chosen).characterAiIndex())).slots) {
            if (cast.isEmpty()) {
                continue;
            }
            const CharacterPropId actor = cast.character.characterProperties();
            if (std::any_of(party.characters.begin(), party.characters.end(),
                            [&](const game::CharacterRecord& record) { return record.actor == actor; })) {
                continue;
            }
            for (std::size_t record = game::kCharacterRecords; record-- > 0;) {
                if (party.membership[record].party != party.currentParty) {
                    party.characters[record].actor = actor;
                    break;
                }
            }
        }
        const StartResult result = oracle.battleStart(scenario);
        ASSERT_TRUE(result.reachedTheLoop);
        ASSERT_EQ(result.cells[kMonsterReachCell], kSlotZeroOutOfReach);
        const battle::BattleState port = trace::portStart(scenario, content().text, content().monsterSprites);
        EXPECT_EQ(static_cast<std::uint8_t>(~port.aiUntargetable.monsters.bits), result.cells[kMonsterReachCell])
            << "formation " << static_cast<int>(*chosen);
        EXPECT_EQ(oracle.compareBattleState(port, scenario.cells, result.cells), "");
    });
}

}  // namespace
}  // namespace ostinato::test
