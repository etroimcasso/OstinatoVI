// How a battle command's attack is aimed.
//
// Aiming happens in two steps. initTargeting() takes the command's aiming rules and targeting
// byte, loads the attack it makes (a plain hit, the item in hand, the spell chosen, or what a
// thrown item or tool becomes), and lets the attack add its own rules. chooseTargets() then
// settles who the attack reaches: it drops the targets that cannot be reached, re-aims when none
// are left, keeps to one side of a pincer or side attack, and picks one target unless the attack
// may hit them all.
//
//     AttackInFlight attack = startAttack(command);
//     initTargeting(attack, command.command, command.attack);
//     chooseTargets(state, attack, actor);   // attack.targets is who it reaches
//
// coverTarget() then lets a True Knight or a Love Token step in front of the target of a blow.
//
// The rules, and the line in the original each comes from, are specified in
// docs/contracts/battle_commands.md.
#pragma once

#include <optional>

#include "battle/attack.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_id.h"

namespace ostinato::battle {

// Items before this one, used with the Item command, cast the spell they carry; it and the items
// after it are used as themselves.
inline constexpr ItemId kFirstItemUsedAsItself = ItemId::SPRINT_SHOES;

// Set up the aiming of `command`'s attack: its rules and targeting byte, and the attack's record.
// `attackOrItem` is the command's attack — for Item, Throw and Tools, the item. Returns true when
// the item itself is the attack (a consumable used, an item thrown, a tool); no record is loaded
// then, and the command loads the item's attack itself.
bool initTargeting(AttackInFlight& attack, BattleCommandId command, AttackId attackOrItem);

// Settle who the attack reaches, starting from attack.targets. An attack under a bounce off
// Reflect's rule (attack.rules.reflected), aimed at the other side only, goes half the time to
// `lastParamsAttacker` — the battle script's BattleScriptBuilder::lastParamsAttacker(), the caster
// for the bounce itself — when that battler is among its targets. When it names no one, the attack
// takes two lore list entries for its targets instead (docs/Bugs.md). The rule lasts the rest of
// the command, so a later attack of the same command is aimed this way too.
void chooseTargets(BattleState& state, AttackInFlight& attack, BattlerSlot attacker,
                   std::optional<BattlerSlot> lastParamsAttacker = std::nullopt);

// `targets` without those not on the field: absent, hidden, or fallen (dead for a party member;
// dead, petrified or a zombie for a monster).
BattlerSet removeAbsentTargets(const BattleState& state, BattlerSet targets);

// `targets` without those no attack can reach: battlers not on the field or not counted in the
// battle, party members held in Seize, monsters their AI has made untargetable, and — with
// `awayFromTheFieldUnreachable` — the battlers away from the field (jumping, carried off by
// Palidor, held in Seize).
BattlerSet reachableTargets(const BattleState& state, BattlerSet targets, bool awayFromTheFieldUnreachable);

// A True Knight or a Love Token: a battler steps in front of the target of a blow and is hit in
// its place. A bodyguard who is controlled or held in Seize does not step in, and a target held in
// Seize is not covered.
void coverTarget(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker);

}  // namespace ostinato::battle
