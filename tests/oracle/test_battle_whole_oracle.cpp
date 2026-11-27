// Whole battles on the oracle (tools/damage-trace/README.md § The battle whole): the frame model's
// opening as the recorded fights measured it, and a script's frames; the battle program driven a loop
// step at a time adds up to the same battle called once; the first fight runs to its end the same on
// both sides; the cells the carried-read gate passes by a rule of its own, and what an empty party slot's
// equipment refresh writes at a later final-battle stage, reach nothing, each proved by a battle run twice
// with the cells written two ways; an empty party slot's hands, which the start reads from bank $7F; eight
// battles read against the listing at the step each is about; a state with its list of actions out of order
// written into a machine; a Jump's ready stance, read on a drawn battle as aimed at the action pool's top
// slot's targets and not the Jump's own; and 100 drawn battles are the port's to their ends.
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
#include <functional>
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
#include "assets/rom_reader.h"
#include "battle/action_queue.h"
#include "battle/battle_end.h"
#include "battle/battle_requests.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/commands.h"
#include "battle/items_obtained.h"
#include "cells/battle/state.h"
#include "cells/damage/per_target.h"
#include "cells/ending/ending.h"
#include "cells/status/effects.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "machine/machine_state.h"
#include "oracle/damage_oracle.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"
#include "retropp/guest_escape.h"
#include "retropp/memory_region.h"
#include "retropp/snes.h"
#include "retropp/vm.h"
#include "scenarios/battle/battle_draw.h"
#include "scenarios/battle/presentation_model.h"
#include "scenarios/battle/start.h"
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
    static assets::HiRomImage cartridge() { return assets::HiRomImage{image()}; }

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
        trace::BattleScenario scenario =
            trace::drawBattleScenario(rng, content().text, content().monsterSprites, oracle.cartridge());
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
    const trace::AimedBattle drawn =
        trace::drawAimedBattleScenario(rng, content.text, content.monsterSprites, oracle.cartridge(), aim);
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

// A morph is laid on a won battle's first seated member: the meter `counter`, run down by kMorphSpeed on
// every time update, from a supply of kMorphSupply (MorphLaid, scenarios/battle/whole_battle.h).
constexpr std::uint16_t kMorphSpeed = 0x0100;
constexpr std::uint8_t kMorphSupply = 200;

// A drawn battle and its number among the sweep's draws, counted from 0.
struct DrawnBattle {
    int number = 0;
    trace::BattleScenario scenario;
};

