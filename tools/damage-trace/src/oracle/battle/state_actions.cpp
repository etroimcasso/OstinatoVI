// The battle's one list of actions laid into the cells as the cartridge holds it: 128 slots, each with
// the slot that follows it (its own at a list's end, $FF when free), its command and attack, its
// targets and its MP cost (AddToQueue :12815-12826, CreateAction :12944-12964); the interrupting
// actions' first slot ($340A), and each battler's first turn action ($32CC,x) and first counterattack
// ($32CD,x).
//
// Where the slots go is the battle's history: AddToQueue hands out the highest free slot. In a
// comparison each list is laid in the slots the cartridge's own list of it holds, in order, and a list
// the port holds more of takes the highest slots no list uses, so a list longer or shorter than the
// cartridge's shows in its links. Written into a machine, the lists take the slots from 127 down — the
// interrupting actions first, then each battler's turn actions and counterattacks, battler 0 first —
// as a battle's start hands them out from an empty list.
//
// What an action holds is what the routine that made it stored: a command's own command, attack,
// targets and cost; a pose reset command $2C with the party slot (CancelAction :1595-1600); a battle
// message command $25 with the message (InitBattle :6158-6161); a spell with no caster command $26 with
// its target and the spell in the targets' low byte (ReraiseEffect :1222-1225, NearFatalEffect
// :1630-1653); a monster's exit or entrance command $24 with its entrance type, its mode and monsters as
// the targets (:1255-1266); Scan command $27 with its target. Their targets, where the routine stored
// whatever $B8 held, and the cost's high byte, which CreateAction never stores, are left out.

#include "oracle/damage_oracle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <stdexcept>
#include <variant>
#include <vector>

#include "battle/action_queue.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"

#include "cells/status/effects.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {

// The commands the interrupting actions carry, besides kVisibilityCommand and kScanCommand.
constexpr std::uint8_t kPoseResetCommand = 0x2C;
constexpr std::uint8_t kBattleMessageCommand = 0x25;
constexpr std::uint8_t kSpellWithoutCasterCommand = 0x26;
constexpr std::uint32_t kTopSlot = kActionSlots - 1;
// The list's cells: the links, then the commands, targets and costs, a word a slot.
constexpr std::uint32_t kWordCells = kActionSlots * 2;

// One action as its slot holds it, with which of its targets' bytes the action determines.
struct SlotContents {
    std::uint8_t command = 0;
    std::uint8_t attack = 0;
    std::uint16_t targets = 0;
    bool targetsKnown = true;
    bool targetsHighKnown = true;
    std::uint8_t mpCost = 0;
};

SlotContents contentsOf(const battle::Action& action) {
    if (const auto* command = std::get_if<battle::CommandAction>(&action)) {
        return SlotContents{.command = static_cast<std::uint8_t>(command->command),
                            .attack = static_cast<std::uint8_t>(command->attack),
                            .targets = maskOf(command->targets),
                            .mpCost = command->mpCost};
    }
    if (const auto* pose = std::get_if<battle::ResetBattlePose>(&action)) {
        return SlotContents{.command = kPoseResetCommand,
                            .attack = static_cast<std::uint8_t>(slotIndex(pose->character)),
                            .targetsKnown = false};
    }
    if (const auto* message = std::get_if<battle::BattleMessageAction>(&action)) {
        return SlotContents{.command = kBattleMessageCommand, .attack = message->message, .targetsKnown = false};
    }
    if (const auto* spell = std::get_if<battle::SpellWithoutCaster>(&action)) {
        return SlotContents{.command = kSpellWithoutCasterCommand,
                            .attack = static_cast<std::uint8_t>(slotIndex(spell->target)),
                            .targets = static_cast<std::uint8_t>(spell->spell),
                            .targetsHighKnown = false};
    }
    if (const auto* visibility = std::get_if<battle::MonsterVisibilityAction>(&action)) {
        return SlotContents{.command = kVisibilityCommand,
                            .attack = static_cast<std::uint8_t>(visibility->type),
                            .targets = static_cast<std::uint16_t>(static_cast<std::uint8_t>(visibility->mode) |
                                                                  visibility->monsters.monsters.bits << 8)};
    }
    if (const auto* scan = std::get_if<battle::ScanAction>(&action)) {
        return SlotContents{.command = kScanCommand,
                            .attack = static_cast<std::uint8_t>(slotIndex(scan->target)),
                            .targetsKnown = false};
    }
    throw std::logic_error("an action of this kind cannot be laid: its slot's form is not written (variant " +
                           std::to_string(action.index()) + ")");
}

}  // namespace

