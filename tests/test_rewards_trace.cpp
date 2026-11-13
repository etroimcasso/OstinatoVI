// A battle's end, traced (tools/damage-trace/damage_oracle): the four steps of a win on the party's
// records followed call by call, the win, the loss and the wrap-up followed whole as what their cells
// hold on entry and on return, with every battle message shown. Their return sites and ShowMsg's call
// are where the US cartridges make them; the trace's lines read back as written and a line that is not
// well formed is refused; every call and every ending the oracle makes, traced, replays against the
// port with no difference; and two recordings of the first fight in Narshe, won and lost on the
// cartridge, replay the same way and hold what the battle showed.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/battle_script.h"
#include "damage_oracle.h"
#include "ostinato/attack_id.h"
#include "ostinato/item_id.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using trace::DamageOracle;
using trace::EndingScenario;
using trace::MessageShown;
using trace::RewardScenario;
using trace::TracedCall;
using trace::TracedEnding;

constexpr int kDraws = 100;

class RewardsTraceTest : public ::testing::Test {
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

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* RewardsTraceTest::image_ = nullptr;

std::string joined(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line + "\n";
    }
    return text;
}

std::vector<TracedCall> callsIn(const std::string& text) {
    std::istringstream in{text};
    return trace::parseTrace(in);
}

std::vector<TracedEnding> endingsIn(const std::string& text) {
    std::istringstream in{text};
    return trace::parseEndings(in);
}

std::string recording(const char* name) {
    const std::filesystem::path file = std::filesystem::path{OSTINATO_DAMAGE_TRACE_RECORDINGS} / name;
    std::ifstream in{file};
    EXPECT_TRUE(in) << file.string() << " could not be read";
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::vector<MessageShown> shown(const TracedEnding& ending) {
    std::vector<MessageShown> messages;
    for (const trace::LoggedMessage& logged : ending.messages) {
        messages.push_back(trace::messageShownBy(logged));
    }
    return messages;
}

const TracedEnding& endingNamed(const std::vector<TracedEnding>& endings, const std::string& routine) {
    const auto found = std::find_if(endings.begin(), endings.end(),
                                    [&](const TracedEnding& ending) { return ending.routine == routine; });
    if (found == endings.end()) {
        throw std::runtime_error("no " + routine + " in the trace");
    }
    return *found;
}

// --- where the trace stops ---------------------------------------------------------------------

TEST_F(RewardsTraceTest, TheEndingsReturnWhereTheirCallersCallThem) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const auto callBefore = [&](std::uint32_t at) {
            return oracle.machine().read(retropp::MemoryRegion{.at = at - 3, .size = 3});
        };
        const std::vector<std::uint8_t> winBattle{0x20, 0x57, 0x5D};
        const std::vector<std::uint8_t> loseBattle{0x20, 0xCA, 0x5F};
        const std::vector<std::uint8_t> wrapUp{0x20, 0x03, 0x49};
        for (const trace::SnapshotRoutine& routine : trace::kSnapshotRoutines) {
            const std::vector<std::uint8_t>& call = routine.entry == 0xC25D57   ? winBattle
                                                    : routine.entry == 0xC25FCA ? loseBattle
                                                                                : wrapUp;
            for (const std::uint32_t site : routine.returns) {
                if (site != 0) {
                    EXPECT_EQ(callBefore(site), call) << routine.name << " returning to " << std::hex << site;
                }
            }
        }
        // ShowMsg stores the message ($2D6F) and calls the graphics program (jsr ExecBtlGfx, $C2:6411).
        EXPECT_EQ(oracle.machine().read(retropp::MemoryRegion{.at = 0xC25FDB, .size = 3}),
                  (std::vector<std::uint8_t>{0x8D, 0x6F, 0x2D}));
        EXPECT_EQ(oracle.machine().read(retropp::MemoryRegion{.at = trace::kShowMessageCall, .size = 3}),
                  (std::vector<std::uint8_t>{0x20, 0x11, 0x64}));
    });
}

