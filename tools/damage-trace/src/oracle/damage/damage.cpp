// The cartridge's damage routines run over a scenario: the base damage, the relic bonus, the
// multiplier, the per-target pass, the damage taken and shown, and the reflected attack.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/attack.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/damage.h"
#include "battle/items_obtained.h"
#include "data/attack_properties.h"
#include "game/character_record.h"
#include "ostinato/status_set.h"

#include "cells/damage/per_target.h"
#include "oracle/routines.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/report_text.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

constexpr std::uint8_t kNotQuiet = 0xFF;

// Damage modification on and off; a link or block that names no one; a ledger entry with nothing in it.
constexpr std::uint8_t kOn = 0xFF;
}  // namespace

std::uint16_t DamageOracle::magicDamage(std::uint8_t power, std::uint8_t magicStat, std::uint8_t level) {
    writeByte(kPower, power);
    writeByte(kAttackerStat, magicStat);
    writeByte(kAttackerLevel, level);
    writeWord(kDamage, 0);
    call([&] { calcMagicDmg_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    return readWord(kDamage);
}

std::uint16_t DamageOracle::calcDamage(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeContext(scenario);
    call([&] { calcDmg_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedRoutines[0]);
    return readWord(kDamage);
}

std::uint16_t DamageOracle::relicBonus(const DamageScenario& scenario, std::uint16_t damage) {
    writeScenario(scenario);
    writeWord(kDamage, damage);
    call([&] {
        relicDmgEffect_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage);
    });
    return readWord(kDamage);
}

MultiplierResult DamageOracle::applyMultiplier(const DamageScenario& scenario) {
    writeScenario(scenario);
    std::uint16_t damage = 0;
    call([&] {
        damage = applyDmgMult_(static_cast<std::uint16_t>(scenario.attack.damage), kWideAccumulatorStatus,
                               kBattleDataBank, kBattleDirectPage);
    });
    return MultiplierResult{.damage = damage, .stepsLeft = readByte(kMultiplier)};
}

TargetDamageResult DamageOracle::targetDamage(const DamageScenario& scenario, battle::BattlerSlot target) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeContext(scenario);
    call([&] {
        calcTargetDmg_(slotIndex(scenario.attacker), slotIndex(target), kWideAccumulatorStatus, kBattleDataBank,
                       kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[1]);
    return readTargetResult();
}

ApplyDamageResult DamageOracle::applyDamage(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeContext(scenario);
    call([&] {
        applyAndShowDmg_(slotIndex(scenario.attacker), kWideAccumulatorStatus, kBattleDataBank, kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[2]);
    return readApplyResult();
}

void DamageOracle::writeApplyCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    writeInFlightWord(kMissed, maskOf(attack.scratch.missed));
    writeWord(kInvincible, maskOf(state.invincible));
    writeWord(kGolemHp, state.golemHp);
    writeInFlightByte(kWeaponSpellcast, attack.scratch.weaponSpellcast.raw);
    writeByte(kBlackBeltTargets, static_cast<std::uint8_t>(~state.scratch.blackBeltEligible.characters.bits));
    writeInFlightByte(kQueueEnd, 0);
    writeInFlightByte(kNumeralBufferCount, 0);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        writeWord(kBattlerBit + at, maskOf(battle::BattlerSet::of(slot)));
        writeWord(kToUpdate + at, battler.pendingUpdates.enabledMagic ? kEnabledMagicBit : 0);
        writeByte(kLastDamagedBy + at,
                  battler.lastDamagedBy ? static_cast<std::uint8_t>(slotIndex(*battler.lastDamagedBy)) : kNoOne);
    }
}

ApplyDamageResult DamageOracle::readApplyResult(std::size_t firstNumeralCommand, std::uint32_t firstNumeralBuffer) {
    ApplyDamageResult result = readApplyCells();
    result.shown = readNumerals(firstNumeralCommand, firstNumeralBuffer);
    return result;
}

ApplyDamageResult DamageOracle::readApplyCells() {
    ApplyDamageResult result;
    const auto entry = [](std::uint16_t word) -> std::optional<std::uint16_t> {
        if (word == kNoDamage) {
            return std::nullopt;
        }
        return word;
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.hp[i] = readWord(kHp + at);
        result.mp[i] = readWord(kMp + at);
        result.statusesToSet[i] = StatusSet{{readByte(kToSet12 + at), readByte(kToSet12 + at + 1),
                                             readByte(kToSet34 + at), readByte(kToSet34 + at + 1)}};
        result.lastAttackerMark[i] = readByte(kLastAttacker + at);
        const std::uint8_t damager = readByte(kLastDamagedBy + at);
        if (damager != kNoOne) {
            result.lastDamagedBy[i] = battle::BattlerSlot::fromIndex(damager / 2);
        }
        result.enabledMagic[i] = (readByte(kToUpdate + at) & kEnabledMagicBit) != 0;
        result.ledger.taken[i] = entry(readWord(kTaken + at));
        result.ledger.healed[i] = entry(readWord(kHealed + at));
    }
    result.blackBeltEligible.characters =
        BattleSlotMask(static_cast<std::uint8_t>(~readByte(kBlackBeltTargets) & kPartyBits));
    result.missed = setOf(readWord(kMissed));
    result.golemHp = readWord(kGolemHp);
    result.weaponSpellcast = readByte(kWeaponSpellcast);
    return result;
}

std::vector<battle::DamageNumbersShown> DamageOracle::readNumerals(std::size_t firstCommand,
                                                                   std::uint32_t firstBuffer) {
    const auto numeral = [](battle::BattlerSlot slot, std::uint16_t word) {
        return battle::DamageNumber{.target = slot,
                                    .amount = static_cast<std::uint16_t>(word & kAmountBits),
                                    .miss = (word & kMissMark) != 0,
                                    .heals = (word & kHealMark) != 0};
    };
    std::vector<battle::DamageNumbersShown> shown;
    const std::uint8_t end = readByte(kQueueEnd);
    if (end == 0) {
        return shown;
    }
    const std::vector<std::uint8_t> queue = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = end});
    std::uint32_t buffers = firstBuffer;
    for (std::size_t at = firstCommand * kQueueCommandSize; at + kQueueCommandSize <= queue.size();
         at += kQueueCommandSize) {
        const std::uint8_t id = queue[at];
        if (id == kEveryNumeral) {
            battle::DamageNumbersShown together{.wait = battle::DamageNumberWait::UntilGone};
            const std::uint32_t buffer = kNumeralBuffer + kNumeralBufferSize * buffers++;
            for (int index = battle::kBattlerSlots - 1; index >= 0; --index) {
                const std::uint16_t word = readWord(buffer + static_cast<std::uint32_t>(index * 2));
                if (word != kNoDamage) {
                    together.numbers.push_back(numeral(battle::BattlerSlot::fromIndex(index), word));
                }
            }
            shown.push_back(std::move(together));
            continue;
        }
        if (id != kOneNumeral) {
            throw std::runtime_error("the apply-and-show routine wrote a command that is not a numeral: " +
                                     std::to_string(id));
        }
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(queue[at + 1]);
        const auto word = static_cast<std::uint16_t>(queue[at + 2] | queue[at + 3] << 8);
        // One pass shows its battlers last slot first, so a slot no lower than the one before begins
        // the second pass.
        const bool continues = !shown.empty() && shown.back().wait == battle::DamageNumberWait::CarryOn &&
                               !shown.back().numbers.empty() &&
                               shown.back().numbers.back().target.index() > slot.index();
        if (!continues) {
            shown.push_back(battle::DamageNumbersShown{.wait = battle::DamageNumberWait::CarryOn});
        }
        shown.back().numbers.push_back(numeral(slot, word));
    }
    return shown;
}

ReflectedAttackResult DamageOracle::reflectedAttack(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeAimCells(scenario);
    call([&] {
        calcReflectDmg_(maskOf(scenario.attack.params.reflectedOff), slotIndex(scenario.attacker),
                        kWideAccumulatorStatus, kBattleDataBank, kBattleDirectPage);
    });
    return readReflectResult();
}

void DamageOracle::writeAimCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    const battle::AimingRules& rules = attack.rules;
    std::uint8_t aiming = 0;
    aiming |= rules.noCharacterTargets ? 0x80 : 0;
    aiming |= rules.randomTarget ? 0x40 : 0;
    aiming |= rules.fallBackToEarlierTargets ? 0x20 : 0;
    aiming |= rules.reflected ? 0x10 : 0;
    aiming |= rules.mayTargetTheFallen ? 0x08 : 0;
    aiming |= rules.keepTargetsThatAreGone ? 0x04 : 0;
    aiming |= rules.opposingSideOnly ? 0x02 : 0;
    aiming |= rules.excludeAttacker ? 0x01 : 0;
    writeInFlightByte(kAimingRules, aiming);
    writeInFlightWord(kTargets, maskOf(attack.targets));
    writeInFlightByte(kAimFlags, attack.scratch.aimExactly ? kAimExactlyBit : 0);
    writeInFlightWord(kEarlierTargets, maskOf(attack.scratch.earlierTargets));
    battle::BattlerSet enemies;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (state.at(battle::BattlerSlot::character(position)).actsAsEnemy) {
            enemies.add(battle::BattlerSlot::character(position));
        }
    }
    writeWord(kEnemyCharacters, maskOf(enemies));
    writeByte(kBattleType, static_cast<std::uint8_t>(state.battleType));
    writeByte(kPincerFirstSide, state.pincerSides[0].bits);
    writeByte(kPincerSecondSide, state.pincerSides[1].bits);

    // The reach check's four cells, written so that what they leave between them is the port's own
    // reachable set: on the field and counted in the battle, not hidden from aiming by its AI, and
    // for a party member not seized.
    battle::BattlerSet seized;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (state.at(battle::BattlerSlot::character(position)).seizedBy) {
            seized.add(battle::BattlerSlot::character(position));
        }
    }
    writeByte(kReachMonsters, 0xFF);
    writeByte(kReachCharacters, static_cast<std::uint8_t>(~seized.characters.bits));
    writeWord(kReachFirst, maskOf(state.targetableOrPresent & state.countedInBattle));
    writeWord(kReachSecond, static_cast<std::uint16_t>(~maskOf(state.aiUntargetable)));
    writeByte(kCountersEnabled, state.scratch.countersEnabled ? kCountersOn : 0);
    writeInFlightByte(kReachKeepsAway, attack.scratch.awayFromTheFieldUnreachable ? kAwayUnreachable : 0);
    writeWord(kAwayFromTheField, maskOf(state.awayFromTheField));

    writeInFlightByte(kTargetCount, static_cast<std::uint8_t>(attack.scratch.targetCount));
    writeInFlightByte(kQuietMisses, attack.scratch.quietMisses ? 0 : kNotQuiet);
    writeInFlightWord(kHitInTheBack, maskOf(attack.scratch.hitInTheBack));
    writeInFlightByte(kQueuedCommand, static_cast<std::uint8_t>(attack.queuedCommand));
    writeInFlightByte(kLastSpell, attack.scratch.lastSpell ? static_cast<std::uint8_t>(*attack.scratch.lastSpell) : kNoOne);
    writeInFlightByte(kLastItem, attack.scratch.lastItem ? static_cast<std::uint8_t>(*attack.scratch.lastItem) : kNoOne);
    writeByte(kExtraHits, state.extraHits ? static_cast<std::uint8_t>(*state.extraHits - 1) : kNoHitsToCome);
    // The block before the bounce's: the frame's own, whose attacker is the caster.
    writeInFlightByte(kNextParamsBlock, kParamsBlockSize);
    writeInFlightByte(kParamsBlocks + 1, static_cast<std::uint8_t>(scenario.attacker.index()));

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        writeByte(kOnTheField + at, battler.present ? 1 : 0);
        writeByte(kCharmedBy + at, battler.charmedBy ? static_cast<std::uint8_t>(slotIndex(*battler.charmedBy)) : kNoOne);
        writeByte(kStamina + at, battler.stamina);
        writeByte(kEvadeAndMagicBlock + at, battler.evadeFactor);
        writeByte(kEvadeAndMagicBlock + at + 1, battler.magicBlockFactor);
        writeByte(kBlockGraphics + at, battler.physicalBlockGraphics.bits);
        writeByte(kBlockGraphics + at + 1, battler.magicBlockGraphics.bits);
        writeByte(kRetort + at, battler.triggers.retort ? 1 : 0);
        const battle::LastHits& last = battler.lastHit;
        const auto by = [](const auto& hit) {
            return hit ? static_cast<std::uint8_t>(slotIndex(hit->by)) : kNoOne;
        };
        writeByte(kLastHitCommandBy + at, by(last.command));
        writeByte(kLastHitCommand + at, last.command ? static_cast<std::uint8_t>(last.command->with) : kNoOne);
        writeByte(kLastHitSpellBy + at, by(last.spell));
        writeByte(kLastHitSpell + at, last.spell ? static_cast<std::uint8_t>(last.spell->with) : kNoOne);
        writeByte(kLastHitItemBy + at, by(last.item));
        writeByte(kLastHitItem + at, last.item ? static_cast<std::uint8_t>(last.item->with) : kNoOne);
        writeByte(kLastHitElementBy + at, by(last.element));
        writeByte(kLastHitElement + at, last.element ? last.element->with.bits : 0);
    }
}

