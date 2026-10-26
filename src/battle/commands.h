// The battle commands' bodies and the attacks they load.
//
// A command body takes the attack in flight that startAttack() made from the chosen command,
// loads the attack the command makes, and runs it. Fight loads the plain hit and, for every
// attack it makes, the weapon in one hand:
//
//     AttackInFlight attack = startAttack(command);
//     BattleScriptBuilder script;
//     fightAttack(state, hooks, script, attack, actor);
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_commands.md.
#pragma once

#include <cstdint>

#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/item_properties.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/status_set.h"

namespace ostinato::battle {

// The statuses a consumable puts on or takes off its target (record bytes +21 to +24 in the
// consumable role).
StatusSet itemStatuses(const ItemProperties& properties);

// The level an attack is made at: the attacker's, or while a Sketch runs, the sketcher's.
std::uint8_t attackerLevel(const BattleState& state, const AttackInFlight& attack, BattlerSlot attacker);

// Take the attacker's level, and the stat its attack is made with: magic power for a spell, vigor
// for a physical attack. Clears any weapon spellcast.
void initAttacker(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker);

// Put one hand's weapon on the plain hit: its power, element and hit rate, its special effect, the
// spell it may cast, and its swing. With an Offering, every hit is aimed at random, cannot be
// dodged or be a critical hit, and casts no spell. An Imp monster without its Imp critical hits
// with power 1.
void loadWeaponAttack(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker, bool offHand);

// The command a monster's chosen attack is made as: Magic for a spell, the skill's command for a
// skill, and so on.
BattleCommandId commandForAttack(AttackId attack);

// Fight and Capture: two attacks, one with each hand (eight with an Offering), at the targets the
// command was aimed at; each may be a critical hit.
void fightAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor);

}  // namespace ostinato::battle
