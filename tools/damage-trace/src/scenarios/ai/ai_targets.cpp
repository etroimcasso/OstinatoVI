// The port's targets and conditions, their differences, and the AI sweeps' draws.

#include "scenarios/ai/ai_targets.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <string>

#include "battle/ai_conditions.h"
#include "battle/ai_targets.h"
#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_set.h"

#include "cells/ai/ai.h"
#include "scenarios/ai/ai_targets_shared.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/status/attacker_effects_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

AiTargetsResult portAiTargets(AiScenario scenario, AiTarget target, bool reachableOnly) {
    const battle::BattlerSet targets = battle::aiTargets(scenario.state, scenario.actor, target, reachableOnly);
    return AiTargetsResult{.targets = targets,
                           .any = !targets.empty(),
                           .targetingTarget = scenario.state.at(scenario.actor).targetingTarget,
                           .rngPosition = scenario.state.rng.position()};
}

AiConditionResult portAiCondition(AiScenario scenario, const AiCondition& condition) {
    const bool holds = battle::aiCondition(scenario.state, scenario.actor, condition, scenario.run);
    return AiConditionResult{.holds = holds,
                             .targets = scenario.run.targets,
                             .restricted = scenario.run.restricted,
                             .targetingTarget = scenario.state.at(scenario.actor).targetingTarget,
                             .rngPosition = scenario.state.rng.position()};
}

std::string differences(const AiTargetsResult& port, const AiTargetsResult& guest) {
    EffectCells ours;
    EffectCells theirs;
    const auto put = [](EffectCells& out, const AiTargetsResult& result) {
        out.put("targets", setText(result.targets));
        out.put("any", result.any);
        out.put("Targetting", markText(result.targetingTarget));
        out.put("random-number position", result.rngPosition);
    };
    put(ours, port);
    put(theirs, guest);
    return cellDifferences(ours.cells, theirs.cells);
}

std::string differences(const AiConditionResult& port, const AiConditionResult& guest) {
    EffectCells ours;
    EffectCells theirs;
    const auto put = [](EffectCells& out, const AiConditionResult& result) {
        out.put("holds", result.holds);
        out.put("targets", setText(result.targets));
        out.put("restricted", result.restricted);
        out.put("Targetting", markText(result.targetingTarget));
        out.put("random-number position", result.rngPosition);
    };
    put(ours, port);
    put(theirs, guest);
    return cellDifferences(ours.cells, theirs.cells);
}
namespace {

// The party members the AI draws pick among: the first sixteen, so a drawn target often names one
// in the party.
constexpr unsigned kDrawnCharacters = 16;
constexpr unsigned kFormationCount = 576;
constexpr unsigned kSelectorCount = 30;
constexpr int kPartyMaxTimer = 600;
constexpr int kHpStep = 128;
}  // namespace

namespace detail {

AiTarget anyAiTarget(std::mt19937& rng) {
    if (oneIn(rng, 4)) {
        return AiTarget{static_cast<CharacterPropId>(rng() % kDrawnCharacters)};
    }
    return AiTarget{static_cast<AiTargetSelector>(static_cast<unsigned>(AiTargetSelector::MONSTER_SLOT_1) +
                                                  rng() % kSelectorCount)};
}
}  // namespace detail

namespace {

StatusSet drawnStatuses(std::mt19937& rng) {
    if (oneIn(rng, 3)) {
        return anyStatuses(rng);
    }
    StatusSet statuses{{sparse(rng), sparse(rng), sparse(rng), sparse(rng)}};
    if (oneIn(rng, 3)) {
        statuses.set(anyStatus(rng));
    }
    return statuses;
}
}  // namespace

namespace detail {

AiVariable anyAiVariable(std::mt19937& rng) {
    return oneIn(rng, 3) ? AiVariable::own() : AiVariable::battle(static_cast<std::uint8_t>(rng() % kAiBattleVariables));
}

AiSwitch anyAiSwitch(std::mt19937& rng) {
    const auto bit = static_cast<std::uint8_t>(rng() % 8);
    switch (rng() % 3) {
    case 0:
        return AiSwitch::own(bit);
    case 1:
        return AiSwitch::battleVariable(static_cast<std::uint8_t>(rng() % kAiBattleVariables), bit);
    default:
        return AiSwitch::battleSwitch(static_cast<int>(rng() % kBattleSwitchCount));
    }
}
}  // namespace detail

namespace {

AiMonsters anyAiMonsters(std::mt19937& rng, bool selfAllowed) {
    AiMonsters monsters;
    do {
        monsters.slots = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F));
    } while (!selfAllowed && monsters.isSelf());
    if (selfAllowed && oneIn(rng, 4)) {
        monsters = AiMonsters::self();
    }
    return monsters;
}

// A small byte, often at a value a drawn battle holds.
std::uint8_t nearly(std::mt19937& rng, int around) {
    return static_cast<std::uint8_t>(std::clamp(around + upTo(rng, 2) - 1, 0, 255));
}

