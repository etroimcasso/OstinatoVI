// A traced special effect rebuilt as a scenario from the cells it read, and replayed against the
// port.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/attack.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/formations.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"
#include "oracle/replay_shared.h"
#include "oracle/routines.h"
#include "oracle/status/attacker_effects_shared.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/report_text.h"
#include "trace/cells.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

// Who a bounce's special effect is worked out for. The bounce works out a hit's statuses and effect
// before the battler it is made from joins the bounces that hit, so for a Launcher's or Super Ball's
// first hit those bytes are still empty and the hit is made by the battler making the attack: the
// attacker of the params block at the entry of its attacker effect, the last before the bounce, or
// with none traced, the attacker of the params block before the bounce's.
battle::BattlerSlot bounceEffectAttacker(const TracedCall& call) {
    const bool scattered = (call.aimingRules & kReflectedRule) == 0;
    const bool noneYet = call.params[kBlockSet] == 0 && call.params[kBlockSet + 1] == 0;
    if (scattered && noneYet) {
        // While hits are to come an attack stops before its own block is written, after its attacker
        // effect: the effect's params name the battler making the attack. With no attacker effect in
        // the trace, the block before the bounce's is the attack's own.
        if (call.attackerEffectMaker) {
            return slotNamed(*call.attackerEffectMaker, "the attacker effect's attacker");
        }
        if (!call.previousAttacker) {
            throw std::runtime_error("a scattered hit's effect with no attacker effect or params block before it");
        }
        return slotNamed(*call.previousAttacker, "the block before's attacker");
    }
    return bounceAttacker(call);
}

// A link cell: the battler it names, and whether its top bit marks the link as ending.
std::optional<battle::BattlerSlot> linkOf(std::uint8_t value) {
    return value == kNoOne ? std::nullopt : slotTimesTwo(static_cast<std::uint8_t>(value & ~kEndingBit));
}

bool linkEnding(std::uint8_t value) {
    return value != kNoOne && (value & kEndingBit) != 0;
}

// One of the battle's interrupting actions as its list node holds it.
battle::Action immediateActionOf(std::uint8_t command, std::uint8_t attack, std::uint16_t targets) {
    if (command == kScanCommand) {
        return battle::ScanAction{.target = battle::BattlerSlot::fromIndex((attack / 2) % battle::kBattlerSlots)};
    }
    if (command == kVisibilityCommand) {
        return battle::MonsterVisibilityAction{
            .mode = static_cast<battle::MonsterVisibilityMode>(targets & 0xFF),
            .monsters = battle::BattlerSet{BattleSlotMask{}, BattleSlotMask(static_cast<std::uint8_t>(targets >> 8))},
            .type = static_cast<MonsterEntranceType>(attack)};
    }
    return battle::CommandAction{.command = static_cast<BattleCommandId>(command),
                                 .attack = static_cast<AttackId>(attack),
                                 .targets = setOf(targets)};
}
}  // namespace

