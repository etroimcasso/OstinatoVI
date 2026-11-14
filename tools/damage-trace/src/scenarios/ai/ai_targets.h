// A battle as a monster's AI script reads it (AiScenario), a script's targets and a script's
// condition: what each leaves, the port's answer, the comparison and the AI sweeps' draws.
#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <string>

#include "battle/ai_targets.h"
#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"

namespace ostinato::trace {

// One battle as a monster's AI script reads it: the state, the battler whose script it is, and what
// the script's run holds so far — its targets, the targets it started with, and whether it is
// restricted.
//
// A scenario run as a turn or a counterattack reads every battler's script as the state holds it
// (Battler::turnScript, counterScript and the two resumes, readied with battle::readyAi()), and a
// counterattack answers `answering`.
struct AiScenario {
    battle::BattleState state;
    battle::BattlerSlot actor = battle::BattlerSlot::monster(0);
    battle::AiRun run;
    battle::BattlerSet answering;
};

// What finding a script's targets leaves, as the same fields on both sides: the battlers found,
// whether any were (the cartridge's carry), the battler's Targetting mark and the random-number
// position.
struct AiTargetsResult {
    battle::BattlerSet targets;
    bool any = false;
    std::optional<battle::BattlerSlot> targetingTarget;
    std::uint8_t rngPosition = 0;
};

// The port's targets for `target` over the scenario (battle::aiTargets), read back as an
// AiTargetsResult.
[[nodiscard]] AiTargetsResult portAiTargets(AiScenario scenario, AiTarget target, bool reachableOnly);

[[nodiscard]] std::string differences(const AiTargetsResult& port, const AiTargetsResult& guest);

// What testing a script's condition leaves, as the same fields on both sides: whether it holds, the
// run's targets and restriction, the battler's Targetting mark and the random-number position.
struct AiConditionResult {
    bool holds = false;
    battle::BattlerSet targets;
    bool restricted = false;
    std::optional<battle::BattlerSlot> targetingTarget;
    std::uint8_t rngPosition = 0;
};

// The port's test of `condition` over the scenario (battle::aiCondition), read back as an
// AiConditionResult.
[[nodiscard]] AiConditionResult portAiCondition(AiScenario scenario, const AiCondition& condition);

[[nodiscard]] std::string differences(const AiConditionResult& port, const AiConditionResult& guest);

// A battle a script's targets are found in, the target asked for and whether the sides are cut to
// the reachable.
struct AiTargetsCase {
    AiScenario scenario;
    AiTarget target;
    bool reachableOnly = false;
};

// A battle a script's condition is tested in, and the condition.
struct AiConditionCase {
    AiScenario scenario;
    AiCondition condition;
};

// The battles the AI sweeps draw, from the same seed: an actor in any slot; for every battler
// whether it is on the field, the party member or monster in its slot (or none), its statuses
// (Dead, Reflect, Hide and the statuses from Dance on often among them), HP, MP and level often at
// the thresholds a condition asks about, weaknesses, the battler that last attacked it and whether
// it is waiting to answer, its Targetting mark, what it was last hit with and by whom, the battler
// that last damaged it, its own variable and timer, and whether it is held in Seize; the battle's
// reachable, counted, untargetable, away-from-the-field and alive sets, the reach flag the last
// command left, the party and monster counts, the timer, the four variables and the switches, the
// names listed, Gau, the formation and the random-number position; and the run's targets, starting
// targets and restriction.
[[nodiscard]] AiScenario drawAiScenario(std::mt19937& rng);

// A drawn battle with any of the thirty selectors or a party member, with or without the cut.
[[nodiscard]] AiTargetsCase drawAiTargetsCase(std::mt19937& rng);

// A drawn battle with any condition a script can hold, its operands drawn within the ranges a
// script's own can take and often at the battle's own values.
[[nodiscard]] AiConditionCase drawAiConditionCase(std::mt19937& rng);

// One line naming a drawn AI battle's inputs, for a report.
[[nodiscard]] std::string describe(const AiScenario& scenario);

}  // namespace ostinato::trace
