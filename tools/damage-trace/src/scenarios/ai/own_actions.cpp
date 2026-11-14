// The port's own actions, the bytes the cartridge queues each as, and the own-action sweep's draw.

#include "scenarios/ai/own_actions.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "battle/attack.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/command_executor.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/monster_sprites.h"
#include "ostinato/attack_id.h"
#include "ostinato/status_id.h"

#include "scenarios/ai/ai_commands.h"
#include "scenarios/ai/ai_run.h"
#include "scenarios/ai/ai_targets.h"
#include "scenarios/ai/ai_targets_shared.h"
#include "scenarios/ai/own_actions_shared.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/attack.h"
#include "scenarios/damage/attack_shared.h"
#include "scenarios/damage/damage.h"

namespace ostinato::trace {

using namespace detail;

namespace {

// Damage over time names the status that fired (rigenetimer, battle_main.asm:15000-15015): Poison's
// bit of the first status byte, Regen's of the fourth, and the bit Sap and Phantasm share.
std::uint8_t damageOverTimeByte(battle::DamageOverTimeKind kind) {
    switch (kind) {
    case battle::DamageOverTimeKind::Poison:
        return 0x04;
    case battle::DamageOverTimeKind::Regen:
        return 0x02;
    case battle::DamageOverTimeKind::Sap:
        return 0x40;
    }
    return 0;
}

// The timers that ran out, one bit each (timer, battle_main.asm:14933-14959): Stop, Reflect, Freeze
// and Sleep.
constexpr std::uint8_t kStopRanOut = 0x01;
constexpr std::uint8_t kReflectRanOut = 0x02;
constexpr std::uint8_t kFreezeRanOut = 0x04;
constexpr std::uint8_t kSleepRanOut = 0x08;
}  // namespace

namespace detail {

OwnActionBytes ownActionBytes(const battle::Action& action) {
    if (const auto* dot = std::get_if<battle::DamageOverTime>(&action)) {
        return {.routine = 0, .command = commandByte(battle::kDamageOverTimeCommand), .attack = damageOverTimeByte(dot->kind)};
    }
    if (const auto* spell = std::get_if<battle::SpellWithoutCaster>(&action)) {
        // The battler in the attack byte, the spell in the targets' first byte.
        return {.routine = 1,
                .command = commandByte(battle::kSpellWithoutCasterCommand),
                .attack = static_cast<std::uint8_t>(slotIndex(spell->target)),
                .targets = static_cast<std::uint8_t>(spell->spell)};
    }
    if (const auto* expired = std::get_if<battle::StatusTimersExpired>(&action)) {
        std::uint8_t bits = expired->stop ? kStopRanOut : 0;
        bits |= expired->reflect ? kReflectRanOut : 0;
        bits |= expired->freeze ? kFreezeRanOut : 0;
        bits |= expired->sleep ? kSleepRanOut : 0;
        return {.routine = 2,
                .command = commandByte(battle::kStatusTimersCommand),
                .attack = static_cast<std::uint8_t>(slotIndex(expired->battler)),
                .targets = bits};
    }
    if (const auto* seize = std::get_if<battle::SeizeDrain>(&action)) {
        return {.routine = 3, .command = commandByte(battle::kSeizeDrainCommand), .targets = maskOf(battle::BattlerSet::of(seize->seized))};
    }
    throw std::logic_error("ownAction() runs damage over time, a spell with no caster, status timers running out, "
                           "Seize's drain and a script's effect");
}

void putOwnActionCells(AttackResult& result, const std::array<bool, battle::kBattlerSlots>& waiting,
                       const std::array<int, battle::kBattlerSlots>& multiplier) {
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string of = " of battler " + std::to_string(i);
        result.cells["damage over time waiting" + of] = waiting[i] ? "1" : "0";
        result.cells["poison multiplier" + of] = std::to_string(multiplier[i]);
    }
}
}  // namespace detail

AttackResult portOwnAction(DamageScenario scenario, const battle::Action& action) {
    if (std::holds_alternative<battle::AiEffectAction>(action)) {
        return portAiCommand(AiScenario{.state = std::move(scenario.state), .actor = scenario.attacker}, action).attack;
    }
    (void)ownActionBytes(action);
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, noText(), MonsterSprites{}};
    battle::BattleState& state = scenario.state;
    // Every action starts with the cells of the one before cleared, as running it clears them.
    state.scratch = battle::ActionScratch{};
    const std::vector<battle::BattleScriptCommand> script = executor.execute(state, scenario.attacker, action);
    AttackResult result =
        portAttackCells(state, battle::AttackInFlight{}, script, nullptr, state.awayFromTheFieldUnreachable, false);
    std::array<bool, battle::kBattlerSlots> waiting{};
    std::array<int, battle::kBattlerSlots> multiplier{};
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        waiting[i] = state.battlers[i].dotActionPending;
        multiplier[i] = state.battlers[i].poisonMultiplier;
    }
    putOwnActionCells(result, waiting, multiplier);
    return result;
}

