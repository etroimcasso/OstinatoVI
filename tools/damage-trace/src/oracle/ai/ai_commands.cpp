// The cartridge's three commands only a script queues, run over a battle.

#include "oracle/damage_oracle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/status_set.h"

#include "cells/ai/ai.h"
#include "cells/damage/frame.h"
#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "oracle/routines.h"
#include "scenarios/ai/ai_run_shared.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

// A command's start sets the sixteen cells from kLastSpell.
constexpr std::uint32_t kPerCommandCells = 16;
// The steps that open with the three commands' own bytes.
constexpr std::uint8_t kVariableStep = 0xF8;
constexpr std::uint8_t kSwitchStep = 0xF9;
}  // namespace

void DamageOracle::writeCommandStart() {
    // A command starts as running one starts it (ExecCmd :3093-3109): a fresh battle script, one
    // attack and no targets remembered, the casting swirl suppressed, the animation going out as a
    // show-animation command, no extra multiplier, weapon spellcast, target count or Dragon Horn,
    // every flag of the two attack-flags bytes set, and the sixteen per-command cells at $FF.
    writeByte(kQueueEnd, 0);
    writeByte(kWorkRam + 0x3A70, 0);
    writeWord(kNextParamsBlock, 0);
    writeWord(kNumeralBufferCount, 0);
    writeWord(kWorkRam + 0x3A30, 0);
    writeWord(kEarlierTargets, 0);
    writeByte(kWorkRam + 0x00B0, static_cast<std::uint8_t>(readByte(kWorkRam + 0x00B0) | kSwirlSuppressedBit));
    writeByte(kAnimationCommand, kShowAnimationCommand);
    writeByte(kWorkRam + 0x00BD, 0);
    writeByte(kWeaponSpellcast, 0);
    writeByte(kTargetCount, 0);
    writeByte(kWorkRam + 0x3A8E, 0);
    writeByte(kFirstAttackFlags, 0xFF);
    writeByte(kAttackFlags, kAttackFlagsAtStart);
    const std::vector<std::uint8_t> perCommand(kPerCommandCells, 0xFF);
    writeBytes(retropp::MemoryRegion{.at = kLastSpell, .size = kPerCommandCells}, perCommand);
}

