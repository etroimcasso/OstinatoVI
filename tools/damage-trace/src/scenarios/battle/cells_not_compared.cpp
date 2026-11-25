// The cells, and bits of cells, the battle program writes that the whole-state comparison does not
// cover, each with its reason and the lines it comes from; and the stack, whose cells a return reads
// back from its own call.
//
// Line numbers are battle/battle_main.asm's, and btlgfx_main.asm's where marked. A provisional row is
// one only a battle's start and its end have shown; a whole battle is still to prove it.

#include <cstdint>
#include <span>

#include "cells/battle/state.h"
#include "cells/damage/per_target.h"
#include "scenarios/battle/battle_cells.h"

namespace ostinato::trace {

using namespace cells;

namespace {

// The battle program's own bits of cells compared in part.
constexpr std::uint8_t kItemKeptForTheCommandBit = 0x10;              // $B1 bit 4
constexpr std::uint8_t kJumpAnchorNearFatalBits = 0x86;               // $3205,x bits 7, 2 and 1
constexpr std::uint8_t kPartySlotBits = 0x0F;                         // a battler mask's low byte
constexpr std::uint8_t kMonsterSlotBits = 0x3F;                       // a battler mask's high byte
constexpr std::uint8_t kSwirlSuppressedBit = 0x10;                    // $B0 bit 4
constexpr std::uint8_t kNeverSetBits = 0x0B;                          // $B0 bits 3, 1 and 0
constexpr std::uint8_t kVerdictHistoryBits = 0x7F;                    // below a verdict rotated into bit 7
constexpr std::uint8_t kAboveTheHandBits = 0xF0;                      // an item record's fifth byte's top four
constexpr std::uint8_t kBelowTheShieldBit = 0x07;                     // an item record's flags' low three
constexpr std::uint8_t kScriptedSetupOnBit = 0x80;                    // $2F49 bit 7

constexpr CellsNotCompared kCellsNotCompared[] = {
    // --- direct page -----------------------------------------------------------------------------
    {.at = kWorkRam + 0x0010,
     .why = "the graphics command's parameter, the party slot a menu command names, set before each command that "
            "takes one (:166-167, :1502-1504, :2802-2804, :12067-12069) and read by the graphics program only"},
    {.at = kWorkRam + 0x00B0, .bits = kSwirlSuppressedBit,
     .why = "$B0 bit 4: one command's, set as a command is made (ExecCmd :3094-3095) and cleared by the commands "
            "that show their swirl (:3430, :3641, :3924, :13048, :13341, :14454)"},
    {.at = kWorkRam + 0x00B0, .bits = kNeverSetBits,
     .why = "$B0 bits 3, 1 and 0: cleared by the start (:6072) and set by no instruction"},
    {.at = kWorkRam + 0x00B1, .bits = kItemKeptForTheCommandBit,
     .why = "$B1 bit 4: an item kept for one command, set by InitTarget_01 (:6529-6530) and cleared as the next "
            "command is aimed (:3963-3964)"},
    {.at = kWorkRam + 0x00A0, .count = 16,
     .why = "the params block of the animation being worked out, one attack's: InitGfxParams lays it out for every "
            "attack (:14430-14466) and CopyGfxParamsToBuf hands it on (provisional)"},
    {.at = kWorkRam + 0x00B2, .count = 2,
     .why = "one command's flags: every command sets both to $FF before its body reads them (ExecCmd :3103-3105); "
            "the one read before, of $B2 as the high byte of an AI command's load of $B1, reaches nothing "
            "(kCellsLeftAsTheMachineHasThem)"},
    {.at = kWorkRam + 0x00B4, .count = 4,
     .why = "the script command an action is shown with and the command, attack and second value being made, the "
            "action's: ExecCmd sets the first (:3096-3097), InitPlayerAction the rest (:394-396) (provisional)"},
    {.at = kWorkRam + 0x00B8, .count = 2,
     .why = "the targets of the action being made: an action that runs loads its own (InitPlayerAction :392-393), a "
            "battler joining the line clears them (:489-490), a player's record sets them (:12662-12663), and "
            "CheckRetal clears them for each battler it considers (:12532-12533); the one read of them carried into "
            "a step is CreateAction's copy into the new action's slot (:12960-12961), held by the copy rule "
            "(carriedTargetsCompared())"},
    {.at = kWorkRam + 0x00BA, .count = 4,
     .why = "the aiming rules and the damage multipliers of the attack being made, the attack's: each attack sets its "
            "rules and multiplier, and every command clears the extra multiplier (ExecCmd :3098) (provisional)"},
    {.at = kWorkRam + 0x00E8, .count = 4,
     .why = "the arithmetic routines' operands and products (MultAB, the divide), set by each caller before the call "
            "(:7227 and on); $00EB is the top byte of the multiply's scratch, and LsrA's shift of $EA runs with the "
            "accumulator wide from CalcRatio (:2145-2154), so it writes $EB too (provisional)"},
    {.at = kWorkRam + 0x00EC, .count = 2,
     .why = "the arithmetic routines' products, above the multiply's scratch byte $00EB (provisional)"},
    {.at = kWorkRam + 0x00EE, .count = 18,
     .why = "the battle program's scratch: each routine sets the cells it reads (:793, :1028, :1819, :1860, :4219)"},
    // --- the attack's record and the field's shared cells ----------------------------------------
    {.at = kWorkRam + 0x11A0, .count = 2,
     .why = "the attack's record and the equipment sums of the last party member refreshed, at the start or in a "
            "loop step: every attack loads its record before reading it, and UpdateEquip clears $11A0-$11DE before "
            "it sums (:2302-2304)"},
    {.at = kAttackRecordFlags, .bits = static_cast<std::uint8_t>(~kPhysicalHitBit),
     .why = "the attack's first flags but bit 0: the attack's own, loaded with its record before it reads them"},
    {.at = kWorkRam + 0x11A3, .count = 12,
     .why = "the attack's record and the equipment sums of the last party member refreshed, at the start or in a "
            "loop step: every attack loads its record before reading it, and UpdateEquip clears $11A0-$11DE before "
            "it sums (:2302-2304)"},
    {.at = kWorkRam + 0x11B0, .count = 47,
     .why = "the attack's damage and the equipment sums of the last party member refreshed, at the start or in a "
            "loop step (UpdateEquip :2302-2304, CalcEquipEffect :2480 on); every attack writes its damage before it "
            "reads it"},
    {.at = kWorkRam + 0x11DF,
     .why = "the field's equipment effects, which the start's equipment sums OR into (CalcEquipEffect :2487-2488) "
            "and the battle program reads nowhere"},
    {.at = kAvailableCharacters, .bits = static_cast<std::uint8_t>(~kShadowAvailableBit),
     .why = "the field's available characters but bit 3: the battle program tests and clears bit 3 alone "
            "(CheckBattleEnd :11958, ShadowLeaves' trb :11991) and reads no other"},
    {.at = kFieldTimerFlags, .bits = static_cast<std::uint8_t>(~kCountdownRanOutBit),
     .why = "the field's timer flags but bit 5: the field's own timers', which CheckBattleEnd masks away "
            "(:11900-11901); the battle program reads the cell nowhere else"},
    // --- the graphics program's cells ------------------------------------------------------------
    {.at = kWorkRam + 0x200D, .count = 18,
     .why = "the monsters' names, their counts and the battlers shown, written for the graphics program "
            "(UpdateMonsterGfxBuf :15223-15230) and read by the battle program only as a formation changes, "
            "after it writes them (:13239)"},
    {.at = kWorkRam + 0x2022, .count = 12,
     .why = "the gauges, Morph gauges and condemned numbers the graphics program shows, written from the battlers "
            "(UpdateCharGfxBuf :15153-15159)"},
    {.at = kWorkRam + 0x2BCE, .count = 0xA0,
     .why = "the damage numerals each script shows, written for it (:16313)"},
    {.at = kWorkRam + 0x2C6E, .count = 0x200,
     .why = "the scripts' parameter blocks and command queue, written for each script before it is shown"},
    {.at = kWorkRam + 0x2E72, .count = 2,
     .why = "the item and flags of the record LoadItemProp makes, written for every record (:13987-13992) and "
            "copied out before anything else reads them (CopyItemProp :13968-13971, the prize :15305-15306, a drop "
            ":15519-15523, a hand :16178-16181); its targeting, $2E74, is compared"},
    {.at = kWorkRam + 0x2E75, .count = 3,
     .why = "the quantity and the fifth byte of the record LoadItemProp makes, each written for every record before "
            "it is copied (:13916, :13937, :15302, :15521, :16180; :13990, :14033), and the byte after it"},
    {.at = kWorkRam + 0x2E78, .count = 0x30,
     .why = "the party's HP, MP and statuses for the graphics program, written from the battlers (:15262)"},
    {.at = kWorkRam + 0x2EA8, .count = 4, .why = "the start's fill; named by no instruction of the battle program"},
    {.at = kWorkRam + 0x2EAE, .count = 0x81,
     .why = "the party's graphics, names and rows for the graphics program (:6745, :7603, :7887-7895) and the "
            "command window setting (:6108-6110); the battle program never reads them"},
    {.at = kWorkRam + 0x1869, .count = 0x200, .provisional = false,
     .why = "the field's inventory, items then quantities, which the battle writes back only at its end (UpdateSRAM "
            ":12127-12148) — what the game keeps, compared once the battle closes — and a final-battle stage's start "
            "reads back into the battle's own copy (InitInventory :13936-13944), which is compared"},
    {.at = kWorkRam + 0x1DC9, .count = 8, .provisional = false,
     .why = "the field's battle switches before its timer flags, which the battle writes back only at its end "
            "(UpdateSRAM :12160) — compared once the battle closes — and a final-battle stage's start reads back into "
            "the battle's own (InitBattle :6083-6087), which are compared"},
    {.at = kWorkRam + 0x1DD2, .count = 11, .provisional = false,
     .why = "the field's battle switches after its timer flags, as the row above"},
    {.at = kWorkRam + 0x2F35, .count = 12,
     .why = "the battle message's four variables, written for each message before it is shown (:15291 and on)"},
    {.at = kWorkRam + 0x2F44,
     .why = "the monsters drawn see-through, the graphics program's, kept with Vanish (:11393, :11405)"},
    {.at = kWorkRam + 0x2F47, .why = "the start's fill; named by no instruction of the battle program"},
    {.at = kWorkRam + 0x2F49, .bits = kScriptedSetupOnBit,
     .why = "$2F49 bit 7 and $2F4A, the scripted setup in force — the formation's own, or Gau's return "
            "(LoadBattleProp :7973-7977, InitParty :7786-7792): the battle program reads them only as it places "
            "the party (:7793-7795), and the opening places the setup's cast by them (btlgfx_main.asm:2111-2121, "
            ":41253-41255); the port's start places the cast and leaves the monsters out of reach from the same "
            "setup (setupInForce())"},
    {.at = kWorkRam + 0x2F4A, .why = "the scripted setup in force, with $2F49 bit 7 (above)"},
    {.at = kWorkRam + 0x2F52, .why = "the start's fill; named by no instruction of the battle program"},
    {.at = kWorkRam + 0x2F55, .count = 0xAB, .why = "the start's fill; named by no instruction of the battle program"},
    // --- the party's lists -----------------------------------------------------------------------
    {.at = kWorkRam + 0x2687, .count = 256, .stride = 5, .bits = kBelowTheShieldBit,
     .why = "the low three bits of each inventory record's flags, which LoadItemProp never sets (:13987-14019): a slot "
            "holds them only when the items obtained merge in a place no record filled, copied whole "
            "(btlgfx_main.asm:9469-9479), and whether the battle menus read them is theirs to say"},
    {.at = kWorkRam + 0x268A, .count = 256, .stride = 5, .bits = kAboveTheHandBits,
     .why = "the top four bits of each inventory record's fifth byte, $F for an item no hand check is made for "
            "(LoadItemProp :13989-13990, :14016-14020) and 0 otherwise (:14021-14033); the menus test one party "
            "slot's bit (btlgfx_main.asm:10957-10958, :20956-20960)"},
    {.at = kWorkRam + 0x2B8A, .count = 8, .stride = 5, .bits = kAboveTheHandBits,
     .why = "the top four bits of each hand record's fifth byte, as the inventory's (LoadItemProp :13989-14033)"},
    {.at = kWorkRam + 0x202F, .count = 16, .stride = 3, .bits = kVerdictHistoryBits,
     .why = "below each command's gray bit, the verdicts of the refreshes before, rotated down (UpdateCmdList "
            ":13610-13611)"},
    {.at = kWorkRam + 0x208F, .count = 316, .stride = 4, .bits = kVerdictHistoryBits,
     .why = "below each spell's not-castable bit, the verdicts of the refreshes before, rotated down "
            "(UpdateEnabledMagic :14387-14393)"},
    {.at = kWorkRam + 0x3082, .count = 2,
     .why = "the two cells after the party's lores, the start's fill; read as no spell by nothing (InitSpellList "
            "walks $3034-$3081, :14160-14198)"},
    // --- each battler ----------------------------------------------------------------------------
    {.at = kWorkRam + 0x3205, .count = 10, .stride = 2, .bits = static_cast<std::uint8_t>(~kJumpAnchorNearFatalBits),
     .why = "bits 0 and 3-6 of $3205,x: the fill's, read by nothing (:264-266, :1129-1131, :1626-1629, :1797-1799, "
            ":2789-2790, :5758-5759, :9024-9028, :9743-9745, :12217-12219 read and write bits 7, 2 and 1 only)"},
    {.at = kWorkRam + 0x327D, .count = 10, .stride = 2,
     .why = "the byte after each battler's last damager, the start's fill; named by no instruction"},
    {.at = kWorkRam + 0x33BC, .count = 20, .why = "the start's fill; named by no instruction of the battle program"},
    {.at = kWorkRam + 0x33FB,
     .why = "the high byte of Doom Gaze's slot, stored with it sixteen bits wide (:7381) and read as one byte "
            "(:12152)"},
    {.at = kWorkRam + 0x3408, .bits = static_cast<std::uint8_t>(~kPartySlotBits),
     .why = "the bits of the counted battlers' mask that name no battler (TargetMaskTbl)"},
    {.at = kWorkRam + 0x3409, .bits = static_cast<std::uint8_t>(~kMonsterSlotBits),
     .why = "the bits of the counted battlers' mask that name no battler (TargetMaskTbl)"},
    {.at = kWorkRam + 0x33FC, .bits = static_cast<std::uint8_t>(~kPartySlotBits),
     .why = "the bits of the counterattack-AI mask that name no battler"},
    {.at = kWorkRam + 0x33FD, .bits = static_cast<std::uint8_t>(~kMonsterSlotBits),
     .why = "the bits of the counterattack-AI mask that name no battler"},
    {.at = kWorkRam + 0x33FE, .bits = static_cast<std::uint8_t>(~kPartySlotBits),
     .why = "the bits of the not-attacked mask that name no battler"},
    {.at = kWorkRam + 0x33FF, .bits = static_cast<std::uint8_t>(~kMonsterSlotBits),
     .why = "the bits of the not-attacked mask that name no battler"},
    {.at = kWorkRam + 0x340B, .count = 5, .why = "the start's fill; named by no instruction of the battle program"},
    {.at = kWorkRam + 0x3410, .count = 6,
     .why = "one command's own: every command fills $3410-$341F with $FF before reading them (ExecCmd :3106-3109)"},
    {.at = kWorkRam + 0x3417, .count = 2,
     .why = "one command's own: every command fills $3410-$341F with $FF before reading them (ExecCmd :3106-3109)"},
    {.at = kWorkRam + 0x341C, .count = 4,
     .why = "one command's own: every command fills $3410-$341F with $FF before reading them (ExecCmd :3106-3109)"},
    {.at = kWorkRam + 0x3A28, .count = 14,
     .why = "the script command, the AI's command, the targets, the script's block and the numerals' count: the "
            "action in flight's"},
    {.at = kWorkRam + 0x3A47,
     .why = "the top byte of $3A46: bit 7 set by a scene change (Cmd_20 :13210-13214) for the next formation's "
            "load; the rest named by no instruction"},
    {.at = kWorkRam + 0x3A48, .count = 2, .why = "the targets an attack missed, the attack's"},
    {.at = kWorkRam + 0x3A4C,
     .why = "the cost the next attack takes, which every action sets (InitPlayerAction :408-409) and the attack "
            "clears (:8161-8166)"},
    {.at = kWorkRam + 0x3A4E, .count = 8,
     .why = "the backup targets and the targets hit in the back, the attack's; $3A50-$3A53 named by no "
            "instruction"},
    {.at = kWorkRam + 0x3A5A, .count = 10,
     .why = "the targets missed and Mind Blast's picks, the attack's; $3A5E-$3A63 named by no instruction"},
    {.at = kWorkRam + 0x3A70, .count = 3, .why = "the attacks left and the script queue's places, the action's"},
    {.at = kWorkRam + 0x3A7A, .count = 6, .why = "the command and attack being made, the action's (:617-619, :12674)"},
    {.at = kWorkRam + 0x3A81,
     .why = "the byte under the Golem's block, loaded with it sixteen bits wide where only the sign of the block "
            "is tested (:3072-3074)"},
    {.at = kWorkRam + 0x3A82, .count = 2,
     .why = "the Golem's and the Interceptor's blocks, set for each attack (:8020-8021)"},
    {.at = kWorkRam + 0x3A89, .why = "the weapon's random spellcast, one attack's"},
    {.at = kWorkRam + 0x3A8B, .why = "the high byte of the engulfed set: party slots only"},
    {.at = kWorkRam + 0x3A8E, .why = "the Dragon Horn, one command's (:3101, :3880)"},
    {.at = kWorkRam + 0x3A92, .why = "the start's zero fill; named by no instruction"},
    {.at = kWorkRam + 0x3A94, .why = "the start's zero fill; named by no instruction"},
    {.at = kWorkRam + 0x3A96, .why = "the debug window's kill-all, set only by it (:16420)"},
    {.at = kWorkRam + 0x3A98, .count = 2,
     .why = "a counterattack script running, one AI run's (:449, :12471, :12493)"},
    {.at = kWorkRam + 0x3A9C, .count = 4, .why = "the start's zero fill; named by no instruction"},
    {.at = kWorkRam + 0x3DAD, .count = 10, .stride = 2,
     .why = "the byte after each battler's AI variable, the start's zero fill; named by no instruction"},
    {.at = kWorkRam + 0x3E25, .count = 10, .stride = 2,
     .why = "the byte after each battler's poison multiplier, the start's zero fill; named by no instruction"},
    {.at = kWorkRam + 0x3E38, .count = 20, .why = "the start's zero fill; named by no instruction"},
    {.at = kWorkRam + 0x3E88, .count = 40, .why = "the start's zero fill; named by no instruction"},
    {.at = kWorkRam + 0x3EC9, .why = "the number of targets, the attack's"},
    {.at = kWorkRam + 0x3ECB,
     .why = "the high byte of the count of the monsters' names, stepped sixteen bits wide (UpdateMonsterGfxBuf "
            ":15221, :15227) and never carried into: the count is cleared first (:15193) and reaches four at most "
            "(:15232-15234)"},
    {.at = kWorkRam + 0x3ECC, .count = 8, .why = "the start's zero fill; named by no instruction"},
    {.at = kWorkRam + 0x3ED6, .count = 2, .why = "the start's $FF fill (:6326); named by no instruction"},
    {.at = kWorkRam + 0x3EE3, .why = "the start's $FF fill (:6326); named by no instruction"},
    {.at = kWorkRam + 0x3F34, .count = 16, .why = "named by no instruction of the battle program"},
    {.at = kWorkRam + 0x3F44, .count = 16,
     .why = "the formation's record, which LoadBattleProp copies (:7985) and the start reads as it loads the "
            "monsters (:7683, :7695)"},
    // --- the stack -------------------------------------------------------------------------------
    {.at = kWorkRam + 0x1500, .count = 0x100,
     .why = "the interrupts' jump code and the stack: return addresses and saved registers"},
    {.at = kWorkRam + 0x0100, .count = 0x100,
     .why = "the stack of a battle called from the machine's reset, the oracle's: return addresses and saved "
            "registers; the battle program names no cell of the page"},
    // --- beyond the battle's cells ---------------------------------------------------------------
    // An empty party slot's record offset is $FFFF, left by the start's fill: InitInventory reads its
    // shield and weapon through it, sixteen bits wide, from bank $7F (:13919-13920, :13928-13929).
    {.at = 0x7F161E, .count = 3,
     .why = "an empty party slot's hands, read through its record offset $FFFF from bank $7F "
            "(InitInventory :13919-13920, :13928-13929); the slot's hand records are not compared"},
};

// The stack and the interrupts' jump code, where the field calls the battle program from; and the
// stack's page from the reset, where the oracle calls it from a machine fresh from its reset code.
constexpr std::uint32_t kStackFirst = kWorkRam + 0x1500;
constexpr std::uint32_t kStackEnd = kWorkRam + 0x1600;
constexpr std::uint32_t kResetStackFirst = kWorkRam + 0x0100;
constexpr std::uint32_t kResetStackEnd = kWorkRam + 0x0200;

bool holds(const CellsNotCompared& row, std::uint32_t cell) {
    return cell >= row.at && (cell - row.at) % row.stride == 0 && (cell - row.at) / row.stride < row.count;
}
}  // namespace

std::span<const CellsNotCompared> cellsNotCompared() { return kCellsNotCompared; }

std::uint8_t notComparedBits(std::uint32_t cell) {
    std::uint8_t bits = 0;
    for (const CellsNotCompared& row : kCellsNotCompared) {
        if (holds(row, cell)) {
            bits |= row.bits;
        }
    }
    return bits;
}

bool onTheStack(std::uint32_t cell) {
    return (cell >= kStackFirst && cell < kStackEnd) || (cell >= kResetStackFirst && cell < kResetStackEnd);
}

}  // namespace ostinato::trace
