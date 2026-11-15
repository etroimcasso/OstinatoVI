// The whole-state comparison's tables and their lookups.

#include "scenarios/battle/battle_cells.h"

#include <algorithm>
#include <cstdint>
#include <span>

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

// Cells whose bits a BattleState field determines only in part.
constexpr CellMeaning kCellMeanings[] = {
    {.at = kWorkRam + 0x00B0, .meaning = kBattleFlags0Bits, .why = "$B0 bits 4, 3, 1 and 0 are one command's or never set"},
    {.at = kWorkRam + 0x00B1, .meaning = static_cast<std::uint8_t>(~kItemKeptForTheCommandBit),
     .why = "$B1 bit 4 is one command's own"},
    {.at = kWorkRam + 0x202F, .count = 16, .stride = 3, .meaning = kVerdictBit,
     .why = "below the gray bit, the verdicts of the refreshes before"},
    {.at = kWorkRam + 0x208F, .count = 316, .stride = 4, .meaning = kVerdictBit,
     .why = "below the not-castable bit, the verdicts of the refreshes before"},
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
