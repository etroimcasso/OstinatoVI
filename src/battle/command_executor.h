// The battle's ActionExecutor: it carries out every action the battle core runs, aims commands,
// chooses for the battlers no one chooses for, and keeps the command menu up to date.
//
//     AttackHooks hooks;                    // damage, statuses and effects plug in here
//     CommandExecutor executor(hooks, cartridge.text, cartridge.monsterSprites);
//     BattleSim battle(start, party, switches, cartridge.text, cartridge.monsterSprites, executor);
//
// execute() runs a battle command — startAttack(), then its body (runCommand() in
// battle/commands.h) — or one of the actions the battle takes on its own
// (battle/system_commands.h), and returns the battle script it wrote. aim() sets up a command's
// attack and chooses its targets when it was chosen without any. chooseAutomaticAction() and the
// three menu choosers are battle/automatic_actions.h; runAi() makes a berserk, confused, charmed or
// colosseum monster pick its own attack. refreshCommandList() decides which of a party member's
// commands can be chosen (battle/command_menu.h).
//
// The level an action's attack is made at stays as BattleState::lastAttackLevel until an action makes
// another attack with a level of its own; a condemned count that starts reads it.
//
// The text and the monster sprites are what a formation change needs to load and place the new
// monsters; they must outlive the executor.
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
#include "data/monster_sprites.h"
#include "data/text_corpus.h"

namespace ostinato::battle {

class CommandExecutor : public ActionExecutor {
public:
    CommandExecutor(AttackHooks& hooks, const TextCorpus& text, const MonsterSprites& sprites)
        : hooks_(hooks), text_(text), sprites_(sprites) {}

    std::vector<BattleScriptCommand> execute(BattleState& state, std::optional<BattlerSlot> actor,
                                             const Action& action) override;

    void aim(BattleState& state, BattlerSlot actor, CommandAction& command) override;

    void chooseAutomaticAction(BattleState& state, BattlerSlot actor) override;

    void runAi(BattleState& state, BattlerSlot actor) override;

    void refreshCommandList(BattleState& state, BattlerSlot slot) override;

    AttackId chooseDanceStep(BattleState& state, BattlerSlot dancer, AttackId chosen) override;
    AttackId chooseRageAttack(BattleState& state, BattlerSlot rager, AttackId chosen) override;
    AttackId chooseRandomEsper(BattleState& state, BattlerSlot caster, AttackId chosen) override;

private:
    AttackHooks& hooks_;
    const TextCorpus& text_;
    const MonsterSprites& sprites_;
};

}  // namespace ostinato::battle