// --- the lines ---------------------------------------------------------------------------------

TEST(RewardsTraceLines, AnEndingAndItsMessagesReadBack) {
    const std::string text =
        "f10 snapshot WinBattle enter $C2:5D57\n"
        "f10 snapshot WinBattle $7E:00B0 0102\n"
        "f11 message 27 000020 000000 000000 000060\n"
        "f12 message 2E 000020 000002 000000 000060\n"
        "f13 snapshot WinBattle return $C2:488F\n"
        "f13 snapshot WinBattle $7E:00B0 0304\n";
    // The return closes on the snapshot's last range, so the ending above is still open.
    EXPECT_TRUE(endingsIn(text).empty());

    std::string whole = text;
    whole += "f13 snapshot WinBattle $7E:64DA 0506\n";
    const std::vector<TracedEnding> endings = endingsIn(whole);
    ASSERT_EQ(endings.size(), 1u);
    const TracedEnding& win = endings.front();
    EXPECT_EQ(win.routine, "WinBattle");
    EXPECT_EQ(win.frame, 10u);
    EXPECT_EQ(win.returnedTo, 0xC2488Fu);
    EXPECT_EQ(win.entry.at(0x7E00B1), 0x02);
    EXPECT_EQ(win.left.at(0x7E00B1), 0x04);
    EXPECT_EQ(win.left.at(0x7E64DB), 0x06);
    EXPECT_FALSE(win.handedMessage.has_value());
    const std::vector<MessageShown> messages = shown(win);
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0], (MessageShown{.message = battle::kMessageGotExperience, .first = 32}));
    EXPECT_EQ(messages[1], (MessageShown{.message = battle::kMessageGainedALevel, .second = 2}));
}

TEST(RewardsTraceLines, ALossCarriesTheMessageItsCallerHandedIt) {
    const std::string text =
        "f1 snapshot LoseBattle enter $C2:5FCA\n"
        "f1 snapshot LoseBattle handed --\n"
        "f2 snapshot LoseBattle return $C2:4831\n"
        "f2 snapshot LoseBattle $7E:64DA 0000\n";
    const std::vector<TracedEnding> endings = endingsIn(text);
    ASSERT_EQ(endings.size(), 1u);
    ASSERT_TRUE(endings.front().handedMessage.has_value());
    EXPECT_FALSE(endings.front().handedMessage->has_value());
}

TEST(RewardsTraceLines, AStepCarriesItsAbilitiesAndMessages) {
    const std::vector<TracedCall> calls = callsIn(
        "f5 enter LearnAbilities $C2:61B6\n"
        "f5 params 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 hits FF rules 00 previous --\n"
        "f5 abilities actor 02 level 07\n"
        "f5 message 42 000000 000000 000000 000000\n"
        "f6 return LearnAbilities $C2:60BF\n");
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].abilitiesActor, 2);
    EXPECT_EQ(calls[0].abilitiesLevel, 7);
    ASSERT_EQ(calls[0].messages.size(), 1u);
    EXPECT_EQ(trace::messageShownBy(calls[0].messages[0]),
              (MessageShown{.message = battle::kMessageMasteredATechnique}));
}

TEST(RewardsTraceLines, ALineThatIsNotWellFormedIsRefused) {
    EXPECT_THROW((void)endingsIn("f1 snapshot WinBattle enter $C2:5D57\nf1 snapshot WinBattle $7E:00B0 0G\n"),
                 std::runtime_error);
    EXPECT_THROW((void)endingsIn("f1 message 27 0020 000000 000000 000000\n"), std::runtime_error);
    EXPECT_THROW((void)callsIn("f1 enter AddExp $C2:6235\nf1 abilities actor 02 lvl 07\n"), std::runtime_error);
}

// --- the oracle's own calls, traced and replayed -------------------------------------------------