ReflectedAttackResult DamageOracle::readReflectResult() {
    ReflectedAttackResult result;
    result.applied = readApplyResult(1, 0);
    const auto slotByte = [](std::uint8_t value) -> std::optional<battle::BattlerSlot> {
        if (value == kNoOne) {
            return std::nullopt;
        }
        return battle::BattlerSlot::fromIndex(value / 2);
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.statusesToClear[i] = StatusSet{{readByte(kToClear12 + at), readByte(kToClear12 + at + 1),
                                               readByte(kToClear34 + at), readByte(kToClear34 + at + 1)}};
        result.lastHitBy[i] = slotByte(readByte(kLastHitCommandBy + at));
    }
    const std::vector<std::uint8_t> command = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize});
    const std::vector<std::uint8_t> block =
        machine_.read(retropp::MemoryRegion{.at = kParamsBlocks + kParamsBlockSize, .size = kParamsBlockSize});
    result.shownAs = bounceText(command, block);
    result.damage = readWord(kDamage);
    result.canBeCritical = (readByte(kFirstAttackFlags) & kCannotBeCriticalBit) == 0;
    const std::uint8_t hits = readByte(kExtraHits);
    if (hits != kNoHitsToCome) {
        result.extraHits = hits + 1;
    }
    result.golemBlocked = slotByte(readByte(kGolemBlock));
    result.interceptorBlocked = slotByte(readByte(kInterceptorBlock));
    result.rngPosition = readByte(kRngPosition);
    return result;
}