// The first of the sweep's 100 drawn battles that the port wins away from the colosseum and that `suits`
// takes, or nothing when none of them is. With no `suits`, the first won battle.
std::optional<DrawnBattle> aWonBattle(
    const assets::IngestedContent& content, const assets::HiRomImage& cartridge,
    const std::function<bool(const trace::BattleScenario&)>& suits = [](const trace::BattleScenario&) { return true; }) {
    std::mt19937 rng{trace::kSweepSeed};
    for (int drawn = 0; drawn < 100; ++drawn) {
        const trace::BattleScenario scenario = trace::drawBattleScenario(rng, content.text, content.monsterSprites, cartridge);
        const trace::PortBattle port = trace::portBattle(scenario, content.text, content.monsterSprites);
        if (port.result && std::holds_alternative<battle::BattleWon>(port.result->outcome) && !port.steps.empty() &&
            !port.steps.front().colosseum && suits(scenario)) {
            return DrawnBattle{.number = drawn, .scenario = scenario};
        }
    }
    return std::nullopt;
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

// `won` laid with the least meter still running as the wrap-up's wait begins, so that it runs out at the
// wait's first time update, or nothing when no meter of 1 to 255 time updates lasts that long. The search
// runs the port's battle for each meter. A time update comes no sooner than two frames after the last
// (UpdateBattleTime :2675-2681), so no meter it tries lasts to a wait that begins much past 510 frames of
// battle time.
std::optional<trace::BattleScenario> morphedToRunOutInTheWait(const trace::BattleScenario& won,
                                                              const assets::IngestedContent& content) {
    for (std::uint16_t updates = 1; updates < 0x100; ++updates) {
        trace::BattleScenario tried = morphed(won, static_cast<std::uint16_t>(updates * kMorphSpeed), content);
        const trace::PortBattle port = trace::portBattle(tried, content.text, content.monsterSprites);
        if (port.wrapUpWaitFrom && *port.wrapUpWaitFrom < port.morphed.size() && port.morphed[*port.wrapUpWaitFrom]) {
            return tried;
        }
    }
    return std::nullopt;
}

// The wrap-up keeps the Morph supply as the meter stands (_c24903 :12064, SaveMorphCounter :1762-1771) and
// then waits 32 frames with battle time running (:12076-12080, BtlGfx_01 btlgfx_main.asm:392-409); a meter
// that runs out in them saves the supply again by a meter of nothing (DecMorphCounter :2815-2829). The
// battle is the first of the sweep's draws that the port wins away from the colosseum and that a meter of
// the search lasts to the wait in, laid with the least such meter.
TEST_F(BattleWholeOracleTest, AMorphRunningOutInTheWrapUpsWaitLeavesTheSupplyAtNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::optional<trace::BattleScenario> scenario;
    const std::optional<DrawnBattle> won =
        aWonBattle(content(), cartridge(), [&](const trace::BattleScenario& drawn) {
            scenario = morphedToRunOutInTheWait(drawn, content());
            return scenario.has_value();
        });
    ASSERT_TRUE(won.has_value()) << "none of the 100 drawn battles is won away from the colosseum with a wrap-up "
                                    "wait that a meter of 1 to 255 time updates lasts to";
    RecordProperty("draw", won->number);
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
    const std::optional<DrawnBattle> won = aWonBattle(content(), cartridge());
    ASSERT_TRUE(won.has_value()) << "none of the 100 drawn battles is won away from the colosseum";
    const trace::BattleScenario scenario = morphed(won->scenario, 4 * kMorphSpeed, content());
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
                                                                        const assets::HiRomImage& cartridge,
                                                                        RefreshEnds ends = RefreshEnds::OnASeatedMember) {
    std::vector<EmptySlotAtALaterStage> found;
    std::mt19937 rng{trace::kSweepSeed};
    for (int drawn = 0; drawn < 100; ++drawn) {
        const trace::AimedBattle battle = trace::drawBattle(rng, content.text, content.monsterSprites, cartridge);
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
    const std::vector<EmptySlotAtALaterStage> battles = battlesWithAnEmptySlotAtALaterStage(content, oracle.cartridge());
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
            battlesWithAnEmptySlotAtALaterStage(content(), oracle.cartridge(), RefreshEnds::AnyWay);
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

// Where InitInventory reads an empty party slot's hands, through the fill's $FFFF record offset, sixteen
// bits wide (:13918-13934): the shield's item is the byte at $7F:161F, the weapon's the byte at $7F:161E.
constexpr std::uint32_t kEmptySlotsWeaponItem = 0x7F161E;
constexpr std::uint32_t kEmptySlotsShieldItem = 0x7F161F;
// The last record's targeting, which a record made for no item carries (LoadItemProp :13987-14000).
constexpr std::uint32_t kLastRecordTargeting = 0x7E2E74;
// How many of the sweep's draws a case looks through for the battle it takes.
constexpr int kDrawsLookedThrough = 400;

// A colosseum match won by a fighter holding nothing in either hand, the three other slots empty and nothing
// in the inventory, for no prize: the first such battle the sweep's draws make, when the port wins it with the
// fighter's hands and the inventory emptied and the prize taken away.
std::optional<DrawnBattle> anEmptyHandedColosseumWin(const assets::IngestedContent& content,
                                                     const assets::HiRomImage& cartridge) {
    std::mt19937 rng{trace::kSweepSeed};
    for (int drawn = 0; drawn < kDrawsLookedThrough; ++drawn) {
        trace::BattleScenario scenario = trace::drawBattleScenario(rng, content.text, content.monsterSprites, cartridge);
        if (!scenario.start.start.colosseum) {
            continue;
        }
        scenario.start.start.colosseum->prize = ItemId::EMPTY;
        game::Party& party = scenario.start.cells.party;
        for (game::CharacterRecord& record : party.characters) {
            if (record.actor == scenario.start.start.colosseum->fighter) {
                record.weapon = ItemId::EMPTY;
                record.shield = ItemId::EMPTY;
            }
        }
        party.inventory.fill(game::InventorySlot{});
        const trace::PortBattle port = trace::portBattle(scenario, content.text, content.monsterSprites);
        if (port.result && std::holds_alternative<battle::BattleWon>(port.result->outcome)) {
            return DrawnBattle{.number = drawn, .scenario = scenario};
        }
    }
    return std::nullopt;
}

// InitInventory reads an empty party slot's hands from bank $7F (:13918-13934) and makes a record for the item
// each byte there names; with nothing in the inventory, the targeting the last of them leaves is what every
// record made for no item after them carries, the record for a prize of nothing among them (WinBattle
// :15300-15306, docs/Bugs.md). The port's empty slot holds nothing in either hand, and the comparison leaves
// that targeting out while the start's records for no item carry it. The match, won, agrees whole; the two
// bytes the harness's parked machine holds there are recorded.
TEST_F(BattleWholeOracleTest, AColosseumWinForNoPrizeWithNothingHeldLeavesTheEmptySlotsTargetingOut) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::optional<DrawnBattle> won = anEmptyHandedColosseumWin(content(), cartridge());
    ASSERT_TRUE(won.has_value()) << "none of " << kDrawsLookedThrough
                                 << " drawn battles is a colosseum match the port wins with nothing held";
    RecordProperty("draw", won->number);
    const trace::PortBattle port = trace::portBattle(won->scenario, content().text, content().monsterSprites);
    ASSERT_TRUE(port.closed.has_value());
    EXPECT_TRUE(DamageOracle::lastRecordsTargetingUnknown(*port.closed));
    EXPECT_FALSE(port.closed->inventory.lastRecordTargeting.has_value());
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const std::vector<std::uint8_t> hands =
            oracle.machine().read(retropp::MemoryRegion{.at = kEmptySlotsWeaponItem, .size = 2});
        RecordProperty("weapon item at $7F:161E", int{hands[0]});
        RecordProperty("shield item at $7F:161F", int{hands[kEmptySlotsShieldItem - kEmptySlotsWeaponItem]});
        const trace::BattleCheck check = oracle.checkBattle(won->scenario);
        EXPECT_EQ(check.differences, "") << trace::describe(won->scenario);
        EXPECT_TRUE(check.cartridge.ended);
        ASSERT_FALSE(check.cartridge.steps.empty());
        RecordProperty("targeting at $2E74", int{check.cartridge.steps.front().cells[kLastRecordTargeting - 0x7E0000]});
    });
}

