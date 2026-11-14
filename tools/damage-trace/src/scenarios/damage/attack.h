// A whole attack, the attack frame from loading the hand to the messages after it: what it leaves,
// the port's run of it, the comparison, and the sweeps' draws of an attack and of an attack on the
// attacker itself.
#pragma once

#include <map>
#include <random>
#include <string>

#include "battle/battle_script.h"

#include "scenarios/damage/damage.h"

namespace ostinato::trace {

// What a whole attack leaves (the attack frame, every attack of a command from loading the hand to
// the messages after it), as the same cells on both sides, each named and given as text: every cell
// the damage taken and shown, the statuses taking effect and a target's special effect are read back
// by; the battle script whole, one line per command, each animation with the params block it shows
// with; what the command's next attack starts from (its two flag bytes, field by field); the attacks
// left, the command cells the frame sets again after each attack, the reach flag and Interceptor's
// guard; and the random-number position.
struct AttackResult {
    std::map<std::string, std::string> cells;
};

// The scenario after the port's whole attack has run over it, and the script it wrote.
struct AttackRun {
    DamageScenario after;
    battle::BattleScriptBuilder script;
};

// The port's whole attack over the scenario by the scenario's attacker (battle::runAttack), worked
// out by the battle's own hooks (BattleHooks).
[[nodiscard]] AttackRun portAttack(DamageScenario scenario);

// The same aimed at the attacker itself (battle::runSelfAttack).
[[nodiscard]] AttackRun portSelfAttack(DamageScenario scenario);

// A port run read back as an AttackResult.
[[nodiscard]] AttackResult attackResult(const AttackRun& run);

[[nodiscard]] std::string differences(const AttackResult& port, const AttackResult& guest);

// The scenarios the whole-attack sweep draws, from the same seed: drawTargetEffectScenario()'s
// battle, then the attack as a command's body hands it to the frame — Fight three times in eight and
// Capture one, each with its two swings or an Offering's eight and every battler's hands (a party
// member's whole, its shield hand empty one time in two; a monster's as loading it leaves them), or
// else any other command with an aiming byte, its attack and the attack chosen, the targeting byte,
// the targets (none
// one time in four), the aiming rules, the name it is shown under, the MP it costs (above the
// attacker's one time in four of those it costs), the swirl, quiet misses, the attack hidden without
// targets, an exact aim, damage modification and re-aiming at random, the first pass, the attacks
// left with a Dragon Horn, the command's extra steps, a critical hit's chance and an automatic one,
// the sketcher; the attacker's Morph, Berserk, Imp and Mute, Air Anchor on it, Runic up on the
// battlers, the weapon's spell cast at random, a Launcher's hits to come, the battlers' facing in a
// side or pincer attack, Strago in the party with the lores known and learned (half the time free to
// learn one, and the attack one of the lores half of those), and every battler's vigor and magic power.
[[nodiscard]] DamageScenario drawAttackScenario(std::mt19937& rng);

// drawAttackScenario()'s battle with the attack the battle's own actions make on their attacker:
// nothing shown three times in four, otherwise as Magic; sparse statuses, removed one time in two;
// undodgeable; the level and stat 0 or drawn; the swirl; the drain, restore, ignore-defense, unsplit
// and undead-inverting flags drawn; and no hand loaded.
[[nodiscard]] DamageScenario drawSelfAttackScenario(std::mt19937& rng);

}  // namespace ostinato::trace