void DamageOracle::writeTargetCells(const DamageScenario& scenario) {
    const battle::AttackInFlight& attack = scenario.attack;
    const battle::BattleState& state = scenario.state;
    writeInFlightByte(kEffect, static_cast<std::uint8_t>(attack.effect.index() * 2));
    std::uint8_t first = kAttackFlagsAtStart;
    if (!attack.drainCapped) {
        first &= static_cast<std::uint8_t>(~kDrainCappedBit);
    }
    if (attack.canBeCritical) {
        first &= static_cast<std::uint8_t>(~kCannotBeCriticalBit);
    }
    writeInFlightByte(kFirstAttackFlags, first);
    writeInFlightByte(kAnimationCommand, kShowAnimationCommand);
    writeInFlightByte(kCommand, static_cast<std::uint8_t>(attack.command));
    writeInFlightByte(kAttackIndex, static_cast<std::uint8_t>(attack.attack));
    writeInFlightByte(kSecondary, attack.secondary);
    writeByte(kRngPosition, state.rng.position());
    writeInFlightByte(kDamageModification, attack.scratch.damageModification ? kOn : 0);
    const auto blockByte = [](const std::optional<battle::BattlerSlot>& slot) {
        return slot ? static_cast<std::uint8_t>(slotIndex(*slot)) : kNoOne;
    };
    writeInFlightByte(kGolemBlock, blockByte(attack.scratch.golemBlocked));
    writeInFlightByte(kInterceptorBlock, blockByte(attack.scratch.interceptorBlocked));
    writeByte(kForcefield, state.elementsNullifiedByForcefield.bits);
    writeInFlightByte(kAnimationPlace, 0);
    writeInFlightByte(kQueueEnd, kQueueCommandSize);
    const std::array<std::uint8_t, kQueueCommandSize> unwritten{kUnwritten, kUnwritten, kUnwritten, kUnwritten};
    writeInFlightBytes(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize}, unwritten);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        writeByte(kLevel + at, battler.level);
        writeByte(kDefenses + at, battler.defense);
        writeByte(kDefenses + at + 1, battler.magicDefense);
        writeWord(kHp + at, battler.hp);
        writeWord(kMp + at, battler.mp);
        writeWord(kMaxHp + at, battler.maxHp);
        writeWord(kMaxMp + at, battler.maxMp);
        writeByte(kAbsorbedNullified + at, battler.elementsAbsorbed.bits);
        writeByte(kAbsorbedNullified + at + 1, battler.elementsNullified.bits);
        writeByte(kWeakHalved + at, battler.elementsWeak.bits);
        writeByte(kWeakHalved + at + 1, battler.elementsHalved.bits);
        std::uint8_t flags = 0;
        flags |= battler.undead ? kUndeadBit : 0;
        flags |= battler.impCritical ? kImpCriticalBit : 0;
        flags |= battler.human ? kHumanBit : 0;
        flags |= battler.nameHidden ? kNameHiddenBit : 0;
        flags |= battler.diesAtZeroMp ? kDiesAtZeroMpBit : 0;
        writeByte(kMonsterFlags + at, flags);
        std::uint8_t stance = 0;
        stance |= battler.defending ? kDefendingBit : 0;
        stance |= battler.backRow ? kBackRowBit : 0;
        stance |= battler.instantDeathProtected ? kInstantDeathProtectedBit : 0;
        writeByte(kStance + at, stance);
        const auto writeStatuses = [&](std::uint32_t first12, std::uint32_t first34, const StatusSet& set) {
            writeByte(first12 + at, set.bytes[0]);
            writeByte(first12 + at + 1, set.bytes[1]);
            writeByte(first34 + at, set.bytes[2]);
            writeByte(first34 + at + 1, set.bytes[3]);
        };
        writeStatuses(kStatus12, kStatus34, battler.status);
        writeStatuses(kToSet12, kToSet34, battler.statusesToSet);
        writeStatuses(kToClear12, kToClear34, battler.statusesToClear);
        // The controller's slot times two, with the top bit set while the link is ending.
        writeByte(kController + at, battler.controlledBy ? static_cast<std::uint8_t>(slotIndex(*battler.controlledBy) |
                                                                                    (battler.controlEnding ? kTopBit : 0))
                                                         : kNoOne);
        const std::uint8_t mark = markByte(battler);
        writeByte(kLastAttacker + at, mark);
        writeInFlightWord(kTaken + at, attack.scratch.ledger.taken[i].value_or(kNoDamage));
        writeInFlightWord(kHealed + at, attack.scratch.ledger.healed[i].value_or(kNoDamage));
    }
}

