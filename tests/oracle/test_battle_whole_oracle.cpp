// Whole battles on the oracle (docs/contracts/battle_core.md § The battle whole): the frame model's
// opening as the recorded fights measured it, and a script's frames; the battle program driven a loop
// step at a time adds up to the same battle called once; the first fight runs to its end the same on
// both sides; the cells the carried-read gate passes by a rule of its own, and what an empty party slot's
// equipment refresh writes at a later final-battle stage, reach nothing, each proved by a battle run twice
// with the cells written two ways; and 100 drawn battles are the port's to their ends.
//
// Every test that asks the cartridge on the engine's machine skips in a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "assets/cartridge.h"
#include "battle/action_queue.h"
#include "battle/battle_end.h"
#include "battle/battle_state.h"
#include "battle/commands.h"
#include "battle/items_obtained.h"
#include "ostinato/battle_command_id.h"
#include "machine/machine_state.h"
#include "oracle/damage_oracle.h"
#include "ostinato/item_id.h"
#include "retropp/guest_escape.h"
#include "retropp/snes.h"
#include "retropp/vm.h"
#include "scenarios/battle/battle_draw.h"
#include "scenarios/battle/presentation_model.h"
#include "scenarios/battle/whole_battle.h"
#include "scenarios/damage/damage.h"
#include "items_obtained_test_support.h"
#include "vanilla_rom.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

namespace fs = std::filesystem;
using trace::DamageOracle;

// The battle program's entry, as the field's jump to it lands (battle/battle_main.asm:39-40).
constexpr std::uint32_t kBattleEntry = 0xC20000;
// How far past first-fight.inputs's last line a run looks for its battle.
constexpr std::uint64_t kRunOutFrames = 600;
// A battle short enough for one bound call: the opening, then the steps up to this many frames, when the
// cartridge's own event 4 ends it.
constexpr std::uint64_t kShortBattleFrames = 120;
constexpr std::uint64_t kModelSeed = 1;
// The battles DrawnBattlesAreThePorts draws.
constexpr int kDrawnBattles = 100;

class BattleWholeOracleTest : public ::testing::Test {
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
    static const assets::IngestedContent& content() { return *content_; }

    // The machine on the tick before the first fight in Narshe's battle program is entered, played from
    // power-on by first-fight.inputs.
    static const std::vector<std::uint8_t>& firstFight() {
        if (firstFight_ == nullptr) {
            std::ifstream inputs{fs::path{OSTINATO_DAMAGE_TRACE_RECORDINGS} / "../../../tools/damage-trace/inputs/first-fight.inputs"};
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

    // The first fight's start, read off the machine as the field leaves it the tick before.
    static trace::StartScenario firstFightScenario() {
        trace::StartScenario scenario;
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            if (!oracle.machine().restore(firstFight())) {
                throw std::runtime_error("the first fight's state did not restore");
            }
            scenario = oracle.readStartScenario();
        });
        return scenario;
    }

    static void ready(DamageOracle& oracle) {
        oracle.start();
        oracle.useText(content().text);
        oracle.useMonsterSprites(content().monsterSprites);
    }

    static std::vector<std::uint8_t>* image_;
    static assets::IngestedContent* content_;
    static std::vector<std::uint8_t>* firstFight_;
};

std::vector<std::uint8_t>* BattleWholeOracleTest::image_ = nullptr;
assets::IngestedContent* BattleWholeOracleTest::content_ = nullptr;
std::vector<std::uint8_t>* BattleWholeOracleTest::firstFight_ = nullptr;

// The stack's pages, where a battle called a step at a time holds the call made for the step: page $01
// for a battle called on a machine fresh from its reset code, page $15 for one the field calls.
constexpr std::size_t kPageSize = 0x100;
constexpr std::size_t kResetStackPage = 0x0100;
constexpr std::size_t kFieldStackPage = 0x1500;

bool onTheStack(std::size_t cell) {
    const std::size_t page = cell & ~(kPageSize - 1);
    return page == kResetStackPage || page == kFieldStackPage;
}

// Every cell off the stack's pages where two battles' cells part, one per line.
std::string differingCells(const trace::BattleCells& a, const trace::BattleCells& b) {
    std::string lines;
    for (std::size_t cell = 0; cell < a.size(); ++cell) {
        if (!onTheStack(cell) && a[cell] != b[cell]) {
            char text[48];
            std::snprintf(text, sizeof text, "$7E:%04zX %02X / %02X\n", cell, a[cell], b[cell]);
            lines += text;
        }
    }
    return lines;
}

