// The port's runs of a script read back as the cartridge's lists hold them, the script's places,
// the differences between two results, and the run sweep's draw.

#include "scenarios/ai/ai_run.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "ai_script_bytes.h"
#include "battle/ai.h"
#include "battle/attack.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/command_executor.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/char_ai.h"
#include "data/monster_sprites.h"
#include "fixtures/ai_scripts_expected.h"
#include "ostinato/monster_animation.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"

#include "cells/ai/ai.h"
#include "scenarios/ai/ai_run_shared.h"
#include "scenarios/ai/ai_targets.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/status/attacker_effects_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {

// The command each AI-only action goes out as.
constexpr std::uint8_t kFormationChangeCommand = 0x20;
constexpr std::uint8_t kMonsterDialogCommand = 0x21;
constexpr std::uint8_t kBattleEventCommand = 0x23;
constexpr std::uint8_t kMonsterVisibilityCommand = 0x24;
constexpr std::uint8_t kMonsterAnimationCommand = 0x2B;
constexpr std::uint8_t kVariableCommand = 0x2E;
constexpr std::uint8_t kSwitchCommand = 0x2F;
constexpr std::uint8_t kEffectCommand = 0x30;
}  // namespace

namespace detail {

const std::map<const AiStep*, std::uint16_t>& stepPlaces() {
    static const std::map<const AiStep*, std::uint16_t> places = [] {
        std::map<const AiStep*, std::uint16_t> map;
        for (const test::ExpectedAiScript& row : test::kExpectedAiScripts) {
            const AiScript& script = aiScript(row.id);
            const auto add = [&](std::span<const AiStep> part, std::uint16_t start) {
                std::uint16_t at = start;
                for (const AiStep& step : part) {
                    map.emplace(&step, at);
                    at = static_cast<std::uint16_t>(at + test::aiStepBytes(step).size());
                }
            };
            add(script.turn, row.turnOffset);
            if (row.counterOffset) {
                add(script.counter, *row.counterOffset);
            }
        }
        return map;
    }();
    return places;
}
}  // namespace detail

namespace {

std::uint16_t placeOf(const AiStep* step) {
    if (step == nullptr) {
        return 0;
    }
    const auto found = stepPlaces().find(step);
    if (found == stepPlaces().end()) {
        throw std::logic_error("a script place that is no step of the cartridge's scripts");
    }
    return found->second;
}

std::uint16_t partPlace(std::span<const AiStep> part) {
    return part.empty() ? kNoPlace : placeOf(part.data());
}
}  // namespace

namespace detail {

std::string cellsText(const ActionCells& cells) {
    const auto byteText = [](const std::optional<std::uint8_t>& value) {
        return value ? std::to_string(*value) : std::string{"--"};
    };
    return std::to_string(cells.command) + " " + byteText(cells.attack) + " " + byteText(cells.targetsLow) + "/" +
           byteText(cells.targetsHigh) + " " + std::to_string(cells.mpCost);
}

ActionCells readCells(std::uint8_t command, std::uint8_t attack, std::uint16_t targets, std::uint8_t mpCost) {
    ActionCells cells{.command = command, .attack = attack, .mpCost = mpCost};
    if (command == kMonsterDialogCommand || command == kBattleEventCommand) {
        return cells;
    }
    cells.targetsLow = static_cast<std::uint8_t>(targets & 0xFF);
    if (command != kVariableCommand && command != kEffectCommand) {
        cells.targetsHigh = static_cast<std::uint8_t>(targets >> 8);
    }
    return cells;
}

std::vector<std::uint8_t> stepBytesFor(const battle::BattleVariableAction& a) {
    switch (a.change) {
    case AiVariableChange::Set:
        return test::aiStepBytes(AiStep::setVariable({.variable = a.variable, .value = a.amount}));
    case AiVariableChange::Add:
        return test::aiStepBytes(AiStep::addToVariable({.variable = a.variable, .amount = a.amount}));
    default:
        return test::aiStepBytes(AiStep::subtractFromVariable({.variable = a.variable, .amount = a.amount}));
    }
}

std::vector<std::uint8_t> stepBytesFor(const battle::BattleSwitchAction& a) {
    return test::aiStepBytes(AiStep{ai::ChangeSwitch{a.which, a.change}});
}

std::vector<std::uint8_t> stepBytesFor(const battle::AiEffectAction& a) {
    return test::aiStepBytes(AiStep{ai::Effect{.effect = a.effect, .target = a.target, .status = a.status}});
}
}  // namespace detail

