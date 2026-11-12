// What a monster's AI script tests before it acts.
//
// A block of a script opens with its conditions (data/ai_scripts.h, AiCondition); the block's
// actions run only if every condition holds. aiCondition() tests one against the battle as it is:
//
//     AiRun run;
//     if (aiCondition(state, monster, ai::HpAtMost{.target = AiTargetSelector::SELF, .hp = 1280}, run)) {
//         // the block goes on
//     }
//
// A condition that names a target, or that asks who last hit the monster, also aims the actions
// after it: when it holds, `run.targets` is who it found. A condition on the monster itself leaves
// the monster as the target, and a party member with a script none. Testing a status draws one of
// the battlers that have it as the target, which takes a random number whether the condition holds
// or not. Two conditions lift the restriction a counterattack can run under (`run.restricted`):
// the monsters-dead test and always().
//
// The rules, and the line in the original each comes from, are specified in
// docs/contracts/battle_ai.md.
#pragma once

#include "battle/ai_targets.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/ai_scripts.h"

namespace ostinato::battle {

// Whether `condition` holds for `actor`'s script, aiming and lifting the restriction in `run` as
// the condition does.
bool aiCondition(BattleState& state, BattlerSlot actor, const AiCondition& condition, AiRun& run);

}  // namespace ostinato::battle
