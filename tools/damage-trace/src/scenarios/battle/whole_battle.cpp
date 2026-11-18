// The port's whole battle over a scenario: BattleSim ticked one frame at a time as the scenario's model
// spends them.

#include "scenarios/battle/whole_battle.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "battle/battle_hooks.h"
#include "battle/battle_script.h"
#include "battle/battle_sim.h"
#include "battle/command_executor.h"
#include "data/formations.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

#include "cells/damage/per_target.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/graphics_commands.h"

namespace ostinato::trace {

namespace {

// The battle events that leave battle time running (GfxCmd_0f btlgfx_main.asm:38204-38208): Gau's
// arrival and Wrexsoul's.
constexpr std::uint8_t kGauEvent = 0x1B;
constexpr std::uint8_t kWrexsoulEvent = 0x1F;
// The script command that plays a battle event (Cmd_23 :13023), and the queue's end.
constexpr std::uint8_t kBattleEventCommand = 0x0F;
constexpr std::uint8_t kEndOfScript = 0xFF;

bool eventStopsTime(std::uint8_t event) {
    return event != kGauEvent && event != kWrexsoulEvent;
}

}  // namespace

bool scriptStopsTime(const std::vector<std::uint8_t>& queue) {
    for (std::size_t at = 0; at + 1 < queue.size() && queue[at] != kEndOfScript; at += cells::kQueueCommandSize) {
        if (queue[at] == kBattleEventCommand && eventStopsTime(queue[at + 1])) {
            return true;
        }
    }
    return false;
}

bool scriptStopsTime(const battle::RunBattleScript& script) {
    for (const battle::BattleScriptCommand& command : script.commands) {
        if (const auto* event = std::get_if<battle::BattleEventPlays>(&command); event && eventStopsTime(event->event)) {
            return true;
        }
    }
    return false;
}

namespace {

std::string menu(const char* what, int position, bool controlling = false) {
    return std::string{what} + " menu " + std::to_string(position) + (controlling ? " controlling" : "");
}

}  // namespace

std::string requestShown(const battle::BattleRequest& request) {
    return std::visit(
        [](const auto& shown) -> std::string {
            using Shown = std::decay_t<decltype(shown)>;
            if constexpr (std::is_same_v<Shown, battle::OpenBattleMenu>) {
                return menu("open", shown.character.position(), shown.controllingMonster);
            } else if constexpr (std::is_same_v<Shown, battle::CloseBattleMenu>) {
                return menu("close", shown.character.position());
            } else if constexpr (std::is_same_v<Shown, battle::RefreshBattleMenu>) {
                return menu("refresh", shown.character.position());
            } else if constexpr (std::is_same_v<Shown, battle::RunBattleScript>) {
                return "script";
            } else if constexpr (std::is_same_v<Shown, battle::RefreshMonsterNames>) {
                return "monster names";
            } else if constexpr (std::is_same_v<Shown, battle::OpenBattleScene>) {
                return "open the scene";
            } else if constexpr (std::is_same_v<Shown, battle::ScrollFinalBattleBackground>) {
                return "scroll the background";
            } else if constexpr (std::is_same_v<Shown, battle::CloseBattleScene>) {
                return "close the scene";
            } else if constexpr (std::is_same_v<Shown, battle::AddItemObtained>) {
                return "item obtained " + std::to_string(static_cast<int>(shown.item));
            } else {
                return "fanfare";
            }
        },
        request);
}

std::optional<std::string> callShown(const GraphicsCall& call) {
    using namespace graphics;
    switch (call.command) {
        case kOpenBattleScene: return "open the scene";
        case kOpenMenu:
            return menu("open", call.slot & ~kControllingMonsterBit, (call.slot & kControllingMonsterBit) != 0);
        case kCloseMenu: return menu("close", call.slot);
        case kPlayBattleScript: return "script";
        case kAddItemObtained: return "item obtained " + std::to_string(call.item[0]);
        case kRefreshMonsterNames: return "monster names";
        case kVictory: return "celebration";
        case kCloseBattle: return "close the scene";
        case kRefreshMenu: return menu("refresh", call.slot);
        case kScrollFinalBattle: return "scroll the background";
        case kOneFrame:
        case kMergeItemsObtained: return std::nullopt;
        default: return "graphics command " + std::to_string(call.command);
    }
}

void layFirstStep(battle::BattleState& state, const FirstStepLaid& laid) {
    for (std::size_t index = 0; index < state.battlers.size(); ++index) {
        battle::Battler& battler = state.battlers[index];
        const StatusSet& added = laid.statusesAdded[index];
        for (std::size_t byte = 0; byte < battler.status.bytes.size(); ++byte) {
            battler.status.bytes[byte] = static_cast<std::uint8_t>(battler.status.bytes[byte] | added.bytes[byte]);
        }
        // What Muddled's set handler leaves beside the bit (SetStatus_0d :11366-11383): the battler drawn
        // facing the other way, and a party member's statuses marked changed. Berserk's leaves nothing
        // (SetStatusTbl :11673, SetStatusNoEffect).
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(index));
        if (added.has(StatusId::CONFUSE)) {
            state.flippedByControl.add(slot);
            if (slot.isCharacter()) {
                state.statusChanged.add(slot);
            }
        }
        if (laid.hp[index]) {
            battler.hp = *laid.hp[index];
        }
    }
}

