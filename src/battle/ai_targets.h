// Who a monster's AI script aims at.
//
// A script names its targets with an AiTarget: one party member by who it is, one monster slot, or
// a selector — itself, a side, a random member of a side, the dead or those with Reflect, the
// battler that last attacked it. aiTargets() turns one into the battlers it names, drawing from the
// battle's random numbers where the selector picks one at random:
//
//     AiRun run;
//     run.targets = aiTargets(state, monster, AiTargetSelector::RAND_CHAR, true);
//
// A side is worked out from the battlers in the battle: the party slots that hold a party member
// and the monster slots that hold a monster. With `reachableOnly` the sides are first cut to the
// battlers an attack can reach: on the field and not fallen or hidden (removeAbsentTargets()), and
// not kept out of reach by anything else (reachableTargets(), both in battle/targeting.h). A step
// that sets the targets of the attacks to follow asks for that; a condition that names a target
// does not. A party member named by who it is, a monster slot, a party slot and the dead or those
// with Reflect are found without the sides, so the cut does not touch them.
//
// The rules, and the line in the original each comes from, are specified in
// docs/contracts/battle_ai.md.
#pragma once

#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/ai_scripts.h"

namespace ostinato::battle {

// What one run of a script carries from one step to the next.
struct AiRun {
    BattlerSet targets;           // who the run's actions are aimed at
    BattlerSet startingTargets;   // the targets the run started with; a block that fails goes back to them
    bool restricted = false;      // only a block that a monsters-dead or always condition opens may act
};

// The battlers `target` names for `actor`'s script. `reachableOnly` drops from the sides the
// battlers an attack cannot reach. Reading TARGETTING_TARGET uses up the mark it reads.
BattlerSet aiTargets(BattleState& state, BattlerSlot actor, AiTarget target, bool reachableOnly);

}  // namespace ostinato::battle
