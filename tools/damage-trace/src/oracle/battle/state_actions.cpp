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
// the targets (:1255-1266); Scan command $27 with its target; an AI's turn or counterattack command $1F
// with the attacker it answers; damage over time command $22 with the status bit; a story event $23 with
// the event; timers run out $29 with the battler and which ran out; the party's escape $2A; Seize's drain
// $2D with the battler seized. Their attacks and targets, where the routine stored whatever $3A7B or $B8
// held, and the cost's high byte, which CreateAction never stores, are left out.
//
// Targets stored as $B8 held them reach nothing once the action runs: InitPlayerAction loads them into $B8
// (:392-393), and no such command reads $B8 before setting it — the pose reset, the battle message, Scan
// and the story event never read it (Cmd_2c :13373-13379, Cmd_25 :13250, Cmd_27 :13274, Cmd_23
// :13019-13024), damage over time sets it through ExecSelfAttack (Cmd_22 :13198, :8006-8007), the escape
// sets it from $3A38 (:5650). After the command, CheckRetal clears $B8 for each battler it considers before
// it tests it (:12532-12533, read at :12554-12555 and :12584), and for a battler it skips (:12527-12531)
// neither reads nor writes it. The actions made with $B8 as a step left it — the Gau and Shadow events
// (_48c4 :12003-12006), the timer's Revert (DecMorphCounter :2827-2829), damage over time (:15015-15018),
// the party's escape (:15093-15094), the pose reset (:1595-1600) — have their targets left out; the timer's
// Revert is the one in a battler's counterattacks (CreateRetalAction), and Cmd_04 reaches ExecSelfAttack,
// which sets $B8 to the member, or queues a script command, without reading them (:4031-4058, :8005-8007).
// A slot whose low byte alone is known was stored a byte wide over the high byte a step left: a spell with
// no caster, timers run out (:13032, :13343-13355); the high byte reaches nothing (ExecSelfAttack's wide
// store replaces it, or the command returns).

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
#include "ostinato/battle_command_id.h"

#include "cells/status/effects.h"
#include "scenarios/ai/ai_run_shared.h"
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

// The commands the battle's own actions carry: an AI's turn or counterattack ($1F, set at :504 and
// :12561), damage over time ($22, :15015-15018), a story event ($23, Cmd_23's own, set by AICmd_f7
// :4528 and :12005), timers run out ($29, :14962), the party's escape ($2A, CreateRunAwayAction :15093)
// and Seize's drain ($2D, :15025).
constexpr std::uint8_t kAiTurnCommand = 0x1F;
constexpr std::uint8_t kDamageOverTimeCommand = 0x22;
constexpr std::uint8_t kBattleEventCommand = 0x23;
constexpr std::uint8_t kTimersRanOutCommand = 0x29;
constexpr std::uint8_t kPartyEscapeCommand = 0x2A;
constexpr std::uint8_t kSeizeDrainCommand = 0x2D;
// Damage over time's attack byte: the status bit that set it off (:15000-15015) — Poison in the first
// status byte, Regen in the third, Phantasm or Seize in the second's or the seized byte's.
constexpr std::uint8_t kPoisonBit = 0x04;
constexpr std::uint8_t kRegenBit = 0x02;
constexpr std::uint8_t kSapBit = 0x40;
// Timers run out: the targets' low byte names which (_c25b06 :14933-14963).
constexpr std::uint8_t kStopRanOutBit = 0x01;
constexpr std::uint8_t kReflectRanOutBit = 0x02;
constexpr std::uint8_t kFreezeRanOutBit = 0x04;
constexpr std::uint8_t kSleepRanOutBit = 0x08;

// One action as its slot holds it, with which of its attack's and targets' bytes the action determines.
struct SlotContents {
    std::uint8_t command = 0;
    std::uint8_t attack = 0;
    std::uint16_t targets = 0;
    bool attackKnown = true;
    bool targetsKnown = true;
    bool targetsHighKnown = true;
    std::uint8_t mpCost = 0;
};

std::uint8_t damageOverTimeBit(battle::DamageOverTimeKind kind) {
    switch (kind) {
        case battle::DamageOverTimeKind::Poison: return kPoisonBit;
        case battle::DamageOverTimeKind::Regen: return kRegenBit;
        case battle::DamageOverTimeKind::Sap: return kSapBit;
    }
    return 0;
}