// An empty party slot's two hand records, made from the bytes of bank $7F (above), are read by nothing away
// from the final battle: the first of the sweep's draws away from it that seats nobody in a slot, run twice on
// the cartridge with both records written 0 and then 255 at the first step's frame wait, agrees with the port
// both times to the battle's end.
TEST_F(BattleWholeOracleTest, AnEmptySlotsHandRecordsReachNothing) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        std::mt19937 rng{trace::kSweepSeed};
        std::optional<trace::BattleScenario> taken;
        std::optional<int> empty;
        for (int drawn = 0; drawn < kDrawsLookedThrough && !taken; ++drawn) {
            const trace::BattleScenario scenario =
                trace::drawBattleScenario(rng, content().text, content().monsterSprites, oracle.cartridge());
            const trace::PortBattle port = trace::portBattle(scenario, content().text, content().monsterSprites);
            if (port.steps.empty() || port.steps.front().finalBattleChain) {
                continue;
            }
            for (int position = 0; position < battle::kPartySlots && !empty; ++position) {
                if (!port.steps.front().at(battle::BattlerSlot::character(position)).actor) {
                    empty = position;
                    taken = scenario;
                    RecordProperty("draw", drawn);
                }
            }
        }
        ASSERT_TRUE(taken.has_value()) << "no drawn battle away from the final battle seats nobody in a slot";
        const auto place = static_cast<std::uint32_t>(*empty) * kHandRecordBytes;
        for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0xFF}}) {
            trace::BattleScenario written = *taken;
            const std::vector<std::uint8_t> record(kHandRecordBytes, value);
            written.cartridgeWrites = {trace::StepCellWrite{.step = 0, .at = kPartyWeaponHands + place, .bytes = record},
                                       trace::StepCellWrite{.step = 0, .at = kPartyShieldHands + place, .bytes = record}};
            const trace::BattleCheck check = oracle.checkBattle(written);
            EXPECT_EQ(check.differences, "") << trace::describe(written) << ", written " << int{value};
            EXPECT_TRUE(check.cartridge.ended);
        }
    });
}

// --- whole battles by hand --------------------------------------------------------------------------
//
// Each case below takes the first of the sweep's draws whose port battle reaches what it is about, runs it
// on both sides to its end, and reads what the listing says it holds at the step it happens: on the
// cartridge's cells and in the port's state.

namespace cells = trace::cells;

// The sweep's draw `draw`, counted from 0.
trace::BattleScenario theDraw(int draw, const assets::IngestedContent& content, const assets::HiRomImage& cartridge) {
    std::mt19937 rng{trace::kSweepSeed};
    trace::BattleScenario scenario;
    for (int drawn = 0; drawn <= draw; ++drawn) {
        scenario = trace::drawBattleScenario(rng, content.text, content.monsterSprites, cartridge);
    }
    return scenario;
}

std::uint8_t cellAt(const trace::CartridgeStep& step, std::uint32_t cell) { return step.cells[cell - cells::kWorkRam]; }

std::uint16_t wordAt(const trace::CartridgeStep& step, std::uint32_t cell) {
    return static_cast<std::uint16_t>(cellAt(step, cell) | cellAt(step, cell + 1) << 8);
}

// A battler's cells are a word apart (battler index × 2).
std::uint32_t battlerCell(std::uint32_t table, battle::BattlerSlot slot) {
    return table + 2u * static_cast<std::uint32_t>(slot.index());
}

// A battler's bit in a battler mask's word: a party slot's in the low byte, a monster slot's in the high.
std::uint16_t battlerBit(battle::BattlerSlot slot) {
    return static_cast<std::uint16_t>(slot.isCharacter() ? 1u << slot.index() : 0x100u << (slot.index() - battle::kPartySlots));
}

// An action slot of the battle's one list: $FF, none.
constexpr std::uint8_t kNoActionSlot = 0xFF;
// The command and attack an action slot holds, a word a slot (CreateAction :12944-12964).
std::uint8_t commandIn(const trace::CartridgeStep& step, std::uint8_t slot) {
    return cellAt(step, cells::kActionCommand + 2u * slot);
}
std::uint8_t attackIn(const trace::CartridgeStep& step, std::uint8_t slot) {
    return cellAt(step, cells::kActionCommand + 2u * slot + 1);
}
// $3AA0,x bit 5: the battler's action is queued (SetFlag0 :2744-2748).
constexpr std::uint8_t kActionQueuedBit = 0x20;
// The command an AI's turn is queued with (:504), and a battle event's (:12005).
constexpr std::uint8_t kAiTurnCommand = 0x1F;
constexpr std::uint8_t kBattleEventCommand = 0x23;
// CheckBattleEnd's flag for a battle the field's countdown ended (:11900-11903): bit 5 of battle switch
// byte $3EBC, from $1DD1's bit 5.
constexpr std::uint32_t kBattleEndSwitches = 0x7E3EBC;
constexpr std::uint8_t kTimerExpiredBit = 0x20;
// The sweep's draws each case below takes, found by running the port over the draws in order.
constexpr int kMonsterTurnDraw = 4;
constexpr int kCounterattackDraw = 0;
constexpr int kBerserkDraw = 0;
constexpr int kCondemnedDraw = 266;
constexpr int kFledDraw = 17;
constexpr int kCountdownDraw = 2;
constexpr int kGauDraw = 3;
constexpr int kFinalStageDraw = 24;