std::vector<std::uint8_t> commandsOf(const std::vector<trace::GraphicsCall>& calls) {
    std::vector<std::uint8_t> commands;
    for (const trace::GraphicsCall& call : calls) {
        commands.push_back(call.command);
    }
    return commands;
}

// The opening as the seven recorded fights measured it: 75 runs of the interrupt's body, 38 of them up to
// and with the first clock call's, then 36 clock calls in all, two waits taking two frames each.
TEST(BattleWholeModelTest, TheMeasuredOpeningIsTheRecordedFightsOpening) {
    const trace::PresentationModel model = trace::measuredPresentationModel(kModelSeed);
    const std::vector<trace::Frame> frames = model.openingFrames();
    ASSERT_EQ(frames.size(), 75u);
    EXPECT_EQ(std::count_if(frames.begin(), frames.end(), [](trace::Frame frame) { return frame.clockCalled; }), 36);
    EXPECT_TRUE(std::none_of(frames.begin(), frames.begin() + 37, [](trace::Frame frame) { return frame.clockCalled; }));
    EXPECT_TRUE(frames[37].clockCalled) << "the 38th frame is the first clock call's";
    EXPECT_TRUE(frames.back().clockCalled);
}

// A script's frames depend on the seed and its place only, number 1 to 12, and run the clock unless the
// script stops time.
TEST(BattleWholeModelTest, AScriptsFramesComeFromItsPlace) {
    const trace::PresentationModel model = trace::measuredPresentationModel(kModelSeed);
    for (std::size_t index = 0; index < 50; ++index) {
        const std::vector<trace::Frame> frames = model.scriptFrames(index, false);
        EXPECT_GE(frames.size(), 1u);
        EXPECT_LE(frames.size(), static_cast<std::size_t>(trace::kLongestScript));
        EXPECT_EQ(frames, model.scriptFrames(index, false));
        EXPECT_TRUE(std::all_of(frames.begin(), frames.end(), [](trace::Frame frame) { return frame.clockCalled; }));
        const std::vector<trace::Frame> stopped = model.scriptFrames(index, true);
        EXPECT_EQ(stopped.size(), frames.size());
        EXPECT_TRUE(std::none_of(stopped.begin(), stopped.end(), [](trace::Frame frame) { return frame.clockCalled; }));
    }
}

// The first fight, from its written scenario, driven a loop step at a time and called once as the field
// calls it: the same graphics calls in the same order, the same steps' cells, and the same cells at the
// end; every step left the stack as it found it.
TEST_F(BattleWholeOracleTest, TheStepsAddUpToTheBattleCalledOnce) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const trace::BattleScenario scenario{.start = firstFightScenario(),
                                         .model = trace::measuredPresentationModel(kModelSeed),
                                         .frameCap = kShortBattleFrames};
    trace::runOnLoop([&] {
        DamageOracle stepped{image()};
        ready(stepped);
        const trace::WholeBattleResult steps = stepped.wholeBattle(scenario);
        DamageOracle once{image()};
        ready(once);
        const trace::WholeBattleResult whole = once.wholeBattleInOneCall(scenario);
        ASSERT_TRUE(whole.ended) << "the battle did not fit one bound call";
        EXPECT_TRUE(steps.ended);
        EXPECT_TRUE(steps.timedOut);
        EXPECT_EQ(steps.stackPointers.size(), 1u);
        EXPECT_EQ(commandsOf(steps.calls), commandsOf(whole.calls));
        ASSERT_EQ(steps.steps.size(), whole.steps.size());
        for (std::size_t step = 0; step < steps.steps.size(); ++step) {
            EXPECT_EQ(differingCells(steps.steps[step].cells, whole.steps[step].cells), "") << "step " << step;
            EXPECT_EQ(steps.steps[step].frame, whole.steps[step].frame) << "step " << step;
        }
        EXPECT_EQ(differingCells(steps.endCells, whole.endCells), "") << "at the end";
    });
}

// The first fight in Narshe, from its written scenario with the measured opening, run to its end on both
// sides: every loop step's state, its requests and the frame it is reached on, both gates, and what the
// game keeps.
TEST_F(BattleWholeOracleTest, TheFirstFightIsThePorts) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const trace::BattleScenario scenario{.start = firstFightScenario(),
                                         .model = trace::measuredPresentationModel(kModelSeed)};
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleCheck check = oracle.checkBattle(scenario);
        EXPECT_EQ(check.differences, "");
        EXPECT_TRUE(check.cartridge.ended);
        EXPECT_FALSE(check.cartridge.timedOut);
    });
}

