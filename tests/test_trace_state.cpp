// The trace tool's own files: a machine's state with its cartridge's name written and read back and
// refused when it is not one, a state taken from a running cartridge and carried on from on a fresh
// machine through the file, an input script read and held frame by frame and refused by line, the
// trace on a machine running that script logging each attack's own context, and the recording of the
// first fight holding a status being set.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "assets/cartridge.h"
#include "damage_oracle.h"
#include "machine_state.h"
#include "ostinato/game_version.h"
#include "retropp/memory_region.h"
#include "retropp/raster_content.h"
#include "retropp/snes.h"
#include "retropp/vm.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

namespace fs = std::filesystem;

using retropp::snes::Buttons;
using trace::InputStep;
using trace::MachineState;

// A directory of its own for one test, removed when the test ends.
class TempDirectory {
public:
    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("ostinato-trace-state-" + std::to_string(stamp));
        fs::create_directories(path_);
    }
    ~TempDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void writeText(const fs::path& file, const std::string& text) {
    std::ofstream out{file, std::ios::binary};
    out << text;
}

// The message a call throws, or nothing when it does not.
template <typename Call>
std::string refusal(Call call) {
    try {
        call();
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return "";
}

std::string scriptRefusal(const std::string& text) {
    std::istringstream script{text};
    return refusal([&] { (void)trace::readInputScript(script); });
}

std::vector<InputStep> scriptOf(const std::string& text) {
    std::istringstream script{text};
    return trace::readInputScript(script);
}

bool holdsNothing(const Buttons& buttons) {
    const Buttons nothing;
    return static_cast<retropp::GuestButtons>(buttons).held == static_cast<retropp::GuestButtons>(nothing).held;
}

// ─── the state file ─────────────────────────────────────────────────────────────────────────────

TEST(TraceStateFile, WhatIsWrittenReadsBack) {
    const TempDirectory directory;
    const fs::path file = directory.path() / "battle.state";
    // Every byte value, line ends among them, so nothing in the engine's bytes can be taken for text.
    std::vector<std::uint8_t> bytes(256);
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        bytes[at] = static_cast<std::uint8_t>(at);
    }
    trace::writeMachineState(file, GameVersion::US_1_1, bytes);
    const MachineState read = trace::readMachineState(file);
    EXPECT_EQ(read.cartridge, GameVersion::US_1_1);
    EXPECT_EQ(read.bytes, bytes);
}