// HP at a multiple of the HP condition's step, or a little either side of one.
std::uint16_t hpAroundStep(std::mt19937& rng) {
    return static_cast<std::uint16_t>(std::max(0, kHpStep * upTo(rng, 40) + upTo(rng, 2) - 1));
}
}  // namespace

AiScenario drawAiScenario(std::mt19937& rng) {
    AiScenario s;
    battle::BattleState& state = s.state;
    s.actor = anySlot(rng);
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        battle::Battler& battler = state.at(slot);
        battler.present = !oneIn(rng, 4);
        if (slot.isCharacter()) {
            if (!oneIn(rng, 4)) {
                battler.actor = static_cast<CharacterPropId>(rng() % kDrawnCharacters);
            }
        } else if (!oneIn(rng, 4)) {
            battler.monster = static_cast<MonsterId>(rng() % kMonsterCount);
        }
        battler.status = drawnStatuses(rng);
        battler.hp = oneIn(rng, 2) ? hpAroundStep(rng) : static_cast<std::uint16_t>(upTo(rng, kPartyMaxHp));
        battler.mp = static_cast<std::uint16_t>(oneIn(rng, 2) ? upTo(rng, 300) : upTo(rng, kPartyMaxMp));
        battler.level = oneIn(rng, 2) ? static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1)) : byte(rng);
        battler.elementsWeak.bits = sparse(rng);
        if (!oneIn(rng, 3)) {
            battler.lastAttacker = anySlot(rng);
            battler.retaliationPending = oneIn(rng, 3);
        }
        if (oneIn(rng, 3)) {
            battler.targetingTarget = anySlot(rng);
        }
        const auto hit = [&](auto with) {
            using T = decltype(with);
            return oneIn(rng, 2) ? std::optional<battle::Hit<T>>{} : battle::Hit<T>{.by = anySlot(rng), .with = with};
        };
        battler.lastHit.command = hit(static_cast<BattleCommandId>(rng() % 32));
        battler.lastHit.spell = hit(static_cast<AttackId>(byte(rng)));
        battler.lastHit.item = hit(static_cast<ItemId>(byte(rng)));
        battler.lastHit.element = hit(ElementSet{sparse(rng)});
        if (oneIn(rng, 2)) {
            battler.lastDamagedBy = anySlot(rng);
        }
        battler.aiVariable = oneIn(rng, 2) ? static_cast<std::uint8_t>(upTo(rng, 10)) : byte(rng);
        battler.turnTimer = static_cast<std::uint16_t>(upTo(rng, kPartyMaxTimer));
        if (slot.isCharacter() && oneIn(rng, 6)) {
            battler.seizedBy = battle::BattlerSlot::monster(upTo(rng, battle::kMonsterSlots - 1));
        }
    }
    state.targetableOrPresent = oneIn(rng, 3) ? anyBattlers(rng) : battle::BattlerSet::allCharacters() | battle::BattlerSet::allMonsters();
    state.countedInBattle = oneIn(rng, 3) ? anyBattlers(rng) : battle::BattlerSet::allCharacters() | battle::BattlerSet::allMonsters();
    state.aiUntargetable = oneIn(rng, 3) ? anyBattlers(rng).monstersOnly() : battle::BattlerSet{};
    state.awayFromTheField = oneIn(rng, 3) ? anyBattlers(rng) : battle::BattlerSet{};
    state.awayFromTheFieldUnreachable = !oneIn(rng, 3);
    state.alive = anyBattlers(rng);
    state.allyCount = upTo(rng, battle::kPartySlots);
    state.enemyCount = upTo(rng, battle::kMonsterSlots + 2);
    state.battleCounter = static_cast<std::uint16_t>(upTo(rng, kPartyMaxTimer));
    for (std::uint8_t& variable : state.battleVariables) {
        variable = oneIn(rng, 2) ? static_cast<std::uint8_t>(upTo(rng, 10)) : byte(rng);
    }
    for (std::uint8_t& variable : state.switches.variables) {
        variable = byte(rng);
    }
    state.monsterNamesListed = static_cast<std::uint8_t>(upTo(rng, static_cast<int>(battle::kMonsterNameLines)));
    state.gauRecruited = oneIn(rng, 2);
    state.formation = static_cast<FormationId>(rng() % kFormationCount);
    state.rng = battle::BattleRng(byte(rng));
    s.run.targets = anyBattlers(rng);
    s.run.startingTargets = anyBattlers(rng);
    s.run.restricted = oneIn(rng, 3);
    return s;
}

AiTargetsCase drawAiTargetsCase(std::mt19937& rng) {
    AiTargetsCase c{.scenario = drawAiScenario(rng)};
    c.target = anyAiTarget(rng);
    c.reachableOnly = oneIn(rng, 2);
    return c;
}

