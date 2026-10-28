// The battle commands' bodies and the attacks they load.
//
// A command body takes the attack in flight that startAttack() made from the chosen command,
// loads the attack the command makes, and runs it (battle/attack.h). runCommand() picks the body
// by the command and is how every command the player or a battler chooses is carried out:
//
//     AttackInFlight attack = startAttack(command);
//     BattleScriptBuilder script;
//     runCommand(state, hooks, script, attack, actor);
//     std::vector<BattleScriptCommand> commands = script.take();
//
// Most bodies are a few steps: name the attack, load the attack the command makes (the plain hit
// for Steal, Row or Jump; the chosen spell for Magic, Lore or Dance; the item for Item, Throw and
// Tools), change what the command changes about it, and run it. A few run a second command inside
// the first: a Sketch makes the attack it caught, a Rage the attack its monster chose, and a
// party member controlling a monster has the monster make the attack picked for it.
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
#include "ostinato/item_id.h"
#include "ostinato/status_set.h"

namespace ostinato::battle {

// The battle event that plays when Terra casts a spell while the story asks for it
// (game::kTerraMagicSceneSwitch): Locke and Edgar see her use magic.
inline constexpr std::uint8_t kBattleEventTerraMagicScene = 6;

// A party member's desperation attack becomes possible once the battle has run this many time
// updates.
inline constexpr std::uint16_t kTimeUpdatesBeforeDesperation = 768;

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

// The plain hit as a command with no attack of its own makes it: aimed by `command`'s rules, with
// no special effect, no level and no attack stat, and impossible to dodge. Steal, Row, Def and the
// other commands that only act on their target or on the user start from this.
void plainCommandAttack(AttackInFlight& attack, BattleCommandId command);

// The command a monster's chosen attack is made as: Magic for a spell, the skill's command for a
// skill, and so on.
BattleCommandId commandForAttack(AttackId attack);

// Whether an item is one of the spears that doubles a Jump's damage.
bool isSpear(ItemId item);

// Fight and Capture: two attacks, one with each hand (eight with an Offering), at the targets the
// command was aimed at; each may be a critical hit.
void fightAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor);

// Take on the monster `slot` rages as (Battler::rage): its special attack and its animation, its
// attack's look in both hands, and its elements, immunities and statuses (loadMonsterTraits()).
// Its statuses are put on when the action's statuses take effect.
void setRage(BattleState& state, BattlerSlot slot);

// Carry out `attack.command` for `actor`: the command's body loads the attack it makes and runs it.
// Mimic and the commands with no body of their own do nothing and show nothing.
void runCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                BattlerSlot actor);

}  // namespace ostinato::battle
