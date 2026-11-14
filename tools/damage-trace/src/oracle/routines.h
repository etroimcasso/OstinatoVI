// The cartridge's battle routines as the oracle calls them: each routine's address and the calls
// that return to it, the opening bytes the oracle checks an image by, and the frame the battle
// program calls them in, in namespace routines. A file that names them says
// `using namespace routines;` inside ostinato::trace.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "oracle/damage_oracle.h"

namespace ostinato::trace::routines {

inline constexpr std::uint32_t kCalcMagicDmg = 0xC22B69;
inline constexpr std::uint32_t kCalcDmg = 0xC22B9D;
inline constexpr std::uint32_t kRelicDmgEffect = 0xC20D4A;
inline constexpr std::uint32_t kApplyDmgMult = 0xC2370B;
inline constexpr std::uint32_t kCalcTargetDmg = 0xC20B83;
inline constexpr std::uint32_t kApplyAndShowDmg = 0xC262EF;
inline constexpr std::uint32_t kCalcReflectDmg = 0xC23483;
inline constexpr std::uint32_t kUpdateStatus = 0xC24391;
inline constexpr std::uint32_t kMagicStatusEffect = 0xC24406;
inline constexpr std::uint32_t kDoAttackerEffect = 0xC23E7D;
inline constexpr std::uint32_t kDoTargetEffect = 0xC2387E;
inline constexpr std::uint32_t kInitGfxParams = 0xC257C2;
inline constexpr std::uint32_t kCoverEffect = 0xC2123B;
inline constexpr std::uint32_t kLearnGenjuMagic = 0xC2602A;
inline constexpr std::uint32_t kDoLevelUp = 0xC260C2;
inline constexpr std::uint32_t kCheckAiTarget = 0xC21F25;
inline constexpr std::uint32_t kInitAi = 0xC22D71;
inline constexpr std::uint32_t kExecMonsterAction = 0xC202DC;
inline constexpr std::uint32_t kExecAiRetal = 0xC24BF4;
// The attack frame: an attack aimed at its own attacker, and every attack of a command.
inline constexpr std::uint32_t kExecSelfAttack = 0xC23167;
inline constexpr std::uint32_t kExecAttack = 0xC2317B;
// The three commands only a script queues: a variable changed, a switch changed, an effect.
inline constexpr std::array<std::uint32_t, 3> kAiCommands{0xC21DFA, 0xC21E1A, 0xC21E5E};
// Four of the battle's own actions: damage over time (Cmd_22), a spell with no caster (Cmd_26), status
// timers running out (Cmd_29) and Seize's drain (Cmd_2d).
inline constexpr std::array<std::uint32_t, 4> kOwnActions{0xC2500B, 0xC24F5F, 0xC25161, 0xC251B2};
// The calls each AI routine returns to as the battle program makes them: loading a monster's
// properties calls InitAI, taking a turn calls ExecMonsterAction, making a counterattack calls
// ExecAIRetal, and running a command calls the command's routine through its table.
inline constexpr std::uint32_t kInitAiCall = 0xC22C3A;
inline constexpr std::uint32_t kExecMonsterActionCall = 0xC20139;
inline constexpr std::uint32_t kExecAiRetalCall = 0xC24B97;
inline constexpr std::uint32_t kCommandCall = 0xC213FE;
// The other calls the trace follows them from: a scripted battle seating a party member with a script
// calls InitAI; a party member with a script whose gauge fills reaches ExecMonsterAction with a jump
// from QueueAction, so the turn returns to QueueAction's caller.
inline constexpr std::uint32_t kSeatedInitAiCall = 0xC2303E;
inline constexpr std::uint32_t kQueuedTurnJump = 0xC2034F;
inline constexpr std::uint32_t kQueueActionCall = 0xC2219E;
// A win's rewards: the experience added, the spells equipment teaches, the levels reached, the
// abilities a level teaches and the rages learned.
inline constexpr std::uint32_t kAddExp = 0xC26235;
inline constexpr std::uint32_t kLearnItemMagic = 0xC25FEF;
inline constexpr std::uint32_t kCheckLevelUp = 0xC2606D;
inline constexpr std::uint32_t kLearnAbilities = 0xC261B6;
inline constexpr std::uint32_t kLearnRage = 0xC24A07;
// The battle program's call to the graphics program (ExecBtlGfx), the graphics program's entry for it
// (ExecBtlGfx_ext, which a JSL enters) and its table of commands (BtlGfxTbl, a word per command, each
// the handler's address in the graphics program's bank; each handler ends with an RTL).
// The battle's end as every ending but two begins it (_c24903), and the title program's laying out
// over work RAM at $7E:5000 (DecompCutsceneProg, in the battle program's bank).
inline constexpr std::uint32_t kWrapUp = 0xC24903;
// The win (WinBattle), the loss (LoseBattle) and what the game keeps (UpdateSRAM).
inline constexpr std::uint32_t kWinBattle = 0xC25D57;
inline constexpr std::uint32_t kLoseBattle = 0xC25FCA;
inline constexpr std::uint32_t kUpdateSram = 0xC24936;
inline constexpr std::uint32_t kDecompCutsceneProg = 0xC26828;
inline constexpr std::uint32_t kExecBtlGfx = 0xC26411;
inline constexpr std::uint32_t kExecBtlGfxExt = 0xC10000;
// The battle program's entry for the field (Battle_ext, `jmp BattleMain`) and BattleMain itself.
inline constexpr std::uint32_t kBattleExt = 0xC20000;
inline constexpr std::uint32_t kBattleMain = 0xC2000C;
inline constexpr std::uint32_t kGraphicsCommandTable = 0xC10008;
inline constexpr std::uint32_t kGraphicsProgramBank = 0xC10000;
// Where each command's handler sits on the US cartridges (BtlGfx_00 to BtlGfx_0d); the table must
// point at them.
inline constexpr std::array<std::uint16_t, 14> kGraphicsCommandHandlers{{
    0x01EC, 0x01FB, 0x0257, 0x024C, 0x950F, 0x01CC, 0x0197, 0x0170, 0x0117, 0x00AF, 0x00AB, 0x0171, 0x002D, 0x0024,
}};
// The two commands that keep the items obtained, which run as the cartridge holds them, and the one
// that answers the menu queue's position, which nothing the oracle runs calls.
inline constexpr std::uint8_t kAddItemObtained = 0x05;
inline constexpr std::uint8_t kMergeItemsObtained = 0x0A;
inline constexpr std::uint8_t kMenuQueuePosition = 0x0D;
inline constexpr const char* kGraphicsKey = "graphics";
// The AI's routines among kTracedRoutines.
inline constexpr const TracedRoutine& kTracedInitAi = kTracedRoutines[9];
inline constexpr const TracedRoutine& kTracedExecMonsterAction = kTracedRoutines[10];
inline constexpr const TracedRoutine& kTracedExecAiRetal = kTracedRoutines[11];
inline constexpr std::array<std::reference_wrapper<const TracedRoutine>, 3> kTracedAiCommands{
    kTracedRoutines[12], kTracedRoutines[13], kTracedRoutines[14]};
// Each returns to the instruction after the calls proved below (kOpenings), three bytes on.
static_assert(kTracedInitAi.entry == kInitAi && kTracedInitAi.frameReturn == kInitAiCall + 3 &&
              kTracedInitAi.otherReturns[0] == kSeatedInitAiCall + 3);
static_assert(kTracedExecMonsterAction.entry == kExecMonsterAction &&
              kTracedExecMonsterAction.frameReturn == kExecMonsterActionCall + 3 &&
              kTracedExecMonsterAction.otherReturns[0] == kQueueActionCall + 3);
static_assert(kTracedExecAiRetal.entry == kExecAiRetal && kTracedExecAiRetal.frameReturn == kExecAiRetalCall + 3);
static_assert(kTracedRoutines[12].entry == kAiCommands[0] && kTracedRoutines[13].entry == kAiCommands[1] &&
              kTracedRoutines[14].entry == kAiCommands[2] && kTracedRoutines[12].frameReturn == kCommandCall + 3 &&
              kTracedRoutines[13].frameReturn == kCommandCall + 3 && kTracedRoutines[14].frameReturn == kCommandCall + 3);
// A script's target test, the condition that names a target and the first step of six others.
inline constexpr std::uint32_t kAiTargetTest = 0xC21D34;
// The table of a script's condition routines (AICondTbl), a word per condition in the game's
// numbering, each the routine's address in the battle program's bank.
inline constexpr std::uint32_t kAiConditionTable = 0xC21D55;
inline constexpr std::uint32_t kBattleProgramBank = 0xC20000;
// Where each condition's routine sits (AICond_00 to AICond_1c); the table must point at them.
inline constexpr std::array<std::uint16_t, 29> kAiConditionRoutines{{
    0x1D2C, 0x1C40, 0x1C3F, 0x1C3B, 0x1C5E, 0x1C70, 0x1BB7, 0x1BC8, 0x1BD7, 0x1C26,
    0x1D2C, 0x1C86, 0x1C9C, 0x1C90, 0x1CC6, 0x1CBA, 0x1CCC, 0x1CDC, 0x1CE4, 0x1CFA,
    0x1CA2, 0x1CB4, 0x1C7F, 0x1D34, 0x1D0D, 0x1CD2, 0x1C2C, 0x1D16, 0x1D22,
}};

// `Reset` (field/reset.asm), where the reset vector's `jml` lands after `clc; xce`
// (field/header.asm): the first instruction the game runs in native mode.
inline constexpr std::uint32_t kReset = 0xC00019;

// The machine is advanced a microsecond at a time until the reset escape fires, so it is parked
// within an instruction or two of it; the whole start takes a few microseconds, and this many steps
// is far more than enough before the start is called a failure.
inline constexpr std::chrono::nanoseconds kStartStep{1'000};
inline constexpr int kStartStepsAllowed = 100'000;

inline constexpr const char* kResetKey = "reset";

struct RoutineOpening {
    const char* name;
    std::uint32_t at;
    std::array<std::uint8_t, 4> bytes;
    std::size_t length;
};
// Each routine's first instruction, as the cartridge holds it.
inline constexpr std::array<RoutineOpening, 51> kOpenings{{
    {"Battle_ext", kBattleExt, {0x4C, 0x0C, 0x00, 0x00}, 3},                         // jmp BattleMain
    {"BattleMain", kBattleMain, {0x08, 0xE2, 0x30, 0x00}, 3},                        // php; shortai
    {"CalcMagicDmg", kCalcMagicDmg, {0xAD, 0xAF, 0x11, 0x00}, 3},    // lda $11af
    {"CalcDmg", kCalcDmg, {0xAD, 0xA2, 0x11, 0x00}, 3},              // lda $11a2
    {"RelicDmgEffect", kRelicDmgEffect, {0x08, 0xAD, 0xA4, 0x11}, 4},  // php; lda $11a4
    {"ApplyDmgMult", kApplyDmgMult, {0x5A, 0xA4, 0xBC, 0x00}, 3},    // phy; ldy $bc
    {"CalcTargetDmg", kCalcTargetDmg, {0x08, 0xE2, 0x20, 0xAD}, 4},  // php; sep #$20; lda $11a6
    {"the apply-and-show routine", kApplyAndShowDmg, {0xDA, 0x5A, 0x64, 0xF0}, 4},  // phx; phy; stz $f0
    {"CalcReflectDmg", kCalcReflectDmg, {0xDA, 0x48, 0x20, 0x00}, 3},                // phx; pha; jsr
    {"UpdateStatus", kUpdateStatus, {0xDA, 0x08, 0xC2, 0x20}, 4},                    // phx; php; rep #$20
    {"MagicStatusEffect", kMagicStatusEffect, {0x08, 0xC2, 0x20, 0xB9}, 4},          // php; rep #$20; lda $3ee4,y
    {"DoAttackerEffect", kDoAttackerEffect, {0xDA, 0x08, 0xE2, 0x30}, 4},            // phx; php; sep #$30
    {"DoTargetEffect", kDoTargetEffect, {0xDA, 0x5A, 0x08, 0xE2}, 4},                // phx; phy; php; sep
    {"InitGfxParams", kInitGfxParams, {0x08, 0xE2, 0x30, 0x64}, 4},                  // php; sep #$30; stz $a0
    {"CoverEffect", kCoverEffect, {0xDA, 0x08, 0xA5, 0xB2}, 4},                      // phx; php; lda $b2
    {"LearnGenjuMagic", kLearnGenjuMagic, {0xDA, 0x20, 0x93, 0x62}, 4},              // phx; jsr GetGenjuPropPtr
    {"DoLevelUp", kDoLevelUp, {0x08, 0xFE, 0x08, 0x16}, 4},                          // php; inc $1608,x
    {"CheckAITarget", kCheckAiTarget, {0xDA, 0x5A, 0x48, 0x64}, 4},                  // phx; phy; pha; stz $b8
    {"the AI's target test", kAiTargetTest, {0xAD, 0x2E, 0x3A, 0x48}, 4},            // lda $3a2e; pha
    // The test's clearing of its targets for the monster itself, the same byte twice.
    {"the AI's target test's clear", kAiTargetTest + 0x1B, {0x64, 0xFC, 0x64, 0xFC}, 4},  // stz $fc; stz $fc
    {"InitAI", kInitAi, {0xDA, 0x48, 0x08, 0xC2}, 4},                                // phx; pha; php; rep
    {"ExecMonsterAction", kExecMonsterAction, {0xC2, 0x20, 0x9C, 0x98}, 4},          // rep #$20; stz $3a98
    {"ExecAIRetal", kExecAiRetal, {0x08, 0xC2, 0x20, 0x9C}, 4},                      // php; rep #$20; stz
    {"Cmd_2e", kAiCommands[0], {0xA6, 0xB6, 0x20, 0x45}, 4},                         // ldx $b6; jsr GetBattleVar
    {"Cmd_2f", kAiCommands[1], {0xA6, 0xB9, 0x20, 0x57}, 4},                         // ldx $b9; jsr _c21e57
    {"Cmd_30", kAiCommands[2], {0xA5, 0xB6, 0x0A, 0xAA}, 4},                         // lda $b6; asl; tax
    {"Cmd_22", kOwnActions[0], {0xAD, 0x77, 0x3A, 0xF0}, 4},                         // lda $3a77; beq
    {"Cmd_26", kOwnActions[1], {0xA5, 0xB8, 0xA6, 0xB6}, 4},                         // lda $b8; ldx $b6
    {"Cmd_29", kOwnActions[2], {0xAE, 0x7D, 0x3A, 0x20}, 4},                         // ldx $3a7d; jsr _c2298a
    {"Cmd_2d", kOwnActions[3], {0xB9, 0xA1, 0x3A, 0x29}, 4},                         // lda $3aa1,y; and #$ef
    {"ExecSelfAttack", kExecSelfAttack, {0xA9, 0x80, 0x14, 0xB3}, 4},                 // lda #$80; trb $b3
    {"ExecAttack", kExecAttack, {0xDA, 0xA5, 0xBD, 0x85}, 4},                         // phx; lda $bd; sta
    // The calls they return to.
    {"the call to InitAI", kInitAiCall, {0x20, 0x71, 0x2D, 0x00}, 3},                // jsr InitAI
    {"the call to ExecMonsterAction", kExecMonsterActionCall, {0x20, 0xDC, 0x02, 0x00}, 3},  // jsr
    {"the call to ExecAIRetal", kExecAiRetalCall, {0x20, 0xF4, 0x4B, 0x00}, 3},      // jsr ExecAIRetal
    {"the command call", kCommandCall, {0xFC, 0xC7, 0x19, 0x00}, 3},                  // jsr (CmdTbl,x)
    {"the seated party member's call to InitAI", kSeatedInitAiCall, {0x20, 0x71, 0x2D, 0x00}, 3},  // jsr InitAI
    {"QueueAction's jump to ExecMonsterAction", kQueuedTurnJump, {0x4C, 0xDC, 0x02, 0x00}, 3},     // jmp
    {"the call to QueueAction", kQueueActionCall, {0x20, 0x1C, 0x03, 0x00}, 3},                    // jsr QueueAction
    {"AddExp", kAddExp, {0x08, 0xC2, 0x21, 0xAD}, 4},                                // php; rep #$21; lda $2f35
    {"LearnItemMagic", kLearnItemMagic, {0xDA, 0xA0, 0x06, 0x00}, 4},                // phx; ldy #$0006
    {"CheckLevelUp", kCheckLevelUp, {0x64, 0xF8, 0x7B, 0xBD}, 4},                    // stz $f8; tdc; lda $1608,x
    {"LearnAbilities", kLearnAbilities, {0xA2, 0x00, 0x00, 0xC9}, 4},                // ldx #$0000; cmp #CHAR::TERRA
    {"LearnRage", kLearnRage, {0xA2, 0x0A, 0xBD, 0x02}, 4},                          // ldx #$0a; lda $2002,x
    {"ExecBtlGfx", kExecBtlGfx, {0xDA, 0x5A, 0x08, 0xE2}, 4},                        // phx; phy; php; sep #$20
    {"ExecBtlGfx_ext", kExecBtlGfxExt, {0x48, 0x7B, 0x68, 0x0A}, 4},                 // pha; tdc; pla; asl
    {"_c24903", kWrapUp, {0x20, 0x36, 0x0B, 0xA2}, 4},                               // jsr SaveMorphCounter; ldx
    {"DecompCutsceneProg", kDecompCutsceneProg, {0x08, 0xC2, 0x30, 0x48}, 4},        // php; rep #$30; pha
    {"WinBattle", kWinBattle, {0x08, 0x20, 0x67, 0x02}, 4},                          // php; jsr EndAction
    {"LoseBattle", kLoseBattle, {0x48, 0xA9, 0x01, 0x0C}, 4},                        // pha; lda #$01; tsb
    {"UpdateSRAM", kUpdateSram, {0xA2, 0x06, 0xBD, 0xD8}, 4},                        // ldx #$06; lda $3ed8,x
}};

// The frame the battle program calls them in: data bank $7E and direct page $0000, so every cell is
// a work-RAM address. CalcDmg and RelicDmgEffect are entered with both register widths narrow,
// ApplyDmgMult, CalcTargetDmg and CoverEffect with the accumulator wide and the index registers
// narrow, and the two routines on the party's records with the accumulator narrow and the index
// registers wide; interrupts are off throughout.
inline constexpr std::uint8_t kBattleDataBank = 0x7E;
inline constexpr std::uint16_t kBattleDirectPage = 0x0000;
inline constexpr std::uint8_t kByteWideStatus = 0x34;
inline constexpr std::uint8_t kWideAccumulatorStatus = 0x14;
inline constexpr std::uint8_t kWideIndexStatus = 0x24;
// The carry, in the status byte a routine is called or returns with.
inline constexpr std::uint8_t kCarryBit = 0x01;

// The read-set gate watches the battle's work RAM below the graphics buffers, the stack among it.
inline constexpr std::uint32_t kGateStart = DamageOracle::kWorkRam;
inline constexpr std::uint32_t kGateEnd = DamageOracle::kWorkRam + 0x4000;
// The end of work RAM's second bank, $7F:FFFF, and the watch over the cells past the battle's.
inline constexpr std::uint32_t kSecondWorkRamBankEnd = 0x800000;
inline constexpr const char* kBeyondKey = "beyond the battle's cells";

}  // namespace ostinato::trace::routines
