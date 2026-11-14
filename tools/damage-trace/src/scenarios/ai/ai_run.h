// A battler's script as the cartridge keeps it and a turn's or a counterattack's run of it: what each
// leaves, the port's answer, the comparison, the paths a counterattack's guard takes and the run
// sweep's draw.
#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"

#include "scenarios/ai/ai_targets.h"

namespace ostinato::trace {

// One battler's script as the cartridge keeps it: where its turn part and its counterattack part
// start, each counted in bytes from the start of the cartridge's script block ($FFFF for none), and
// for each part the blocks skipped and the place to go on from when it last stopped at a wait (the
// place 0 when it has never stopped at one).
struct AiScriptPlaces {
    std::uint16_t turn = 0;
    std::uint16_t counter = 0;
    std::uint8_t turnSkipped = 0;
    std::uint16_t turnWaitPlace = 0;
    std::uint8_t counterSkipped = 0;
    std::uint16_t counterWaitPlace = 0;
};

// The port's script for `slot` over the scenario, as the cartridge keeps it.
[[nodiscard]] AiScriptPlaces portScriptPlaces(const battle::BattleState& state, battle::BattlerSlot slot);

[[nodiscard]] std::string differences(const AiScriptPlaces& port, const AiScriptPlaces& guest);

// What a turn or a counterattack's run of a script leaves, as the same fields on both sides: the
// battler's actions and counterattacks as the cartridge's lists hold them (each "command attack
// targets MP", with "--" for the bytes the command never reads), its advance wait, its script's
// places, its Targetting mark, the three sets a counterattack's guard reads and changes, the
// counterattack line by battler and the random-number position.
struct AiRunResult {
    std::vector<std::string> actions;
    std::vector<std::string> counterActions;
    std::optional<std::uint8_t> advanceWait;
    AiScriptPlaces script;
    std::optional<battle::BattlerSlot> targetingTarget;
    battle::BattlerSet counterAiRanThisStep;
    battle::BattlerSet diedAwaitingCounter;
    battle::BattlerSet attackedThisStep;
    std::vector<int> counterQueue;
    std::uint8_t rngPosition = 0;
};

// One action as the cartridge's lists hold it, with "--" for the bytes its command never reads.
[[nodiscard]] std::string listedActionText(const battle::Action& action);

// The port's turn (battle::runAiTurn) and counterattack (battle::runAiCounter, with a counterattack
// running) over the scenario, read back as an AiRunResult. The commands are aimed by CommandExecutor.
[[nodiscard]] AiRunResult portAiTurn(AiScenario scenario);
[[nodiscard]] AiRunResult portAiCounter(AiScenario scenario);

// The port's turn for `actor` on `state`, which keeps what the run leaves — for turns run one after
// another.
[[nodiscard]] AiRunResult portAiTurn(battle::BattleState& state, battle::BattlerSlot actor);

[[nodiscard]] std::string differences(const AiRunResult& port, const AiRunResult& guest);

// The ways a counterattack's guard can find the battler, each a different path through it.
enum class CounterGuard : std::uint8_t {
    AlreadyRan,           // its counterattack part has run this step
    DiedAfterAnAttack,    // it died this action, and another attacked it this step
    DiedUnattacked,       // it died this action with no one else attacking it
    QuickForAnother,      // Quick is running for another battler
    HeldByAStatus,        // it answers with Sleep, Confuse, Berserk, Stop or Frozen
    Charmed,
    Free,
    QuickForItself,
};
inline constexpr int kCounterGuards = 8;

// A drawn battle in which `actor` runs `script` (drawAiScenario()'s battle, the actor on the field,
// the script readied, its advance wait unset or short, no one away from the field unless the last
// command left them out of reach), for a counterattack with the guard on `guard`'s path and the
// battler it answers drawn.
[[nodiscard]] AiScenario drawAiRunScenario(std::mt19937& rng, std::uint16_t script, battle::BattlerSlot actor,
                                           CounterGuard guard = CounterGuard::Free);

}  // namespace ostinato::trace
