// An attack's special effect on one target (DoTargetEffect): what it leaves, the port's run of it,
// the comparison and the sweep's draw.
#pragma once

#include <map>
#include <random>
#include <string>

#include "battle/battle_script.h"
#include "battle/battler_set.h"

#include "scenarios/damage/damage.h"

namespace ostinato::trace {

// What an attack's special effect on one target leaves, as the same cells on both sides, each named
// and given as text: whether the effect missed the target; the attack's record, damage, multiplier,
// animation values, params and aim; the command's message and what it names, its spell or sketch to
// make next, its earlier targets, the battlers shown "Miss", whether counterattacks are on and whether
// an attack with no one left is hidden; the battle's battlers leaving, gone, fallen, swallowed, taken
// from the party and holding an item, the monsters drawn flipped, Zinger's two, the end-of-battle
// event and the party's gil; for every battler its HP, MP, level, statuses and statuses waiting, what
// is to be worked out again, Air Anchor, its row, gauge and menu flags, Overcast, Phantasm and the
// control menu flag, its elements, every link, its item in hand, AI variable, items to steal, gold,
// the command it was last hit with, its last-attacker mark, its actions waiting and, for a party
// member, its control menu; the commands, params blocks and interrupting actions the effect added;
// and the random-number position.
struct TargetEffectResult {
    std::map<std::string, std::string> cells;
};

// The scenario after the port's target effect has run over it: the target, whether the effect missed
// it, and the script it wrote. The script starts with the attack's own animation, put there by the
// attack frame before any target's effect.
struct TargetEffectRun {
    DamageScenario after;
    battle::BattlerSlot target = battle::BattlerSlot::monster(0);
    bool missed = false;
    battle::BattleScriptBuilder script;
};

// The port's special effect of the scenario's attack on `target`.
[[nodiscard]] TargetEffectRun runTargetEffect(DamageScenario scenario, battle::BattlerSlot target);

// A port run read back as a TargetEffectResult.
[[nodiscard]] TargetEffectResult targetEffectResult(const TargetEffectRun& run);

[[nodiscard]] std::string differences(const TargetEffectResult& port, const TargetEffectResult& guest);

// The scenarios the target-effect sweep draws, from the same seed: every cell the attacker-effect
// sweep draws, then an effect that acts on the target three times in four, a target and, for the
// command effects that open a party member's menu, a party member attacking; for every battler its
// level, stamina and statuses, whether instant death can reach it, its metamorph byte, items to steal
// and monster, every link (a Control link sometimes ending), its AI variable, item in hand and the
// action it has waiting; the formation, the attack's statuses and hit rate, Mind Blast's picks, the
// battlers gone, fallen, swallowed and taken from the party, Zinger's two and the monsters drawn
// flipped. The target is left as the attack frame leaves it for the effect.
[[nodiscard]] TargetScenario drawTargetEffectScenario(std::mt19937& rng);

}  // namespace ostinato::trace