// A battle the port and the cartridge run alike to its end.
trace::BattleCheck checkedWhole(DamageOracle& oracle, const trace::BattleScenario& scenario) {
    trace::BattleCheck check = oracle.checkBattle(scenario);
    EXPECT_EQ(check.differences, "") << trace::describe(scenario);
    EXPECT_TRUE(check.cartridge.ended);
    EXPECT_EQ(check.cartridge.steps.size(), check.port.steps.size());
    return check;
}

// The first step whose port state `holds`, or nothing.
std::optional<std::size_t> firstStepWhere(const trace::PortBattle& port,
                                          const std::function<bool(const battle::BattleState&)>& holds) {
    for (std::size_t step = 0; step < port.steps.size(); ++step) {
        if (holds(port.steps[step])) {
            return step;
        }
    }
    return std::nullopt;
}

// A monster's turn: its gauge full, the AI's turn is made its first action and queued (:504, :2741-2748):
// at the first step a monster's action waits, $3AA0,x bit 5 is set and its first action ($32CC,x) is an
// AI's turn, command $1F.
TEST_F(BattleWholeOracleTest, AMonstersTurnIsQueuedAsItsAisTurn) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleCheck check = checkedWhole(oracle, theDraw(kMonsterTurnDraw, content(), oracle.cartridge()));
        std::optional<battle::BattlerSlot> monster;
        const std::optional<std::size_t> step = firstStepWhere(check.port, [&](const battle::BattleState& state) {
            for (int position = 0; position < battle::kMonsterSlots && !monster; ++position) {
                if (state.at(battle::BattlerSlot::monster(position)).actionQueued) {
                    monster = battle::BattlerSlot::monster(position);
                }
            }
            return monster.has_value();
        });
        ASSERT_TRUE(step.has_value()) << "no monster's action is queued in draw " << kMonsterTurnDraw;
        ASSERT_LT(*step, check.cartridge.steps.size());
        const trace::CartridgeStep& theirs = check.cartridge.steps[*step];
        const battle::Battler& ours = check.port.steps[*step].at(*monster);
        EXPECT_NE(cellAt(theirs, battlerCell(cells::kOnTheField, *monster)) & kActionQueuedBit, 0);
        const std::uint8_t first = cellAt(theirs, battlerCell(cells::kFirstAction, *monster));
        ASSERT_NE(first, kNoActionSlot);
        EXPECT_EQ(commandIn(theirs, first), kAiTurnCommand);
        ASSERT_FALSE(ours.actions.empty());
        EXPECT_TRUE(std::holds_alternative<battle::AiTurn>(ours.actions.front()));
    });
}

// A counterattack runs with $B1 bit 0 set (ExecRetal :12411-12414): at the first step the port holds one
// running, the cartridge's $B1 has the bit.
TEST_F(BattleWholeOracleTest, ACounterattackRunsWithItsFlagSet) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleCheck check =
            checkedWhole(oracle, theDraw(kCounterattackDraw, content(), oracle.cartridge()));
        const std::optional<std::size_t> step =
            firstStepWhere(check.port, [](const battle::BattleState& state) { return state.counterattackRunning; });
        ASSERT_TRUE(step.has_value()) << "no counterattack runs in draw " << kCounterattackDraw;
        ASSERT_LT(*step, check.cartridge.steps.size());
        EXPECT_NE(cellAt(check.cartridge.steps[*step], cells::kRunningCell) & cells::kCounterattackBit, 0);
        ASSERT_GT(*step, 0u);
        EXPECT_EQ(cellAt(check.cartridge.steps[*step - 1], cells::kRunningCell) & cells::kCounterattackBit, 0);
        EXPECT_FALSE(check.port.steps[*step - 1].counterattackRunning);
    });
}

// The commands a Berserk member may take (BerserkCmdTbl :757-760): Fight, Capture, Rage, Jump and Magitek;
// ChooseCmd clears every other (:697-706).
bool aBerserkCommand(std::uint8_t command) {
    using enum BattleCommandId;
    for (const BattleCommandId allowed : {FIGHT, CAPTURE, RAGE, JUMP, MAGITEK}) {
        if (command == static_cast<std::uint8_t>(allowed)) {
            return true;
        }
    }
    return false;
}

