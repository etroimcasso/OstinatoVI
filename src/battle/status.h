// How statuses go on the battlers and come off: what an attack's statuses do to one target it hits,
// and the statuses waiting on every battler taking effect once the attack is over.
//
// An attack never changes a battler's statuses directly. For each target hit, statusEffect() works
// out what the attack sets and clears there and adds it to the target's Battler::statusesToSet and
// statusesToClear; once the attack is over, applyStatusChanges() makes every battler's waiting
// statuses take effect:
//
//     statusEffect(state, attack, attacker, target);   // for each target hit, before its damage
//     // ... the attack's damage, its bounce off Reflect ...
//     applyStatusChanges(state);                        // once, after the attack
//
// BattleHooks (battle/battle_hooks.h) calls them at those points of the attack frame.
//
// statusModification() and calcStatus() read nothing of the battle, for anything that uses an item
// or a spell with no battle around it.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_status.md.
#pragma once

#include <cstdint>

#include "battle/attack.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "ostinato/attack_flags.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

namespace ostinato::battle {

// How long a status lasts once it goes on, in the steps of the battler's status timers
// (Battler::statusTimer): the timer it starts counts down to 0 and the status comes off.
inline constexpr std::uint8_t kSleepDuration = 18;
inline constexpr std::uint8_t kStopDuration = 18;
inline constexpr std::uint8_t kReflectDuration = 26;
inline constexpr std::uint8_t kFreezeDuration = 34;

// The statuses a Zombie cannot have: they come off a battler as it becomes one, and any it is given
// while it is one come off again at once.
inline constexpr StatusSet kStatusesAZombieCannotHave =
    StatusSet::of(StatusId::BLIND, StatusId::POISON, StatusId::NEAR_FATAL, StatusId::BERSERK, StatusId::CONFUSE,
                  StatusId::SLEEP);

// The statuses a battler loses as it is petrified or killed, of those it has or is to be given.
// Reraise, Hide and Interceptor stay, as do the statuses not named.
inline constexpr StatusSet kStatusesPetrifyTakesOff =
    StatusSet::of(StatusId::BLIND, StatusId::POISON, StatusId::INVISIBLE, StatusId::NEAR_FATAL, StatusId::IMAGE,
                  StatusId::SILENCE, StatusId::BERSERK, StatusId::CONFUSE, StatusId::SAP, StatusId::SLEEP,
                  StatusId::DANCE, StatusId::REGEN, StatusId::SLOW, StatusId::HASTE, StatusId::STOP, StatusId::SHELL,
                  StatusId::PROTECT, StatusId::REFLECT, StatusId::RAGE, StatusId::FROZEN, StatusId::MORPH,
                  StatusId::CHANT, StatusId::FLOAT);

// The statuses a battler loses as it is killed, on top of kStatusesPetrifyTakesOff.
inline constexpr StatusSet kStatusesDeadTakesOff = StatusSet::of(StatusId::PETRIFY, StatusId::CONDEMNED);

// What an attack does to one target's statuses: the statuses it puts on and those it takes off.
struct StatusChange {
    StatusSet toSet;
    StatusSet toClear;
};

// What an attack with `attackStatuses` (AttackProperties::statuses) and `flags2` does to a target
// whose statuses are `current`. The attack's flags pick what it does with its statuses:
//
//   - neither REMOVE_STATUS nor TOGGLE_STATUS: it sets those the target does not have;
//   - REMOVE_STATUS: it clears those the target has;
//   - TOGGLE_STATUS: both — it sets those the target does not have and clears those it has.
//
// No attack in the game has both flags; one that did is treated as a toggle.
//
// Two more statuses come off: Invisible, when the attack is not `physical`, and Frozen, when it is a
// `fire` attack — each only from a target that has it and only when the attack's own statuses do not
// name it.
[[nodiscard]] StatusChange statusModification(const StatusSet& current, const StatusSet& attackStatuses,
                                              AttackFlags2 flags2, bool physical, bool fire);

// The statuses a battler has once `toSet` goes on and `toClear` comes off: a status in both ends up
// off.
[[nodiscard]] StatusSet calcStatus(const StatusSet& current, const StatusSet& toSet, const StatusSet& toClear);

// The attack's statuses on `target`, one of the targets it hits: statusModification() over the
// attack's record, with two rules of the battle's own:
//
//   - with AttackInFlight::vanishApplies false (an Item, an attack a battler makes on itself),
//     Invisible does not come off;
//   - a remove-status attack that inverts on the undead and names Dead or Zombie (Life) kills an
//     undead target: Dead is to be set.
//
// The result is added to the target's statusesToSet and statusesToClear; nothing changes its statuses
// until applyStatusChanges(). An attack with AttackMiscFlag::MISS_IF_STATUS_IMMUNE misses a target
// when nothing it sets or clears is a status the target can be given (Battler::inflictableStatuses):
// the target goes into attack.scratch.missedByEffect, so the attack frame skips its damage, and into
// attack.scratch.missed, so it is shown "Miss". `attacker` is not read.
void statusEffect(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target);

// Every present battler's statuses to set and to clear take effect, last slot first, and both are
// emptied. For each battler:
//
//   - Only statuses it can be given (Battler::inflictableStatuses) go on — or come off: a status it is
//     immune to stays as it is, which is how its equipment's statuses stay on.
//   - A petrified battler is petrified again. Near fatal goes on when its HP is at most an eighth of
//     its maximum, and comes off when it is above; at 0 HP it goes on. A battler with its Overcast
//     trigger that is to die becomes a Zombie instead, and its Condemned comes off.
//   - A battler waiting to retaliate has Battler::statusForCounter set to the statuses it is to have,
//     worked out before any of the rest below.
//   - Each status going on or coming off does what it does to the battler and the battle: Dead and
//     Petrify take most other statuses off, mark the battler as having died this action and drop its
//     actions; Sleep, Stop, Reflect and Frozen start their timers; Haste and Slow take each other off;
//     and so on (docs/contracts/battle_status.md, "What each status does").
//   - A Zombie loses kStatusesAZombieCannotHave.
//
// HP is not touched: a battler set to die keeps its HP until the battle checks who has fallen.
void applyStatusChanges(BattleState& state);

}  // namespace ostinato::battle
