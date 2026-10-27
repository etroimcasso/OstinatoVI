// What a battler does when no one chooses for it.
//
// A party member whose turn comes with nothing chosen acts on its own: a dancer dances on, a
// raging party member makes its monster's attack, one in Magitek armor fires a beam, and a party
// member who is berserk, confused, charmed, a zombie or fighting in the colosseum picks one of
// its own commands at random. A monster that is berserk, confused, charmed or in the colosseum
// picks one of the attacks it would make under Control instead of following its AI.
//
//     chooseAutomaticAction(state, executor, slot);  // adds the chosen command to its list
//
// The three menu choices that come down to chance are here too: the step a Dance turns into, the
// attack a Rage makes, and the esper a slot reel's summon calls.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_commands.md.
#pragma once

#include "battle/action_executor.h"
#include "battle/action_queue.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "ostinato/attack_id.h"

namespace ostinato::battle {

// Choose what party member `slot` does with nothing chosen, and add it with its advance wait and
// aim. Dance, Rage and Magitek armor come first, in that order; otherwise the party member picks
// one of its own commands (chooseOwnCommand()). An X-Magic pick casts two spells: a Magic with a
// second spell drawn first, then the X-Magic. A Lore that comes out as Roulette is made as the
// Roulette command, which takes no advance wait and no aim.
void chooseAutomaticAction(BattleState& state, ActionExecutor& executor, BattlerSlot slot);

// The command a party member picks on its own, with the attack it makes. Each of its four
// commands is a candidate unless the row is empty, a confused party member could not use it, or —
// when berserk, or a zombie who is neither confused, charmed nor in the colosseum — a berserk one
// could not; a command whose attack is chosen at random drops out when it has none to make (a
// mute caster, Morph without enough supply, Bushido without a Bushido weapon). One more Fight is
// always a candidate, ahead of the rest, so Fight comes up more often than the others. The
// candidates' attacks are drawn last row first, then one candidate is drawn.
CommandAction chooseOwnCommand(BattleState& state, BattlerSlot slot);

// Whether monster `slot` picks its own attack rather than following its AI: in the colosseum,
// charmed, berserk or confused.
bool choosesItsOwnAttack(const BattleState& state, BattlerSlot slot);

// Monster `slot` picks one of its four Control attacks: from a slot drawn at random, it takes the
// first filled one counting down and round again. A berserk monster makes the plain hit instead.
// The attack is made as the command it belongs to and added with its advance wait and aim.
void chooseMonsterAction(BattleState& state, ActionExecutor& executor, BattlerSlot slot);

// The step a dancer dances: one of the four steps of its dance (Battler::dance), the first seven
// times in sixteen, the second three in eight, the third one in eight and the fourth one in
// sixteen. A dancer with no dance chosen takes one of the dances the party knows, at random.
AttackId chooseDanceStep(BattleState& state, BattlerSlot slot);

// The attack a raging party member makes: one of its monster's two Rage attacks, on a coin. A
// party member with no monster chosen (Battler::rage) takes one of the rages the party knows, at
// random.
AttackId chooseRageAttack(BattleState& state, BattlerSlot slot);

// The esper a slot reel's summon calls: one of the espers but Odin and Raiden, each equally likely.
AttackId chooseRandomEsper(BattleState& state);

}  // namespace ostinato::battle