OwnActionCase drawOwnActionCase(std::mt19937& rng) {
    OwnActionCase c{.scenario = drawAttackScenario(rng), .action = battle::NoAction{}};
    battle::BattleState& state = c.scenario.state;
    state.enemyCount = oneIn(rng, 8) ? 0 : static_cast<std::uint8_t>(1 + upTo(rng, battle::kMonsterSlots - 1));
    for (battle::Battler& battler : state.battlers) {
        battler.poisonMultiplier = 2 * upTo(rng, 7);
        battler.heldByStatus = oneIn(rng, 4);
        battler.dotActionPending = oneIn(rng, 4);
    }
    // Three times in four the battler an action acts on can be aimed at: on the field, counted in
    // the battle and targetable by anyone.
    const auto reachableMostly = [&](battle::BattlerSlot slot) {
        if (oneIn(rng, 4)) {
            return;
        }
        state.at(slot).present = true;
        state.targetableOrPresent.add(slot);
        state.countedInBattle.add(slot);
        state.aiUntargetable.remove(slot);
        state.awayFromTheField.remove(slot);
    };
    // Held three times in four: the status an action stands for.
    const auto heldMostly = [&](battle::Battler& battler, StatusId status) {
        if (oneIn(rng, 4)) {
            battler.status.clear(status);
        } else {
            battler.status.set(status);
        }
    };
    switch (rng() % 5) {
    case 0: {
        constexpr std::array<std::pair<battle::DamageOverTimeKind, StatusId>, 3> kinds{{
            {battle::DamageOverTimeKind::Poison, StatusId::POISON},
            {battle::DamageOverTimeKind::Regen, StatusId::REGEN},
            {battle::DamageOverTimeKind::Sap, StatusId::SAP},
        }};
        const auto& [kind, status] = kinds[rng() % kinds.size()];
        battle::Battler& actor = state.at(c.scenario.attacker);
        actor.dotActionPending = true;
        heldMostly(actor, status);
        reachableMostly(c.scenario.attacker);
        c.action = battle::DamageOverTime{.kind = kind};
        break;
    }
    case 1: {
        const bool doom = oneIn(rng, 3);
        const AttackId spell = doom ? AttackId::DOOM : static_cast<AttackId>(upTo(rng, 53));
        if (doom && oneIn(rng, 4)) {
            state.enemyCount = 0;
        }
        const battle::BattlerSlot target = anySlot(rng);
        reachableMostly(target);
        c.action = battle::SpellWithoutCaster{.spell = spell, .target = target};
        break;
    }
    case 2: {
        battle::StatusTimersExpired expired{.battler = anySlot(rng)};
        while (!(expired.stop || expired.reflect || expired.freeze || expired.sleep)) {
            expired.stop = oneIn(rng, 2);
            expired.reflect = oneIn(rng, 2);
            expired.freeze = oneIn(rng, 2);
            expired.sleep = oneIn(rng, 2);
        }
        battle::Battler& battler = state.at(expired.battler);
        reachableMostly(expired.battler);
        for (const auto& [ran, status] : {std::pair{expired.stop, StatusId::STOP}, std::pair{expired.reflect, StatusId::REFLECT},
                                          std::pair{expired.freeze, StatusId::FROZEN},
                                          std::pair{expired.sleep, StatusId::SLEEP}}) {
            if (ran) {
                heldMostly(battler, status);
            }
        }
        c.action = expired;
        break;
    }
    case 3: {
        // A monster holding a party member, the only hold the battle makes.
        for (battle::Battler& battler : state.battlers) {
            battler.seizing.reset();
            battler.seizedBy.reset();
        }
        const battle::BattlerSlot seizer = battle::BattlerSlot::monster(upTo(rng, battle::kMonsterSlots - 1));
        const battle::BattlerSlot seized = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
        c.scenario.attacker = seizer;
        state.at(seizer).seizing = seized;
        state.at(seized).seizedBy = seizer;
        state.at(seizer).dotActionPending = true;
        reachableMostly(seizer);
        reachableMostly(seized);
        c.action = battle::SeizeDrain{.seized = seized};
        break;
    }
    default:
        reachableMostly(c.scenario.attacker);
        c.action = battle::AiEffectAction{.effect = oneIn(rng, 2) ? AiEffect::SetStatus : AiEffect::ClearStatus,
                                          .target = anyAiTarget(rng),
                                          .status = anyStatus(rng)};
        break;
    }
    return c;
}

std::string describe(const OwnActionCase& drawn) {
    std::string action = std::visit(
        [](const auto& a) -> std::string {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, battle::DamageOverTime>) {
                constexpr std::array<const char*, 3> kinds{"Poison", "Regen", "Sap"};
                return std::string{"damage over time, "} + kinds[static_cast<std::size_t>(a.kind)];
            } else if constexpr (std::is_same_v<T, battle::SpellWithoutCaster>) {
                return "spell " + std::to_string(static_cast<int>(a.spell)) + " at " + std::to_string(a.target.index());
            } else if constexpr (std::is_same_v<T, battle::StatusTimersExpired>) {
                return "timers out at " + std::to_string(a.battler.index()) + (a.stop ? " Stop" : "") +
                       (a.reflect ? " Reflect" : "") + (a.freeze ? " Freeze" : "") + (a.sleep ? " Sleep" : "");
            } else if constexpr (std::is_same_v<T, battle::SeizeDrain>) {
                return "Seize's drain of " + std::to_string(a.seized.index());
            } else {
                return listedActionText(a);
            }
        },
        drawn.action);
    return describe(drawn.scenario) + " enemies " + std::to_string(drawn.scenario.state.enemyCount) + "; " + action;
}

}  // namespace ostinato::trace
