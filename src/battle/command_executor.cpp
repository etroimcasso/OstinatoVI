#include "battle/command_executor.h"

#include <cstddef>
#include <variant>

#include "battle/commands.h"
#include "battle/targeting.h"
#include "data/battle_commands.h"
#include "ostinato/attack_flags.h"

namespace ostinato::battle {

namespace {

// Commands past the menu's own take no aim when they are chosen.
bool isMenuCommand(BattleCommandId command) {
    return static_cast<std::size_t>(command) < kCommandTargetingInit.size();
}

}  // namespace

std::vector<BattleScriptCommand> CommandExecutor::execute(BattleState& state, std::optional<BattlerSlot> actor,
                                                          const Action& action) {
    const auto* command = std::get_if<CommandAction>(&action);
    if (command == nullptr || !actor) {
        return {};
    }
    AttackInFlight attack = startAttack(*command);
    BattleScriptBuilder script;
    switch (command->command) {
    case BattleCommandId::FIGHT:
    case BattleCommandId::CAPTURE:
        fightAttack(state, hooks_, script, attack, *actor);
        break;
    default:
        return {};
    }
    state.scratch.physical = attack.record.traits.has(AttackTrait::PHYSICAL);
    state.scratch.finalTargets = attack.targets;
    return script.take();
}

void CommandExecutor::aim(BattleState& state, BattlerSlot actor, CommandAction& command) {
    if (!isMenuCommand(command.command)) {
        return;
    }
    AttackInFlight attack = startAttack(command);
    initTargeting(attack, command.command, command.attack);
    attack.rules.keepTargetsThatAreGone = false;
    if (!command.targets.empty()) {
        return;
    }
    chooseTargets(state, attack, actor);
    command.targets = attack.targets;
}

}  // namespace ostinato::battle