DamageScenario DamageOracle::readEffectScenario() {
    DamageScenario s = readStatusScenario();
    battle::BattleState& state = s.state;
    battle::AttackInFlight& attack = s.attack;

    // The aim and the hit check's cells.
    const std::uint8_t aiming = readByte(kAimingRules);
    attack.rules.noCharacterTargets = (aiming & 0x80) != 0;
    attack.rules.randomTarget = (aiming & 0x40) != 0;
    attack.rules.fallBackToEarlierTargets = (aiming & 0x20) != 0;
    attack.rules.reflected = (aiming & 0x10) != 0;
    attack.rules.mayTargetTheFallen = (aiming & 0x08) != 0;
    attack.rules.keepTargetsThatAreGone = (aiming & 0x04) != 0;
    attack.rules.opposingSideOnly = (aiming & 0x02) != 0;
    attack.rules.excludeAttacker = (aiming & 0x01) != 0;
    attack.targets = setOf(readWord(kTargets));
    const std::uint8_t aimFlags = readByte(kAimFlags);
    attack.scratch.aimExactly = (aimFlags & kAimExactlyBit) != 0;
    state.endTurnAfterAction = (aimFlags & kEndTurnBit) != 0;
    attack.scratch.earlierTargets = setOf(readWord(kEarlierTargets));
    const battle::BattlerSet enemies = setOf(readWord(kEnemyCharacters));
    state.battleType = static_cast<battle::BattleType>(readByte(kBattleType));
    state.pincerSides[0] = BattleSlotMask(readByte(kPincerFirstSide));
    state.pincerSides[1] = BattleSlotMask(readByte(kPincerSecondSide));
    state.targetableOrPresent = setOf(readWord(kReachFirst));
    state.aiUntargetable = setOf(static_cast<std::uint16_t>(~readWord(kReachSecond)));
    state.scratch.countersEnabled = (readByte(kCountersEnabled) & kCountersOn) != 0;
    attack.scratch.awayFromTheFieldUnreachable = readByte(kReachKeepsAway) == kAwayUnreachable;
    state.awayFromTheField = setOf(readWord(kAwayFromTheField));
    attack.scratch.targetCount = readByte(kTargetCount);
    attack.scratch.quietMisses = readByte(kQuietMisses) == 0;
    attack.scratch.hitInTheBack = setOf(readWord(kHitInTheBack));
    attack.queuedCommand = static_cast<BattleCommandId>(readByte(kQueuedCommand));
    const std::uint8_t lastSpell = readByte(kLastSpell);
    attack.scratch.lastSpell = lastSpell == kNoOne ? std::nullopt : std::optional<AttackId>(static_cast<AttackId>(lastSpell));
    const std::uint8_t lastItem = readByte(kLastItem);
    attack.scratch.lastItem = lastItem == kNoOne ? std::nullopt : std::optional<ItemId>(static_cast<ItemId>(lastItem));
    const std::uint8_t hits = readByte(kExtraHits);
    state.extraHits = hits == kNoHitsToCome ? std::nullopt : std::optional<int>(hits + 1);

    // The effects' own cells.
    attack.params = paramsFromBytes(machine_.read(retropp::MemoryRegion{.at = kParamsFlags, .size = 8}));
    attack.castingSwirlSuppressed = (readByte(kSwirlCell) & kSwirlSuppressedBit) != 0;
    const std::uint8_t running = readByte(kRunningCell);
    state.counterattackRunning = (running & kCounterattackBit) != 0;
    state.cannotRun = (running & kCannotRunBit) != 0;
    state.escapeBlockedByMonster = (running & kEscapeBlockedBit) != 0;
    attack.automaticCritical = (readByte(kAttackFlags) & kAutomaticCriticalClearBit) == 0;
    const std::uint8_t spell = readByte(kFollowUpSpell);
    attack.scratch.followUpSpell = spell == kNoOne ? std::nullopt : std::optional<AttackId>(static_cast<AttackId>(spell));
    const std::uint8_t message = readByte(kMessage);
    attack.scratch.message = message == kNoOne ? std::nullopt : std::optional<std::uint8_t>(message);
    attack.scratch.attacksLeft = readByte(kAttacksLeft);
    attack.scratch.dragonHorn = readByte(kDragonHorn) != 0;
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        const battle::BattlerSet pick = setOf(readWord(kMindBlastPicks + static_cast<std::uint32_t>(position * 2)));
        attack.scratch.mindBlastPicks[position] = std::nullopt;
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            if (pick.has(battle::BattlerSlot::fromIndex(index))) {
                attack.scratch.mindBlastPicks[position] = battle::BattlerSlot::fromIndex(index);
            }
        }
    }
    const std::uint8_t quickTurns = readByte(kQuickTurns);
    state.quickTurnsLeft = quickTurns == kNoQuick ? std::nullopt : std::optional<std::uint8_t>(quickTurns);
    state.quickTarget = slotOrNone(readByte(kQuickTarget));
    const std::uint8_t endOfBattle = readByte(kEndOfBattleEvent);
    state.endOfBattleEvent = endOfBattle == 0 ? std::nullopt
                                              : std::optional<battle::EndOfBattleEvent>(
                                                    static_cast<battle::EndOfBattleEvent>(endOfBattle / 2));
    state.leavingAfterAction = setOf(readWord(kLeaving));
    state.allyCount = readByte(kAllyCount);
    const std::vector<std::uint8_t> gil = machine_.read(retropp::MemoryRegion{.at = kGil, .size = 3});
    state.party.gil = static_cast<std::uint32_t>(gil[0] | gil[1] << 8 | gil[2] << 16);
    const std::vector<std::uint8_t> steps = machine_.read(retropp::MemoryRegion{.at = kSteps, .size = 3});
    state.party.steps = static_cast<std::uint32_t>(steps[0] | steps[1] << 8 | steps[2] << 16);
    state.playClockFrames = readByte(kPlayClockFrames);

    // The target effects' cells.
    const std::uint8_t formationFlags = readByte(kFormationFlags);
    const auto formation = std::find_if(formationAux().begin(), formationAux().end(),
                                        [&](const FormationAuxEntry& entry) { return entry.record.flags == formationFlags; });
    if (formation == formationAux().end()) {
        throw std::runtime_error("no formation has the flags " + std::to_string(formationFlags));
    }
    state.formation = formation->id;
    state.holdingItem = battle::BattlerSet{BattleSlotMask(readByte(kHoldingItem)), BattleSlotMask{}};
    state.engulfed = battle::BattlerSet{BattleSlotMask(readByte(kEngulfed)), BattleSlotMask{}};
    state.takenFromTheParty = battle::BattlerSet{BattleSlotMask(readByte(kTakenFromTheParty)), BattleSlotMask{}};
    state.partyLeft = battle::BattlerSet{BattleSlotMask(readByte(kPartyLeft)), BattleSlotMask{}};
    state.flippedByControl = setOf(readWord(kFlippedSet));
    state.heldByZinger = slotOrNone(readByte(kZingerMonster));
    state.swallowedByZinger = slotOrNone(readByte(kZingerSwallowed));
    if (const std::optional<battle::BattlerSlot> sketched = slotOrNone(readByte(kSketched))) {
        attack.scratch.sketch = battle::SketchDrawn{.monster = *sketched};
    }
    const std::uint8_t messageItem = readByte(kMessageItem);
    attack.scratch.messageItem =
        messageItem == kNoOne ? std::nullopt : std::optional<ItemId>(static_cast<ItemId>(messageItem));
    const std::vector<std::uint8_t> amount = machine_.read(retropp::MemoryRegion{.at = kMessageAmount, .size = 3});
    const auto messageAmount = static_cast<std::uint32_t>(amount[0] | amount[1] << 8 | amount[2] << 16);
    attack.scratch.messageAmount = messageAmount == 0 ? std::nullopt : std::optional<std::uint32_t>(messageAmount);
    attack.scratch.hideWithoutTargets = (readByte(kShownWithoutTargets) & kShownBit) == 0;

    // A list of actions from its head, each node's next its own at the list's end.
    const auto list = [&](std::uint8_t head, bool immediate) {
        std::deque<battle::Action> actions;
        std::uint8_t node = head;
        for (std::uint32_t step = 0; node != kFreeSlot && step < kActionSlots; ++step) {
            const auto word = static_cast<std::uint32_t>(node) * 2;
            const std::uint16_t command = readWord(kActionCommand + word);
            const auto commandIndex = static_cast<std::uint8_t>(command & 0xFF);
            const auto attackIndex = static_cast<std::uint8_t>(command >> 8);
            const std::uint16_t targets = readWord(kActionTargets + word);
            if (immediate) {
                actions.push_back(immediateActionOf(commandIndex, attackIndex, targets));
            } else {
                actions.push_back(battle::CommandAction{.command = static_cast<BattleCommandId>(commandIndex),
                                                        .attack = static_cast<AttackId>(attackIndex),
                                                        .targets = setOf(targets),
                                                        .mpCost = readByte(kActionMp + word)});
            }
            const std::uint8_t following = readByte(kActionNext + node);
            if (following == node || (following & kEndingBit) != 0) {
                break;
            }
            node = following;
        }
        return actions;
    };
    state.immediateActions = list(readByte(kFirstImmediateAction), true);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battler.actsAsEnemy = enemies.has(slot);
        battler.inBattleMenu = (readByte(kOnTheField + at) & kInBattleMenuBit) != 0;
        battler.charmedBy = slotOrNone(readByte(kCharmedBy + at));
        battler.stamina = readByte(kStamina + at);
        battler.evadeFactor = readByte(kEvadeAndMagicBlock + at);
        battler.magicBlockFactor = readByte(kEvadeAndMagicBlock + at + 1);
        battler.physicalBlockGraphics.bits = readByte(kBlockGraphics + at);
        battler.magicBlockGraphics.bits = readByte(kBlockGraphics + at + 1);
        const auto hit = [&](std::uint32_t byCell, std::uint32_t withCell, auto with) {
            using With = decltype(with);
            const std::uint8_t by = readByte(byCell + at);
            const std::optional<battle::BattlerSlot> who = slotOrNone(by);
            return who ? std::optional<battle::Hit<With>>(battle::Hit<With>{.by = *who, .with = With{readByte(withCell + at)}})
                       : std::nullopt;
        };
        battler.lastHit.command = hit(kLastHitCommandBy, kLastHitCommand, BattleCommandId{});
        battler.lastHit.spell = hit(kLastHitSpellBy, kLastHitSpell, AttackId{});
        battler.lastHit.item = hit(kLastHitItemBy, kLastHitItem, ItemId{});
        if (const std::optional<battle::BattlerSlot> who = slotOrNone(readByte(kLastHitElementBy + at))) {
            battler.lastHit.element = battle::Hit<ElementSet>{.by = *who, .with = ElementSet{readByte(kLastHitElement + at)}};
        } else {
            battler.lastHit.element.reset();
        }
        battler.seizing = slotOrNone(readByte(kSeizing + at));
        battler.monsterFlags.bits = readByte(kMonsterBattleFlags + at);
        battler.gold = readWord(kGold + at);

        const std::uint8_t controlling = readByte(kControlling + at);
        const std::uint8_t controlledBy = readByte(kController + at);
        battler.controlling = linkOf(controlling);
        battler.controlledBy = linkOf(controlledBy);
        battler.controlEnding = linkEnding(controlling) || linkEnding(controlledBy);
        battler.seizedBy = slotOrNone(readByte(kSeizedBy + at));
        battler.charming = slotOrNone(readByte(kCharming + at));
        battler.bodyguard = slotOrNone(readByte(kBodyguard + at));
        battler.guarding = slotOrNone(readByte(kGuarding + at));
        const std::uint8_t item = readByte(kItemInHand + at);
        battler.itemInHand = item == kNoOne ? std::nullopt : std::optional<ItemId>(static_cast<ItemId>(item));
        battler.targetingTarget = slotOrNone(readByte(kTargetingTarget + at));
        battler.aiVariable = readByte(kAiVariable + at);
        battler.rareSteal = static_cast<ItemId>(readByte(kRareSteal + at));
        battler.commonSteal = static_cast<ItemId>(readByte(kCommonSteal + at));
        battler.metamorph.packed = readByte(kMetamorph + at);
        if (slot.isMonster()) {
            battler.monster = static_cast<MonsterId>(readWord(kMonsterIndex + at));
        }
        battler.airAnchored = (readByte(kAnchorCell + at) & kNotAnchoredBit) == 0;
        const std::uint8_t triggers = readByte(kMoreTriggers + at);
        battler.triggers.usesControlMenu = (triggers & kControlMenuBit) != 0;
        battler.actions = list(readByte(kFirstAction + at), false);
        if (slot.isCharacter()) {
            const auto position = static_cast<std::size_t>(slot.position());
            const auto place = static_cast<std::uint32_t>(position) * kHandItemSize;
            battler.weaponHand.battlePower = readByte(kHandPower + at);
            battler.shieldHand.battlePower = readByte(kHandPower + at + 1);
            state.inventory.weaponHands[position].item = static_cast<ItemId>(readByte(kWeaponHands + place));
            state.inventory.weaponHands[position].quantity = readByte(kWeaponHands + place + kHandQuantity);
            state.inventory.shieldHands[position].item = static_cast<ItemId>(readByte(kShieldHands + place));
            state.inventory.shieldHands[position].quantity = readByte(kShieldHands + place + kHandQuantity);
            const auto menu = kControlMenus + static_cast<std::uint32_t>(position) * kControlMenuSize;
            for (std::size_t row = 0; row < battler.controlMenu.size(); ++row) {
                const auto first = menu + static_cast<std::uint32_t>(row) * kControlRowSize;
                battler.controlMenu[row] = battle::ControlMenuEntry{.attack = static_cast<AttackId>(readByte(first)),
                                                                    .castable = (readByte(first + 1) & kUncastableBit) == 0,
                                                                    .targeting = Targeting{readByte(first + 2)}};
            }
        }
    }
    return s;
}