// The last three places of the items obtained are other programs' between battles
// (docs/contracts/battle_rewards.md § D.8): a battle that touches none of them hands back every bit it was
// handed. The sweep's first drawn battle, handed places no item's record could fill — flags bits no record
// sets ($07) and a fifth byte's high four ($F0).
TEST_F(BattleWholeOracleTest, ATailNoRecordCouldFillIsHandedBackAsItCame) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const battle::ItemsObtainedTail handed{kNoRecordSaysThis, battle::kEmptyItemRecord, kNoRecordSaysThis};
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        std::mt19937 rng{trace::kSweepSeed};
        trace::BattleScenario scenario = trace::drawBattleScenario(rng, content().text, content().monsterSprites);
        scenario.start.start.itemsObtainedTail = handed;
        const trace::BattleCheck check = oracle.checkBattle(scenario);
        EXPECT_EQ(check.differences, "") << trace::describe(scenario);
        EXPECT_EQ(check.cartridge.itemsObtainedTail, handed);
        ASSERT_TRUE(check.port.result.has_value());
        EXPECT_EQ(check.port.result->itemsObtainedTail, handed);
    });
}

// The cells the proofs below write, and the two ways each is written.
constexpr std::uint32_t kAttackThirdFlags = 0x7E11A4;
constexpr std::uint32_t kScriptFourthByte = 0x7E00B7;
constexpr std::uint32_t kTargetsBeingMade = 0x7E00B8;

// The step at whose frame wait a proof writes its cell: the one before the first step whose state holds
// battle event `event` among the actions that jump the line, so the write is in place through the step
// that queues it. Nothing when no step does.
std::optional<std::size_t> stepBeforeTheEventIsQueued(const trace::PortBattle& port, std::uint8_t event) {
    for (std::size_t step = 1; step < port.steps.size(); ++step) {
        const auto& queued = port.steps[step].immediateActions;
        if (std::any_of(queued.begin(), queued.end(), [event](const battle::Action& action) {
                const auto* played = std::get_if<battle::BattleEventAction>(&action);
                return played != nullptr && played->event == event;
            })) {
            return step - 1;
        }
    }
    return std::nullopt;
}

// A battle aimed at `aim`, then run twice on the cartridge with `at` written as `first` and then as
// `second` at the frame wait of the step before battle event `event` is queued: both runs agree with the
// port to the battle's end, so nothing the battle reads depends on what the cell held.
void expectTheCellReachesNothing(DamageOracle& oracle, const assets::IngestedContent& content, trace::BattleAim aim,
                                 std::uint8_t event, std::uint32_t at, const std::vector<std::uint8_t>& first,
                                 const std::vector<std::uint8_t>& second) {
    std::mt19937 rng{trace::kSweepSeed};
    const trace::AimedBattle drawn = trace::drawAimedBattleScenario(rng, content.text, content.monsterSprites, aim);
    ASSERT_TRUE(drawn.reached) << "no battle of " << trace::kAimTries << " drawn reached " << trace::aimName(aim);
    const trace::PortBattle port = trace::portBattle(drawn.scenario, content.text, content.monsterSprites);
    const std::optional<std::size_t> step = stepBeforeTheEventIsQueued(port, event);
    ASSERT_TRUE(step.has_value()) << "the port's battle queues no event " << int{event};
    for (const std::vector<std::uint8_t>& bytes : {first, second}) {
        trace::BattleScenario written = drawn.scenario;
        written.cartridgeWrites = {trace::StepCellWrite{.step = *step, .at = at, .bytes = bytes}};
        const trace::BattleCheck check = oracle.checkBattle(written);
        EXPECT_EQ(check.differences, "") << trace::describe(written) << ", written " << int{bytes.front()} << " at step "
                                         << *step;
        EXPECT_TRUE(check.cartridge.ended);
    }
}

// Terra casting while battle switch 65 calls for her event: the event is queued and then `tsb $11a4` sets
// the undodgeable bit in whatever the last record left (Cmd_02 battle/battle_main.asm:3660-3672); the
// spell's own record replaces the byte before anything reads it (InitCmdTarget :4150-4152, LoadMagicProp
// :6858-6868). The byte written 0 and 255 at that step changes nothing.
TEST_F(BattleWholeOracleTest, WhatTerrasMagicFindsInTheAttacksThirdFlagsReachesNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        expectTheCellReachesNothing(oracle, content(), trace::BattleAim::TerrasMagicEvent,
                                    battle::kBattleEventTerraMagicScene, kAttackThirdFlags, {0x00}, {0xFF});
    });
}

