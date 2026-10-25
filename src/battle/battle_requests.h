// What the battle asks of its presentation.
//
// The battle never draws, plays sound or opens a window itself. It records each thing it needs
// shown as a BattleRequest on BattleState::requests, in order, and the presentation drains them
// once per frame. A RunBattleScript request is the one the battle waits on: the battle loop takes
// no further step until the presentation reports that the script has finished playing.
//
//     for (const BattleRequest& request : state.requests) {
//         std::visit(presentation, request);
//     }
//     state.requests.clear();
//
// A battle script is the sequence of things one action shows — its name, its animation, the
// numbers — as BattleScriptCommand values.
#pragma once

#include <variant>
#include <vector>

#include "battle/battler_set.h"
#include "ostinato/battle_command_id.h"

namespace ostinato::battle {

// A character steps forward for the action it has chosen, while its advance wait runs: the ready
// stance, or the leap into the air for a Jump.
struct ReadyStance {
    BattlerSlot actor = BattlerSlot::character(0);
    BattleCommandId command = BattleCommandId::FIGHT;
    BattlerSet targets;
};

// The battler that just acted goes back to its place.
struct StepBack {};

using BattleScriptCommand = std::variant<ReadyStance, StepBack>;

// Open the battle menu for a character. `controllingMonster` shows the menu of the monster the
// character is controlling instead of the character's own.
struct OpenBattleMenu {
    BattlerSlot character = BattlerSlot::character(0);
    bool controllingMonster = false;
};

// Close a character's battle menu.
struct CloseBattleMenu {
    BattlerSlot character = BattlerSlot::character(0);
};

// Redraw a character's battle menu: its commands or what they offer changed.
struct RefreshBattleMenu {
    BattlerSlot character = BattlerSlot::character(0);
};

// Play a battle script. The battle loop waits until it has finished.
struct RunBattleScript {
    std::vector<BattleScriptCommand> commands;
};

// The monsters' names or numbers changed: redraw the list of them.
struct RefreshMonsterNames {};

using BattleRequest =
    std::variant<OpenBattleMenu, CloseBattleMenu, RefreshBattleMenu, RunBattleScript, RefreshMonsterNames>;

}  // namespace ostinato::battle