namespace {

// A step that queues an action of its own puts its first operand in the attack byte and the next
// two in the targets word.
ActionCells stepCells(std::uint8_t command, const std::vector<std::uint8_t>& bytes) {
    const auto at = [&](std::size_t i) -> std::uint8_t { return i < bytes.size() ? bytes[i] : 0; };
    return readCells(command, at(1), static_cast<std::uint16_t>(at(2) | at(3) << 8), 0);
}

AiRunResult portRunResult(const battle::BattleState& state, battle::BattlerSlot actor) {
    const battle::Battler& battler = state.at(actor);
    AiRunResult result;
    for (const battle::Action& action : battler.actions) {
        result.actions.push_back(listedActionText(action));
    }
    for (const battle::Action& action : battler.counterActions) {
        result.counterActions.push_back(listedActionText(action));
    }
    result.advanceWait = battler.advanceWait;
    result.script = portScriptPlaces(state, actor);
    result.targetingTarget = battler.targetingTarget;
    result.counterAiRanThisStep = state.counterAiRanThisStep;
    result.diedAwaitingCounter = state.diedAwaitingCounter;
    result.attackedThisStep = state.attackedThisStep;
    for (std::size_t i = 0; i < state.counterQueue.size(); ++i) {
        result.counterQueue.push_back(state.counterQueue.at(i).index());
    }
    result.rngPosition = state.rng.position();
    return result;
}
}  // namespace

std::string listedActionText(const battle::Action& action) {
    return std::visit(
        [](const auto& a) -> std::string {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, battle::CommandAction>) {
                return cellsText(readCells(static_cast<std::uint8_t>(a.command), static_cast<std::uint8_t>(a.attack),
                                           maskOf(a.targets), a.mpCost));
            } else if constexpr (std::is_same_v<T, battle::FormationChange>) {
                return cellsText(stepCells(kFormationChangeCommand,
                                           test::aiStepBytes(AiStep::changeBattle({.formation = a.formation,
                                                                                    .exit = a.exit,
                                                                                    .restoreMonsters = a.restoreMonsters,
                                                                                    .scrollBackground = a.scrollBackground}))));
            } else if constexpr (std::is_same_v<T, battle::MonsterDialogAction>) {
                return cellsText(stepCells(kMonsterDialogCommand, test::aiStepBytes(AiStep::dialog(a.dialog))));
            } else if constexpr (std::is_same_v<T, battle::BattleEventAction>) {
                return cellsText(stepCells(kBattleEventCommand, test::aiStepBytes(AiStep::battleEvent(a.event))));
            } else if constexpr (std::is_same_v<T, battle::MonsterVisibilityAction>) {
                return cellsText(readCells(kMonsterVisibilityCommand, static_cast<std::uint8_t>(a.type),
                                           static_cast<std::uint16_t>(static_cast<std::uint8_t>(a.mode) |
                                                                      a.monsters.monsters.bits << 8),
                                           0));
            } else if constexpr (std::is_same_v<T, battle::MonsterAnimationAction>) {
                const bool sound = a.animation == MonsterAnimation::SoundEffect;
                const std::uint16_t targets =
                    sound ? static_cast<std::uint16_t>(a.pan | a.soundEffect << 8) : a.monsters.monsters.bits;
                return cellsText(readCells(kMonsterAnimationCommand, static_cast<std::uint8_t>(a.animation), targets, 0));
            } else if constexpr (std::is_same_v<T, battle::BattleVariableAction>) {
                return cellsText(stepCells(kVariableCommand, stepBytesFor(a)));
            } else if constexpr (std::is_same_v<T, battle::BattleSwitchAction>) {
                return cellsText(stepCells(kSwitchCommand, stepBytesFor(a)));
            } else if constexpr (std::is_same_v<T, battle::AiEffectAction>) {
                return cellsText(stepCells(kEffectCommand, stepBytesFor(a)));
            } else {
                return "an action no script queues";
            }
        },
        action);
}

AiScriptPlaces portScriptPlaces(const battle::BattleState& state, battle::BattlerSlot slot) {
    const battle::Battler& battler = state.at(slot);
    return AiScriptPlaces{.turn = partPlace(battler.turnScript),
                          .counter = partPlace(battler.counterScript),
                          .turnSkipped = battler.turnResume.blocksSkipped,
                          .turnWaitPlace = placeOf(battler.turnResume.at),
                          .counterSkipped = battler.counterResume.blocksSkipped,
                          .counterWaitPlace = placeOf(battler.counterResume.at)};
}

std::string differences(const AiScriptPlaces& port, const AiScriptPlaces& guest) {
    EffectCells ours;
    EffectCells theirs;
    const auto put = [](EffectCells& out, const AiScriptPlaces& places) {
        out.put("turn part", places.turn);
        out.put("counterattack part", places.counter);
        out.put("turn's blocks skipped", places.turnSkipped);
        out.put("turn's wait place", places.turnWaitPlace);
        out.put("counterattack's blocks skipped", places.counterSkipped);
        out.put("counterattack's wait place", places.counterWaitPlace);
    };
    put(ours, port);
    put(theirs, guest);
    return cellDifferences(ours.cells, theirs.cells);
}