// `counterattack`: the action is in a battler's counterattacks.
SlotContents contentsOf(const battle::Action& action, bool counterattack) {
    if (const auto* command = std::get_if<battle::CommandAction>(&action)) {
        // A Revert among the counterattacks is the one a morph running out makes (CreateRetalAction
        // :2829), with $B8 as the step left it; a chosen Revert is a turn action (:12641).
        const bool timersRevert = counterattack && command->command == BattleCommandId::REVERT;
        return SlotContents{.command = static_cast<std::uint8_t>(command->command),
                            .attack = static_cast<std::uint8_t>(command->attack),
                            .targets = maskOf(command->targets),
                            .targetsKnown = !timersRevert,
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
    // An AI's turn takes its targets from $B8, cleared for a turn (:489-490) and holding the attacker
    // answered for a counterattack (CheckRetal :12550-12563); its attack byte is whatever $3A7B held.
    if (const auto* turn = std::get_if<battle::AiTurn>(&action)) {
        return SlotContents{.command = kAiTurnCommand, .targets = maskOf(turn->answering), .attackKnown = false};
    }
    if (const auto* hurt = std::get_if<battle::DamageOverTime>(&action)) {
        return SlotContents{.command = kDamageOverTimeCommand, .attack = damageOverTimeBit(hurt->kind),
                            .targetsKnown = false};
    }
    if (const auto* drain = std::get_if<battle::SeizeDrain>(&action)) {
        return SlotContents{.command = kSeizeDrainCommand, .attack = 0, .targets = maskOf(battle::BattlerSet::of(drain->seized))};
    }
    if (const auto* timers = std::get_if<battle::StatusTimersExpired>(&action)) {
        return SlotContents{.command = kTimersRanOutCommand,
                            .attack = static_cast<std::uint8_t>(slotIndex(timers->battler)),
                            .targets = static_cast<std::uint8_t>((timers->stop ? kStopRanOutBit : 0) |
                                                                 (timers->reflect ? kReflectRanOutBit : 0) |
                                                                 (timers->freeze ? kFreezeRanOutBit : 0) |
                                                                 (timers->sleep ? kSleepRanOutBit : 0)),
                            .targetsHighKnown = false};
    }
    if (std::holds_alternative<battle::PartyEscape>(action)) {
        return SlotContents{.command = kPartyEscapeCommand, .attackKnown = false, .targetsKnown = false};
    }
    if (const auto* event = std::get_if<battle::BattleEventAction>(&action)) {
        return SlotContents{.command = kBattleEventCommand, .attack = event->event, .targetsKnown = false};
    }
    // The actions only a monster's script queues: a formation change $20, dialog $21, an animation $2B, a
    // variable $2E, a switch $2F and an effect $30, each the step's own operands (AICmd_f2-AICmd_fb
    // :4476-4549), compared by the bytes their command reads.
    if (const std::optional<ActionCells> cells = listedActionCells(action)) {
        return SlotContents{.command = cells->command,
                            .attack = cells->attack.value_or(0),
                            .targets = static_cast<std::uint16_t>(cells->targetsLow.value_or(0) |
                                                                  cells->targetsHigh.value_or(0) << 8),
                            .attackKnown = cells->attack.has_value(),
                            .targetsKnown = cells->targetsLow.has_value(),
                            .targetsHighKnown = cells->targetsHigh.has_value(),
                            .mpCost = cells->mpCost};
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
        bool counterattacks = false;
    };
    std::vector<ListToLay> lists{{kFirstImmediateAction, &state.immediateActions}};
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        lists.push_back({kFirstAction + at, &state.battlers[i].actions});
        lists.push_back({kFirstAction + at + 1, &state.battlers[i].counterActions, true});
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
            const SlotContents contents = contentsOf((*lists[l].actions)[k], lists[l].counterattacks);
            const std::uint32_t word = static_cast<std::uint32_t>(slot) * 2;
            layByteAsTheCartridgeHoldsIt(kActionCommand + word, contents.command);
            if (contents.attackKnown) {
                layByteAsTheCartridgeHoldsIt(kActionCommand + word + 1, contents.attack);
            }
            if (contents.targetsKnown) {
                layByteAsTheCartridgeHoldsIt(kActionTargets + word, static_cast<std::uint8_t>(contents.targets & 0xFF));
                if (contents.targetsHighKnown) {
                    layByteAsTheCartridgeHoldsIt(kActionTargets + word + 1, static_cast<std::uint8_t>(contents.targets >> 8));
                } else {
                    // The high byte the maker's byte-wide store left as it was (see the file's head).
                    leaveOutAsCarriedToNothing(kActionTargets + word + 1, 1);
                }
            } else {
                // Targets the maker stored as $B8 held them: the action's run loads them into $B8
                // (InitPlayerAction :392-393), and neither its command nor anything ExecCmd runs after it
                // reads $B8 before setting it (see the file's head).
                leaveOutAsCarriedToNothing(kActionTargets + word, 2);
            }
            layByteAsTheCartridgeHoldsIt(kActionMp + word, contents.mpCost);
        }
    }
    layAsTheCartridgeHoldsIt(kActionNext, links);
}

}  // namespace ostinato::trace
