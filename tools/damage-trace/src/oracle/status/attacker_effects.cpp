// The cartridge's special effect on the attacker run over a scenario and read back as named cells.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/attack.h"
#include "battle/attacker_effects.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/attack_properties.h"
#include "ostinato/attack_flags.h"
#include "ostinato/status_set.h"

#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"
#include "oracle/routines.h"
#include "oracle/status/attacker_effects_shared.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/report_text.h"
#include "scenarios/status/attacker_effects_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

constexpr std::uint32_t kParamsAttacker = kWorkRam + 0x00A1;
constexpr std::uint32_t kParamsTargets = kWorkRam + 0x00A2;
constexpr std::uint32_t kParamsHit = kWorkRam + 0x00A4;

constexpr std::uint8_t kParamsMonsterBit = 0x80;
constexpr std::uint8_t kParamsNoCharacterTargetsBit = 0x40;
constexpr std::uint8_t kParamsSwirlBit = 0x10;
constexpr std::uint8_t kParamsDragonHornBit = 0x02;
constexpr std::uint8_t kDragonHornOn = 0xFF;
}  // namespace

namespace detail {

battle::AttackAnimationParams paramsFromBytes(std::span<const std::uint8_t> block) {
    battle::AttackAnimationParams params;
    params.attackerIsMonster = (block[0] & kParamsMonsterBit) != 0;
    params.noCharacterTargets = (block[0] & kParamsNoCharacterTargetsBit) != 0;
    params.castingSwirl = (block[0] & kParamsSwirlBit) != 0;
    params.dragonHorn = (block[0] & kParamsDragonHornBit) != 0;
    params.attacker = battle::BattlerSlot::fromIndex(block[1] % battle::kBattlerSlots);
    params.targets = setOf(static_cast<std::uint16_t>(block[2] | block[3] << 8));
    params.targetsHit = setOf(static_cast<std::uint16_t>(block[4] | block[5] << 8));
    params.reflectedOff = setOf(static_cast<std::uint16_t>(block[6] | block[7] << 8));
    return params;
}
}  // namespace detail