// Every step the oracle runs, traced: each replays against the port with no difference. The level
// sweep runs the cartridge's own level loop, so the abilities it reaches are traced with the actor and
// level it read from the record.
TEST_F(RewardsTraceTest, EveryTracedStepReplays) {
    std::map<std::string, int> replayed;
    withOracle([&](DamageOracle& oracle) {
        std::mt19937 experienceRng{trace::kSweepSeed};
        std::mt19937 equipmentRng{trace::kSweepSeed};
        std::mt19937 levelRng{trace::kSweepSeed};
        std::mt19937 rageRng{trace::kSweepSeed};
        for (int draw = 0; draw < kDraws; ++draw) {
            oracle.trace(true);
            (void)oracle.addExperience(trace::drawExperienceScenario(experienceRng));
            (void)oracle.learnFromEquipment(trace::drawEquipmentScenario(equipmentRng));
            (void)oracle.levelUps(trace::drawLevelScenario(levelRng));
            (void)oracle.learnRages(trace::drawRageScenario(rageRng));
            oracle.trace(false);
            for (const TracedCall& call : callsIn(joined(oracle.takeTrace()))) {
                EXPECT_EQ(oracle.replay(call), "") << "draw " << draw << ": " << call.routine;
                ++replayed[call.routine];
            }
        }
    });
    EXPECT_EQ(replayed["AddExp"], kDraws);
    EXPECT_EQ(replayed["LearnItemMagic"], kDraws);
    EXPECT_EQ(replayed["LearnRage"], kDraws);
    EXPECT_GT(replayed["LearnAbilities"], 0);
    EXPECT_EQ(replayed["LearnAbilities"], replayed["DoLevelUp"]);
}

// Every ending the oracle plays, traced: the wrap-up, the win (with the wrap-up and the steps inside
// it) and the loss, each with the message its caller hands it — each replays with no difference.
TEST_F(RewardsTraceTest, EveryTracedEndingReplays) {
    std::map<std::string, int> replayed;
    int messages = 0;
    withOracle([&](DamageOracle& oracle) {
        std::mt19937 endingRng{trace::kSweepSeed};
        std::mt19937 winRng{trace::kSweepSeed};
        for (int draw = 0; draw < kDraws; ++draw) {
            const EndingScenario ending = trace::drawEndingScenario(endingRng);
            const EndingScenario won = trace::drawWinScenario(winRng);
            oracle.trace(true);
            (void)oracle.wrapUp(ending);
            (void)oracle.win(won);
            (void)oracle.loss(won, won.state.colosseum ? std::nullopt
                                                       : std::optional<std::uint8_t>{battle::kMessageAnnihilated});
            oracle.trace(false);
            const std::string text = joined(oracle.takeTrace());
            for (const TracedEnding& traced : endingsIn(text)) {
                EXPECT_EQ(oracle.replayEnding(traced), "") << "draw " << draw << ": " << traced.routine;
                ++replayed[traced.routine];
                messages += static_cast<int>(traced.messages.size());
            }
            for (const TracedCall& call : callsIn(text)) {
                EXPECT_EQ(oracle.replay(call), "") << "draw " << draw << ": " << call.routine;
                ++replayed[call.routine];
            }
        }
    });
    EXPECT_EQ(replayed["WinBattle"], kDraws);
    EXPECT_EQ(replayed["LoseBattle"], kDraws);
    EXPECT_EQ(replayed["_c24903"], 3 * kDraws);  // its own, and inside the win and the loss
    EXPECT_GT(replayed["AddExp"], 0);
    EXPECT_GT(messages, 0);
}

// --- the recordings ------------------------------------------------------------------------------