TargetDamageResult DamageOracle::readTargetResult() {
    TargetDamageResult result;
    const auto entry = [](std::uint16_t word) -> std::optional<std::uint16_t> {
        if (word == kNoDamage) {
            return std::nullopt;
        }
        return word;
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.ledger.taken[i] = entry(readWord(kTaken + at));
        result.ledger.healed[i] = entry(readWord(kHealed + at));
        const auto readStatuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        result.statusesToSet[i] = readStatuses(kToSet12, kToSet34);
        result.statusesToClear[i] = readStatuses(kToClear12, kToClear34);
        result.controlled[i] = (readByte(kController + at) & kWaitingBit) == 0;
        result.lastAttackerMark[i] = readByte(kLastAttacker + at);
    }
    result.stepsLeft = readByte(kMultiplier);
    result.secondary = readByte(kSecondary);
    const std::vector<std::uint8_t> place = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize});
    if (std::any_of(place.begin(), place.end(), [](std::uint8_t b) { return b != kUnwritten; })) {
        result.animationRewrittenWith = place[kQueueCommandSize - 1];
    }
    result.rngPosition = readByte(kRngPosition);
    return result;
}

void DamageOracle::writeScenario(const DamageScenario& scenario) {
    const battle::AttackInFlight& attack = scenario.attack;
    std::array<std::uint8_t, sizeof(AttackProperties)> record{};
    std::memcpy(record.data(), &attack.record, record.size());
    writeInFlightBytes(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(record.size())}, record);
    writeInFlightByte(kAttackerStat, attack.attackerStat);
    writeInFlightByte(kAttackerLevel, attack.attackerLevel);
    writeInFlightWord(kDamage, static_cast<std::uint16_t>(attack.damage));
    std::uint8_t flags = kAttackFlagsAtStart;
    if (attack.twoHanded) {
        flags &= static_cast<std::uint8_t>(~kNotTwoHanded);
    }
    if (!attack.ignoreDefenseStopsMultiplier) {
        flags &= static_cast<std::uint8_t>(~kIgnoreDefenseStopsMultiplier);
    }
    if (!attack.vanishApplies) {
        flags &= static_cast<std::uint8_t>(~kVanishAppliesBit);
    }
    writeInFlightByte(kAttackFlags, flags);
    writeInFlightByte(kMultiplier, static_cast<std::uint8_t>(attack.multiplier));

    for (std::size_t i = 0; i < scenario.state.battlers.size(); ++i) {
        const battle::Battler& battler = scenario.state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const auto first = static_cast<std::uint8_t>((battler.relicEffects1.bits & kLowSevenBits) |
                                                     (battler.relicEffects2.bits & kTopBit));
        writeByte(kRelicEffects1And3 + at, first);
        writeByte(kRelicEffects1And3 + at + 1, battler.relicEffects3.bits);
        writeByte(kRelicEffects4And5 + at, battler.relicEffects4.bits);
        writeByte(kRelicEffects4And5 + at + 1, battler.relicEffects5.bits);
    }
}

}  // namespace ostinato::trace
