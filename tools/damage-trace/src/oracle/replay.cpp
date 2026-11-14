// A traced call rebuilt as a scenario from the cells it read, and replayed against the port.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/damage.h"
#include "battle/items_obtained.h"
#include "data/attack_properties.h"
#include "game/character_record.h"
#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/status_set.h"

#include "cells/damage/per_target.h"
#include "oracle/replay_shared.h"
#include "oracle/routines.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "trace/cells.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

const TracedRoutine& routineNamed(const std::string& name) {
    for (const TracedRoutine& routine : kTracedRoutines) {
        if (name == routine.name) {
            return routine;
        }
    }
    throw std::runtime_error("the trace names a routine it does not follow: " + name);
}
}  // namespace

namespace detail {

battle::BattlerSlot bounceAttacker(const TracedCall& call) {
    if ((call.aimingRules & kReflectedRule) != 0) {
        int from = 0;
        while (from < battle::kBattlerSlots && call.params[static_cast<std::size_t>(from)] == kNoOne) {
            ++from;
        }
        if (from == battle::kBattlerSlots) {
            throw std::runtime_error("the bounce's block names no battler it bounced off");
        }
        return battle::BattlerSlot::fromIndex(from);
    }
    const battle::BattlerSet hitters =
        setOf(static_cast<std::uint16_t>(call.params[kBlockSet] | call.params[kBlockSet + 1] << 8));
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        if (hitters.has(battle::BattlerSlot::fromIndex(index))) {
            return battle::BattlerSlot::fromIndex(index);
        }
    }
    throw std::runtime_error("a scattered hit whose block names no battler it was made from");
}
}  // namespace detail

void DamageOracle::writeContext(const DamageScenario& scenario) {
    writeByte(kParamsCells + kBlockAttacker, static_cast<std::uint8_t>(scenario.attacker.index()));
}

