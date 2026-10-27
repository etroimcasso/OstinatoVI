// The battle's ActionExecutor: it carries out the battle commands and aims them.
//
//     AttackHooks hooks;                    // damage, statuses and effects plug in here
//     CommandExecutor executor(hooks);
//     stepBattle(state, executor);
//
// execute() starts the command's attack (startAttack()), runs its body (runCommand() in
// battle/commands.h) and returns the battle script it wrote. aim() sets up the command's attack and
// chooses its targets when it was chosen without any. refreshCommandList() decides which of a party
// member's commands can be chosen (battle/command_menu.h). An action that is not a battle command
// runs as no action and shows nothing.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_commands.md.
#pragma once

#include <optional>
#include <vector>

#include "battle/action_executor.h"
#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"

namespace ostinato::battle {

class CommandExecutor : public ActionExecutor {
public:
    explicit CommandExecutor(AttackHooks& hooks) : hooks_(hooks) {}

    std::vector<BattleScriptCommand> execute(BattleState& state, std::optional<BattlerSlot> actor,
                                             const Action& action) override;

    void aim(BattleState& state, BattlerSlot actor, CommandAction& command) override;

    void refreshCommandList(BattleState& state, BattlerSlot slot) override;

private:
    AttackHooks& hooks_;
};

}  // namespace ostinato::battle