// A Berserk member chooses only among BerserkCmdTbl's commands: at every step where its first action waits,
// that action's command is one of them, on both sides.
TEST_F(BattleWholeOracleTest, ABerserkMembersActionsAreBerserkCommands) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleScenario scenario = theDraw(kBerserkDraw, content(), oracle.cartridge());
        const trace::BattleCheck check = checkedWhole(oracle, scenario);
        std::optional<battle::BattlerSlot> berserk;
        for (int position = 0; position < battle::kPartySlots && !berserk; ++position) {
            if (scenario.firstStep.statusesAdded[static_cast<std::size_t>(position)].has(StatusId::BERSERK)) {
                berserk = battle::BattlerSlot::character(position);
            }
        }
        ASSERT_TRUE(berserk.has_value()) << "draw " << kBerserkDraw << " lays Berserk on no member";
        int chosen = 0;
        for (std::size_t step = 0; step < std::min(check.port.steps.size(), check.cartridge.steps.size()); ++step) {
            const battle::Battler& ours = check.port.steps[step].at(*berserk);
            const std::uint8_t first = cellAt(check.cartridge.steps[step], battlerCell(cells::kFirstAction, *berserk));
            if (ours.actions.empty() || first == kNoActionSlot) {
                continue;
            }
            const auto* command = std::get_if<battle::CommandAction>(&ours.actions.front());
            ASSERT_NE(command, nullptr) << "step " << step;
            EXPECT_TRUE(aBerserkCommand(static_cast<std::uint8_t>(command->command))) << "step " << step;
            EXPECT_TRUE(aBerserkCommand(commandIn(check.cartridge.steps[step], first))) << "step " << step;
            ++chosen;
        }
        EXPECT_GT(chosen, 0) << "the Berserk member's action never waits at a step";
    });
}

// Whether a request is a script that shows `spell`.
bool showsSpell(const battle::BattleRequest& request, AttackId spell) {
    const auto* script = std::get_if<battle::RunBattleScript>(&request);
    return script != nullptr &&
           std::any_of(script->commands.begin(), script->commands.end(), [spell](const battle::BattleScriptCommand& command) {
               const auto* animation = std::get_if<battle::AttackAnimation>(&command);
               return animation != nullptr && animation->attack == spell;
           });
}

// A condemned count runs down by one on each of the battler's status timer turns while it is 2 or more, and
// on reaching 1 Doom is made an interrupting action with no caster (:14867-14874, CondemnDeath
// :15059-15062), which runs before the next step. At the last step the port's count stands at 2 the
// cartridge's $3B05,x is 2; at the next both are below 2; and the port's battle shows Doom after it.
TEST_F(BattleWholeOracleTest, ACondemnedCountRunningOutCastsDoom) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleCheck check = checkedWhole(oracle, theDraw(kCondemnedDraw, content(), oracle.cartridge()));
        std::optional<battle::BattlerSlot> condemned;
        std::optional<std::size_t> step;
        for (std::size_t at = 1; at < check.port.steps.size() && !step; ++at) {
            for (std::size_t index = 0; index < check.port.steps[at].battlers.size(); ++index) {
                if (check.port.steps[at - 1].battlers[index].condemnedNumber == 2 &&
                    check.port.steps[at].battlers[index].condemnedNumber < 2) {
                    condemned = battle::BattlerSlot::fromIndex(static_cast<int>(index));
                    step = at;
                }
            }
        }
        ASSERT_TRUE(step.has_value()) << "no condemned count runs out in draw " << kCondemnedDraw;
        ASSERT_LT(*step, check.cartridge.steps.size());
        RecordProperty("step", static_cast<int>(*step));
        RecordProperty("battler", condemned->index());
        const std::uint32_t count = battlerCell(cells::kCondemnedNumber, *condemned);
        EXPECT_EQ(cellAt(check.cartridge.steps[*step - 1], count), 2);
        EXPECT_LT(cellAt(check.cartridge.steps[*step], count), 2);
        EXPECT_EQ(cellAt(check.cartridge.steps[*step], count), check.port.steps[*step].at(*condemned).condemnedNumber);
        const auto from = check.port.requests.begin() + static_cast<std::ptrdiff_t>(check.port.stepRequests[*step - 1]);
        EXPECT_TRUE(std::any_of(from, check.port.requests.end(),
                                [](const battle::BattleRequest& request) { return showsSpell(request, AttackId::DOOM); }))
            << "the port shows no Doom after the count runs out";
    });
}

// The run buttons held until a member's escape succeeds: the escape marks the member in $3A38
// (:15137-15142), and the battle ends as the party has run. At the first step a member has escaped, the
// cartridge's $3A38 holds that member's bit; the port's battle ends with the party fled.
TEST_F(BattleWholeOracleTest, AFledBattleMarksTheMemberWhoEscaped) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleScenario scenario = theDraw(kFledDraw, content(), oracle.cartridge());
        ASSERT_TRUE(scenario.model.runButtonsHeldFrom.has_value());
        const trace::BattleCheck check = checkedWhole(oracle, scenario);
        const std::optional<std::size_t> step = firstStepWhere(
            check.port, [](const battle::BattleState& state) { return !state.partyJustEscaped.empty(); });
        ASSERT_TRUE(step.has_value()) << "no member escapes in draw " << kFledDraw;
        ASSERT_LT(*step, check.cartridge.steps.size());
        std::uint8_t escaped = 0;
        for (int position = 0; position < battle::kPartySlots; ++position) {
            if (check.port.steps[*step].partyJustEscaped.has(battle::BattlerSlot::character(position))) {
                escaped = static_cast<std::uint8_t>(escaped | battlerBit(battle::BattlerSlot::character(position)));
            }
        }
        EXPECT_EQ(cellAt(check.cartridge.steps[*step], cells::kPartyJustEscaped), escaped);
        ASSERT_TRUE(check.port.result.has_value());
        const auto* ended = std::get_if<battle::BattleEnded>(&check.port.result->outcome);
        ASSERT_NE(ended, nullptr);
        EXPECT_EQ(ended->reason, battle::BattleEndReason::RanOutOfTime);
    });
}

