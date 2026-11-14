// The port's script commands, their differences, and the command sweep's draw.

#include "scenarios/ai/ai_commands.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "battle/attack.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/command_executor.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/monster_sprites.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/status_set.h"

#include "scenarios/ai/ai_targets.h"
#include "scenarios/ai/ai_targets_shared.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/attack_shared.h"
#include "scenarios/status/attacker_effects_shared.h"

namespace ostinato::trace {

using namespace detail;

AiCommandResult portAiCommand(AiScenario scenario, const battle::Action& action) {
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, noText(), MonsterSprites{}};
    // Every action starts with the cells of the one before cleared, as running it clears them.
    scenario.state.scratch = battle::ActionScratch{};
    const std::vector<battle::BattleScriptCommand> script = executor.execute(scenario.state, scenario.actor, action);
    const battle::BattleState& state = scenario.state;
    AiCommandResult result;
    result.attack =
        portAttackCells(state, battle::AttackInFlight{}, script, nullptr, state.awayFromTheFieldUnreachable, false);
    result.battleVariables = state.battleVariables;
    result.switches = state.switches.variables;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        result.ownVariables[i] = battler.aiVariable;
        result.ownTimers[i] = battler.turnTimer;
        result.atbRates[i] = battler.atbRate;
        result.statuses[i] = battler.status;
        result.statusesToSet[i] = battler.statusesToSet;
        result.statusesToClear[i] = battler.statusesToClear;
    }
    result.invincible = state.invincible;
    result.aiUntargetable = state.aiUntargetable;
    result.battleCounter = state.battleCounter;
    result.endOfBattleEvent = state.endOfBattleEvent;
    result.gauRecruited = state.gauRecruited;
    result.membership = state.party.membership;
    result.rngPosition = state.rng.position();
    return result;
}

std::string differences(const AiCommandResult& port, const AiCommandResult& guest) {
    EffectCells ours;
    EffectCells theirs;
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    const auto put = [&](EffectCells& out, const AiCommandResult& result) {
        for (std::size_t i = 0; i < result.battleVariables.size(); ++i) {
            out.put("battle variable " + std::to_string(i), result.battleVariables[i]);
        }
        for (std::size_t i = 0; i < result.switches.size(); ++i) {
            out.put("switch byte " + std::to_string(i), result.switches[i]);
        }
        for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
            const std::string of = " of battler " + std::to_string(i);
            out.put("own variable" + of, result.ownVariables[i]);
            out.put("own timer" + of, result.ownTimers[i]);
            out.put("ATB rate" + of, result.atbRates[i]);
            out.put("statuses" + of, statuses(result.statuses[i]));
            out.put("statuses to set" + of, statuses(result.statusesToSet[i]));
            out.put("statuses to clear" + of, statuses(result.statusesToClear[i]));
        }
        out.put("invincible", setText(result.invincible));
        out.put("AI-untargetable", setText(result.aiUntargetable));
        out.put("battle timer", result.battleCounter);
        out.put("end-of-battle event", result.endOfBattleEvent ? static_cast<int>(*result.endOfBattleEvent) : 0);
        out.put("Gau recruited", result.gauRecruited);
        for (std::size_t i = 0; i < result.membership.size(); ++i) {
            const game::PartyMembership& m = result.membership[i];
            out.put("party of record " + std::to_string(i), std::to_string(m.party) + " slot " + std::to_string(m.slot) +
                                                                (m.backRow ? " back" : " front") +
                                                                (m.enabled ? " enabled" : " disabled") +
                                                                (m.visible ? " visible" : " hidden"));
        }
        out.put("random-number position", result.rngPosition);
    };
    put(ours, port);
    put(theirs, guest);
    return cellDifferences(ours.cells, theirs.cells) + differences(port.attack, guest.attack);
}

AiCommandCase drawAiCommandCase(std::mt19937& rng) {
    AiCommandCase c{.scenario = drawAiScenario(rng), .action = battle::NoAction{}};
    battle::BattleState& state = c.scenario.state;
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        state.party.membership[record] = game::PartyMembership{.party = static_cast<std::uint8_t>(upTo(rng, 3)),
                                                               .slot = static_cast<std::uint8_t>(upTo(rng, 3)),
                                                               .backRow = oneIn(rng, 2),
                                                               .enabled = oneIn(rng, 2),
                                                               .visible = oneIn(rng, 2)};
    }
    state.party.currentParty = static_cast<std::uint8_t>(1 + upTo(rng, 2));
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        battle::Battler& battler = state.at(battle::BattlerSlot::fromIndex(index));
        battler.atbRate = static_cast<std::uint16_t>(rng() & 0xFFFF);
        if (index < battle::kPartySlots && battler.actor) {
            battler.record = rng() % game::kCharacterRecords;
        }
    }
    state.invincible = anyBattlers(rng);
    state.aiUntargetable = anyBattlers(rng).monstersOnly();
    if (oneIn(rng, 3)) {
        state.endOfBattleEvent = static_cast<battle::EndOfBattleEvent>(1 + upTo(rng, 5));
    }
    const auto amount = [&] {
        return oneIn(rng, 4) ? static_cast<std::uint8_t>(upTo(rng, 1)) : static_cast<std::uint8_t>(upTo(rng, 63));
    };
    switch (rng() % 3) {
    case 0:
        c.action = battle::BattleVariableAction{.variable = anyAiVariable(rng),
                                                .change = static_cast<AiVariableChange>(rng() % 3),
                                                .amount = amount()};
        break;
    case 1:
        c.action = battle::BattleSwitchAction{.which = anyAiSwitch(rng), .change = static_cast<AiSwitchChange>(rng() % 3)};
        break;
    default: {
        constexpr std::array<AiEffect, 14> effects{
            AiEffect::ResetOwnTimer,  AiEffect::MakeInvincible, AiEffect::EndBattle,       AiEffect::RecruitGau,
            AiEffect::ResetBattleTimer, AiEffect::EndInvincible, AiEffect::MakeTargetable, AiEffect::MakeUntargetable,
            AiEffect::FillGauge,      AiEffect::EndBattleLearningRages, AiEffect::Nothing, AiEffect::SetStatus,
            AiEffect::ClearStatus,    AiEffect::HideSelf};
        const AiEffect effect = effects[rng() % effects.size()];
        c.action = battle::AiEffectAction{.effect = effect, .target = anyAiTarget(rng), .status = anyStatus(rng)};
        // Gau is recruited by a party member's script: the record it joins is the party member's.
        if (effect == AiEffect::RecruitGau) {
            c.scenario.actor = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
            battle::Battler& gau = state.at(c.scenario.actor);
            gau.actor = gau.actor.value_or(CharacterPropId::GAU);
            gau.record = rng() % game::kCharacterRecords;
        }
        break;
    }
    }
    return c;
}

}  // namespace ostinato::trace