// A script command's fourth byte ($B7) is set only by the attacks that have one, and every other attack's
// command carries the last one's into the graphics program's block (_c235d4 :8566-8569; ExecCmd does not
// reset it, :3096-3109). Terra's Magic sets none: the byte written 0 and 255 at that step changes nothing
// the battle program reads.
TEST_F(BattleWholeOracleTest, AScriptCommandsCarriedFourthByteReachesNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        expectTheCellReachesNothing(oracle, content(), trace::BattleAim::TerrasMagicEvent,
                                    battle::kBattleEventTerraMagicScene, kScriptFourthByte, {0x00}, {0xFF});
    });
}

// Gau's event is made with the targets being made ($B8-$B9) as a step before left them (GauAppears
// :12010-12017 to _48c4 :12003-12006), and CreateAction copies them into the action's slot
// (:12960-12961); a story event never reads the targets it stored (Cmd_23 :13019-13024). The pair written
// 0 and 65,535 at that step changes nothing.
TEST_F(BattleWholeOracleTest, TheTargetsGausEventCopiesReachNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        expectTheCellReachesNothing(oracle, content(), trace::BattleAim::GauTurnsUp, battle::kGauJoinsEvent,
                                    kTargetsBeingMade, {0x00, 0x00}, {0xFF, 0xFF});
    });
}

// The morph laid on the sweep's first drawn battle that the port wins away from the colosseum, on its first
// seated member: the meter `counter`, run down by kMorphSpeed on every time update, from a supply of
// kMorphSupply (MorphLaid, scenarios/battle/whole_battle.h).
constexpr std::uint16_t kMorphSpeed = 0x0100;
constexpr std::uint8_t kMorphSupply = 200;

trace::BattleScenario aWonBattle(const assets::IngestedContent& content) {
    std::mt19937 rng{trace::kSweepSeed};
    for (int drawn = 0; drawn < 100; ++drawn) {
        const trace::BattleScenario scenario = trace::drawBattleScenario(rng, content.text, content.monsterSprites);
        const trace::PortBattle port = trace::portBattle(scenario, content.text, content.monsterSprites);
        if (port.result && std::holds_alternative<battle::BattleWon>(port.result->outcome) && !port.steps.empty() &&
            !port.steps.front().colosseum) {
            return scenario;
        }
    }
    throw std::runtime_error("none of 100 drawn battles was won");
}

trace::BattleScenario morphed(trace::BattleScenario scenario, std::uint16_t counter,
                              const assets::IngestedContent& content) {
    const trace::PortBattle port = trace::portBattle(scenario, content.text, content.monsterSprites);
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::BattlerSlot slot = battle::BattlerSlot::character(position);
        if (port.steps.front().at(slot).actor) {
            scenario.firstStep.morph = trace::MorphLaid{.member = slot, .counter = counter, .speed = kMorphSpeed};
            scenario.start.cells.party.morphSupply = kMorphSupply;
            return scenario;
        }
    }
    throw std::runtime_error("the battle seats no party member");
}

// The wrap-up keeps the Morph supply as the meter stands (_c24903 :12064, SaveMorphCounter :1762-1771) and
// then waits 32 frames with battle time running (:12076-12080, BtlGfx_01 btlgfx_main.asm:392-409); a meter
// that runs out in them saves the supply again by a meter of nothing (DecMorphCounter :2815-2829). A won
// battle laid with a morph whose meter runs out at the first time update of the wait.
TEST_F(BattleWholeOracleTest, AMorphRunningOutInTheWrapUpsWaitLeavesTheSupplyAtNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const trace::BattleScenario won = aWonBattle(content());
    // The least meter still running as the wait begins: it runs out at the wait's first time update.
    std::optional<trace::BattleScenario> scenario;
    for (std::uint16_t updates = 1; updates < 0x100 && !scenario; ++updates) {
        const trace::BattleScenario tried = morphed(won, static_cast<std::uint16_t>(updates * kMorphSpeed), content());
        const trace::PortBattle port = trace::portBattle(tried, content().text, content().monsterSprites);
        if (port.wrapUpWaitFrom && *port.wrapUpWaitFrom < port.morphed.size() && port.morphed[*port.wrapUpWaitFrom]) {
            scenario = tried;
        }
    }
    ASSERT_TRUE(scenario.has_value()) << "no meter lasted to the wrap-up's wait";
    RecordProperty("meter", scenario->firstStep.morph->counter);
    const trace::PortBattle port = trace::portBattle(*scenario, content().text, content().monsterSprites);
    ASSERT_TRUE(port.wrapUpWaitFrom.has_value());
    const std::uint64_t waitEnd = *port.wrapUpWaitFrom + battle::kWrapUpWaitFrames;
    ASSERT_LT(waitEnd, port.morphed.size());
    EXPECT_FALSE(port.morphed[waitEnd]) << "the meter outlasted the wait";
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleCheck check = oracle.checkBattle(*scenario);
        EXPECT_EQ(check.differences, "") << trace::describe(*scenario);
        EXPECT_EQ(check.cartridge.kept.cells.party.morphSupply, 0);
        ASSERT_TRUE(check.port.result.has_value());
        EXPECT_EQ(check.port.result->party.morphSupply, 0);
    });
}

