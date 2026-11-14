// The battle's own actions (damage over time, a spell with no caster, status timers running out,
// Seize's drain, a script's SetStatus and ClearStatus): the port's answer, read back as a whole
// attack, and the own-action sweep's draw.
#pragma once

#include <random>
#include <string>

#include "scenarios/damage/attack.h"
#include "scenarios/damage/damage.h"

namespace ostinato::trace {

// The port's action `action` for the scenario's attacker (CommandExecutor::execute(), with the battle's
// own hooks): DamageOverTime, SpellWithoutCaster, StatusTimersExpired, SeizeDrain or an
// AiEffectAction. Read back as ownAction() reads the cartridge's.
[[nodiscard]] AttackResult portOwnAction(DamageScenario scenario, const battle::Action& action);

// A drawn battle and one of the battle's own actions in it.
struct OwnActionCase {
    DamageScenario scenario;
    battle::Action action;
};

// The battles and actions the own-action sweep draws, from the same seed: drawAttackScenario()'s
// battle, then one of five actions with its operands in the game's own ranges — damage over time
// (Poison, Regen or Sap, for a battler holding the status three times in four, its Poison multiplier
// 0 to 14 by twos, held by a status one time in four, no enemies left one time in eight); a spell with
// no caster (Doom one time in three, with no enemies left one time in four of those, otherwise any of
// the 54 spells) at any battler; status timers running out for a battler (one to four of Stop,
// Reflect, Freeze and Sleep, each status held three times in four); Seize's drain by a monster holding
// a party member; and a script's SetStatus or ClearStatus. Every battler is held by a status and has
// a damage-over-time action waiting one time in four each, the battler a beat or a drain is for always
// waiting, and the battler an action acts on is on the field and reachable three times in four.
[[nodiscard]] OwnActionCase drawOwnActionCase(std::mt19937& rng);

// One line naming a drawn battle's inputs and its action, for a report.
[[nodiscard]] std::string describe(const OwnActionCase& drawn);

}  // namespace ostinato::trace
