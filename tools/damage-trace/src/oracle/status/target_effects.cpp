// The cartridge's special effect on one target run over a scenario and read back as named cells.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/attack.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "battle/target_effects.h"
#include "data/ai_scripts.h"
#include "data/attack_properties.h"
#include "data/formations.h"
#include "ostinato/attack_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_set.h"

#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"
#include "oracle/routines.h"
#include "oracle/status/attacker_effects_shared.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/report_text.h"
#include "scenarios/status/attacker_effects_shared.h"
#include "scenarios/status/target_effects_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

constexpr std::uint32_t kFlipped = kWorkRam + 0x2F54;
}  // namespace

void DamageOracle::writeTargetEffectCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    writeByte(kFormationFlags, getFormationAux(state.formation).flags);
    writeByte(kHoldingItem, state.holdingItem.characters.bits);
    writeByte(kEngulfed, state.engulfed.characters.bits);
    writeByte(kTakenFromTheParty, state.takenFromTheParty.characters.bits);
    writeByte(kPartyLeft, state.partyLeft.characters.bits);
    writeByte(kMonstersFallen, state.monstersFallen.monsters.bits);
    writeWord(kFlippedSet, maskOf(state.flippedByControl));
    writeByte(kZingerMonster, slotByte(state.heldByZinger));
    writeByte(kZingerSwallowed, slotByte(state.swallowedByZinger));
    writeInFlightByte(kSketched, attack.scratch.sketch ? static_cast<std::uint8_t>(slotIndex(attack.scratch.sketch->monster)) : kNoOne);
    writeInFlightByte(kMessageItem, attack.scratch.messageItem ? static_cast<std::uint8_t>(*attack.scratch.messageItem) : kNoOne);
    std::array<std::uint8_t, 3> amount{};
    writeThreeBytes(amount, attack.scratch.messageAmount.value_or(0));
    writeInFlightBytes(retropp::MemoryRegion{.at = kMessageAmount, .size = 3}, amount);
    writeInFlightByte(kShownWithoutTargets, attack.scratch.hideWithoutTargets ? 0 : kShownBit);

    // The script's queue holds the attack's own animation, put there by the frame; a rewrite of it
    // shows on either side.
    writeInFlightByte(kAnimationPlace, 0);
    writeInFlightByte(kQueueEnd, kQueueCommandSize);
    const std::array<std::uint8_t, kQueueCommandSize> unwritten{kUnwritten, kUnwritten, kUnwritten, kUnwritten};
    writeInFlightBytes(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize}, unwritten);

    // Every list empty; then each battler's waiting commands, handed slots from the top down as the
    // battle hands them out.
    std::array<std::uint8_t, kActionSlots> free{};
    free.fill(kFreeSlot);
    writeBytes(retropp::MemoryRegion{.at = kActionNext, .size = kActionSlots}, free);
    writeByte(kFirstImmediateAction, kFreeSlot);
    std::uint8_t next = kActionSlots - 1;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        std::uint8_t head = kFreeSlot;
        std::optional<std::uint8_t> previous;
        for (const battle::Action& action : battler.actions) {
            const auto* command = std::get_if<battle::CommandAction>(&action);
            if (command == nullptr) {
                continue;
            }
            const std::uint8_t slot = next--;
            const auto word = static_cast<std::uint32_t>(slot) * 2;
            writeWord(kActionCommand + word, static_cast<std::uint16_t>(static_cast<std::uint8_t>(command->command) |
                                                                        static_cast<std::uint8_t>(command->attack) << 8));
            writeWord(kActionTargets + word, maskOf(command->targets));
            writeWord(kActionMp + word, command->mpCost);
            writeByte(kActionNext + slot, slot);
            if (previous) {
                writeByte(kActionNext + *previous, slot);
            } else {
                head = slot;
            }
            previous = slot;
        }
        writeByte(kFirstAction + at, head);
    }

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        writeByte(kControlling + at, linkByte(battler.controlling, battler.controlEnding));
        writeByte(kController + at, linkByte(battler.controlledBy, battler.controlEnding));
        writeByte(kSeizedBy + at, slotByte(battler.seizedBy));
        writeByte(kCharming + at, slotByte(battler.charming));
        writeByte(kBodyguard + at, slotByte(battler.bodyguard));
        writeByte(kGuarding + at, slotByte(battler.guarding));
        writeByte(kItemInHand + at, battler.itemInHand ? static_cast<std::uint8_t>(*battler.itemInHand) : kNoOne);
        writeByte(kTargetingTarget + at, slotByte(battler.targetingTarget));
        writeByte(kAiVariable + at, battler.aiVariable);
        writeByte(kRareSteal + at, static_cast<std::uint8_t>(battler.rareSteal));
        writeByte(kCommonSteal + at, static_cast<std::uint8_t>(battler.commonSteal));
        writeByte(kMetamorph + at, battler.metamorph.packed);
        writeWord(kMonsterIndex + at, static_cast<std::uint16_t>(battler.monster.value_or(MonsterId{})));
        writeByte(kAnchorCell + at, battler.airAnchored ? 0 : kNotAnchoredBit);
        writeByte(kMoreTriggers + at, triggersByte(battler.triggers));
        if (slot.isCharacter()) {
            const auto place = kControlMenus + static_cast<std::uint32_t>(slot.position()) * kControlMenuSize;
            for (std::size_t row = 0; row < battler.controlMenu.size(); ++row) {
                const battle::ControlMenuEntry& entry = battler.controlMenu[row];
                const auto first = place + static_cast<std::uint32_t>(row) * kControlRowSize;
                writeByte(first, static_cast<std::uint8_t>(entry.attack));
                writeByte(first + 1, entry.castable ? 0 : kUncastableBit);
                writeByte(first + 2, entry.targeting.bits);
            }
        }
    }
}