// A battle the field's countdown has run out on ends at its first end check (CheckBattleEnd :11900-11904):
// the cartridge keeps the flag in battle switch byte $3EBC, bit 5, as the battle closes, and the port's
// battle ends with the timer expired.
TEST_F(BattleWholeOracleTest, ATimedOutEventBattleEndsOnTheCountdown) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleCheck check = checkedWhole(oracle, theDraw(kCountdownDraw, content(), oracle.cartridge()));
        EXPECT_NE(check.cartridge.endCells[kBattleEndSwitches - cells::kWorkRam] & kTimerExpiredBit, 0);
        ASSERT_TRUE(check.port.result.has_value());
        const auto* ended = std::get_if<battle::BattleEnded>(&check.port.result->outcome);
        ASSERT_NE(ended, nullptr);
        EXPECT_EQ(ended->reason, battle::BattleEndReason::TimerExpired);
    });
}

// Gau turning up on the Veldt (GauAppears :12010-12017): his bit in the battlers to join ($2F4E) and among
// the characters acting as enemies ($3A40), $3A46 bit 2, and battle event $1B made an interrupting action,
// command $23 with the event as its attack (:12003-12006).
TEST_F(BattleWholeOracleTest, GauTurningUpQueuesHisEvent) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleCheck check = checkedWhole(oracle, theDraw(kGauDraw, content(), oracle.cartridge()));
        const std::optional<std::size_t> step =
            firstStepWhere(check.port, [](const battle::BattleState& state) { return state.gauJoining; });
        ASSERT_TRUE(step.has_value()) << "Gau does not turn up in draw " << kGauDraw;
        ASSERT_LT(*step, check.cartridge.steps.size());
        const trace::CartridgeStep& theirs = check.cartridge.steps[*step];
        const battle::BattleState& ours = check.port.steps[*step];
        std::uint16_t gau = 0;
        for (int position = 0; position < battle::kPartySlots; ++position) {
            if (ours.joiningAfterAction.has(battle::BattlerSlot::character(position))) {
                gau = battlerBit(battle::BattlerSlot::character(position));
            }
        }
        ASSERT_NE(gau, 0) << "the port joins no party slot";
        EXPECT_EQ(wordAt(theirs, cells::kJoining) & gau, gau);
        EXPECT_EQ(wordAt(theirs, cells::kEnemyCharacters) & gau, gau);
        EXPECT_NE(cellAt(theirs, cells::kAimFlags) & cells::kGauJoiningBit, 0);
        const std::uint8_t first = cellAt(theirs, cells::kFirstImmediateAction);
        ASSERT_NE(first, kNoActionSlot);
        EXPECT_EQ(commandIn(theirs, first), kBattleEventCommand);
        EXPECT_EQ(attackIn(theirs, first), battle::kGauJoinsEvent);
        ASSERT_FALSE(ours.immediateActions.empty());
        const auto* event = std::get_if<battle::BattleEventAction>(&ours.immediateActions.front());
        ASSERT_NE(event, nullptr);
        EXPECT_EQ(event->event, battle::kGauJoinsEvent);
    });
}

// A stage of the final battle won and the next begun: the start runs again, seeding the random numbers
// with four times the play clock's low byte (InitBattle :6088-6090), and the background scrolls by
// $3EE1 (:6180-6193). At the next stage's first step, both sides hold its formation and the scroll, and
// every step after agrees.
TEST_F(BattleWholeOracleTest, AFinalStageScrollsOnToTheNext) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleCheck check = checkedWhole(oracle, theDraw(kFinalStageDraw, content(), oracle.cartridge()));
        const std::optional<std::size_t> step = firstStepWhere(
            check.port, [](const battle::BattleState& state) { return state.formation != state.startingFormation; });
        ASSERT_TRUE(step.has_value()) << "no next stage begins in draw " << kFinalStageDraw;
        ASSERT_LT(*step, check.cartridge.steps.size());
        const trace::CartridgeStep& theirs = check.cartridge.steps[*step];
        const battle::BattleState& ours = check.port.steps[*step];
        EXPECT_EQ(wordAt(theirs, cells::kFormationCell), static_cast<std::uint16_t>(ours.formation));
        ASSERT_TRUE(ours.finalBattleScroll.has_value());
        EXPECT_EQ(cellAt(theirs, cells::kFinalBattleScroll), *ours.finalBattleScroll);
        const bool scrolled = std::any_of(check.port.requests.begin(), check.port.requests.end(), [](const battle::BattleRequest& r) {
            return std::holds_alternative<battle::ScrollFinalBattleBackground>(r);
        });
        EXPECT_TRUE(scrolled);
    });
}

