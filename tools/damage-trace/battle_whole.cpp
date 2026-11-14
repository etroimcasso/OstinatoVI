// The battle whole on the oracle: a BattleState laid into the cartridge's cells and compared with
// them by the bits that have meaning, and the gates that keep the comparison whole — every cell the
// battle program writes compared or listed, and every listed cell it reads written first.

#include <cstdint>
#include <cstdio>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "damage_oracle.h"

#include "battle/battle_hooks.h"
#include "battle/command_executor.h"

namespace ostinato::trace {

namespace {

constexpr std::uint32_t kWorkRam = DamageOracle::kWorkRam;

// The bits of $3205,x the battle reads and writes: 7, 2 and 1 (:264-266, :1129-1131, :1626-1629,
// :1797-1799, :2789-2790, :5758-5759, :9024-9028, :9743-9745, :12217-12219). The rest hold the fill.
constexpr std::uint8_t kJumpAnchorNearFatalBits = 0x86;
// A word naming battlers ($3018,x's masks, TargetMaskTbl): the low byte's bits 0-3 are the party
// slots, the high byte's bits 0-5 the monster slots. The other bits name no battler.
constexpr std::uint8_t kPartySlotBits = 0x0F;
constexpr std::uint8_t kMonsterSlotBits = 0x3F;

// $B1's bit 4: an item kept for one command (InitTarget_01 :6529-6530, cleared by ExecCmd's aim :3963-3964).
constexpr std::uint8_t kItemKeptForTheCommandBit = 0x10;

// Cells whose bits a BattleState field determines only in part.
constexpr CellMeaning kCellMeanings[] = {
    {.at = kWorkRam + 0x00B1, .meaning = static_cast<std::uint8_t>(~kItemKeptForTheCommandBit),
     .why = "$B1 bit 4 is one command's own"},
    {.at = kWorkRam + 0x3205, .count = 10, .stride = 2, .meaning = kJumpAnchorNearFatalBits,
     .why = "bits 0 and 3-6 of $3205,x are read by nothing"},
    {.at = kWorkRam + 0x3408, .meaning = kPartySlotBits, .why = "the bits of a battler mask that name no battler"},
    {.at = kWorkRam + 0x3409, .meaning = kMonsterSlotBits, .why = "the bits of a battler mask that name no battler"},
};

// Cells, or bits of cells, the battle program writes that the comparison does not cover.
constexpr CellsNotCompared kCellsNotCompared[] = {
    {.at = kWorkRam + 0x00B1, .bits = kItemKeptForTheCommandBit,
     .why = "$B1 bit 4: an item kept for one command, set by InitTarget_01 (:6529-6530) and cleared as the next "
            "command is aimed (:3963-3964)"},
    {.at = kWorkRam + 0x3205, .count = 10, .stride = 2, .bits = static_cast<std::uint8_t>(~kJumpAnchorNearFatalBits),
     .why = "bits 0 and 3-6 of $3205,x: the fill's, read by nothing (:264-266, :1129-1131, :1626-1629, :1797-1799, "
            ":2789-2790, :5758-5759, :9024-9028, :9743-9745, :12217-12219 read and write bits 7, 2 and 1 only)"},
    {.at = kWorkRam + 0x3408, .bits = static_cast<std::uint8_t>(~kPartySlotBits),
     .why = "the bits of the counted battlers' mask that name no battler (TargetMaskTbl)"},
    {.at = kWorkRam + 0x3409, .bits = static_cast<std::uint8_t>(~kMonsterSlotBits),
     .why = "the bits of the counted battlers' mask that name no battler (TargetMaskTbl)"},
    // An empty party slot's record offset is $FFFF, left by the start's fill: InitInventory reads its
    // shield and weapon through it, sixteen bits wide, from bank $7F (:13919-13920, :13928-13929).
    {.at = 0x7F161E, .count = 3,
     .why = "an empty party slot's hands, read through its record offset $FFFF from bank $7F "
            "(InitInventory :13919-13920, :13928-13929); the slot's hand records are not compared"},
};

template <typename Row>
bool holds(const Row& row, std::uint32_t cell) {
    return cell >= row.at && (cell - row.at) % row.stride == 0 && (cell - row.at) / row.stride < row.count;
}

}  // namespace

std::span<const CellMeaning> cellMeanings() { return kCellMeanings; }
std::span<const CellsNotCompared> cellsNotCompared() { return kCellsNotCompared; }

std::uint8_t meaningBits(std::uint32_t cell) {
    std::uint8_t bits = 0xFF;
    for (const CellMeaning& row : kCellMeanings) {
        if (holds(row, cell)) {
            bits &= row.meaning;
        }
    }
    return bits;
}

std::uint8_t notComparedBits(std::uint32_t cell) {
    std::uint8_t bits = 0;
    for (const CellsNotCompared& row : kCellsNotCompared) {
        if (holds(row, cell)) {
            bits |= row.bits;
        }
    }
    return bits;
}

battle::BattleState portStart(const StartScenario& scenario, const TextCorpus& text, const MonsterSprites& sprites) {
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, text, sprites};
    return battle::startBattle(scenario.start, scenario.cells.party, scenario.switches, text, sprites, executor);
}