void DamageOracle::writeEffectCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    const battle::AttackAnimationParams& params = attack.params;
    std::uint8_t paramsFlags = 0;
    paramsFlags |= params.attackerIsMonster ? kParamsMonsterBit : 0;
    paramsFlags |= params.noCharacterTargets ? kParamsNoCharacterTargetsBit : 0;
    paramsFlags |= params.castingSwirl ? kParamsSwirlBit : 0;
    paramsFlags |= params.dragonHorn ? kParamsDragonHornBit : 0;
    writeInFlightByte(kParamsFlags, paramsFlags);
    writeInFlightByte(kParamsAttacker, static_cast<std::uint8_t>(params.attacker.index()));
    writeInFlightWord(kParamsTargets, maskOf(params.targets));
    writeInFlightWord(kParamsHit, maskOf(params.targetsHit));
    writeInFlightWord(kParamsReflectedOff, maskOf(params.reflectedOff));
    writeInFlightByte(kSwirlCell, attack.castingSwirlSuppressed ? kSwirlSuppressedBit : 0);
    std::uint8_t running = state.counterattackRunning ? kCounterattackBit : 0;
    running |= state.cannotRun ? kCannotRunBit : 0;
    running |= state.escapeBlockedByMonster ? kEscapeBlockedBit : 0;
    writeByte(kRunningCell, running);
    std::uint8_t second = readByte(kAttackFlags);
    if (attack.automaticCritical) {
        second &= static_cast<std::uint8_t>(~kAutomaticCriticalClearBit);
    }
    writeInFlightByte(kAttackFlags, second);
    std::uint8_t aimFlags = attack.scratch.aimExactly ? kAimExactlyBit : 0;
    aimFlags |= state.endTurnAfterAction ? kEndTurnBit : 0;
    writeByte(kAimFlags, aimFlags);

    writeInFlightByte(kFollowUpSpell, attack.scratch.followUpSpell ? static_cast<std::uint8_t>(*attack.scratch.followUpSpell) : kNoOne);
    writeInFlightByte(kMessage, attack.scratch.message.value_or(kNoOne));
    writeInFlightByte(kAttacksLeft, static_cast<std::uint8_t>(attack.scratch.attacksLeft));
    writeInFlightByte(kDragonHorn, attack.scratch.dragonHorn ? kDragonHornOn : 0);
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        const auto& pick = attack.scratch.mindBlastPicks[position];
        writeInFlightWord(kMindBlastPicks + static_cast<std::uint32_t>(position * 2), pick ? maskOf(battle::BattlerSet::of(*pick)) : 0);
    }
    writeByte(kQuickTurns, state.quickTurnsLeft.value_or(kNoQuick));
    writeByte(kQuickTarget, state.quickTarget ? static_cast<std::uint8_t>(slotIndex(*state.quickTarget)) : kNoOne);
    writeByte(kEndOfBattleEvent,
              state.endOfBattleEvent ? static_cast<std::uint8_t>(static_cast<std::uint8_t>(*state.endOfBattleEvent) * 2) : 0);
    writeWord(kLeaving, maskOf(state.leavingAfterAction));
    writeByte(kAllyCount, static_cast<std::uint8_t>(state.allyCount));
    writeWord(kGolemHp, state.golemHp);
    writeInFlightWord(kMissed, maskOf(attack.scratch.missed));
    // The script's queue starts empty, and the params blocks after the frame's own.
    writeInFlightByte(kQueueEnd, 0);
    writeInFlightByte(kAnimationPlace, 0);

    std::array<std::uint8_t, 3> three{};
    writeThreeBytes(three, state.party.gil);
    writeBytes(retropp::MemoryRegion{.at = kGil, .size = 3}, three);
    writeThreeBytes(three, state.party.steps);
    writeBytes(retropp::MemoryRegion{.at = kSteps, .size = 3}, three);
    writeByte(kPlayClockFrames, state.playClockFrames);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        std::uint8_t field = battler.present ? kPresentBit : 0;
        field |= battler.inBattleMenu ? kInBattleMenuBit : 0;
        field |= battler.menuMayOpen ? kMenuMayOpenBit : 0;
        writeByte(kOnTheField + at, field);
        writeByte(kSeizing + at, battler.seizing ? static_cast<std::uint8_t>(slotIndex(*battler.seizing)) : kNoOne);
        writeByte(kMonsterBattleFlags + at, battler.monsterFlags.bits);
        writeWord(kGold + at, battler.gold);
        if (slot.isCharacter()) {
            writeByte(kHandPower + at, battler.weaponHand.battlePower);
            writeByte(kHandPower + at + 1, battler.shieldHand.battlePower);
            const auto place = static_cast<std::uint32_t>(slot.position()) * kHandItemSize;
            const battle::BattleItem& weapon = state.inventory.weaponHands[static_cast<std::size_t>(slot.position())];
            const battle::BattleItem& shield = state.inventory.shieldHands[static_cast<std::size_t>(slot.position())];
            writeByte(kWeaponHands + place, static_cast<std::uint8_t>(weapon.item));
            writeByte(kWeaponHands + place + kHandQuantity, weapon.quantity);
            writeByte(kShieldHands + place, static_cast<std::uint8_t>(shield.item));
            writeByte(kShieldHands + place + kHandQuantity, shield.quantity);
            writeByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position()), battler.equipmentChanged ? 1 : 0);
        }
    }
}