std::string DamageOracle::replayEffectCall(const TracedCall& call, const TracedRoutine& routine, bool bounce) {
    const bool onTarget = routine.entry == kDoTargetEffect;
    // The battle as the oracle writes a default scenario: for the effect on a target, the attack's own
    // animation first in the script's queue, as the frame leaves it.
    const DamageScenario plain;
    writeScenario(plain);
    writeTargetCells(plain);
    writeApplyCells(plain);
    writeStatusCells(plain);
    writeAimCells(plain);
    writeEffectCells(plain);
    if (onTarget) {
        writeTargetEffectCells(plain);
    }
    const std::uint8_t ourPlace = readByte(kAnimationPlace);
    const std::uint8_t ourEnd = readByte(kQueueEnd);
    const std::uint8_t ourNext = readByte(kNextParamsBlock);

    // The cartridge's queue and params blocks stood elsewhere: what the call read and wrote of them is
    // moved to the default's places — the frame's animation to its place, what follows the queue's end
    // and the next params block to theirs.
    const auto recorded = [&](std::uint32_t cell) -> std::optional<std::uint8_t> {
        const auto found = call.inputs.find(cell);
        return found != call.inputs.end() ? std::optional<std::uint8_t>(found->second) : std::nullopt;
    };
    const std::optional<std::uint8_t> theirPlace = recorded(kAnimationPlace);
    const std::optional<std::uint8_t> theirEnd = recorded(kQueueEnd);
    const std::optional<std::uint8_t> theirNext = recorded(kNextParamsBlock);
    const auto moved = [&](std::uint32_t cell) -> std::optional<std::uint32_t> {
        if (cell >= kQueue && cell < kQueue + kQueueSpan) {
            const std::uint32_t offset = cell - kQueue;
            if (theirEnd && offset >= *theirEnd) {
                return kQueue + ourEnd + (offset - *theirEnd);
            }
            if (onTarget && theirPlace && offset >= *theirPlace && offset < *theirPlace + kQueueCommandSize) {
                return kQueue + ourPlace + (offset - *theirPlace);
            }
            throw std::runtime_error("the call touched the script's queue at " + busAddress(cell) +
                                     " with no place or end read to place it by");
        }
        if (cell >= kParamsBlocks && cell < kParamsBlocks + kBlocksSize) {
            const std::uint32_t offset = cell - kParamsBlocks;
            if (theirNext && offset >= *theirNext) {
                return kParamsBlocks + ourNext + (offset - *theirNext);
            }
            throw std::runtime_error("the call touched a params block at " + busAddress(cell) +
                                     " with no next block read to place it by");
        }
        if (cell == kAnimationPlace || cell == kQueueEnd || cell == kNextParamsBlock) {
            return std::nullopt;
        }
        return cell;
    };
    for (const auto& [cell, value] : call.inputs) {
        if (const std::optional<std::uint32_t> to = moved(cell)) {
            writeByte(*to, value);
        }
    }
    // The attack's context at the call's entry.
    writeBytes(retropp::MemoryRegion{.at = kParamsCells, .size = kParamsSize}, call.params);
    const battle::BattlerSlot attacker =
        bounce ? bounceEffectAttacker(call) : slotNamed(call.params[kBlockAttacker], "the params block's attacker");
    std::optional<battle::BattlerSlot> target;
    if (onTarget) {
        if (!call.target) {
            throw std::runtime_error("DoTargetEffect's target is not in the trace: no attack's statuses on one target "
                                     "were worked out just before it");
        }
        target = call.target;
        // The frame records who last hit the target, and with what command, before the effect; a pair the
        // call did not read stands as the frame left it.
        const auto at = static_cast<std::uint32_t>(target->index() * 2);
        if (!call.inputs.contains(kLastHitCommandBy + at)) {
            writeByte(kLastHitCommandBy + at, static_cast<std::uint8_t>(slotIndex(attacker)));
        }
        if (!call.inputs.contains(kLastHitCommand + at)) {
            writeByte(kLastHitCommand + at, readByte(kQueuedCommand));
        }
    }
    // A party member the reach cell shows held in Seize, whose holder's cell the call did not read, is
    // given a holder: the call read only the reach cell, so who holds it plays no part in its answer.
    if (const std::optional<std::uint8_t> reach = recorded(kReachCharacters)) {
        for (int position = 0; position < battle::kPartySlots; ++position) {
            const auto at = static_cast<std::uint32_t>(battle::BattlerSlot::character(position).index() * 2);
            if ((*reach & (1u << position)) == 0 && !call.inputs.contains(kSeizedBy + at) &&
                readByte(kSeizedBy + at) == kNoOne) {
                writeByte(kSeizedBy + at, static_cast<std::uint8_t>(slotIndex(battle::BattlerSlot::monster(0))));
            }
        }
    }

    DamageScenario s = readEffectScenario();
    s.attacker = attacker;

    // The cartridge's answer: the same cells with the call's writes, moved the same way.
    const auto guestCells = [&] {
        for (const auto& [cell, value] : call.writes) {
            if (cell == kQueueEnd && theirEnd) {
                writeByte(kQueueEnd, static_cast<std::uint8_t>(ourEnd + (value - *theirEnd)));
            } else if (cell == kAnimationPlace && theirEnd) {
                // A command added: its place is counted from the queue's end.
                writeByte(kAnimationPlace, static_cast<std::uint8_t>(ourEnd + (value - *theirEnd)));
            } else if (cell == kAnimationPlace && theirPlace) {
                writeByte(kAnimationPlace, static_cast<std::uint8_t>(ourPlace + (value - *theirPlace)));
            } else if (cell == kNextParamsBlock && theirNext) {
                writeByte(kNextParamsBlock, static_cast<std::uint8_t>(ourNext + (value - *theirNext)));
            } else if (cell == kAnimationPlace || cell == kQueueEnd || cell == kNextParamsBlock) {
                throw std::runtime_error("the call moved the script's place at " + busAddress(cell) +
                                         " without reading where it stood");
            } else if (const std::optional<std::uint32_t> to = moved(cell)) {
                writeByte(*to, value);
            }
        }
    };
    if (!onTarget) {
        const AttackerEffectResult port = attackerEffectResult(runAttackerEffect(s));
        guestCells();
        return differences(port, readAttackerEffectResult());
    }
    const TargetEffectResult port = targetEffectResult(runTargetEffect(s, *target));
    guestCells();
    return differences(port, readTargetEffectResult());
}

}  // namespace ostinato::trace
