// An attack's special effect on its attacker (DoAttackerEffect): what it leaves, the port's run of
// it, the comparison and the sweep's draw.
#pragma once

#include <map>
#include <random>
#include <string>

#include "battle/battle_script.h"

#include "scenarios/damage/damage.h"

namespace ostinato::trace {

// What an attack's special effect on its attacker leaves, as the same cells on both sides, each named
// and given as text: the attack's record, damage and animation values, its flags, params and aim,
// what the command carries to its next attack, the battle's cells the effects write, the attacker's
// own cells and the party's gil, the params blocks and animations added to the script, and the
// random-number position.
struct AttackerEffectResult {
    std::map<std::string, std::string> cells;
};

// The scenario after the port's attacker effect has run over it, and the script it wrote.
struct AttackerEffectRun {
    DamageScenario after;
    battle::BattleScriptBuilder script;
};

// The port's special effect of the scenario's attack on the scenario's attacker.
[[nodiscard]] AttackerEffectRun runAttackerEffect(DamageScenario scenario);

// A port run read back as an AttackerEffectResult.
[[nodiscard]] AttackerEffectResult attackerEffectResult(const AttackerEffectRun& run);

[[nodiscard]] std::string differences(const AttackerEffectResult& port, const AttackerEffectResult& guest);

// The scenarios the attacker-effect sweep draws, from the same seed: an effect anywhere in the effect
// space, half the time one of the effects that act on the attacker; the attack's record, damage,
// multiplier, command and animation values, its flags, aim and params; the command's attacks to come,
// target count and message; for every battler its level, HP and MP, statuses, flags, hands, gold, Seize
// and whether it is on the field or in its menu; the party's gil and steps, the play clock's frames,
// Forcefield, Quick, the Golem, the allies standing, whether the party can run, whether a counterattack
// is running, and the random-number position.
[[nodiscard]] DamageScenario drawAttackerEffectScenario(std::mt19937& rng);

}  // namespace ostinato::trace