void DamageOracle::writeBattleState(const battle::BattleState& state, const PartyCells& boosts) {
    writeAttackCells(DamageScenario{.state = state});
    writeAiCells(AiScenario{.state = state});
    writeAiScriptCells(state);
    PartyCells cells = boosts;
    cells.party = state.party;
    writeEndingScenario(EndingScenario{.cells = cells, .state = state});
    writeStateAsTheCartridgeHoldsIt(state);
}

DamageOracle::LaidState DamageOracle::layState(const battle::BattleState& state, const PartyCells& boosts) {
    LaidState laid;
    capture_ = &laid;
    writeBattleState(state, boosts);
    capture_ = nullptr;
    return laid;
}

std::string DamageOracle::compareBattleState(const battle::BattleState& state, const PartyCells& boosts,
                                             const BattleCells& cartridge) {
    const LaidState laid = layState(state, boosts);
    std::string lines;
    char text[112];
    for (const auto& [cell, bits] : laid.disagreements) {
        if ((bits & meaningBits(cell)) != 0) {
            std::snprintf(text, sizeof text, "$7E:%04X two writers lay it differently (bits %02X)\n", cell & 0xFFFF,
                          bits & meaningBits(cell));
            lines += text;
        }
    }
    for (const auto& [cell, port] : laid.cells) {
        const std::uint32_t offset = cell - kWorkRam;
        if (cell < kWorkRam || offset >= cartridge.size()) {
            continue;
        }
        const std::uint8_t meaning = meaningBits(cell);
        if (((cartridge[offset] ^ port) & meaning) != 0) {
            std::snprintf(text, sizeof text, "$7E:%04X port %02X cartridge %02X (bits %02X)\n", offset, port,
                          cartridge[offset], meaning);
            lines += text;
        }
    }
    return lines;
}

std::string DamageOracle::uncoveredWrites(const battle::BattleState& state, const PartyCells& boosts,
                                          const std::set<std::uint32_t>& written) {
    const LaidState laid = layState(state, boosts);
    std::string lines;
    char text[80];
    for (const std::uint32_t cell : written) {
        const std::uint8_t compared = laid.cells.contains(cell) ? meaningBits(cell) : 0;
        const auto left = static_cast<std::uint8_t>(~(compared | notComparedBits(cell)));
        if (left != 0 && !laid.leftOut.contains(cell)) {
            std::snprintf(text, sizeof text, "$7E:%04X written, bits %02X not compared\n", cell & 0xFFFF, left);
            lines += text;
        }
    }
    return lines;
}

std::string DamageOracle::carriedReads(const battle::BattleState& state, const PartyCells& boosts,
                                       const std::vector<UnwrittenRead>& reads) {
    const LaidState laid = layState(state, boosts);
    std::set<std::uint32_t> named;
    std::string lines;
    char text[80];
    for (const UnwrittenRead& read : reads) {
        const bool held = laid.cells.contains(read.at) && meaningBits(read.at) == 0xFF;
        if (held || named.contains(read.at)) {
            continue;
        }
        named.insert(read.at);
        std::snprintf(text, sizeof text, "$7E:%04X read after the comparison, %02X, not compared\n", read.at & 0xFFFF,
                      read.value);
        lines += text;
    }
    return lines;
}

}  // namespace ostinato::trace
