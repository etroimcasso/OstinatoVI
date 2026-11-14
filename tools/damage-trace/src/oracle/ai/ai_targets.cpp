// The cartridge's script targets and conditions run over a battle.

#include "oracle/damage_oracle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "retropp/memory_region.h"

#include "ai_script_bytes.h"
#include "battle/ai_conditions.h"
#include "battle/ai_targets.h"
#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "game/battle_switches.h"

#include "cells/ai/ai.h"
#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"
#include "oracle/routines.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

// The script reader's cells: the four bytes of the step it is at (the step's command, then a
// condition's number and two operands), whether the run is restricted (one byte, 0 when it is not),
// the run's targets and the targets it started with.
constexpr std::uint32_t kAiStep = kWorkRam + 0x3A2C;
// The step command a condition goes out as.
constexpr std::uint8_t kAiConditionStep = 0xFC;
constexpr std::uint8_t kAiRestrictedOn = 0xFF;

std::optional<battle::BattlerSlot> slotAt(std::uint8_t value) {
    if (value >= kTopBit) {
        return std::nullopt;
    }
    return battle::BattlerSlot::fromIndex(value / 2);
}
}  // namespace

void DamageOracle::writeAiCells(const AiScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        writeWord(kBattlerBit + at, maskOf(battle::BattlerSet::of(slot)));
        if (slot.isCharacter()) {
            writeByte(kActor + at, battler.actor ? static_cast<std::uint8_t>(*battler.actor) : kNoOne);
        } else {
            writeWord(kMonsterIndex + at, battler.monster ? static_cast<std::uint16_t>(*battler.monster) : kNoMonster);
        }
        std::uint8_t field = battler.present ? kPresentBit : 0;
        field |= battler.inBattleMenu ? kInBattleMenuBit : 0;
        field |= battler.menuMayOpen ? kMenuMayOpenBit : 0;
        writeByte(kOnTheField + at, field);
        writeByte(kStatus12 + at, battler.status.bytes[0]);
        writeByte(kStatus12 + at + 1, battler.status.bytes[1]);
        writeByte(kStatus34 + at, battler.status.bytes[2]);
        writeByte(kStatus34 + at + 1, battler.status.bytes[3]);
        writeWord(kHp + at, battler.hp);
        writeWord(kMp + at, battler.mp);
        writeByte(kLevel + at, battler.level);
        writeByte(kWeakHalved + at, battler.elementsWeak.bits);
        writeByte(kLastAttacker + at, markByte(battler));
        writeByte(kTargetingTarget + at, slotByte(battler.targetingTarget));
        const battle::LastHits& last = battler.lastHit;
        const auto by = [](const auto& hit) { return hit ? static_cast<std::uint8_t>(slotIndex(hit->by)) : kNoOne; };
        writeByte(kLastHitCommandBy + at, by(last.command));
        writeByte(kLastHitCommand + at, last.command ? static_cast<std::uint8_t>(last.command->with) : kNoOne);
        writeByte(kLastHitSpellBy + at, by(last.spell));
        writeByte(kLastHitSpell + at, last.spell ? static_cast<std::uint8_t>(last.spell->with) : kNoOne);
        writeByte(kLastHitItemBy + at, by(last.item));
        writeByte(kLastHitItem + at, last.item ? static_cast<std::uint8_t>(last.item->with) : kNoOne);
        writeByte(kLastHitElementBy + at, by(last.element));
        writeByte(kLastHitElement + at, last.element ? last.element->with.bits : 0);
        writeByte(kLastDamagedBy + at, slotByte(battler.lastDamagedBy));
        writeByte(kAiVariable + at, battler.aiVariable);
        writeWord(kOwnTimer + at, battler.turnTimer);
    }

    // The reach check's four cells, written so that what they leave between them is the port's own
    // reachable set (reachableTargets()), and the sign of the word above the counterattack cell the
    // reach flag the last command left.
    writeByte(kReachMonsters, 0xFF);
    writeByte(kReachCharacters, static_cast<std::uint8_t>(~seizedSet(state).characters.bits));
    writeWord(kReachFirst, maskOf(state.targetableOrPresent & state.countedInBattle));
    writeWord(kReachSecond, static_cast<std::uint16_t>(~maskOf(state.aiUntargetable)));
    writeByte(kCountersEnabled, state.scratch.countersEnabled ? kCountersOn : 0);
    writeByte(kReachKeepsAway, state.awayFromTheFieldUnreachable ? kAwayUnreachable : 0);
    writeWord(kAwayFromTheField, maskOf(state.awayFromTheField));

    std::uint8_t held = 0;
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        if (state.at(battle::BattlerSlot::monster(position)).monster) {
            held = static_cast<std::uint8_t>(held | 1u << position);
        }
    }
    writeWord(kAlive, maskOf(state.alive));
    writeByte(kMonstersHeld, held);
    writeByte(kAllyCount, static_cast<std::uint8_t>(state.allyCount));
    writeByte(kEnemyCount, static_cast<std::uint8_t>(state.enemyCount));
    writeWord(kBattleTimer, state.battleCounter);
    writeBytes(retropp::MemoryRegion{.at = kBattleVariables, .size = kAiBattleVariables}, state.battleVariables);
    writeBytes(retropp::MemoryRegion{.at = kBattleSwitches, .size = game::kBattleVariables}, state.switches.variables);
    writeByte(kNamesListed, state.monsterNamesListed);
    const std::uint8_t story = readByte(kStoryFlags);
    writeByte(kStoryFlags, static_cast<std::uint8_t>(state.gauRecruited ? story | kGauRecruitedBit
                                                                        : story & ~kGauRecruitedBit));
    writeWord(kFormation, static_cast<std::uint16_t>(state.formation));
    writeByte(kRngPosition, state.rng.position());

    writeInFlightWord(kAiTargets, maskOf(scenario.run.targets));
    writeInFlightWord(kAiStartingTargets, maskOf(scenario.run.startingTargets));
    writeInFlightByte(kAiRestricted, scenario.run.restricted ? kAiRestrictedOn : 0);
}