// A morph's meter running out makes a Revert among the morphed battler's counterattacks with the targets
// being made ($B8-$B9) as a step before left them (DecMorphCounter :2827-2829, CreateRetalAction to
// CreateAction's copy :12960-12961), targets the state leaves out (oracle/battle/state_actions.cpp). The
// pair written 0 and 65,535 at the step before the Revert is made changes nothing.
TEST_F(BattleWholeOracleTest, TheTargetsAMorphsRevertCopiesReachNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const trace::BattleScenario scenario = morphed(aWonBattle(content()), 4 * kMorphSpeed, content());
    const trace::PortBattle port = trace::portBattle(scenario, content().text, content().monsterSprites);
    // The Revert is made and runs inside one loop step: the step before the first one that holds no morph.
    std::optional<std::size_t> step;
    for (std::size_t at = 1; at < port.steps.size() && !step; ++at) {
        if (port.steps[at - 1].morphedBattler && !port.steps[at].morphedBattler) {
            step = at - 1;
        }
    }
    ASSERT_TRUE(step.has_value()) << "the port's battle runs the meter out in no step";
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        for (const std::vector<std::uint8_t>& bytes : {std::vector<std::uint8_t>{0x00, 0x00}, {0xFF, 0xFF}}) {
            trace::BattleScenario written = scenario;
            written.cartridgeWrites = {trace::StepCellWrite{.step = *step, .at = kTargetsBeingMade, .bytes = bytes}};
            const trace::BattleCheck check = oracle.checkBattle(written);
            EXPECT_EQ(check.differences, "") << trace::describe(written) << ", written " << int{bytes.front()}
                                             << " at step " << *step;
            EXPECT_TRUE(check.cartridge.ended);
        }
    });
}

// A battle of the sweep's draws aimed at a final stage scrolled on whose next stage seats nobody in a party
// slot: the port's first step of that stage, the slots empty there, and whether the first step's refresh
// ends on one of them. The refresh's last slot leaves its own sums in $11A0-$11DE (UpdateEquip :2301-2348):
// a seated member's, or an empty slot's, summed from the bytes below the first character record.
struct EmptySlotAtALaterStage {
    trace::BattleScenario scenario;
    std::size_t firstStep = 0;
    std::vector<int> empty;
    bool refreshEndsOnAnEmptySlot = false;
    // The port's last attack's level and physical bit at the stage's second step.
    std::uint8_t lastAttackLevel = 0;
    bool lastAttackPhysical = false;
};

// Which of those battles a proof takes: those whose first step's refresh ends on a seated member, or every
// one whose first step's refresh takes an empty slot, wherever it ends.
enum class RefreshEnds { OnASeatedMember, AnyWay };

std::vector<EmptySlotAtALaterStage> battlesWithAnEmptySlotAtALaterStage(const assets::IngestedContent& content,
                                                                        RefreshEnds ends = RefreshEnds::OnASeatedMember) {
    std::vector<EmptySlotAtALaterStage> found;
    std::mt19937 rng{trace::kSweepSeed};
    for (int drawn = 0; drawn < 100; ++drawn) {
        const trace::AimedBattle battle = trace::drawBattle(rng, content.text, content.monsterSprites);
        if (battle.aim != trace::BattleAim::FinalStageScrolledOn || !battle.reached) {
            continue;
        }
        const trace::PortBattle port = trace::portBattle(battle.scenario, content.text, content.monsterSprites);
        for (std::size_t step = 0; step < port.steps.size(); ++step) {
            const battle::BattleState& state = port.steps[step];
            if (state.formation == state.startingFormation) {
                continue;
            }
            EmptySlotAtALaterStage later{.scenario = battle.scenario, .firstStep = step, .empty = {}};
            for (int position = 0; position < battle::kPartySlots; ++position) {
                if (!state.at(battle::BattlerSlot::character(position)).actor) {
                    later.empty.push_back(position);
                }
            }
            // The last slot the refresh takes is the lowest whose flag is set (UpdateCharProp :5597-5631).
            std::optional<int> last;
            for (int position = battle::kPartySlots - 1; position >= 0; --position) {
                if (state.equipmentChangeFlags[static_cast<std::size_t>(position)] != 0) {
                    last = position;
                }
            }
            later.refreshEndsOnAnEmptySlot = last && !state.at(battle::BattlerSlot::character(*last)).actor;
            const bool refreshesAnEmptySlot = std::any_of(later.empty.begin(), later.empty.end(), [&](int position) {
                return state.equipmentChangeFlags[static_cast<std::size_t>(position)] != 0;
            });
            const bool taken = ends == RefreshEnds::AnyWay ? refreshesAnEmptySlot
                                                           : last && !later.refreshEndsOnAnEmptySlot;
            if (!later.empty.empty() && taken && step + 1 < port.steps.size()) {
                later.lastAttackLevel = port.steps[step + 1].lastAttackLevel;
                later.lastAttackPhysical = port.steps[step + 1].lastAttackPhysical;
                found.push_back(std::move(later));
            }
            break;
        }
    }
    return found;
}