PortBattle portBattle(const BattleScenario& scenario, const TextCorpus& text, const MonsterSprites& sprites) {
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, text, sprites};
    battle::BattleSim sim{scenario.start.start, scenario.start.cells.party, scenario.start.switches, text, sprites,
                          executor};
    PortBattle port;
    // The frames the presentation still has to play: the opening's and the scripts'. A tick with none
    // left is the loop's frame wait.
    std::deque<Frame> playing;
    std::size_t scripts = 0;
    const auto take = [&] {
        for (battle::BattleRequest& request : sim.takeRequests()) {
            if (std::holds_alternative<battle::OpenBattleScene>(request)) {
                for (const Frame frame : scenario.model.openingFrames()) {
                    playing.push_back(frame);
                }
            } else if (const auto* script = std::get_if<battle::RunBattleScript>(&request)) {
                for (const Frame frame : scenario.model.scriptFrames(scripts++, scriptStopsTime(*script))) {
                    playing.push_back(frame);
                }
            }
            port.requests.push_back(std::move(request));
        }
    };
    take();
    // What the graphics program's interrupt sets as a frame begins: the countdown run out, and the party
    // running; and the facings it hands over, kept for the cartridge's run.
    const auto beginFrame = [&](Frame played) {
        battle::BattleState& state = sim.state();
        port.facings.push_back(detail::maskOf(state.facingRight));
        if (scenario.model.timerRunsOutAt == port.frames) {
            state.eventTimerExpired = true;
        }
        state.partyRunning =
            scenario.model.partyRuns(port.frames, getFormationAux(state.formation).runningDisabled(), played);
    };
    // The opening plays out before the loop's first frame wait: none of its frames is the battle's.
    const std::size_t openingFrames = playing.size();
    for (std::size_t frame = 0; frame < openingFrames && !sim.result(); ++frame) {
        const Frame played = playing.front();
        playing.pop_front();
        beginFrame(played);
        sim.tick({.timeRunning = played.clockCalled, .presentationIdle = false, .playerActions = {}});
        ++port.frames;
        take();
    }
    while (!sim.result()) {
        if (port.frames >= scenario.frameCap) {
            port.timedOut = true;
            break;
        }
        Frame played{.clockCalled = true};
        bool idle = true;
        if (playing.empty()) {
            if (port.steps.empty()) {
                layFirstStep(sim.state(), scenario.firstStep);
            }
            port.steps.push_back(sim.state());
            port.stepFrames.push_back(port.frames);
            port.stepRequests.push_back(port.requests.size());
        } else {
            played = playing.front();
            playing.pop_front();
            // The script's last frame is the one the battle goes on from.
            idle = playing.empty();
        }
        beginFrame(played);
        sim.tick({.timeRunning = played.clockCalled, .presentationIdle = idle, .playerActions = {}});
        ++port.frames;
        take();
    }
    port.result = sim.result();
    return port;
}

}  // namespace ostinato::trace
