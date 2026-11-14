// The tables of the whole-state comparison: the cells and bits a BattleState field determines, the
// cells it does not cover and why, the cells left as the machine has them, and a guest's write or
// unwritten read as the oracle records it.
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace ostinato::trace {

// One write a routine made to a watched cell, as the watch saw it.
struct WatchedWrite {
    std::uint32_t at = 0;  // the cell's work-RAM address, $7E:xxxx
    std::uint8_t value = 0;
};

// A cell a routine read before anything wrote it, and the byte the read found.
struct UnwrittenRead {
    std::uint32_t at = 0;  // the cell's work-RAM address, $7E:xxxx
    std::uint8_t value = 0;
};

// Cells, or bits of cells, whose bits a BattleState field does not determine in full: `count` cells
// from `at`, `stride` apart, compared by the bits in `meaning` only. Every other bit of them is the
// start's fill or a routine's leavings, for the reason in `why`.
struct CellMeaning {
    std::uint32_t at = 0;
    std::uint32_t count = 1;
    std::uint32_t stride = 1;
    std::uint8_t meaning = 0xFF;
    const char* why = "";
};

// Cells, or bits of cells, the battle program writes that the whole-state comparison does not cover:
// `count` cells from `at`, `stride` apart, the bits in `bits`. A provisional entry is one only a battle's
// start and its end have shown; a whole battle is still to prove it. Each carries its reason and the
// lines it comes from.
struct CellsNotCompared {
    std::uint32_t at = 0;
    std::uint32_t count = 1;
    std::uint32_t stride = 1;
    std::uint8_t bits = 0xFF;
    bool provisional = true;
    const char* why = "";
};

// The two tables, kCellMeanings and kCellsNotCompared.
[[nodiscard]] std::span<const CellMeaning> cellMeanings();
[[nodiscard]] std::span<const CellsNotCompared> cellsNotCompared();
// The bits of `cell` a BattleState field determines: every bit, unless kCellMeanings narrows it.
[[nodiscard]] std::uint8_t meaningBits(std::uint32_t cell);
// The bits of `cell` on kCellsNotCompared: none, unless a row lists it.
[[nodiscard]] std::uint8_t notComparedBits(std::uint32_t cell);

// Cells the attack frame reads that no scenario writes, left as the machine has them: `count` cells
// from `at`, `stride` apart. Each is a cell whose value reaches nothing the game shows or keeps.
struct CellsLeftAsTheMachineHasThem {
    std::uint32_t at = 0;
    std::uint32_t count = 1;
    std::uint32_t stride = 1;
    const char* why = "";
};
inline constexpr std::array<CellsLeftAsTheMachineHasThem, 15> kCellsLeftAsTheMachineHasThem{{
    {.at = 0x7E01FE, .count = 2, .why = "the return landing the engine plants for the call, read by its last rts"},
    {.at = 0x7E01EE, .count = 8,
     .why = "the return landing the engine plants for a graphics command the oracle runs inside a call, read by "
            "the command's rtl"},
    {.at = 0x7E00F9,
     .why = "the byte above the experience a level needs, rotated with it by a 16-bit rol and never compared; the "
            "win zeroes it before it counts a drop there"},
    {.at = 0x7E2F41,
     .why = "the byte after the win's gold total, which the total's 16-bit increment of its third byte reads and puts "
            "back; six monsters' gold never carries out of the third byte"},
    {.at = 0x7E2E74,
     .why = "an empty item's targeting, which the item record leaves as it was and the list copies; a place whose "
            "item is empty is never read past its item"},
    {.at = 0x7E00EB,
     .why = "the top byte of the multiply's scratch, which every shift of it (3 to 6 steps) moves out unread"},
    {.at = 0x7E00F3,
     .why = "the byte after the damage-healed flag, loaded with it where only the flag's lowest bit is tested"},
    {.at = 0x7E2F34,
     .why = "the byte after the four equipment flags, which a 16-bit increment of the last one reads and puts back"},
    {.at = 0x7E2F44, .count = 2,
     .why = "the monsters drawn see-through, read and put back by Vanish's setting and clearing"},
    {.at = 0x7E32DF, .count = 10, .stride = 2,
     .why = "the byte before each last-attacker mark, loaded with it where only the mark's sign is tested"},
    {.at = 0x7E3A29, .count = 3,
     .why = "the last three bytes of a script command, which the commands queued with them do not read"},
    {.at = 0x7E3A81, .why = "the byte before the Golem's block, loaded with it where only the next byte's sign is tested"},
    {.at = 0x7E00B9,
     .why = "the byte above the start's party-member test, read by a sixteen-bit bit test against a party member's "
            "bit whose high byte is zero (LoadCharProp battle_main.asm:6622-6623)"},
    {.at = 0x7E11DF,
     .why = "the field's equipment field effects, which the start's equipment sums OR into and never clear "
            "(CalcEquipEffect :2487-2488; UpdateEquip clears $11A0-$11DE only, :2302-2304); the battle reads it "
            "nowhere else"},
    {.at = 0x7E1A69,
     .why = "the high byte of the last inventory slot's quantity, loaded sixteen bits wide (InitInventory "
            ":13935-13937) and overwritten with $FF before anything reads it (LoadItemProp :13989-13990)"},
}};

// Whether `cell` is one of kCellsLeftAsTheMachineHasThem.
[[nodiscard]] bool leftAsTheMachineHasIt(std::uint32_t cell);

}  // namespace ostinato::trace
