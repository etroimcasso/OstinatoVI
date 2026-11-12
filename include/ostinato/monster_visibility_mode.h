// What showing or hiding monsters does to each of them.
//
// A monster's AI script names one of these when it brings monsters onto the field or takes them
// off it (data/ai_scripts.h); the battle carries it out as a MonsterVisibilityAction
// (battle/action_queue.h).
#pragma once

#include <cstdint>

namespace ostinato {

enum class MonsterVisibilityMode : std::uint8_t {
    ReviveAtFullHp = 0,      // back on the field at full HP
    Hide = 1,                // off the field, and no longer a target
    ReviveAtCurrentHp = 2,   // back on the field with the HP it had
    HideButTargetable = 3,   // off the field, but still a target
    HideAndKeepBattleGoing = 4,  // off the field; the battle does not end while no monster is left
    HideAndKill = 5,         // off the field, and dead
};

}  // namespace ostinato
