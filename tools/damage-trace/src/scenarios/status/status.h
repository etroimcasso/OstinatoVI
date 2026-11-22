// Every battler's waiting statuses taking effect (UpdateStatus) and an attack's statuses on one
// target (MagicStatusEffect): what each leaves, the port's answer, the comparison and the sweeps'
// draws.
#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>

#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "ostinato/status_set.h"

#include "scenarios/damage/damage.h"

namespace ostinato::trace {

// What every battler's waiting statuses taking effect leaves, as the same fields on both sides:
// each battler's statuses, its statuses to set and to clear, the statuses it is to have for a
// counterattack, what is to be worked out again for it (as the cartridge's byte holds it: bit 7
// castable spells, 6 drop its actions, 5 start a condemned count, 4 stop it, 3 its commands, 2 its
// speed, 1 its menu after Morph, 0 Palidor), whether its menu may open, whether its gauge empties
// after the action, Phantasm's drain, whether a party member's equipment is to be worked out again,
// its four status timers; and the battlers that died this action, the party members whose statuses
// changed, and the monsters standing and fallen.
struct StatusChangesResult {
    std::array<StatusSet, battle::kBattlerSlots> status{};
    std::array<StatusSet, battle::kBattlerSlots> statusesToSet{};
    std::array<StatusSet, battle::kBattlerSlots> statusesToClear{};
    std::array<StatusSet, battle::kBattlerSlots> statusForCounter{};
    std::array<std::uint8_t, battle::kBattlerSlots> pendingUpdates{};
    std::array<bool, battle::kBattlerSlots> menuMayOpen{};
    std::array<bool, battle::kBattlerSlots> emptyGaugeAfterAction{};
    std::array<bool, battle::kBattlerSlots> phantasm{};
    std::array<std::uint8_t, battle::kPartySlots> equipmentChangeFlags{};
    std::uint8_t menuWindowDesign = 0;
    std::array<std::uint8_t, battle::kBattlerSlots> sleepTimer{};
    std::array<std::uint8_t, battle::kBattlerSlots> stopTimer{};
    std::array<std::uint8_t, battle::kBattlerSlots> reflectTimer{};
    std::array<std::uint8_t, battle::kBattlerSlots> freezeTimer{};
    battle::BattlerSet diedAwaitingCounter;
    battle::BattlerSet statusChanged;
    battle::BattlerSet monstersStanding;
    battle::BattlerSet monstersFallen;
};

// The port's statuses taking effect over `scenario`, read back as a StatusChangesResult.
[[nodiscard]] StatusChangesResult portStatusChanges(DamageScenario scenario);

[[nodiscard]] std::string differences(const StatusChangesResult& port, const StatusChangesResult& guest);

// What an attack's statuses on one target leave, as the same fields on both sides: the target's
// statuses to set and to clear, the battlers the effect missed and the battlers shown "Miss".
struct StatusEffectResult {
    StatusSet statusesToSet;
    StatusSet statusesToClear;
    battle::BattlerSet missedByEffect;
    battle::BattlerSet missed;
};

// The port's statuses of the scenario's attack on `target`, read back as a StatusEffectResult. The
// effect's misses start empty, as the attack frame leaves them for each target.
[[nodiscard]] StatusEffectResult portStatusEffect(DamageScenario scenario, battle::BattlerSlot target);

[[nodiscard]] std::string differences(const StatusEffectResult& port, const StatusEffectResult& guest);

// The scenarios the statuses-taking-effect sweep draws, from the same seed: for every battler,
// whether it is on the field, its statuses, its statuses to set and to clear and the statuses it can
// be given, every bit of each drawn; its HP and maximum HP (a party member's at most 9,999), often
// near an eighth of the maximum; its Overcast and Phantasm triggers, whether it is waiting to
// retaliate, what is to be worked out again for it, its menu and gauge flags, its timers and
// whether its equipment is to be worked out again; and the battlers that died this action, the
// party members whose statuses changed and the monsters standing and fallen.
[[nodiscard]] DamageScenario drawStatusChangesScenario(std::mt19937& rng);

// The scenarios the status-effect sweep draws, from the same seed: an attack with every bit of its
// traits, element, second flags, misc flags and statuses drawn — except the two status modes
// together, which no attack has and on which the cartridge runs unrelated code — whether Vanish
// applies, and a target
// with its statuses, statuses to set and to clear, the statuses it can be given and whether it is
// undead drawn; and the battlers already shown "Miss".
[[nodiscard]] TargetScenario drawStatusEffectScenario(std::mt19937& rng);

}  // namespace ostinato::trace