std::optional<battle::BattlerSlot> DamageOracle::readTargetingTarget(battle::BattlerSlot actor) {
    return slotAt(readByte(kTargetingTarget + slotIndex(actor)));
}

AiTargetsResult DamageOracle::aiTargets(const AiScenario& scenario, AiTarget target, bool reachableOnly) {
    writeAiCells(scenario);
    std::uint8_t status = 0;
    const std::uint8_t entry = reachableOnly ? kByteWideStatus : static_cast<std::uint8_t>(kByteWideStatus | kCarryBit);
    call([&] {
        status = checkAiTarget_(test::ai_bytes::target(target), slotIndex(scenario.actor), entry, kBattleDataBank,
                                kBattleDirectPage);
    });
    return AiTargetsResult{.targets = setOf(readWord(kTargets)),
                           .any = (status & kCarryBit) != 0,
                           .targetingTarget = readTargetingTarget(scenario.actor),
                           .rngPosition = readByte(kRngPosition)};
}

AiConditionResult DamageOracle::aiCondition(const AiScenario& scenario, const AiCondition& condition) {
    writeAiCells(scenario);
    const test::ai_bytes::ConditionBytes bytes = test::ai_bytes::condition(condition);
    const std::array<std::uint8_t, 4> step{kAiConditionStep, bytes.number, bytes.first, bytes.second};
    writeBytes(retropp::MemoryRegion{.at = kAiStep, .size = static_cast<std::uint32_t>(step.size())}, step);
    std::uint8_t status = 0;
    call([&] {
        status = aiConditions_.at(bytes.number)(slotIndex(scenario.actor), kByteWideStatus, kBattleDataBank,
                                                kBattleDirectPage);
    });
    return AiConditionResult{.holds = (status & kCarryBit) != 0,
                             .targets = setOf(readWord(kAiTargets)),
                             .restricted = readByte(kAiRestricted) != 0,
                             .targetingTarget = readTargetingTarget(scenario.actor),
                             .rngPosition = readByte(kRngPosition)};
}

}  // namespace ostinato::trace