TEST(TraceStateFile, TheFileOpensWithItsTwoLines) {
    const TempDirectory directory;
    const fs::path file = directory.path() / "battle.state";
    const std::vector<std::uint8_t> bytes{0x41};
    trace::writeMachineState(file, GameVersion::US_1_0, bytes);
    std::ifstream in{file, std::ios::binary};
    const std::string whole{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    EXPECT_EQ(whole, "OstinatoVI machine state 1\ncartridge US_1_0\nA");
}

TEST(TraceStateFile, WritingAgainReplacesTheFile) {
    const TempDirectory directory;
    const fs::path file = directory.path() / "battle.state";
    const std::vector<std::uint8_t> first{1, 2, 3, 4};
    const std::vector<std::uint8_t> second{5};
    trace::writeMachineState(file, GameVersion::US_1_0, first);
    trace::writeMachineState(file, GameVersion::US_1_1, second);
    const MachineState read = trace::readMachineState(file);
    EXPECT_EQ(read.cartridge, GameVersion::US_1_1);
    EXPECT_EQ(read.bytes, second);
}

TEST(TraceStateFile, AStateBelongsToItsOwnRevisionAlone) {
    const TempDirectory directory;
    const fs::path file = directory.path() / "battle.state";
    const std::vector<std::uint8_t> bytes{1, 2, 3};
    trace::writeMachineState(file, GameVersion::US_1_0, bytes);
    const MachineState read = trace::readMachineState(file);
    EXPECT_EQ(read.cartridge, GameVersion::US_1_0);
    EXPECT_TRUE(trace::stateIsOf(read, GameVersion::US_1_0));
    EXPECT_FALSE(trace::stateIsOf(read, GameVersion::US_1_1));
    EXPECT_FALSE(trace::stateIsOf(read, GameVersion::JP_1_0));
}

TEST(TraceStateFile, EveryRevisionHasItsName) {
    EXPECT_EQ(trace::cartridgeName(GameVersion::JP_1_0), "JP_1_0");
    EXPECT_EQ(trace::cartridgeName(GameVersion::US_1_0), "US_1_0");
    EXPECT_EQ(trace::cartridgeName(GameVersion::US_1_1), "US_1_1");
}

TEST(TraceStateFile, AFileThatIsNotAStateIsRefusedNamingIt) {
    const TempDirectory directory;
    const fs::path file = directory.path() / "not-a-state.state";
    writeText(file, "something else entirely\n");
    const std::string message = refusal([&] { (void)trace::readMachineState(file); });
    EXPECT_NE(message.find(file.string()), std::string::npos) << message;
    EXPECT_NE(message.find("is not a machine state"), std::string::npos) << message;
}

TEST(TraceStateFile, AFileCutShortAfterItsLinesIsRefusedNamingIt) {
    const TempDirectory directory;
    const fs::path file = directory.path() / "cut-short.state";
    writeText(file, "OstinatoVI machine state 1\ncartridge US_1_1\n");
    const std::string message = refusal([&] { (void)trace::readMachineState(file); });
    EXPECT_NE(message.find(file.string()), std::string::npos) << message;
    EXPECT_NE(message.find("holds no state"), std::string::npos) << message;
}

TEST(TraceStateFile, ARevisionThatDoesNotExistIsRefusedNamingIt) {
    const TempDirectory directory;
    const fs::path file = directory.path() / "other.state";
    writeText(file, "OstinatoVI machine state 1\ncartridge US_2_0\nbytes");
    const std::string message = refusal([&] { (void)trace::readMachineState(file); });
    EXPECT_NE(message.find(file.string()), std::string::npos) << message;
    EXPECT_NE(message.find("US_2_0"), std::string::npos) << message;
}

TEST(TraceStateFile, AFileThatCannotBeReadOrWrittenIsNamed) {
    const TempDirectory directory;
    const fs::path missing = directory.path() / "no-such-directory" / "battle.state";
    const std::vector<std::uint8_t> bytes{1};
    const std::string read = refusal([&] { (void)trace::readMachineState(missing); });
    EXPECT_NE(read.find(missing.string()), std::string::npos) << read;
    const std::string written = refusal([&] { trace::writeMachineState(missing, GameVersion::US_1_1, bytes); });
    EXPECT_NE(written.find(missing.string()), std::string::npos) << written;
}

// ─── a state taken from the running cartridge ───────────────────────────────────────────────────

struct WorkRam {
    retropp::MemoryRegion cells;
};

retropp::RegionMapId<WorkRam> declareWorkRam(retropp::Vm& machine) {
    return machine.registerRegions(retropp::regions(
        retropp::region(&WorkRam::cells, retropp::MemoryRegion{.at = 0x7E0000, .size = 0x10000}, "work RAM")));
}

std::vector<std::uint8_t> pictureOf(const retropp::Vm& machine) {
    const retropp::RasterContent picture = machine.video();
    return std::vector<std::uint8_t>{picture.pixels.begin(), picture.pixels.end()};
}

// A machine running from power-on and one restored from its state through the file, both run on past
// the state's moment: 240 ticks on they stand on the same memory and picture, and one tick short of
// that they do not, so the comparison sees a tick.
TEST(TraceStateMachine, AStateThroughTheFileCarriesOnWhereTheMachineStood) {
    const std::vector<std::uint8_t> rom = vanillaRom();
    const std::span<const std::uint8_t> image = assets::stripCopierHeader(rom);
    const TempDirectory directory;
    const fs::path file = directory.path() / "battle.state";

    trace::runOnLoop([&] {
        retropp::Vm::SNES source{retropp::VmConfig{.video = true}};
        source.hostRom(image);
        const retropp::RegionMapId<WorkRam> sourceRam = declareWorkRam(source);
        source.run(retropp::Vm::Advance::OnTick);
        for (int tick = 0; tick < 300; ++tick) {
            source.advanceTick();
        }
        trace::writeMachineState(file, GameVersion::US_1_1, source.state());

        const MachineState saved = trace::readMachineState(file);
        retropp::Vm::SNES restored{retropp::VmConfig{.video = true}};
        restored.hostRom(image);
        const retropp::RegionMapId<WorkRam> restoredRam = declareWorkRam(restored);
        ASSERT_TRUE(restored.restore(saved.bytes));
        restored.run(retropp::Vm::Advance::OnTick);

        for (int tick = 0; tick < 239; ++tick) {
            source.advanceTick();
        }
        for (int tick = 0; tick < 240; ++tick) {
            restored.advanceTick();
        }
        const std::vector<std::uint8_t> oneTickShort = source.read(sourceRam, &WorkRam::cells);
        const std::vector<std::uint8_t> restoredCells = restored.read(restoredRam, &WorkRam::cells);
        EXPECT_NE(oneTickShort, restoredCells);

        source.advanceTick();
        EXPECT_EQ(source.read(sourceRam, &WorkRam::cells), restoredCells);
        EXPECT_EQ(pictureOf(source), pictureOf(restored));

        source.stop();
        restored.stop();
    });
}

// ─── the input script ───────────────────────────────────────────────────────────────────────────

TEST(TraceInputScript, FramesAndButtonsAreRead) {
    const std::vector<InputStep> script = scriptOf("10 A\n20 Up+Start\n30 -\n");
    ASSERT_EQ(script.size(), 3U);
    EXPECT_EQ(script[0].frame, 10U);
    EXPECT_TRUE(script[0].buttons.a);
    EXPECT_FALSE(script[0].buttons.b);
    EXPECT_EQ(script[1].frame, 20U);
    EXPECT_TRUE(script[1].buttons.up);
    EXPECT_TRUE(script[1].buttons.start);
    EXPECT_FALSE(script[1].buttons.a);
    EXPECT_EQ(script[2].frame, 30U);
    EXPECT_TRUE(holdsNothing(script[2].buttons));
}

TEST(TraceInputScript, EveryButtonHasItsName) {
    const std::vector<InputStep> script =
        scriptOf("0 A\n1 B\n2 X\n3 Y\n4 L\n5 R\n6 Start\n7 Select\n8 Up\n9 Down\n10 Left\n11 Right\n");
    ASSERT_EQ(script.size(), 12U);
    EXPECT_TRUE(script[0].buttons.a);
    EXPECT_TRUE(script[1].buttons.b);
    EXPECT_TRUE(script[2].buttons.x);
    EXPECT_TRUE(script[3].buttons.y);
    EXPECT_TRUE(script[4].buttons.l);
    EXPECT_TRUE(script[5].buttons.r);
    EXPECT_TRUE(script[6].buttons.start);
    EXPECT_TRUE(script[7].buttons.select);
    EXPECT_TRUE(script[8].buttons.up);
    EXPECT_TRUE(script[9].buttons.down);
    EXPECT_TRUE(script[10].buttons.left);
    EXPECT_TRUE(script[11].buttons.right);
}

TEST(TraceInputScript, CommentsAndBlankLinesArePassedOver) {
    const std::vector<InputStep> script = scriptOf("# the title screen\n\n   \n40 Start  # past it\n# done\n");
    ASSERT_EQ(script.size(), 1U);
    EXPECT_EQ(script[0].frame, 40U);
    EXPECT_TRUE(script[0].buttons.start);
}

TEST(TraceInputScript, ALineIsHeldUntilTheNext) {
    const std::vector<InputStep> script = scriptOf("10 A\n20 B\n30 -\n");
    EXPECT_TRUE(trace::buttonsAt(script, 10).a);
    EXPECT_TRUE(trace::buttonsAt(script, 19).a);
    EXPECT_FALSE(trace::buttonsAt(script, 20).a);
    EXPECT_TRUE(trace::buttonsAt(script, 20).b);
    EXPECT_TRUE(trace::buttonsAt(script, 29).b);
    EXPECT_TRUE(holdsNothing(trace::buttonsAt(script, 30)));
}

TEST(TraceInputScript, NothingIsHeldBeforeTheFirstLineAndTheLastLineStays) {
    const std::vector<InputStep> script = scriptOf("10 A\n20 Up\n");
    EXPECT_TRUE(holdsNothing(trace::buttonsAt(script, 0)));
    EXPECT_TRUE(holdsNothing(trace::buttonsAt(script, 9)));
    EXPECT_TRUE(trace::buttonsAt(script, 20000).up);
    EXPECT_TRUE(holdsNothing(trace::buttonsAt({}, 5)));
}

TEST(TraceInputScript, ANameThatIsNotAButtonIsRefusedNamingTheLine) {
    const std::string message = scriptRefusal("10 A\n20 Jump\n");
    EXPECT_NE(message.find("line 2"), std::string::npos) << message;
    EXPECT_NE(message.find("Jump"), std::string::npos) << message;
    EXPECT_NE(scriptRefusal("10 A+\n").find("line 1"), std::string::npos);
}

TEST(TraceInputScript, AFrameThatDoesNotIncreaseIsRefusedNamingTheLine) {
    EXPECT_NE(scriptRefusal("10 A\n10 B\n").find("line 2"), std::string::npos);
    EXPECT_NE(scriptRefusal("10 A\n\n# later\n9 B\n").find("line 4"), std::string::npos);
}

TEST(TraceInputScript, ALineOfAnyOtherShapeIsRefusedNamingTheLine) {
    EXPECT_NE(scriptRefusal("10\n").find("line 1"), std::string::npos);
    EXPECT_NE(scriptRefusal("10 A B\n").find("line 1"), std::string::npos);
    EXPECT_NE(scriptRefusal("A 10\n").find("line 1"), std::string::npos);
    EXPECT_NE(scriptRefusal("-5 A\n").find("line 1"), std::string::npos);
}

// ─── the running machine ────────────────────────────────────────────────────────────────────────

// The script run from power-on with the trace on the running machine, as far as the guards' first
// attack: the script reaches the fight, each call's context is the attack's own though the game wrote
// it during the tick the call ran on, and every call replays against the port.
TEST(TraceRunningMachine, TheScriptReachesTheFightAndTheContextIsTheAttacksOwn) {
    constexpr std::uint64_t kFirstAttackDone = 10216 + 320;
    constexpr std::uint8_t kFirstGuardToAttack = 7;

    const std::vector<std::uint8_t> rom = vanillaRom();
    const std::span<const std::uint8_t> image = assets::stripCopierHeader(rom);
    std::ifstream inputs{fs::path{OSTINATO_DAMAGE_TRACE_RECORDINGS} / "../../../tools/damage-trace/first-fight.inputs"};
    ASSERT_TRUE(inputs) << "first-fight.inputs could not be read";
    const std::vector<InputStep> script = trace::readInputScript(inputs);

    trace::runOnLoop([&] {
        std::stringstream log;
        {
            retropp::Vm::SNES machine{retropp::VmConfig{}};
            machine.hostRom(image);
            std::uint64_t frame = 0;
            const trace::DamageTrace traced{machine, [&](const std::string& line) { log << line << '\n'; },
                                            [&] { return frame; }};
            machine.run(retropp::Vm::Advance::OnTick);
            for (; frame < kFirstAttackDone; ++frame) {
                machine.buttons(retropp::snes::Ports{.one = trace::buttonsAt(script, frame), .two = std::nullopt});
                machine.advanceTick();
            }
            machine.stop();
        }

        const std::vector<trace::TracedCall> calls = trace::parseTrace(log);
        const auto target = std::find_if(calls.begin(), calls.end(), [](const trace::TracedCall& call) {
            return call.routine == "CalcTargetDmg";
        });
        ASSERT_NE(target, calls.end());
        EXPECT_EQ(target->params[1], kFirstGuardToAttack);

        trace::DamageOracle oracle{image};
        oracle.start();
        for (const trace::TracedCall& call : calls) {
            EXPECT_EQ(oracle.replay(call), "") << "frame " << call.frame << ": " << call.routine;
        }
    });
}

// ─── the recording ──────────────────────────────────────────────────────────────────────────────

// The attack's statuses on one target go to the cells of what is waiting to be set, two for each
// battler at $3DD4 and two at $3DE8.
constexpr std::uint32_t kStatusesToSetFirst = 0x7E3DD4;
constexpr std::uint32_t kStatusesToSetEnd = 0x7E3DFC;

// The played fight set a status: at least one call of the attack's statuses on one target wrote a
// status waiting to be set. A missing recording fails naming the file.
TEST(TraceRecording, TheFirstFightSetsAStatus) {
    const fs::path file = fs::path{OSTINATO_DAMAGE_TRACE_RECORDINGS} / "first-fight.log";
    std::ifstream log{file};
    ASSERT_TRUE(log) << file.string() << " could not be read";
    int statusCalls = 0;
    int settingCalls = 0;
    for (const trace::TracedCall& call : trace::parseTrace(log)) {
        if (call.routine != "MagicStatusEffect") {
            continue;
        }
        ++statusCalls;
        const bool setsOne = std::any_of(call.writes.begin(), call.writes.end(), [](const auto& write) {
            return write.first >= kStatusesToSetFirst && write.first < kStatusesToSetEnd && write.second != 0;
        });
        settingCalls += setsOne ? 1 : 0;
    }
    EXPECT_GT(settingCalls, 0) << statusCalls << " calls of MagicStatusEffect in " << file.string();
    RecordProperty("status calls", statusCalls);
    RecordProperty("calls setting a status", settingCalls);
}

}  // namespace
}  // namespace ostinato::test