TargetEffectResult DamageOracle::targetEffect(const DamageScenario& scenario, battle::BattlerSlot target) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    writeTargetEffectCells(scenario);
    call([&] {
        doTargetEffect_(slotIndex(scenario.attacker), slotIndex(target), kByteWideStatus, kBattleDataBank,
                        kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[6]);
    return readTargetEffectResult();
}

TargetEffectResult DamageOracle::readTargetEffectResult() {
    EffectCells out;
    out.put("missed by the effect", readWord(kMissedByEffect) != 0);
    std::array<std::uint8_t, sizeof(AttackProperties)> bytes{};
    const std::vector<std::uint8_t> read =
        machine_.read(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(bytes.size())});
    std::copy(read.begin(), read.end(), bytes.begin());
    AttackProperties record{};
    std::memcpy(&record, bytes.data(), bytes.size());
    putRecord(out, record);
    out.put("damage", readWord(kDamage));
    out.put("multiplier", readByte(kMultiplier));
    out.put("command", readByte(kCommand));
    out.put("attack", readByte(kAttackIndex));
    out.put("second value", readByte(kSecondary));
    out.put("targets", std::to_string(readWord(kTargets)));
    const std::vector<std::uint8_t> params = machine_.read(retropp::MemoryRegion{.at = kParamsFlags, .size = 8});
    out.put("params", paramsText(paramsFromBytes(params)));
    out.put("earlier targets", std::to_string(readWord(kEarlierTargets)));
    out.put("shown Miss", std::to_string(readWord(kMissed)));
    const std::uint8_t message = readByte(kMessage);
    out.put("message", message == kNoOne ? std::string{"none"} : std::to_string(message));
    out.put("message item", readByte(kMessageItem));
    out.put("message amount", readWord(kMessageAmount));
    const std::uint8_t spell = readByte(kFollowUpSpell);
    out.put("spell next", spell == kNoOne ? std::string{"none"} : std::to_string(spell));
    out.put("sketched", slotText(slotTimesTwo(readByte(kSketched))));
    out.put("counterattacks", (readByte(kCountersEnabled) & kCountersOn) != 0);
    out.put("hidden without targets", (readByte(kShownWithoutTargets) & kShownBit) == 0);
    out.put("leaving", std::to_string(readWord(kLeaving)));
    out.put("party gone", readByte(kPartyLeft));
    out.put("monsters fallen", readByte(kMonstersFallen));
    out.put("engulfed", readByte(kEngulfed));
    out.put("taken from the party", readByte(kTakenFromTheParty));
    out.put("holding an item", readByte(kHoldingItem));
    out.put("flipped", readByte(kFlipped));
    out.put("Zinger's monster", slotText(slotTimesTwo(readByte(kZingerMonster))));
    out.put("Zinger swallowed", slotText(slotTimesTwo(readByte(kZingerSwallowed))));
    out.put("seized", static_cast<std::uint8_t>(~readByte(kReachCharacters)));
    out.put("end of battle", readByte(kEndOfBattleEvent) / 2);
    const std::vector<std::uint8_t> gil = machine_.read(retropp::MemoryRegion{.at = kGil, .size = 3});
    out.put("gil", gil[0] | gil[1] << 8 | gil[2] << 16);

    const auto list = [&](std::uint8_t head, bool immediate) {
        std::string text;
        std::uint8_t node = head;
        for (std::uint32_t step = 0; node != kFreeSlot && step < kActionSlots; ++step) {
            const auto word = static_cast<std::uint32_t>(node) * 2;
            const std::uint16_t command = readWord(kActionCommand + word);
            const std::uint16_t targets = readWord(kActionTargets + word);
            const auto commandIndex = static_cast<std::uint8_t>(command & 0xFF);
            const auto attack = static_cast<std::uint8_t>(command >> 8);
            text += immediate ? immediateText(commandIndex, attack, targets)
                              : actionText(commandIndex, attack, targets, readByte(kActionMp + word));
            text += "; ";
            const std::uint8_t following = readByte(kActionNext + node);
            if (following == node || (following & kEndingBit) != 0) {
                break;
            }
            node = following;
        }
        return text.empty() ? std::string{"none"} : text;
    };
    out.put("interrupting actions", list(readByte(kFirstImmediateAction), true));

    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::string of = " of battler " + std::to_string(i);
        const auto statuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return statusesText(StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                                           readByte(first34 + at + 1)}});
        };
        out.put("HP" + of, readWord(kHp + at));
        out.put("MP" + of, readWord(kMp + at));
        out.put("level" + of, readByte(kLevel + at));
        out.put("statuses" + of, statuses(kStatus12, kStatus34));
        out.put("statuses to set" + of, statuses(kToSet12, kToSet34));
        out.put("statuses to clear" + of, statuses(kToClear12, kToClear34));
        out.put("to work out" + of, readByte(kToUpdate + at));
        out.put("Air Anchor" + of, (readByte(kAnchorCell + at) & kNotAnchoredBit) == 0);
        out.put("back row" + of, (readByte(kStance + at) & kBackRowBit) != 0);
        out.put("gauge empties" + of, (readByte(kStance + at) & kEmptyGaugeBit) != 0);
        out.put("menu may open" + of, (readByte(kOnTheField + at) & kMenuMayOpenBit) != 0);
        out.put("triggers" + of, readByte(kMoreTriggers + at) & (kOvercastBit | kPhantasmBit | kControlMenuBit));
        out.put("elements" + of, std::to_string(readByte(kAbsorbedNullified + at)) + "/" +
                                     std::to_string(readByte(kAbsorbedNullified + at + 1)) + "/" +
                                     std::to_string(readByte(kWeakHalved + at)) + "/" +
                                     std::to_string(readByte(kWeakHalved + at + 1)));
        out.put("controlling" + of, readByte(kControlling + at));
        out.put("controlled by" + of, readByte(kController + at));
        out.put("seizing" + of, readByte(kSeizing + at));
        out.put("seized by" + of, readByte(kSeizedBy + at));
        out.put("charming" + of, readByte(kCharming + at));
        out.put("charmed by" + of, readByte(kCharmedBy + at));
        out.put("bodyguard" + of, readByte(kBodyguard + at));
        out.put("guarding" + of, readByte(kGuarding + at));
        out.put("Targetting" + of, readByte(kTargetingTarget + at));
        out.put("item in hand" + of, readByte(kItemInHand + at));
        out.put("AI variable" + of, readByte(kAiVariable + at));
        out.put("items to steal" + of,
                std::to_string(readByte(kRareSteal + at)) + "/" + std::to_string(readByte(kCommonSteal + at)));
        out.put("gold" + of, readWord(kGold + at));
        out.put("last hit with" + of, readByte(kLastHitCommand + at));
        out.put("last attacker" + of, readByte(kLastAttacker + at));
        out.put("actions" + of, list(readByte(kFirstAction + at), false));
        if (slot.isCharacter()) {
            const auto place = kControlMenus + static_cast<std::uint32_t>(slot.position()) * kControlMenuSize;
            std::array<battle::ControlMenuEntry, 4> menu{};
            for (std::size_t row = 0; row < menu.size(); ++row) {
                const auto first = place + static_cast<std::uint32_t>(row) * kControlRowSize;
                menu[row] = battle::ControlMenuEntry{.attack = static_cast<AttackId>(readByte(first)),
                                                     .castable = (readByte(first + 1) & kUncastableBit) == 0,
                                                     .targeting = Targeting{readByte(first + 2)}};
            }
            out.put("control menu" + of, controlMenuText(menu));
        }
    }

    std::string commands;
    const int queued = readByte(kQueueEnd) / kQueueCommandSize;
    for (int command = 0; command < queued; ++command) {
        const std::vector<std::uint8_t> entry = machine_.read(retropp::MemoryRegion{
            .at = kQueue + static_cast<std::uint32_t>(command) * kQueueCommandSize, .size = kQueueCommandSize});
        if (std::all_of(entry.begin(), entry.end(), [](std::uint8_t b) { return b == kUnwritten; })) {
            commands += "the frame's animation; ";
        } else if (entry[0] == kMessageCommand) {
            commands += "message " + std::to_string(entry[1]) + "; ";
        } else if (entry[0] == kShowAnimationCommand) {
            commands += "animation " + std::to_string(entry[1]) + " " + std::to_string(entry[2]) + " " +
                        std::to_string(entry[3]) + "; ";
        } else {
            commands += "other; ";
        }
    }
    out.put("script", commands);
    const int blocks = (readByte(kNextParamsBlock) - kParamsBlockSize) / kParamsBlockSize;
    out.put("script params blocks", blocks);
    for (int block = 0; block < blocks; ++block) {
        const std::vector<std::uint8_t> block_bytes = machine_.read(retropp::MemoryRegion{
            .at = kParamsBlocks + kParamsBlockSize * static_cast<std::uint32_t>(block + 1), .size = kParamsBlockSize});
        out.put("script params " + std::to_string(block), paramsText(paramsFromBytes(block_bytes)));
    }
    out.put("random-number position", readByte(kRngPosition));
    return TargetEffectResult{.cells = std::move(out.cells)};
}

}  // namespace ostinato::trace