AiCommandResult DamageOracle::aiCommand(const AiScenario& scenario, const battle::Action& action) {
    const battle::BattleState& state = scenario.state;
    // Every cell the attack frame reads, from the attack a command starts with (CommandExecutor):
    // SetStatus and ClearStatus run the frame whole.
    battle::AttackInFlight starting;
    starting.attackerLevel = state.lastAttackLevel;
    writeAttackCells(DamageScenario{.state = state, .attack = starting, .attacker = scenario.actor});
    writeAiCells(scenario);
    // The AI's untargetable monsters as their own cell holds them, which the effects change; the
    // other reach cell that writeAiCells() folds them into is opened.
    writeByte(kAiTargetable, static_cast<std::uint8_t>(~state.aiUntargetable.monsters.bits));
    writeWord(kReachSecondCell, 0xFFFF);
    writeWord(kInvincible, maskOf(state.invincible));
    writeByte(kEndOfBattleEvent,
              state.endOfBattleEvent ? static_cast<std::uint8_t>(static_cast<int>(*state.endOfBattleEvent) * 2) : 0);
    writeByte(kPartyPlayed, state.party.currentParty);
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        const game::PartyMembership& m = state.party.membership[record];
        writeByte(kPartyBytes + static_cast<std::uint32_t>(record),
                  static_cast<std::uint8_t>((m.party & kPartyNumberBits) | (m.slot & kPartySlotBits) << kPartySlotShift |
                                            (m.backRow ? kPartyRowBit : 0) | (m.enabled ? kRecordEnabledBit : 0) |
                                            (m.visible ? kRecordShownBit : 0)));
    }
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::Battler& battler = state.at(battle::BattlerSlot::fromIndex(index));
        const auto at = static_cast<std::uint32_t>(index * 2);
        writeWord(kAtbRate + at, battler.atbRate);
        if (index < battle::kPartySlots) {
            writeByte(kRecordIndex + at, battler.record ? static_cast<std::uint8_t>(*battler.record) : kNoOne);
        }
        writeByte(kToSet12 + at, battler.statusesToSet.bytes[0]);
        writeByte(kToSet12 + at + 1, battler.statusesToSet.bytes[1]);
        writeByte(kToSet34 + at, battler.statusesToSet.bytes[2]);
        writeByte(kToSet34 + at + 1, battler.statusesToSet.bytes[3]);
        writeByte(kToClear12 + at, battler.statusesToClear.bytes[0]);
        writeByte(kToClear12 + at + 1, battler.statusesToClear.bytes[1]);
        writeByte(kToClear34 + at, battler.statusesToClear.bytes[2]);
        writeByte(kToClear34 + at + 1, battler.statusesToClear.bytes[3]);
    }
    // The action's three bytes where its command reads them.
    const std::vector<std::uint8_t> bytes = std::visit(
        [](const auto& a) -> std::vector<std::uint8_t> {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, battle::BattleVariableAction> ||
                          std::is_same_v<T, battle::BattleSwitchAction> || std::is_same_v<T, battle::AiEffectAction>) {
                return stepBytesFor(a);
            } else {
                throw std::logic_error("aiCommand() runs the three commands only a script queues");
            }
        },
        action);
    writeCommandStart();

    const auto at = [&](std::size_t i) -> std::uint8_t { return i < bytes.size() ? bytes[i] : 0; };
    writeByte(kCommandAttack, at(1));
    writeWord(kCommandTargets, static_cast<std::uint16_t>(at(2) | at(3) << 8));
    const std::size_t command = bytes[0] == kVariableStep ? 0 : bytes[0] == kSwitchStep ? 1 : 2;
    // The command and attack bytes the action leaves for the battle program as it runs.
    constexpr std::array<BattleCommandId, 3> kCommands{battle::kBattleVariableCommand, battle::kBattleSwitchCommand,
                                                      battle::kAiEffectCommand};
    writeByte(kCommand, static_cast<std::uint8_t>(kCommands[command]));
    writeByte(kQueuedCommand, static_cast<std::uint8_t>(kCommands[command]));
    writeByte(kQueuedAttack, at(1));
    call([&] {
        aiCommands_[command](slotIndex(scenario.actor), kByteWideStatus, kBattleDataBank, kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedAiCommands[command]);
    AiCommandResult result = readAiCommandResult();
    result.attack = readAttackResult(false);
    return result;
}

AiCommandResult DamageOracle::readAiCommandResult() {
    AiCommandResult result;
    for (std::size_t i = 0; i < result.battleVariables.size(); ++i) {
        result.battleVariables[i] = readByte(kBattleVariables + static_cast<std::uint32_t>(i));
    }
    for (std::size_t i = 0; i < result.switches.size(); ++i) {
        result.switches[i] = readByte(kBattleSwitches + static_cast<std::uint32_t>(i));
    }
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.ownVariables[i] = readByte(kAiVariable + at);
        result.ownTimers[i] = readWord(kOwnTimer + at);
        result.atbRates[i] = readWord(kAtbRate + at);
        result.statuses[i] = StatusSet{{readByte(kStatus12 + at), readByte(kStatus12 + at + 1),
                                        readByte(kStatus34 + at), readByte(kStatus34 + at + 1)}};
        result.statusesToSet[i] = StatusSet{{readByte(kToSet12 + at), readByte(kToSet12 + at + 1),
                                             readByte(kToSet34 + at), readByte(kToSet34 + at + 1)}};
        result.statusesToClear[i] = StatusSet{{readByte(kToClear12 + at), readByte(kToClear12 + at + 1),
                                               readByte(kToClear34 + at), readByte(kToClear34 + at + 1)}};
    }
    result.invincible = setOf(readWord(kInvincible));
    result.aiUntargetable = battle::BattlerSet{
        BattleSlotMask{}, BattleSlotMask(static_cast<std::uint8_t>(~readByte(kAiTargetable) & kMonsterSlotBits))};
    result.battleCounter = readWord(kBattleTimer);
    const std::uint8_t event = readByte(kEndOfBattleEvent);
    if (event != 0) {
        result.endOfBattleEvent = static_cast<battle::EndOfBattleEvent>(event / 2);
    }
    result.gauRecruited = (readByte(kStoryFlags) & kGauRecruitedBit) != 0;
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        const std::uint8_t value = readByte(kPartyBytes + static_cast<std::uint32_t>(record));
        result.membership[record] =
            game::PartyMembership{.party = static_cast<std::uint8_t>(value & kPartyNumberBits),
                                  .slot = static_cast<std::uint8_t>(value >> kPartySlotShift & kPartySlotBits),
                                  .backRow = (value & kPartyRowBit) != 0,
                                  .enabled = (value & kRecordEnabledBit) != 0,
                                  .visible = (value & kRecordShownBit) != 0};
    }
    result.rngPosition = readByte(kRngPosition);
    return result;
}

}  // namespace ostinato::trace