// The slots of the battle's one list in use, highest first: a slot whose next slot is not free
// (AddToQueue :12815-12826).
std::vector<std::uint8_t> slotsInUse(const trace::BattleCells& cells) {
    std::vector<std::uint8_t> used;
    for (std::uint32_t slot = cells::kActionSlots; slot-- > 0;) {
        if (cells[cells::kActionNext - cells::kWorkRam + slot] != cells::kFreeSlot) {
            used.push_back(static_cast<std::uint8_t>(slot));
        }
    }
    return used;
}

// Whether `used`, highest first, is the block a machine written from an empty list takes: 127 and down.
bool fromTheTopDown(const std::vector<std::uint8_t>& used) {
    for (std::size_t k = 0; k < used.size(); ++k) {
        if (used[k] != cells::kActionSlots - 1 - k) {
            return false;
        }
    }
    return true;
}

// The battle's list as a battle leaves it mid-way, with slots freed between slots in use, written into a
// machine: the writer lays the lists from slot 127 down, and the machine's cells read back as the state
// written. The state is the first of the sweep's drawn battles' steps whose cartridge list is not the block
// from 127 down and holds two actions or more.
TEST_F(BattleWholeOracleTest, AStateWithItsListOutOfOrderWrittenIntoAMachineReadsBackTheSame) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        std::mt19937 rng{trace::kSweepSeed};
        std::optional<battle::BattleState> taken;
        trace::PartyCells boosts;
        for (int drawn = 0; drawn < 10 && !taken; ++drawn) {
            const trace::BattleScenario scenario =
                trace::drawBattleScenario(rng, content().text, content().monsterSprites, oracle.cartridge());
            const trace::BattleCheck check = oracle.checkBattle(scenario);
            ASSERT_EQ(check.differences, "") << "draw " << drawn;
            const std::size_t steps = std::min(check.port.steps.size(), check.cartridge.steps.size());
            for (std::size_t step = 0; step < steps && !taken; ++step) {
                const std::vector<std::uint8_t> used = slotsInUse(check.cartridge.steps[step].cells);
                if (used.size() >= 2 && !fromTheTopDown(used)) {
                    taken = check.port.steps[step];
                    boosts = scenario.start.cells;
                    RecordProperty("draw", drawn);
                    RecordProperty("step", static_cast<int>(step));
                    RecordProperty("slots in use", static_cast<int>(used.size()));
                }
            }
        }
        ASSERT_TRUE(taken.has_value()) << "no step of ten drawn battles leaves its list out of order";

        DamageOracle written{image()};
        ready(written);
        written.writeBattleState(*taken, boosts);
        const std::vector<std::uint8_t> read =
            written.machine().read(retropp::MemoryRegion{.at = cells::kWorkRam, .size = sizeof(trace::BattleCells)});
        trace::BattleCells cells{};
        std::copy(read.begin(), read.end(), cells.begin());
        const std::vector<std::uint8_t> used = slotsInUse(cells);
        EXPECT_GE(used.size(), 2u);
        EXPECT_TRUE(fromTheTopDown(used));
        EXPECT_EQ(written.compareBattleState(*taken, boosts, cells), "");
    });
}

// Whether each Chain Saw the port's battle worked killed, in the order they were worked.
std::vector<bool> chainSawKills(const trace::PortBattle& port) {
    std::vector<bool> kills;
    for (const battle::BattleRequest& request : port.requests) {
        const auto* script = std::get_if<battle::RunBattleScript>(&request);
        if (script == nullptr) {
            continue;
        }
        for (const battle::BattleScriptCommand& command : script->commands) {
            const auto* animation = std::get_if<battle::AttackAnimation>(&command);
            const auto* what = animation != nullptr ? std::get_if<BattleCommandId>(&animation->what) : nullptr;
            if (what != nullptr && *what == BattleCommandId::TOOLS && animation->item == ItemId::CHAIN_SAW) {
                kills.push_back(animation->chainSawKill);
            }
        }
    }
    return kills;
}

// A draw of the sweep's whose Chain Saw kills otherwise with the byte above the random numbers' position at
// 1 than at 0.
constexpr int kChainSawDraw = 12;

// The Chain Saw's roll reads the RNG table at the position plus 256 × the byte above it (Rand :12378-12384,
// called with the index registers sixteen bits wide from ToolsEffect_03 :7127, CalcItemEffect :7015-7091).
// Draw kChainSawDraw of the sweep's, whose Chain Saw rolls where the table and the 256 bytes past it differ
// in their low two bits, started with the byte at 1: the cartridge and the port agree to the battle's end.
TEST_F(BattleWholeOracleTest, AChainSawsRollWithTheByteAboveThePositionAtOneReadsPastTheTable) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        std::mt19937 rng{trace::kSweepSeed};
        trace::BattleScenario pastTheTable;
        for (int draw = 0; draw <= kChainSawDraw; ++draw) {
            pastTheTable = trace::drawBattleScenario(rng, content().text, content().monsterSprites, oracle.cartridge());
        }
        trace::BattleScenario inTheTable = pastTheTable;
        trace::setByteAboveRngPosition(pastTheTable.start, oracle.cartridge(), 1);
        trace::setByteAboveRngPosition(inTheTable.start, oracle.cartridge(), 0);
        const std::vector<bool> kills = chainSawKills(trace::portBattle(pastTheTable, content().text, content().monsterSprites));
        ASSERT_FALSE(kills.empty()) << "draw " << kChainSawDraw << " works no Chain Saw";
        EXPECT_NE(kills, chainSawKills(trace::portBattle(inTheTable, content().text, content().monsterSprites)))
            << "draw " << kChainSawDraw << "'s Chain Saw kills alike with the byte at 1 and at 0";
        const trace::BattleCheck check = oracle.checkBattle(pastTheTable);
        EXPECT_EQ(check.differences, "") << trace::describe(pastTheTable);
        EXPECT_TRUE(check.cartridge.ended);
    });
}