AiConditionCase drawAiConditionCase(std::mt19937& rng) {
    AiConditionCase c{.scenario = drawAiScenario(rng), .condition = ai::Never{}};
    const battle::BattleState& state = c.scenario.state;
    const battle::Battler& actor = state.at(c.scenario.actor);
    const battle::Battler& other = state.at(anySlot(rng));
    switch (rng() % std::variant_size_v<AiCondition>) {
    case 0:
        c.condition = ai::Never{};
        break;
    case 1: {
        const auto command = actor.lastHit.command && oneIn(rng, 2) ? actor.lastHit.command->with
                                                                   : static_cast<BattleCommandId>(rng() % 32);
        c.condition = ai::HitByCommand{static_cast<BattleCommandId>(rng() % 32), command};
        break;
    }
    case 2: {
        const auto spell = actor.lastHit.spell && oneIn(rng, 2) ? actor.lastHit.spell->with : static_cast<AttackId>(byte(rng));
        c.condition = ai::HitByAttack{spell, static_cast<AttackId>(byte(rng))};
        break;
    }
    case 3: {
        const auto item = actor.lastHit.item && oneIn(rng, 2) ? actor.lastHit.item->with : static_cast<ItemId>(byte(rng));
        c.condition = ai::HitByItem{static_cast<ItemId>(byte(rng)), item};
        break;
    }
    case 4: {
        const auto elements = actor.lastHit.element && oneIn(rng, 2) ? actor.lastHit.element->with : ElementSet{sparse(rng)};
        c.condition = ai::HitByElement{elements};
        break;
    }
    case 5:
        c.condition = ai::Damaged{};
        break;
    case 6:
        c.condition = ai::HpAtMost{.target = anyAiTarget(rng),
                                   .hp = static_cast<std::uint16_t>(kHpStep * (oneIn(rng, 2) ? other.hp / kHpStep + upTo(rng, 1)
                                                                                             : upTo(rng, 255)))};
        break;
    case 7:
        c.condition = ai::MpAtMost{.target = anyAiTarget(rng), .mp = oneIn(rng, 2) ? nearly(rng, other.mp) : byte(rng)};
        break;
    case 8:
        c.condition = ai::HasStatus{.target = anyAiTarget(rng), .status = anyStatus(rng)};
        break;
    case 9:
        c.condition = ai::NoStatus{.target = anyAiTarget(rng), .status = anyStatus(rng)};
        break;
    case 10:
        c.condition = ai::HalfOwnTimerAtLeast{nearly(rng, actor.turnTimer / 2)};
        break;
    case 11:
        c.condition = ai::VariableBelow{.variable = anyAiVariable(rng), .value = static_cast<std::uint8_t>(upTo(rng, 10))};
        break;
    case 12:
        c.condition = ai::VariableAtLeast{.variable = anyAiVariable(rng), .value = static_cast<std::uint8_t>(upTo(rng, 10))};
        break;
    case 13:
        c.condition = ai::LevelBelow{.target = anyAiTarget(rng), .level = oneIn(rng, 2) ? nearly(rng, other.level) : byte(rng)};
        break;
    case 14:
        c.condition = ai::LevelAtLeast{.target = anyAiTarget(rng), .level = oneIn(rng, 2) ? nearly(rng, other.level) : byte(rng)};
        break;
    case 15:
        c.condition = ai::OneKindOfMonster{};
        break;
    case 16:
        c.condition = ai::MonstersAlive{anyAiMonsters(rng, true)};
        break;
    case 17:
        c.condition = ai::MonstersDead{anyAiMonsters(rng, true)};
        break;
    case 18:
        c.condition = ai::PartyAtLeast{static_cast<std::uint8_t>(upTo(rng, battle::kPartySlots + 1))};
        break;
    case 19:
        c.condition = ai::MonstersAtMost{static_cast<std::uint8_t>(upTo(rng, battle::kMonsterSlots + 2))};
        break;
    case 20:
        c.condition = ai::SwitchSet{anyAiSwitch(rng)};
        break;
    case 21:
        c.condition = ai::SwitchClear{anyAiSwitch(rng)};
        break;
    case 22:
        c.condition = ai::HalfBattleTimerAtLeast{nearly(rng, state.battleCounter / 2)};
        break;
    case 23:
        c.condition = ai::TargetValid{anyAiTarget(rng)};
        break;
    case 24:
        c.condition = ai::GauNotRecruited{};
        break;
    case 25:
        c.condition = ai::InSlot{anyAiMonsters(rng, false)};
        break;
    case 26:
        c.condition = ai::WeakTo{.target = anyAiTarget(rng), .elements = ElementSet{sparse(rng)}};
        break;
    case 27:
        c.condition = ai::FormationIs{oneIn(rng, 2) ? state.formation : static_cast<FormationId>(rng() % kFormationCount)};
        break;
    default:
        c.condition = ai::Always{};
        break;
    }
    return c;
}

std::string describe(const AiScenario& s) {
    return "actor " + std::to_string(s.actor.index()) + " random-number position " +
           std::to_string(s.state.rng.position()) + " run targets " + setText(s.run.targets) + " starting " +
           setText(s.run.startingTargets) + (s.run.restricted ? " restricted" : "");
}

}  // namespace ostinato::trace