AttackerEffectResult DamageOracle::attackerEffect(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    // The frame calls the routine with the carry its facing step (UpdateFacingDirection) leaves: set
    // after a blow by a party member in a side attack or by a monster in a pincer or a side attack.
    const battle::BattleType layout = scenario.state.battleType;
    const bool blow = scenario.attack.record.traits.has(AttackTrait::PHYSICAL);
    const bool carry = blow && (scenario.attacker.isCharacter()
                                    ? layout == battle::BattleType::Side
                                    : layout == battle::BattleType::Pincer || layout == battle::BattleType::Side);
    const auto status = static_cast<std::uint8_t>(kByteWideStatus | (carry ? kCarryBit : 0));
    call([&] { doAttackerEffect_(slotIndex(scenario.attacker), status, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedRoutines[5]);
    return readAttackerEffectResult();
}

AttackerEffectResult DamageOracle::readAttackerEffectResult() {
    EffectCells out;
    std::array<std::uint8_t, sizeof(AttackProperties)> bytes{};
    const std::vector<std::uint8_t> read =
        machine_.read(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(bytes.size())});
    std::copy(read.begin(), read.end(), bytes.begin());
    AttackProperties record{};
    std::memcpy(&record, bytes.data(), bytes.size());
    putRecord(out, record);
    out.put("effect", readByte(kEffect) / 2);
    out.put("damage", readWord(kDamage));
    out.put("command", readByte(kCommand));
    out.put("attack", readByte(kAttackIndex));
    out.put("second value", readByte(kSecondary));
    out.put("can be critical", (readByte(kFirstAttackFlags) & kCannotBeCriticalBit) == 0);
    out.put("automatic critical", (readByte(kAttackFlags) & kAutomaticCriticalClearBit) == 0);
    out.put("swirl suppressed", (readByte(kSwirlCell) & kSwirlSuppressedBit) != 0);
    out.put("multiplier", readByte(kMultiplier));
    const std::vector<std::uint8_t> params = machine_.read(retropp::MemoryRegion{.at = kParamsFlags, .size = 8});
    out.put("params", paramsText(paramsFromBytes(params)));
    out.put("targets", std::to_string(readWord(kTargets)));
    out.put("random target", (readByte(kAimingRules) & kRandomTargetBit) != 0);
    out.put("fall back to earlier targets", (readByte(kAimingRules) & kFallBackToEarlierTargetsBit) != 0);
    out.put("other side only", (readByte(kAimingRules) & kOpposingSideBit) != 0);
    const std::uint8_t spell = readByte(kFollowUpSpell);
    out.put("spell next", spell == kNoOne ? std::string{"none"} : std::to_string(spell));
    const std::uint8_t message = readByte(kMessage);
    out.put("message", message == kNoOne ? std::string{"none"} : std::to_string(message));
    out.put("damage modification", readByte(kDamageModification) != 0);
    out.put("attacks left", readByte(kAttacksLeft));
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        out.put("Mind Blast pick " + std::to_string(position),
                std::to_string(readWord(kMindBlastPicks + static_cast<std::uint32_t>(position * 2))));
    }
    out.put("shown Miss", std::to_string(readWord(kMissed)));
    const std::uint8_t hits = readByte(kExtraHits);
    out.put("hits to come", hits == kNoHitsToCome ? std::string{"none"} : std::to_string(hits + 1));
    out.put("the Golem's HP", readWord(kGolemHp));
    out.put("Forcefield", readByte(kForcefield));
    out.put("end of battle", readByte(kEndOfBattleEvent) / 2);
    const std::uint8_t quickTurns = readByte(kQuickTurns);
    out.put("Quick's turns", quickTurns == kNoQuick ? std::string{"none"} : std::to_string(quickTurns));
    out.put("Quick's target", slotText(slotTimesTwo(readByte(kQuickTarget))));
    out.put("leaving", std::to_string(readWord(kLeaving)));
    out.put("turn ends", (readByte(kAimFlags) & kEndTurnBit) != 0);
    const std::vector<std::uint8_t> gil = machine_.read(retropp::MemoryRegion{.at = kGil, .size = 3});
    out.put("gil", gil[0] | gil[1] << 8 | gil[2] << 16);
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::string of = " of battler " + std::to_string(i);
        out.put("HP" + of, readWord(kHp + at));
        out.put("MP" + of, readWord(kMp + at));
        out.put("statuses to set" + of,
                statusesText(StatusSet{{readByte(kToSet12 + at), readByte(kToSet12 + at + 1), readByte(kToSet34 + at),
                                        readByte(kToSet34 + at + 1)}}));
        out.put("last attacker" + of, readByte(kLastAttacker + at));
        out.put("gold" + of, readWord(kGold + at));
        if (slot.isCharacter()) {
            const auto place = static_cast<std::uint32_t>(slot.position()) * kHandItemSize;
            out.put("hands' power" + of,
                    std::to_string(readByte(kHandPower + at)) + "/" + std::to_string(readByte(kHandPower + at + 1)));
            out.put("hands" + of, std::to_string(readByte(kWeaponHands + place)) + "x" +
                                      std::to_string(readByte(kWeaponHands + place + kHandQuantity)) + "/" +
                                      std::to_string(readByte(kShieldHands + place)) + "x" +
                                      std::to_string(readByte(kShieldHands + place + kHandQuantity)));
            out.put("equipment to work out" + of,
                    readByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position())) != 0);
        }
    }
    // What the effect added to the script: the animations queued, and the params blocks after the
    // frame's own.
    out.put("script animations", readByte(kQueueEnd) / kQueueCommandSize);
    const int blocks = (readByte(kNextParamsBlock) - kParamsBlockSize) / kParamsBlockSize;
    out.put("script params blocks", blocks);
    for (int block = 0; block < blocks; ++block) {
        const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{
            .at = kParamsBlocks + kParamsBlockSize * static_cast<std::uint32_t>(block + 1), .size = kParamsBlockSize});
        out.put("script params " + std::to_string(block), paramsText(paramsFromBytes(bytes)));
    }
    out.put("random-number position", readByte(kRngPosition));
    return AttackerEffectResult{.cells = std::move(out.cells)};
}

}  // namespace ostinato::trace