// The action pool's top slot, the first AddToQueue hands out (:12815-12826).
constexpr std::uint8_t kTopActionSlot = cells::kActionSlots - 1;

// Where an action slot keeps its targets: the party's byte, then the monsters'.
constexpr std::uint32_t actionTargets(std::uint8_t slot) { return cells::kActionTargets + 2u * slot; }

// The sweep's draw whose Jump ready stance aims at another action's targets: a colosseum match.
constexpr int kJumpStanceDraw = 92;

// A Jump ready stance the cartridge played: the jumper, the loop step at whose frame wait it was still to
// come, and the targets it carries, as the word the block holds them in (the party's byte, then the monsters').
struct JumpStance {
    battle::BattlerSlot actor = battle::BattlerSlot::character(0);
    std::size_t stepBefore = 0;
    std::uint16_t targets = 0;
};

// Every Jump ready stance of the cartridge's scripts that a loop step's frame wait comes before, in order.
std::vector<JumpStance> jumpStances(const trace::WholeBattleResult& battle) {
    std::vector<JumpStance> stances;
    for (const auto& [call, script] : battle.scripts) {
        for (const trace::ScriptCommandRead& read : script) {
            const auto* stance = read.command ? std::get_if<battle::ReadyStance>(&*read.command) : nullptr;
            if (stance == nullptr || stance->command != BattleCommandId::JUMP || battle.steps.empty() ||
                battle.steps.front().firstCall >= call) {
                continue;
            }
            std::size_t before = 0;
            while (before + 1 < battle.steps.size() && battle.steps[before + 1].firstCall < call) {
                ++before;
            }
            stances.push_back(JumpStance{
                .actor = stance->actor,
                .stepBefore = before,
                .targets = static_cast<std::uint16_t>(stance->targets.characters.bits | stance->targets.monsters.bits << 8)});
        }
    }
    return stances;
}

// A Jump's ready stance aims at the targets of the action pool's top slot, whatever action holds or last held
// it, and not at the Jump's own (_gaugefull :5743-5789: UpdateCharGfxBuf :15256-15276 leaves Y at $FE for
// :5779's read of $3520,y), and the command comparison leaves those targets out (docs/Bugs.md). On the sweep's
// draw 92 the top slot is read at the frame wait of the step before the stance's, its link, command, attack and
// targets recorded, and the Jump's own targets at the step after, from the jumper's first action. The stance
// carries the top slot's targets, they are not the Jump's own, and the battle agrees with the port to its end.
TEST_F(BattleWholeOracleTest, AJumpsReadyStanceAimsAtTheTopSlotsTargets) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        const trace::BattleScenario drawn = theDraw(kJumpStanceDraw, content(), oracle.cartridge());
        const trace::BattleCheck check = oracle.checkBattle(drawn);
        EXPECT_EQ(check.differences, "") << trace::describe(drawn);
        const std::vector<JumpStance> stances = jumpStances(check.cartridge);
        ASSERT_FALSE(stances.empty()) << "draw " << kJumpStanceDraw << " plays no Jump ready stance";
        const JumpStance& stance = stances.front();
        ASSERT_LT(stance.stepBefore + 1, check.cartridge.steps.size()) << "no loop step follows the stance";
        const trace::CartridgeStep& before = check.cartridge.steps[stance.stepBefore];
        const trace::CartridgeStep& after = check.cartridge.steps[stance.stepBefore + 1];
        RecordProperty("draw 92's step before the stance", static_cast<int>(stance.stepBefore));
        RecordProperty("draw 92's top slot's link", int{cellAt(before, cells::kActionNext + kTopActionSlot)});
        RecordProperty("draw 92's top slot's command", int{commandIn(before, kTopActionSlot)});
        RecordProperty("draw 92's top slot's attack", int{attackIn(before, kTopActionSlot)});
        RecordProperty("draw 92's top slot's targets", int{wordAt(before, actionTargets(kTopActionSlot))});

        const std::uint8_t jump = cellAt(after, battlerCell(cells::kFirstAction, stance.actor));
        ASSERT_EQ(commandIn(after, jump), static_cast<std::uint8_t>(BattleCommandId::JUMP))
            << "the jumper's first action, slot " << int{jump} << ", is not its Jump";
        RecordProperty("draw 92's Jump's slot", int{jump});
        RecordProperty("draw 92's Jump's targets", int{wordAt(after, actionTargets(jump))});

        EXPECT_EQ(stance.targets, wordAt(before, actionTargets(kTopActionSlot)));
        EXPECT_NE(stance.targets, wordAt(after, actionTargets(jump)));
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
            const trace::BattleScenario scenario =
                trace::drawBattleScenario(rng, content().text, content().monsterSprites, oracle.cartridge());
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
