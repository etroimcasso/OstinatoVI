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
// three menu choosers are battle/automatic_actions.h. runAi() reads a battler's AI script on its
// turn, except that a berserk, confused, charmed or colosseum monster picks its own attack instead;
// runCounterAi() reads the script's counterattack part and initAi() readies the script
// (battle/ai.h). refreshCommandList() decides which of a party member's commands can be chosen
// (battle/command_menu.h). playBattleEnd() plays out the battle's outcome: the win and its rewards,
// the loss, or a special ending (battle/battle_end.h).
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

    // A battler's AI script (battle/ai.h): its turn part on its turn (runAiTurn()), its counterattack
    // part when it answers an attack (runAiCounter()), and the script readied when the battler
    // takes its slot (readyAi()).
    void runAi(BattleState& state, BattlerSlot actor) override;
    void runCounterAi(BattleState& state, BattlerSlot actor, BattlerSet answering) override;
    void initAi(BattleState& state, BattlerSlot slot) override;

    void refreshCommandList(BattleState& state, BattlerSlot slot) override;

    // The statuses waiting on every battler take effect (applyStatusChanges() in battle/status.h).
    void applyStatusChanges(BattleState& state) override;

    // A near-fatal battler's relics cast their spells (nearFatalRelicSpells() in battle/status.h).
    void checkNearFatal(BattleState& state, BattlerSlot slot) override;

    // The links between battlers after each action (battle/links.h): a Control link is marked as
    // ending (endControlIfUnable()), a battler that fell or left lets go of its links
    // (releaseLinks()), and the Control links marked as ending are let go (releaseEndedControl()).
    void checkControl(BattleState& state, BattlerSlot slot) override;
    void releaseLinks(BattleState& state, BattlerSlot slot) override;
    void clearControl(BattleState& state, BattlerSlot slot) override;

    // Morph's meter (battle/morph.h): its menu after Morph or Revert (morphMenuChanged()), and the
    // meter running down on each time update (runDownMorph()).
    void refreshMorph(BattleState& state, BattlerSlot slot) override;
    void tickMorph(BattleState& state, BattlerSlot slot) override;

    AttackId chooseDanceStep(BattleState& state, BattlerSlot dancer, AttackId chosen) override;
    AttackId chooseRageAttack(BattleState& state, BattlerSlot rager, AttackId chosen) override;
    AttackId chooseRandomEsper(BattleState& state, BattlerSlot caster, AttackId chosen) override;

    // The battle's end (playBattleEnd() in battle/battle_end.h).
    void playBattleEnd(BattleState& state, const BattleOutcome& outcome) override;

private:
    AttackHooks& hooks_;
    const TextCorpus& text_;
    const MonsterSprites& sprites_;
};

}  // namespace ostinato::battle
