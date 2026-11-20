// The whole-state comparison's tables and their lookups.

#include "scenarios/battle/battle_cells.h"

#include <algorithm>
#include <cstdint>
#include <span>

#include "cells/battle/state.h"
#include "cells/damage/per_target.h"

namespace ostinato::trace {

using namespace cells;

bool leftAsTheMachineHasIt(std::uint32_t cell) {
    return std::any_of(kCellsLeftAsTheMachineHasThem.begin(), kCellsLeftAsTheMachineHasThem.end(),
                       [cell](const CellsLeftAsTheMachineHasThem& row) {
                           return cell >= row.at && (cell - row.at) % row.stride == 0 &&
                                  (cell - row.at) / row.stride < row.count;
                       });
}

bool leftAsTheMachineHasItInAStep(std::uint32_t cell) {
    return std::any_of(kCellsLeftAsTheMachineHasThem.begin(), kCellsLeftAsTheMachineHasThem.end(),
                       [cell](const CellsLeftAsTheMachineHasThem& row) {
                           return !row.startOnly && cell >= row.at && (cell - row.at) % row.stride == 0 &&
                                  (cell - row.at) / row.stride < row.count;
                       });
}
namespace {

// The bits of $3205,x the battle reads and writes: 7, 2 and 1 (:264-266, :1129-1131, :1626-1629,
// :1797-1799, :2789-2790, :5758-5759, :9024-9028, :9743-9745, :12217-12219). The rest hold the fill.
constexpr std::uint8_t kJumpAnchorNearFatalBits = 0x86;
// A word naming battlers ($3018,x's masks, TargetMaskTbl): the low byte's bits 0-3 are the party
// slots, the high byte's bits 0-5 the monster slots. The other bits name no battler.
constexpr std::uint8_t kPartySlotBits = 0x0F;
constexpr std::uint8_t kMonsterSlotBits = 0x3F;

// $B1's bit 4: an item kept for one command (InitTarget_01 :6529-6530, cleared by ExecCmd's aim :3963-3964).
constexpr std::uint8_t kItemKeptForTheCommandBit = 0x10;
// $B0's bits a field determines: 7, 6, 5 and 2.
constexpr std::uint8_t kBattleFlags0Bits = 0xE4;
// A verdict rotated into bit 7 of a menu row's or a spell entry's second byte.
constexpr std::uint8_t kVerdictBit = 0x80;
// An item record's fifth byte: one bit per party slot that cannot hold the item (LoadItemProp
// :14021-14033), $FF for an item no hand check is made for (:13989-13990, :14016-14020). Its readers test
// one party slot's bit (btlgfx_main.asm:10957-10958 with _c14b73 :10710, :20956-20960 with :20985-20986).
constexpr std::uint8_t kHandBits = 0x0F;
// The formation's third byte as the start keeps it ($2F49), whose bit 7 turns the scripted setup in
// force on (LoadBattleProp :7973-7977, InitParty :7786-7792).
constexpr std::uint32_t kFormationScriptFlags = kWorkRam + 0x2F49;
constexpr std::uint8_t kScriptedSetupOnBit = 0x80;
// An item record's flags: bits 3-7 are what LoadItemProp sets (:13987-14019); bits 0-2 it never sets,
// and reach an inventory slot only from a place of the items obtained no record filled, which the merge
// copies whole (_c14445 btlgfx_main.asm:9469-9479).
constexpr std::uint8_t kItemFlagBits = 0xF8;
// The inventory's records and the hands' (InitInventory :13911-13956), five bytes each.
constexpr std::uint32_t kInventoryFlagBytes = kWorkRam + 0x2687;
constexpr std::uint32_t kInventoryFifthBytes = kWorkRam + 0x268A;
constexpr std::uint32_t kHandFifthBytes = kWorkRam + 0x2B8A;
constexpr std::uint32_t kInventoryRecordCount = 256;
constexpr std::uint32_t kHandRecordCount = 8;
constexpr std::uint32_t kItemRecordStride = 5;

// Cells whose bits a BattleState field determines only in part.
constexpr CellMeaning kCellMeanings[] = {
    {.at = kWorkRam + 0x00B0, .meaning = kBattleFlags0Bits, .why = "$B0 bits 4, 3, 1 and 0 are one command's or never set"},
    {.at = kWorkRam + 0x00B1, .meaning = static_cast<std::uint8_t>(~kItemKeptForTheCommandBit),
     .why = "$B1 bit 4 is one command's own"},
    {.at = kWorkRam + 0x202F, .count = 16, .stride = 3, .meaning = kVerdictBit,
     .why = "below the gray bit, the verdicts of the refreshes before"},
    {.at = kWorkRam + 0x208F, .count = 316, .stride = 4, .meaning = kVerdictBit,
     .why = "below the not-castable bit, the verdicts of the refreshes before"},
    {.at = kFormationScriptFlags, .meaning = static_cast<std::uint8_t>(~kScriptedSetupOnBit),
     .why = "$2F49 bit 7, the scripted setup in force, the start's for its own opening"},
    {.at = kFormationScriptFlags + 1, .meaning = 0, .why = "$2F4A, the scripted setup in force, with $2F49 bit 7"},
    {.at = kAttackRecordFlags, .meaning = kPhysicalHitBit,
     .why = "the attack's first flags but bit 0, the attack's own, loaded with its record"},
    {.at = kFieldTimerFlags, .meaning = kCountdownRanOutBit,
     .why = "the field's timer flags: bit 5, a story countdown run out, is the battle's to read"},
    {.at = kAvailableCharacters, .meaning = kShadowAvailableBit,
     .why = "the field's available characters: bit 3, Shadow available, is the battle's to read and clear"},
    {.at = kInventoryFlagBytes, .count = kInventoryRecordCount, .stride = kItemRecordStride, .meaning = kItemFlagBits,
     .why = "the flags' low three bits, which no record sets: a slot holds them only from a merged place no record "
            "filled, and what the battle menus read of them is theirs"},
    {.at = kInventoryFifthBytes, .count = kInventoryRecordCount, .stride = kItemRecordStride, .meaning = kHandBits,
     .why = "above the party slots' bits, LoadItemProp's leavings, which no reader tests"},
    {.at = kHandFifthBytes, .count = kHandRecordCount, .stride = kItemRecordStride, .meaning = kHandBits,
     .why = "above the party slots' bits, LoadItemProp's leavings, which no reader tests"},
    {.at = kWorkRam + 0x3205, .count = 10, .stride = 2, .meaning = kJumpAnchorNearFatalBits,
     .why = "bits 0 and 3-6 of $3205,x are read by nothing"},
    {.at = kWorkRam + 0x33FC, .meaning = kPartySlotBits, .why = "the bits of a battler mask that name no battler"},
    {.at = kWorkRam + 0x33FD, .meaning = kMonsterSlotBits, .why = "the bits of a battler mask that name no battler"},
    {.at = kWorkRam + 0x33FE, .meaning = kPartySlotBits, .why = "the bits of a battler mask that name no battler"},
    {.at = kWorkRam + 0x33FF, .meaning = kMonsterSlotBits, .why = "the bits of a battler mask that name no battler"},
    {.at = kWorkRam + 0x3408, .meaning = kPartySlotBits, .why = "the bits of a battler mask that name no battler"},
    {.at = kWorkRam + 0x3409, .meaning = kMonsterSlotBits, .why = "the bits of a battler mask that name no battler"},
};

template <typename Row>
bool holds(const Row& row, std::uint32_t cell) {
    return cell >= row.at && (cell - row.at) % row.stride == 0 && (cell - row.at) / row.stride < row.count;
}
}  // namespace

std::span<const CellMeaning> cellMeanings() { return kCellMeanings; }

std::uint8_t meaningBits(std::uint32_t cell) {
    std::uint8_t bits = 0xFF;
    for (const CellMeaning& row : kCellMeanings) {
        if (holds(row, cell)) {
            bits &= row.meaning;
        }
    }
    return bits;
}

}  // namespace ostinato::trace
