// The three commands only a script queues (Cmd_2e, Cmd_2f and Cmd_30): what each leaves, the port's
// answer, the comparison and the sweep's draw.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <string>

#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/ai_scripts.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/status_set.h"

#include "scenarios/ai/ai_targets.h"
#include "scenarios/damage/attack.h"

namespace ostinato::trace {

// What one of the three commands only a script queues leaves, as the same fields on both sides: the
// four battle variables, the twenty switch bytes, every battler's own variable, timer, ATB rate,
// statuses and statuses to set and to clear; the invincible and AI-untargetable sets, the battle's
// timer, the end-of-battle event, Gau recruited and the party membership of every character record;
// and the random-number position.
struct AiCommandResult {
    std::array<std::uint8_t, kAiBattleVariables> battleVariables{};
    std::array<std::uint8_t, game::kBattleVariables> switches{};
    std::array<std::uint8_t, battle::kBattlerSlots> ownVariables{};
    std::array<std::uint16_t, battle::kBattlerSlots> ownTimers{};
    std::array<std::uint16_t, battle::kBattlerSlots> atbRates{};
    std::array<StatusSet, battle::kBattlerSlots> statuses{};
    std::array<StatusSet, battle::kBattlerSlots> statusesToSet{};
    std::array<StatusSet, battle::kBattlerSlots> statusesToClear{};
    battle::BattlerSet invincible;
    battle::BattlerSet aiUntargetable;
    std::uint16_t battleCounter = 0;
    std::optional<battle::EndOfBattleEvent> endOfBattleEvent;
    bool gauRecruited = false;
    std::array<game::PartyMembership, game::kCharacterRecords> membership{};
    std::uint8_t rngPosition = 0;
    // The cells of the battle an attack leaves and the script, as AttackResult names them: unchanged
    // on both sides for a command that makes no attack, and what SetStatus's and ClearStatus's
    // attack leaves for those two. The attack's own cells end with the command and are not read.
    AttackResult attack;
};

// The port's command `action` (a BattleVariableAction, BattleSwitchAction or AiEffectAction) run by
// CommandExecutor::execute() for the scenario's actor, read back as an AiCommandResult.
[[nodiscard]] AiCommandResult portAiCommand(AiScenario scenario, const battle::Action& action);

[[nodiscard]] std::string differences(const AiCommandResult& port, const AiCommandResult& guest);

// A drawn battle with one of the three commands only a script queues, every operation and every
// effect, its operands drawn within what a script can hold.
struct AiCommandCase {
    AiScenario scenario;
    battle::Action action;
};
[[nodiscard]] AiCommandCase drawAiCommandCase(std::mt19937& rng);

}  // namespace ostinato::trace
