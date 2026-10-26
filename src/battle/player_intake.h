// The commands the player chooses in the battle menu, as the battle takes them in.
//
// When the player finishes choosing a command for a party member, the battle menu hands the
// battle a PlayerAction: who acts, the command, its attack (a spell, an item, a dance — whatever
// the command picks from) and its targets, plus a second spell and its targets for X-Magic. The
// menu adds it to BattleState::chosenActions; on its next time update the battle takes every one
// waiting, in the order they were chosen:
//
//     state.chosenActions.push_back(PlayerAction{.character = BattlerSlot::character(0),
//                                                .command = BattleCommandId::FIGHT,
//                                                .targets = BattlerSet::of(BattlerSlot::monster(1))});
//     tickBattleTime(state, true, executor);   // takes it in: the character steps into line
//
// Taking an action in settles what it will do: X-Magic becomes two spells, a Summon of the
// equipped esper names the esper, a slot reel names its spell, a blitz the party has not learned
// becomes a plain attack, and the commands that aim again when they run (Bushido, Blitz, Rage,
// Leap, Dance) drop their targets. The rules are in docs/contracts/battle_core.md.
#pragma once

#include "battle/battler_set.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"

namespace ostinato::battle {

struct BattleState;
class ActionExecutor;

struct PlayerAction {
    BattlerSlot character = BattlerSlot::character(0);
    BattleCommandId command = BattleCommandId::FIGHT;

    // What the command uses, as the menu chose it: a spell (Magic), an item (Item, Throw), a dance
    // (by DanceId), a rage (by MonsterId), a slot reel outcome (0-7), and for Summon, Lore,
    // Magitek, Blitz and Bushido the attack's place in its own list (the first esper, lore,
    // Magitek attack, blitz or Bushido technique is 0). AttackId::NONE asks for the equipped
    // esper (Summon), a random dance or a random rage.
    AttackId attack{};
    BattlerSet targets;

    // X-Magic's second spell.
    AttackId secondAttack{};
    BattlerSet secondTargets;
};

// Take in every action the player has finished choosing: each acting character joins the
// advance-wait line with its command, its menu closed.
void takePlayerActions(BattleState& state, ActionExecutor& executor);

}  // namespace ostinato::battle