AiRunResult portAiTurn(battle::BattleState& state, battle::BattlerSlot actor) {
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, noText(), MonsterSprites{}};
    state.counterattackRunning = false;
    battle::runAiTurn(state, executor, actor);
    return portRunResult(state, actor);
}

AiRunResult portAiTurn(AiScenario scenario) {
    return portAiTurn(scenario.state, scenario.actor);
}

AiRunResult portAiCounter(AiScenario scenario) {
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, noText(), MonsterSprites{}};
    scenario.state.counterattackRunning = true;
    battle::runAiCounter(scenario.state, executor, scenario.actor, scenario.answering);
    return portRunResult(scenario.state, scenario.actor);
}

std::string differences(const AiRunResult& port, const AiRunResult& guest) {
    EffectCells ours;
    EffectCells theirs;
    const auto put = [](EffectCells& out, const AiRunResult& result) {
        const auto list = [](const std::vector<std::string>& actions) {
            std::string text;
            for (const std::string& action : actions) {
                text += "[" + action + "]";
            }
            return text;
        };
        out.put("actions", list(result.actions));
        out.put("counterattacks", list(result.counterActions));
        out.put("advance wait", result.advanceWait ? std::to_string(*result.advanceWait) : std::string{"unset"});
        out.put("turn part", result.script.turn);
        out.put("counterattack part", result.script.counter);
        out.put("turn's blocks skipped", result.script.turnSkipped);
        out.put("turn's wait place", result.script.turnWaitPlace);
        out.put("counterattack's blocks skipped", result.script.counterSkipped);
        out.put("counterattack's wait place", result.script.counterWaitPlace);
        out.put("Targetting", markText(result.targetingTarget));
        out.put("counterattack parts run", setText(result.counterAiRanThisStep));
        out.put("died awaiting a counterattack", setText(result.diedAwaitingCounter));
        out.put("attacked this step", setText(result.attackedThisStep));
        std::string line;
        for (const int index : result.counterQueue) {
            line += std::to_string(index) + " ";
        }
        out.put("counterattack line", line);
        out.put("random-number position", result.rngPosition);
    };
    put(ours, port);
    put(theirs, guest);
    return cellDifferences(ours.cells, theirs.cells);
}

AiScenario drawAiRunScenario(std::mt19937& rng, std::uint16_t script, battle::BattlerSlot actor, CounterGuard guard) {
    AiScenario s = drawAiScenario(rng);
    s.actor = actor;
    s.run = {};
    battle::BattleState& state = s.state;
    // The battler's aim reads the reach flag the way the shipped aim does only while no one is
    // away from the field.
    if (!state.awayFromTheFieldUnreachable) {
        state.awayFromTheField = {};
    }
    battle::Battler& battler = state.at(actor);
    battler.present = true;
    if (actor.isMonster() && !battler.monster) {
        battler.monster = static_cast<MonsterId>(rng() % kMonsterCount);
    }
    if (oneIn(rng, 2)) {
        battler.advanceWait = static_cast<std::uint8_t>(upTo(rng, 20));
    }
    battler.aiScript = script;
    battle::readyAi(state, actor);

    // The other battlers' marks in the three sets, and the actor's on the guard's path.
    state.counterAiRanThisStep = anyBattlers(rng).without(battle::BattlerSet::of(actor));
    state.diedAwaitingCounter = anyBattlers(rng).without(battle::BattlerSet::of(actor));
    state.attackedThisStep = anyBattlers(rng).without(battle::BattlerSet::of(actor));
    battle::BattlerSlot other = anySlot(rng);
    while (other == actor) {
        other = anySlot(rng);
    }
    switch (guard) {
    case CounterGuard::AlreadyRan:
        state.counterAiRanThisStep.add(actor);
        break;
    case CounterGuard::DiedAfterAnAttack:
        state.diedAwaitingCounter.add(actor);
        state.attackedThisStep.add(actor);
        break;
    case CounterGuard::DiedUnattacked:
        state.diedAwaitingCounter.add(actor);
        break;
    case CounterGuard::QuickForAnother:
        state.quickTarget = other;
        state.quickTurnsLeft = static_cast<std::uint8_t>(1 + upTo(rng, 2));
        break;
    case CounterGuard::HeldByAStatus: {
        constexpr std::array<StatusId, 5> held{StatusId::SLEEP, StatusId::CONFUSE, StatusId::BERSERK, StatusId::STOP,
                                              StatusId::FROZEN};
        battler.statusForCounter.set(held[rng() % held.size()]);
        break;
    }
    case CounterGuard::Charmed:
        battler.charmedBy = other;
        break;
    case CounterGuard::Free:
        break;
    case CounterGuard::QuickForItself:
        state.quickTarget = actor;
        state.quickTurnsLeft = static_cast<std::uint8_t>(1 + upTo(rng, 2));
        break;
    }
    if (oneIn(rng, 4)) {
        s.answering = {};
    } else {
        s.answering = battle::BattlerSet::of(other);
    }
    return s;
}

}  // namespace ostinato::trace