// What the equipment refresh writes for an empty slot at a final-battle stage after the first
// (oracle/battle/battle_state.cpp, UpdateEquipBattle :6746-6832, FixImmuneStatus :6359-6380,
// UpdateImmuneStatus :6386-6433), at twice the slot: bytes, then words.
constexpr std::array<std::uint32_t, 12> kRefreshedSlotBytes{0x7E3B2C, 0x7E3B2D, 0x7E3B19, 0x7E3B40,
                                                           0x7E3B41, 0x7E3B54, 0x7E3B55, 0x7E3C6C,
                                                           0x7E3C6D, 0x7E3D71, 0x7E3C95, 0x7E331D};
constexpr std::array<std::uint32_t, 15> kRefreshedSlotWords{0x7E3C44, 0x7E3B68, 0x7E3B7C, 0x7E3D34, 0x7E3B90,
                                                           0x7E3BA4, 0x7E3BB8, 0x7E331C, 0x7E3BCC, 0x7E3BE0,
                                                           0x7E3CBC, 0x7E3CA8, 0x7E3CD0, 0x7E3CE4, 0x7E3C58};
constexpr std::uint32_t kGenjiGloveBytes = 0x7E2E6E;
constexpr std::uint32_t kPartyCommandMenus = 0x7E202E;
constexpr std::uint32_t kCommandMenuBytes = 12;
constexpr std::uint32_t kCommandRowBytes = 3;
// UpdateEquip's two boost bytes for the record below the first character record. The record is read with
// the index at $FFFF by the time the weapon is read, and the two writes follow at $FFFE (:2343-2362):
// absolute indexed addressing carries the index into the bank, so they land in bank $7F, not on the stack.
constexpr std::uint32_t kBelowTheRecordsHpBoost = 0x7F15E7;
constexpr std::uint32_t kBelowTheRecordsMpBoost = 0x7F15EB;
// The block UpdateEquip clears and sums into, $11A0-$11DE (:2301-2304).
constexpr std::uint32_t kEquipmentSums = 0x7E11A0;
constexpr std::uint32_t kEquipmentSumBytes = 0x3F;
constexpr std::uint32_t kLastAttackLevel = 0x7E11AF;
constexpr std::uint32_t kAttackFirstFlags = 0x7E11A2;
constexpr std::uint8_t kPhysicalHitBit = 0x01;

constexpr std::uint32_t kPartyWeaponHands = 0x7E2B86;
constexpr std::uint32_t kPartyShieldHands = 0x7E2B9A;
constexpr std::uint32_t kHandRecordBytes = 5;

using SlotWrites = std::vector<trace::StepCellWrite> (*)(std::size_t step, int position);

// Where a proof writes its cells: at the stage's first step's frame wait, before the refresh reads them, or
// at its second's, after the refresh wrote them.
enum class WrittenAt { TheStagesFirstStep, TheStagesSecondStep };

