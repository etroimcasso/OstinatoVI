#include "battle/command_executor.h"

#include <cstddef>
#include <variant>

#include "battle/automatic_actions.h"
#include "battle/command_menu.h"
#include "battle/commands.h"
#include "battle/system_commands.h"
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
    // The attack starts at the last attack's level: an action whose attack sets none keeps it, and
    // so does an action that makes no attack.
    AttackInFlight attack;
    attack.attackerLevel = state.lastAttackLevel;
    BattleScriptBuilder script;
    if (const auto* command = std::get_if<CommandAction>(&action)) {
        if (!actor) {
            return {};
        }
        attack = startAttack(*command);
        attack.attackerLevel = state.lastAttackLevel;
        runCommand(state, hooks_, script, attack, *actor);
    } else if (const auto* dot = std::get_if<DamageOverTime>(&action)) {
        if (actor) {
            damageOverTime(state, hooks_, script, attack, *actor, dot->kind);
        }
    } else if (const auto* spell = std::get_if<SpellWithoutCaster>(&action)) {
        spellWithoutCaster(state, hooks_, script, attack, spell->spell, spell->target);
    } else if (const auto* expired = std::get_if<StatusTimersExpired>(&action)) {
        statusTimersExpire(state, hooks_, script, attack, *expired);
    } else if (std::holds_alternative<PartyEscape>(action)) {
        partyEscapes(state, *this, script);
    } else if (const auto* pose = std::get_if<ResetBattlePose>(&action)) {
        script.add(PoseReset{.battler = pose->character});
    } else if (const auto* seize = std::get_if<SeizeDrain>(&action)) {
        if (actor) {
            seizeDrain(state, hooks_, script, attack, *actor, seize->seized);
        }
    } else if (const auto* event = std::get_if<BattleEventAction>(&action)) {
        script.add(BattleEventPlays{.event = event->event});
    } else if (const auto* message = std::get_if<BattleMessageAction>(&action)) {
        script.add(BattleMessage{.message = message->message});
    } else if (const auto* change = std::get_if<FormationChange>(&action)) {
        formationChange(state, *this, script, *change, text_, sprites_);
    } else if (const auto* dialog = std::get_if<MonsterDialogAction>(&action)) {
        script.add(MonsterDialogShown{.dialog = dialog->dialog});
    } else if (const auto* visibility = std::get_if<MonsterVisibilityAction>(&action)) {
        setMonsterVisibility(state, script, *visibility);
    } else if (const auto* scanned = std::get_if<ScanAction>(&action)) {
        scan(state, scanned->target);
    } else if (const auto* animation = std::get_if<MonsterAnimationAction>(&action)) {
        script.add(MonsterAnimationPlays{.animation = animation->animation,
                                         .monsters = animation->monsters,
                                         .soundEffect = animation->soundEffect,
                                         .pan = animation->pan});
    }
    state.scratch.physical = attack.record.traits.has(AttackTrait::PHYSICAL);
    state.scratch.finalTargets = attack.targets;
    state.lastAttackLevel = attack.attackerLevel;
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

void CommandExecutor::chooseAutomaticAction(BattleState& state, BattlerSlot actor) {
    battle::chooseAutomaticAction(state, *this, actor);
}

void CommandExecutor::runAi(BattleState& state, BattlerSlot actor) {
    if (actor.isMonster() && choosesItsOwnAttack(state, actor)) {
        chooseMonsterAction(state, *this, actor);
    }
}

void CommandExecutor::refreshCommandList(BattleState& state, BattlerSlot slot) {
    refreshCommandMenu(state, slot);
}

AttackId CommandExecutor::chooseDanceStep(BattleState& state, BattlerSlot dancer, AttackId) {
    return battle::chooseDanceStep(state, dancer);
}

AttackId CommandExecutor::chooseRageAttack(BattleState& state, BattlerSlot rager, AttackId) {
    return battle::chooseRageAttack(state, rager);
}

AttackId CommandExecutor::chooseRandomEsper(BattleState& state, BattlerSlot, AttackId) {
    return battle::chooseRandomEsper(state);
}

}  // namespace ostinato::battle
