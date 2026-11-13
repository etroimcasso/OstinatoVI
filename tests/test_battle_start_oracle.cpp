// A battle's start on the oracle (docs/contracts/battle_core.md § A battle's start): the battle
// program's entry is where the US cartridges hold it and an image without it is refused; the program is
// called whole, as the field calls it, from the first fight in Narshe as first-fight.inputs leaves the
// machine and from that fight's scenario written into a machine fresh from its reset code, and in both
// it opens, reaches the loop's first frame wait and returns; and a start scenario written reads back
// the same.

#include <gtest/gtest.h>

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

class BattleStartOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
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

    static std::vector<std::uint8_t>* image_;
    static std::vector<std::uint8_t>* firstFight_;
};

std::vector<std::uint8_t>* BattleStartOracleTest::image_ = nullptr;
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
        const StartResult result = fresh.battleStart(scenario);
        EXPECT_TRUE(result.reachedTheLoop);
        EXPECT_TRUE(result.returned);
        ASSERT_GE(result.calls.size(), 2u);
        EXPECT_EQ(result.calls[0].command, 0x00);
        EXPECT_EQ(result.calls[1].command, 0x01);
    });
}

}  // namespace
}  // namespace ostinato::test
