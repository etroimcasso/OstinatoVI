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
// Whether `cell` is the stack's or the interrupts' jump code ($7E:1500-15FF): a return reads back what
// its own call pushed, so a read there is never carried from a comparison point.
[[nodiscard]] bool onTheStack(std::uint32_t cell);

// Cells a routine reads that no scenario writes, left as the machine has them: `count` cells from
// `at`, `stride` apart. Each is a cell whose value reaches nothing the battle holds — read and put back,
// shifted out, or overwritten before anything reads it. The read-set gate passes a read of one before
// anything wrote it, and the carried-read gate a read of one after the comparison point. A row marked
// `startOnly` holds for a battle's start alone: a whole battle's steps do not pass a read of it.
struct CellsLeftAsTheMachineHasThem {
    std::uint32_t at = 0;
    std::uint32_t count = 1;
    std::uint32_t stride = 1;
    const char* why = "";
    bool startOnly = false;
};
inline constexpr std::array<CellsLeftAsTheMachineHasThem, 23> kCellsLeftAsTheMachineHasThem{{
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
            "bit whose high byte is zero (LoadCharProp battle_main.asm:6622-6623), called once, from InitBattle "
            "(:6217); a step's reads of it are held by CreateAction's copy alone (carriedReads())",
     .startOnly = true},
    {.at = 0x7E00B7,
     .why = "the fourth byte of the script command being made, which ExecCmd does not reset (:3096-3109): set by the "
            "attacks that have one — the hand's item for a Fight (:6958-6960), a special attack's animation "
            "(:8193-8194), the monster sketched (:9538-9541), the Atma Weapon's length (:2275), a dance's (:3712, "
            ":3724), the dice (:10685-10694) — and read back by the battle program only inside the attack that set "
            "it (:9188 after :6960; :10693 and :10719 after :10685); every other attack's script command carries "
            "the last one's into the script's block (_c235d4 :8566-8569), the graphics program's"},
    {.at = 0x7E11DF,
     .why = "the field's equipment field effects, which the start's equipment sums OR into and never clear "
            "(CalcEquipEffect :2487-2488; UpdateEquip clears $11A0-$11DE only, :2302-2304); the battle reads it "
            "nowhere else"},
    {.at = 0x7E1A69,
     .why = "the high byte of the last inventory slot's quantity, loaded sixteen bits wide (InitInventory "
            ":13935-13937) and overwritten with $FF before anything reads it (LoadItemProp :13989-13990)"},
    {.at = 0x7E3720, .count = 256,
     .why = "the advance-wait line's places outside the line: the search that takes a battler out of the line "
            "looks once at the place its start points to even when the line is empty (RemoveAllActions "
            ":1583-1591), and a match stores $FF in a place the line does not hold; the places in the line are "
            "laid from its start and compared"},
    {.at = 0x7E1DDD, .count = 64,
     .why = "the field's record of the battles Gau can find on the Veldt, which the battle's end reads only to set "
            "the formation's bit in it and store it back (UpdateSRAM :12163-12178): the bit is "
            "BattleResult::veldtFormation, and the field keeps the rest"},
    {.at = 0x7E3A7B,
     .why = "the attack byte of an action whose routine sets only its command (an AI's turn :504-506, its "
            "counterattack :12561-12563, the party's escape :15093-15094): CreateAction copies $3A7A-$3A7B as a word "
            "into the action's slot (:12958-12959), whose attack byte the comparison leaves out for those actions"},
    {.at = 0x7E00B2,
     .why = "the high byte of the counterattack flag's load: CreateDefaultAction reads $B1 sixteen bits wide when "
            "an AI command calls it with the accumulator wide (AICmd_f0 :4432-4443, AICmd_f9 :4513-4518, AICmd_fa "
            ":4544-4548) and branches on bit 0 alone (:12901-12903); the byte lands in the accumulator's high byte, "
            "which every instruction up to GetMPCost's tdc leaves alone (:12932-12974, the index registers narrow "
            "from :4369), and in the zero flag the action's php saves, which NextAICmd's cmp sets again before "
            "anything tests it (:4345)"},
    {.at = 0x7E3FE2, .count = 2,
     .why = "past the first status bytes ($3EE4,x) for an X of $FE or $FF: an interrupting action runs with X as "
            "the loop left it (BattleLoop :78-80, _c22163 :5704-5711) — $FE after the menus' update steps down the "
            "four party members two at a time (_c200cc :162-171), $FF otherwise — and InitPlayerAction's Imp test "
            "reads $3EE4,x "
            "(:410-412); every interrupting action's command is $23 or above (:1224, :1260, :1599, :1636, "
            ":3669, :6160, :9717, :14962, :15061, :15093, :16427), so the test returns either way (:413-415) "
            "and the pla there sets the flags again (:440)"},
    {.at = 0x7E3ECB,
     .why = "the high byte of the count of the monsters' names, read and stored back by the count's sixteen-bit "
            "step (UpdateMonsterGfxBuf :15221, :15227), which never carries into it; the flags the step sets are "
            "set again by the next instruction's cmp (:15228)"},
    {.at = 0x7E3421, .count = 128, .stride = 2,
     .why = "an action slot's attack byte where the routine that made the action set only its command: the action's "
            "run loads it into $3A7D and $B6 (InitPlayerAction :394-396), which neither an AI's turn "
            "(ExecMonsterAction :447-463) nor the party's escape reads before the next command sets them"},
}};

// Whether `cell` is one of kCellsLeftAsTheMachineHasThem; in a whole battle's step, the rows not
// marked startOnly.
[[nodiscard]] bool leftAsTheMachineHasIt(std::uint32_t cell);
[[nodiscard]] bool leftAsTheMachineHasItInAStep(std::uint32_t cell);

}  // namespace ostinato::trace