// Each battle of battlesWithAnEmptySlotAtALaterStage() run twice on the cartridge with the cells `writes`
// names for each empty slot written at `at`, as 0 and then as 255: both runs agree with the port to the
// battle's end, so nothing the battle reads depends on what those cells held.
void expectAnEmptySlotsRefreshReachesNothing(DamageOracle& oracle, const assets::IngestedContent& content,
                                             SlotWrites writes, WrittenAt at = WrittenAt::TheStagesSecondStep) {
    const std::vector<EmptySlotAtALaterStage> battles = battlesWithAnEmptySlotAtALaterStage(content);
    ASSERT_FALSE(battles.empty()) << "no battle the sweep draws seats nobody in a slot at a later final stage";
    for (const EmptySlotAtALaterStage& battle : battles) {
        const std::size_t step = battle.firstStep + (at == WrittenAt::TheStagesSecondStep ? 1 : 0);
        for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0xFF}}) {
            trace::BattleScenario written = battle.scenario;
            for (const int position : battle.empty) {
                for (trace::StepCellWrite write : writes(step, position)) {
                    std::fill(write.bytes.begin(), write.bytes.end(), value);
                    written.cartridgeWrites.push_back(std::move(write));
                }
            }
            const trace::BattleCheck check = oracle.checkBattle(written);
            EXPECT_EQ(check.differences, "") << trace::describe(written) << ", written " << int{value} << " at step "
                                             << step;
            EXPECT_TRUE(check.cartridge.ended);
        }
    }
}

// UpdateEquipBattle's, FixImmuneStatus's and UpdateImmuneStatus's cells of the empty slot, the Genji Glove
// byte with them.
std::vector<trace::StepCellWrite> theSlotsTables(std::size_t step, int position) {
    std::vector<trace::StepCellWrite> writes;
    const auto at = static_cast<std::uint32_t>(2 * position);
    for (const std::uint32_t table : kRefreshedSlotBytes) {
        writes.push_back({.step = step, .at = table + at, .bytes = {0}});
    }
    for (const std::uint32_t table : kRefreshedSlotWords) {
        writes.push_back({.step = step, .at = table + at, .bytes = {0, 0}});
    }
    writes.push_back({.step = step, .at = kGenjiGloveBytes + static_cast<std::uint32_t>(position), .bytes = {0}});
    writes.push_back({.step = step, .at = 0x7E3A21 + at, .bytes = {0}});
    writes.push_back({.step = step, .at = 0x7E3330 + at, .bytes = {0, 0}});
    return writes;
}

// UpdateCmdList's verdict bytes of each of the empty slot's four command rows.
std::vector<trace::StepCellWrite> theSlotsCommandRows(std::size_t step, int position) {
    std::vector<trace::StepCellWrite> writes;
    for (std::uint32_t row = 0; row < kCommandMenuBytes / kCommandRowBytes; ++row) {
        writes.push_back({.step = step,
                          .at = kPartyCommandMenus + static_cast<std::uint32_t>(position) * kCommandMenuBytes +
                                row * kCommandRowBytes + 1,
                          .bytes = {0, 0}});
    }
    return writes;
}

// The empty slot's two hand records, its command rows whole, and the two of its cells the refresh reads and
// writes back changed — $3A21,x (:6830-6832) and the statuses blocked at $3330-$3331,x (:6388-6432).
std::vector<trace::StepCellWrite> theSlotsHandsAndRows(std::size_t step, int position) {
    const auto place = static_cast<std::uint32_t>(position) * kHandRecordBytes;
    const auto at = static_cast<std::uint32_t>(2 * position);
    return {{.step = step, .at = 0x7E3A21 + at, .bytes = {0}},
            {.step = step, .at = 0x7E3330 + at, .bytes = {0, 0}},
            {.step = step, .at = kPartyWeaponHands + place, .bytes = std::vector<std::uint8_t>(kHandRecordBytes)},
            {.step = step, .at = kPartyShieldHands + place, .bytes = std::vector<std::uint8_t>(kHandRecordBytes)},
            {.step = step,
             .at = kPartyCommandMenus + static_cast<std::uint32_t>(position) * kCommandMenuBytes,
             .bytes = std::vector<std::uint8_t>(kCommandMenuBytes)}};
}

// UpdateEquip's two boost bytes in bank $7F. The party's relic byte the refresh ORs into (:6787) is
// compared as the port's zero: with the bytes the refresh sums zero here the OR changes nothing, and its
// one reader runs before any refresh (:7557, :7632).
std::vector<trace::StepCellWrite> theBoostsInBank7F(std::size_t step, int) {
    return {{.step = step, .at = kBelowTheRecordsHpBoost, .bytes = {0}},
            {.step = step, .at = kBelowTheRecordsMpBoost, .bytes = {0}}};
}

// At a final-battle stage after the first an empty slot's flag stands at the fill's $FF, and the refresh runs
// for it through the fill's record offset, from the field program's stack (UpdateCharProp :5614-5623,
// UpdateEquip :2295-2348). The port does not run it (docs/Bugs.md); what it writes for the slot reaches
// nothing.
TEST_F(BattleWholeOracleTest, WhatAnEmptySlotsRefreshWritesReachesNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        expectAnEmptySlotsRefreshReachesNothing(oracle, content(), theSlotsTables);
    });
}

