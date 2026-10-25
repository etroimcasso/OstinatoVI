// The parts of a battle the battle core hands off: what each command does, how actions are aimed,
// how a battler's AI chooses, and the status upkeep between actions.
//
// The battle core decides when things happen — whose turn it is, what is in line, when a battle
// ends. Everything that decides what happens goes through an ActionExecutor. Every member has a
// default that does nothing, so a test or a host supplies only the parts it needs:
//
//     struct HitForTen : ActionExecutor {
//         std::vector<BattleScriptCommand> execute(BattleState& state,
//                                                  std::optional<BattlerSlot> actor,
//                                                  const Action& action) override {
//             if (const auto* command = std::get_if<CommandAction>(&action)) { ... }
//             return {};
//         }
//     };
//
//     HitForTen executor;
//     stepBattle(state, executor);
//
// NullExecutor does nothing at all: every command passes with no effect and no AI ever acts.
#pragma once

#include <optional>
#include <vector>

#include "battle/action_queue.h"
#include "battle/battle_requests.h"
#include "battle/battle_state.h"

namespace ostinato::battle {

class ActionExecutor {
public:
    virtual ~ActionExecutor() = default;

    // Carry out one action and return the battle script that shows it. `actor` is the battler
    // taking it; an action that interrupted the line has none. Before this runs, the battle has
    // reset BattleState::scratch; the action reports through it what the counterattack checks
    // need (whether the hit was physical, who it ended up hitting).
    virtual std::vector<BattleScriptCommand> execute(BattleState&, std::optional<BattlerSlot> /*actor*/,
                                                     const Action&) {
        return {};
    }

    // Aim a command that has no targets yet, or whose targets have to be chosen again: when it is
    // chosen and whenever it is re-timed.
    virtual void aim(BattleState&, BattlerSlot /*actor*/, CommandAction&) {}

    // Choose what a party member does when it acts on its own — berserk, confused, dancing,
    // raging, in Magitek armor, in the colosseum — by adding the action with
    // addAutomaticAction().
    virtual void chooseAutomaticAction(BattleState&, BattlerSlot /*actor*/) {}

    // Run a battler's AI for its turn: add the actions it chooses.
    virtual void runAi(BattleState&, BattlerSlot /*actor*/) {}

    // Run a battler's counterattack AI: add the counterattacks it chooses.
    virtual void runCounterAi(BattleState&, BattlerSlot /*actor*/) {}

    // Status upkeep after each action, for the effects that tie two battlers together.
    //   checkControl  a controller that can no longer control lets go
    //   releaseLinks  a battler that died or left releases Zinger, Love Token and Charm
    //   clearControl  control links marked for release are released
    virtual void checkControl(BattleState&, BattlerSlot) {}
    virtual void releaseLinks(BattleState&, BattlerSlot) {}
    virtual void clearControl(BattleState&, BattlerSlot) {}

    // The updates an action can leave pending for a battler (PendingUpdates) that belong to other
    // parts of the battle: its battle commands, its menu after Morph, and the spells a near-fatal
    // relic casts.
    virtual void refreshCommandList(BattleState&, BattlerSlot) {}
    virtual void refreshMorph(BattleState&, BattlerSlot) {}
    virtual void checkNearFatal(BattleState&, BattlerSlot) {}

    // Run down the Morph meter of the morphed battler on every time update.
    virtual void tickMorph(BattleState&, BattlerSlot) {}
};

// An executor that does nothing.
using NullExecutor = ActionExecutor;

}  // namespace ostinato::battle