void DamageOracle::layActionsAsTheCartridgeHoldsIt(const battle::BattleState& state) {
    // The lists, in the order a start hands out their slots, each with the cell naming its first slot.
    struct ListToLay {
        std::uint32_t head;
        const std::deque<battle::Action>* actions;
    };
    std::vector<ListToLay> lists{{kFirstImmediateAction, &state.immediateActions}};
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        lists.push_back({kFirstAction + at, &state.battlers[i].actions});
        lists.push_back({kFirstAction + at + 1, &state.battlers[i].counterActions});
    }

    // Each list's slots: the cartridge's own list's, in a comparison.
    const bool fromCartridge = capture_ != nullptr && positionsFrom_ != nullptr;
    std::vector<std::vector<std::uint8_t>> slots(lists.size());
    std::set<std::uint8_t> used;
    if (fromCartridge) {
        const BattleCells& cartridge = *positionsFrom_;
        const auto cell = [&](std::uint32_t at) { return cartridge[at - kWorkRam]; };
        for (std::size_t l = 0; l < lists.size(); ++l) {
            std::uint8_t slot = cell(lists[l].head);
            while (slot < kActionSlots && slots[l].size() < lists[l].actions->size() && !used.contains(slot)) {
                slots[l].push_back(slot);
                used.insert(slot);
                const std::uint8_t next = cell(kActionNext + slot);
                if (next == slot) {
                    break;
                }
                slot = next;
            }
        }
    }
    std::uint32_t highest = kTopSlot;
    const auto freeSlot = [&]() -> std::uint8_t {
        while (used.contains(static_cast<std::uint8_t>(highest))) {
            --highest;
        }
        const auto slot = static_cast<std::uint8_t>(highest);
        used.insert(slot);
        return slot;
    };
    for (std::size_t l = 0; l < lists.size(); ++l) {
        while (slots[l].size() < lists[l].actions->size()) {
            slots[l].push_back(freeSlot());
        }
    }

    // Every slot free, nothing of a free slot's own laid; then each list.
    std::array<std::uint8_t, kActionSlots> links{};
    links.fill(kFreeSlot);
    leaveOut(kActionCommand, kWordCells);
    leaveOut(kActionTargets, kWordCells);
    leaveOut(kActionMp, kWordCells);
    for (std::size_t l = 0; l < lists.size(); ++l) {
        const std::vector<std::uint8_t>& list = slots[l];
        layByteAsTheCartridgeHoldsIt(lists[l].head, list.empty() ? kFreeSlot : list.front());
        for (std::size_t k = 0; k < list.size(); ++k) {
            const std::uint8_t slot = list[k];
            links[slot] = k + 1 < list.size() ? list[k + 1] : slot;
            const SlotContents contents = contentsOf((*lists[l].actions)[k]);
            const std::uint32_t word = static_cast<std::uint32_t>(slot) * 2;
            const std::array<std::uint8_t, 2> commandAndAttack{contents.command, contents.attack};
            layAsTheCartridgeHoldsIt(kActionCommand + word, commandAndAttack);
            if (contents.targetsKnown) {
                layByteAsTheCartridgeHoldsIt(kActionTargets + word, static_cast<std::uint8_t>(contents.targets & 0xFF));
                if (contents.targetsHighKnown) {
                    layByteAsTheCartridgeHoldsIt(kActionTargets + word + 1, static_cast<std::uint8_t>(contents.targets >> 8));
                }
            }
            layByteAsTheCartridgeHoldsIt(kActionMp + word, contents.mpCost);
        }
    }
    layAsTheCartridgeHoldsIt(kActionNext, links);
}

}  // namespace ostinato::trace