DamageScenario DamageOracle::readScenario() {
    DamageScenario s;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    const std::vector<std::uint8_t> record =
        machine_.read(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(sizeof(AttackProperties))});
    std::memcpy(&attack.record, record.data(), record.size());
    attack.attackerStat = readByte(kAttackerStat);
    attack.attackerLevel = readByte(kAttackerLevel);
    attack.damage = readWord(kDamage);
    const std::uint8_t flags = readByte(kAttackFlags);
    attack.twoHanded = (flags & kNotTwoHanded) == 0;
    attack.ignoreDefenseStopsMultiplier = (flags & kIgnoreDefenseStopsMultiplier) != 0;
    attack.vanishApplies = (flags & kVanishAppliesBit) != 0;
    attack.multiplier = readByte(kMultiplier);
    attack.effect = effectAt(static_cast<std::uint8_t>(readByte(kEffect) / 2));
    const std::uint8_t first = readByte(kFirstAttackFlags);
    attack.drainCapped = (first & kDrainCappedBit) != 0;
    attack.canBeCritical = (first & kCannotBeCriticalBit) == 0;
    attack.command = static_cast<BattleCommandId>(readByte(kCommand));
    attack.attack = static_cast<AttackId>(readByte(kAttackIndex));
    attack.secondary = readByte(kSecondary);
    state.rng = battle::BattleRng{readByte(kRngPosition)};
    attack.scratch.damageModification = readByte(kDamageModification) != 0;
    attack.scratch.golemBlocked = slotTimesTwo(readByte(kGolemBlock));
    attack.scratch.interceptorBlocked = slotTimesTwo(readByte(kInterceptorBlock));
    state.elementsNullifiedByForcefield.bits = readByte(kForcefield);
    attack.scratch.missed = setOf(readWord(kMissed));
    state.invincible = setOf(readWord(kInvincible));
    state.golemHp = readWord(kGolemHp);
    attack.scratch.weaponSpellcast.raw = readByte(kWeaponSpellcast);
    state.scratch.blackBeltEligible.characters =
        BattleSlotMask(static_cast<std::uint8_t>(~readByte(kBlackBeltTargets) & kPartyBits));

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        battler.level = readByte(kLevel + at);
        battler.defense = readByte(kDefenses + at);
        battler.magicDefense = readByte(kDefenses + at + 1);
        battler.hp = readWord(kHp + at);
        battler.mp = readWord(kMp + at);
        battler.maxHp = readWord(kMaxHp + at);
        battler.maxMp = readWord(kMaxMp + at);
        battler.elementsAbsorbed.bits = readByte(kAbsorbedNullified + at);
        battler.elementsNullified.bits = readByte(kAbsorbedNullified + at + 1);
        battler.elementsWeak.bits = readByte(kWeakHalved + at);
        battler.elementsHalved.bits = readByte(kWeakHalved + at + 1);
        const std::uint8_t monsterFlags = readByte(kMonsterFlags + at);
        battler.undead = (monsterFlags & kUndeadBit) != 0;
        battler.impCritical = (monsterFlags & kImpCriticalBit) != 0;
        battler.human = (monsterFlags & kHumanBit) != 0;
        battler.nameHidden = (monsterFlags & kNameHiddenBit) != 0;
        battler.diesAtZeroMp = (monsterFlags & kDiesAtZeroMpBit) != 0;
        const std::uint8_t stance = readByte(kStance + at);
        battler.defending = (stance & kDefendingBit) != 0;
        battler.backRow = (stance & kBackRowBit) != 0;
        battler.instantDeathProtected = (stance & kInstantDeathProtectedBit) != 0;
        const auto statuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        battler.status = statuses(kStatus12, kStatus34);
        battler.statusesToSet = statuses(kToSet12, kToSet34);
        battler.statusesToClear = statuses(kToClear12, kToClear34);
        const std::uint8_t link = readByte(kController + at);
        if (link != kNoOne) {
            battler.controlledBy = slotTimesTwo(static_cast<std::uint8_t>(link & ~kTopBit));
            battler.controlEnding = (link & kTopBit) != 0;
        }
        const std::uint8_t mark = readByte(kLastAttacker + at);
        const int lastAttacker = mark & ~kWaitingBit;
        if (lastAttacker < battle::kBattlerSlots) {
            battler.lastAttacker = battle::BattlerSlot::fromIndex(lastAttacker);
        }
        battler.retaliationPending = (mark & kWaitingBit) != 0;
        const auto entry = [](std::uint16_t word) -> std::optional<std::uint16_t> {
            if (word == kNoDamage) {
                return std::nullopt;
            }
            return word;
        };
        attack.scratch.ledger.taken[i] = entry(readWord(kTaken + at));
        attack.scratch.ledger.healed[i] = entry(readWord(kHealed + at));
        const std::uint8_t relicsLow = readByte(kRelicEffects1And3 + at);
        battler.relicEffects1.bits = static_cast<std::uint8_t>(relicsLow & kLowSevenBits);
        battler.relicEffects2.bits = static_cast<std::uint8_t>(relicsLow & kTopBit);
        battler.relicEffects3.bits = readByte(kRelicEffects1And3 + at + 1);
        battler.relicEffects4.bits = readByte(kRelicEffects4And5 + at);
        battler.relicEffects5.bits = readByte(kRelicEffects4And5 + at + 1);
        battler.pendingUpdates.enabledMagic = (readByte(kToUpdate + at) & kEnabledMagicBit) != 0;
        battler.lastDamagedBy = slotTimesTwo(readByte(kLastDamagedBy + at));
    }
    return s;
}