// The first fight in Narshe won with Terra at level 5, 6 short of the 672 experience level 6 needs,
// and the battle's random-number position set to 2 as the win begins (tools/damage-trace/README.md
// has the run): the share of 32 takes all three party members up a level, Terra's teaching her Antdot,
// the first fallen guard's draw is rare and drops a Tonic, and the party gets 96 GP.
TEST_F(RewardsTraceTest, TheRecordedWinIsThePorts) {
    const std::string text = recording("rewards-win.log");
    const std::vector<TracedEnding> endings = endingsIn(text);
    const std::vector<TracedCall> calls = callsIn(text);
    ASSERT_EQ(endings.size(), 2u);
    const TracedEnding& win = endingNamed(endings, "WinBattle");
    (void)endingNamed(endings, "_c24903");
    EXPECT_EQ(win.entry.at(0x7E00BE), 2);
    EXPECT_EQ(win.entry.at(0x7E1608), 5);
    EXPECT_EQ(win.left.at(0x7E1608), 6);
    EXPECT_EQ(shown(win), (std::vector<MessageShown>{
                              {.message = battle::kMessageGotExperience, .first = 32},
                              {.message = battle::kMessageGainedALevel, .second = 2},
                              {.message = battle::kMessageGainedALevel, .second = 1},
                              {.message = battle::kMessageGainedALevel, .second = 0},
                              {.message = battle::kMessageLearnedSpell,
                               .first = static_cast<std::uint32_t>(AttackId::ANTDOT),
                               .second = 0},
                              {.message = battle::kMessageGotItem, .first = static_cast<std::uint32_t>(ItemId::TONIC)},
                              {.message = battle::kMessageGotGp, .second = 96},
                          }));

    std::map<std::string, int> steps;
    for (const TracedCall& call : calls) {
        ++steps[call.routine];
    }
    EXPECT_EQ(steps["AddExp"], 3);
    EXPECT_EQ(steps["LearnItemMagic"], 1);  // Terra; Wedge and Vicks learn no spells
    EXPECT_EQ(steps["DoLevelUp"], 3);
    EXPECT_EQ(steps["LearnAbilities"], 3);
    const auto terra = std::find_if(calls.begin(), calls.end(), [](const TracedCall& call) {
        return call.routine == "LearnAbilities" && call.abilitiesActor == 0;
    });
    ASSERT_NE(terra, calls.end());
    EXPECT_EQ(terra->abilitiesLevel, 6);

    withOracle([&](DamageOracle& oracle) {
        for (const TracedCall& call : calls) {
            EXPECT_EQ(oracle.replay(call), "") << "frame " << call.frame << ": " << call.routine;
        }
        for (const TracedEnding& ending : endings) {
            EXPECT_EQ(oracle.replayEnding(ending), "") << "frame " << ending.frame << ": " << ending.routine;
        }
    });
}

// The same fight with the three party members' HP written to 1 and nothing pressed until the guards
// have felled them; A then closes "Annihilated". The loss, from the end check, shows that message alone
// after its wrap-up.
TEST_F(RewardsTraceTest, TheRecordedLossIsThePorts) {
    const std::string text = recording("rewards-loss.log");
    const std::vector<TracedEnding> endings = endingsIn(text);
    ASSERT_EQ(endings.size(), 2u);
    const TracedEnding& loss = endingNamed(endings, "LoseBattle");
    (void)endingNamed(endings, "_c24903");
    EXPECT_EQ(loss.returnedTo, 0xC24831u);
    EXPECT_EQ(shown(loss), (std::vector<MessageShown>{{.message = battle::kMessageAnnihilated}}));

    withOracle([&](DamageOracle& oracle) {
        for (const TracedEnding& ending : endings) {
            EXPECT_EQ(oracle.replayEnding(ending), "") << "frame " << ending.frame << ": " << ending.routine;
        }
        for (const TracedCall& call : callsIn(text)) {
            EXPECT_EQ(oracle.replay(call), "") << "frame " << call.frame << ": " << call.routine;
        }
    });
}

}  // namespace
}  // namespace ostinato::test