TEST_F(BattleWholeOracleTest, TheCommandRowsAnEmptySlotsRefreshWritesReachNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        expectAnEmptySlotsRefreshReachesNothing(oracle, content(), theSlotsCommandRows);
    });
}

// The refresh reads the empty slot's hands, command rows, $3A21,x and statuses blocked as the stage's first step
// left them (UpdateCharProp :5610-5612, UpdateCmdList :13583, :13611, :6830-6832, :6388-6432): what they held
// reaches the bank-$7F stores and the slot's own cells alone.
TEST_F(BattleWholeOracleTest, TheHandsAndRowsAnEmptySlotsRefreshReadsReachNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        expectAnEmptySlotsRefreshReachesNothing(oracle, content(), theSlotsHandsAndRows, WrittenAt::TheStagesFirstStep);
    });
}

TEST_F(BattleWholeOracleTest, TheBoostsAnEmptySlotsRefreshWritesInBank7FReachNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        expectAnEmptySlotsRefreshReachesNothing(oracle, content(), theBoostsInBank7F);
    });
}

// The block the refresh sums into holds, after an empty slot's refresh, that slot's sums of the record
// below the first character record, unless a seated member is refreshed after it (UpdateCharProp
// :5597-5631, UpdateEquip :2301-2348). The port holds two of its cells, the last attack's level ($11AF) and
// its physical bit ($11A2 bit 0), as the refresh before left them, and the comparison leaves them out from
// the step after such a refresh until the level is written again (docs/Bugs.md). Every battle the sweep
// draws whose first step at a later stage refreshes an empty slot, run twice on the cartridge with the
// whole block written 0 and then 255 at the stage's second step — the two held cells as the port holds
// them where a seated member was refreshed last: both runs agree with the port to the battle's end.
TEST_F(BattleWholeOracleTest, TheBlockAnEmptySlotsRefreshLeavesReachesNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const std::vector<EmptySlotAtALaterStage> battles =
            battlesWithAnEmptySlotAtALaterStage(content(), RefreshEnds::AnyWay);
        ASSERT_TRUE(std::any_of(battles.begin(), battles.end(),
                                [](const EmptySlotAtALaterStage& battle) { return battle.refreshEndsOnAnEmptySlot; }))
            << "no battle the sweep draws refreshes an empty slot last at a later final stage";
        for (const EmptySlotAtALaterStage& battle : battles) {
            const std::size_t step = battle.firstStep + 1;
            for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0xFF}}) {
                std::vector<std::uint8_t> block(kEquipmentSumBytes, value);
                if (!battle.refreshEndsOnAnEmptySlot) {
                    block[kLastAttackLevel - kEquipmentSums] = battle.lastAttackLevel;
                    std::uint8_t& flags = block[kAttackFirstFlags - kEquipmentSums];
                    flags = static_cast<std::uint8_t>((flags & ~kPhysicalHitBit) |
                                                      (battle.lastAttackPhysical ? kPhysicalHitBit : 0));
                }
                trace::BattleScenario written = battle.scenario;
                written.cartridgeWrites = {trace::StepCellWrite{.step = step, .at = kEquipmentSums, .bytes = block}};
                const trace::BattleCheck check = oracle.checkBattle(written);
                EXPECT_EQ(check.differences, "") << trace::describe(written) << ", written " << int{value}
                                                 << " at step " << step;
                EXPECT_TRUE(check.cartridge.ended);
            }
        }
    });
}

// Drawn battles with no player (trace::drawBattleScenario()), each the port's to its end, and between them
// every row of the reach table taken: each part a member plays, and each way a battle ends.
TEST_F(BattleWholeOracleTest, DrawnBattlesAreThePorts) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::map<std::string_view, int> reached;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        std::mt19937 rng{trace::kSweepSeed};
        for (int draw = 0; draw < kDrawnBattles; ++draw) {
            const trace::BattleScenario scenario = trace::drawBattleScenario(rng, content().text, content().monsterSprites);
            const trace::BattleCheck check = oracle.checkBattle(scenario);
            EXPECT_EQ(check.differences, "") << "draw " << draw << ": " << trace::describe(scenario);
            if (check.differences.empty()) {
                for (const std::string_view row : trace::battleReached(scenario, check)) {
                    ++reached[row];
                }
            }
        }
    });
    for (const std::string_view row : trace::battleReachRows()) {
        EXPECT_GT(reached[row], 0) << row;
    }
}

}  // namespace
}  // namespace ostinato::test