std::string DamageOracle::replay(const TracedCall& call) {
    const TracedRoutine& routine = routineNamed(call.routine);
    const bool bounce = routine.bounceReturn != 0 && call.returnedTo == routine.bounceReturn;
    const bool other = std::find(routine.otherReturns.begin(), routine.otherReturns.end(), call.returnedTo) !=
                       routine.otherReturns.end();
    if (!bounce && !other && call.returnedTo != routine.frameReturn) {
        throw std::runtime_error(call.routine + " returned to " + busAddress(call.returnedTo) +
                                 ", which is none of the calls the trace follows");
    }
    if (routine.entry == kInitAi || routine.entry == kExecMonsterAction || routine.entry == kExecAiRetal ||
        std::find(kAiCommands.begin(), kAiCommands.end(), routine.entry) != kAiCommands.end()) {
        return replayAiCall(call, routine);
    }
    if (routine.entry == kUpdateStatus || routine.entry == kMagicStatusEffect) {
        return replayStatusCall(call, routine);
    }
    if (routine.entry == kDoAttackerEffect || routine.entry == kDoTargetEffect) {
        return replayEffectCall(call, routine, bounce);
    }
    if (routine.entry == kAddExp || routine.entry == kLearnItemMagic || routine.entry == kLearnAbilities ||
        routine.entry == kLearnRage) {
        return replayRewardCall(call, routine);
    }
    if (routine.partyRecords) {
        return replayRecordCall(call, routine);
    }

    // The battle as the oracle writes a default scenario, then every cell the routine read as it read it.
    const DamageScenario plain;
    writeScenario(plain);
    writeTargetCells(plain);
    writeApplyCells(plain);
    for (const auto& [cell, value] : call.inputs) {
        writeByte(cell, value);
    }
    DamageScenario s = readScenario();
    const auto input = [&](std::uint32_t cell) {
        const auto found = call.inputs.find(cell);
        return found != call.inputs.end() ? found->second : readByte(cell);
    };
    const auto overlayWrites = [&](std::uint32_t queueShift) {
        for (const auto& [cell, value] : call.writes) {
            if (cell >= kQueue && cell < kQueue + kQueueSpan && queueShift != 0) {
                if (cell - kQueue >= queueShift) {
                    writeByte(cell - queueShift, value);
                }
                continue;
            }
            writeByte(cell, value);
        }
    };

    if (routine.entry == kCalcDmg) {
        // A party member's blow reads its fourth relic effects; a monster's reads no battler's cell, and
        // a spell's answer does not depend on who cast it.
        const bool physical = s.attack.record.traits.has(AttackTrait::PHYSICAL);
        s.attacker = physical ? battle::BattlerSlot::monster(0) : battle::BattlerSlot::character(0);
        for (const auto& [cell, value] : call.inputs) {
            if (cell >= kRelicEffects4And5 && cell < kRelicEffects4And5 + 2 * battle::kBattlerSlots) {
                s.attacker = battle::BattlerSlot::fromIndex(static_cast<int>((cell - kRelicEffects4And5) / 2));
            }
        }
        const std::uint16_t port = physical ? battle::physicalDamage(s.state, s.attack, s.attacker)
                                            : battle::magicDamage(s.attack.record.power, s.attack.attackerStat,
                                                                  s.attack.attackerLevel);
        overlayWrites(0);
        const std::uint16_t guest = readWord(kDamage);
        if (port == guest) {
            return {};
        }
        return "damage word: port " + std::to_string(port) + ", cartridge " + std::to_string(guest) + "\n";
    }

    if (routine.entry == kCalcTargetDmg) {
        // The battlers whose cells the routine touched, other than `attacker`.
        const auto touchedBesides = [&](battle::BattlerSlot attacker) {
            std::set<int> touched;
            for (const auto* cells : {&call.inputs, &call.writes}) {
                for (const auto& [cell, value] : *cells) {
                    if (const std::optional<int> battler = battlerOfCell(cell)) {
                        touched.insert(*battler);
                    }
                }
            }
            touched.erase(attacker.index());
            return touched;
        };
        battle::BattlerSlot target = battle::BattlerSlot::character(0);
        if (!bounce) {
            s.attacker = slotNamed(call.params[kBlockAttacker], "the params block's attacker");
            // The target is the battler whose cells the routine touched, other than the attacker; with
            // no other, the attacker itself.
            const std::set<int> touched = touchedBesides(s.attacker);
            if (touched.size() > 1) {
                throw std::runtime_error("CalcTargetDmg touched more than one battler besides the attacker");
            }
            target = touched.empty() ? s.attacker : battle::BattlerSlot::fromIndex(*touched.begin());
        } else if ((call.aimingRules & kReflectedRule) != 0) {
            // A bounce: the battler it bounced off this time (bounceAttacker()), and its byte the battler
            // it went on to.
            s.attacker = bounceAttacker(call);
            target = slotNamed(call.params[static_cast<std::size_t>(s.attacker.index())], "the bounce's target");
        } else {
            // Hits were still to come as the bounce began, and the hit is made from the battler
            // bounceAttacker() names. While hits are still to come the target is in the byte the hit
            // was given, one past the hits now to come. Once they have run out a later bounce's target
            // is in its own battler's byte, so a call with none to come is the last hit (byte 0) or a
            // later bounce, told apart by whose cells the call touched.
            s.attacker = bounceAttacker(call);
            const std::optional<battle::BattlerSlot> from = s.attacker;
            std::vector<std::uint8_t> bytes;
            if (call.hitsToCome != kNoHitsToCome) {
                bytes.push_back(call.params[static_cast<std::size_t>(call.hitsToCome + 1) % kParamsSize]);
            } else {
                bytes.push_back(call.params[0]);
                bytes.push_back(call.params[static_cast<std::size_t>(from->index())]);
            }
            std::erase(bytes, kNoOne);
            if (bytes.empty()) {
                throw std::runtime_error("a scattered hit whose block names no target");
            }
            // With no other battler touched, a byte naming the battler the hit is made from is the
            // target.
            const std::set<int> touched = touchedBesides(s.attacker);
            target = slotNamed(bytes.front(), "the hit's target");
            for (const std::uint8_t byte : bytes) {
                if (touched.empty() ? byte == from->index() : touched.contains(byte)) {
                    target = slotNamed(byte, "the hit's target");
                }
            }
        }
        const TargetDamageResult port = portTargetDamage(s, target);
        writeScenario(s);
        writeTargetCells(s);
        overlayWrites(input(kAnimationPlace));
        return differences(port, readTargetResult());
    }

    // The damage taken and shown.
    if (!bounce) {
        s.attacker = slotNamed(call.params[kBlockAttacker], "the params block's attacker");
    } else if ((call.aimingRules & kReflectedRule) == 0) {
        // A Launcher's or Super Ball's damage is shown with its maker's bit in the block's battler
        // set (CalcReflectDmg :8451-8452), the block before it or not.
        const battle::BattlerSet maker =
            setOf(static_cast<std::uint16_t>(call.params[kBlockSet] | call.params[kBlockSet + 1] << 8));
        std::optional<battle::BattlerSlot> found;
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            if (maker.has(battle::BattlerSlot::fromIndex(index))) {
                found = battle::BattlerSlot::fromIndex(index);
            }
        }
        if (!found) {
            throw std::runtime_error("a Launcher's damage shown with no maker in its block");
        }
        s.attacker = *found;
    } else {
        if (!call.previousAttacker) {
            throw std::runtime_error("a bounce's damage shown with no params block before it");
        }
        s.attacker = slotNamed(*call.previousAttacker, "the block before's attacker");
    }
    const std::uint8_t queueEnd = input(kQueueEnd);
    const std::uint8_t buffers = input(kNumeralBufferCount);
    const ApplyDamageResult port = portApplyDamage(s);
    writeScenario(s);
    writeTargetCells(s);
    writeApplyCells(s);
    writeByte(kQueueEnd, queueEnd);
    writeByte(kNumeralBufferCount, buffers);
    overlayWrites(0);
    return differences(port, readApplyResult(queueEnd / kQueueCommandSize, buffers));
}

}  // namespace ostinato::trace
