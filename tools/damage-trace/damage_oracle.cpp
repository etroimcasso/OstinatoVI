#include "damage_oracle.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "retropp/guest_escape.h"
#include "retropp/guest_watch.h"
#include "retropp/isa.h"
#include "retropp/memory_region.h"
#include "retropp/snes.h"
#include "retropp/timing.h"

#include "ai_script_bytes.h"
#include "battle/ai.h"
#include "battle/ai_conditions.h"
#include "battle/ai_targets.h"
#include "battle/command_executor.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "fixtures/ai_scripts_expected.h"
#include "ostinato/monster_animation.h"
#include "ostinato/text_class.h"
#include "battle/attacker_effects.h"
#include "battle/battle_end.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_layout.h"
#include "battle/battle_start.h"
#include "battle/battle_script.h"
#include "battle/damage.h"
#include "battle/items_obtained.h"
#include "battle/leveling.h"
#include "battle/rewards.h"
#include "battle/spell_learning.h"
#include "battle/status.h"
#include "battle/target_effects.h"
#include "battle/targeting.h"
#include "data/attack_properties.h"
#include "data/battle_commands.h"
#include "data/esper_properties.h"
#include "data/battle_tables.h"
#include "data/char_ai.h"
#include "data/formations.h"
#include "data/level_up.h"
#include "data/monster_attacks.h"
#include "ostinato/attack_id.h"
#include "ostinato/item_id.h"
#include "ostinato/attack_flags.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/game_limits.h"
#include "ostinato/game_version.h"
#include "ostinato/formation_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"

namespace ostinato::trace {

namespace {

// --- the routines (battle/battle_main.asm; the battle program sits at $C2:0000) -----------------

constexpr std::uint32_t kCalcMagicDmg = 0xC22B69;
constexpr std::uint32_t kCalcDmg = 0xC22B9D;
constexpr std::uint32_t kRelicDmgEffect = 0xC20D4A;
constexpr std::uint32_t kApplyDmgMult = 0xC2370B;
constexpr std::uint32_t kCalcTargetDmg = 0xC20B83;
constexpr std::uint32_t kApplyAndShowDmg = 0xC262EF;
constexpr std::uint32_t kCalcReflectDmg = 0xC23483;
constexpr std::uint32_t kUpdateStatus = 0xC24391;
constexpr std::uint32_t kMagicStatusEffect = 0xC24406;
constexpr std::uint32_t kDoAttackerEffect = 0xC23E7D;
constexpr std::uint32_t kDoTargetEffect = 0xC2387E;
constexpr std::uint32_t kInitGfxParams = 0xC257C2;
constexpr std::uint32_t kCoverEffect = 0xC2123B;
constexpr std::uint32_t kLearnGenjuMagic = 0xC2602A;
constexpr std::uint32_t kDoLevelUp = 0xC260C2;
constexpr std::uint32_t kCheckAiTarget = 0xC21F25;
constexpr std::uint32_t kInitAi = 0xC22D71;
constexpr std::uint32_t kExecMonsterAction = 0xC202DC;
constexpr std::uint32_t kExecAiRetal = 0xC24BF4;
// The attack frame: an attack aimed at its own attacker, and every attack of a command.
constexpr std::uint32_t kExecSelfAttack = 0xC23167;
constexpr std::uint32_t kExecAttack = 0xC2317B;
// The three commands only a script queues: a variable changed, a switch changed, an effect.
constexpr std::array<std::uint32_t, 3> kAiCommands{0xC21DFA, 0xC21E1A, 0xC21E5E};
// Four of the battle's own actions: damage over time (Cmd_22), a spell with no caster (Cmd_26), status
// timers running out (Cmd_29) and Seize's drain (Cmd_2d).
constexpr std::array<std::uint32_t, 4> kOwnActions{0xC2500B, 0xC24F5F, 0xC25161, 0xC251B2};
// The calls each AI routine returns to as the battle program makes them: loading a monster's
// properties calls InitAI, taking a turn calls ExecMonsterAction, making a counterattack calls
// ExecAIRetal, and running a command calls the command's routine through its table.
constexpr std::uint32_t kInitAiCall = 0xC22C3A;
constexpr std::uint32_t kExecMonsterActionCall = 0xC20139;
constexpr std::uint32_t kExecAiRetalCall = 0xC24B97;
constexpr std::uint32_t kCommandCall = 0xC213FE;
// The other calls the trace follows them from: a scripted battle seating a party member with a script
// calls InitAI; a party member with a script whose gauge fills reaches ExecMonsterAction with a jump
// from QueueAction, so the turn returns to QueueAction's caller.
constexpr std::uint32_t kSeatedInitAiCall = 0xC2303E;
constexpr std::uint32_t kQueuedTurnJump = 0xC2034F;
constexpr std::uint32_t kQueueActionCall = 0xC2219E;
// A win's rewards: the experience added, the spells equipment teaches, the levels reached, the
// abilities a level teaches and the rages learned.
constexpr std::uint32_t kAddExp = 0xC26235;
constexpr std::uint32_t kLearnItemMagic = 0xC25FEF;
constexpr std::uint32_t kCheckLevelUp = 0xC2606D;
constexpr std::uint32_t kLearnAbilities = 0xC261B6;
constexpr std::uint32_t kLearnRage = 0xC24A07;
// The battle program's call to the graphics program (ExecBtlGfx), the graphics program's entry for it
// (ExecBtlGfx_ext, which a JSL enters) and its table of commands (BtlGfxTbl, a word per command, each
// the handler's address in the graphics program's bank; each handler ends with an RTL).
// The battle's end as every ending but two begins it (_c24903), and the title program's laying out
// over work RAM at $7E:5000 (DecompCutsceneProg, in the battle program's bank).
constexpr std::uint32_t kWrapUp = 0xC24903;
// The win (WinBattle), the loss (LoseBattle) and what the game keeps (UpdateSRAM).
constexpr std::uint32_t kWinBattle = 0xC25D57;
constexpr std::uint32_t kLoseBattle = 0xC25FCA;
constexpr std::uint32_t kUpdateSram = 0xC24936;
constexpr std::uint32_t kDecompCutsceneProg = 0xC26828;
constexpr std::uint32_t kExecBtlGfx = 0xC26411;
constexpr std::uint32_t kExecBtlGfxExt = 0xC10000;
// The battle program's entry for the field (Battle_ext, `jmp BattleMain`) and BattleMain itself.
constexpr std::uint32_t kBattleExt = 0xC20000;
constexpr std::uint32_t kBattleMain = 0xC2000C;
constexpr std::uint32_t kGraphicsCommandTable = 0xC10008;
constexpr std::uint32_t kGraphicsProgramBank = 0xC10000;
// Where each command's handler sits on the US cartridges (BtlGfx_00 to BtlGfx_0d); the table must
// point at them.
constexpr std::array<std::uint16_t, 14> kGraphicsCommandHandlers{{
    0x01EC, 0x01FB, 0x0257, 0x024C, 0x950F, 0x01CC, 0x0197, 0x0170, 0x0117, 0x00AF, 0x00AB, 0x0171, 0x002D, 0x0024,
}};
// The two commands that keep the items obtained, which run as the cartridge holds them, and the one
// that answers the menu queue's position, which nothing the oracle runs calls.
constexpr std::uint8_t kAddItemObtained = 0x05;
constexpr std::uint8_t kMergeItemsObtained = 0x0A;
constexpr std::uint8_t kMenuQueuePosition = 0x0D;
constexpr const char* kGraphicsKey = "graphics";
// The AI's routines among kTracedRoutines.
constexpr const TracedRoutine& kTracedInitAi = kTracedRoutines[9];
constexpr const TracedRoutine& kTracedExecMonsterAction = kTracedRoutines[10];
constexpr const TracedRoutine& kTracedExecAiRetal = kTracedRoutines[11];
constexpr std::array<std::reference_wrapper<const TracedRoutine>, 3> kTracedAiCommands{
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
constexpr std::uint32_t kAiTargetTest = 0xC21D34;
// The table of a script's condition routines (AICondTbl), a word per condition in the game's
// numbering, each the routine's address in the battle program's bank.
constexpr std::uint32_t kAiConditionTable = 0xC21D55;
constexpr std::uint32_t kBattleProgramBank = 0xC20000;
// Where each condition's routine sits (AICond_00 to AICond_1c); the table must point at them.
constexpr std::array<std::uint16_t, 29> kAiConditionRoutines{{
    0x1D2C, 0x1C40, 0x1C3F, 0x1C3B, 0x1C5E, 0x1C70, 0x1BB7, 0x1BC8, 0x1BD7, 0x1C26,
    0x1D2C, 0x1C86, 0x1C9C, 0x1C90, 0x1CC6, 0x1CBA, 0x1CCC, 0x1CDC, 0x1CE4, 0x1CFA,
    0x1CA2, 0x1CB4, 0x1C7F, 0x1D34, 0x1D0D, 0x1CD2, 0x1C2C, 0x1D16, 0x1D22,
}};

// `Reset` (field/reset.asm), where the reset vector's `jml` lands after `clc; xce`
// (field/header.asm): the first instruction the game runs in native mode.
constexpr std::uint32_t kReset = 0xC00019;

// The machine is advanced a microsecond at a time until the reset escape fires, so it is parked
// within an instruction or two of it; the whole start takes a few microseconds, and this many steps
// is far more than enough before the start is called a failure.
constexpr std::chrono::nanoseconds kStartStep{1'000};
constexpr int kStartStepsAllowed = 100'000;

constexpr const char* kResetKey = "reset";

struct RoutineOpening {
    const char* name;
    std::uint32_t at;
    std::array<std::uint8_t, 4> bytes;
    std::size_t length;
};
// Each routine's first instruction, as the cartridge holds it.
constexpr std::array<RoutineOpening, 51> kOpenings{{
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
constexpr std::uint8_t kBattleDataBank = 0x7E;
constexpr std::uint16_t kBattleDirectPage = 0x0000;
constexpr std::uint8_t kByteWideStatus = 0x34;
constexpr std::uint8_t kWideAccumulatorStatus = 0x14;
constexpr std::uint8_t kWideIndexStatus = 0x24;
// The carry, in the status byte a routine is called or returns with.
constexpr std::uint8_t kCarryBit = 0x01;

// The read-set gate watches the battle's work RAM below the graphics buffers, the stack among it.
constexpr std::uint32_t kGateStart = DamageOracle::kWorkRam;
constexpr std::uint32_t kGateEnd = DamageOracle::kWorkRam + 0x4000;
// The end of work RAM's second bank, $7F:FFFF.
constexpr std::uint32_t kSecondWorkRamBankEnd = 0x800000;

// --- the cells the per-target pass reads and writes ----------------------------------------------

constexpr std::uint32_t kWorkRam = DamageOracle::kWorkRam;
constexpr std::uint32_t kEffect = kWorkRam + 0x11A9;            // the special effect, its index doubled
constexpr std::uint32_t kFirstAttackFlags = kWorkRam + 0x00B2;  // bit 7: a drain is capped
constexpr std::uint32_t kAnimationCommand = kWorkRam + 0x00B4;  // the script command the animation goes out as
// Every command starts with its animation going out as script command $06, show the animation
// (ExecCmd, battle_main.asm:3096-3097).
constexpr std::uint8_t kShowAnimationCommand = 0x06;
constexpr std::uint32_t kCommand = kWorkRam + 0x00B5;
constexpr std::uint32_t kAttackIndex = kWorkRam + 0x00B6;
constexpr std::uint32_t kSecondary = kWorkRam + 0x00B7;
constexpr std::uint32_t kRngPosition = kWorkRam + 0x00BE;
constexpr std::uint32_t kDamageModification = kWorkRam + 0x3414;
constexpr std::uint32_t kGolemBlock = kWorkRam + 0x3A82;
constexpr std::uint32_t kInterceptorBlock = kWorkRam + 0x3A83;
constexpr std::uint32_t kForcefield = kWorkRam + 0x3EC8;
// The battle script's queue: where the attack's animation went, the next free place, the queue.
constexpr std::uint32_t kAnimationPlace = kWorkRam + 0x3A71;
constexpr std::uint32_t kQueueEnd = kWorkRam + 0x3A72;
constexpr std::uint32_t kQueue = kWorkRam + 0x2D6E;
constexpr std::uint8_t kQueueCommandSize = 4;
// Per battler, at twice the battler's index.
constexpr std::uint32_t kLevel = kWorkRam + 0x3B18;
constexpr std::uint32_t kDefenses = kWorkRam + 0x3BB8;   // defense, then magic defense
constexpr std::uint32_t kHp = kWorkRam + 0x3BF4;
constexpr std::uint32_t kMp = kWorkRam + 0x3C08;
constexpr std::uint32_t kMaxHp = kWorkRam + 0x3C1C;
constexpr std::uint32_t kMaxMp = kWorkRam + 0x3C30;
constexpr std::uint32_t kAbsorbedNullified = kWorkRam + 0x3BCC;
constexpr std::uint32_t kWeakHalved = kWorkRam + 0x3BE0;
constexpr std::uint32_t kMonsterFlags = kWorkRam + 0x3C95;
constexpr std::uint32_t kStance = kWorkRam + 0x3AA1;
constexpr std::uint32_t kStatus12 = kWorkRam + 0x3EE4;
constexpr std::uint32_t kStatus34 = kWorkRam + 0x3EF8;
constexpr std::uint32_t kToSet12 = kWorkRam + 0x3DD4;
constexpr std::uint32_t kToSet34 = kWorkRam + 0x3DE8;
constexpr std::uint32_t kToClear12 = kWorkRam + 0x3DFC;
constexpr std::uint32_t kToClear34 = kWorkRam + 0x3E10;
constexpr std::uint32_t kController = kWorkRam + 0x32B9;
constexpr std::uint32_t kLastAttacker = kWorkRam + 0x32E0;
constexpr std::uint32_t kTaken = kWorkRam + 0x33D0;
constexpr std::uint32_t kHealed = kWorkRam + 0x33E4;

// --- the cells taking and showing the damage reads and writes ------------------------------------

// Per battler, a word at twice the battler's index: its bit in the battler sets below (the party
// members' in the low byte, the monsters' in the high byte), what is to be worked out again for it,
// and the battler that last damaged it.
constexpr std::uint32_t kBattlerBit = kWorkRam + 0x3018;
constexpr std::uint32_t kToUpdate = kWorkRam + 0x3204;
constexpr std::uint32_t kLastDamagedBy = kWorkRam + 0x327C;
constexpr std::uint32_t kMissed = kWorkRam + 0x3A5A;
constexpr std::uint32_t kInvincible = kWorkRam + 0x3A3C;
constexpr std::uint32_t kGolemHp = kWorkRam + 0x3A36;
constexpr std::uint32_t kWeaponSpellcast = kWorkRam + 0x3A89;
constexpr std::uint32_t kBlackBeltTargets = kWorkRam + 0x3419;  // a party member's bit clear: it may be countered for
// The numerals for five or more battlers are copied to the graphics buffer, 20 bytes per command,
// counted from the buffer's start.
constexpr std::uint32_t kNumeralBuffer = kWorkRam + 0x2BCE;
constexpr std::uint32_t kNumeralBufferCount = kWorkRam + 0x3A34;
constexpr std::uint32_t kNumeralBufferSize = 20;
// The castable spells to be worked out again.
constexpr std::uint8_t kEnabledMagicBit = 0x80;
// The numeral commands: one battler's numeral, or every battler's at once from the buffer.
constexpr std::uint8_t kOneNumeral = 0x0B;
constexpr std::uint8_t kEveryNumeral = 0x03;
// A numeral's word: the gain mark, the miss mark and the amount.
constexpr std::uint16_t kHealMark = 0x8000;
constexpr std::uint16_t kMissMark = 0x4000;
constexpr std::uint16_t kAmountBits = 0x3FFF;
constexpr std::uint8_t kPartyBits = 0x0F;

// --- the cells the reflected attack's aim and hit check read ------------------------------------

constexpr std::uint32_t kTargets = kWorkRam + 0x00B8;
constexpr std::uint32_t kAimingRules = kWorkRam + 0x00BA;
constexpr std::uint32_t kAimFlags = kWorkRam + 0x3A46;          // bit 1: the next aim is exact
constexpr std::uint32_t kEarlierTargets = kWorkRam + 0x3A4E;
constexpr std::uint32_t kEnemyCharacters = kWorkRam + 0x3A40;   // party members fighting against the party
constexpr std::uint32_t kBattleType = kWorkRam + 0x201F;
constexpr std::uint32_t kPincerFirstSide = kWorkRam + 0x2EAC;
constexpr std::uint32_t kPincerSecondSide = kWorkRam + 0x2EAD;
// The reach check ANDs four cells: the party members' bits of the first two words with the
// monsters' bits of a byte and the next two words.
constexpr std::uint32_t kReachMonsters = kWorkRam + 0x2F46;
constexpr std::uint32_t kReachCharacters = kWorkRam + 0x3403;
constexpr std::uint32_t kReachFirst = kWorkRam + 0x3A78;
constexpr std::uint32_t kReachSecond = kWorkRam + 0x3408;
constexpr std::uint32_t kCountersEnabled = kWorkRam + 0x341A;   // negative: counterattacks are on
// The reach check loads the counterattack cell as a word, so its sign is this next cell's: negative,
// battlers away from the field are dropped. A command starts it at $FF and only an attack aimed at
// its own attacker clears it.
constexpr std::uint32_t kReachKeepsAway = kWorkRam + 0x341B;
constexpr std::uint32_t kAwayFromTheField = kWorkRam + 0x3F2C;
constexpr std::uint32_t kTargetCount = kWorkRam + 0x3EC9;
constexpr std::uint32_t kQuietMisses = kWorkRam + 0x341C;       // 0: misses show nothing
constexpr std::uint32_t kHitInTheBack = kWorkRam + 0x3A54;
constexpr std::uint32_t kQueuedCommand = kWorkRam + 0x3A7C;
constexpr std::uint32_t kLastSpell = kWorkRam + 0x3410;
constexpr std::uint32_t kLastItem = kWorkRam + 0x3411;
constexpr std::uint32_t kExtraHits = kWorkRam + 0x3405;         // one less than the hits still to come
// The params blocks the script's animations read, sixteen bytes each, and where the next goes; the
// battle-type step reads the attacker of the block before it.
constexpr std::uint32_t kParamsBlocks = kWorkRam + 0x2C6E;
constexpr std::uint32_t kNextParamsBlock = kWorkRam + 0x3A32;
constexpr std::uint8_t kParamsBlockSize = 16;
// Per battler, at twice the battler's index.
constexpr std::uint32_t kOnTheField = kWorkRam + 0x3AA0;        // bit 0
constexpr std::uint32_t kCharmedBy = kWorkRam + 0x3395;
constexpr std::uint32_t kStamina = kWorkRam + 0x3B40;
constexpr std::uint32_t kEvadeAndMagicBlock = kWorkRam + 0x3B54;  // evade, then magic block
constexpr std::uint32_t kBlockGraphics = kWorkRam + 0x3CE4;       // a blow's, then a spell's
constexpr std::uint32_t kRetort = kWorkRam + 0x3E4C;              // bit 0
constexpr std::uint32_t kLastHitCommandBy = kWorkRam + 0x3290;
constexpr std::uint32_t kLastHitCommand = kWorkRam + 0x3D48;
constexpr std::uint32_t kLastHitSpellBy = kWorkRam + 0x3291;
constexpr std::uint32_t kLastHitSpell = kWorkRam + 0x3D49;
constexpr std::uint32_t kLastHitItemBy = kWorkRam + 0x32A4;
constexpr std::uint32_t kLastHitItem = kWorkRam + 0x3D5C;
constexpr std::uint32_t kLastHitElementBy = kWorkRam + 0x32A5;
constexpr std::uint32_t kLastHitElement = kWorkRam + 0x3D5D;

constexpr std::uint8_t kAimExactlyBit = 0x02;
constexpr std::uint8_t kCannotBeCriticalBit = 0x02;
constexpr std::uint8_t kVanishAppliesBit = 0x80;
constexpr std::uint8_t kInstantDeathProtectedBit = 0x04;
constexpr std::uint8_t kEquipmentBlocks = 0x0F;
constexpr std::uint8_t kCountersOn = 0x80;
constexpr std::uint8_t kNotQuiet = 0xFF;
constexpr std::uint8_t kAwayUnreachable = 0xFF;  // kReachKeepsAway as a command starts
constexpr std::uint8_t kNoHitsToCome = 0xFF;
// The bounce's two commands.
constexpr std::uint8_t kReflectedCommand = 0x09;
constexpr std::uint8_t kScatteredCommand = 0x15;
// The bounce's block: a battler per slot, then a block per party slot, then a battler set.
constexpr std::size_t kBlockBlocks = 10;
constexpr std::size_t kBlockSet = 14;

// Damage modification on and off; a link or block that names no one; a ledger entry with nothing in it.
constexpr std::uint8_t kOn = 0xFF;
constexpr std::uint8_t kNoOne = 0xFF;
constexpr std::uint16_t kNoDamage = 0xFFFF;
constexpr std::uint8_t kDrainCappedBit = 0x80;
constexpr std::uint8_t kWaitingBit = 0x80;
// A battler's last-attacker mark: the attacker's slot index with the top bit set while the battler
// waits to answer it. With no last attacker the slot bits hold $7F, as the battle's start leaves
// them when it fills the cell with $FF; no slot reads as a battler there.
constexpr std::uint8_t kNoLastAttacker = 0x7F;

std::uint8_t markByte(const battle::Battler& battler) {
    const std::uint8_t slot =
        battler.lastAttacker ? static_cast<std::uint8_t>(battler.lastAttacker->index()) : kNoLastAttacker;
    return static_cast<std::uint8_t>(slot | (battler.retaliationPending ? kWaitingBit : 0));
}
// The monster flags byte: undead, the Imp critical, human, the name hidden, dying at 0 MP.
constexpr std::uint8_t kUndeadBit = 0x80;
constexpr std::uint8_t kImpCriticalBit = 0x40;
constexpr std::uint8_t kHumanBit = 0x10;
constexpr std::uint8_t kNameHiddenBit = 0x04;
constexpr std::uint8_t kDiesAtZeroMpBit = 0x01;
// The stance byte: Defending, the back row.
constexpr std::uint8_t kDefendingBit = 0x02;
constexpr std::uint8_t kBackRowBit = 0x20;
// The queue's place for the attack's animation is filled with this before a call, and the port's
// with an animation of this second value, so a rewrite on either side shows.
constexpr std::uint8_t kUnwritten = 0xEE;

// The second attack-flags byte as a command starts it, every bit set, with the two bits the damage
// routines read: set while the hand is not held in both hands, set while an attack that ignores
// defense is not multiplied (docs/contracts/battle_commands.md, "A command's start").
constexpr std::uint8_t kAttackFlagsAtStart = 0xFF;
constexpr std::uint8_t kNotTwoHanded = 0x40;
constexpr std::uint8_t kIgnoreDefenseStopsMultiplier = 0x10;

// The first relic word holds relic effects 1 in its low byte, with that byte's top bit taken by
// relic effects 2's top bit, and relic effects 3 in its high byte (InitCharBattle).
constexpr std::uint8_t kLowSevenBits = 0x7F;
constexpr std::uint8_t kTopBit = 0x80;

std::uint16_t slotIndex(battle::BattlerSlot slot) {
    return static_cast<std::uint16_t>(slot.index() * 2);
}

// A battler set as the battle program holds it: a word with the party members' bits in its low byte
// and the monsters' in its high byte.
std::uint16_t maskOf(battle::BattlerSet set) {
    return static_cast<std::uint16_t>(set.characters.bits | set.monsters.bits << 8);
}

battle::BattlerSet setOf(std::uint16_t mask) {
    return battle::BattlerSet{BattleSlotMask(static_cast<std::uint8_t>(mask & 0xFF)),
                              BattleSlotMask(static_cast<std::uint8_t>(mask >> 8))};
}

std::uint8_t byte(std::mt19937& rng) {
    return static_cast<std::uint8_t>(rng() & 0xFF);
}

}  // namespace

DamageOracle::DamageOracle(std::span<const std::uint8_t> image) {
    machine_.hostRom(image);
    for (const RoutineOpening& routine : kOpenings) {
        const std::vector<std::uint8_t> bytes =
            machine_.read(retropp::MemoryRegion{.at = routine.at, .size = static_cast<std::uint32_t>(routine.length)});
        if (!std::equal(bytes.begin(), bytes.end(), routine.bytes.begin())) {
            throw std::runtime_error(std::string{"the cartridge does not hold "} + routine.name +
                                     " where the US cartridges do");
        }
    }

    machine_.registerEscapes(retropp::escapes(retropp::GuestEscape{
        .key = kResetKey, .at = kReset, .handler = [this](retropp::Vm&, std::uint32_t) { reset_ = true; }}));
    // The trace's watches cover the damage word and the multiplier's steps; their writes during a
    // call are what writes() hands back.
    trace_ = std::make_unique<DamageTrace>(
        machine_,
        [this](const std::string& line) {
            if (tracing_) {
                traced_.push_back(line);
            }
        },
        [] { return std::uint64_t{0}; },
        [this](std::uint32_t at, std::uint8_t value, bool write) {
            if (recording_ && write && (at == kDamage || at == kDamage + 1 || at == kMultiplier)) {
                writes_.push_back(WatchedWrite{.at = at, .value = value});
            }
            noteGuestAccess(at, value, write);
        });
    registerGate();

    const retropp::RoutineBinding byteWide{
        .inputs = {retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
        .isa = retropp::Isa::Wdc65816};
    calcMagicDmg_ = machine_.bindRoutine<Call>(kCalcMagicDmg, byteWide);
    calcDmg_ = machine_.bindRoutine<Call>(kCalcDmg, byteWide);
    relicDmgEffect_ = machine_.bindRoutine<Call>(kRelicDmgEffect, byteWide);
    applyDmgMult_ = machine_.bindRoutine<Multiply>(
        kApplyDmgMult, {.inputs = {retropp::snes::C, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                        .output = retropp::snes::C,
                        .isa = retropp::Isa::Wdc65816});
    applyAndShowDmg_ = machine_.bindRoutine<Call>(
        kApplyAndShowDmg, {.inputs = {retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                           .isa = retropp::Isa::Wdc65816});
    calcReflectDmg_ = machine_.bindRoutine<TargetCall>(
        kCalcReflectDmg,
        {.inputs = {retropp::snes::C, retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    calcTargetDmg_ = machine_.bindRoutine<TargetCall>(
        kCalcTargetDmg,
        {.inputs = {retropp::snes::X, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    updateStatus_ = machine_.bindRoutine<PassCall>(
        kUpdateStatus,
        {.inputs = {retropp::snes::P, retropp::snes::DB, retropp::snes::D}, .isa = retropp::Isa::Wdc65816});
    magicStatusEffect_ = machine_.bindRoutine<TargetCall>(
        kMagicStatusEffect,
        {.inputs = {retropp::snes::X, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    doAttackerEffect_ = machine_.bindRoutine<Call>(kDoAttackerEffect, byteWide);
    doTargetEffect_ = machine_.bindRoutine<TargetCall>(
        kDoTargetEffect,
        {.inputs = {retropp::snes::X, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    initGfxParams_ = machine_.bindRoutine<Call>(kInitGfxParams, byteWide);
    coverEffect_ = machine_.bindRoutine<Call>(kCoverEffect, byteWide);
    learnGenjuMagic_ = machine_.bindRoutine<TargetCall>(
        kLearnGenjuMagic,
        {.inputs = {retropp::snes::C, retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    doLevelUp_ = machine_.bindRoutine<Call>(kDoLevelUp, byteWide);

    // The AI's routines answer in the carry, read from the status byte they return with.
    checkAiTarget_ = machine_.bindRoutine<TargetsCall>(
        kCheckAiTarget,
        {.inputs = {retropp::snes::A, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .output = retropp::snes::P,
         .isa = retropp::Isa::Wdc65816});
    const std::vector<std::uint8_t> table = machine_.read(retropp::MemoryRegion{
        .at = kAiConditionTable, .size = static_cast<std::uint32_t>(kAiConditionRoutines.size() * 2)});
    for (std::size_t number = 0; number < kAiConditionRoutines.size(); ++number) {
        const auto word = static_cast<std::uint16_t>(table[number * 2] | table[number * 2 + 1] << 8);
        if (word != kAiConditionRoutines[number]) {
            throw std::runtime_error("the cartridge's table of AI conditions does not point where the US "
                                     "cartridges' does at condition " + std::to_string(number));
        }
        aiConditions_.push_back(machine_.bindRoutine<ConditionCall>(
            kBattleProgramBank | word,
            {.inputs = {retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
             .output = retropp::snes::P,
             .isa = retropp::Isa::Wdc65816}));
    }

    initAi_ = machine_.bindRoutine<SetupCall>(
        kInitAi, {.inputs = {retropp::snes::C, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                  .isa = retropp::Isa::Wdc65816});
    execMonsterAction_ = machine_.bindRoutine<Call>(kExecMonsterAction, byteWide);
    execAiRetal_ = machine_.bindRoutine<Call>(kExecAiRetal, byteWide);
    for (std::size_t i = 0; i < kAiCommands.size(); ++i) {
        aiCommands_[i] = machine_.bindRoutine<Call>(
            kAiCommands[i], {.inputs = {retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                             .isa = retropp::Isa::Wdc65816});
    }
    for (std::size_t i = 0; i < kOwnActions.size(); ++i) {
        ownActions_[i] = machine_.bindRoutine<Call>(
            kOwnActions[i], {.inputs = {retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                             .isa = retropp::Isa::Wdc65816});
    }
    execSelfAttack_ = machine_.bindRoutine<Call>(kExecSelfAttack, byteWide);
    execAttack_ = machine_.bindRoutine<Call>(kExecAttack, byteWide);

    addExp_ = machine_.bindRoutine<Call>(kAddExp, byteWide);
    learnItemMagic_ = machine_.bindRoutine<Call>(kLearnItemMagic, byteWide);
    checkLevelUp_ = machine_.bindRoutine<Call>(kCheckLevelUp, byteWide);
    learnAbilities_ = machine_.bindRoutine<AbilitiesCall>(
        kLearnAbilities, {.inputs = {retropp::snes::C, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                          .isa = retropp::Isa::Wdc65816});
    learnRage_ = machine_.bindRoutine<Call>(kLearnRage, byteWide);
    execBtlGfx_ = machine_.bindRoutine<AbilitiesCall>(
        kExecBtlGfx, {.inputs = {retropp::snes::C, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                      .isa = retropp::Isa::Wdc65816});

    // The graphics program's handlers, found through its own table.
    const std::vector<std::uint8_t> graphicsTable = machine_.read(retropp::MemoryRegion{
        .at = kGraphicsCommandTable, .size = static_cast<std::uint32_t>(kGraphicsCommandHandlers.size() * 2)});
    std::array<std::uint32_t, kGraphicsCommandHandlers.size()> handlers{};
    for (std::size_t command = 0; command < handlers.size(); ++command) {
        const auto word = static_cast<std::uint16_t>(graphicsTable[command * 2] | graphicsTable[command * 2 + 1] << 8);
        if (word != kGraphicsCommandHandlers[command]) {
            throw std::runtime_error("the cartridge's table of graphics commands does not point where the US "
                                     "cartridges' does at command " + std::to_string(command));
        }
        handlers[command] = kGraphicsProgramBank | word;
    }
    addItemObtained_ = machine_.bindRoutine<Call>(retropp::snes::rtl(handlers[kAddItemObtained]), byteWide);
    mergeItemsObtained_ = machine_.bindRoutine<Call>(retropp::snes::rtl(handlers[kMergeItemsObtained]), byteWide);
    wrapUp_ = machine_.bindRoutine<Call>(kWrapUp, byteWide);
    winBattle_ = machine_.bindRoutine<Call>(kWinBattle, byteWide);
    loseBattle_ = machine_.bindRoutine<AbilitiesCall>(
        kLoseBattle, {.inputs = {retropp::snes::C, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                      .isa = retropp::Isa::Wdc65816});
    updateSram_ = machine_.bindRoutine<Call>(kUpdateSram, byteWide);
    decompCutsceneProg_ = machine_.bindRoutine<Call>(kDecompCutsceneProg, byteWide);
    battleMain_ = machine_.bindRoutine<Call>(retropp::snes::rtl(kBattleExt), byteWide);

    // While a reward routine runs, the graphics program's entry answers here: the command in the
    // accumulator, the cells the graphics program reads at their places.
    const auto cell = [](std::uint32_t at) { return retropp::Location::memory(kWorkRam | at); };
    machine_.registerEscapes(retropp::escapes(retropp::GuestEscape{
        .key = kGraphicsKey,
        .at = retropp::snes::rtl(kExecBtlGfxExt),
        .replaces = retropp::routine(
            retropp::RoutineBinding{.inputs = {retropp::snes::A, cell(0x0010), cell(0x2D6E), cell(0x2D72),
                                               cell(0x2E72), cell(0x2E76), cell(0x2F35), cell(0x2F39), cell(0x2F3D)}},
            [this](std::uint8_t command, std::uint8_t slot, std::uint32_t script, std::uint8_t scriptEnd,
                   std::uint32_t item, std::uint8_t itemLast, std::uint32_t first4, std::uint32_t middle4,
                   std::uint32_t last4) {
                GraphicsCall call{.command = command, .slot = slot};
                const auto unpack = [](auto& bytes, std::size_t from, std::uint32_t word) {
                    for (std::size_t i = 0; i < 4; ++i) {
                        bytes[from + i] = static_cast<std::uint8_t>(word >> (8 * i) & 0xFF);
                    }
                };
                unpack(call.script, 0, script);
                call.script[4] = scriptEnd;
                unpack(call.item, 0, item);
                call.item[4] = itemLast;
                unpack(call.messageVariables, 0, first4);
                unpack(call.messageVariables, 4, middle4);
                unpack(call.messageVariables, 8, last4);
                graphicsCalls_.push_back(call);
                answerGraphics(call);
            }),
        .armed = false}));
}

void DamageOracle::answerGraphics(const GraphicsCall& call) {
    answering_ = true;
    if (start_) {
        answerInBattle(call);
    }
    // The handlers are entered as the graphics program's entry leaves them: the command's place in its
    // table in X, the accumulator narrow and the index registers wide.
    const auto tableIndex = static_cast<std::uint16_t>(call.command * 2);
    switch (call.command) {
        case kAddItemObtained:
            addItemObtained_(tableIndex, kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
            break;
        case kMergeItemsObtained:
            mergeItemsObtained_(tableIndex, kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
            break;
        case kMenuQueuePosition:
            unansweredGraphics_ = call.command;
            break;
        default:
            // Every other command waits on the screen or the sound, or does nothing on a parked machine.
            break;
    }
    answering_ = false;
}

void DamageOracle::registerGate() {
    // The trace's watches already answer for their ranges and hand every access to noteGuestAccess();
    // the gate watches the rest of the battle's work RAM, gap by gap.
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> taken = DamageTrace::watchedRanges();
    const auto handler = [this](bool write) {
        return [this, write](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
            noteGuestAccess(kWorkRam | (at & 0xFFFF), value, write);
            return retropp::AccessVerdict::proceed();
        };
    };
    retropp::WatchMap gaps;
    std::uint32_t from = kGateStart;
    for (const auto& [start, end] : taken) {
        if (start > from) {
            gaps.declarations.push_back(retropp::GuestWatch{.key = "read-set gate " + std::to_string(from),
                                                            .at = retropp::MemoryRegion{.at = from, .size = start - from},
                                                            .onRead = handler(false),
                                                            .onWrite = handler(true)});
        }
        from = std::max(from, end);
    }
    if (from < kGateEnd) {
        gaps.declarations.push_back(retropp::GuestWatch{.key = "read-set gate " + std::to_string(from),
                                                        .at = retropp::MemoryRegion{.at = from, .size = kGateEnd - from},
                                                        .onRead = handler(false),
                                                        .onWrite = handler(true)});
    }
    // A whole battle's accesses beyond the battle's cells, from past the trace's last watch to the end
    // of the second bank of work RAM. The gaps above walk on past $7E:3FFF to the trace's last watch, and
    // they and the trace's own watches hand every access to noteGuestAccess().
    const std::uint32_t past = std::max(from, kGateEnd);
    if (past < kSecondWorkRamBankEnd) {
        gaps.declarations.push_back(retropp::GuestWatch{
            .key = "beyond the battle's cells",
            .at = retropp::MemoryRegion{.at = past, .size = kSecondWorkRamBankEnd - past},
            .onRead = [this](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
                noteAccessBeyond(at, value, false);
                return retropp::AccessVerdict::proceed();
            },
            .onWrite = [this](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
                noteAccessBeyond(at, value, true);
                return retropp::AccessVerdict::proceed();
            }});
    }
    machine_.registerWatches(gaps);
}

DamageOracle::~DamageOracle() = default;

void DamageOracle::start() {
    // The machine runs on the game's own tick, a microsecond at a time, until the reset escape
    // fires, and is parked there.
    machine_.run(retropp::Vm::Advance::OnTick);
    for (int step = 0; step < kStartStepsAllowed && !reset_; ++step) {
        machine_.advanceTick(kStartStep);
    }
    machine_.stop();
    if (!reset_) {
        throw std::runtime_error("the cartridge did not reach its reset code");
    }
}

void DamageOracle::call(const std::function<void()>& routine) {
    if (!reset_) {
        throw std::logic_error("the oracle's cartridge has not been started (DamageOracle::start)");
    }
    writes_.clear();
    trace_->forgetContextWrites();
    unwrittenReads_.clear();
    gateFresh_ = false;
    gateCalling_ = true;
    recording_ = true;
    routine();
    recording_ = false;
    gateCalling_ = false;
    gateFresh_ = true;
}

void runOnLoop(const std::function<void()>& work) {
    TickClock clock{retropp::TimingProfile::Snes.tickPeriod()};
    retropp::RunLoop loop{clock, retropp::TimingProfile::Snes};
    std::exception_ptr failure;
    bool done = false;
    loop.simTick([&](const retropp::InputState&) {
        if (!done) {
            done = true;
            try {
                work();
            } catch (...) {
                failure = std::current_exception();
            }
            loop.exitRequest();
        }
    });
    loop.renderLoop([] {});
    loop.exitAction([] { return retropp::ExitVerdict::Proceed; });
    loop.run();
    if (failure) {
        std::rethrow_exception(failure);
    }
}

std::uint16_t DamageOracle::magicDamage(std::uint8_t power, std::uint8_t magicStat, std::uint8_t level) {
    writeByte(kPower, power);
    writeByte(kAttackerStat, magicStat);
    writeByte(kAttackerLevel, level);
    writeWord(kDamage, 0);
    call([&] { calcMagicDmg_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    return readWord(kDamage);
}

std::uint16_t DamageOracle::calcDamage(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeContext(scenario);
    call([&] { calcDmg_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedRoutines[0]);
    return readWord(kDamage);
}

std::uint16_t DamageOracle::relicBonus(const DamageScenario& scenario, std::uint16_t damage) {
    writeScenario(scenario);
    writeWord(kDamage, damage);
    call([&] {
        relicDmgEffect_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage);
    });
    return readWord(kDamage);
}

MultiplierResult DamageOracle::applyMultiplier(const DamageScenario& scenario) {
    writeScenario(scenario);
    std::uint16_t damage = 0;
    call([&] {
        damage = applyDmgMult_(static_cast<std::uint16_t>(scenario.attack.damage), kWideAccumulatorStatus,
                               kBattleDataBank, kBattleDirectPage);
    });
    return MultiplierResult{.damage = damage, .stepsLeft = readByte(kMultiplier)};
}

TargetDamageResult DamageOracle::targetDamage(const DamageScenario& scenario, battle::BattlerSlot target) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeContext(scenario);
    call([&] {
        calcTargetDmg_(slotIndex(scenario.attacker), slotIndex(target), kWideAccumulatorStatus, kBattleDataBank,
                       kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[1]);
    return readTargetResult();
}

ApplyDamageResult DamageOracle::applyDamage(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeContext(scenario);
    call([&] {
        applyAndShowDmg_(slotIndex(scenario.attacker), kWideAccumulatorStatus, kBattleDataBank, kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[2]);
    return readApplyResult();
}

void DamageOracle::writeApplyCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    writeInFlightWord(kMissed, maskOf(attack.scratch.missed));
    writeWord(kInvincible, maskOf(state.invincible));
    writeWord(kGolemHp, state.golemHp);
    writeInFlightByte(kWeaponSpellcast, attack.scratch.weaponSpellcast.raw);
    writeByte(kBlackBeltTargets, static_cast<std::uint8_t>(~state.scratch.blackBeltEligible.characters.bits));
    writeInFlightByte(kQueueEnd, 0);
    writeInFlightByte(kNumeralBufferCount, 0);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        writeWord(kBattlerBit + at, maskOf(battle::BattlerSet::of(slot)));
        writeWord(kToUpdate + at, battler.pendingUpdates.enabledMagic ? kEnabledMagicBit : 0);
        writeByte(kLastDamagedBy + at,
                  battler.lastDamagedBy ? static_cast<std::uint8_t>(slotIndex(*battler.lastDamagedBy)) : kNoOne);
    }
}

ApplyDamageResult DamageOracle::readApplyResult(std::size_t firstNumeralCommand, std::uint32_t firstNumeralBuffer) {
    ApplyDamageResult result = readApplyCells();
    result.shown = readNumerals(firstNumeralCommand, firstNumeralBuffer);
    return result;
}

ApplyDamageResult DamageOracle::readApplyCells() {
    ApplyDamageResult result;
    const auto entry = [](std::uint16_t word) -> std::optional<std::uint16_t> {
        if (word == kNoDamage) {
            return std::nullopt;
        }
        return word;
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.hp[i] = readWord(kHp + at);
        result.mp[i] = readWord(kMp + at);
        result.statusesToSet[i] = StatusSet{{readByte(kToSet12 + at), readByte(kToSet12 + at + 1),
                                             readByte(kToSet34 + at), readByte(kToSet34 + at + 1)}};
        result.lastAttackerMark[i] = readByte(kLastAttacker + at);
        const std::uint8_t damager = readByte(kLastDamagedBy + at);
        if (damager != kNoOne) {
            result.lastDamagedBy[i] = battle::BattlerSlot::fromIndex(damager / 2);
        }
        result.enabledMagic[i] = (readByte(kToUpdate + at) & kEnabledMagicBit) != 0;
        result.ledger.taken[i] = entry(readWord(kTaken + at));
        result.ledger.healed[i] = entry(readWord(kHealed + at));
    }
    result.blackBeltEligible.characters =
        BattleSlotMask(static_cast<std::uint8_t>(~readByte(kBlackBeltTargets) & kPartyBits));
    result.missed = setOf(readWord(kMissed));
    result.golemHp = readWord(kGolemHp);
    result.weaponSpellcast = readByte(kWeaponSpellcast);
    return result;
}

std::vector<battle::DamageNumbersShown> DamageOracle::readNumerals(std::size_t firstCommand,
                                                                   std::uint32_t firstBuffer) {
    const auto numeral = [](battle::BattlerSlot slot, std::uint16_t word) {
        return battle::DamageNumber{.target = slot,
                                    .amount = static_cast<std::uint16_t>(word & kAmountBits),
                                    .miss = (word & kMissMark) != 0,
                                    .heals = (word & kHealMark) != 0};
    };
    std::vector<battle::DamageNumbersShown> shown;
    const std::uint8_t end = readByte(kQueueEnd);
    if (end == 0) {
        return shown;
    }
    const std::vector<std::uint8_t> queue = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = end});
    std::uint32_t buffers = firstBuffer;
    for (std::size_t at = firstCommand * kQueueCommandSize; at + kQueueCommandSize <= queue.size();
         at += kQueueCommandSize) {
        const std::uint8_t id = queue[at];
        if (id == kEveryNumeral) {
            battle::DamageNumbersShown together{.wait = battle::DamageNumberWait::UntilGone};
            const std::uint32_t buffer = kNumeralBuffer + kNumeralBufferSize * buffers++;
            for (int index = battle::kBattlerSlots - 1; index >= 0; --index) {
                const std::uint16_t word = readWord(buffer + static_cast<std::uint32_t>(index * 2));
                if (word != kNoDamage) {
                    together.numbers.push_back(numeral(battle::BattlerSlot::fromIndex(index), word));
                }
            }
            shown.push_back(std::move(together));
            continue;
        }
        if (id != kOneNumeral) {
            throw std::runtime_error("the apply-and-show routine wrote a command that is not a numeral: " +
                                     std::to_string(id));
        }
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(queue[at + 1]);
        const auto word = static_cast<std::uint16_t>(queue[at + 2] | queue[at + 3] << 8);
        // One pass shows its battlers last slot first, so a slot no lower than the one before begins
        // the second pass.
        const bool continues = !shown.empty() && shown.back().wait == battle::DamageNumberWait::CarryOn &&
                               !shown.back().numbers.empty() &&
                               shown.back().numbers.back().target.index() > slot.index();
        if (!continues) {
            shown.push_back(battle::DamageNumbersShown{.wait = battle::DamageNumberWait::CarryOn});
        }
        shown.back().numbers.push_back(numeral(slot, word));
    }
    return shown;
}

namespace {

// What the port's state and script hold after the damage is taken and shown.
ApplyDamageResult portApplied(const battle::BattleState& state, const battle::AttackInFlight& attack,
                              const battle::BattleScriptBuilder& script) {
    ApplyDamageResult result;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        result.hp[i] = battler.hp;
        result.mp[i] = battler.mp;
        result.statusesToSet[i] = battler.statusesToSet;
        const std::uint8_t mark = markByte(battler);
        result.lastAttackerMark[i] = mark;
        result.lastDamagedBy[i] = battler.lastDamagedBy;
        result.enabledMagic[i] = battler.pendingUpdates.enabledMagic;
    }
    result.blackBeltEligible = state.scratch.blackBeltEligible;
    result.missed = attack.scratch.missed;
    result.golemHp = state.golemHp;
    result.weaponSpellcast = attack.scratch.weaponSpellcast.raw;
    result.ledger = attack.scratch.ledger;
    for (const battle::BattleScriptCommand& command : script.commands()) {
        if (const auto* numerals = std::get_if<battle::DamageNumbersShown>(&command)) {
            result.shown.push_back(*numerals);
        }
    }
    return result;
}

std::string slotText(battle::BattlerSlot slot) {
    return std::to_string(slot.index());
}

std::string shownText(const battle::ReflectedAttackShown& shown) {
    std::string text = "bounced attack " + std::to_string(static_cast<int>(shown.attack)) + ":";
    for (std::size_t i = 0; i < shown.bouncedTo.size(); ++i) {
        if (shown.bouncedTo[i]) {
            text += " " + std::to_string(i) + ">" + slotText(*shown.bouncedTo[i]);
        }
    }
    text += " hit " + std::to_string(maskOf(shown.bouncesThatHit)) + " blocks";
    for (const battle::BlockShown block : shown.blocks) {
        text += " " + std::to_string(static_cast<int>(block));
    }
    return text;
}

std::string shownText(const battle::ScatteredHitsShown& shown) {
    std::string text = "scattered attack " + std::to_string(static_cast<int>(shown.attack)) + " from " +
                       slotText(shown.attacker) + ":";
    for (const battle::BattlerSlot target : shown.targets) {
        text += " " + slotText(target);
    }
    return text;
}

// A bounce off Reflect or a Launcher's hits as the queue's command and its params block hold them,
// as shownText() gives it.
std::string bounceText(std::span<const std::uint8_t> command, std::span<const std::uint8_t> block) {
    const auto attack = static_cast<AttackId>(command[2]);
    const std::uint16_t set = static_cast<std::uint16_t>(block[kBlockSet] | block[kBlockSet + 1] << 8);
    if (command[0] == kReflectedCommand) {
        battle::ReflectedAttackShown shown{.attack = attack};
        for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
            if (block[i] != kNoOne) {
                shown.bouncedTo[i] = battle::BattlerSlot::fromIndex(block[i]);
            }
        }
        shown.bouncesThatHit = setOf(set);
        for (std::size_t i = 0; i < battle::kPartySlots; ++i) {
            shown.blocks[i] = static_cast<battle::BlockShown>(block[kBlockBlocks + i]);
        }
        return shownText(shown);
    }
    if (command[0] == kScatteredCommand) {
        battle::ScatteredHitsShown shown{.attack = attack};
        for (int i = battle::kBattlerSlots - 1; i >= 0; --i) {
            if (block[static_cast<std::size_t>(i)] != kNoOne) {
                shown.targets.push_back(battle::BattlerSlot::fromIndex(block[static_cast<std::size_t>(i)]));
            }
        }
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            if (setOf(set).has(battle::BattlerSlot::fromIndex(index))) {
                shown.attacker = battle::BattlerSlot::fromIndex(index);
            }
        }
        return shownText(shown);
    }
    return "command " + std::to_string(command[0]);
}

}  // namespace

ApplyDamageResult portApplyDamage(DamageScenario scenario) {
    battle::BattleScriptBuilder script;
    battle::applyDamage(scenario.state, scenario.attack, scenario.attacker, script);
    return portApplied(scenario.state, scenario.attack, script);
}

ReflectedAttackResult portReflectedAttack(DamageScenario scenario) {
    battle::BattleScriptBuilder script;
    // The block before the bounce's: the frame's own, whose attacker is the caster, as
    // reflectedAttack() writes it.
    script.addParams(battle::AttackAnimationParams{.attacker = scenario.attacker});
    battle::BattleHooks hooks;
    battle::resolveReflectedAttack(scenario.state, hooks, script, scenario.attack, scenario.attacker);
    ReflectedAttackResult result;
    result.applied = portApplied(scenario.state, scenario.attack, script);
    for (const battle::BattleScriptCommand& command : script.commands()) {
        if (const auto* bounces = std::get_if<battle::ReflectedAttackShown>(&command)) {
            result.shownAs = shownText(*bounces);
        } else if (const auto* hits = std::get_if<battle::ScatteredHitsShown>(&command)) {
            result.shownAs = shownText(*hits);
        }
    }
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = scenario.state.battlers[i];
        result.statusesToClear[i] = battler.statusesToClear;
        if (battler.lastHit.command) {
            result.lastHitBy[i] = battler.lastHit.command->by;
        }
    }
    result.damage = scenario.attack.damage;
    result.canBeCritical = scenario.attack.canBeCritical;
    result.extraHits = scenario.state.extraHits;
    result.golemBlocked = scenario.attack.scratch.golemBlocked;
    result.interceptorBlocked = scenario.attack.scratch.interceptorBlocked;
    result.rngPosition = scenario.state.rng.position();
    return result;
}

ReflectedAttackResult DamageOracle::reflectedAttack(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeAimCells(scenario);
    call([&] {
        calcReflectDmg_(maskOf(scenario.attack.params.reflectedOff), slotIndex(scenario.attacker),
                        kWideAccumulatorStatus, kBattleDataBank, kBattleDirectPage);
    });
    return readReflectResult();
}

void DamageOracle::writeAimCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    const battle::AimingRules& rules = attack.rules;
    std::uint8_t aiming = 0;
    aiming |= rules.noCharacterTargets ? 0x80 : 0;
    aiming |= rules.randomTarget ? 0x40 : 0;
    aiming |= rules.fallBackToEarlierTargets ? 0x20 : 0;
    aiming |= rules.reflected ? 0x10 : 0;
    aiming |= rules.mayTargetTheFallen ? 0x08 : 0;
    aiming |= rules.keepTargetsThatAreGone ? 0x04 : 0;
    aiming |= rules.opposingSideOnly ? 0x02 : 0;
    aiming |= rules.excludeAttacker ? 0x01 : 0;
    writeInFlightByte(kAimingRules, aiming);
    writeInFlightWord(kTargets, maskOf(attack.targets));
    writeInFlightByte(kAimFlags, attack.scratch.aimExactly ? kAimExactlyBit : 0);
    writeInFlightWord(kEarlierTargets, maskOf(attack.scratch.earlierTargets));
    battle::BattlerSet enemies;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (state.at(battle::BattlerSlot::character(position)).actsAsEnemy) {
            enemies.add(battle::BattlerSlot::character(position));
        }
    }
    writeWord(kEnemyCharacters, maskOf(enemies));
    writeByte(kBattleType, static_cast<std::uint8_t>(state.battleType));
    writeByte(kPincerFirstSide, state.pincerSides[0].bits);
    writeByte(kPincerSecondSide, state.pincerSides[1].bits);

    // The reach check's four cells, written so that what they leave between them is the port's own
    // reachable set: on the field and counted in the battle, not hidden from aiming by its AI, and
    // for a party member not seized.
    battle::BattlerSet seized;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (state.at(battle::BattlerSlot::character(position)).seizedBy) {
            seized.add(battle::BattlerSlot::character(position));
        }
    }
    writeByte(kReachMonsters, 0xFF);
    writeByte(kReachCharacters, static_cast<std::uint8_t>(~seized.characters.bits));
    writeWord(kReachFirst, maskOf(state.targetableOrPresent & state.countedInBattle));
    writeWord(kReachSecond, static_cast<std::uint16_t>(~maskOf(state.aiUntargetable)));
    writeByte(kCountersEnabled, state.scratch.countersEnabled ? kCountersOn : 0);
    writeInFlightByte(kReachKeepsAway, attack.scratch.awayFromTheFieldUnreachable ? kAwayUnreachable : 0);
    writeWord(kAwayFromTheField, maskOf(state.awayFromTheField));

    writeInFlightByte(kTargetCount, static_cast<std::uint8_t>(attack.scratch.targetCount));
    writeInFlightByte(kQuietMisses, attack.scratch.quietMisses ? 0 : kNotQuiet);
    writeInFlightWord(kHitInTheBack, maskOf(attack.scratch.hitInTheBack));
    writeInFlightByte(kQueuedCommand, static_cast<std::uint8_t>(attack.queuedCommand));
    writeInFlightByte(kLastSpell, attack.scratch.lastSpell ? static_cast<std::uint8_t>(*attack.scratch.lastSpell) : kNoOne);
    writeInFlightByte(kLastItem, attack.scratch.lastItem ? static_cast<std::uint8_t>(*attack.scratch.lastItem) : kNoOne);
    writeByte(kExtraHits, state.extraHits ? static_cast<std::uint8_t>(*state.extraHits - 1) : kNoHitsToCome);
    // The block before the bounce's: the frame's own, whose attacker is the caster.
    writeInFlightByte(kNextParamsBlock, kParamsBlockSize);
    writeInFlightByte(kParamsBlocks + 1, static_cast<std::uint8_t>(scenario.attacker.index()));

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        writeByte(kOnTheField + at, battler.present ? 1 : 0);
        writeByte(kCharmedBy + at, battler.charmedBy ? static_cast<std::uint8_t>(slotIndex(*battler.charmedBy)) : kNoOne);
        writeByte(kStamina + at, battler.stamina);
        writeByte(kEvadeAndMagicBlock + at, battler.evadeFactor);
        writeByte(kEvadeAndMagicBlock + at + 1, battler.magicBlockFactor);
        writeByte(kBlockGraphics + at, battler.physicalBlockGraphics.bits);
        writeByte(kBlockGraphics + at + 1, battler.magicBlockGraphics.bits);
        writeByte(kRetort + at, battler.triggers.retort ? 1 : 0);
        const battle::LastHits& last = battler.lastHit;
        const auto by = [](const auto& hit) {
            return hit ? static_cast<std::uint8_t>(slotIndex(hit->by)) : kNoOne;
        };
        writeByte(kLastHitCommandBy + at, by(last.command));
        writeByte(kLastHitCommand + at, last.command ? static_cast<std::uint8_t>(last.command->with) : kNoOne);
        writeByte(kLastHitSpellBy + at, by(last.spell));
        writeByte(kLastHitSpell + at, last.spell ? static_cast<std::uint8_t>(last.spell->with) : kNoOne);
        writeByte(kLastHitItemBy + at, by(last.item));
        writeByte(kLastHitItem + at, last.item ? static_cast<std::uint8_t>(last.item->with) : kNoOne);
        writeByte(kLastHitElementBy + at, by(last.element));
        writeByte(kLastHitElement + at, last.element ? last.element->with.bits : 0);
    }
}

ReflectedAttackResult DamageOracle::readReflectResult() {
    ReflectedAttackResult result;
    result.applied = readApplyResult(1, 0);
    const auto slotByte = [](std::uint8_t value) -> std::optional<battle::BattlerSlot> {
        if (value == kNoOne) {
            return std::nullopt;
        }
        return battle::BattlerSlot::fromIndex(value / 2);
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.statusesToClear[i] = StatusSet{{readByte(kToClear12 + at), readByte(kToClear12 + at + 1),
                                               readByte(kToClear34 + at), readByte(kToClear34 + at + 1)}};
        result.lastHitBy[i] = slotByte(readByte(kLastHitCommandBy + at));
    }
    const std::vector<std::uint8_t> command = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize});
    const std::vector<std::uint8_t> block =
        machine_.read(retropp::MemoryRegion{.at = kParamsBlocks + kParamsBlockSize, .size = kParamsBlockSize});
    result.shownAs = bounceText(command, block);
    result.damage = readWord(kDamage);
    result.canBeCritical = (readByte(kFirstAttackFlags) & kCannotBeCriticalBit) == 0;
    const std::uint8_t hits = readByte(kExtraHits);
    if (hits != kNoHitsToCome) {
        result.extraHits = hits + 1;
    }
    result.golemBlocked = slotByte(readByte(kGolemBlock));
    result.interceptorBlocked = slotByte(readByte(kInterceptorBlock));
    result.rngPosition = readByte(kRngPosition);
    return result;
}

std::string differences(const ReflectedAttackResult& port, const ReflectedAttackResult& guest) {
    std::string out = differences(port.applied, guest.applied);
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    const auto slotName = [](const std::optional<battle::BattlerSlot>& slot) {
        return slot ? std::to_string(slot->index()) : std::string{"none"};
    };
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("statuses to clear" + slot, statuses(port.statusesToClear[i]), statuses(guest.statusesToClear[i]));
        note("last hit by" + slot, slotName(port.lastHitBy[i]), slotName(guest.lastHitBy[i]));
    }
    note("shown", port.shownAs, guest.shownAs);
    note("damage", std::to_string(port.damage), std::to_string(guest.damage));
    note("can be critical", std::to_string(port.canBeCritical), std::to_string(guest.canBeCritical));
    note("hits to come", port.extraHits ? std::to_string(*port.extraHits) : "none",
         guest.extraHits ? std::to_string(*guest.extraHits) : "none");
    note("Golem's block", slotName(port.golemBlocked), slotName(guest.golemBlocked));
    note("Interceptor's block", slotName(port.interceptorBlocked), slotName(guest.interceptorBlocked));
    note("random-number position", std::to_string(port.rngPosition), std::to_string(guest.rngPosition));
    return out;
}

std::string differences(const ApplyDamageResult& port, const ApplyDamageResult& guest) {
    std::string out;
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    const auto slotName = [](const std::optional<battle::BattlerSlot>& slot) {
        return slot ? std::to_string(slot->index()) : std::string{"none"};
    };
    const auto entry = [](const std::optional<std::uint16_t>& value) {
        return value ? std::to_string(*value) : std::string{"none"};
    };
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    const auto numerals = [](const std::vector<battle::DamageNumbersShown>& shown) {
        std::string text;
        for (const battle::DamageNumbersShown& one : shown) {
            text += one.wait == battle::DamageNumberWait::UntilGone ? "[together" : "[one at a time";
            for (const battle::DamageNumber& number : one.numbers) {
                text += " " + std::to_string(number.target.index()) + ":" +
                        (number.miss ? std::string{"miss"} : std::to_string(number.amount)) + (number.heals ? "+" : "");
            }
            text += "]";
        }
        return text;
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("HP" + slot, std::to_string(port.hp[i]), std::to_string(guest.hp[i]));
        note("MP" + slot, std::to_string(port.mp[i]), std::to_string(guest.mp[i]));
        note("statuses to set" + slot, statuses(port.statusesToSet[i]), statuses(guest.statusesToSet[i]));
        note("last attacker" + slot, std::to_string(port.lastAttackerMark[i]), std::to_string(guest.lastAttackerMark[i]));
        note("last damaged by" + slot, slotName(port.lastDamagedBy[i]), slotName(guest.lastDamagedBy[i]));
        note("castable spells to work out" + slot, std::to_string(port.enabledMagic[i]),
             std::to_string(guest.enabledMagic[i]));
        note("damage taken" + slot, entry(port.ledger.taken[i]), entry(guest.ledger.taken[i]));
        note("damage healed" + slot, entry(port.ledger.healed[i]), entry(guest.ledger.healed[i]));
    }
    note("Black Belt targets", std::to_string(maskOf(port.blackBeltEligible)),
         std::to_string(maskOf(guest.blackBeltEligible)));
    note("shown Miss", std::to_string(maskOf(port.missed)), std::to_string(maskOf(guest.missed)));
    note("the Golem's HP", std::to_string(port.golemHp), std::to_string(guest.golemHp));
    note("the weapon's spellcast", std::to_string(port.weaponSpellcast), std::to_string(guest.weaponSpellcast));
    note("numerals", numerals(port.shown), numerals(guest.shown));
    return out;
}

void DamageOracle::writeTargetCells(const DamageScenario& scenario) {
    const battle::AttackInFlight& attack = scenario.attack;
    const battle::BattleState& state = scenario.state;
    writeInFlightByte(kEffect, static_cast<std::uint8_t>(attack.effect.index() * 2));
    std::uint8_t first = kAttackFlagsAtStart;
    if (!attack.drainCapped) {
        first &= static_cast<std::uint8_t>(~kDrainCappedBit);
    }
    if (attack.canBeCritical) {
        first &= static_cast<std::uint8_t>(~kCannotBeCriticalBit);
    }
    writeInFlightByte(kFirstAttackFlags, first);
    writeInFlightByte(kAnimationCommand, kShowAnimationCommand);
    writeInFlightByte(kCommand, static_cast<std::uint8_t>(attack.command));
    writeInFlightByte(kAttackIndex, static_cast<std::uint8_t>(attack.attack));
    writeInFlightByte(kSecondary, attack.secondary);
    writeByte(kRngPosition, state.rng.position());
    writeInFlightByte(kDamageModification, attack.scratch.damageModification ? kOn : 0);
    const auto blockByte = [](const std::optional<battle::BattlerSlot>& slot) {
        return slot ? static_cast<std::uint8_t>(slotIndex(*slot)) : kNoOne;
    };
    writeInFlightByte(kGolemBlock, blockByte(attack.scratch.golemBlocked));
    writeInFlightByte(kInterceptorBlock, blockByte(attack.scratch.interceptorBlocked));
    writeByte(kForcefield, state.elementsNullifiedByForcefield.bits);
    writeInFlightByte(kAnimationPlace, 0);
    writeInFlightByte(kQueueEnd, kQueueCommandSize);
    const std::array<std::uint8_t, kQueueCommandSize> unwritten{kUnwritten, kUnwritten, kUnwritten, kUnwritten};
    writeInFlightBytes(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize}, unwritten);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        writeByte(kLevel + at, battler.level);
        writeByte(kDefenses + at, battler.defense);
        writeByte(kDefenses + at + 1, battler.magicDefense);
        writeWord(kHp + at, battler.hp);
        writeWord(kMp + at, battler.mp);
        writeWord(kMaxHp + at, battler.maxHp);
        writeWord(kMaxMp + at, battler.maxMp);
        writeByte(kAbsorbedNullified + at, battler.elementsAbsorbed.bits);
        writeByte(kAbsorbedNullified + at + 1, battler.elementsNullified.bits);
        writeByte(kWeakHalved + at, battler.elementsWeak.bits);
        writeByte(kWeakHalved + at + 1, battler.elementsHalved.bits);
        std::uint8_t flags = 0;
        flags |= battler.undead ? kUndeadBit : 0;
        flags |= battler.impCritical ? kImpCriticalBit : 0;
        flags |= battler.human ? kHumanBit : 0;
        flags |= battler.nameHidden ? kNameHiddenBit : 0;
        flags |= battler.diesAtZeroMp ? kDiesAtZeroMpBit : 0;
        writeByte(kMonsterFlags + at, flags);
        std::uint8_t stance = 0;
        stance |= battler.defending ? kDefendingBit : 0;
        stance |= battler.backRow ? kBackRowBit : 0;
        stance |= battler.instantDeathProtected ? kInstantDeathProtectedBit : 0;
        writeByte(kStance + at, stance);
        const auto writeStatuses = [&](std::uint32_t first12, std::uint32_t first34, const StatusSet& set) {
            writeByte(first12 + at, set.bytes[0]);
            writeByte(first12 + at + 1, set.bytes[1]);
            writeByte(first34 + at, set.bytes[2]);
            writeByte(first34 + at + 1, set.bytes[3]);
        };
        writeStatuses(kStatus12, kStatus34, battler.status);
        writeStatuses(kToSet12, kToSet34, battler.statusesToSet);
        writeStatuses(kToClear12, kToClear34, battler.statusesToClear);
        // The controller's slot times two, with the top bit set while the link is ending.
        writeByte(kController + at, battler.controlledBy ? static_cast<std::uint8_t>(slotIndex(*battler.controlledBy) |
                                                                                    (battler.controlEnding ? kTopBit : 0))
                                                         : kNoOne);
        const std::uint8_t mark = markByte(battler);
        writeByte(kLastAttacker + at, mark);
        writeInFlightWord(kTaken + at, attack.scratch.ledger.taken[i].value_or(kNoDamage));
        writeInFlightWord(kHealed + at, attack.scratch.ledger.healed[i].value_or(kNoDamage));
    }
}

TargetDamageResult DamageOracle::readTargetResult() {
    TargetDamageResult result;
    const auto entry = [](std::uint16_t word) -> std::optional<std::uint16_t> {
        if (word == kNoDamage) {
            return std::nullopt;
        }
        return word;
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.ledger.taken[i] = entry(readWord(kTaken + at));
        result.ledger.healed[i] = entry(readWord(kHealed + at));
        const auto readStatuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        result.statusesToSet[i] = readStatuses(kToSet12, kToSet34);
        result.statusesToClear[i] = readStatuses(kToClear12, kToClear34);
        result.controlled[i] = (readByte(kController + at) & kWaitingBit) == 0;
        result.lastAttackerMark[i] = readByte(kLastAttacker + at);
    }
    result.stepsLeft = readByte(kMultiplier);
    result.secondary = readByte(kSecondary);
    const std::vector<std::uint8_t> place = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize});
    if (std::any_of(place.begin(), place.end(), [](std::uint8_t b) { return b != kUnwritten; })) {
        result.animationRewrittenWith = place[kQueueCommandSize - 1];
    }
    result.rngPosition = readByte(kRngPosition);
    return result;
}

TargetDamageResult portTargetDamage(DamageScenario scenario, battle::BattlerSlot target) {
    battle::BattleScriptBuilder script;
    script.addAnimation(battle::AttackAnimation{.secondary = kUnwritten});
    battle::targetDamage(scenario.state, scenario.attack, scenario.attacker, target, script);

    TargetDamageResult result;
    result.ledger = scenario.attack.scratch.ledger;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = scenario.state.battlers[i];
        result.statusesToSet[i] = battler.statusesToSet;
        result.statusesToClear[i] = battler.statusesToClear;
        result.controlled[i] = battler.controlledBy && !battler.controlEnding;
        const std::uint8_t mark = markByte(battler);
        result.lastAttackerMark[i] = mark;
    }
    result.stepsLeft = scenario.attack.multiplier;
    result.secondary = scenario.attack.secondary;
    const auto* animation = std::get_if<battle::AttackAnimation>(&script.commands().front());
    if (animation == nullptr || animation->secondary != kUnwritten) {
        result.animationRewrittenWith = animation != nullptr ? animation->secondary : 0;
    }
    result.rngPosition = scenario.state.rng.position();
    return result;
}

std::string differences(const TargetDamageResult& port, const TargetDamageResult& guest) {
    std::string out;
    const auto entry = [](const std::optional<std::uint16_t>& value) {
        return value ? std::to_string(*value) : std::string{"none"};
    };
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("damage taken" + slot, entry(port.ledger.taken[i]), entry(guest.ledger.taken[i]));
        note("damage healed" + slot, entry(port.ledger.healed[i]), entry(guest.ledger.healed[i]));
        note("statuses to set" + slot, statuses(port.statusesToSet[i]), statuses(guest.statusesToSet[i]));
        note("statuses to clear" + slot, statuses(port.statusesToClear[i]), statuses(guest.statusesToClear[i]));
        note("controlled" + slot, std::to_string(port.controlled[i]), std::to_string(guest.controlled[i]));
        note("last attacker" + slot, std::to_string(port.lastAttackerMark[i]),
             std::to_string(guest.lastAttackerMark[i]));
    }
    note("multiplier steps", std::to_string(port.stepsLeft), std::to_string(guest.stepsLeft));
    note("animation second value", std::to_string(port.secondary), std::to_string(guest.secondary));
    note("animation rewritten with", port.animationRewrittenWith ? std::to_string(*port.animationRewrittenWith) : "none",
         guest.animationRewrittenWith ? std::to_string(*guest.animationRewrittenWith) : "none");
    note("random-number position", std::to_string(port.rngPosition), std::to_string(guest.rngPosition));
    return out;
}

void DamageOracle::writeScenario(const DamageScenario& scenario) {
    const battle::AttackInFlight& attack = scenario.attack;
    std::array<std::uint8_t, sizeof(AttackProperties)> record{};
    std::memcpy(record.data(), &attack.record, record.size());
    writeInFlightBytes(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(record.size())}, record);
    writeInFlightByte(kAttackerStat, attack.attackerStat);
    writeInFlightByte(kAttackerLevel, attack.attackerLevel);
    writeInFlightWord(kDamage, static_cast<std::uint16_t>(attack.damage));
    std::uint8_t flags = kAttackFlagsAtStart;
    if (attack.twoHanded) {
        flags &= static_cast<std::uint8_t>(~kNotTwoHanded);
    }
    if (!attack.ignoreDefenseStopsMultiplier) {
        flags &= static_cast<std::uint8_t>(~kIgnoreDefenseStopsMultiplier);
    }
    if (!attack.vanishApplies) {
        flags &= static_cast<std::uint8_t>(~kVanishAppliesBit);
    }
    writeInFlightByte(kAttackFlags, flags);
    writeInFlightByte(kMultiplier, static_cast<std::uint8_t>(attack.multiplier));

    for (std::size_t i = 0; i < scenario.state.battlers.size(); ++i) {
        const battle::Battler& battler = scenario.state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const auto first = static_cast<std::uint8_t>((battler.relicEffects1.bits & kLowSevenBits) |
                                                     (battler.relicEffects2.bits & kTopBit));
        writeByte(kRelicEffects1And3 + at, first);
        writeByte(kRelicEffects1And3 + at + 1, battler.relicEffects3.bits);
        writeByte(kRelicEffects4And5 + at, battler.relicEffects4.bits);
        writeByte(kRelicEffects4And5 + at + 1, battler.relicEffects5.bits);
    }
}

void DamageOracle::writeBytes(const retropp::MemoryRegion& place, std::span<const std::uint8_t> bytes) {
    writeCells(place, bytes, CellWrite::State);
}

void DamageOracle::writeInFlightBytes(const retropp::MemoryRegion& place, std::span<const std::uint8_t> bytes) {
    writeCells(place, bytes, CellWrite::InFlight);
}

void DamageOracle::writeInFlightByte(std::uint32_t at, std::uint8_t value) {
    const std::array<std::uint8_t, 1> bytes{value};
    writeCells(retropp::MemoryRegion{.at = at, .size = 1}, bytes, CellWrite::InFlight);
}

void DamageOracle::writeInFlightWord(std::uint32_t at, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value & 0xFF),
                                            static_cast<std::uint8_t>(value >> 8)};
    writeCells(retropp::MemoryRegion{.at = at, .size = 2}, bytes, CellWrite::InFlight);
}

void DamageOracle::writeCells(const retropp::MemoryRegion& place, std::span<const std::uint8_t> bytes,
                              CellWrite kind) {
    if (capture_ != nullptr) {
        if (kind == CellWrite::InFlight) {
            return;
        }
        for (std::uint32_t i = 0; i < place.size; ++i) {
            const auto [cell, laid] = capture_->cells.emplace(place.at + i, bytes[i]);
            if (kind == CellWrite::AsTheCartridgeHoldsIt) {
                capture_->disagreements.erase(place.at + i);
            } else if (!laid && cell->second != bytes[i]) {
                capture_->disagreements[place.at + i] |= static_cast<std::uint8_t>(cell->second ^ bytes[i]);
            }
            cell->second = bytes[i];
        }
        return;
    }
    machine_.write(place, bytes);
    if (start_ && start_->reachedTheLoop) {
        for (std::uint32_t cell = place.at; cell < place.at + place.size; ++cell) {
            writtenSincePoint_.insert(cell);
        }
    }
    // The first write after a call starts the next scenario's cells afresh.
    if (gateFresh_) {
        gateWritten_.clear();
        gateFresh_ = false;
    }
    for (std::uint32_t cell = place.at; cell < place.at + place.size; ++cell) {
        gateWritten_.insert(cell);
    }
}

void DamageOracle::noteAccessBeyond(std::uint32_t cell, std::uint8_t value, bool write) {
    if (!start_ || !gateCalling_ || answering_) {
        return;
    }
    if (write) {
        writtenBeyond_.insert(cell);
    } else if (!writtenBeyond_.contains(cell) && notComparedBits(cell) == 0) {
        start_->readsBeyondTheBattlesCells.push_back(UnwrittenRead{.at = cell, .value = value});
    }
}

void DamageOracle::noteGuestAccess(std::uint32_t cell, std::uint8_t value, bool write) {
    if (cell >= kGateEnd) {
        noteAccessBeyond(cell, value, write);
    }
    if (!gateCalling_) {
        return;
    }
    // A whole battle's gates: the battle program's writes up to the comparison point, and its reads
    // after it of a cell nothing has written since. The graphics
    // program's writes inside an answer count as writes since the point; its reads are its own.
    if (start_ && cell < kGateEnd) {
        StartResult& start = *start_;
        if (!start.reachedTheLoop) {
            if (write && !answering_) {
                start.battleWrites.insert(cell);
            }
        } else if (write) {
            writtenSincePoint_.insert(cell);
        } else if (!answering_ && !writtenSincePoint_.contains(cell)) {
            start.readsAfterThePoint.push_back(UnwrittenRead{.at = cell, .value = value});
        }
    }
    if (write) {
        gateWritten_.insert(cell);
    } else if (!gateWritten_.contains(cell)) {
        unwrittenReads_.emplace(cell, value);
    }
}

bool leftAsTheMachineHasIt(std::uint32_t cell) {
    return std::any_of(kCellsLeftAsTheMachineHasThem.begin(), kCellsLeftAsTheMachineHasThem.end(),
                       [cell](const CellsLeftAsTheMachineHasThem& row) {
                           return cell >= row.at && (cell - row.at) % row.stride == 0 &&
                                  (cell - row.at) / row.stride < row.count;
                       });
}

std::vector<UnwrittenRead> DamageOracle::unwrittenReads() const {
    std::vector<UnwrittenRead> reads;
    for (const auto& [cell, value] : unwrittenReads_) {
        reads.push_back(UnwrittenRead{.at = cell, .value = value});
    }
    return reads;
}

void DamageOracle::writeByte(std::uint32_t at, std::uint8_t value) {
    const std::array<std::uint8_t, 1> bytes{value};
    writeBytes(retropp::MemoryRegion{.at = at, .size = 1}, bytes);
}

void DamageOracle::writeWord(std::uint32_t at, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value & 0xFF),
                                            static_cast<std::uint8_t>(value >> 8)};
    writeBytes(retropp::MemoryRegion{.at = at, .size = 2}, bytes);
}

std::uint8_t DamageOracle::readByte(std::uint32_t at) {
    return machine_.read(retropp::MemoryRegion{.at = at, .size = 1})[0];
}

std::uint16_t DamageOracle::readWord(std::uint32_t at) {
    const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{.at = at, .size = 2});
    return static_cast<std::uint16_t>(bytes[0] | bytes[1] << 8);
}

DamageScenario drawScenario(std::mt19937& rng) {
    DamageScenario s;
    s.attacker = battle::BattlerSlot::fromIndex(static_cast<int>(rng() % battle::kBattlerSlots));
    s.attack.record.traits.bits = byte(rng);
    s.attack.record.flags1.bits = byte(rng);
    s.attack.record.flags2.bits = byte(rng);
    s.attack.record.power = byte(rng);
    s.attack.attackerStat = byte(rng);
    // A party member's level is 1 to 99; a monster's is whatever its record says.
    s.attack.attackerLevel = s.attacker.isCharacter() ? static_cast<std::uint8_t>(1 + rng() % 99) : byte(rng);
    s.attack.twoHanded = (rng() & 1) != 0;
    s.attack.ignoreDefenseStopsMultiplier = (rng() & 1) != 0;
    s.attack.multiplier = static_cast<int>(rng() % 8);
    s.attack.damage = static_cast<int>(rng() & 0xFFFF);
    battle::Battler& attacker = s.state.at(s.attacker);
    attacker.relicEffects1.bits = byte(rng);
    attacker.relicEffects2.bits = byte(rng);
    attacker.relicEffects3.bits = byte(rng);
    attacker.relicEffects4.bits = byte(rng);
    attacker.relicEffects5.bits = byte(rng);
    return s;
}

namespace {

// A party member's HP and MP never pass these.
constexpr int kPartyMaxHp = 9999;
constexpr int kPartyMaxMp = 999;
constexpr int kPartyMaxLevel = 99;
constexpr int kLedgerMax = 9999;
// No fractional attack or item in the game has a power above this.
constexpr int kMostFractionalPower = 16;

// True one time in `n`.
bool oneIn(std::mt19937& rng, unsigned n) {
    return rng() % n == 0;
}

// A byte with about a quarter of its bits set, so the flags it stands for come up one at a time.
std::uint8_t sparse(std::mt19937& rng) {
    return static_cast<std::uint8_t>(byte(rng) & byte(rng));
}

int upTo(std::mt19937& rng, int most) {
    return static_cast<int>(rng() % static_cast<unsigned>(most + 1));
}

battle::BattlerSlot anySlot(std::mt19937& rng) {
    return battle::BattlerSlot::fromIndex(static_cast<int>(rng() % battle::kBattlerSlots));
}

}  // namespace

TargetScenario drawTargetScenario(std::mt19937& rng) {
    TargetScenario drawn;
    DamageScenario& s = drawn.scenario;
    s.attacker = anySlot(rng);
    drawn.target = anySlot(rng);

    battle::AttackInFlight& attack = s.attack;
    attack.record.traits.bits = byte(rng);
    attack.record.flags1.bits = byte(rng);
    attack.record.flags2.bits = byte(rng);
    attack.record.element.bits = oneIn(rng, 2) ? 0 : byte(rng);
    attack.record.power = oneIn(rng, 16) ? 0 : byte(rng);
    if (attack.record.flags2.has(AttackFlag2::FRACTIONAL_DAMAGE)) {
        attack.record.power = static_cast<std::uint8_t>(upTo(rng, kMostFractionalPower));
    }
    for (std::uint8_t& status : attack.record.statuses.bytes) {
        status = sparse(rng);
    }
    attack.damage = oneIn(rng, 2) ? upTo(rng, 0x3FFF) : upTo(rng, battle::kDamageWordMax);
    attack.multiplier = upTo(rng, 7);
    attack.command = oneIn(rng, 8) ? BattleCommandId::ITEM : BattleCommandId::FIGHT;
    attack.attack = static_cast<AttackId>(byte(rng));
    attack.secondary = byte(rng);
    if (s.attacker.isCharacter() && oneIn(rng, 4)) {
        attack.effect = battle::SpecialEffect::of(WeaponSpecialEffect::ATMA_WEAPON);
    }
    attack.scratch.damageModification = !oneIn(rng, 4);
    attack.drainCapped = oneIn(rng, 2);
    attack.ignoreDefenseStopsMultiplier = oneIn(rng, 2);
    if (oneIn(rng, 8)) {
        attack.scratch.golemBlocked = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    if (oneIn(rng, 8)) {
        attack.scratch.interceptorBlocked = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }

    battle::BattleState& state = s.state;
    state.elementsNullifiedByForcefield.bits = oneIn(rng, 4) ? byte(rng) : 0;
    state.rng = battle::BattleRng{byte(rng)};
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const bool party = battle::BattlerSlot::fromIndex(static_cast<int>(i)).isCharacter();
        battler.level = party ? static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1)) : byte(rng);
        battler.maxHp = static_cast<std::uint16_t>(1 + upTo(rng, (party ? kPartyMaxHp : battle::kDamageWordMax) - 1));
        battler.hp = static_cast<std::uint16_t>(upTo(rng, battler.maxHp));
        battler.maxMp = static_cast<std::uint16_t>(upTo(rng, party ? kPartyMaxMp : battle::kDamageWordMax));
        battler.mp = static_cast<std::uint16_t>(upTo(rng, battler.maxMp));
        battler.defense = oneIn(rng, 8) ? 255 : byte(rng);
        battler.magicDefense = oneIn(rng, 8) ? 255 : byte(rng);
        battler.elementsAbsorbed.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.elementsNullified.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.elementsWeak.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.elementsHalved.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.undead = oneIn(rng, 4);
        battler.impCritical = oneIn(rng, 2);
        battler.human = oneIn(rng, 2);
        battler.nameHidden = oneIn(rng, 2);
        battler.diesAtZeroMp = oneIn(rng, 2);
        battler.defending = oneIn(rng, 2);
        battler.backRow = oneIn(rng, 2);
        for (std::uint8_t& status : battler.status.bytes) {
            status = sparse(rng);
        }
        for (std::uint8_t& status : battler.statusesToSet.bytes) {
            status = static_cast<std::uint8_t>(sparse(rng) & byte(rng));
        }
        for (std::uint8_t& status : battler.statusesToClear.bytes) {
            status = static_cast<std::uint8_t>(sparse(rng) & byte(rng));
        }
        if (oneIn(rng, 4)) {
            battler.controlledBy = anySlot(rng);
        }
        if (oneIn(rng, 2)) {
            battler.lastAttacker = anySlot(rng);
        }
        battler.retaliationPending = oneIn(rng, 2);
        if (oneIn(rng, 4)) {
            attack.scratch.ledger.taken[i] = static_cast<std::uint16_t>(upTo(rng, kLedgerMax));
        }
        if (oneIn(rng, 8)) {
            attack.scratch.ledger.healed[i] = static_cast<std::uint16_t>(upTo(rng, kLedgerMax));
        }
    }
    return drawn;
}

DamageScenario drawApplyScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    attack.record.flags1.bits = oneIn(rng, 3) ? static_cast<std::uint8_t>(AttackFlag1::AFFECT_MP) : 0;
    attack.scratch.ledger = {};
    attack.scratch.weaponSpellcast.raw = oneIn(rng, 2) ? byte(rng) : 0;
    state.golemHp = oneIn(rng, 2) ? static_cast<std::uint16_t>(upTo(rng, battle::kDamageWordMax)) : 0;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battle::Battler& battler = state.battlers[i];
        if (oneIn(rng, 4)) {
            battler.status.set(StatusId::ZOMBIE);
        }
        // Small pools often enough that a loss brings them to 0.
        if (oneIn(rng, 3)) {
            battler.hp = static_cast<std::uint16_t>(std::min<int>(battler.hp, upTo(rng, 64)));
            battler.mp = static_cast<std::uint16_t>(std::min<int>(battler.mp, upTo(rng, 64)));
        }
        if (oneIn(rng, 2)) {
            attack.scratch.ledger.taken[i] = static_cast<std::uint16_t>(oneIn(rng, 2) ? upTo(rng, 128) : upTo(rng, kLedgerMax));
        }
        if (oneIn(rng, 4)) {
            attack.scratch.ledger.healed[i] = static_cast<std::uint16_t>(oneIn(rng, 2) ? upTo(rng, 128) : upTo(rng, kLedgerMax));
        }
        if (oneIn(rng, 4)) {
            attack.scratch.missed.add(slot);
        }
        if (slot.isMonster() && oneIn(rng, 8)) {
            state.invincible.add(slot);
        }
        if (slot.isCharacter() && oneIn(rng, 4)) {
            state.scratch.blackBeltEligible.add(slot);
        }
        if (oneIn(rng, 4)) {
            battler.lastDamagedBy = anySlot(rng);
        }
        battler.pendingUpdates.enabledMagic = oneIn(rng, 4);
    }
    return s;
}

DamageScenario drawReflectScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    AttackProperties& record = attack.record;
    // A spell the status and special-effect passes change nothing for.
    record.traits.clear(AttackTrait::PHYSICAL);
    record.flags2.clear(AttackFlag2::REMOVE_STATUS);
    record.flags2.clear(AttackFlag2::TOGGLE_STATUS);
    record.misc.clear(AttackMiscFlag::MISS_IF_STATUS_IMMUNE);
    record.statuses = {};
    record.hitRate = byte(rng);
    attack.effect = {};
    // A bounce is a spell's, so the Golem and Interceptor, which step in only for Fight and
    // Capture, never block it.
    attack.command = BattleCommandId::MAGIC;
    attack.queuedCommand = attack.command;
    attack.scratch.ledger = {};
    attack.canBeCritical = oneIn(rng, 2);
    attack.vanishApplies = !oneIn(rng, 4);
    attack.scratch.quietMisses = oneIn(rng, 4);
    attack.scratch.aimExactly = oneIn(rng, 4);
    attack.scratch.targetCount = 1 + upTo(rng, 4);
    attack.scratch.weaponSpellcast.raw = oneIn(rng, 2) ? byte(rng) : 0;
    if (oneIn(rng, 2)) {
        attack.scratch.lastSpell = static_cast<AttackId>(byte(rng));
    }
    if (oneIn(rng, 4)) {
        attack.scratch.lastItem = static_cast<ItemId>(byte(rng));
    }
    const auto anySet = [&] {
        battle::BattlerSet set;
        set.characters = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x0F));
        set.monsters = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F));
        return set;
    };
    attack.targets = anySet();
    attack.scratch.earlierTargets = anySet();
    attack.scratch.hitInTheBack = oneIn(rng, 4) ? anySet() : battle::BattlerSet{};
    battle::AimingRules& rules = attack.rules;
    rules.noCharacterTargets = oneIn(rng, 8);
    rules.fallBackToEarlierTargets = oneIn(rng, 4);
    rules.mayTargetTheFallen = oneIn(rng, 4);
    rules.keepTargetsThatAreGone = oneIn(rng, 2);
    rules.opposingSideOnly = oneIn(rng, 2);
    rules.excludeAttacker = oneIn(rng, 8);
    // The hits to come and the battlers the spell bounced off are drawn apart: a Launcher's own
    // bounce is off its attacker, and a count left standing meets whatever bounce comes next.
    if (oneIn(rng, 3)) {
        state.extraHits = 1 + upTo(rng, 7);
    }
    if (oneIn(rng, 3)) {
        attack.params.reflectedOff = battle::BattlerSet::of(s.attacker);
    } else {
        attack.params.reflectedOff = anySet();
        if (attack.params.reflectedOff.empty()) {
            attack.params.reflectedOff.add(anySlot(rng));
        }
    }

    state.battleType = static_cast<battle::BattleType>(upTo(rng, 3));
    state.pincerSides[0] = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F));
    state.pincerSides[1] = BattleSlotMask(static_cast<std::uint8_t>(~state.pincerSides[0].bits & 0x3F));
    state.targetableOrPresent = oneIn(rng, 4) ? anySet() : battle::BattlerSet::allCharacters() | battle::BattlerSet::allMonsters();
    state.aiUntargetable = oneIn(rng, 8) ? anySet().monstersOnly() : battle::BattlerSet{};
    state.awayFromTheField = oneIn(rng, 8) ? anySet() : battle::BattlerSet{};
    state.scratch.countersEnabled = !oneIn(rng, 4);
    attack.scratch.awayFromTheFieldUnreachable = !oneIn(rng, 4);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battle::Battler& battler = state.battlers[i];
        battler.present = !oneIn(rng, 6);
        battler.status.clear(StatusId::INVISIBLE);
        battler.status.clear(StatusId::FROZEN);
        if (oneIn(rng, 3)) {
            battler.status.set(StatusId::REFLECT);
        }
        battler.stamina = byte(rng);
        battler.evadeFactor = byte(rng);
        battler.magicBlockFactor = byte(rng);
        // The four blocks equipment can show: a dagger's, a sword's, a shield's and the Zephyr Cape's.
        battler.physicalBlockGraphics.bits = oneIn(rng, 2) ? static_cast<std::uint8_t>(sparse(rng) & kEquipmentBlocks) : 0;
        battler.magicBlockGraphics.bits = oneIn(rng, 2) ? static_cast<std::uint8_t>(sparse(rng) & kEquipmentBlocks) : 0;
        battler.instantDeathProtected = oneIn(rng, 4);
        battler.triggers.retort = oneIn(rng, 4);
        if (oneIn(rng, 8)) {
            battler.charmedBy = anySlot(rng);
        }
        if (slot.isCharacter()) {
            battler.actsAsEnemy = oneIn(rng, 8);
            if (oneIn(rng, 16)) {
                battler.seizedBy = battle::BattlerSlot::monster(upTo(rng, battle::kMonsterSlots - 1));
            }
        }
        if (oneIn(rng, 2)) {
            battler.lastHit.command = battle::Hit<BattleCommandId>{.by = anySlot(rng), .with = BattleCommandId::FIGHT};
        }
    }
    return s;
}

std::string describe(const TargetScenario& drawn) {
    const DamageScenario& s = drawn.scenario;
    const battle::AttackInFlight& a = s.attack;
    const battle::Battler& target = s.state.at(drawn.target);
    return "attacker " + std::to_string(s.attacker.index()) + " target " + std::to_string(drawn.target.index()) +
           " traits " + std::to_string(a.record.traits.bits) + " flags1 " + std::to_string(a.record.flags1.bits) +
           " flags2 " + std::to_string(a.record.flags2.bits) + " element " + std::to_string(a.record.element.bits) +
           " power " + std::to_string(a.record.power) + " damage " + std::to_string(a.damage) + " steps " +
           std::to_string(a.multiplier) + " command " + std::to_string(static_cast<int>(a.command)) + " effect " +
           std::to_string(a.effect.index()) + " modification " + std::to_string(a.scratch.damageModification) +
           " capped " + std::to_string(a.drainCapped) + " rng " + std::to_string(s.state.rng.position()) +
           " target hp " + std::to_string(target.hp) + "/" + std::to_string(target.maxHp) + " mp " +
           std::to_string(target.mp) + "/" + std::to_string(target.maxMp) + " defenses " +
           std::to_string(target.defense) + "/" + std::to_string(target.magicDefense) + " undead " +
           std::to_string(target.undead);
}

std::string describe(const DamageScenario& s) {
    const battle::Battler& attacker = s.state.at(s.attacker);
    return "attacker " + std::to_string(s.attacker.index()) + " traits " + std::to_string(s.attack.record.traits.bits) +
           " flags1 " + std::to_string(s.attack.record.flags1.bits) + " flags2 " +
           std::to_string(s.attack.record.flags2.bits) + " power " + std::to_string(s.attack.record.power) + " stat " +
           std::to_string(s.attack.attackerStat) + " level " + std::to_string(s.attack.attackerLevel) +
           " two-handed " + std::to_string(s.attack.twoHanded) + " stops " +
           std::to_string(s.attack.ignoreDefenseStopsMultiplier) + " steps " + std::to_string(s.attack.multiplier) +
           " damage " + std::to_string(s.attack.damage) + " relics " + std::to_string(attacker.relicEffects1.bits) +
           "/" + std::to_string(attacker.relicEffects2.bits) + "/" + std::to_string(attacker.relicEffects3.bits) + "/" +
           std::to_string(attacker.relicEffects4.bits) + "/" + std::to_string(attacker.relicEffects5.bits);
}

// --- the trace ---------------------------------------------------------------------------------

namespace {

// The attack's context read at each traced entry: the params block, the hits to come, the aiming
// rules, where the next params block goes in the script's buffer, and the buffer itself.
struct ContextCells {
    retropp::MemoryRegion params;
    retropp::MemoryRegion hits;
    retropp::MemoryRegion rules;
    retropp::MemoryRegion nextBlock;
    retropp::MemoryRegion blocks;
    retropp::MemoryRegion message;
    retropp::MemoryRegion messageVariables;
};

// The battle program's half of work RAM, whole, for a snapshot.
struct TraceRam {
    retropp::MemoryRegion bytes;
};
constexpr std::uint32_t kTraceRam = 0x7E0000;
constexpr std::uint32_t kTraceRamSize = 0x10000;

// The message ShowMsg shows, in the battle script's second byte, and the four message variables.
constexpr std::uint32_t kShownMessage = 0x7E2D6F;
constexpr std::uint32_t kShownVariables = 0x7E2F35;
constexpr std::uint32_t kShownVariablesSize = 12;
constexpr std::uint32_t kVariableBytes = 3;

// A character record's first cell, its size, and where its actor and level are.
constexpr std::uint32_t kTracedRecords = 0x7E1600;
constexpr std::uint32_t kTracedRecordSize = 37;
constexpr std::uint32_t kTracedRecordCount = 16;
constexpr std::uint32_t kRecordActorByte = 0;
constexpr std::uint32_t kRecordLevelByte = 8;

// The routine whose entry logs the actor and level read before it.
constexpr std::string_view kLearnAbilitiesName = "LearnAbilities";

// The parts of kEndingRanges no range of kTracedRanges or kPartyRecordRanges covers, as [first, end).
std::vector<std::pair<std::uint32_t, std::uint32_t>> endingGaps() {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> covered;
    for (const TracedRange& range : kTracedRanges) {
        covered.emplace_back(range.at, range.at + range.size);
    }
    for (const TracedRange& range : kPartyRecordRanges) {
        covered.emplace_back(range.at, range.at + range.size);
    }
    std::sort(covered.begin(), covered.end());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> gaps;
    for (const TracedRange& range : kEndingRanges) {
        std::uint32_t from = range.at;
        const std::uint32_t end = range.at + range.size;
        for (const auto& [start, stop] : covered) {
            if (stop <= from || start >= end) {
                continue;
            }
            if (start > from) {
                gaps.emplace_back(from, start);
            }
            from = std::max(from, stop);
        }
        if (from < end) {
            gaps.emplace_back(from, end);
        }
    }
    std::sort(gaps.begin(), gaps.end());
    return gaps;
}

constexpr std::uint32_t kParamsCells = 0x7E00A0;
// The script queue's whole span, and the aiming rule set while a spell bounces off Reflect.
constexpr std::uint32_t kQueueSpan = 256;
constexpr std::uint8_t kReflectedRule = 0x10;
constexpr std::uint32_t kParamsSize = 16;
constexpr std::uint32_t kBlocksSize = 256;
// The attacker is the second byte of a params block.
constexpr std::uint32_t kBlockAttacker = 1;

std::string busAddress(std::uint32_t at) {
    char text[12];
    std::snprintf(text, sizeof text, "$%02X:%04X", (at >> 16) & 0xFF, at & 0xFFFF);
    return text;
}

std::string hexByte(std::uint8_t value) {
    char text[4];
    std::snprintf(text, sizeof text, "%02X", value);
    return text;
}

std::string framePrefix(std::uint64_t frame) {
    return "f" + std::to_string(frame);
}

}  // namespace

// A running machine answers a read with what its last completed step published, which misses what the
// step under way has written. `written` holds the last byte the guest wrote to each cell the trace
// watches, and a read lays those over the published bytes. `actorRead` and `levelRead` are the last
// actor byte and level byte the guest read from a character record.
struct DamageTrace::Places {
    retropp::RegionMapId<ContextCells> id;
    retropp::RegionMapId<TraceRam> ram;
    std::map<std::uint32_t, std::uint8_t> written{};
    std::optional<std::uint8_t> actorRead;
    std::optional<std::uint8_t> levelRead;

    std::vector<std::uint8_t> overlay(std::vector<std::uint8_t> bytes, std::uint32_t at) const {
        const std::uint32_t end = at + static_cast<std::uint32_t>(bytes.size());
        for (auto cell = written.lower_bound(at); cell != written.end() && cell->first < end; ++cell) {
            bytes[cell->first - at] = cell->second;
        }
        return bytes;
    }

    std::vector<std::uint8_t> read(retropp::Vm& vm, retropp::MemoryRegion ContextCells::*key, std::uint32_t at) const {
        return overlay(vm.read(id, key), at);
    }

    std::vector<std::uint8_t> readRam(retropp::Vm& vm) const {
        return overlay(vm.read(ram, &TraceRam::bytes), kTraceRam);
    }
};

std::vector<std::pair<std::uint32_t, std::uint32_t>> DamageTrace::watchedRanges() {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges = endingGaps();
    for (const TracedRange& range : kTracedRanges) {
        ranges.emplace_back(range.at, range.at + range.size);
    }
    for (const TracedRange& range : kPartyRecordRanges) {
        ranges.emplace_back(range.at, range.at + range.size);
    }
    std::sort(ranges.begin(), ranges.end());
    return ranges;
}

void DamageTrace::logSnapshot(retropp::Vm& vm, const std::string& routine, const char* how, std::uint32_t at) {
    const std::string prefix = framePrefix(frame_()) + " snapshot " + routine;
    line_(prefix + " " + how + " " + busAddress(at));
    if (routine == kSnapshotRoutines[1].name && std::string_view{how} == "enter" && lossMessage_) {
        line_(prefix + " handed " + (*lossMessage_ ? hexByte(**lossMessage_) : std::string{"--"}));
        lossMessage_.reset();
    }
    const std::vector<std::uint8_t> ram = places_->readRam(vm);
    for (const TracedRange& range : kEndingRanges) {
        std::string text = prefix + " " + busAddress(range.at) + " ";
        text.reserve(text.size() + 2 * range.size);
        for (std::uint32_t i = 0; i < range.size; ++i) {
            text += hexByte(ram[range.at - kTraceRam + i]);
        }
        line_(text);
    }
}

DamageTrace::DamageTrace(retropp::Vm& machine, Line line, Frame frame, Access access)
    : vm_(&machine), line_(std::move(line)), frame_(std::move(frame)) {
    places_ = std::make_unique<Places>(Places{
        .id = machine.registerRegions(retropp::regions(
            retropp::region(&ContextCells::params, retropp::MemoryRegion{.at = kParamsCells, .size = kParamsSize},
                            "params block"),
            retropp::region(&ContextCells::hits, retropp::MemoryRegion{.at = kExtraHits, .size = 1}, "hits to come"),
            retropp::region(&ContextCells::rules, retropp::MemoryRegion{.at = kAimingRules, .size = 1}, "aiming rules"),
            retropp::region(&ContextCells::nextBlock, retropp::MemoryRegion{.at = kNextParamsBlock, .size = 1},
                            "next params block"),
            retropp::region(&ContextCells::blocks, retropp::MemoryRegion{.at = kParamsBlocks, .size = kBlocksSize},
                            "params blocks"),
            retropp::region(&ContextCells::message, retropp::MemoryRegion{.at = kShownMessage, .size = 1},
                            "message shown"),
            retropp::region(&ContextCells::messageVariables,
                            retropp::MemoryRegion{.at = kShownVariables, .size = kShownVariablesSize},
                            "message variables"))),
        .ram = machine.registerRegions(retropp::regions(retropp::region(
            &TraceRam::bytes, retropp::MemoryRegion{.at = kTraceRam, .size = kTraceRamSize}, "battle's work RAM")))});

    retropp::EscapeMap escapes;
    // Each return site with the routines whose calls return there, in the order kTracedRoutines
    // names them, and the caller it is for each.
    std::map<std::uint32_t, std::vector<std::pair<TracedRoutine, const char*>>> sites;
    for (const TracedRoutine& routine : kTracedRoutines) {
        escapes.declarations.push_back(retropp::GuestEscape{
            .key = std::string{routine.name} + " entry",
            .at = routine.entry,
            .handler = [this, routine](retropp::Vm& vm, std::uint32_t at) {
                ++open_;
                ++openCalls_[routine.name];
                partyRecordsOpen_ = routine.partyRecords;
                const std::string prefix = framePrefix(frame_());
                line_(prefix + " enter " + routine.name + " " + busAddress(at));
                const std::vector<std::uint8_t> params = places_->read(vm, &ContextCells::params, kParamsCells);
                const std::uint8_t hits = places_->read(vm, &ContextCells::hits, kExtraHits)[0];
                const std::uint8_t rules = places_->read(vm, &ContextCells::rules, kAimingRules)[0];
                const std::uint8_t next = places_->read(vm, &ContextCells::nextBlock, kNextParamsBlock)[0];
                std::string context = prefix + " params";
                for (const std::uint8_t byte : params) {
                    context += " " + hexByte(byte);
                }
                context += " hits " + hexByte(hits) + " rules " + hexByte(rules) + " previous ";
                if (next >= kParamsBlockSize) {
                    const std::vector<std::uint8_t> blocks = places_->read(vm, &ContextCells::blocks, kParamsBlocks);
                    context += hexByte(blocks[next - kParamsBlockSize + kBlockAttacker]);
                } else {
                    context += "--";
                }
                line_(context);
                if (routine.name == kLearnAbilitiesName) {
                    if (places_->actorRead && places_->levelRead) {
                        line_(prefix + " abilities actor " + hexByte(*places_->actorRead) + " level " +
                              hexByte(*places_->levelRead));
                    } else {
                        line_(prefix + " abilities --");
                    }
                }
            }});
        for (const std::uint32_t site :
             {routine.frameReturn, routine.bounceReturn, routine.otherReturns[0], routine.otherReturns[1]}) {
            if (site == 0) {
                continue;
            }
            const char* caller = site == routine.frameReturn    ? " frame return"
                                 : site == routine.bounceReturn ? " bounce return"
                                 : site == routine.otherReturns[0] ? " return 1"
                                                                   : " return 2";
            sites[site].emplace_back(routine, caller);
        }
    }
    for (const auto& [site, served] : sites) {
        std::string key = served.front().first.name + std::string{served.front().second};
        for (std::size_t i = 1; i < served.size(); ++i) {
            key += std::string{", "} + served[i].first.name;
        }
        escapes.declarations.push_back(retropp::GuestEscape{
            .key = key,
            .at = site,
            .handler = [this, served](retropp::Vm&, std::uint32_t at) {
                for (const auto& [routine, caller] : served) {
                    const auto open = openCalls_.find(routine.name);
                    if (open == openCalls_.end() || open->second == 0) {
                        continue;
                    }
                    --open->second;
                    if (open_ > 0) {
                        --open_;
                    }
                    partyRecordsOpen_ = false;
                    line_(framePrefix(frame_()) + " return " + routine.name + " " + busAddress(at));
                    return;
                }
            }});
    }
    for (const SnapshotRoutine& routine : kSnapshotRoutines) {
        const std::string name = routine.name;
        escapes.declarations.push_back(retropp::GuestEscape{
            .key = name + " snapshot entry",
            .at = routine.entry,
            .handler = [this, name](retropp::Vm& vm, std::uint32_t at) {
                ++openSnapshots_;
                ++openSnapshotCalls_[name];
                logSnapshot(vm, name, "enter", at);
            }});
        for (std::size_t i = 0; i < routine.returns.size() && routine.returns[i] != 0; ++i) {
            escapes.declarations.push_back(retropp::GuestEscape{
                .key = name + " snapshot return " + std::to_string(i + 1),
                .at = routine.returns[i],
                .handler = [this, name](retropp::Vm& vm, std::uint32_t at) {
                    const auto open = openSnapshotCalls_.find(name);
                    if (open == openSnapshotCalls_.end() || open->second == 0) {
                        return;
                    }
                    --open->second;
                    --openSnapshots_;
                    logSnapshot(vm, name, "return", at);
                }});
        }
    }
    escapes.declarations.push_back(retropp::GuestEscape{
        .key = "message shown",
        .at = kShowMessageCall,
        .handler = [this](retropp::Vm& vm, std::uint32_t) {
            if (open_ == 0 && openSnapshots_ == 0) {
                return;
            }
            const std::uint8_t message = places_->read(vm, &ContextCells::message, kShownMessage)[0];
            const std::vector<std::uint8_t> variables =
                places_->read(vm, &ContextCells::messageVariables, kShownVariables);
            std::string text = framePrefix(frame_()) + " message " + hexByte(message);
            for (std::uint32_t v = 0; v < kShownVariablesSize; v += kVariableBytes) {
                text += " " + hexByte(variables[v + 2]) + hexByte(variables[v + 1]) + hexByte(variables[v]);
            }
            line_(text);
        }});
    machine.registerEscapes(escapes);

    // A watch names the address the access drove; a direct-page access drives $00:00xx, and the same
    // byte of work RAM is logged under its $7E address so every line names one cell one way.
    const auto watched = [this, access](char how, bool partyRecords = false, bool logged = true) {
        return [this, access, how, partyRecords, logged](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
            const std::uint32_t cell = kWorkRam | (at & 0xFFFF);
            if (how == 'w') {
                places_->written[cell] = value;
            } else if (cell >= kTracedRecords && cell < kTracedRecords + kTracedRecordSize * kTracedRecordCount) {
                const std::uint32_t byte = (cell - kTracedRecords) % kTracedRecordSize;
                if (byte == kRecordActorByte) {
                    places_->actorRead = value;
                } else if (byte == kRecordLevelByte) {
                    places_->levelRead = value;
                }
            }
            if (access) {
                access(cell, value, how == 'w');
            }
            if (logged && open_ > 0 && (!partyRecords || partyRecordsOpen_)) {
                char text[48];
                std::snprintf(text, sizeof text, " %c $7E:%04X %02X", how, cell & 0xFFFF, value);
                line_(framePrefix(frame_()) + text);
            }
            return retropp::AccessVerdict::proceed();
        };
    };
    retropp::WatchMap watches;
    for (const TracedRange& range : kTracedRanges) {
        watches.declarations.push_back(retropp::GuestWatch{.key = range.key,
                                                           .at = retropp::MemoryRegion{.at = range.at, .size = range.size},
                                                           .onRead = watched('r'),
                                                           .onWrite = watched('w')});
    }
    for (const TracedRange& range : kPartyRecordRanges) {
        watches.declarations.push_back(retropp::GuestWatch{.key = range.key,
                                                           .at = retropp::MemoryRegion{.at = range.at, .size = range.size},
                                                           .onRead = watched('r', true),
                                                           .onWrite = watched('w', true)});
    }
    for (const auto& [first, end] : endingGaps()) {
        watches.declarations.push_back(retropp::GuestWatch{.key = "ending cells " + busAddress(first),
                                                           .at = retropp::MemoryRegion{.at = first, .size = end - first},
                                                           .onRead = watched('r', false, false),
                                                           .onWrite = watched('w', false, false)});
    }
    machine.registerWatches(watches);
}

DamageTrace::~DamageTrace() = default;

void DamageTrace::forgetContextWrites() {
    places_->written.clear();
    places_->actorRead.reset();
    places_->levelRead.reset();
}

void DamageTrace::noteWrite(std::uint32_t at, std::uint8_t value) {
    places_->written[at] = value;
}

void DamageTrace::handOverLossMessage(std::optional<std::uint8_t> message) {
    lossMessage_ = message;
}

void DamageTrace::readForAbilities(std::uint8_t actor, std::uint8_t level) {
    places_->actorRead = actor;
    places_->levelRead = level;
}

void DamageTrace::returnFromSnapshot(const SnapshotRoutine& routine) {
    const auto open = openSnapshotCalls_.find(routine.name);
    if (open == openSnapshotCalls_.end() || open->second == 0) {
        return;
    }
    --open->second;
    --openSnapshots_;
    logSnapshot(*vm_, routine.name, "return", routine.returns[0]);
}

void DamageTrace::returnToFrame(const TracedRoutine& routine) {
    if (open_ > 0) {
        --open_;
    }
    if (const auto open = openCalls_.find(routine.name); open != openCalls_.end() && open->second > 0) {
        --open->second;
    }
    partyRecordsOpen_ = false;
    line_(framePrefix(frame_()) + " return " + routine.name + " " + busAddress(routine.frameReturn));
}

namespace {

// The target of an attack's statuses on one target, as its call shows it: the one battler whose cells
// it touched.
std::optional<battle::BattlerSlot> statusCallTarget(const TracedCall& call);

}  // namespace

std::vector<TracedCall> parseTrace(std::istream& log) {
    std::vector<TracedCall> calls;
    std::optional<TracedCall> open;
    std::set<std::uint32_t> written;
    // The attacker of the params block at the last attacker effect's entry.
    std::optional<std::uint8_t> attackerEffectMaker;
    std::string text;
    int number = 0;
    const auto fail = [&](const std::string& why) {
        throw std::runtime_error("trace line " + std::to_string(number) + ": " + why + ": " + text);
    };
    const auto hex = [&](const std::string& word, std::uint32_t most) {
        std::size_t used = 0;
        unsigned long value = 0;
        try {
            value = std::stoul(word, &used, 16);
        } catch (const std::exception&) {
            fail("not a hex number");
        }
        if (used != word.size() || value > most) {
            fail("not a hex number");
        }
        return static_cast<std::uint32_t>(value);
    };
    // "$7E:3BF4" or "$C2:0B83" as one address.
    const auto address = [&](const std::string& word) {
        if (word.size() != 8 || word[0] != '$' || word[3] != ':') {
            fail("not an address");
        }
        return hex(word.substr(1, 2), 0xFF) << 16 | hex(word.substr(4), 0xFFFF);
    };
    while (std::getline(log, text)) {
        ++number;
        if (text.size() < 2 || text[0] != 'f' || !std::isdigit(static_cast<unsigned char>(text[1]))) {
            continue;
        }
        std::istringstream in{text};
        std::string frameWord;
        std::string kind;
        in >> frameWord >> kind;
        std::uint64_t frame = 0;
        try {
            frame = std::stoull(frameWord.substr(1));
        } catch (const std::exception&) {
            fail("not a frame");
        }
        if (kind == "enter") {
            std::string name;
            std::string at;
            in >> name >> at;
            address(at);
            open = TracedCall{.routine = name, .frame = frame};
            open->attackerEffectMaker = attackerEffectMaker;
            if (name == kTracedRoutines[6].name && !calls.empty() && calls.back().routine == kTracedRoutines[4].name) {
                open->target = statusCallTarget(calls.back());
            }
            written.clear();
        } else if (kind == "params") {
            std::array<std::uint8_t, 16> params{};
            for (std::uint8_t& byte : params) {
                std::string word;
                in >> word;
                byte = static_cast<std::uint8_t>(hex(word, 0xFF));
            }
            std::string hitsWord, hits, rulesWord, rules, previousWord, previous;
            in >> hitsWord >> hits >> rulesWord >> rules >> previousWord >> previous;
            if (hitsWord != "hits" || rulesWord != "rules" || previousWord != "previous") {
                fail("not a params line");
            }
            if (open) {
                open->params = params;
                open->hitsToCome = static_cast<std::uint8_t>(hex(hits, 0xFF));
                open->aimingRules = static_cast<std::uint8_t>(hex(rules, 0xFF));
                if (previous != "--") {
                    open->previousAttacker = static_cast<std::uint8_t>(hex(previous, 0xFF));
                }
            }
        } else if (kind == "r" || kind == "w") {
            std::string at;
            std::string value;
            in >> at >> value;
            const std::uint32_t cell = address(at);
            const auto byte = static_cast<std::uint8_t>(hex(value, 0xFF));
            if (!open) {
                continue;
            }
            if (kind == "r") {
                if (!written.contains(cell) && !open->inputs.contains(cell)) {
                    open->inputs[cell] = byte;
                }
            } else {
                open->writes[cell] = byte;
                written.insert(cell);
            }
        } else if (kind == "return") {
            std::string name;
            std::string at;
            in >> name >> at;
            const std::uint32_t site = address(at);
            if (open && open->routine == name) {
                open->returnedTo = site;
                if (name == kTracedRoutines[5].name) {
                    attackerEffectMaker = open->params[kBlockAttacker];
                }
                calls.push_back(std::move(*open));
                open.reset();
            }
        } else if (kind == "abilities") {
            std::string actorWord;
            in >> actorWord;
            if (actorWord == "--") {
                continue;
            }
            std::string actor, levelWord, level;
            in >> actor >> levelWord >> level;
            if (actorWord != "actor" || levelWord != "level") {
                fail("not an abilities line");
            }
            if (open) {
                open->abilitiesActor = static_cast<std::uint8_t>(hex(actor, 0xFF));
                open->abilitiesLevel = static_cast<std::uint8_t>(hex(level, 0xFF));
            }
        } else if (kind == "message") {
            std::string message;
            in >> message;
            LoggedMessage logged{.message = static_cast<std::uint8_t>(hex(message, 0xFF))};
            for (std::size_t variable = 0; variable < 4; ++variable) {
                std::string word;
                in >> word;
                if (word.size() != 6) {
                    fail("not a message variable");
                }
                const std::uint32_t value = hex(word, 0xFFFFFF);
                for (std::size_t i = 0; i < 3; ++i) {
                    logged.variables[variable * 3 + i] = static_cast<std::uint8_t>(value >> (8 * i) & 0xFF);
                }
            }
            if (open) {
                open->messages.push_back(logged);
            }
        } else if (kind == "snapshot") {
            continue;
        } else {
            fail("not a trace event");
        }
    }
    return calls;
}

std::vector<TracedEnding> parseEndings(std::istream& log) {
    std::vector<TracedEnding> endings;
    // The endings entered and not yet returned from, innermost last, and which of their two
    // snapshots each range line fills.
    std::vector<std::pair<TracedEnding, bool>> open;
    std::string text;
    int number = 0;
    const auto fail = [&](const std::string& why) {
        throw std::runtime_error("trace line " + std::to_string(number) + ": " + why + ": " + text);
    };
    const auto hex = [&](const std::string& word, std::uint32_t most) {
        std::size_t used = 0;
        unsigned long value = 0;
        try {
            value = std::stoul(word, &used, 16);
        } catch (const std::exception&) {
            fail("not a hex number");
        }
        if (used != word.size() || value > most) {
            fail("not a hex number");
        }
        return static_cast<std::uint32_t>(value);
    };
    const auto address = [&](const std::string& word) {
        if (word.size() != 8 || word[0] != '$' || word[3] != ':') {
            fail("not an address");
        }
        return hex(word.substr(1, 2), 0xFF) << 16 | hex(word.substr(4), 0xFFFF);
    };
    while (std::getline(log, text)) {
        ++number;
        if (text.size() < 2 || text[0] != 'f' || !std::isdigit(static_cast<unsigned char>(text[1]))) {
            continue;
        }
        std::istringstream in{text};
        std::string frameWord;
        std::string kind;
        in >> frameWord >> kind;
        std::uint64_t frame = 0;
        try {
            frame = std::stoull(frameWord.substr(1));
        } catch (const std::exception&) {
            fail("not a frame");
        }
        if (kind == "message") {
            std::string message;
            in >> message;
            LoggedMessage logged{.message = static_cast<std::uint8_t>(hex(message, 0xFF))};
            for (std::size_t variable = 0; variable < 4; ++variable) {
                std::string word;
                in >> word;
                if (word.size() != 6) {
                    fail("not a message variable");
                }
                const std::uint32_t value = hex(word, 0xFFFFFF);
                for (std::size_t i = 0; i < 3; ++i) {
                    logged.variables[variable * 3 + i] = static_cast<std::uint8_t>(value >> (8 * i) & 0xFF);
                }
            }
            for (auto& [ending, returned] : open) {
                ending.messages.push_back(logged);
            }
            continue;
        }
        if (kind != "snapshot") {
            continue;
        }
        std::string name;
        std::string word;
        in >> name >> word;
        if (word == "enter") {
            std::string at;
            in >> at;
            address(at);
            open.emplace_back(TracedEnding{.routine = name, .frame = frame}, false);
            continue;
        }
        if (open.empty() || open.back().first.routine != name) {
            continue;
        }
        if (word == "return") {
            std::string at;
            in >> at;
            open.back().first.returnedTo = address(at);
            open.back().second = true;
            continue;
        }
        if (word == "handed") {
            std::string message;
            in >> message;
            open.back().first.handedMessage =
                message == "--" ? std::optional<std::uint8_t>{}
                                : std::optional<std::uint8_t>{static_cast<std::uint8_t>(hex(message, 0xFF))};
            continue;
        }
        const std::uint32_t first = address(word);
        std::string bytes;
        in >> bytes;
        if (bytes.size() % 2 != 0) {
            fail("not a range's bytes");
        }
        auto& [ending, returned] = open.back();
        std::map<std::uint32_t, std::uint8_t>& cells = returned ? ending.left : ending.entry;
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            cells[first + static_cast<std::uint32_t>(i / 2)] = static_cast<std::uint8_t>(hex(bytes.substr(i, 2), 0xFF));
        }
        // The return's last range closes the ending.
        const TracedRange& last = kEndingRanges.back();
        if (returned && first == last.at) {
            endings.push_back(std::move(ending));
            open.pop_back();
        }
    }
    return endings;
}

MessageShown messageShownBy(const LoggedMessage& logged) {
    GraphicsCall call{.command = kPlayBattleScript};
    call.script[0] = kShowMessageScriptCommand;
    call.script[1] = logged.message;
    call.messageVariables = logged.variables;
    return *messageShown(call);
}

// --- replaying a traced call ---------------------------------------------------------------------

namespace {

// The weapons' effects take the indexes 1 to 14, the items' $49 to $4E and the commands' $50 to $57;
// every other index is an attack's own.
constexpr std::uint8_t kLastWeaponEffect = 14;
constexpr std::uint8_t kFirstItemEffect = 0x49;
constexpr std::uint8_t kLastItemEffect = 0x4E;
constexpr std::uint8_t kItemEffectBase = 0x48;
constexpr std::uint8_t kFirstCommandEffect = 0x50;
constexpr std::uint8_t kLastCommandEffect = 0x57;

battle::SpecialEffect effectAt(std::uint8_t index) {
    if (index == 0) {
        return {};
    }
    if (index <= kLastWeaponEffect) {
        return battle::SpecialEffect::of(static_cast<WeaponSpecialEffect>(index));
    }
    if (index >= kFirstItemEffect && index <= kLastItemEffect) {
        return battle::SpecialEffect::of(static_cast<ItemUseEffect>(index - kItemEffectBase));
    }
    if (index >= kFirstCommandEffect && index <= kLastCommandEffect) {
        return battle::SpecialEffect::of(static_cast<battle::CommandEffect>(index));
    }
    return battle::SpecialEffect::of(static_cast<AttackSpecialEffect>(index));
}

// A battler named by a byte holding its slot index times two, or no one.
std::optional<battle::BattlerSlot> slotTimesTwo(std::uint8_t value) {
    if (value / 2 >= battle::kBattlerSlots) {
        return std::nullopt;
    }
    return battle::BattlerSlot::fromIndex(value / 2);
}

// The per-battler cells, each a word at twice the battler's index from its first.
constexpr std::array<std::uint32_t, 22> kBattlerCells{
    kBattlerBit, kToUpdate,  kLastDamagedBy, kController - 1, kLastAttacker, kTaken,          kHealed,
    kOnTheField, kLevel,     kDefenses,      kAbsorbedNullified, kWeakHalved, kHp,           kMp,
    kMaxHp,      kMaxMp,     DamageOracle::kRelicEffects1And3, DamageOracle::kRelicEffects4And5, kMonsterFlags, kToSet12,
    kStatus12,   kStatus34,
};

// The battler a per-battler cell belongs to, if it is one.
std::optional<int> battlerOfCell(std::uint32_t cell) {
    for (const std::uint32_t first : kBattlerCells) {
        if (cell >= first && cell < first + 2 * battle::kBattlerSlots) {
            return static_cast<int>((cell - first) / 2);
        }
    }
    for (const std::uint32_t first : {kToSet34, kToClear12, kToClear34}) {
        if (cell >= first && cell < first + 2 * battle::kBattlerSlots) {
            return static_cast<int>((cell - first) / 2);
        }
    }
    return std::nullopt;
}

const TracedRoutine& routineNamed(const std::string& name) {
    for (const TracedRoutine& routine : kTracedRoutines) {
        if (name == routine.name) {
            return routine;
        }
    }
    throw std::runtime_error("the trace names a routine it does not follow: " + name);
}

battle::BattlerSlot slotNamed(std::uint8_t index, const char* what) {
    if (index >= battle::kBattlerSlots) {
        throw std::runtime_error(std::string{what} + " names no battler: " + std::to_string(index));
    }
    return battle::BattlerSlot::fromIndex(index);
}

// Who a bounce's call is made for. A spell bounced off Reflect: the lowest slot whose byte in the
// bounce's block is set is the battler it bounced off this time. A Launcher's or Super Ball's hits:
// the lowest battler in the bounces that hit (bytes 14-15), which takes its bit just before the call,
// the bounce going last slot first — for their own hits, the attacker.
battle::BattlerSlot bounceAttacker(const TracedCall& call) {
    if ((call.aimingRules & kReflectedRule) != 0) {
        int from = 0;
        while (from < battle::kBattlerSlots && call.params[static_cast<std::size_t>(from)] == kNoOne) {
            ++from;
        }
        if (from == battle::kBattlerSlots) {
            throw std::runtime_error("the bounce's block names no battler it bounced off");
        }
        return battle::BattlerSlot::fromIndex(from);
    }
    const battle::BattlerSet hitters =
        setOf(static_cast<std::uint16_t>(call.params[kBlockSet] | call.params[kBlockSet + 1] << 8));
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        if (hitters.has(battle::BattlerSlot::fromIndex(index))) {
            return battle::BattlerSlot::fromIndex(index);
        }
    }
    throw std::runtime_error("a scattered hit whose block names no battler it was made from");
}

// Who a bounce's special effect is worked out for. The bounce works out a hit's statuses and effect
// before the battler it is made from joins the bounces that hit, so for a Launcher's or Super Ball's
// first hit those bytes are still empty and the hit is made by the battler making the attack: the
// attacker of the params block at the entry of its attacker effect, the last before the bounce, or
// with none traced, the attacker of the params block before the bounce's.
battle::BattlerSlot bounceEffectAttacker(const TracedCall& call) {
    const bool scattered = (call.aimingRules & kReflectedRule) == 0;
    const bool noneYet = call.params[kBlockSet] == 0 && call.params[kBlockSet + 1] == 0;
    if (scattered && noneYet) {
        // While hits are to come an attack stops before its own block is written, after its attacker
        // effect: the effect's params name the battler making the attack. With no attacker effect in
        // the trace, the block before the bounce's is the attack's own.
        if (call.attackerEffectMaker) {
            return slotNamed(*call.attackerEffectMaker, "the attacker effect's attacker");
        }
        if (!call.previousAttacker) {
            throw std::runtime_error("a scattered hit's effect with no attacker effect or params block before it");
        }
        return slotNamed(*call.previousAttacker, "the block before's attacker");
    }
    return bounceAttacker(call);
}

}  // namespace

void DamageOracle::writeContext(const DamageScenario& scenario) {
    writeByte(kParamsCells + kBlockAttacker, static_cast<std::uint8_t>(scenario.attacker.index()));
}

DamageScenario DamageOracle::readScenario() {
    DamageScenario s;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    const std::vector<std::uint8_t> record =
        machine_.read(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(sizeof(AttackProperties))});
    std::memcpy(&attack.record, record.data(), record.size());
    attack.attackerStat = readByte(kAttackerStat);
    attack.attackerLevel = readByte(kAttackerLevel);
    attack.damage = readWord(kDamage);
    const std::uint8_t flags = readByte(kAttackFlags);
    attack.twoHanded = (flags & kNotTwoHanded) == 0;
    attack.ignoreDefenseStopsMultiplier = (flags & kIgnoreDefenseStopsMultiplier) != 0;
    attack.vanishApplies = (flags & kVanishAppliesBit) != 0;
    attack.multiplier = readByte(kMultiplier);
    attack.effect = effectAt(static_cast<std::uint8_t>(readByte(kEffect) / 2));
    const std::uint8_t first = readByte(kFirstAttackFlags);
    attack.drainCapped = (first & kDrainCappedBit) != 0;
    attack.canBeCritical = (first & kCannotBeCriticalBit) == 0;
    attack.command = static_cast<BattleCommandId>(readByte(kCommand));
    attack.attack = static_cast<AttackId>(readByte(kAttackIndex));
    attack.secondary = readByte(kSecondary);
    state.rng = battle::BattleRng{readByte(kRngPosition)};
    attack.scratch.damageModification = readByte(kDamageModification) != 0;
    attack.scratch.golemBlocked = slotTimesTwo(readByte(kGolemBlock));
    attack.scratch.interceptorBlocked = slotTimesTwo(readByte(kInterceptorBlock));
    state.elementsNullifiedByForcefield.bits = readByte(kForcefield);
    attack.scratch.missed = setOf(readWord(kMissed));
    state.invincible = setOf(readWord(kInvincible));
    state.golemHp = readWord(kGolemHp);
    attack.scratch.weaponSpellcast.raw = readByte(kWeaponSpellcast);
    state.scratch.blackBeltEligible.characters =
        BattleSlotMask(static_cast<std::uint8_t>(~readByte(kBlackBeltTargets) & kPartyBits));

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        battler.level = readByte(kLevel + at);
        battler.defense = readByte(kDefenses + at);
        battler.magicDefense = readByte(kDefenses + at + 1);
        battler.hp = readWord(kHp + at);
        battler.mp = readWord(kMp + at);
        battler.maxHp = readWord(kMaxHp + at);
        battler.maxMp = readWord(kMaxMp + at);
        battler.elementsAbsorbed.bits = readByte(kAbsorbedNullified + at);
        battler.elementsNullified.bits = readByte(kAbsorbedNullified + at + 1);
        battler.elementsWeak.bits = readByte(kWeakHalved + at);
        battler.elementsHalved.bits = readByte(kWeakHalved + at + 1);
        const std::uint8_t monsterFlags = readByte(kMonsterFlags + at);
        battler.undead = (monsterFlags & kUndeadBit) != 0;
        battler.impCritical = (monsterFlags & kImpCriticalBit) != 0;
        battler.human = (monsterFlags & kHumanBit) != 0;
        battler.nameHidden = (monsterFlags & kNameHiddenBit) != 0;
        battler.diesAtZeroMp = (monsterFlags & kDiesAtZeroMpBit) != 0;
        const std::uint8_t stance = readByte(kStance + at);
        battler.defending = (stance & kDefendingBit) != 0;
        battler.backRow = (stance & kBackRowBit) != 0;
        battler.instantDeathProtected = (stance & kInstantDeathProtectedBit) != 0;
        const auto statuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        battler.status = statuses(kStatus12, kStatus34);
        battler.statusesToSet = statuses(kToSet12, kToSet34);
        battler.statusesToClear = statuses(kToClear12, kToClear34);
        const std::uint8_t link = readByte(kController + at);
        if (link != kNoOne) {
            battler.controlledBy = slotTimesTwo(static_cast<std::uint8_t>(link & ~kTopBit));
            battler.controlEnding = (link & kTopBit) != 0;
        }
        const std::uint8_t mark = readByte(kLastAttacker + at);
        const int lastAttacker = mark & ~kWaitingBit;
        if (lastAttacker < battle::kBattlerSlots) {
            battler.lastAttacker = battle::BattlerSlot::fromIndex(lastAttacker);
        }
        battler.retaliationPending = (mark & kWaitingBit) != 0;
        const auto entry = [](std::uint16_t word) -> std::optional<std::uint16_t> {
            if (word == kNoDamage) {
                return std::nullopt;
            }
            return word;
        };
        attack.scratch.ledger.taken[i] = entry(readWord(kTaken + at));
        attack.scratch.ledger.healed[i] = entry(readWord(kHealed + at));
        const std::uint8_t relicsLow = readByte(kRelicEffects1And3 + at);
        battler.relicEffects1.bits = static_cast<std::uint8_t>(relicsLow & kLowSevenBits);
        battler.relicEffects2.bits = static_cast<std::uint8_t>(relicsLow & kTopBit);
        battler.relicEffects3.bits = readByte(kRelicEffects1And3 + at + 1);
        battler.relicEffects4.bits = readByte(kRelicEffects4And5 + at);
        battler.relicEffects5.bits = readByte(kRelicEffects4And5 + at + 1);
        battler.pendingUpdates.enabledMagic = (readByte(kToUpdate + at) & kEnabledMagicBit) != 0;
        battler.lastDamagedBy = slotTimesTwo(readByte(kLastDamagedBy + at));
    }
    return s;
}

std::string DamageOracle::replay(const TracedCall& call) {
    const TracedRoutine& routine = routineNamed(call.routine);
    const bool bounce = routine.bounceReturn != 0 && call.returnedTo == routine.bounceReturn;
    const bool other = std::find(routine.otherReturns.begin(), routine.otherReturns.end(), call.returnedTo) !=
                       routine.otherReturns.end();
    if (!bounce && !other && call.returnedTo != routine.frameReturn) {
        throw std::runtime_error(call.routine + " returned to " + busAddress(call.returnedTo) +
                                 ", which is none of the calls the trace follows");
    }
    if (routine.entry == kInitAi || routine.entry == kExecMonsterAction || routine.entry == kExecAiRetal ||
        std::find(kAiCommands.begin(), kAiCommands.end(), routine.entry) != kAiCommands.end()) {
        return replayAiCall(call, routine);
    }
    if (routine.entry == kUpdateStatus || routine.entry == kMagicStatusEffect) {
        return replayStatusCall(call, routine);
    }
    if (routine.entry == kDoAttackerEffect || routine.entry == kDoTargetEffect) {
        return replayEffectCall(call, routine, bounce);
    }
    if (routine.entry == kAddExp || routine.entry == kLearnItemMagic || routine.entry == kLearnAbilities ||
        routine.entry == kLearnRage) {
        return replayRewardCall(call, routine);
    }
    if (routine.partyRecords) {
        return replayRecordCall(call, routine);
    }

    // The battle as the oracle writes a default scenario, then every cell the routine read as it read it.
    const DamageScenario plain;
    writeScenario(plain);
    writeTargetCells(plain);
    writeApplyCells(plain);
    for (const auto& [cell, value] : call.inputs) {
        writeByte(cell, value);
    }
    DamageScenario s = readScenario();
    const auto input = [&](std::uint32_t cell) {
        const auto found = call.inputs.find(cell);
        return found != call.inputs.end() ? found->second : readByte(cell);
    };
    const auto overlayWrites = [&](std::uint32_t queueShift) {
        for (const auto& [cell, value] : call.writes) {
            if (cell >= kQueue && cell < kQueue + kQueueSpan && queueShift != 0) {
                if (cell - kQueue >= queueShift) {
                    writeByte(cell - queueShift, value);
                }
                continue;
            }
            writeByte(cell, value);
        }
    };

    if (routine.entry == kCalcDmg) {
        // A party member's blow reads its fourth relic effects; a monster's reads no battler's cell, and
        // a spell's answer does not depend on who cast it.
        const bool physical = s.attack.record.traits.has(AttackTrait::PHYSICAL);
        s.attacker = physical ? battle::BattlerSlot::monster(0) : battle::BattlerSlot::character(0);
        for (const auto& [cell, value] : call.inputs) {
            if (cell >= kRelicEffects4And5 && cell < kRelicEffects4And5 + 2 * battle::kBattlerSlots) {
                s.attacker = battle::BattlerSlot::fromIndex(static_cast<int>((cell - kRelicEffects4And5) / 2));
            }
        }
        const std::uint16_t port = physical ? battle::physicalDamage(s.state, s.attack, s.attacker)
                                            : battle::magicDamage(s.attack.record.power, s.attack.attackerStat,
                                                                  s.attack.attackerLevel);
        overlayWrites(0);
        const std::uint16_t guest = readWord(kDamage);
        if (port == guest) {
            return {};
        }
        return "damage word: port " + std::to_string(port) + ", cartridge " + std::to_string(guest) + "\n";
    }

    if (routine.entry == kCalcTargetDmg) {
        // The battlers whose cells the routine touched, other than `attacker`.
        const auto touchedBesides = [&](battle::BattlerSlot attacker) {
            std::set<int> touched;
            for (const auto* cells : {&call.inputs, &call.writes}) {
                for (const auto& [cell, value] : *cells) {
                    if (const std::optional<int> battler = battlerOfCell(cell)) {
                        touched.insert(*battler);
                    }
                }
            }
            touched.erase(attacker.index());
            return touched;
        };
        battle::BattlerSlot target = battle::BattlerSlot::character(0);
        if (!bounce) {
            s.attacker = slotNamed(call.params[kBlockAttacker], "the params block's attacker");
            // The target is the battler whose cells the routine touched, other than the attacker; with
            // no other, the attacker itself.
            const std::set<int> touched = touchedBesides(s.attacker);
            if (touched.size() > 1) {
                throw std::runtime_error("CalcTargetDmg touched more than one battler besides the attacker");
            }
            target = touched.empty() ? s.attacker : battle::BattlerSlot::fromIndex(*touched.begin());
        } else if ((call.aimingRules & kReflectedRule) != 0) {
            // A bounce: the battler it bounced off this time (bounceAttacker()), and its byte the battler
            // it went on to.
            s.attacker = bounceAttacker(call);
            target = slotNamed(call.params[static_cast<std::size_t>(s.attacker.index())], "the bounce's target");
        } else {
            // Hits were still to come as the bounce began, and the hit is made from the battler
            // bounceAttacker() names. While hits are still to come the target is in the byte the hit
            // was given, one past the hits now to come. Once they have run out a later bounce's target
            // is in its own battler's byte, so a call with none to come is the last hit (byte 0) or a
            // later bounce, told apart by whose cells the call touched.
            s.attacker = bounceAttacker(call);
            const std::optional<battle::BattlerSlot> from = s.attacker;
            std::vector<std::uint8_t> bytes;
            if (call.hitsToCome != kNoHitsToCome) {
                bytes.push_back(call.params[static_cast<std::size_t>(call.hitsToCome + 1) % kParamsSize]);
            } else {
                bytes.push_back(call.params[0]);
                bytes.push_back(call.params[static_cast<std::size_t>(from->index())]);
            }
            std::erase(bytes, kNoOne);
            if (bytes.empty()) {
                throw std::runtime_error("a scattered hit whose block names no target");
            }
            // With no other battler touched, a byte naming the battler the hit is made from is the
            // target.
            const std::set<int> touched = touchedBesides(s.attacker);
            target = slotNamed(bytes.front(), "the hit's target");
            for (const std::uint8_t byte : bytes) {
                if (touched.empty() ? byte == from->index() : touched.contains(byte)) {
                    target = slotNamed(byte, "the hit's target");
                }
            }
        }
        const TargetDamageResult port = portTargetDamage(s, target);
        writeScenario(s);
        writeTargetCells(s);
        overlayWrites(input(kAnimationPlace));
        return differences(port, readTargetResult());
    }

    // The damage taken and shown.
    if (!bounce) {
        s.attacker = slotNamed(call.params[kBlockAttacker], "the params block's attacker");
    } else if ((call.aimingRules & kReflectedRule) == 0) {
        // A Launcher's or Super Ball's damage is shown with its maker's bit in the block's battler
        // set (CalcReflectDmg :8451-8452), the block before it or not.
        const battle::BattlerSet maker =
            setOf(static_cast<std::uint16_t>(call.params[kBlockSet] | call.params[kBlockSet + 1] << 8));
        std::optional<battle::BattlerSlot> found;
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            if (maker.has(battle::BattlerSlot::fromIndex(index))) {
                found = battle::BattlerSlot::fromIndex(index);
            }
        }
        if (!found) {
            throw std::runtime_error("a Launcher's damage shown with no maker in its block");
        }
        s.attacker = *found;
    } else {
        if (!call.previousAttacker) {
            throw std::runtime_error("a bounce's damage shown with no params block before it");
        }
        s.attacker = slotNamed(*call.previousAttacker, "the block before's attacker");
    }
    const std::uint8_t queueEnd = input(kQueueEnd);
    const std::uint8_t buffers = input(kNumeralBufferCount);
    const ApplyDamageResult port = portApplyDamage(s);
    writeScenario(s);
    writeTargetCells(s);
    writeApplyCells(s);
    writeByte(kQueueEnd, queueEnd);
    writeByte(kNumeralBufferCount, buffers);
    overlayWrites(0);
    return differences(port, readApplyResult(queueEnd / kQueueCommandSize, buffers));
}


// --- the statuses -----------------------------------------------------------------------------------

namespace {

// Per battler, at twice the battler's index: the statuses it can be given, its flags, its triggers,
// its four status timers and the statuses it is to have for a counterattack. A party member's
// equipment flag is one byte per party slot.
constexpr std::uint32_t kInflictable12 = kWorkRam + 0x331C;
constexpr std::uint32_t kInflictable34 = kWorkRam + 0x3330;
constexpr std::uint32_t kTriggers = kWorkRam + 0x3E4C;      // the first byte: bit 0 Retort
constexpr std::uint32_t kMoreTriggers = kWorkRam + 0x3E4D;  // bit 1 Overcast, bit 6 Phantasm
constexpr std::uint32_t kSleepTimer = kWorkRam + 0x3CF9;
constexpr std::uint32_t kStopTimer = kWorkRam + 0x3AF1;
constexpr std::uint32_t kReflectTimer = kWorkRam + 0x3F0C;
constexpr std::uint32_t kFreezeTimer = kWorkRam + 0x3F0D;
constexpr std::uint32_t kForCounter12 = kWorkRam + 0x3E60;
constexpr std::uint32_t kForCounter34 = kWorkRam + 0x3E74;
constexpr std::uint32_t kEquipmentChanged = kWorkRam + 0x2F30;
// The battle's own: the monsters standing and fallen (a byte of monster bits each), the battlers that
// died this action, the party members whose statuses changed, and the effect's misses.
constexpr std::uint32_t kMonstersStanding = kWorkRam + 0x2F2F;
constexpr std::uint32_t kMonstersFallen = kWorkRam + 0x3A3A;
constexpr std::uint32_t kDiedThisAction = kWorkRam + 0x3A56;
constexpr std::uint32_t kStatusChanged = kWorkRam + 0x3A4A;
constexpr std::uint32_t kMissedByEffect = kWorkRam + 0x3A48;

constexpr std::uint8_t kPresentBit = 0x01;
constexpr std::uint8_t kMenuMayOpenBit = 0x80;
constexpr std::uint8_t kEmptyGaugeBit = 0x40;
constexpr std::uint8_t kRetortBit = 0x01;
constexpr std::uint8_t kOvercastBit = 0x02;
constexpr std::uint8_t kPhantasmBit = 0x40;

// What is to be worked out again, as the cartridge's byte holds it.
std::uint8_t pendingByte(const battle::PendingUpdates& pending) {
    std::uint8_t bits = 0;
    bits |= pending.enabledMagic ? 0x80 : 0;
    bits |= pending.removeAllActions ? 0x40 : 0;
    bits |= pending.startCondemned ? 0x20 : 0;
    bits |= pending.stopCondemned ? 0x10 : 0;
    bits |= pending.commandList ? 0x08 : 0;
    bits |= pending.speed ? 0x04 : 0;
    bits |= pending.morph ? 0x02 : 0;
    bits |= pending.leaveTheField ? 0x01 : 0;
    return bits;
}

std::string statusesText(const StatusSet& set) {
    return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
           "/" + std::to_string(set.bytes[3]);
}

}  // namespace

void DamageOracle::writeStatusCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    writeByte(kMonstersStanding, state.monstersStanding.monsters.bits);
    writeByte(kMonstersFallen, state.monstersFallen.monsters.bits);
    writeWord(kDiedThisAction, maskOf(state.diedAwaitingCounter));
    writeWord(kStatusChanged, maskOf(state.statusChanged));
    writeInFlightWord(kMissed, maskOf(scenario.attack.scratch.missed));
    writeInFlightWord(kMissedByEffect, 0);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        writeWord(kBattlerBit + at, maskOf(battle::BattlerSet::of(slot)));
        writeByte(kInflictable12 + at, battler.inflictableStatuses.bytes[0]);
        writeByte(kInflictable12 + at + 1, battler.inflictableStatuses.bytes[1]);
        writeByte(kInflictable34 + at, battler.inflictableStatuses.bytes[2]);
        writeByte(kInflictable34 + at + 1, battler.inflictableStatuses.bytes[3]);
        std::uint8_t flags = battler.present ? kPresentBit : 0;
        flags |= battler.menuMayOpen ? kMenuMayOpenBit : 0;
        writeByte(kOnTheField + at, flags);
        std::uint8_t stance = 0;
        stance |= battler.defending ? kDefendingBit : 0;
        stance |= battler.backRow ? kBackRowBit : 0;
        stance |= battler.instantDeathProtected ? kInstantDeathProtectedBit : 0;
        stance |= battler.emptyGaugeAfterAction ? kEmptyGaugeBit : 0;
        writeByte(kStance + at, stance);
        writeByte(kToUpdate + at, pendingByte(battler.pendingUpdates));
        writeByte(kTriggers + at, battler.triggers.retort ? kRetortBit : 0);
        std::uint8_t triggers = battler.triggers.overcast ? kOvercastBit : 0;
        triggers |= battler.triggers.phantasm ? kPhantasmBit : 0;
        writeByte(kMoreTriggers + at, triggers);
        writeByte(kSleepTimer + at, battler.sleepTimer);
        writeByte(kStopTimer + at, battler.stopTimer);
        writeByte(kReflectTimer + at, battler.reflectTimer);
        writeByte(kFreezeTimer + at, battler.freezeTimer);
        writeByte(kForCounter12 + at, battler.statusForCounter.bytes[0]);
        writeByte(kForCounter12 + at + 1, battler.statusForCounter.bytes[1]);
        writeByte(kForCounter34 + at, battler.statusForCounter.bytes[2]);
        writeByte(kForCounter34 + at + 1, battler.statusForCounter.bytes[3]);
        if (slot.isCharacter()) {
            writeByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position()), battler.equipmentChanged ? 1 : 0);
        }
    }
}

StatusChangesResult DamageOracle::statusChanges(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeStatusCells(scenario);
    call([&] { updateStatus_(kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedRoutines[3]);
    return readStatusChangesResult();
}

StatusChangesResult DamageOracle::readStatusChangesResult() {
    StatusChangesResult result;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const auto readStatuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        result.status[i] = readStatuses(kStatus12, kStatus34);
        result.statusesToSet[i] = readStatuses(kToSet12, kToSet34);
        result.statusesToClear[i] = readStatuses(kToClear12, kToClear34);
        result.statusForCounter[i] = readStatuses(kForCounter12, kForCounter34);
        result.pendingUpdates[i] = readByte(kToUpdate + at);
        result.menuMayOpen[i] = (readByte(kOnTheField + at) & kMenuMayOpenBit) != 0;
        result.emptyGaugeAfterAction[i] = (readByte(kStance + at) & kEmptyGaugeBit) != 0;
        result.phantasm[i] = (readByte(kMoreTriggers + at) & kPhantasmBit) != 0;
        if (slot.isCharacter()) {
            result.equipmentChanged[i] = readByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position())) != 0;
        }
        result.sleepTimer[i] = readByte(kSleepTimer + at);
        result.stopTimer[i] = readByte(kStopTimer + at);
        result.reflectTimer[i] = readByte(kReflectTimer + at);
        result.freezeTimer[i] = readByte(kFreezeTimer + at);
    }
    result.diedAwaitingCounter = setOf(readWord(kDiedThisAction));
    result.statusChanged = setOf(readWord(kStatusChanged));
    result.monstersStanding.monsters = BattleSlotMask(readByte(kMonstersStanding));
    result.monstersFallen.monsters = BattleSlotMask(readByte(kMonstersFallen));
    return result;
}

namespace {

// What the state holds of the statuses taking effect, read as a StatusChangesResult.
StatusChangesResult statusChangesOf(const battle::BattleState& state) {
    StatusChangesResult result;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        result.status[i] = battler.status;
        result.statusesToSet[i] = battler.statusesToSet;
        result.statusesToClear[i] = battler.statusesToClear;
        result.statusForCounter[i] = battler.statusForCounter;
        result.pendingUpdates[i] = pendingByte(battler.pendingUpdates);
        result.menuMayOpen[i] = battler.menuMayOpen;
        result.emptyGaugeAfterAction[i] = battler.emptyGaugeAfterAction;
        result.phantasm[i] = battler.triggers.phantasm;
        result.equipmentChanged[i] = battle::BattlerSlot::fromIndex(static_cast<int>(i)).isCharacter() &&
                                     battler.equipmentChanged;
        result.sleepTimer[i] = battler.sleepTimer;
        result.stopTimer[i] = battler.stopTimer;
        result.reflectTimer[i] = battler.reflectTimer;
        result.freezeTimer[i] = battler.freezeTimer;
    }
    result.diedAwaitingCounter = state.diedAwaitingCounter;
    result.statusChanged = state.statusChanged;
    result.monstersStanding = state.monstersStanding.monstersOnly();
    result.monstersFallen = state.monstersFallen.monstersOnly();
    return result;
}

}  // namespace

StatusChangesResult portStatusChanges(DamageScenario scenario) {
    battle::applyStatusChanges(scenario.state);
    return statusChangesOf(scenario.state);
}

std::string differences(const StatusChangesResult& port, const StatusChangesResult& guest) {
    std::string out;
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("statuses" + slot, statusesText(port.status[i]), statusesText(guest.status[i]));
        note("statuses to set" + slot, statusesText(port.statusesToSet[i]), statusesText(guest.statusesToSet[i]));
        note("statuses to clear" + slot, statusesText(port.statusesToClear[i]), statusesText(guest.statusesToClear[i]));
        note("statuses for a counterattack" + slot, statusesText(port.statusForCounter[i]),
             statusesText(guest.statusForCounter[i]));
        note("to work out again" + slot, std::to_string(port.pendingUpdates[i]), std::to_string(guest.pendingUpdates[i]));
        note("menu may open" + slot, std::to_string(port.menuMayOpen[i]), std::to_string(guest.menuMayOpen[i]));
        note("gauge empties after the action" + slot, std::to_string(port.emptyGaugeAfterAction[i]),
             std::to_string(guest.emptyGaugeAfterAction[i]));
        note("Phantasm" + slot, std::to_string(port.phantasm[i]), std::to_string(guest.phantasm[i]));
        note("equipment to work out" + slot, std::to_string(port.equipmentChanged[i]),
             std::to_string(guest.equipmentChanged[i]));
        note("sleep timer" + slot, std::to_string(port.sleepTimer[i]), std::to_string(guest.sleepTimer[i]));
        note("stop timer" + slot, std::to_string(port.stopTimer[i]), std::to_string(guest.stopTimer[i]));
        note("reflect timer" + slot, std::to_string(port.reflectTimer[i]), std::to_string(guest.reflectTimer[i]));
        note("freeze timer" + slot, std::to_string(port.freezeTimer[i]), std::to_string(guest.freezeTimer[i]));
    }
    note("died this action", std::to_string(maskOf(port.diedAwaitingCounter)),
         std::to_string(maskOf(guest.diedAwaitingCounter)));
    note("statuses changed", std::to_string(maskOf(port.statusChanged)), std::to_string(maskOf(guest.statusChanged)));
    note("monsters standing", std::to_string(maskOf(port.monstersStanding)),
         std::to_string(maskOf(guest.monstersStanding)));
    note("monsters fallen", std::to_string(maskOf(port.monstersFallen)), std::to_string(maskOf(guest.monstersFallen)));
    return out;
}

StatusEffectResult DamageOracle::statusEffect(const DamageScenario& scenario, battle::BattlerSlot target) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeStatusCells(scenario);
    call([&] {
        magicStatusEffect_(slotIndex(scenario.attacker), slotIndex(target), kWideAccumulatorStatus, kBattleDataBank,
                           kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[4]);
    return readStatusEffectResult(target);
}

StatusEffectResult DamageOracle::readStatusEffectResult(battle::BattlerSlot target) {
    const auto at = static_cast<std::uint32_t>(slotIndex(target));
    return StatusEffectResult{
        .statusesToSet = StatusSet{{readByte(kToSet12 + at), readByte(kToSet12 + at + 1), readByte(kToSet34 + at),
                                    readByte(kToSet34 + at + 1)}},
        .statusesToClear = StatusSet{{readByte(kToClear12 + at), readByte(kToClear12 + at + 1),
                                      readByte(kToClear34 + at), readByte(kToClear34 + at + 1)}},
        .missedByEffect = setOf(readWord(kMissedByEffect)),
        .missed = setOf(readWord(kMissed)),
    };
}

DamageScenario DamageOracle::readStatusScenario() {
    DamageScenario s = readScenario();
    battle::BattleState& state = s.state;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const auto statuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        battler.inflictableStatuses = statuses(kInflictable12, kInflictable34);
        battler.statusForCounter = statuses(kForCounter12, kForCounter34);
        const std::uint8_t flags = readByte(kOnTheField + at);
        battler.present = (flags & kPresentBit) != 0;
        battler.menuMayOpen = (flags & kMenuMayOpenBit) != 0;
        battler.emptyGaugeAfterAction = (readByte(kStance + at) & kEmptyGaugeBit) != 0;
        const std::uint8_t pending = readByte(kToUpdate + at);
        battler.pendingUpdates = battle::PendingUpdates{
            .leaveTheField = (pending & 0x01) != 0,
            .enabledMagic = (pending & 0x80) != 0,
            .removeAllActions = (pending & 0x40) != 0,
            .startCondemned = (pending & 0x20) != 0,
            .stopCondemned = (pending & 0x10) != 0,
            .commandList = (pending & 0x08) != 0,
            .speed = (pending & 0x04) != 0,
            .morph = (pending & 0x02) != 0,
        };
        battler.triggers.retort = (readByte(kTriggers + at) & kRetortBit) != 0;
        const std::uint8_t triggers = readByte(kMoreTriggers + at);
        battler.triggers.overcast = (triggers & kOvercastBit) != 0;
        battler.triggers.phantasm = (triggers & kPhantasmBit) != 0;
        battler.sleepTimer = readByte(kSleepTimer + at);
        battler.stopTimer = readByte(kStopTimer + at);
        battler.reflectTimer = readByte(kReflectTimer + at);
        battler.freezeTimer = readByte(kFreezeTimer + at);
        if (slot.isCharacter()) {
            battler.equipmentChanged = readByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position())) != 0;
        }
    }
    state.diedAwaitingCounter = setOf(readWord(kDiedThisAction));
    state.statusChanged = setOf(readWord(kStatusChanged));
    state.monstersStanding.monsters = BattleSlotMask(readByte(kMonstersStanding));
    state.monstersFallen.monsters = BattleSlotMask(readByte(kMonstersFallen));
    return s;
}

namespace {

std::optional<battle::BattlerSlot> statusCallTarget(const TracedCall& call) {
    std::set<int> touched;
    for (const auto* cells : {&call.inputs, &call.writes}) {
        for (const auto& [cell, value] : *cells) {
            if (const std::optional<int> battler = battlerOfCell(cell)) {
                touched.insert(*battler);
            }
            for (const std::uint32_t first : {kInflictable12, kInflictable34}) {
                if (cell >= first && cell < first + 2 * battle::kBattlerSlots) {
                    touched.insert(static_cast<int>((cell - first) / 2));
                }
            }
        }
    }
    if (touched.size() != 1) {
        return std::nullopt;
    }
    return battle::BattlerSlot::fromIndex(*touched.begin());
}

}  // namespace

std::string DamageOracle::replayStatusCall(const TracedCall& call, const TracedRoutine& routine) {
    // The battle as the oracle writes a default scenario, then every cell the routine read as it read it.
    const DamageScenario plain;
    writeScenario(plain);
    writeTargetCells(plain);
    writeApplyCells(plain);
    writeStatusCells(plain);
    for (const auto& [cell, value] : call.inputs) {
        writeByte(cell, value);
    }
    const DamageScenario s = readStatusScenario();
    // The cartridge's answer: the scenario's cells as the oracle writes them, then the routine's writes.
    const auto guestCells = [&] {
        writeScenario(s);
        writeTargetCells(s);
        writeStatusCells(s);
        for (const auto& [cell, value] : call.writes) {
            writeByte(cell, value);
        }
    };

    if (routine.entry == kUpdateStatus) {
        const StatusChangesResult port = portStatusChanges(s);
        guestCells();
        return differences(port, readStatusChangesResult());
    }

    const std::optional<battle::BattlerSlot> found = statusCallTarget(call);
    if (!found) {
        throw std::runtime_error("MagicStatusEffect touched more than one battler's cells, or none; its target is the "
                                 "one it touches");
    }
    const battle::BattlerSlot target = *found;
    const StatusEffectResult port = portStatusEffect(s, target);
    guestCells();
    return differences(port, readStatusEffectResult(target));
}

StatusEffectResult portStatusEffect(DamageScenario scenario, battle::BattlerSlot target) {
    scenario.attack.scratch.missedByEffect = {};
    battle::statusEffect(scenario.state, scenario.attack, scenario.attacker, target);
    const battle::Battler& battler = scenario.state.at(target);
    return StatusEffectResult{
        .statusesToSet = battler.statusesToSet,
        .statusesToClear = battler.statusesToClear,
        .missedByEffect = scenario.attack.scratch.missedByEffect,
        .missed = scenario.attack.scratch.missed,
    };
}

std::string differences(const StatusEffectResult& port, const StatusEffectResult& guest) {
    std::string out;
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    note("statuses to set", statusesText(port.statusesToSet), statusesText(guest.statusesToSet));
    note("statuses to clear", statusesText(port.statusesToClear), statusesText(guest.statusesToClear));
    note("missed by the effect", std::to_string(maskOf(port.missedByEffect)), std::to_string(maskOf(guest.missedByEffect)));
    note("shown Miss", std::to_string(maskOf(port.missed)), std::to_string(maskOf(guest.missed)));
    return out;
}

namespace {

StatusSet anyStatuses(std::mt19937& rng) {
    return StatusSet{{byte(rng), byte(rng), byte(rng), byte(rng)}};
}

battle::BattlerSet anyBattlers(std::mt19937& rng) {
    return battle::BattlerSet{BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x0F)),
                              BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F))};
}

}  // namespace

DamageScenario drawStatusChangesScenario(std::mt19937& rng) {
    DamageScenario s;
    battle::BattleState& state = s.state;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const bool character = battle::BattlerSlot::fromIndex(static_cast<int>(i)).isCharacter();
        battler.present = !oneIn(rng, 4);
        battler.status = anyStatuses(rng);
        battler.statusesToSet = anyStatuses(rng);
        battler.statusesToClear = anyStatuses(rng);
        battler.inflictableStatuses = anyStatuses(rng);
        battler.statusForCounter = anyStatuses(rng);
        battler.maxHp = static_cast<std::uint16_t>(character ? 1 + upTo(rng, kPartyMaxHp - 1) : 1 + upTo(rng, 65534));
        // Half the time the HP is within a step of an eighth of the maximum, where Near fatal turns.
        if ((rng() & 1) != 0) {
            const int eighth = battler.maxHp / 8;
            battler.hp = static_cast<std::uint16_t>(std::clamp(eighth - 1 + upTo(rng, 2), 0, static_cast<int>(battler.maxHp)));
        } else {
            battler.hp = static_cast<std::uint16_t>(upTo(rng, battler.maxHp));
        }
        battler.triggers.overcast = (rng() & 1) != 0;
        battler.triggers.phantasm = (rng() & 1) != 0;
        battler.triggers.retort = (rng() & 1) != 0;
        battler.retaliationPending = (rng() & 1) != 0;
        if ((rng() & 1) != 0) {
            battler.lastAttacker = anySlot(rng);
        }
        const std::uint8_t pending = byte(rng);
        battler.pendingUpdates = battle::PendingUpdates{
            .leaveTheField = (pending & 0x01) != 0,
            .enabledMagic = (pending & 0x80) != 0,
            .removeAllActions = (pending & 0x40) != 0,
            .startCondemned = (pending & 0x20) != 0,
            .stopCondemned = (pending & 0x10) != 0,
            .commandList = (pending & 0x08) != 0,
            .speed = (pending & 0x04) != 0,
            .morph = (pending & 0x02) != 0,
        };
        battler.menuMayOpen = (rng() & 1) != 0;
        battler.emptyGaugeAfterAction = (rng() & 1) != 0;
        battler.equipmentChanged = character && (rng() & 1) != 0;
        battler.sleepTimer = byte(rng);
        battler.stopTimer = byte(rng);
        battler.reflectTimer = byte(rng);
        battler.freezeTimer = byte(rng);
    }
    state.diedAwaitingCounter = anyBattlers(rng);
    state.statusChanged = anyBattlers(rng);
    state.monstersStanding = anyBattlers(rng).monstersOnly();
    state.monstersFallen = anyBattlers(rng).monstersOnly();
    return s;
}

TargetScenario drawStatusEffectScenario(std::mt19937& rng) {
    TargetScenario drawn;
    DamageScenario& s = drawn.scenario;
    s.attacker = anySlot(rng);
    drawn.target = anySlot(rng);
    AttackProperties& record = s.attack.record;
    record.traits.bits = byte(rng);
    record.element.bits = byte(rng);
    record.flags2.bits = byte(rng);
    // Never both status modes: no attack has them, and the cartridge would take the bytes after its
    // three-entry mode table as an address and run whatever code is there.
    if (record.flags2.has(AttackFlag2::REMOVE_STATUS) && record.flags2.has(AttackFlag2::TOGGLE_STATUS)) {
        record.flags2.clear((rng() & 1) != 0 ? AttackFlag2::REMOVE_STATUS : AttackFlag2::TOGGLE_STATUS);
    }
    record.misc.bits = byte(rng);
    record.statuses = anyStatuses(rng);
    s.attack.vanishApplies = (rng() & 1) != 0;
    s.attack.scratch.missed = anyBattlers(rng);
    battle::Battler& target = s.state.at(drawn.target);
    target.present = true;
    target.status = anyStatuses(rng);
    target.statusesToSet = anyStatuses(rng);
    target.statusesToClear = anyStatuses(rng);
    target.inflictableStatuses = (rng() & 1) != 0 ? anyStatuses(rng) : StatusSet{};
    target.undead = (rng() & 1) != 0;
    return drawn;
}

// --- the special effect on the attacker ----------------------------------------------------------

namespace {

// The direct page: the params block ($A0-$AF), the swirl's suppression ($B0 bit 4), the command's
// running state ($B1: bit 0 a counterattack, bit 2 the party cannot run).
constexpr std::uint32_t kParamsFlags = kWorkRam + 0x00A0;
constexpr std::uint32_t kParamsAttacker = kWorkRam + 0x00A1;
constexpr std::uint32_t kParamsTargets = kWorkRam + 0x00A2;
constexpr std::uint32_t kParamsHit = kWorkRam + 0x00A4;
constexpr std::uint32_t kParamsReflectedOff = kWorkRam + 0x00A6;
constexpr std::uint32_t kSwirlCell = kWorkRam + 0x00B0;
constexpr std::uint32_t kRunningCell = kWorkRam + 0x00B1;
// The command's cells: the spell to cast next, the message to show, the attacks still to make, a
// Dragon Horn's jump, Mind Blast's four picks.
constexpr std::uint32_t kFollowUpSpell = kWorkRam + 0x3400;
constexpr std::uint32_t kMessage = kWorkRam + 0x3401;
constexpr std::uint32_t kAttacksLeft = kWorkRam + 0x3A70;
constexpr std::uint32_t kDragonHorn = kWorkRam + 0x3A8E;
constexpr std::uint32_t kMindBlastPicks = kWorkRam + 0x3A5C;
// The battle's cells: Quick's turns left and its target, the end-of-battle event (its index times
// two), the battlers leaving after the action, the allies still fighting.
constexpr std::uint32_t kQuickTurns = kWorkRam + 0x3402;
constexpr std::uint32_t kQuickTarget = kWorkRam + 0x3404;
constexpr std::uint32_t kEndOfBattleEvent = kWorkRam + 0x3A6E;
constexpr std::uint32_t kLeaving = kWorkRam + 0x2F4C;
constexpr std::uint32_t kAllyCount = kWorkRam + 0x3A76;
// Per battler, at twice the battler's index: the hands' power (a party member's weapon hand, then
// its shield hand), Seize's hold, the monster's battle flags and its gold.
constexpr std::uint32_t kHandPower = kWorkRam + 0x3B68;
constexpr std::uint32_t kSeizing = kWorkRam + 0x3358;
constexpr std::uint32_t kMonsterBattleFlags = kWorkRam + 0x3C80;
constexpr std::uint32_t kGold = kWorkRam + 0x3D98;
// The battle's copy of each party member's hands, five bytes an item: the weapon hands, then the
// shield hands. The item is the first byte, the quantity the fourth.
constexpr std::uint32_t kWeaponHands = kWorkRam + 0x2B86;
constexpr std::uint32_t kShieldHands = kWorkRam + 0x2B9A;
constexpr std::uint32_t kHandItemSize = 5;
constexpr std::uint32_t kHandQuantity = 3;
// The party's gil and steps, three bytes each, and the play clock's frames (1-60, stepped by the
// graphics program's interrupt, IncGameTime menu_common.asm:3488-3519).
constexpr std::uint32_t kGil = kWorkRam + 0x1860;
constexpr std::uint32_t kSteps = kWorkRam + 0x1866;
constexpr std::uint32_t kPlayClockFrames = kWorkRam + 0x021E;

constexpr std::uint8_t kParamsMonsterBit = 0x80;
constexpr std::uint8_t kParamsNoCharacterTargetsBit = 0x40;
constexpr std::uint8_t kParamsSwirlBit = 0x10;
constexpr std::uint8_t kParamsDragonHornBit = 0x02;
constexpr std::uint8_t kSwirlSuppressedBit = 0x10;
constexpr std::uint8_t kCounterattackBit = 0x01;
constexpr std::uint8_t kCannotRunBit = 0x04;
constexpr std::uint8_t kInBattleMenuBit = 0x04;
constexpr std::uint8_t kAutomaticCriticalClearBit = 0x02;  // $B3 bit 1: clear for an automatic critical
constexpr std::uint8_t kRandomTargetBit = 0x40;
constexpr std::uint8_t kFallBackToEarlierTargetsBit = 0x20;
constexpr std::uint8_t kOpposingSideBit = 0x02;
constexpr std::uint8_t kEndTurnBit = 0x10;                  // $3A46 bit 4
constexpr std::uint8_t kDragonHornOn = 0xFF;
constexpr std::uint8_t kNoQuick = 0xFF;

void writeThreeBytes(std::array<std::uint8_t, 3>& bytes, std::uint32_t value) {
    bytes = {static_cast<std::uint8_t>(value & 0xFF), static_cast<std::uint8_t>((value >> 8) & 0xFF),
             static_cast<std::uint8_t>((value >> 16) & 0xFF)};
}

std::string setText(battle::BattlerSet set) {
    return std::to_string(maskOf(set));
}

std::string slotText(const std::optional<battle::BattlerSlot>& slot) {
    return slot ? std::to_string(slot->index()) : std::string{"none"};
}

std::string paramsText(const battle::AttackAnimationParams& params) {
    return "attacker " + std::to_string(params.attacker.index()) + " targets " + setText(params.targets) + " hit " +
           setText(params.targetsHit) + " reflected off " + setText(params.reflectedOff) + " swirl " +
           std::to_string(params.castingSwirl) + " monster " + std::to_string(params.attackerIsMonster) +
           " no party targets " + std::to_string(params.noCharacterTargets) + " dragon horn " +
           std::to_string(params.dragonHorn);
}

battle::AttackAnimationParams paramsFromBytes(std::span<const std::uint8_t> block) {
    battle::AttackAnimationParams params;
    params.attackerIsMonster = (block[0] & kParamsMonsterBit) != 0;
    params.noCharacterTargets = (block[0] & kParamsNoCharacterTargetsBit) != 0;
    params.castingSwirl = (block[0] & kParamsSwirlBit) != 0;
    params.dragonHorn = (block[0] & kParamsDragonHornBit) != 0;
    params.attacker = battle::BattlerSlot::fromIndex(block[1] % battle::kBattlerSlots);
    params.targets = setOf(static_cast<std::uint16_t>(block[2] | block[3] << 8));
    params.targetsHit = setOf(static_cast<std::uint16_t>(block[4] | block[5] << 8));
    params.reflectedOff = setOf(static_cast<std::uint16_t>(block[6] | block[7] << 8));
    return params;
}

}  // namespace

void DamageOracle::writeEffectCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    const battle::AttackAnimationParams& params = attack.params;
    std::uint8_t paramsFlags = 0;
    paramsFlags |= params.attackerIsMonster ? kParamsMonsterBit : 0;
    paramsFlags |= params.noCharacterTargets ? kParamsNoCharacterTargetsBit : 0;
    paramsFlags |= params.castingSwirl ? kParamsSwirlBit : 0;
    paramsFlags |= params.dragonHorn ? kParamsDragonHornBit : 0;
    writeInFlightByte(kParamsFlags, paramsFlags);
    writeInFlightByte(kParamsAttacker, static_cast<std::uint8_t>(params.attacker.index()));
    writeInFlightWord(kParamsTargets, maskOf(params.targets));
    writeInFlightWord(kParamsHit, maskOf(params.targetsHit));
    writeInFlightWord(kParamsReflectedOff, maskOf(params.reflectedOff));
    writeInFlightByte(kSwirlCell, attack.castingSwirlSuppressed ? kSwirlSuppressedBit : 0);
    std::uint8_t running = state.counterattackRunning ? kCounterattackBit : 0;
    running |= state.cannotRun ? kCannotRunBit : 0;
    writeByte(kRunningCell, running);
    std::uint8_t second = readByte(kAttackFlags);
    if (attack.automaticCritical) {
        second &= static_cast<std::uint8_t>(~kAutomaticCriticalClearBit);
    }
    writeInFlightByte(kAttackFlags, second);
    std::uint8_t aimFlags = attack.scratch.aimExactly ? kAimExactlyBit : 0;
    aimFlags |= state.endTurnAfterAction ? kEndTurnBit : 0;
    writeByte(kAimFlags, aimFlags);

    writeInFlightByte(kFollowUpSpell, attack.scratch.followUpSpell ? static_cast<std::uint8_t>(*attack.scratch.followUpSpell) : kNoOne);
    writeInFlightByte(kMessage, attack.scratch.message.value_or(kNoOne));
    writeInFlightByte(kAttacksLeft, static_cast<std::uint8_t>(attack.scratch.attacksLeft));
    writeInFlightByte(kDragonHorn, attack.scratch.dragonHorn ? kDragonHornOn : 0);
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        const auto& pick = attack.scratch.mindBlastPicks[position];
        writeInFlightWord(kMindBlastPicks + static_cast<std::uint32_t>(position * 2), pick ? maskOf(battle::BattlerSet::of(*pick)) : 0);
    }
    writeByte(kQuickTurns, state.quickTurnsLeft.value_or(kNoQuick));
    writeByte(kQuickTarget, state.quickTarget ? static_cast<std::uint8_t>(slotIndex(*state.quickTarget)) : kNoOne);
    writeByte(kEndOfBattleEvent,
              state.endOfBattleEvent ? static_cast<std::uint8_t>(static_cast<std::uint8_t>(*state.endOfBattleEvent) * 2) : 0);
    writeWord(kLeaving, maskOf(state.leavingAfterAction));
    writeByte(kAllyCount, static_cast<std::uint8_t>(state.allyCount));
    writeWord(kGolemHp, state.golemHp);
    writeInFlightWord(kMissed, maskOf(attack.scratch.missed));
    // The script's queue starts empty, and the params blocks after the frame's own.
    writeInFlightByte(kQueueEnd, 0);
    writeInFlightByte(kAnimationPlace, 0);

    std::array<std::uint8_t, 3> three{};
    writeThreeBytes(three, state.party.gil);
    writeBytes(retropp::MemoryRegion{.at = kGil, .size = 3}, three);
    writeThreeBytes(three, state.party.steps);
    writeBytes(retropp::MemoryRegion{.at = kSteps, .size = 3}, three);
    writeByte(kPlayClockFrames, state.playClockFrames);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        std::uint8_t field = battler.present ? kPresentBit : 0;
        field |= battler.inBattleMenu ? kInBattleMenuBit : 0;
        field |= battler.menuMayOpen ? kMenuMayOpenBit : 0;
        writeByte(kOnTheField + at, field);
        writeByte(kSeizing + at, battler.seizing ? static_cast<std::uint8_t>(slotIndex(*battler.seizing)) : kNoOne);
        writeByte(kMonsterBattleFlags + at, battler.monsterFlags.bits);
        writeWord(kGold + at, battler.gold);
        if (slot.isCharacter()) {
            writeByte(kHandPower + at, battler.weaponHand.battlePower);
            writeByte(kHandPower + at + 1, battler.shieldHand.battlePower);
            const auto place = static_cast<std::uint32_t>(slot.position()) * kHandItemSize;
            const battle::BattleItem& weapon = state.inventory.weaponHands[static_cast<std::size_t>(slot.position())];
            const battle::BattleItem& shield = state.inventory.shieldHands[static_cast<std::size_t>(slot.position())];
            writeByte(kWeaponHands + place, static_cast<std::uint8_t>(weapon.item));
            writeByte(kWeaponHands + place + kHandQuantity, weapon.quantity);
            writeByte(kShieldHands + place, static_cast<std::uint8_t>(shield.item));
            writeByte(kShieldHands + place + kHandQuantity, shield.quantity);
            writeByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position()), battler.equipmentChanged ? 1 : 0);
        }
    }
}

AttackerEffectResult DamageOracle::attackerEffect(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    // The frame calls the routine with the carry its facing step (UpdateFacingDirection) leaves: set
    // after a blow by a party member in a side attack or by a monster in a pincer or a side attack.
    const battle::BattleType layout = scenario.state.battleType;
    const bool blow = scenario.attack.record.traits.has(AttackTrait::PHYSICAL);
    const bool carry = blow && (scenario.attacker.isCharacter()
                                    ? layout == battle::BattleType::Side
                                    : layout == battle::BattleType::Pincer || layout == battle::BattleType::Side);
    const auto status = static_cast<std::uint8_t>(kByteWideStatus | (carry ? kCarryBit : 0));
    call([&] { doAttackerEffect_(slotIndex(scenario.attacker), status, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedRoutines[5]);
    return readAttackerEffectResult();
}

namespace {

// The cells both sides are read into, by name.
struct EffectCells {
    std::map<std::string, std::string> cells;
    void put(const std::string& name, const std::string& value) { cells[name] = value; }
    void put(const std::string& name, int value) { cells[name] = std::to_string(value); }
};

void putRecord(EffectCells& out, const AttackProperties& record) {
    out.put("power", record.power);
    out.put("hit rate", record.hitRate);
    out.put("traits", record.traits.bits);
    out.put("flags 1", record.flags1.bits);
    out.put("flags 2", record.flags2.bits);
}

}  // namespace

AttackerEffectResult DamageOracle::readAttackerEffectResult() {
    EffectCells out;
    std::array<std::uint8_t, sizeof(AttackProperties)> bytes{};
    const std::vector<std::uint8_t> read =
        machine_.read(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(bytes.size())});
    std::copy(read.begin(), read.end(), bytes.begin());
    AttackProperties record{};
    std::memcpy(&record, bytes.data(), bytes.size());
    putRecord(out, record);
    out.put("effect", readByte(kEffect) / 2);
    out.put("damage", readWord(kDamage));
    out.put("command", readByte(kCommand));
    out.put("attack", readByte(kAttackIndex));
    out.put("second value", readByte(kSecondary));
    out.put("can be critical", (readByte(kFirstAttackFlags) & kCannotBeCriticalBit) == 0);
    out.put("automatic critical", (readByte(kAttackFlags) & kAutomaticCriticalClearBit) == 0);
    out.put("swirl suppressed", (readByte(kSwirlCell) & kSwirlSuppressedBit) != 0);
    out.put("multiplier", readByte(kMultiplier));
    const std::vector<std::uint8_t> params = machine_.read(retropp::MemoryRegion{.at = kParamsFlags, .size = 8});
    out.put("params", paramsText(paramsFromBytes(params)));
    out.put("targets", std::to_string(readWord(kTargets)));
    out.put("random target", (readByte(kAimingRules) & kRandomTargetBit) != 0);
    out.put("fall back to earlier targets", (readByte(kAimingRules) & kFallBackToEarlierTargetsBit) != 0);
    out.put("other side only", (readByte(kAimingRules) & kOpposingSideBit) != 0);
    const std::uint8_t spell = readByte(kFollowUpSpell);
    out.put("spell next", spell == kNoOne ? std::string{"none"} : std::to_string(spell));
    const std::uint8_t message = readByte(kMessage);
    out.put("message", message == kNoOne ? std::string{"none"} : std::to_string(message));
    out.put("damage modification", readByte(kDamageModification) != 0);
    out.put("attacks left", readByte(kAttacksLeft));
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        out.put("Mind Blast pick " + std::to_string(position),
                std::to_string(readWord(kMindBlastPicks + static_cast<std::uint32_t>(position * 2))));
    }
    out.put("shown Miss", std::to_string(readWord(kMissed)));
    const std::uint8_t hits = readByte(kExtraHits);
    out.put("hits to come", hits == kNoHitsToCome ? std::string{"none"} : std::to_string(hits + 1));
    out.put("the Golem's HP", readWord(kGolemHp));
    out.put("Forcefield", readByte(kForcefield));
    out.put("end of battle", readByte(kEndOfBattleEvent) / 2);
    const std::uint8_t quickTurns = readByte(kQuickTurns);
    out.put("Quick's turns", quickTurns == kNoQuick ? std::string{"none"} : std::to_string(quickTurns));
    out.put("Quick's target", slotText(slotTimesTwo(readByte(kQuickTarget))));
    out.put("leaving", std::to_string(readWord(kLeaving)));
    out.put("turn ends", (readByte(kAimFlags) & kEndTurnBit) != 0);
    const std::vector<std::uint8_t> gil = machine_.read(retropp::MemoryRegion{.at = kGil, .size = 3});
    out.put("gil", gil[0] | gil[1] << 8 | gil[2] << 16);
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::string of = " of battler " + std::to_string(i);
        out.put("HP" + of, readWord(kHp + at));
        out.put("MP" + of, readWord(kMp + at));
        out.put("statuses to set" + of,
                statusesText(StatusSet{{readByte(kToSet12 + at), readByte(kToSet12 + at + 1), readByte(kToSet34 + at),
                                        readByte(kToSet34 + at + 1)}}));
        out.put("last attacker" + of, readByte(kLastAttacker + at));
        out.put("gold" + of, readWord(kGold + at));
        if (slot.isCharacter()) {
            const auto place = static_cast<std::uint32_t>(slot.position()) * kHandItemSize;
            out.put("hands' power" + of,
                    std::to_string(readByte(kHandPower + at)) + "/" + std::to_string(readByte(kHandPower + at + 1)));
            out.put("hands" + of, std::to_string(readByte(kWeaponHands + place)) + "x" +
                                      std::to_string(readByte(kWeaponHands + place + kHandQuantity)) + "/" +
                                      std::to_string(readByte(kShieldHands + place)) + "x" +
                                      std::to_string(readByte(kShieldHands + place + kHandQuantity)));
            out.put("equipment to work out" + of,
                    readByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position())) != 0);
        }
    }
    // What the effect added to the script: the animations queued, and the params blocks after the
    // frame's own.
    out.put("script animations", readByte(kQueueEnd) / kQueueCommandSize);
    const int blocks = (readByte(kNextParamsBlock) - kParamsBlockSize) / kParamsBlockSize;
    out.put("script params blocks", blocks);
    for (int block = 0; block < blocks; ++block) {
        const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{
            .at = kParamsBlocks + kParamsBlockSize * static_cast<std::uint32_t>(block + 1), .size = kParamsBlockSize});
        out.put("script params " + std::to_string(block), paramsText(paramsFromBytes(bytes)));
    }
    out.put("random-number position", readByte(kRngPosition));
    return AttackerEffectResult{.cells = std::move(out.cells)};
}

AttackerEffectRun runAttackerEffect(DamageScenario scenario) {
    AttackerEffectRun run{.after = std::move(scenario)};
    battle::attackerEffect(run.after.state, run.after.attack, run.after.attacker, run.script);
    return run;
}

AttackerEffectResult attackerEffectResult(const AttackerEffectRun& run) {
    const battle::BattleState& state = run.after.state;
    const battle::AttackInFlight& attack = run.after.attack;
    EffectCells out;
    putRecord(out, attack.record);
    out.put("effect", attack.effect.index());
    out.put("damage", static_cast<std::uint16_t>(attack.damage));
    out.put("command", static_cast<std::uint8_t>(attack.command));
    out.put("attack", static_cast<std::uint8_t>(attack.attack));
    out.put("second value", attack.secondary);
    out.put("can be critical", attack.canBeCritical);
    out.put("automatic critical", attack.automaticCritical);
    out.put("swirl suppressed", attack.castingSwirlSuppressed);
    out.put("multiplier", attack.multiplier);
    out.put("params", paramsText(attack.params));
    out.put("targets", std::to_string(maskOf(attack.targets)));
    out.put("random target", attack.rules.randomTarget);
    out.put("fall back to earlier targets", attack.rules.fallBackToEarlierTargets);
    out.put("other side only", attack.rules.opposingSideOnly);
    out.put("spell next", attack.scratch.followUpSpell ? std::to_string(static_cast<int>(*attack.scratch.followUpSpell))
                                                       : std::string{"none"});
    out.put("message", attack.scratch.message ? std::to_string(*attack.scratch.message) : std::string{"none"});
    out.put("damage modification", attack.scratch.damageModification);
    out.put("attacks left", static_cast<std::uint8_t>(attack.scratch.attacksLeft));
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        const auto& pick = attack.scratch.mindBlastPicks[position];
        out.put("Mind Blast pick " + std::to_string(position),
                std::to_string(pick ? maskOf(battle::BattlerSet::of(*pick)) : 0));
    }
    out.put("shown Miss", std::to_string(maskOf(attack.scratch.missed)));
    out.put("hits to come", state.extraHits ? std::to_string(*state.extraHits) : std::string{"none"});
    out.put("the Golem's HP", state.golemHp);
    out.put("Forcefield", state.elementsNullifiedByForcefield.bits);
    out.put("end of battle", state.endOfBattleEvent ? static_cast<int>(*state.endOfBattleEvent) : 0);
    out.put("Quick's turns", state.quickTurnsLeft ? std::to_string(*state.quickTurnsLeft) : std::string{"none"});
    out.put("Quick's target", slotText(state.quickTarget));
    out.put("leaving", std::to_string(maskOf(state.leavingAfterAction)));
    out.put("turn ends", state.endTurnAfterAction);
    out.put("gil", static_cast<int>(state.party.gil));
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::string of = " of battler " + std::to_string(i);
        out.put("HP" + of, battler.hp);
        out.put("MP" + of, battler.mp);
        out.put("statuses to set" + of, statusesText(battler.statusesToSet));
        const std::uint8_t mark = markByte(battler);
        out.put("last attacker" + of, mark);
        out.put("gold" + of, battler.gold);
        if (slot.isCharacter()) {
            const auto position = static_cast<std::size_t>(slot.position());
            const battle::BattleItem& weapon = state.inventory.weaponHands[position];
            const battle::BattleItem& shield = state.inventory.shieldHands[position];
            out.put("hands' power" + of,
                    std::to_string(battler.weaponHand.battlePower) + "/" + std::to_string(battler.shieldHand.battlePower));
            out.put("hands" + of, std::to_string(static_cast<int>(weapon.item)) + "x" + std::to_string(weapon.quantity) +
                                      "/" + std::to_string(static_cast<int>(shield.item)) + "x" +
                                      std::to_string(shield.quantity));
            out.put("equipment to work out" + of, battler.equipmentChanged);
        }
    }
    battle::BattleScriptBuilder script = run.script;
    const std::vector<battle::BattleScriptCommand> commands = script.take();
    int animations = 0;
    std::vector<std::string> blocks;
    for (const battle::BattleScriptCommand& command : commands) {
        if (const auto* animation = std::get_if<battle::AttackAnimation>(&command)) {
            ++animations;
            blocks.push_back(paramsText(animation->params));
        }
    }
    out.put("script animations", animations);
    out.put("script params blocks", static_cast<int>(blocks.size()));
    for (std::size_t block = 0; block < blocks.size(); ++block) {
        out.put("script params " + std::to_string(block), blocks[block]);
    }
    out.put("random-number position", state.rng.position());
    return AttackerEffectResult{.cells = std::move(out.cells)};
}

std::string differences(const AttackerEffectResult& port, const AttackerEffectResult& guest) {
    std::string out;
    for (const auto& [cell, theirs] : guest.cells) {
        const auto ours = port.cells.find(cell);
        const std::string value = ours == port.cells.end() ? std::string{"(not read)"} : ours->second;
        if (value != theirs) {
            out += cell + ": port " + value + ", cartridge " + theirs + "\n";
        }
    }
    return out;
}

DamageScenario drawAttackerEffectScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    constexpr std::uint8_t kEffectSpace = battle::SpecialEffect::of(battle::CommandEffect::AirAnchor).index() + 1;
    attack.effect = effectAt(static_cast<std::uint8_t>(rng() % kEffectSpace));
    // A weapon's effect comes from a party member's hand.
    constexpr std::uint8_t kLastWeaponEffectIndex = 14;
    if (attack.effect.index() != 0 && attack.effect.index() <= kLastWeaponEffectIndex && s.attacker.isMonster()) {
        s.attacker = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    attack.record.hitRate = byte(rng);
    // The attacker's level as the attack loads it: a party member's 1 to 99, as every Dice and Fixed
    // Dice wielder's is.
    attack.attackerLevel = static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1));
    attack.damage = upTo(rng, battle::kDamageWordMax);
    attack.canBeCritical = oneIn(rng, 2);
    attack.automaticCritical = oneIn(rng, 4);
    attack.castingSwirlSuppressed = oneIn(rng, 2);
    attack.command = oneIn(rng, 2) ? BattleCommandId::FIGHT : static_cast<BattleCommandId>(byte(rng) % 0x20);
    attack.targets = anyBattlers(rng);
    attack.rules.randomTarget = oneIn(rng, 4);
    attack.rules.opposingSideOnly = oneIn(rng, 2);
    attack.params.attacker = s.attacker;
    attack.params.attackerIsMonster = s.attacker.isMonster();
    attack.params.targets = anyBattlers(rng);
    attack.params.targetsHit = oneIn(rng, 2) ? attack.params.targets : anyBattlers(rng);
    attack.params.noCharacterTargets = attack.params.targets.charactersOnly().empty();
    attack.params.castingSwirl = oneIn(rng, 2);
    attack.scratch.targetCount = 1 + upTo(rng, battle::kBattlerSlots - 1);
    attack.scratch.attacksLeft = upTo(rng, 3);
    attack.scratch.dragonHorn = oneIn(rng, 8);
    attack.scratch.missed = oneIn(rng, 4) ? anyBattlers(rng) : battle::BattlerSet{};
    if (oneIn(rng, 4)) {
        attack.scratch.message = static_cast<std::uint8_t>(byte(rng) % kNoOne);
    }
    if (oneIn(rng, 4)) {
        attack.scratch.followUpSpell = static_cast<AttackId>(byte(rng) % 0xFF);
    }

    state.party.gil = oneIn(rng, 4) ? static_cast<std::uint32_t>(upTo(rng, 5000))
                                    : static_cast<std::uint32_t>(upTo(rng, static_cast<int>(kMaxGil)));
    state.party.steps = static_cast<std::uint32_t>(upTo(rng, static_cast<int>(kMaxSteps)));
    state.playClockFrames = byte(rng);
    state.counterattackRunning = oneIn(rng, 4);
    state.cannotRun = oneIn(rng, 2);
    state.allyCount = upTo(rng, battle::kPartySlots);
    state.golemHp = oneIn(rng, 2) ? static_cast<std::uint16_t>(upTo(rng, battle::kDamageWordMax)) : 0;
    state.endTurnAfterAction = oneIn(rng, 4);
    state.awayFromTheField = oneIn(rng, 4) ? anyBattlers(rng) : battle::BattlerSet{};
    if (oneIn(rng, 2)) {
        state.quickTurnsLeft = static_cast<std::uint8_t>(upTo(rng, 2));
        state.quickTarget = anySlot(rng);
    }
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battler.present = !oneIn(rng, 4);
        battler.inBattleMenu = oneIn(rng, 4);
        battler.menuMayOpen = oneIn(rng, 2);
        battler.monsterFlags.bits = sparse(rng);
        battler.gold = oneIn(rng, 4) ? 0 : static_cast<std::uint16_t>(upTo(rng, battle::kDamageWordMax));
        if (oneIn(rng, 2)) {
            battler.status.set(StatusId::REFLECT);
        }
        if (oneIn(rng, 4)) {
            battler.seizing = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
        }
        if (slot.isCharacter()) {
            battler.weaponHand.battlePower = byte(rng);
            battler.shieldHand.battlePower = byte(rng);
            battler.equipmentChanged = oneIn(rng, 4);
            const auto position = static_cast<std::size_t>(slot.position());
            const auto hand = [&] {
                return battle::BattleItem{.item = oneIn(rng, 2) ? ItemId::OGRE_NIX : static_cast<ItemId>(byte(rng)),
                                          .quantity = 1};
            };
            state.inventory.weaponHands[position] = hand();
            state.inventory.shieldHands[position] = hand();
        }
    }
    return s;
}

// --- the special effect on each target -----------------------------------------------------------

namespace {

// Per battler, at twice the battler's index: the Control link the other way from kController, the
// battler holding it in Seize, the battler it charms, Love Token's two, Targetting's target, the item
// it holds, its AI variable, its two items to steal, its metamorph byte, its monster (a word), the
// byte holding Air Anchor (bit 2, clear while anchored), and the first of its actions waiting.
constexpr std::uint32_t kControlling = kWorkRam + 0x32B8;
constexpr std::uint32_t kSeizedBy = kWorkRam + 0x3359;
constexpr std::uint32_t kCharming = kWorkRam + 0x3394;
constexpr std::uint32_t kBodyguard = kWorkRam + 0x336C;
constexpr std::uint32_t kGuarding = kWorkRam + 0x336D;
constexpr std::uint32_t kItemInHand = kWorkRam + 0x32F4;
constexpr std::uint32_t kTargetingTarget = kWorkRam + 0x32F5;
constexpr std::uint32_t kAiVariable = kWorkRam + 0x3DAC;
constexpr std::uint32_t kRareSteal = kWorkRam + 0x3308;
constexpr std::uint32_t kCommonSteal = kWorkRam + 0x3309;
constexpr std::uint32_t kMetamorph = kWorkRam + 0x3C94;
constexpr std::uint32_t kMonsterIndex = kWorkRam + 0x1FF9;
constexpr std::uint32_t kAnchorCell = kWorkRam + 0x3205;
constexpr std::uint32_t kFirstAction = kWorkRam + 0x32CC;
// The battle's: the formation's flags (bit 3: Leap disabled), the party members holding an item,
// engulfed, taken from the party and gone from the battle, the monsters drawn flipped, Zinger's
// monster and the party member it swallowed, the monster Sketch caught, the message's item and
// amount, and whether an attack with no one left is shown (negative: shown).
constexpr std::uint32_t kFormationFlags = kWorkRam + 0x2F49;
constexpr std::uint32_t kHoldingItem = kWorkRam + 0x3A8C;
constexpr std::uint32_t kEngulfed = kWorkRam + 0x3A8A;
constexpr std::uint32_t kTakenFromTheParty = kWorkRam + 0x3A88;
constexpr std::uint32_t kPartyLeft = kWorkRam + 0x3A39;
constexpr std::uint32_t kFlipped = kWorkRam + 0x2F54;
// The battlers drawn facing the other way, a word as the statuses and Control set it: party members
// in its low byte (Confuse), monsters in its high byte (kFlipped).
constexpr std::uint32_t kFlippedSet = kWorkRam + 0x2F53;
constexpr std::uint32_t kZingerMonster = kWorkRam + 0x33F8;
constexpr std::uint32_t kZingerSwallowed = kWorkRam + 0x33F9;
constexpr std::uint32_t kSketched = kWorkRam + 0x3417;
constexpr std::uint32_t kMessageItem = kWorkRam + 0x2F35;
constexpr std::uint32_t kMessageAmount = kWorkRam + 0x2F38;
constexpr std::uint32_t kShownWithoutTargets = kWorkRam + 0x341D;
// The battle's one list of actions: each slot's next slot (its own at the list's end, negative when
// free), and its command and attack, targets and MP cost as words at twice the slot; the first of
// the actions that interrupt the line.
constexpr std::uint32_t kActionNext = kWorkRam + 0x3184;
constexpr std::uint32_t kActionCommand = kWorkRam + 0x3420;
constexpr std::uint32_t kActionTargets = kWorkRam + 0x3520;
constexpr std::uint32_t kActionMp = kWorkRam + 0x3620;
constexpr std::uint32_t kFirstImmediateAction = kWorkRam + 0x340A;
constexpr std::uint32_t kActionSlots = 128;
// Each party member's control menu: four rows of three bytes — the attack, a byte whose top bit is
// set when it cannot be cast, and its targeting.
constexpr std::uint32_t kControlMenus = kWorkRam + 0x205E;
constexpr std::uint32_t kControlMenuSize = 12;
constexpr std::uint32_t kControlRowSize = 3;

constexpr std::uint8_t kEndingBit = 0x80;
constexpr std::uint8_t kNotAnchoredBit = 0x04;
constexpr std::uint8_t kControlMenuBit = 0x01;
constexpr std::uint8_t kUncastableBit = 0x80;
constexpr std::uint8_t kShownBit = 0x80;
constexpr std::uint8_t kFreeSlot = 0xFF;
constexpr std::uint8_t kMessageCommand = 0x02;
// The commands the interrupting actions an effect makes carry: a monster's exit or entrance, Scan.
constexpr std::uint8_t kVisibilityCommand = 0x24;
constexpr std::uint8_t kScanCommand = 0x27;

std::uint8_t slotByte(const std::optional<battle::BattlerSlot>& slot) {
    return slot ? static_cast<std::uint8_t>(slotIndex(*slot)) : kNoOne;
}

std::uint8_t linkByte(const std::optional<battle::BattlerSlot>& slot, bool ending) {
    return slot ? static_cast<std::uint8_t>(slotIndex(*slot) | (ending ? kEndingBit : 0)) : kNoOne;
}

std::uint8_t commandByte(const std::variant<BattleCommandId, battle::SpecialAnimation>& what) {
    return std::visit([](auto value) { return static_cast<std::uint8_t>(value); }, what);
}

std::string actionText(std::uint8_t command, std::uint8_t attack, std::uint16_t targets, std::uint8_t mp) {
    return "command " + std::to_string(command) + " attack " + std::to_string(attack) + " targets " +
           std::to_string(targets) + " mp " + std::to_string(mp);
}

std::string actionsText(const std::deque<battle::Action>& actions) {
    std::string out;
    for (const battle::Action& action : actions) {
        if (const auto* command = std::get_if<battle::CommandAction>(&action)) {
            out += actionText(static_cast<std::uint8_t>(command->command), static_cast<std::uint8_t>(command->attack),
                              maskOf(command->targets), command->mpCost) +
                   "; ";
        } else {
            out += "other; ";
        }
    }
    return out.empty() ? std::string{"none"} : out;
}

std::string immediateText(std::uint8_t command, std::uint8_t attack, std::uint16_t targets) {
    if (command == kScanCommand) {
        return "scan " + std::to_string(attack / 2);
    }
    if (command == kVisibilityCommand) {
        return "visibility mode " + std::to_string(targets & 0xFF) + " monsters " + std::to_string(targets >> 8) +
               " type " + std::to_string(attack);
    }
    return "command " + std::to_string(command) + " attack " + std::to_string(attack) + " targets " +
           std::to_string(targets);
}

std::string immediateActionsText(const std::deque<battle::Action>& actions) {
    std::string out;
    for (const battle::Action& action : actions) {
        if (const auto* scan = std::get_if<battle::ScanAction>(&action)) {
            out += immediateText(kScanCommand, static_cast<std::uint8_t>(slotIndex(scan->target)), 0) + "; ";
        } else if (const auto* shown = std::get_if<battle::MonsterVisibilityAction>(&action)) {
            out += immediateText(kVisibilityCommand, static_cast<std::uint8_t>(shown->type),
                                 static_cast<std::uint16_t>(static_cast<std::uint8_t>(shown->mode) |
                                                            shown->monsters.monsters.bits << 8)) +
                   "; ";
        } else if (const auto* command = std::get_if<battle::CommandAction>(&action)) {
            out += immediateText(static_cast<std::uint8_t>(command->command), static_cast<std::uint8_t>(command->attack),
                                 maskOf(command->targets)) +
                   "; ";
        } else {
            out += "other; ";
        }
    }
    return out.empty() ? std::string{"none"} : out;
}

std::string controlMenuText(const std::array<battle::ControlMenuEntry, 4>& menu) {
    std::string out;
    for (const battle::ControlMenuEntry& row : menu) {
        out += std::to_string(static_cast<std::uint8_t>(row.attack)) + (row.castable ? " castable " : " not castable ") +
               std::to_string(row.targeting.bits) + "; ";
    }
    return out;
}

std::uint8_t triggersByte(const battle::BattlerTriggers& triggers) {
    std::uint8_t bits = triggers.overcast ? kOvercastBit : 0;
    bits |= triggers.phantasm ? kPhantasmBit : 0;
    bits |= triggers.usesControlMenu ? kControlMenuBit : 0;
    return bits;
}

battle::BattlerSet seizedSet(const battle::BattleState& state) {
    battle::BattlerSet seized;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (state.at(battle::BattlerSlot::character(position)).seizedBy) {
            seized.add(battle::BattlerSlot::character(position));
        }
    }
    return seized;
}

// The port's script as the cartridge's queue reads: the attack's own animation the frame put there,
// each animation by its command, attack and second value, each message by its index.
std::string scriptCommandText(const battle::BattleScriptCommand& command, bool first) {
    if (const auto* animation = std::get_if<battle::AttackAnimation>(&command)) {
        if (first && animation->secondary == kUnwritten && animation->attack == static_cast<AttackId>(kUnwritten)) {
            return "the frame's animation";
        }
        return "animation " + std::to_string(commandByte(animation->what)) + " " +
               std::to_string(static_cast<std::uint8_t>(animation->attack)) + " " + std::to_string(animation->secondary);
    }
    if (const auto* message = std::get_if<battle::BattleMessage>(&command)) {
        return "message " + std::to_string(message->message);
    }
    return "other";
}

}  // namespace

void DamageOracle::writeTargetEffectCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    writeByte(kFormationFlags, getFormationAux(state.formation).flags);
    writeByte(kHoldingItem, state.holdingItem.characters.bits);
    writeByte(kEngulfed, state.engulfed.characters.bits);
    writeByte(kTakenFromTheParty, state.takenFromTheParty.characters.bits);
    writeByte(kPartyLeft, state.partyLeft.characters.bits);
    writeByte(kMonstersFallen, state.monstersFallen.monsters.bits);
    writeWord(kFlippedSet, maskOf(state.flippedByControl));
    writeByte(kZingerMonster, slotByte(state.heldByZinger));
    writeByte(kZingerSwallowed, slotByte(state.swallowedByZinger));
    writeInFlightByte(kSketched, attack.scratch.sketch ? static_cast<std::uint8_t>(slotIndex(attack.scratch.sketch->monster)) : kNoOne);
    writeInFlightByte(kMessageItem, attack.scratch.messageItem ? static_cast<std::uint8_t>(*attack.scratch.messageItem) : kNoOne);
    std::array<std::uint8_t, 3> amount{};
    writeThreeBytes(amount, attack.scratch.messageAmount.value_or(0));
    writeInFlightBytes(retropp::MemoryRegion{.at = kMessageAmount, .size = 3}, amount);
    writeInFlightByte(kShownWithoutTargets, attack.scratch.hideWithoutTargets ? 0 : kShownBit);

    // The script's queue holds the attack's own animation, put there by the frame; a rewrite of it
    // shows on either side.
    writeInFlightByte(kAnimationPlace, 0);
    writeInFlightByte(kQueueEnd, kQueueCommandSize);
    const std::array<std::uint8_t, kQueueCommandSize> unwritten{kUnwritten, kUnwritten, kUnwritten, kUnwritten};
    writeInFlightBytes(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize}, unwritten);

    // Every list empty; then each battler's waiting commands, handed slots from the top down as the
    // battle hands them out.
    std::array<std::uint8_t, kActionSlots> free{};
    free.fill(kFreeSlot);
    writeBytes(retropp::MemoryRegion{.at = kActionNext, .size = kActionSlots}, free);
    writeByte(kFirstImmediateAction, kFreeSlot);
    std::uint8_t next = kActionSlots - 1;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        std::uint8_t head = kFreeSlot;
        std::optional<std::uint8_t> previous;
        for (const battle::Action& action : battler.actions) {
            const auto* command = std::get_if<battle::CommandAction>(&action);
            if (command == nullptr) {
                continue;
            }
            const std::uint8_t slot = next--;
            const auto word = static_cast<std::uint32_t>(slot) * 2;
            writeWord(kActionCommand + word, static_cast<std::uint16_t>(static_cast<std::uint8_t>(command->command) |
                                                                        static_cast<std::uint8_t>(command->attack) << 8));
            writeWord(kActionTargets + word, maskOf(command->targets));
            writeWord(kActionMp + word, command->mpCost);
            writeByte(kActionNext + slot, slot);
            if (previous) {
                writeByte(kActionNext + *previous, slot);
            } else {
                head = slot;
            }
            previous = slot;
        }
        writeByte(kFirstAction + at, head);
    }

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        writeByte(kControlling + at, linkByte(battler.controlling, battler.controlEnding));
        writeByte(kController + at, linkByte(battler.controlledBy, battler.controlEnding));
        writeByte(kSeizedBy + at, slotByte(battler.seizedBy));
        writeByte(kCharming + at, slotByte(battler.charming));
        writeByte(kBodyguard + at, slotByte(battler.bodyguard));
        writeByte(kGuarding + at, slotByte(battler.guarding));
        writeByte(kItemInHand + at, battler.itemInHand ? static_cast<std::uint8_t>(*battler.itemInHand) : kNoOne);
        writeByte(kTargetingTarget + at, slotByte(battler.targetingTarget));
        writeByte(kAiVariable + at, battler.aiVariable);
        writeByte(kRareSteal + at, static_cast<std::uint8_t>(battler.rareSteal));
        writeByte(kCommonSteal + at, static_cast<std::uint8_t>(battler.commonSteal));
        writeByte(kMetamorph + at, battler.metamorph.packed);
        writeWord(kMonsterIndex + at, static_cast<std::uint16_t>(battler.monster.value_or(MonsterId{})));
        writeByte(kAnchorCell + at, battler.airAnchored ? 0 : kNotAnchoredBit);
        writeByte(kMoreTriggers + at, triggersByte(battler.triggers));
        if (slot.isCharacter()) {
            const auto place = kControlMenus + static_cast<std::uint32_t>(slot.position()) * kControlMenuSize;
            for (std::size_t row = 0; row < battler.controlMenu.size(); ++row) {
                const battle::ControlMenuEntry& entry = battler.controlMenu[row];
                const auto first = place + static_cast<std::uint32_t>(row) * kControlRowSize;
                writeByte(first, static_cast<std::uint8_t>(entry.attack));
                writeByte(first + 1, entry.castable ? 0 : kUncastableBit);
                writeByte(first + 2, entry.targeting.bits);
            }
        }
    }
}

TargetEffectResult DamageOracle::targetEffect(const DamageScenario& scenario, battle::BattlerSlot target) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    writeTargetEffectCells(scenario);
    call([&] {
        doTargetEffect_(slotIndex(scenario.attacker), slotIndex(target), kByteWideStatus, kBattleDataBank,
                        kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[6]);
    return readTargetEffectResult();
}

TargetEffectResult DamageOracle::readTargetEffectResult() {
    EffectCells out;
    out.put("missed by the effect", readWord(kMissedByEffect) != 0);
    std::array<std::uint8_t, sizeof(AttackProperties)> bytes{};
    const std::vector<std::uint8_t> read =
        machine_.read(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(bytes.size())});
    std::copy(read.begin(), read.end(), bytes.begin());
    AttackProperties record{};
    std::memcpy(&record, bytes.data(), bytes.size());
    putRecord(out, record);
    out.put("damage", readWord(kDamage));
    out.put("multiplier", readByte(kMultiplier));
    out.put("command", readByte(kCommand));
    out.put("attack", readByte(kAttackIndex));
    out.put("second value", readByte(kSecondary));
    out.put("targets", std::to_string(readWord(kTargets)));
    const std::vector<std::uint8_t> params = machine_.read(retropp::MemoryRegion{.at = kParamsFlags, .size = 8});
    out.put("params", paramsText(paramsFromBytes(params)));
    out.put("earlier targets", std::to_string(readWord(kEarlierTargets)));
    out.put("shown Miss", std::to_string(readWord(kMissed)));
    const std::uint8_t message = readByte(kMessage);
    out.put("message", message == kNoOne ? std::string{"none"} : std::to_string(message));
    out.put("message item", readByte(kMessageItem));
    out.put("message amount", readWord(kMessageAmount));
    const std::uint8_t spell = readByte(kFollowUpSpell);
    out.put("spell next", spell == kNoOne ? std::string{"none"} : std::to_string(spell));
    out.put("sketched", slotText(slotTimesTwo(readByte(kSketched))));
    out.put("counterattacks", (readByte(kCountersEnabled) & kCountersOn) != 0);
    out.put("hidden without targets", (readByte(kShownWithoutTargets) & kShownBit) == 0);
    out.put("leaving", std::to_string(readWord(kLeaving)));
    out.put("party gone", readByte(kPartyLeft));
    out.put("monsters fallen", readByte(kMonstersFallen));
    out.put("engulfed", readByte(kEngulfed));
    out.put("taken from the party", readByte(kTakenFromTheParty));
    out.put("holding an item", readByte(kHoldingItem));
    out.put("flipped", readByte(kFlipped));
    out.put("Zinger's monster", slotText(slotTimesTwo(readByte(kZingerMonster))));
    out.put("Zinger swallowed", slotText(slotTimesTwo(readByte(kZingerSwallowed))));
    out.put("seized", static_cast<std::uint8_t>(~readByte(kReachCharacters)));
    out.put("end of battle", readByte(kEndOfBattleEvent) / 2);
    const std::vector<std::uint8_t> gil = machine_.read(retropp::MemoryRegion{.at = kGil, .size = 3});
    out.put("gil", gil[0] | gil[1] << 8 | gil[2] << 16);

    const auto list = [&](std::uint8_t head, bool immediate) {
        std::string text;
        std::uint8_t node = head;
        for (std::uint32_t step = 0; node != kFreeSlot && step < kActionSlots; ++step) {
            const auto word = static_cast<std::uint32_t>(node) * 2;
            const std::uint16_t command = readWord(kActionCommand + word);
            const std::uint16_t targets = readWord(kActionTargets + word);
            const auto commandIndex = static_cast<std::uint8_t>(command & 0xFF);
            const auto attack = static_cast<std::uint8_t>(command >> 8);
            text += immediate ? immediateText(commandIndex, attack, targets)
                              : actionText(commandIndex, attack, targets, readByte(kActionMp + word));
            text += "; ";
            const std::uint8_t following = readByte(kActionNext + node);
            if (following == node || (following & kEndingBit) != 0) {
                break;
            }
            node = following;
        }
        return text.empty() ? std::string{"none"} : text;
    };
    out.put("interrupting actions", list(readByte(kFirstImmediateAction), true));

    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::string of = " of battler " + std::to_string(i);
        const auto statuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return statusesText(StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                                           readByte(first34 + at + 1)}});
        };
        out.put("HP" + of, readWord(kHp + at));
        out.put("MP" + of, readWord(kMp + at));
        out.put("level" + of, readByte(kLevel + at));
        out.put("statuses" + of, statuses(kStatus12, kStatus34));
        out.put("statuses to set" + of, statuses(kToSet12, kToSet34));
        out.put("statuses to clear" + of, statuses(kToClear12, kToClear34));
        out.put("to work out" + of, readByte(kToUpdate + at));
        out.put("Air Anchor" + of, (readByte(kAnchorCell + at) & kNotAnchoredBit) == 0);
        out.put("back row" + of, (readByte(kStance + at) & kBackRowBit) != 0);
        out.put("gauge empties" + of, (readByte(kStance + at) & kEmptyGaugeBit) != 0);
        out.put("menu may open" + of, (readByte(kOnTheField + at) & kMenuMayOpenBit) != 0);
        out.put("triggers" + of, readByte(kMoreTriggers + at) & (kOvercastBit | kPhantasmBit | kControlMenuBit));
        out.put("elements" + of, std::to_string(readByte(kAbsorbedNullified + at)) + "/" +
                                     std::to_string(readByte(kAbsorbedNullified + at + 1)) + "/" +
                                     std::to_string(readByte(kWeakHalved + at)) + "/" +
                                     std::to_string(readByte(kWeakHalved + at + 1)));
        out.put("controlling" + of, readByte(kControlling + at));
        out.put("controlled by" + of, readByte(kController + at));
        out.put("seizing" + of, readByte(kSeizing + at));
        out.put("seized by" + of, readByte(kSeizedBy + at));
        out.put("charming" + of, readByte(kCharming + at));
        out.put("charmed by" + of, readByte(kCharmedBy + at));
        out.put("bodyguard" + of, readByte(kBodyguard + at));
        out.put("guarding" + of, readByte(kGuarding + at));
        out.put("Targetting" + of, readByte(kTargetingTarget + at));
        out.put("item in hand" + of, readByte(kItemInHand + at));
        out.put("AI variable" + of, readByte(kAiVariable + at));
        out.put("items to steal" + of,
                std::to_string(readByte(kRareSteal + at)) + "/" + std::to_string(readByte(kCommonSteal + at)));
        out.put("gold" + of, readWord(kGold + at));
        out.put("last hit with" + of, readByte(kLastHitCommand + at));
        out.put("last attacker" + of, readByte(kLastAttacker + at));
        out.put("actions" + of, list(readByte(kFirstAction + at), false));
        if (slot.isCharacter()) {
            const auto place = kControlMenus + static_cast<std::uint32_t>(slot.position()) * kControlMenuSize;
            std::array<battle::ControlMenuEntry, 4> menu{};
            for (std::size_t row = 0; row < menu.size(); ++row) {
                const auto first = place + static_cast<std::uint32_t>(row) * kControlRowSize;
                menu[row] = battle::ControlMenuEntry{.attack = static_cast<AttackId>(readByte(first)),
                                                     .castable = (readByte(first + 1) & kUncastableBit) == 0,
                                                     .targeting = Targeting{readByte(first + 2)}};
            }
            out.put("control menu" + of, controlMenuText(menu));
        }
    }

    std::string commands;
    const int queued = readByte(kQueueEnd) / kQueueCommandSize;
    for (int command = 0; command < queued; ++command) {
        const std::vector<std::uint8_t> entry = machine_.read(retropp::MemoryRegion{
            .at = kQueue + static_cast<std::uint32_t>(command) * kQueueCommandSize, .size = kQueueCommandSize});
        if (std::all_of(entry.begin(), entry.end(), [](std::uint8_t b) { return b == kUnwritten; })) {
            commands += "the frame's animation; ";
        } else if (entry[0] == kMessageCommand) {
            commands += "message " + std::to_string(entry[1]) + "; ";
        } else if (entry[0] == kShowAnimationCommand) {
            commands += "animation " + std::to_string(entry[1]) + " " + std::to_string(entry[2]) + " " +
                        std::to_string(entry[3]) + "; ";
        } else {
            commands += "other; ";
        }
    }
    out.put("script", commands);
    const int blocks = (readByte(kNextParamsBlock) - kParamsBlockSize) / kParamsBlockSize;
    out.put("script params blocks", blocks);
    for (int block = 0; block < blocks; ++block) {
        const std::vector<std::uint8_t> block_bytes = machine_.read(retropp::MemoryRegion{
            .at = kParamsBlocks + kParamsBlockSize * static_cast<std::uint32_t>(block + 1), .size = kParamsBlockSize});
        out.put("script params " + std::to_string(block), paramsText(paramsFromBytes(block_bytes)));
    }
    out.put("random-number position", readByte(kRngPosition));
    return TargetEffectResult{.cells = std::move(out.cells)};
}

TargetEffectRun runTargetEffect(DamageScenario scenario, battle::BattlerSlot target) {
    TargetEffectRun run{.after = std::move(scenario), .target = target};
    run.script.addAnimation(battle::AttackAnimation{.attack = static_cast<AttackId>(kUnwritten), .secondary = kUnwritten});
    run.missed = battle::targetEffect(run.after.state, run.after.attack, run.after.attacker, target, run.script);
    return run;
}

TargetEffectResult targetEffectResult(const TargetEffectRun& run) {
    const battle::BattleState& state = run.after.state;
    const battle::AttackInFlight& attack = run.after.attack;
    EffectCells out;
    out.put("missed by the effect", run.missed);
    putRecord(out, attack.record);
    out.put("damage", static_cast<std::uint16_t>(attack.damage));
    out.put("multiplier", static_cast<std::uint8_t>(attack.multiplier));
    out.put("command", static_cast<std::uint8_t>(attack.command));
    out.put("attack", static_cast<std::uint8_t>(attack.attack));
    out.put("second value", attack.secondary);
    out.put("targets", std::to_string(maskOf(attack.targets)));
    out.put("params", paramsText(attack.params));
    out.put("earlier targets", std::to_string(maskOf(attack.scratch.earlierTargets)));
    out.put("shown Miss", std::to_string(maskOf(attack.scratch.missed)));
    out.put("message", attack.scratch.message ? std::to_string(*attack.scratch.message) : std::string{"none"});
    out.put("message item", attack.scratch.messageItem ? static_cast<std::uint8_t>(*attack.scratch.messageItem) : kNoOne);
    out.put("message amount", static_cast<int>(attack.scratch.messageAmount.value_or(0) & 0xFFFF));
    const std::optional<AttackId>& next = attack.scratch.followUpSpell;
    out.put("spell next", next ? std::to_string(static_cast<std::uint8_t>(*next)) : std::string{"none"});
    out.put("sketched", attack.scratch.sketch ? std::to_string(attack.scratch.sketch->monster.index()) : std::string{"none"});
    out.put("counterattacks", state.scratch.countersEnabled);
    out.put("hidden without targets", attack.scratch.hideWithoutTargets);
    out.put("leaving", std::to_string(maskOf(state.leavingAfterAction)));
    out.put("party gone", state.partyLeft.characters.bits);
    out.put("monsters fallen", state.monstersFallen.monsters.bits);
    out.put("engulfed", state.engulfed.characters.bits);
    out.put("taken from the party", state.takenFromTheParty.characters.bits);
    out.put("holding an item", state.holdingItem.characters.bits);
    out.put("flipped", state.flippedByControl.monsters.bits);
    out.put("Zinger's monster", slotText(state.heldByZinger));
    out.put("Zinger swallowed", slotText(state.swallowedByZinger));
    out.put("seized", seizedSet(state).characters.bits);
    out.put("end of battle", state.endOfBattleEvent ? static_cast<int>(*state.endOfBattleEvent) : 0);
    out.put("gil", static_cast<int>(state.party.gil));
    out.put("interrupting actions", immediateActionsText(state.immediateActions));
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::string of = " of battler " + std::to_string(i);
        out.put("HP" + of, battler.hp);
        out.put("MP" + of, battler.mp);
        out.put("level" + of, battler.level);
        out.put("statuses" + of, statusesText(battler.status));
        out.put("statuses to set" + of, statusesText(battler.statusesToSet));
        out.put("statuses to clear" + of, statusesText(battler.statusesToClear));
        out.put("to work out" + of, pendingByte(battler.pendingUpdates));
        out.put("Air Anchor" + of, battler.airAnchored);
        out.put("back row" + of, battler.backRow);
        out.put("gauge empties" + of, battler.emptyGaugeAfterAction);
        out.put("menu may open" + of, battler.menuMayOpen);
        out.put("triggers" + of, triggersByte(battler.triggers));
        out.put("elements" + of, std::to_string(battler.elementsAbsorbed.bits) + "/" +
                                     std::to_string(battler.elementsNullified.bits) + "/" +
                                     std::to_string(battler.elementsWeak.bits) + "/" +
                                     std::to_string(battler.elementsHalved.bits));
        out.put("controlling" + of, linkByte(battler.controlling, battler.controlEnding));
        out.put("controlled by" + of, linkByte(battler.controlledBy, battler.controlEnding));
        out.put("seizing" + of, slotByte(battler.seizing));
        out.put("seized by" + of, slotByte(battler.seizedBy));
        out.put("charming" + of, slotByte(battler.charming));
        out.put("charmed by" + of, slotByte(battler.charmedBy));
        out.put("bodyguard" + of, slotByte(battler.bodyguard));
        out.put("guarding" + of, slotByte(battler.guarding));
        out.put("Targetting" + of, slotByte(battler.targetingTarget));
        out.put("item in hand" + of, battler.itemInHand ? static_cast<std::uint8_t>(*battler.itemInHand) : kNoOne);
        out.put("AI variable" + of, battler.aiVariable);
        out.put("items to steal" + of, std::to_string(static_cast<std::uint8_t>(battler.rareSteal)) + "/" +
                                           std::to_string(static_cast<std::uint8_t>(battler.commonSteal)));
        out.put("gold" + of, battler.gold);
        out.put("last hit with" + of,
                battler.lastHit.command ? static_cast<std::uint8_t>(battler.lastHit.command->with) : kNoOne);
        const std::uint8_t mark = markByte(battler);
        out.put("last attacker" + of, mark);
        out.put("actions" + of, actionsText(battler.actions));
        if (slot.isCharacter()) {
            out.put("control menu" + of, controlMenuText(battler.controlMenu));
        }
    }
    std::string commands;
    for (std::size_t command = 0; command < run.script.commands().size(); ++command) {
        commands += scriptCommandText(run.script.commands()[command], command == 0) + "; ";
    }
    out.put("script", commands);
    out.put("script params blocks", static_cast<int>(run.script.params().size()));
    for (std::size_t block = 0; block < run.script.params().size(); ++block) {
        out.put("script params " + std::to_string(block), paramsText(run.script.params()[block]));
    }
    out.put("random-number position", state.rng.position());
    return TargetEffectResult{.cells = std::move(out.cells)};
}

std::string differences(const TargetEffectResult& port, const TargetEffectResult& guest) {
    std::string out;
    for (const auto& [cell, theirs] : guest.cells) {
        const auto ours = port.cells.find(cell);
        const std::string value = ours == port.cells.end() ? std::string{"(not read)"} : ours->second;
        if (value != theirs) {
            out += cell + ": port " + value + ", cartridge " + theirs + "\n";
        }
    }
    return out;
}

namespace {

// The effects TargetEffectTbl names (battle_main.asm:10014-10112).
const std::array<battle::SpecialEffect, 46> kTargetEffectsNamed{
    battle::SpecialEffect::of(WeaponSpecialEffect::INSTANT_KILL),
    battle::SpecialEffect::of(WeaponSpecialEffect::MAN_EATER),
    battle::SpecialEffect::of(WeaponSpecialEffect::SNIPER_HAWKEYE),
    battle::SpecialEffect::of(WeaponSpecialEffect::SCIMITAR_ZANTETSUKEN),
    battle::SpecialEffect::of(AttackSpecialEffect::SCAN),
    battle::SpecialEffect::of(AttackSpecialEffect::METAMORPH),
    battle::SpecialEffect::of(AttackSpecialEffect::PALIDOR),
    battle::SpecialEffect::of(AttackSpecialEffect::TAPIR),
    battle::SpecialEffect::of(AttackSpecialEffect::EXPLODER),
    battle::SpecialEffect::of(AttackSpecialEffect::REFLECT_LORE),
    battle::SpecialEffect::of(AttackSpecialEffect::DISCHORD),
    battle::SpecialEffect::of(AttackSpecialEffect::PEP_UP),
    battle::SpecialEffect::of(AttackSpecialEffect::RIPPLER),
    battle::SpecialEffect::of(AttackSpecialEffect::STONE),
    battle::SpecialEffect::of(AttackSpecialEffect::DISABLE_COUNTERATTACK),
    battle::SpecialEffect::of(AttackSpecialEffect::MISSES_FLOATING_TARGETS),
    battle::SpecialEffect::of(AttackSpecialEffect::WALLCHANGE),
    battle::SpecialEffect::of(AttackSpecialEffect::ESCAPE),
    battle::SpecialEffect::of(AttackSpecialEffect::MIND_BLAST),
    battle::SpecialEffect::of(AttackSpecialEffect::R_POLARITY),
    battle::SpecialEffect::of(AttackSpecialEffect::LOVE_TOKEN),
    battle::SpecialEffect::of(AttackSpecialEffect::SEIZE),
    battle::SpecialEffect::of(AttackSpecialEffect::TARGETTING),
    battle::SpecialEffect::of(AttackSpecialEffect::SUPLEX),
    battle::SpecialEffect::of(AttackSpecialEffect::BABABREATH),
    battle::SpecialEffect::of(AttackSpecialEffect::CHARM),
    battle::SpecialEffect::of(AttackSpecialEffect::DOOM),
    battle::SpecialEffect::of(AttackSpecialEffect::OVERCAST),
    battle::SpecialEffect::of(AttackSpecialEffect::SNEEZE),
    battle::SpecialEffect::of(AttackSpecialEffect::ENGULF),
    battle::SpecialEffect::of(AttackSpecialEffect::ZINGER),
    battle::SpecialEffect::of(AttackSpecialEffect::EVIL_TOOT),
    battle::SpecialEffect::of(AttackSpecialEffect::PHANTASM),
    battle::SpecialEffect::of(AttackSpecialEffect::STUNNER),
    battle::SpecialEffect::of(AttackSpecialEffect::FALLEN_ONE),
    battle::SpecialEffect::of(AttackSpecialEffect::DISCARD),
    battle::SpecialEffect::of(ItemUseEffect::SUPER_BALL),
    battle::SpecialEffect::of(ItemUseEffect::SMOKE_BOMB),
    battle::SpecialEffect::of(ItemUseEffect::ELIXIR),
    battle::SpecialEffect::of(battle::CommandEffect::Possess),
    battle::SpecialEffect::of(battle::CommandEffect::Steal),
    battle::SpecialEffect::of(battle::CommandEffect::Control),
    battle::SpecialEffect::of(battle::CommandEffect::Leap),
    battle::SpecialEffect::of(battle::CommandEffect::Sketch),
    battle::SpecialEffect::of(battle::CommandEffect::Debilitator),
    battle::SpecialEffect::of(battle::CommandEffect::AirAnchor),
};

std::optional<battle::BattlerSlot> sometimes(std::mt19937& rng, unsigned n) {
    return oneIn(rng, n) ? std::optional<battle::BattlerSlot>{anySlot(rng)} : std::nullopt;
}

}  // namespace

TargetScenario drawTargetEffectScenario(std::mt19937& rng) {
    TargetScenario drawn;
    drawn.scenario = drawAttackerEffectScenario(rng);
    DamageScenario& s = drawn.scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    if (!oneIn(rng, 4)) {
        attack.effect = kTargetEffectsNamed[rng() % kTargetEffectsNamed.size()];
    }
    // A weapon's effect comes from a party member's hand, and Control fills a party member's menu.
    constexpr std::uint8_t kLastWeaponEffectIndex = 14;
    const bool weapon = attack.effect.index() != 0 && attack.effect.index() <= kLastWeaponEffectIndex;
    if ((weapon || attack.effect == battle::SpecialEffect::of(battle::CommandEffect::Control)) && s.attacker.isMonster()) {
        s.attacker = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    attack.params.attacker = s.attacker;
    attack.params.attackerIsMonster = s.attacker.isMonster();
    drawn.target = oneIn(rng, 8) ? s.attacker : anySlot(rng);
    if (oneIn(rng, 8)) {
        attack.command = BattleCommandId::JUMP;
    }
    attack.scratch.hideWithoutTargets = oneIn(rng, 4);
    attack.scratch.earlierTargets = anyBattlers(rng);
    for (std::optional<battle::BattlerSlot>& pick : attack.scratch.mindBlastPicks) {
        pick = sometimes(rng, 2);
    }

    state.formation = static_cast<FormationId>(rng() % formationAux().size());
    state.heldByZinger = sometimes(rng, 8);
    state.swallowedByZinger = sometimes(rng, 8);
    state.flippedByControl = oneIn(rng, 4) ? anyBattlers(rng).monstersOnly() : battle::BattlerSet{};
    state.engulfed = oneIn(rng, 4) ? anyBattlers(rng).charactersOnly() : battle::BattlerSet{};
    state.takenFromTheParty = oneIn(rng, 4) ? anyBattlers(rng).charactersOnly() : battle::BattlerSet{};
    state.partyLeft = oneIn(rng, 4) ? anyBattlers(rng).charactersOnly() : battle::BattlerSet{};
    state.holdingItem = oneIn(rng, 4) ? anyBattlers(rng).charactersOnly() : battle::BattlerSet{};
    state.monstersFallen = oneIn(rng, 4) ? anyBattlers(rng).monstersOnly() : battle::BattlerSet{};
    state.scratch.countersEnabled = !oneIn(rng, 4);
    const auto monsterCount = static_cast<unsigned>(monsterControls().size());
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battler.stamina = byte(rng);
        battler.instantDeathProtected = oneIn(rng, 4);
        battler.metamorph.packed = byte(rng);
        battler.rareSteal = oneIn(rng, 3) ? ItemId::EMPTY : static_cast<ItemId>(byte(rng) % 0xFF);
        battler.commonSteal = oneIn(rng, 3) ? ItemId::EMPTY : static_cast<ItemId>(byte(rng) % 0xFF);
        if (slot.isMonster()) {
            battler.monster = static_cast<MonsterId>(rng() % monsterCount);
        }
        battler.controlling = sometimes(rng, 8);
        battler.controlEnding = oneIn(rng, 4);
        battler.charming = sometimes(rng, 8);
        battler.charmedBy = sometimes(rng, 8);
        battler.seizedBy = sometimes(rng, 8);
        battler.bodyguard = sometimes(rng, 8);
        battler.guarding = sometimes(rng, 8);
        battler.targetingTarget = sometimes(rng, 8);
        battler.aiVariable = byte(rng);
        if (oneIn(rng, 4)) {
            battler.itemInHand = static_cast<ItemId>(byte(rng) % 0xFF);
        }
        battler.relicEffects3.bits = sparse(rng);
        battler.triggers.usesControlMenu = oneIn(rng, 4);
        battler.triggers.overcast = oneIn(rng, 4);
        battler.triggers.phantasm = oneIn(rng, 4);
        battler.airAnchored = oneIn(rng, 4);
        battler.emptyGaugeAfterAction = oneIn(rng, 4);
        if (oneIn(rng, 4)) {
            battler.actions.push_back(battle::CommandAction{.command = static_cast<BattleCommandId>(byte(rng) % 0x20),
                                                            .attack = static_cast<AttackId>(byte(rng)),
                                                            .targets = anyBattlers(rng),
                                                            .mpCost = byte(rng)});
        }
        if (oneIn(rng, 2)) {
            battler.lastHit.command =
                battle::Hit<BattleCommandId>{.by = anySlot(rng), .with = static_cast<BattleCommandId>(byte(rng) % 0x20)};
        }
    }
    // The target as the attack frame leaves it for the effect: out of the targets hit, marked to
    // answer the attacker, and its last command hit recorded.
    battle::Battler& target = state.at(drawn.target);
    target.lastAttacker = s.attacker;
    target.retaliationPending = true;
    target.lastHit.command = battle::Hit<BattleCommandId>{.by = s.attacker, .with = attack.queuedCommand};
    attack.params.targetsHit.remove(drawn.target);
    return drawn;
}

// --- replaying a special effect --------------------------------------------------------------------

namespace {

std::optional<battle::BattlerSlot> slotOrNone(std::uint8_t value) {
    return value == kNoOne ? std::nullopt : slotTimesTwo(value);
}

// A link cell: the battler it names, and whether its top bit marks the link as ending.
std::optional<battle::BattlerSlot> linkOf(std::uint8_t value) {
    return value == kNoOne ? std::nullopt : slotTimesTwo(static_cast<std::uint8_t>(value & ~kEndingBit));
}

bool linkEnding(std::uint8_t value) {
    return value != kNoOne && (value & kEndingBit) != 0;
}

// One of the battle's interrupting actions as its list node holds it.
battle::Action immediateActionOf(std::uint8_t command, std::uint8_t attack, std::uint16_t targets) {
    if (command == kScanCommand) {
        return battle::ScanAction{.target = battle::BattlerSlot::fromIndex((attack / 2) % battle::kBattlerSlots)};
    }
    if (command == kVisibilityCommand) {
        return battle::MonsterVisibilityAction{
            .mode = static_cast<battle::MonsterVisibilityMode>(targets & 0xFF),
            .monsters = battle::BattlerSet{BattleSlotMask{}, BattleSlotMask(static_cast<std::uint8_t>(targets >> 8))},
            .type = static_cast<MonsterEntranceType>(attack)};
    }
    return battle::CommandAction{.command = static_cast<BattleCommandId>(command),
                                 .attack = static_cast<AttackId>(attack),
                                 .targets = setOf(targets)};
}

}  // namespace

DamageScenario DamageOracle::readEffectScenario() {
    DamageScenario s = readStatusScenario();
    battle::BattleState& state = s.state;
    battle::AttackInFlight& attack = s.attack;

    // The aim and the hit check's cells.
    const std::uint8_t aiming = readByte(kAimingRules);
    attack.rules.noCharacterTargets = (aiming & 0x80) != 0;
    attack.rules.randomTarget = (aiming & 0x40) != 0;
    attack.rules.fallBackToEarlierTargets = (aiming & 0x20) != 0;
    attack.rules.reflected = (aiming & 0x10) != 0;
    attack.rules.mayTargetTheFallen = (aiming & 0x08) != 0;
    attack.rules.keepTargetsThatAreGone = (aiming & 0x04) != 0;
    attack.rules.opposingSideOnly = (aiming & 0x02) != 0;
    attack.rules.excludeAttacker = (aiming & 0x01) != 0;
    attack.targets = setOf(readWord(kTargets));
    const std::uint8_t aimFlags = readByte(kAimFlags);
    attack.scratch.aimExactly = (aimFlags & kAimExactlyBit) != 0;
    state.endTurnAfterAction = (aimFlags & kEndTurnBit) != 0;
    attack.scratch.earlierTargets = setOf(readWord(kEarlierTargets));
    const battle::BattlerSet enemies = setOf(readWord(kEnemyCharacters));
    state.battleType = static_cast<battle::BattleType>(readByte(kBattleType));
    state.pincerSides[0] = BattleSlotMask(readByte(kPincerFirstSide));
    state.pincerSides[1] = BattleSlotMask(readByte(kPincerSecondSide));
    state.targetableOrPresent = setOf(readWord(kReachFirst));
    state.aiUntargetable = setOf(static_cast<std::uint16_t>(~readWord(kReachSecond)));
    state.scratch.countersEnabled = (readByte(kCountersEnabled) & kCountersOn) != 0;
    attack.scratch.awayFromTheFieldUnreachable = readByte(kReachKeepsAway) == kAwayUnreachable;
    state.awayFromTheField = setOf(readWord(kAwayFromTheField));
    attack.scratch.targetCount = readByte(kTargetCount);
    attack.scratch.quietMisses = readByte(kQuietMisses) == 0;
    attack.scratch.hitInTheBack = setOf(readWord(kHitInTheBack));
    attack.queuedCommand = static_cast<BattleCommandId>(readByte(kQueuedCommand));
    const std::uint8_t lastSpell = readByte(kLastSpell);
    attack.scratch.lastSpell = lastSpell == kNoOne ? std::nullopt : std::optional<AttackId>(static_cast<AttackId>(lastSpell));
    const std::uint8_t lastItem = readByte(kLastItem);
    attack.scratch.lastItem = lastItem == kNoOne ? std::nullopt : std::optional<ItemId>(static_cast<ItemId>(lastItem));
    const std::uint8_t hits = readByte(kExtraHits);
    state.extraHits = hits == kNoHitsToCome ? std::nullopt : std::optional<int>(hits + 1);

    // The effects' own cells.
    attack.params = paramsFromBytes(machine_.read(retropp::MemoryRegion{.at = kParamsFlags, .size = 8}));
    attack.castingSwirlSuppressed = (readByte(kSwirlCell) & kSwirlSuppressedBit) != 0;
    const std::uint8_t running = readByte(kRunningCell);
    state.counterattackRunning = (running & kCounterattackBit) != 0;
    state.cannotRun = (running & kCannotRunBit) != 0;
    attack.automaticCritical = (readByte(kAttackFlags) & kAutomaticCriticalClearBit) == 0;
    const std::uint8_t spell = readByte(kFollowUpSpell);
    attack.scratch.followUpSpell = spell == kNoOne ? std::nullopt : std::optional<AttackId>(static_cast<AttackId>(spell));
    const std::uint8_t message = readByte(kMessage);
    attack.scratch.message = message == kNoOne ? std::nullopt : std::optional<std::uint8_t>(message);
    attack.scratch.attacksLeft = readByte(kAttacksLeft);
    attack.scratch.dragonHorn = readByte(kDragonHorn) != 0;
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        const battle::BattlerSet pick = setOf(readWord(kMindBlastPicks + static_cast<std::uint32_t>(position * 2)));
        attack.scratch.mindBlastPicks[position] = std::nullopt;
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            if (pick.has(battle::BattlerSlot::fromIndex(index))) {
                attack.scratch.mindBlastPicks[position] = battle::BattlerSlot::fromIndex(index);
            }
        }
    }
    const std::uint8_t quickTurns = readByte(kQuickTurns);
    state.quickTurnsLeft = quickTurns == kNoQuick ? std::nullopt : std::optional<std::uint8_t>(quickTurns);
    state.quickTarget = slotOrNone(readByte(kQuickTarget));
    const std::uint8_t endOfBattle = readByte(kEndOfBattleEvent);
    state.endOfBattleEvent = endOfBattle == 0 ? std::nullopt
                                              : std::optional<battle::EndOfBattleEvent>(
                                                    static_cast<battle::EndOfBattleEvent>(endOfBattle / 2));
    state.leavingAfterAction = setOf(readWord(kLeaving));
    state.allyCount = readByte(kAllyCount);
    const std::vector<std::uint8_t> gil = machine_.read(retropp::MemoryRegion{.at = kGil, .size = 3});
    state.party.gil = static_cast<std::uint32_t>(gil[0] | gil[1] << 8 | gil[2] << 16);
    const std::vector<std::uint8_t> steps = machine_.read(retropp::MemoryRegion{.at = kSteps, .size = 3});
    state.party.steps = static_cast<std::uint32_t>(steps[0] | steps[1] << 8 | steps[2] << 16);
    state.playClockFrames = readByte(kPlayClockFrames);

    // The target effects' cells.
    const std::uint8_t formationFlags = readByte(kFormationFlags);
    const auto formation = std::find_if(formationAux().begin(), formationAux().end(),
                                        [&](const FormationAuxEntry& entry) { return entry.record.flags == formationFlags; });
    if (formation == formationAux().end()) {
        throw std::runtime_error("no formation has the flags " + std::to_string(formationFlags));
    }
    state.formation = formation->id;
    state.holdingItem = battle::BattlerSet{BattleSlotMask(readByte(kHoldingItem)), BattleSlotMask{}};
    state.engulfed = battle::BattlerSet{BattleSlotMask(readByte(kEngulfed)), BattleSlotMask{}};
    state.takenFromTheParty = battle::BattlerSet{BattleSlotMask(readByte(kTakenFromTheParty)), BattleSlotMask{}};
    state.partyLeft = battle::BattlerSet{BattleSlotMask(readByte(kPartyLeft)), BattleSlotMask{}};
    state.flippedByControl = setOf(readWord(kFlippedSet));
    state.heldByZinger = slotOrNone(readByte(kZingerMonster));
    state.swallowedByZinger = slotOrNone(readByte(kZingerSwallowed));
    if (const std::optional<battle::BattlerSlot> sketched = slotOrNone(readByte(kSketched))) {
        attack.scratch.sketch = battle::SketchDrawn{.monster = *sketched};
    }
    const std::uint8_t messageItem = readByte(kMessageItem);
    attack.scratch.messageItem =
        messageItem == kNoOne ? std::nullopt : std::optional<ItemId>(static_cast<ItemId>(messageItem));
    const std::vector<std::uint8_t> amount = machine_.read(retropp::MemoryRegion{.at = kMessageAmount, .size = 3});
    const auto messageAmount = static_cast<std::uint32_t>(amount[0] | amount[1] << 8 | amount[2] << 16);
    attack.scratch.messageAmount = messageAmount == 0 ? std::nullopt : std::optional<std::uint32_t>(messageAmount);
    attack.scratch.hideWithoutTargets = (readByte(kShownWithoutTargets) & kShownBit) == 0;

    // A list of actions from its head, each node's next its own at the list's end.
    const auto list = [&](std::uint8_t head, bool immediate) {
        std::deque<battle::Action> actions;
        std::uint8_t node = head;
        for (std::uint32_t step = 0; node != kFreeSlot && step < kActionSlots; ++step) {
            const auto word = static_cast<std::uint32_t>(node) * 2;
            const std::uint16_t command = readWord(kActionCommand + word);
            const auto commandIndex = static_cast<std::uint8_t>(command & 0xFF);
            const auto attackIndex = static_cast<std::uint8_t>(command >> 8);
            const std::uint16_t targets = readWord(kActionTargets + word);
            if (immediate) {
                actions.push_back(immediateActionOf(commandIndex, attackIndex, targets));
            } else {
                actions.push_back(battle::CommandAction{.command = static_cast<BattleCommandId>(commandIndex),
                                                        .attack = static_cast<AttackId>(attackIndex),
                                                        .targets = setOf(targets),
                                                        .mpCost = readByte(kActionMp + word)});
            }
            const std::uint8_t following = readByte(kActionNext + node);
            if (following == node || (following & kEndingBit) != 0) {
                break;
            }
            node = following;
        }
        return actions;
    };
    state.immediateActions = list(readByte(kFirstImmediateAction), true);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battler.actsAsEnemy = enemies.has(slot);
        battler.inBattleMenu = (readByte(kOnTheField + at) & kInBattleMenuBit) != 0;
        battler.charmedBy = slotOrNone(readByte(kCharmedBy + at));
        battler.stamina = readByte(kStamina + at);
        battler.evadeFactor = readByte(kEvadeAndMagicBlock + at);
        battler.magicBlockFactor = readByte(kEvadeAndMagicBlock + at + 1);
        battler.physicalBlockGraphics.bits = readByte(kBlockGraphics + at);
        battler.magicBlockGraphics.bits = readByte(kBlockGraphics + at + 1);
        const auto hit = [&](std::uint32_t byCell, std::uint32_t withCell, auto with) {
            using With = decltype(with);
            const std::uint8_t by = readByte(byCell + at);
            const std::optional<battle::BattlerSlot> who = slotOrNone(by);
            return who ? std::optional<battle::Hit<With>>(battle::Hit<With>{.by = *who, .with = With{readByte(withCell + at)}})
                       : std::nullopt;
        };
        battler.lastHit.command = hit(kLastHitCommandBy, kLastHitCommand, BattleCommandId{});
        battler.lastHit.spell = hit(kLastHitSpellBy, kLastHitSpell, AttackId{});
        battler.lastHit.item = hit(kLastHitItemBy, kLastHitItem, ItemId{});
        if (const std::optional<battle::BattlerSlot> who = slotOrNone(readByte(kLastHitElementBy + at))) {
            battler.lastHit.element = battle::Hit<ElementSet>{.by = *who, .with = ElementSet{readByte(kLastHitElement + at)}};
        } else {
            battler.lastHit.element.reset();
        }
        battler.seizing = slotOrNone(readByte(kSeizing + at));
        battler.monsterFlags.bits = readByte(kMonsterBattleFlags + at);
        battler.gold = readWord(kGold + at);

        const std::uint8_t controlling = readByte(kControlling + at);
        const std::uint8_t controlledBy = readByte(kController + at);
        battler.controlling = linkOf(controlling);
        battler.controlledBy = linkOf(controlledBy);
        battler.controlEnding = linkEnding(controlling) || linkEnding(controlledBy);
        battler.seizedBy = slotOrNone(readByte(kSeizedBy + at));
        battler.charming = slotOrNone(readByte(kCharming + at));
        battler.bodyguard = slotOrNone(readByte(kBodyguard + at));
        battler.guarding = slotOrNone(readByte(kGuarding + at));
        const std::uint8_t item = readByte(kItemInHand + at);
        battler.itemInHand = item == kNoOne ? std::nullopt : std::optional<ItemId>(static_cast<ItemId>(item));
        battler.targetingTarget = slotOrNone(readByte(kTargetingTarget + at));
        battler.aiVariable = readByte(kAiVariable + at);
        battler.rareSteal = static_cast<ItemId>(readByte(kRareSteal + at));
        battler.commonSteal = static_cast<ItemId>(readByte(kCommonSteal + at));
        battler.metamorph.packed = readByte(kMetamorph + at);
        if (slot.isMonster()) {
            battler.monster = static_cast<MonsterId>(readWord(kMonsterIndex + at));
        }
        battler.airAnchored = (readByte(kAnchorCell + at) & kNotAnchoredBit) == 0;
        const std::uint8_t triggers = readByte(kMoreTriggers + at);
        battler.triggers.usesControlMenu = (triggers & kControlMenuBit) != 0;
        battler.actions = list(readByte(kFirstAction + at), false);
        if (slot.isCharacter()) {
            const auto position = static_cast<std::size_t>(slot.position());
            const auto place = static_cast<std::uint32_t>(position) * kHandItemSize;
            battler.weaponHand.battlePower = readByte(kHandPower + at);
            battler.shieldHand.battlePower = readByte(kHandPower + at + 1);
            state.inventory.weaponHands[position].item = static_cast<ItemId>(readByte(kWeaponHands + place));
            state.inventory.weaponHands[position].quantity = readByte(kWeaponHands + place + kHandQuantity);
            state.inventory.shieldHands[position].item = static_cast<ItemId>(readByte(kShieldHands + place));
            state.inventory.shieldHands[position].quantity = readByte(kShieldHands + place + kHandQuantity);
            const auto menu = kControlMenus + static_cast<std::uint32_t>(position) * kControlMenuSize;
            for (std::size_t row = 0; row < battler.controlMenu.size(); ++row) {
                const auto first = menu + static_cast<std::uint32_t>(row) * kControlRowSize;
                battler.controlMenu[row] = battle::ControlMenuEntry{.attack = static_cast<AttackId>(readByte(first)),
                                                                    .castable = (readByte(first + 1) & kUncastableBit) == 0,
                                                                    .targeting = Targeting{readByte(first + 2)}};
            }
        }
    }
    return s;
}

std::string DamageOracle::replayEffectCall(const TracedCall& call, const TracedRoutine& routine, bool bounce) {
    const bool onTarget = routine.entry == kDoTargetEffect;
    // The battle as the oracle writes a default scenario: for the effect on a target, the attack's own
    // animation first in the script's queue, as the frame leaves it.
    const DamageScenario plain;
    writeScenario(plain);
    writeTargetCells(plain);
    writeApplyCells(plain);
    writeStatusCells(plain);
    writeAimCells(plain);
    writeEffectCells(plain);
    if (onTarget) {
        writeTargetEffectCells(plain);
    }
    const std::uint8_t ourPlace = readByte(kAnimationPlace);
    const std::uint8_t ourEnd = readByte(kQueueEnd);
    const std::uint8_t ourNext = readByte(kNextParamsBlock);

    // The cartridge's queue and params blocks stood elsewhere: what the call read and wrote of them is
    // moved to the default's places — the frame's animation to its place, what follows the queue's end
    // and the next params block to theirs.
    const auto recorded = [&](std::uint32_t cell) -> std::optional<std::uint8_t> {
        const auto found = call.inputs.find(cell);
        return found != call.inputs.end() ? std::optional<std::uint8_t>(found->second) : std::nullopt;
    };
    const std::optional<std::uint8_t> theirPlace = recorded(kAnimationPlace);
    const std::optional<std::uint8_t> theirEnd = recorded(kQueueEnd);
    const std::optional<std::uint8_t> theirNext = recorded(kNextParamsBlock);
    const auto moved = [&](std::uint32_t cell) -> std::optional<std::uint32_t> {
        if (cell >= kQueue && cell < kQueue + kQueueSpan) {
            const std::uint32_t offset = cell - kQueue;
            if (theirEnd && offset >= *theirEnd) {
                return kQueue + ourEnd + (offset - *theirEnd);
            }
            if (onTarget && theirPlace && offset >= *theirPlace && offset < *theirPlace + kQueueCommandSize) {
                return kQueue + ourPlace + (offset - *theirPlace);
            }
            throw std::runtime_error("the call touched the script's queue at " + busAddress(cell) +
                                     " with no place or end read to place it by");
        }
        if (cell >= kParamsBlocks && cell < kParamsBlocks + kBlocksSize) {
            const std::uint32_t offset = cell - kParamsBlocks;
            if (theirNext && offset >= *theirNext) {
                return kParamsBlocks + ourNext + (offset - *theirNext);
            }
            throw std::runtime_error("the call touched a params block at " + busAddress(cell) +
                                     " with no next block read to place it by");
        }
        if (cell == kAnimationPlace || cell == kQueueEnd || cell == kNextParamsBlock) {
            return std::nullopt;
        }
        return cell;
    };
    for (const auto& [cell, value] : call.inputs) {
        if (const std::optional<std::uint32_t> to = moved(cell)) {
            writeByte(*to, value);
        }
    }
    // The attack's context at the call's entry.
    writeBytes(retropp::MemoryRegion{.at = kParamsCells, .size = kParamsSize}, call.params);
    const battle::BattlerSlot attacker =
        bounce ? bounceEffectAttacker(call) : slotNamed(call.params[kBlockAttacker], "the params block's attacker");
    std::optional<battle::BattlerSlot> target;
    if (onTarget) {
        if (!call.target) {
            throw std::runtime_error("DoTargetEffect's target is not in the trace: no attack's statuses on one target "
                                     "were worked out just before it");
        }
        target = call.target;
        // The frame records who last hit the target, and with what command, before the effect; a pair the
        // call did not read stands as the frame left it.
        const auto at = static_cast<std::uint32_t>(target->index() * 2);
        if (!call.inputs.contains(kLastHitCommandBy + at)) {
            writeByte(kLastHitCommandBy + at, static_cast<std::uint8_t>(slotIndex(attacker)));
        }
        if (!call.inputs.contains(kLastHitCommand + at)) {
            writeByte(kLastHitCommand + at, readByte(kQueuedCommand));
        }
    }
    // A party member the reach cell shows held in Seize, whose holder's cell the call did not read, is
    // given a holder: the call read only the reach cell, so who holds it plays no part in its answer.
    if (const std::optional<std::uint8_t> reach = recorded(kReachCharacters)) {
        for (int position = 0; position < battle::kPartySlots; ++position) {
            const auto at = static_cast<std::uint32_t>(battle::BattlerSlot::character(position).index() * 2);
            if ((*reach & (1u << position)) == 0 && !call.inputs.contains(kSeizedBy + at) &&
                readByte(kSeizedBy + at) == kNoOne) {
                writeByte(kSeizedBy + at, static_cast<std::uint8_t>(slotIndex(battle::BattlerSlot::monster(0))));
            }
        }
    }

    DamageScenario s = readEffectScenario();
    s.attacker = attacker;

    // The cartridge's answer: the same cells with the call's writes, moved the same way.
    const auto guestCells = [&] {
        for (const auto& [cell, value] : call.writes) {
            if (cell == kQueueEnd && theirEnd) {
                writeByte(kQueueEnd, static_cast<std::uint8_t>(ourEnd + (value - *theirEnd)));
            } else if (cell == kAnimationPlace && theirEnd) {
                // A command added: its place is counted from the queue's end.
                writeByte(kAnimationPlace, static_cast<std::uint8_t>(ourEnd + (value - *theirEnd)));
            } else if (cell == kAnimationPlace && theirPlace) {
                writeByte(kAnimationPlace, static_cast<std::uint8_t>(ourPlace + (value - *theirPlace)));
            } else if (cell == kNextParamsBlock && theirNext) {
                writeByte(kNextParamsBlock, static_cast<std::uint8_t>(ourNext + (value - *theirNext)));
            } else if (cell == kAnimationPlace || cell == kQueueEnd || cell == kNextParamsBlock) {
                throw std::runtime_error("the call moved the script's place at " + busAddress(cell) +
                                         " without reading where it stood");
            } else if (const std::optional<std::uint32_t> to = moved(cell)) {
                writeByte(*to, value);
            }
        }
    };
    if (!onTarget) {
        const AttackerEffectResult port = attackerEffectResult(runAttackerEffect(s));
        guestCells();
        return differences(port, readAttackerEffectResult());
    }
    const TargetEffectResult port = targetEffectResult(runTargetEffect(s, *target));
    guestCells();
    return differences(port, readTargetEffectResult());
}


// --- the cover and the attack's animation set up -------------------------------------------------

namespace {

// The ally a cover stepped in front of, by its slot index, and the byte after it. The cover shifts
// the two as one word, so the byte after is written as every attack starts it (ClearGfxParams): 0.
constexpr std::uint32_t kCoveredAlly = kWorkRam + 0x00A8;
constexpr std::uint32_t kAfterCoveredAlly = kWorkRam + 0x00A9;

std::string cellDifferences(const std::map<std::string, std::string>& port,
                            const std::map<std::string, std::string>& guest) {
    std::string out;
    for (const auto& [cell, theirs] : guest) {
        const auto ours = port.find(cell);
        const std::string value = ours == port.end() ? std::string{"(not read)"} : ours->second;
        if (value != theirs) {
            out += cell + ": port " + value + ", cartridge " + theirs + "\n";
        }
    }
    return out;
}

}  // namespace

ParamsInitResult DamageOracle::initParams(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    call([&] { initGfxParams_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    EffectCells out;
    const std::vector<std::uint8_t> params = machine_.read(retropp::MemoryRegion{.at = kParamsFlags, .size = 8});
    out.put("params", paramsText(paramsFromBytes(params)));
    const std::uint8_t rules = readByte(kAimingRules);
    out.put("random target", (rules & kRandomTargetBit) != 0);
    out.put("fall back to earlier targets", (rules & kFallBackToEarlierTargetsBit) != 0);
    out.put("other side only", (rules & kOpposingSideBit) != 0);
    out.put("swirl suppressed", (readByte(kSwirlCell) & kSwirlSuppressedBit) != 0);
    return ParamsInitResult{.cells = std::move(out.cells)};
}

ParamsInitResult portInitParams(DamageScenario scenario) {
    battle::AttackInFlight& attack = scenario.attack;
    battle::initParams(attack, scenario.attacker);
    EffectCells out;
    out.put("params", paramsText(attack.params));
    out.put("random target", attack.rules.randomTarget);
    out.put("fall back to earlier targets", attack.rules.fallBackToEarlierTargets);
    out.put("other side only", attack.rules.opposingSideOnly);
    out.put("swirl suppressed", attack.castingSwirlSuppressed);
    return ParamsInitResult{.cells = std::move(out.cells)};
}

std::string differences(const ParamsInitResult& port, const ParamsInitResult& guest) {
    return cellDifferences(port.cells, guest.cells);
}

CoverResult DamageOracle::coverEffect(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    writeTargetEffectCells(scenario);
    // The battlers stepping in are marked in the same word as the battlers a spell bounced off.
    const battle::AttackAnimationParams& params = scenario.attack.params;
    writeWord(kParamsReflectedOff, maskOf(params.reflectedOff | params.steppingIn));
    writeByte(kCoveredAlly,
              params.coveredAlly ? static_cast<std::uint8_t>(params.coveredAlly->index()) : kNoOne);
    writeByte(kAfterCoveredAlly, 0);
    call([&] {
        coverEffect_(slotIndex(scenario.attacker), kWideAccumulatorStatus, kBattleDataBank, kBattleDirectPage);
    });
    EffectCells out;
    out.put("targets", std::to_string(readWord(kTargets)));
    const std::uint8_t covered = readByte(kCoveredAlly);
    out.put("covered ally", covered == kNoOne ? std::string{"none"} : std::to_string(covered));
    out.put("stepping in", std::to_string(readWord(kParamsReflectedOff)));
    return CoverResult{.cells = std::move(out.cells)};
}

CoverResult portCoverTarget(DamageScenario scenario) {
    const battle::AttackInFlight& attack = scenario.attack;
    battle::coverTarget(scenario.state, scenario.attack, scenario.attacker);
    EffectCells out;
    out.put("targets", std::to_string(maskOf(attack.targets)));
    out.put("covered ally", slotText(attack.params.coveredAlly));
    out.put("stepping in", std::to_string(maskOf(attack.params.reflectedOff | attack.params.steppingIn)));
    return CoverResult{.cells = std::move(out.cells)};
}

std::string differences(const CoverResult& port, const CoverResult& guest) {
    return cellDifferences(port.cells, guest.cells);
}

DamageScenario drawCoverScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetEffectScenario(rng).scenario;
    battle::BattleState& state = s.state;
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        battle::Battler& battler = state.at(slot);
        if (oneIn(rng, 2)) {
            battler.status = {};
        }
        if (oneIn(rng, 2)) {
            battler.status.set(StatusId::NEAR_FATAL);
        }
        if (oneIn(rng, 2)) {
            battler.relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
        }
        // A Control link or a Seize hold on this battler, its other end on the battler holding it.
        const battle::BattlerSlot other = anySlot(rng);
        if (other != slot && oneIn(rng, 6)) {
            const bool ending = oneIn(rng, 2);
            battler.controlledBy = other;
            battler.controlEnding = ending;
            state.at(other).controlling = slot;
            state.at(other).controlEnding = ending;
        } else if (other != slot && oneIn(rng, 6)) {
            battler.seizedBy = other;
            state.at(other).seizing = slot;
        }
    }
    battle::BattlerSet targets;
    if (!oneIn(rng, 4)) {
        targets = anyBattlers(rng);
        if (targets.empty()) {
            targets.add(anySlot(rng));
        }
    }
    s.attack.targets = targets;
    s.attack.params.reflectedOff = {};
    s.attack.params.steppingIn = {};
    s.attack.params.coveredAlly.reset();
    return s;
}

// --- a whole attack --------------------------------------------------------------------------------

namespace {

// The frame's own cells (ExecAttack, battle/battle_main.asm:8015-8139, and the routines it calls):
// the targeting byte and the command's extra steps in direct page; the targets of the attack being
// made, the MP it costs, the name it is shown under, the command whose hands each attack loads, the
// attack re-aimed at random (negative: it is not), the battler Interceptor guards, and the attack as
// it was chosen; the battlers facing right (party members in the low byte, monsters in the high);
// Strago's slot times two; the lores the party knows and the lores seen this battle, three bytes each.
constexpr std::uint32_t kTargetingByte = kWorkRam + 0x00BB;
constexpr std::uint32_t kExtraMultiplier = kWorkRam + 0x00BD;
constexpr std::uint32_t kCurrentTargets = kWorkRam + 0x3A30;
constexpr std::uint32_t kMpCost = kWorkRam + 0x3A4C;
constexpr std::uint32_t kNameKind = kWorkRam + 0x3412;
constexpr std::uint32_t kFightCommand = kWorkRam + 0x3413;
constexpr std::uint32_t kRandomizeTargets = kWorkRam + 0x3415;
constexpr std::uint32_t kInterceptorGuard = kWorkRam + 0x3416;
constexpr std::uint32_t kQueuedAttack = kWorkRam + 0x3A7D;
constexpr std::uint32_t kFacingRight = kWorkRam + 0x2F50;
constexpr std::uint32_t kStragoSlot = kWorkRam + 0x3007;
constexpr std::uint32_t kLoresKnown = kWorkRam + 0x1D29;
constexpr std::uint32_t kLoresLearned = kWorkRam + 0x3A84;
constexpr std::uint32_t kLoreBytes = 3;
// The run difficulty, the byte after the monsters fallen (kMonstersFallen): read with it as a word.
constexpr std::uint32_t kRunDifficulty = kWorkRam + 0x3A3B;
// Per battler, at twice the battler's index: its special attack and that attack's animation, the
// monster its special attack is named for (a word), its vigor and its magic power.
constexpr std::uint32_t kSpecialAttack = kWorkRam + 0x322D;
constexpr std::uint32_t kSpecialAttackAnimation = kWorkRam + 0x3C81;
constexpr std::uint32_t kNamedMonster = kWorkRam + 0x33A8;
constexpr std::uint32_t kVigor = kWorkRam + 0x3B2C;
constexpr std::uint32_t kMagicPower = kWorkRam + 0x3B41;
// Per battler, at twice its index: its weapon hand, then its shield hand — the item (a monster's
// attack graphic), the hit rate, the element, the weapon's flags, the spell it casts and its block
// and weapon-effect byte. A Fight loads the hand it swings with from these (_c2299f).
constexpr std::uint32_t kHandItems = kWorkRam + 0x3CA8;
constexpr std::uint32_t kHandHitRate = kWorkRam + 0x3B7C;
constexpr std::uint32_t kHandElement = kWorkRam + 0x3B90;
constexpr std::uint32_t kHandWeaponFlags = kWorkRam + 0x3BA4;
constexpr std::uint32_t kHandSpellCast = kWorkRam + 0x3D34;
constexpr std::uint32_t kHandBlockEffect = kWorkRam + 0x3CBC;
// The battle's lore list, one entry per lore: a known lore's number plus kLoreListFirstEntry, and
// 0xFF for a lore not known.
constexpr std::uint32_t kLoreList = kWorkRam + 0x310F;
constexpr std::uint8_t kLoreListFirstEntry = 0x37;

// The triggers byte beside Retort (kTriggers, bit 0): a monster's own Runic, a party member's.
constexpr std::uint8_t kMonsterRunicBit = 0x02;
constexpr std::uint8_t kRunicBit = 0x04;

// The first attack-flags byte ($B2) as the frame reads it, beside a drain capped (bit 7) and the
// cannot-be-critical bit (bit 1): clear while Runic's animation is still to show, set for the first
// pass, clear when the weapon's spell goes after the hit's targets, set while no name has been shown.
constexpr std::uint8_t kRunicShownBit = 0x40;
constexpr std::uint8_t kFirstPassBit = 0x20;
constexpr std::uint8_t kOtherTargetsBit = 0x10;
constexpr std::uint8_t kNameNotShownBit = 0x01;
// The second ($B3), beside Vanish applying (bit 7), not two-handed (bit 6), the ignore-defense bit
// (bit 4) and the automatic critical (bit 1, clear): the attacker's row ignored, an attack with no
// targets aimed again, and an attack that hits someone (clear: a Blitz whose buttons were wrong).
constexpr std::uint8_t kRowIgnoredBit = 0x20;
constexpr std::uint8_t kEmptyTargetsReaimBit = 0x04;
constexpr std::uint8_t kHitsSomeoneBit = 0x01;

// The name kinds by the number ShowAttackNameTbl (battle_main.asm:8778-8787) gives them; the entry at
// 3 is a bare return that no command's start writes.
std::uint8_t nameKindByte(const std::optional<battle::AttackNameKind>& kind) {
    if (!kind) {
        return kNoOne;
    }
    switch (*kind) {
    case battle::AttackNameKind::Attack:
        return 0;
    case battle::AttackNameKind::Item:
        return 1;
    case battle::AttackNameKind::Esper:
        return 2;
    case battle::AttackNameKind::Bushido:
        return 4;
    case battle::AttackNameKind::Command:
        return 5;
    case battle::AttackNameKind::MonsterSpecialAttack:
        return 6;
    case battle::AttackNameKind::JokerDoom:
        return 7;
    case battle::AttackNameKind::Blitz:
        return 8;
    }
    return kNoOne;
}

// The script's commands as the frame writes them into the queue (_c2629e, battle_main.asm:16132-16149):
// four bytes each, the command first.
constexpr std::uint8_t kNameCommand = 0x01;
constexpr std::uint8_t kMonsterNameCommand = 0x11;
constexpr std::uint8_t kPoseResetCommand = 0x0E;
// An esper's name is numbered from the first esper's attack.
constexpr std::uint8_t kFirstEsperAttack = static_cast<std::uint8_t>(EsperId::RAMUH);

std::optional<battle::BattlerSlot> stragoInParty(const battle::BattleState& state) {
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::BattlerSlot slot = battle::BattlerSlot::character(position);
        if (state.at(slot).actor == CharacterPropId::STRAGO) {
            return slot;
        }
    }
    return std::nullopt;
}

template <std::size_t N>
std::array<std::uint8_t, kLoreBytes> loreBytes(const std::bitset<N>& lores) {
    std::array<std::uint8_t, kLoreBytes> bytes{};
    for (std::size_t lore = 0; lore < N && lore < kLoreBytes * 8; ++lore) {
        if (lores.test(lore)) {
            bytes[lore / 8] = static_cast<std::uint8_t>(bytes[lore / 8] | 1u << (lore % 8));
        }
    }
    return bytes;
}

// A params block as the frame's animations read it: paramsText()'s fields with the battlers a spell
// bounced off and the battlers stepping in as the one set they share, then the critical flash, a
// monster's special attack, the covered ally's byte (every attack's params start cleared, so 0 with
// none covered) and each party slot's block.
std::string frameParamsText(battle::AttackAnimationParams params) {
    params.reflectedOff = params.reflectedOff | params.steppingIn;
    const int covered = params.coveredAlly ? params.coveredAlly->index() : 0;
    std::string text = paramsText(params) + " critical " + std::to_string(params.criticalFlash) + " monster attack " +
                       std::to_string(params.monsterAttack) + " covered " + std::to_string(covered) + " blocks";
    for (const battle::BlockShown block : params.blocks) {
        text += " " + std::to_string(static_cast<int>(block));
    }
    return text;
}

constexpr std::uint8_t kCriticalFlashBit = 0x20;
constexpr std::uint8_t kMonsterAttackBit = 0x01;
constexpr std::size_t kCoveredAllyByte = 8;
constexpr std::size_t kBlocksByte = 10;

battle::AttackAnimationParams frameParamsFromBytes(std::span<const std::uint8_t> block) {
    battle::AttackAnimationParams params = paramsFromBytes(block);
    params.criticalFlash = (block[0] & kCriticalFlashBit) != 0;
    params.monsterAttack = (block[0] & kMonsterAttackBit) != 0;
    if (block[kCoveredAllyByte] != 0) {
        params.coveredAlly = battle::BattlerSlot::fromIndex(block[kCoveredAllyByte] % battle::kBattlerSlots);
    }
    for (std::size_t i = 0; i < battle::kPartySlots; ++i) {
        params.blocks[i] = static_cast<battle::BlockShown>(block[kBlocksByte + i]);
    }
    return params;
}

// Whether an animation's own handler reads the attack and second value after its command: Mimic's,
// Defend's and the two AI commands' are a bare return (btlgfx CmdAnim_12), Runic's reads its params
// block alone (CmdAnim_25), and a command with its top bit set plays nothing (GfxCmd_06).
bool animationReadsItsAttack(std::uint8_t what) {
    constexpr std::array<std::uint8_t, 5> kReadNothing{0x12, 0x15, 0x1E, 0x1F,
                                                       static_cast<std::uint8_t>(battle::SpecialAnimation::RunicAbsorbsSpell)};
    return (what & kTopBit) == 0 && std::find(kReadNothing.begin(), kReadNothing.end(), what) == kReadNothing.end();
}

std::string numeralText(const battle::DamageNumber& number) {
    return std::to_string(number.target.index()) + ":" +
           (number.miss ? std::string{"miss"} : std::to_string(number.amount)) + (number.heals ? "+" : "");
}

// The port's script as the cartridge's queue holds it, one line per queue command; each animation, a
// bounce and a Launcher's hits with the params block it shows with. `params` are the blocks not yet
// handed to an animation, in order (BattleScriptBuilder::params()); a finished script carries each
// animation's on the animation.
std::string portScriptText(const std::vector<battle::BattleScriptCommand>& commands,
                           const std::deque<battle::AttackAnimationParams>* params) {
    std::string text;
    std::size_t nextParams = 0;
    for (const battle::BattleScriptCommand& command : commands) {
        std::visit(
            [&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, battle::AttackAnimation>) {
                    text += "animation " + std::to_string(commandByte(c.what));
                    if (animationReadsItsAttack(commandByte(c.what))) {
                        text += " " + std::to_string(static_cast<std::uint8_t>(c.attack)) + " " +
                                std::to_string(c.secondary);
                    }
                    if (params == nullptr) {
                        text += " | " + frameParamsText(c.params);
                    } else if (nextParams < params->size()) {
                        text += " | " + frameParamsText((*params)[nextParams++]);
                    } else {
                        text += " | no params";
                    }
                } else if constexpr (std::is_same_v<T, battle::AttackNameShown>) {
                    text += "name 0 " + std::to_string(static_cast<std::uint8_t>(c.attack));
                } else if constexpr (std::is_same_v<T, battle::ItemNameShown>) {
                    text += "name 1 " + std::to_string(static_cast<std::uint8_t>(c.item));
                } else if constexpr (std::is_same_v<T, battle::EsperNameShown>) {
                    text += "name 2 " +
                            std::to_string(static_cast<std::uint8_t>(static_cast<std::uint8_t>(c.esper) - kFirstEsperAttack));
                } else if constexpr (std::is_same_v<T, battle::BushidoNameShown>) {
                    text += "name 4 " + std::to_string(static_cast<std::uint8_t>(c.technique));
                } else if constexpr (std::is_same_v<T, battle::CommandNameShown>) {
                    text += "name 5 " + std::to_string(static_cast<std::uint8_t>(c.command));
                } else if constexpr (std::is_same_v<T, battle::MonsterSpecialAttackNameShown>) {
                    text += "monster name " + std::to_string(static_cast<std::uint16_t>(c.monster));
                } else if constexpr (std::is_same_v<T, battle::BattleMessage>) {
                    text += "message " + std::to_string(c.message);
                } else if constexpr (std::is_same_v<T, battle::PoseReset>) {
                    text += "pose reset " + std::to_string(c.battler.index());
                } else if constexpr (std::is_same_v<T, battle::DamageNumbersShown>) {
                    if (c.wait == battle::DamageNumberWait::UntilGone) {
                        text += "numerals together";
                        for (const battle::DamageNumber& number : c.numbers) {
                            text += " " + numeralText(number);
                        }
                    } else {
                        for (std::size_t i = 0; i < c.numbers.size(); ++i) {
                            text += (i == 0 ? "numeral " : "; numeral ") + numeralText(c.numbers[i]);
                        }
                    }
                } else if constexpr (std::is_same_v<T, battle::ReflectedAttackShown> ||
                                     std::is_same_v<T, battle::ScatteredHitsShown>) {
                    text += shownText(c);
                } else {
                    text += "a command the attack frame does not write";
                }
            },
            command);
        text += "; ";
    }
    if (params != nullptr) {
        for (; nextParams < params->size(); ++nextParams) {
            text += "params with no animation | " + frameParamsText((*params)[nextParams]) + "; ";
        }
    }
    return text;
}

// The keys of the cells that belong to the attack itself, which end with the command.
bool attackOwnCell(const std::string& cell) {
    static const std::set<std::string> kOwn{
        "missed by the effect", "power",       "hit rate",       "traits",         "flags 1",
        "flags 2",              "damage",      "multiplier",     "command",        "attack",
        "second value",         "targets",     "params",         "earlier targets", "shown Miss",
        "message",              "message item", "message amount", "spell next",     "sketched",
        "hidden without targets", "the weapon's spellcast", "attacks left", "damage modification",
        "re-aimed at random",   "quiet misses",
    };
    return kOwn.contains(cell) || cell.starts_with("next attack: ") || cell.starts_with("damage taken of ") ||
           cell.starts_with("damage healed of ");
}

void putApplied(EffectCells& out, const ApplyDamageResult& applied) {
    const auto entry = [](const std::optional<std::uint16_t>& value) {
        return value ? std::to_string(*value) : std::string{"none"};
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string of = " of battler " + std::to_string(i);
        out.put("HP" + of, applied.hp[i]);
        out.put("MP" + of, applied.mp[i]);
        out.put("statuses to set" + of, statusesText(applied.statusesToSet[i]));
        out.put("last attacker" + of, applied.lastAttackerMark[i]);
        out.put("last damaged by" + of, slotText(applied.lastDamagedBy[i]));
        out.put("castable spells to work out" + of, applied.enabledMagic[i]);
        out.put("damage taken" + of, entry(applied.ledger.taken[i]));
        out.put("damage healed" + of, entry(applied.ledger.healed[i]));
    }
    out.put("Black Belt targets", std::to_string(maskOf(applied.blackBeltEligible)));
    out.put("shown Miss", std::to_string(maskOf(applied.missed)));
    out.put("the Golem's HP", applied.golemHp);
    out.put("the weapon's spellcast", applied.weaponSpellcast);
}

void putStatusChanges(EffectCells& out, const StatusChangesResult& changes) {
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string of = " of battler " + std::to_string(i);
        out.put("statuses" + of, statusesText(changes.status[i]));
        out.put("statuses to set" + of, statusesText(changes.statusesToSet[i]));
        out.put("statuses to clear" + of, statusesText(changes.statusesToClear[i]));
        out.put("statuses for a counterattack" + of, statusesText(changes.statusForCounter[i]));
        out.put("to work out again" + of, changes.pendingUpdates[i]);
        out.put("menu may open" + of, changes.menuMayOpen[i]);
        out.put("gauge empties after the action" + of, changes.emptyGaugeAfterAction[i]);
        out.put("Phantasm" + of, changes.phantasm[i]);
        out.put("equipment to work out" + of, changes.equipmentChanged[i]);
        out.put("sleep timer" + of, changes.sleepTimer[i]);
        out.put("stop timer" + of, changes.stopTimer[i]);
        out.put("reflect timer" + of, changes.reflectTimer[i]);
        out.put("freeze timer" + of, changes.freezeTimer[i]);
    }
    out.put("died this action", std::to_string(maskOf(changes.diedAwaitingCounter)));
    out.put("statuses changed", std::to_string(maskOf(changes.statusChanged)));
    out.put("monsters standing", std::to_string(maskOf(changes.monstersStanding)));
}

// The cells a target's special effect is read back by, with the script's own keys left to the whole
// script.
// A whole attack's params block is in the script; the direct-page copy it was written from is cleared
// at every attack's start (ClearGfxParams) and read by nothing after the frame, and after a bounce off
// Reflect it holds the bounce's block.
void putEffectCells(EffectCells& out, const std::map<std::string, std::string>& cells) {
    for (const auto& [cell, value] : cells) {
        if (cell != "script" && cell != "params" && !cell.starts_with("script params")) {
            out.cells[cell] = value;
        }
    }
}

std::uint8_t firstFlagsByte(const battle::AttackInFlight& attack) {
    std::uint8_t first = kAttackFlagsAtStart;
    const auto clearIf = [&](bool condition, std::uint8_t bit) {
        if (condition) {
            first = static_cast<std::uint8_t>(first & ~bit);
        }
    };
    clearIf(!attack.drainCapped, kDrainCappedBit);
    clearIf(attack.runicAbsorbing, kRunicShownBit);
    clearIf(!attack.firstPass, kFirstPassBit);
    clearIf(attack.followUpSameTargets, kOtherTargetsBit);
    clearIf(attack.canBeCritical, kCannotBeCriticalBit);
    clearIf(attack.nameShown, kNameNotShownBit);
    return first;
}

std::uint8_t secondFlagsByte(const battle::AttackInFlight& attack) {
    std::uint8_t second = kAttackFlagsAtStart;
    const auto clearIf = [&](bool condition, std::uint8_t bit) {
        if (condition) {
            second = static_cast<std::uint8_t>(second & ~bit);
        }
    };
    clearIf(!attack.vanishApplies, kVanishAppliesBit);
    clearIf(attack.twoHanded, kNotTwoHanded);
    clearIf(!attack.rowIgnored, kRowIgnoredBit);
    clearIf(!attack.ignoreDefenseStopsMultiplier, kIgnoreDefenseStopsMultiplier);
    clearIf(!attack.emptyTargetsReaim, kEmptyTargetsReaimBit);
    clearIf(attack.automaticCritical, kAutomaticCriticalClearBit);
    clearIf(attack.hitsNoOne, kHitsSomeoneBit);
    return second;
}

// What the command's next attack starts from, field by field, out of the two flag bytes.
void putNextAttack(EffectCells& out, std::uint8_t first, std::uint8_t second) {
    out.put("next attack: drain capped", (first & kDrainCappedBit) != 0);
    out.put("next attack: Runic's animation to show", (first & kRunicShownBit) == 0);
    out.put("next attack: first pass", (first & kFirstPassBit) != 0);
    out.put("next attack: the weapon's spell after the hit's targets", (first & kOtherTargetsBit) == 0);
    out.put("next attack: can be critical", (first & kCannotBeCriticalBit) == 0);
    out.put("next attack: name shown", (first & kNameNotShownBit) == 0);
    out.put("next attack: Vanish applies", (second & kVanishAppliesBit) != 0);
    out.put("next attack: two-handed", (second & kNotTwoHanded) == 0);
    out.put("next attack: row ignored", (second & kRowIgnoredBit) != 0);
    out.put("next attack: ignore-defense stops the multiplier", (second & kIgnoreDefenseStopsMultiplier) != 0);
    out.put("next attack: no targets aims again", (second & kEmptyTargetsReaimBit) != 0);
    out.put("next attack: automatic critical", (second & kAutomaticCriticalClearBit) == 0);
    out.put("next attack: hits no one", (second & kHitsSomeoneBit) == 0);
}

// The port's cells after a whole attack. With `attackCells` false the attack's own cells are left out:
// those are the cells of a command run whole, whose attack ends with it.
AttackResult portAttackCells(const battle::BattleState& state, const battle::AttackInFlight& attack,
                             const std::vector<battle::BattleScriptCommand>& commands,
                             const std::deque<battle::AttackAnimationParams>* params, bool awayUnreachable,
                             bool attackCells) {
    EffectCells out;
    battle::BattleScriptBuilder none;
    putApplied(out, portApplied(state, attack, none));
    putStatusChanges(out, statusChangesOf(state));
    DamageScenario scenario{.state = state, .attack = attack};
    TargetEffectRun run{.after = std::move(scenario), .missed = !attack.scratch.missedByEffect.empty()};
    putEffectCells(out, targetEffectResult(run).cells);
    // The sketch cell holds the sketcher while it makes its attack, and the monster a Sketch caught.
    std::optional<battle::BattlerSlot> sketched = attack.scratch.sketcher;
    if (attack.scratch.sketch) {
        sketched = attack.scratch.sketch->monster;
    }
    out.put("sketched", slotText(sketched));
    out.put("flipped", std::to_string(maskOf(state.flippedByControl)));
    // A message's item and amount stay in their cells for the script to show; the port's message
    // carries them.
    for (const battle::BattleScriptCommand& command : commands) {
        if (const auto* message = std::get_if<battle::BattleMessage>(&command)) {
            if (message->firstNumber) {
                out.put("message item", static_cast<std::uint8_t>(*message->firstNumber));
            }
            if (message->secondNumber) {
                out.put("message amount", static_cast<int>(*message->secondNumber & 0xFFFF));
            }
        }
    }
    out.put("script", portScriptText(commands, params));
    putNextAttack(out, firstFlagsByte(attack), secondFlagsByte(attack));
    out.put("attacks left", static_cast<std::uint8_t>(attack.scratch.attacksLeft));
    out.put("damage modification", attack.scratch.damageModification);
    out.put("re-aimed at random", attack.scratch.randomizeTargets);
    out.put("quiet misses", attack.scratch.quietMisses);
    out.put("away from the field unreachable", awayUnreachable);
    out.put("Interceptor guarding", slotText(state.scratch.interceptorGuarding));
    if (!attackCells) {
        std::erase_if(out.cells, [](const auto& cell) { return attackOwnCell(cell.first); });
    }
    return AttackResult{.cells = std::move(out.cells)};
}

AttackRun runPortAttack(DamageScenario scenario, bool self) {
    AttackRun run{.after = std::move(scenario)};
    battle::BattleHooks hooks;
    if (self) {
        battle::runSelfAttack(run.after.state, hooks, run.script, run.after.attack, run.after.attacker);
    } else {
        battle::runAttack(run.after.state, hooks, run.script, run.after.attack, run.after.attacker);
    }
    return run;
}

}  // namespace

void DamageOracle::writeAttackCells(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    // writeApplyCells() writes the to-work-out word whole; the Air Anchor byte in its high byte is
    // written after it.
    writeTargetEffectCells(scenario);

    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    writeInFlightByte(kTargetingByte, attack.targeting.bits);
    writeInFlightByte(kExtraMultiplier, static_cast<std::uint8_t>(attack.extraMultiplier));
    writeInFlightByte(kFirstAttackFlags, firstFlagsByte(attack));
    writeInFlightByte(kAttackFlags, secondFlagsByte(attack));
    writeInFlightWord(kCurrentTargets, maskOf(attack.scratch.currentTargets));
    // The frame loads, subtracts and clears the MP cost as a word; only that clear writes its high byte.
    writeInFlightWord(kMpCost, attack.scratch.mpCost);
    writeByte(kRunDifficulty, state.runDifficulty);
    writeInFlightByte(kNameKind, nameKindByte(attack.scratch.nameKind));
    writeInFlightByte(kFightCommand, attack.scratch.fightCommand ? static_cast<std::uint8_t>(*attack.scratch.fightCommand) : kNoOne);
    writeInFlightByte(kRandomizeTargets, attack.scratch.randomizeTargets ? 0 : kNoOne);
    writeByte(kInterceptorGuard, slotByte(state.scratch.interceptorGuarding));
    writeInFlightByte(kQueuedAttack, static_cast<std::uint8_t>(attack.queuedAttack));
    writeWord(kFacingRight, maskOf(state.facingRight));
    writeByte(kStragoSlot, slotByte(stragoInParty(state)));
    writeBytes(retropp::MemoryRegion{.at = kLoresKnown, .size = kLoreBytes}, loreBytes(state.party.loresKnown));
    writeBytes(retropp::MemoryRegion{.at = kLoresLearned, .size = kLoreBytes}, loreBytes(state.loresLearned));
    for (std::size_t lore = 0; lore < game::kLores; ++lore) {
        writeByte(kLoreList + static_cast<std::uint32_t>(lore),
                  state.party.loresKnown.test(lore) ? static_cast<std::uint8_t>(kLoreListFirstEntry + lore) : kNoOne);
    }
    // The sketch cell holds the sketcher while it makes the attack its sketch caught.
    std::optional<battle::BattlerSlot> sketched = attack.scratch.sketcher;
    if (!sketched && attack.scratch.sketch) {
        sketched = attack.scratch.sketch->monster;
    }
    writeInFlightByte(kSketched, slotByte(sketched));
    // The script starts empty: the frame writes every command and params block of its own.
    writeInFlightByte(kQueueEnd, 0);
    writeInFlightByte(kAnimationPlace, 0);
    writeInFlightByte(kNextParamsBlock, 0);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        std::uint8_t triggers = battler.triggers.retort ? kRetortBit : 0;
        triggers |= battler.triggers.monsterRunic ? kMonsterRunicBit : 0;
        triggers |= battler.triggers.runic ? kRunicBit : 0;
        writeByte(kTriggers + at, triggers);
        writeByte(kSpecialAttack + at, battler.specialAttack.packed);
        writeByte(kSpecialAttackAnimation + at, static_cast<std::uint8_t>(battler.specialAttackAnimation));
        writeWord(kNamedMonster + at, static_cast<std::uint16_t>(battler.monster.value_or(battler.rage.value_or(MonsterId{}))));
        writeByte(kVigor + at, battler.vigor);
        writeByte(kMagicPower + at, battler.magicPower);
        for (const auto& [hand, place] : {std::pair{&battler.weaponHand, at}, std::pair{&battler.shieldHand, at + 1}}) {
            writeByte(kHandPower + place, hand->battlePower);
            writeByte(kHandItems + place, static_cast<std::uint8_t>(hand->item));
            writeByte(kHandHitRate + place, hand->hitRate);
            writeByte(kHandElement + place, hand->element.bits);
            writeByte(kHandWeaponFlags + place, hand->weaponFlags.bits);
            writeByte(kHandSpellCast + place, hand->spellCast.raw);
            writeByte(kHandBlockEffect + place, hand->blockEffect.packed);
        }
    }
}

AttackResult DamageOracle::attack(const DamageScenario& scenario) {
    writeAttackCells(scenario);
    call([&] { execAttack_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    return readAttackResult(true);
}

AttackResult DamageOracle::selfAttack(const DamageScenario& scenario) {
    writeAttackCells(scenario);
    call([&] { execSelfAttack_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    return readAttackResult(true);
}

AttackResult DamageOracle::readAttackResult(bool attackCells) {
    EffectCells out;
    putApplied(out, readApplyCells());
    putStatusChanges(out, readStatusChangesResult());
    putEffectCells(out, readTargetEffectResult().cells);
    out.put("sketched", slotText(slotTimesTwo(readByte(kSketched))));
    out.put("flipped", std::to_string(readWord(kFlippedSet)));

    // The whole queue, each animation, bounce and Launcher's hits with the next params block.
    std::string script;
    const std::uint8_t end = readByte(kQueueEnd);
    const std::vector<std::uint8_t> queue = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = end});
    const int blockCount = readByte(kNextParamsBlock) / kParamsBlockSize;
    int nextBlock = 0;
    const auto takeBlock = [&]() -> std::optional<std::vector<std::uint8_t>> {
        if (nextBlock >= blockCount) {
            return std::nullopt;
        }
        return machine_.read(retropp::MemoryRegion{
            .at = kParamsBlocks + kParamsBlockSize * static_cast<std::uint32_t>(nextBlock++), .size = kParamsBlockSize});
    };
    std::uint32_t buffers = 0;
    for (std::size_t at = 0; at + kQueueCommandSize <= queue.size(); at += kQueueCommandSize) {
        const std::span<const std::uint8_t> entry{queue.data() + at, kQueueCommandSize};
        const auto word = static_cast<std::uint16_t>(entry[2] | entry[3] << 8);
        const auto numeral = [](battle::BattlerSlot slot, std::uint16_t value) {
            return battle::DamageNumber{.target = slot,
                                        .amount = static_cast<std::uint16_t>(value & kAmountBits),
                                        .miss = (value & kMissMark) != 0,
                                        .heals = (value & kHealMark) != 0};
        };
        switch (entry[0]) {
        case kShowAnimationCommand: {
            script += "animation " + std::to_string(entry[1]);
            if (animationReadsItsAttack(entry[1])) {
                script += " " + std::to_string(entry[2]) + " " + std::to_string(entry[3]);
            }
            const auto block = takeBlock();
            script += block ? " | " + frameParamsText(frameParamsFromBytes(*block)) : std::string{" | no params"};
            break;
        }
        case kReflectedCommand:
        case kScatteredCommand: {
            const auto block = takeBlock();
            script += block ? bounceText(entry, *block) : "bounce " + std::to_string(entry[0]) + " | no params";
            break;
        }
        case kNameCommand:
            script += "name " + std::to_string(entry[1]) + " " + std::to_string(entry[2]);
            break;
        case kMonsterNameCommand:
            script += "monster name " + std::to_string(entry[1] | entry[2] << 8);
            break;
        case kMessageCommand:
            script += "message " + std::to_string(entry[1]);
            break;
        case kPoseResetCommand:
            script += "pose reset " + std::to_string(entry[1]);
            break;
        case kOneNumeral:
            script += "numeral " + numeralText(numeral(battle::BattlerSlot::fromIndex(entry[1] % battle::kBattlerSlots), word));
            break;
        case kEveryNumeral: {
            script += "numerals together";
            const std::uint32_t buffer = kNumeralBuffer + kNumeralBufferSize * buffers++;
            for (int index = battle::kBattlerSlots - 1; index >= 0; --index) {
                const std::uint16_t value = readWord(buffer + static_cast<std::uint32_t>(index * 2));
                if (value != kNoDamage) {
                    script += " " + numeralText(numeral(battle::BattlerSlot::fromIndex(index), value));
                }
            }
            break;
        }
        default:
            script += "command " + std::to_string(entry[0]) + " " + std::to_string(entry[1]) + " " +
                      std::to_string(entry[2]) + " " + std::to_string(entry[3]);
            break;
        }
        script += "; ";
    }
    while (const auto block = takeBlock()) {
        script += "params with no animation | " + frameParamsText(frameParamsFromBytes(*block)) + "; ";
    }
    out.put("script", script);

    putNextAttack(out, readByte(kFirstAttackFlags), readByte(kAttackFlags));
    out.put("attacks left", readByte(kAttacksLeft));
    out.put("damage modification", readByte(kDamageModification) != 0);
    out.put("re-aimed at random", (readByte(kRandomizeTargets) & kTopBit) == 0);
    out.put("quiet misses", readByte(kQuietMisses) == 0);
    out.put("away from the field unreachable", (readByte(kReachKeepsAway) & kTopBit) != 0);
    out.put("Interceptor guarding", slotText(slotTimesTwo(readByte(kInterceptorGuard))));
    if (!attackCells) {
        std::erase_if(out.cells, [](const auto& cell) { return attackOwnCell(cell.first); });
    }
    return AttackResult{.cells = std::move(out.cells)};
}

AttackRun portAttack(DamageScenario scenario) {
    return runPortAttack(std::move(scenario), false);
}

AttackRun portSelfAttack(DamageScenario scenario) {
    return runPortAttack(std::move(scenario), true);
}

AttackResult attackResult(const AttackRun& run) {
    return portAttackCells(run.after.state, run.after.attack, run.script.commands(), &run.script.params(),
                           run.after.attack.scratch.awayFromTheFieldUnreachable, true);
}

std::string differences(const AttackResult& port, const AttackResult& guest) {
    return cellDifferences(port.cells, guest.cells);
}

DamageScenario drawAttackScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetEffectScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    battle::Battler& attacker = state.at(s.attacker);

    // The attack as a command's body hands it to the frame: Fight three times in eight, Capture once,
    // otherwise any other command with an aiming byte.
    const unsigned pick = rng() % 8;
    BattleCommandId command = pick < 3 ? BattleCommandId::FIGHT : BattleCommandId::CAPTURE;
    while (pick > 3 && (command == BattleCommandId::FIGHT || command == BattleCommandId::CAPTURE)) {
        command = static_cast<BattleCommandId>(rng() % kCommandTargetingInit.size());
    }
    attack.command = command;
    attack.queuedCommand = command;
    attack.attack = static_cast<AttackId>(byte(rng));
    attack.queuedAttack = static_cast<AttackId>(byte(rng));
    attack.targeting = Targeting{byte(rng)};
    attack.targets = oneIn(rng, 4) ? battle::BattlerSet{} : anyBattlers(rng);
    attack.rules = battle::AimingRules{.noCharacterTargets = oneIn(rng, 4),
                                       .randomTarget = oneIn(rng, 4),
                                       .fallBackToEarlierTargets = oneIn(rng, 4),
                                       .mayTargetTheFallen = oneIn(rng, 4),
                                       .keepTargetsThatAreGone = oneIn(rng, 4),
                                       .opposingSideOnly = oneIn(rng, 4),
                                       .excludeAttacker = oneIn(rng, 4)};
    constexpr std::array<battle::AttackNameKind, 8> kinds{
        battle::AttackNameKind::Attack,  battle::AttackNameKind::Item,    battle::AttackNameKind::Esper,
        battle::AttackNameKind::Bushido, battle::AttackNameKind::Command, battle::AttackNameKind::MonsterSpecialAttack,
        battle::AttackNameKind::JokerDoom, battle::AttackNameKind::Blitz};
    const unsigned kind = rng() % 12;
    attack.scratch.nameKind.reset();
    if (kind < kinds.size() && (kinds[kind] != battle::AttackNameKind::MonsterSpecialAttack || s.attacker.isMonster())) {
        attack.scratch.nameKind = kinds[kind];
    }
    attacker.specialAttack.packed = byte(rng);
    attacker.specialAttackAnimation = static_cast<MonsterAttackAnimation>(byte(rng));
    // A monster's special attack is named for it only as the Special command's attack ($EF) loads it
    // (InitAttacker), with that attack's own record.
    if (attack.scratch.nameKind == battle::AttackNameKind::MonsterSpecialAttack) {
        attack.attack = AttackId::SPECIAL;
        battle::loadAttackRecord(attack, AttackId::SPECIAL);
    }
    attack.record.mpCost = oneIn(rng, 2) ? 0 : byte(rng);
    attack.scratch.mpCost = 0;
    if (oneIn(rng, 2)) {
        attack.scratch.mpCost = byte(rng);
        if (oneIn(rng, 4) && attacker.mp < 0xFF) {
            attack.scratch.mpCost = static_cast<std::uint8_t>(attacker.mp + 1 + upTo(rng, 0xFE - attacker.mp));
        }
    }
    attack.scratch.quietMisses = oneIn(rng, 4);
    attack.scratch.aimExactly = oneIn(rng, 8);
    attack.scratch.randomizeTargets = oneIn(rng, 4);
    attack.scratch.currentTargets = anyBattlers(rng);
    attack.scratch.weaponSpellcast = {};
    if (oneIn(rng, 4)) {
        attack.scratch.weaponSpellcast.raw =
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(SpellCastMode::RANDOM_ON_ATTACK) | (byte(rng) & 0x3F));
    }
    if (oneIn(rng, 16)) {
        attack.scratch.sketcher = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    attack.firstPass = !oneIn(rng, 4);
    attack.extraMultiplier = upTo(rng, 3);
    attack.scratch.attacksLeft = upTo(rng, 2);
    attack.canBeCritical = oneIn(rng, 2);
    if (attack.canBeCritical) {
        attack.rules.opposingSideOnly = true;
    }
    attack.automaticCritical = oneIn(rng, 16);
    attack.emptyTargetsReaim = !oneIn(rng, 8);
    attack.rowIgnored = !oneIn(rng, 4);
    attack.hitsNoOne = oneIn(rng, 16);
    attack.nameShown = oneIn(rng, 8);

    // The attacker's statuses that change its attack, and Air Anchor on it.
    for (const StatusId status : {StatusId::MORPH, StatusId::BERSERK, StatusId::IMP, StatusId::SILENCE}) {
        if (oneIn(rng, 8)) {
            attacker.status.set(status);
        } else {
            attacker.status.clear(status);
        }
    }
    attacker.airAnchored = oneIn(rng, 8);
    if (oneIn(rng, 4)) {
        attack.record.flags1.set(AttackFlag1::ENABLE_RUNIC);
    }
    if (oneIn(rng, 12)) {
        state.extraHits = 1 + upTo(rng, 7);
    } else {
        state.extraHits.reset();
    }
    state.facingRight = anyBattlers(rng);
    constexpr std::array<battle::BattleType, 3> layouts{battle::BattleType::Side, battle::BattleType::Pincer,
                                                         battle::BattleType::Front};
    state.battleType = layouts[rng() % layouts.size()];
    if (oneIn(rng, 4)) {
        battle::Battler& strago = state.at(battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1)));
        strago.actor = CharacterPropId::STRAGO;
        // Half the time in none of the statuses that keep him from learning (LearnLore :8800-8802).
        if (oneIn(rng, 2)) {
            for (const StatusId status : {StatusId::DEAD, StatusId::PETRIFY, StatusId::BLIND, StatusId::ZOMBIE,
                                          StatusId::SLEEP, StatusId::CONFUSE, StatusId::BERSERK, StatusId::STOP,
                                          StatusId::FROZEN, StatusId::RAGE, StatusId::HIDE}) {
                strago.status.clear(status);
            }
        }
        if (oneIn(rng, 2)) {
            attack.record.flags1.set(AttackFlag1::LEARNABLE_LORE);
            // One of the lores, for Strago to see.
            if (oneIn(rng, 2)) {
                attack.attack = static_cast<AttackId>(static_cast<std::uint8_t>(AttackId::CONDEMNED) +
                                                      upTo(rng, static_cast<int>(game::kLores) - 1));
            }
        }
    }
    for (std::size_t lore = 0; lore < game::kLores; ++lore) {
        state.party.loresKnown.set(lore, oneIn(rng, 2));
        state.loresLearned.set(lore, oneIn(rng, 4));
    }
    for (battle::Battler& battler : state.battlers) {
        battler.vigor = byte(rng);
        battler.magicPower = byte(rng);
        battler.triggers.runic = oneIn(rng, 8);
        battler.triggers.monsterRunic = oneIn(rng, 8);
    }
    // No attack removes and toggles its statuses at once; on both the cartridge runs unrelated code.
    if (attack.record.flags2.has(AttackFlag2::REMOVE_STATUS)) {
        attack.record.flags2.clear(AttackFlag2::TOGGLE_STATUS);
    }
    // An effect comes on a record it can come on: a weapon's and a command's ride the plain hit, which
    // ignores Reflect, and an attack's own rides an attack that can bounce off Reflect only if one
    // that can carries it. Sketch's command makes one attack with no spell to cast after it.
    static const std::set<std::uint8_t> kBouncingEffects = [] {
        std::set<std::uint8_t> effects;
        for (int id = 0; id <= 0xFF; ++id) {
            const AttackProperties& record = getAttackProperties(static_cast<AttackId>(id));
            if (!record.flags1.has(AttackFlag1::IGNORE_REFLECT)) {
                effects.insert(battle::SpecialEffect::of(record.specialEffect).index());
            }
        }
        return effects;
    }();
    if (!kBouncingEffects.contains(attack.effect.index())) {
        attack.record.flags1.set(AttackFlag1::IGNORE_REFLECT);
    }
    if (attack.effect == battle::SpecialEffect::of(battle::CommandEffect::Sketch)) {
        attack.scratch.attacksLeft = 0;
        attack.scratch.followUpSpell.reset();
        attack.scratch.weaponSpellcast = {};
    }

    // Fight and Capture as their body hands them on (fightAttack): each attack loads a hand, two
    // swings or an Offering's eight, the plain hit (a monster's special attack one time in four),
    // able to be critical and named by nothing but a monster's special attack. A party member's hands
    // are drawn whole, the shield hand empty one time in two and the weapon hand without power one
    // time in eight; a monster's as loading it leaves them, a weapon hand with its attack's power,
    // graphic and hit rate and nothing else (only a party member's equipment fills the rest).
    if (command == BattleCommandId::FIGHT || command == BattleCommandId::CAPTURE) {
        attack.scratch.fightCommand = command;
        attack.attack = s.attacker.isMonster() && oneIn(rng, 4) ? AttackId::SPECIAL : AttackId::BATTLE;
        attack.queuedAttack = attack.attack;
        const bool offering = oneIn(rng, 4);
        if (offering) {
            attacker.relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
        } else {
            attacker.relicEffects4.clear(RelicEffect4::FIGHT_TO_X_FIGHT);
        }
        attack.scratch.attacksLeft = offering ? 7 : 1;
        attack.scratch.dragonHorn = false;
        attack.canBeCritical = true;
        attack.scratch.nameKind.reset();
        const auto drawHand = [&](battle::Hand& hand, bool empty, bool monster) {
            hand = battle::Hand{};
            if (empty) {
                return;
            }
            hand.item = static_cast<ItemId>(byte(rng));
            hand.battlePower = oneIn(rng, 8) ? 0 : byte(rng);
            hand.hitRate = byte(rng);
            if (monster) {
                return;
            }
            hand.element.bits = sparse(rng);
            hand.weaponFlags.bits = byte(rng);
            hand.spellCast.raw = oneIn(rng, 4) ? byte(rng) : 0;
            hand.blockEffect.packed = byte(rng);
        };
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
            battle::Battler& battler = state.at(slot);
            drawHand(battler.weaponHand, false, slot.isMonster());
            drawHand(battler.shieldHand, slot.isMonster() || oneIn(rng, 2), slot.isMonster());
        }
    }

    // The links as the battle makes them, each with both its ends: a party member controlling a
    // monster, a monster holding a party member in Seize, a battler charming another, and a Love
    // Token's bodyguard. A battler is at one end of each kind of link at most.
    for (battle::Battler& battler : state.battlers) {
        battler.controlling.reset();
        battler.controlledBy.reset();
        battler.controlEnding = false;
        battler.seizing.reset();
        battler.seizedBy.reset();
        battler.charming.reset();
        battler.charmedBy.reset();
        battler.bodyguard.reset();
        battler.guarding.reset();
    }
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        const battle::BattlerSlot monster = battle::BattlerSlot::monster(position);
        const battle::BattlerSlot member = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
        if (oneIn(rng, 6) && !state.at(member).controlling) {
            const bool ending = oneIn(rng, 4);
            state.at(member).controlling = monster;
            state.at(member).controlEnding = ending;
            state.at(monster).controlledBy = member;
            state.at(monster).controlEnding = ending;
        }
        if (oneIn(rng, 8) && !state.at(member).seizedBy && !state.at(monster).seizing) {
            state.at(monster).seizing = member;
            state.at(member).seizedBy = monster;
        }
    }
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        const battle::BattlerSlot other = anySlot(rng);
        if (other == slot) {
            continue;
        }
        if (oneIn(rng, 10) && !state.at(slot).charmedBy && !state.at(other).charming) {
            state.at(slot).charmedBy = other;
            state.at(other).charming = slot;
        }
        if (oneIn(rng, 10) && !state.at(slot).bodyguard && !state.at(other).guarding) {
            state.at(slot).bodyguard = other;
            state.at(other).guarding = slot;
        }
    }
    return s;
}

DamageScenario drawSelfAttackScenario(std::mt19937& rng) {
    DamageScenario s = drawAttackScenario(rng);
    battle::AttackInFlight& attack = s.attack;
    attack.command = oneIn(rng, 4) ? BattleCommandId::MAGIC : battle::kNothingShown;
    // The battle's own actions load no hand; the attacks left are drawn as for the other commands.
    if (std::exchange(attack.scratch.fightCommand, std::nullopt)) {
        attack.scratch.attacksLeft = upTo(rng, 2);
    }
    for (std::uint8_t& status : attack.record.statuses.bytes) {
        status = sparse(rng);
    }
    if (oneIn(rng, 2)) {
        attack.record.flags2.set(AttackFlag2::REMOVE_STATUS);
        attack.record.flags2.clear(AttackFlag2::TOGGLE_STATUS);
    } else {
        attack.record.flags2.clear(AttackFlag2::REMOVE_STATUS);
    }
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    attack.attackerLevel = oneIn(rng, 2) ? 0 : byte(rng);
    attack.attackerStat = oneIn(rng, 2) ? 0 : byte(rng);
    attack.castingSwirlSuppressed = oneIn(rng, 2);
    const auto drawn = [&](auto& flags, auto flag) {
        if (oneIn(rng, 4)) {
            flags.set(flag);
        } else {
            flags.clear(flag);
        }
    };
    drawn(attack.record.flags2, AttackFlag2::DRAIN);
    drawn(attack.record.flags2, AttackFlag2::RESTORE_HP_MP);
    drawn(attack.record.traits, AttackTrait::IGNORE_DEFENSE);
    drawn(attack.record.traits, AttackTrait::NO_DAMAGE_SPLIT);
    drawn(attack.record.traits, AttackTrait::INVERT_ON_UNDEAD);
    return s;
}

// --- the party's records at a battle's end --------------------------------------------------------

namespace {

// The sixteen character records, 37 bytes each, and the places of a record's fields within it
// (notes/field-ram.txt): the actor, graphics, name, level, HP, maximum HP with the HP boost in its top
// two bits, MP, maximum MP with the MP boost, experience (three bytes), the first and fourth status
// bytes, the four commands, the four stats, the esper and the six pieces of equipment.
constexpr std::uint32_t kCharacterRecords = kWorkRam + 0x1600;
constexpr std::uint32_t kRecordSize = 37;
constexpr std::size_t kRecordGraphics = 1;
constexpr std::size_t kRecordName = 2;
constexpr std::size_t kRecordLevel = 8;
constexpr std::size_t kRecordHp = 9;
constexpr std::size_t kRecordMaxHp = 11;
constexpr std::size_t kRecordMp = 13;
constexpr std::size_t kRecordMaxMp = 15;
constexpr std::size_t kRecordExperience = 17;
constexpr std::size_t kRecordFirstStatus = 20;
constexpr std::size_t kRecordFourthStatus = 21;
constexpr std::size_t kRecordCommands = 22;
constexpr std::size_t kRecordVigor = 26;
constexpr std::size_t kRecordSpeed = 27;
constexpr std::size_t kRecordStamina = 28;
constexpr std::size_t kRecordMagicPower = 29;
constexpr std::size_t kRecordEsper = 30;
constexpr std::size_t kRecordEquipment = 31;
// The esper byte names an esper by its place among the 27, or holds this for none.
constexpr std::uint8_t kNoEsper = 0xFF;
constexpr std::size_t kEspers = 27;
// The maximum HP and MP words keep the boost in their top two bits.
constexpr int kBoostShift = 14;
constexpr std::uint16_t kMaximumBits = 0x3FFF;

// The spells each of the first twelve characters knows, 54 bytes each.
constexpr std::uint32_t kSpellsKnown = kWorkRam + 0x1A6E;
constexpr std::uint32_t kSpellsKnownSize = 648;

// The battle's-end cells in direct page: the win's flags, whether the party has magicite (battle
// switch 67's bit as the win copies it, which is magic points earned in the flags and is OR-ed into
// them when a spell is stored), the place of the spells being learned and the battle's magic points.
constexpr std::uint32_t kWinFlags = kWorkRam + 0x00F0;
constexpr std::uint32_t kPartyHasMagicite = kWorkRam + 0x00F1;
constexpr std::uint32_t kSpellListPlace = kWorkRam + 0x00F4;
constexpr std::uint32_t kMagicPoints = kWorkRam + 0x00FB;
constexpr std::uint8_t kMagiciteBit = 0x08;

const TracedRoutine& kLearnGenjuMagicRoutine = kTracedRoutines[7];
const TracedRoutine& kDoLevelUpRoutine = kTracedRoutines[8];

std::uint32_t recordAt(std::size_t index) {
    return kCharacterRecords + kRecordSize * static_cast<std::uint32_t>(index);
}

std::uint32_t spellListAt(CharacterPropId actor) {
    return kSpellsKnown + static_cast<std::uint32_t>(game::kLearnableSpells) * static_cast<std::uint8_t>(actor);
}

std::uint8_t esperByte(const std::optional<EsperId>& esper) {
    if (!esper) {
        return kNoEsper;
    }
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(*esper) - static_cast<std::uint8_t>(EsperId::RAMUH));
}

std::optional<EsperId> esperOfByte(std::uint8_t value) {
    if (value == kNoEsper) {
        return std::nullopt;
    }
    if (value >= kEspers) {
        throw std::runtime_error("the record's esper byte names no esper: " + std::to_string(value));
    }
    return static_cast<EsperId>(value + static_cast<std::uint8_t>(EsperId::RAMUH));
}

std::uint16_t boostedWord(int maximum, std::uint8_t boostBits) {
    return static_cast<std::uint16_t>((maximum & kMaximumBits) | boostBits << kBoostShift);
}

void addLine(std::string& out, const char* field, int port, int guest) {
    if (port != guest) {
        out += std::string{field} + ": port " + std::to_string(port) + ", cartridge " + std::to_string(guest) + "\n";
    }
}

// A character record's 37 bytes, as the cartridge keeps them.
std::array<std::uint8_t, kRecordSize> recordBytes(const game::CharacterRecord& record, std::uint8_t hpBoostBits,
                                                  std::uint8_t mpBoostBits) {
    std::array<std::uint8_t, kRecordSize> bytes{};
    bytes[0] = static_cast<std::uint8_t>(record.actor);
    bytes[kRecordGraphics] = static_cast<std::uint8_t>(record.graphics);
    std::copy(record.name.begin(), record.name.end(), bytes.begin() + kRecordName);
    bytes[kRecordLevel] = record.level;
    const auto putWord = [&](std::size_t at, std::uint16_t word) {
        bytes[at] = static_cast<std::uint8_t>(word & 0xFF);
        bytes[at + 1] = static_cast<std::uint8_t>(word >> 8);
    };
    putWord(kRecordHp, record.hp);
    putWord(kRecordMaxHp, boostedWord(record.maxHp, hpBoostBits));
    putWord(kRecordMp, record.mp);
    putWord(kRecordMaxMp, boostedWord(record.maxMp, mpBoostBits));
    bytes[kRecordExperience] = static_cast<std::uint8_t>(record.experience & 0xFF);
    bytes[kRecordExperience + 1] = static_cast<std::uint8_t>(record.experience >> 8 & 0xFF);
    bytes[kRecordExperience + 2] = static_cast<std::uint8_t>(record.experience >> 16 & 0xFF);
    bytes[kRecordFirstStatus] = record.status.bytes[0];
    bytes[kRecordFourthStatus] = record.status.bytes[3];
    for (std::size_t i = 0; i < record.commands.size(); ++i) {
        bytes[kRecordCommands + i] = static_cast<std::uint8_t>(record.commands[i]);
    }
    bytes[kRecordVigor] = record.vigor;
    bytes[kRecordSpeed] = record.speed;
    bytes[kRecordStamina] = record.stamina;
    bytes[kRecordMagicPower] = record.magicPower;
    bytes[kRecordEsper] = esperByte(record.esper);
    const std::array<ItemId, 6> equipment = record.equipment();
    for (std::size_t i = 0; i < equipment.size(); ++i) {
        bytes[kRecordEquipment + i] = static_cast<std::uint8_t>(equipment[i]);
    }
    return bytes;
}

// The record its 37 bytes hold, with the two boosts out of the top of its maximum HP and MP words.
struct RecordRead {
    game::CharacterRecord record;
    std::uint8_t hpBoostBits = 0;
    std::uint8_t mpBoostBits = 0;
};

RecordRead recordOf(std::span<const std::uint8_t> bytes) {
    const auto word = [&](std::size_t at) { return static_cast<std::uint16_t>(bytes[at] | bytes[at + 1] << 8); };
    RecordRead read;
    game::CharacterRecord& record = read.record;
    record.actor = static_cast<CharacterPropId>(bytes[0]);
    record.graphics = static_cast<CharacterGfxId>(bytes[kRecordGraphics]);
    std::copy(bytes.begin() + kRecordName, bytes.begin() + kRecordName + record.name.size(), record.name.begin());
    record.level = bytes[kRecordLevel];
    record.hp = word(kRecordHp);
    record.maxHp = static_cast<std::uint16_t>(word(kRecordMaxHp) & kMaximumBits);
    read.hpBoostBits = static_cast<std::uint8_t>(word(kRecordMaxHp) >> kBoostShift);
    record.mp = word(kRecordMp);
    record.maxMp = static_cast<std::uint16_t>(word(kRecordMaxMp) & kMaximumBits);
    read.mpBoostBits = static_cast<std::uint8_t>(word(kRecordMaxMp) >> kBoostShift);
    record.experience = static_cast<std::uint32_t>(bytes[kRecordExperience] | bytes[kRecordExperience + 1] << 8 |
                                                   bytes[kRecordExperience + 2] << 16);
    record.status.bytes[0] = bytes[kRecordFirstStatus];
    record.status.bytes[3] = bytes[kRecordFourthStatus];
    for (std::size_t i = 0; i < record.commands.size(); ++i) {
        record.commands[i] = static_cast<BattleCommandId>(bytes[kRecordCommands + i]);
    }
    record.vigor = bytes[kRecordVigor];
    record.speed = bytes[kRecordSpeed];
    record.stamina = bytes[kRecordStamina];
    record.magicPower = bytes[kRecordMagicPower];
    record.esper = esperOfByte(bytes[kRecordEsper]);
    ItemId* const equipment[] = {&record.weapon, &record.shield, &record.helmet,
                                 &record.armor,  &record.relic1, &record.relic2};
    for (std::size_t i = 0; i < std::size(equipment); ++i) {
        *equipment[i] = static_cast<ItemId>(bytes[kRecordEquipment + i]);
    }
    return read;
}

}  // namespace

void DamageOracle::writeRecordScenario(const RecordScenario& scenario) {
    if (scenario.recordIndex >= game::kCharacterRecords) {
        throw std::logic_error("a record scenario names one of the sixteen records");
    }
    if (static_cast<std::size_t>(scenario.actor) >= game::kSpellLearners) {
        throw std::logic_error("a record scenario's spells are one of the first twelve characters'");
    }
    const std::array<std::uint8_t, kRecordSize> bytes =
        recordBytes(scenario.record, scenario.hpBoostBits, scenario.mpBoostBits);
    writeBytes(retropp::MemoryRegion{.at = recordAt(scenario.recordIndex), .size = kRecordSize}, bytes);
    writeBytes(retropp::MemoryRegion{.at = spellListAt(scenario.actor),
                                         .size = static_cast<std::uint32_t>(game::kLearnableSpells)},
                   scenario.spells.progress);
}

LevelUpResult DamageOracle::readLevelUpResult(std::size_t recordIndex) {
    const std::uint32_t at = recordAt(recordIndex);
    const std::uint16_t maxHp = readWord(at + kRecordMaxHp);
    const std::uint16_t maxMp = readWord(at + kRecordMaxMp);
    return LevelUpResult{.level = readByte(at + kRecordLevel),
                         .maxHp = static_cast<std::uint16_t>(maxHp & kMaximumBits),
                         .maxMp = static_cast<std::uint16_t>(maxMp & kMaximumBits),
                         .hpBoostBits = static_cast<std::uint8_t>(maxHp >> kBoostShift),
                         .mpBoostBits = static_cast<std::uint8_t>(maxMp >> kBoostShift),
                         .vigor = readByte(at + kRecordVigor),
                         .speed = readByte(at + kRecordSpeed),
                         .stamina = readByte(at + kRecordStamina),
                         .magicPower = readByte(at + kRecordMagicPower)};
}

SpellLearningResult DamageOracle::readSpellLearningResult(CharacterPropId actor) {
    SpellLearningResult result;
    const std::vector<std::uint8_t> bytes = machine_.read(
        retropp::MemoryRegion{.at = spellListAt(actor), .size = static_cast<std::uint32_t>(game::kLearnableSpells)});
    std::copy(bytes.begin(), bytes.end(), result.progress.begin());
    result.progressed = (readByte(kWinFlags) & kMagiciteBit) != 0;
    return result;
}

SpellLearningResult DamageOracle::learnFromEsper(const RecordScenario& scenario) {
    if (!scenario.record.esper) {
        throw std::logic_error("the spells learned from an esper are called for a record with one");
    }
    writeRecordScenario(scenario);
    writeWord(kSpellListPlace, static_cast<std::uint16_t>(spellListAt(scenario.actor) & 0xFFFF));
    writeByte(kMagicPoints, scenario.magicPoints);
    writeByte(kPartyHasMagicite, kMagiciteBit);
    writeByte(kWinFlags, 0);
    call([&] {
        learnGenjuMagic_(esperByte(scenario.record.esper),
                         static_cast<std::uint16_t>(recordAt(scenario.recordIndex) - kCharacterRecords),
                         kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
    });
    trace_->returnToFrame(kLearnGenjuMagicRoutine);
    return readSpellLearningResult(scenario.actor);
}

LevelUpResult DamageOracle::levelUp(const RecordScenario& scenario) {
    if (scenario.record.level >= kPartyMaxLevel) {
        throw std::logic_error("the game never levels up a character at 99");
    }
    writeRecordScenario(scenario);
    call([&] {
        doLevelUp_(static_cast<std::uint16_t>(recordAt(scenario.recordIndex) - kCharacterRecords), kWideIndexStatus,
                   kBattleDataBank, kBattleDirectPage);
    });
    trace_->returnToFrame(kDoLevelUpRoutine);
    return readLevelUpResult(scenario.recordIndex);
}

LevelUpResult portLevelUp(const RecordScenario& scenario) {
    game::CharacterRecord record = scenario.record;
    battle::levelUp(record, GameVersion::US_1_1);
    return LevelUpResult{.level = record.level,
                         .maxHp = record.maxHp,
                         .maxMp = record.maxMp,
                         .hpBoostBits = scenario.hpBoostBits,
                         .mpBoostBits = scenario.mpBoostBits,
                         .vigor = record.vigor,
                         .speed = record.speed,
                         .stamina = record.stamina,
                         .magicPower = record.magicPower};
}

std::string differences(const LevelUpResult& port, const LevelUpResult& guest) {
    std::string out;
    addLine(out, "level", port.level, guest.level);
    addLine(out, "maximum HP", port.maxHp, guest.maxHp);
    addLine(out, "maximum MP", port.maxMp, guest.maxMp);
    addLine(out, "HP boost", port.hpBoostBits, guest.hpBoostBits);
    addLine(out, "MP boost", port.mpBoostBits, guest.mpBoostBits);
    addLine(out, "vigor", port.vigor, guest.vigor);
    addLine(out, "speed", port.speed, guest.speed);
    addLine(out, "stamina", port.stamina, guest.stamina);
    addLine(out, "magic power", port.magicPower, guest.magicPower);
    return out;
}

SpellLearningResult portLearnFromEsper(const RecordScenario& scenario) {
    SpellLearningResult result;
    game::SpellProgress spells = scenario.spells;
    result.progressed = battle::learnFromEsper(spells, *scenario.record.esper, scenario.magicPoints);
    result.progress = spells.progress;
    return result;
}

std::string differences(const SpellLearningResult& port, const SpellLearningResult& guest) {
    std::string out;
    for (std::size_t spell = 0; spell < port.progress.size(); ++spell) {
        const std::string field = "spell " + std::to_string(spell);
        addLine(out, field.c_str(), port.progress[spell], guest.progress[spell]);
    }
    addLine(out, "progressed", port.progressed, guest.progressed);
    return out;
}

RecordScenario drawLevelUpScenario(std::mt19937& rng) {
    RecordScenario s;
    s.recordIndex = rng() % game::kCharacterRecords;
    game::CharacterRecord& record = s.record;
    record.actor = static_cast<CharacterPropId>(s.recordIndex);
    record.level = static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 2));
    // Often within a level's largest gain of the cap, doubled by the largest bonus.
    record.maxHp = static_cast<std::uint16_t>(oneIn(rng, 3) ? kPartyMaxHp - upTo(rng, 400) : upTo(rng, kPartyMaxHp));
    record.maxMp = static_cast<std::uint16_t>(oneIn(rng, 3) ? kPartyMaxMp - upTo(rng, 30) : upTo(rng, kPartyMaxMp));
    record.hp = static_cast<std::uint16_t>(upTo(rng, record.maxHp));
    record.mp = static_cast<std::uint16_t>(upTo(rng, record.maxMp));
    record.experience = static_cast<std::uint32_t>(rng() & 0xFFFFFF);
    // Often near the limit, and now and then at the top of the byte, where the bonus comes round.
    for (std::uint8_t* stat : {&record.vigor, &record.speed, &record.stamina, &record.magicPower}) {
        *stat = static_cast<std::uint8_t>(oneIn(rng, 8)   ? 252 + upTo(rng, 3)
                                          : oneIn(rng, 3) ? 126 + upTo(rng, 2)
                                                          : upTo(rng, 128));
    }
    s.hpBoostBits = static_cast<std::uint8_t>(upTo(rng, 3));
    s.mpBoostBits = static_cast<std::uint8_t>(upTo(rng, 3));
    if (!oneIn(rng, 5)) {
        record.esper = static_cast<EsperId>(static_cast<std::uint8_t>(EsperId::RAMUH) + rng() % kEspers);
    }
    return s;
}

RecordScenario drawLearningScenario(std::mt19937& rng) {
    RecordScenario s;
    s.recordIndex = rng() % game::kSpellLearners;
    s.actor = static_cast<CharacterPropId>(s.recordIndex);
    s.record.actor = s.actor;
    s.record.esper = static_cast<EsperId>(static_cast<std::uint8_t>(EsperId::RAMUH) + rng() % kEspers);
    // A battle's 0 to 10, and one time in four any byte, so the product passes a byte.
    s.magicPoints = static_cast<std::uint8_t>(oneIn(rng, 4) ? byte(rng) : upTo(rng, 10));
    for (std::uint8_t& learned : s.spells.progress) {
        switch (rng() % 8) {
            case 0:
                learned = game::SpellProgress::kSpellJustLearned;
                break;
            case 1:
                learned = game::SpellProgress::kSpellLearned;
                break;
            case 2:
                learned = static_cast<std::uint8_t>(90 + upTo(rng, 9));
                break;
            default:
                learned = static_cast<std::uint8_t>(upTo(rng, 99));
                break;
        }
    }
    return s;
}

std::string describe(const RecordScenario& s) {
    const game::CharacterRecord& r = s.record;
    return "record " + std::to_string(s.recordIndex) + " actor " + std::to_string(static_cast<int>(s.actor)) +
           " level " + std::to_string(r.level) + " max hp " + std::to_string(r.maxHp) + " max mp " +
           std::to_string(r.maxMp) + " boosts " + std::to_string(s.hpBoostBits) + "/" + std::to_string(s.mpBoostBits) +
           " stats " + std::to_string(r.vigor) + "/" + std::to_string(r.speed) + "/" + std::to_string(r.stamina) + "/" +
           std::to_string(r.magicPower) + " esper " + std::to_string(esperByte(r.esper)) + " magic points " +
           std::to_string(s.magicPoints);
}

std::string DamageOracle::replayRecordCall(const TracedCall& call, const TracedRoutine& routine) {
    const auto input = [&](std::uint32_t cell) -> std::optional<std::uint8_t> {
        const auto found = call.inputs.find(cell);
        if (found == call.inputs.end()) {
            return std::nullopt;
        }
        return found->second;
    };
    const auto overlayWrites = [&] {
        for (const auto& [cell, value] : call.writes) {
            writeByte(cell, value);
        }
    };

    if (routine.entry == kDoLevelUp) {
        // The record is the one the call's record cells fall in.
        std::optional<std::size_t> index;
        for (const auto* cells : {&call.inputs, &call.writes}) {
            for (const auto& [cell, value] : *cells) {
                if (cell < kCharacterRecords || cell >= recordAt(game::kCharacterRecords)) {
                    continue;
                }
                const std::size_t in = (cell - kCharacterRecords) / kRecordSize;
                if (index && *index != in) {
                    throw std::runtime_error("DoLevelUp touched more than one character record");
                }
                index = in;
            }
        }
        if (!index) {
            throw std::runtime_error("DoLevelUp touched no character record");
        }
        const std::uint32_t at = recordAt(*index);
        const auto field = [&](std::size_t offset) { return input(at + static_cast<std::uint32_t>(offset)).value_or(0); };
        RecordScenario s;
        s.recordIndex = *index;
        s.record.level = field(kRecordLevel);
        if (s.record.level >= kPartyMaxLevel) {
            throw std::runtime_error("DoLevelUp was called at level " + std::to_string(s.record.level));
        }
        const auto word = [&](std::size_t offset) {
            return static_cast<std::uint16_t>(field(offset) | field(offset + 1) << 8);
        };
        s.record.maxHp = static_cast<std::uint16_t>(word(kRecordMaxHp) & kMaximumBits);
        s.hpBoostBits = static_cast<std::uint8_t>(word(kRecordMaxHp) >> kBoostShift);
        s.record.maxMp = static_cast<std::uint16_t>(word(kRecordMaxMp) & kMaximumBits);
        s.mpBoostBits = static_cast<std::uint8_t>(word(kRecordMaxMp) >> kBoostShift);
        s.record.vigor = field(kRecordVigor);
        s.record.speed = field(kRecordSpeed);
        s.record.stamina = field(kRecordStamina);
        s.record.magicPower = field(kRecordMagicPower);
        s.record.esper = esperOfByte(input(at + kRecordEsper).value_or(kNoEsper));
        const LevelUpResult port = portLevelUp(s);
        writeRecordScenario(s);
        overlayWrites();
        return differences(port, readLevelUpResult(*index));
    }

    // The spells learned from an esper: the spell cells read name the character and the spells. Each
    // spell the esper teaches is read once, whatever its percentage, so the spells read name the
    // esper — no two espers teach the same spells.
    std::optional<std::size_t> actor;
    std::set<std::size_t> taught;
    for (const auto& [cell, value] : call.inputs) {
        if (cell < kSpellsKnown || cell >= kSpellsKnown + kSpellsKnownSize) {
            continue;
        }
        const std::size_t place = cell - kSpellsKnown;
        const std::size_t of = place / game::kLearnableSpells;
        if (actor && *actor != of) {
            throw std::runtime_error("LearnGenjuMagic read more than one character's spells");
        }
        actor = of;
        taught.insert(place % game::kLearnableSpells);
    }
    if (!actor) {
        throw std::runtime_error("LearnGenjuMagic read no spell");
    }
    std::optional<EsperId> esper;
    for (const EsperPropertiesEntry& entry : kEsperProperties) {
        std::set<std::size_t> spells;
        for (const EsperSpell& slot : entry.record.spells) {
            if (slot.spell != AttackId::NONE) {
                spells.insert(static_cast<std::size_t>(slot.spell));
            }
        }
        if (spells == taught) {
            esper = entry.id;
        }
    }
    if (!esper) {
        throw std::runtime_error("LearnGenjuMagic read spells no esper teaches");
    }
    const std::optional<std::uint8_t> magicPoints = input(kMagicPoints);
    if (!magicPoints) {
        throw std::runtime_error("LearnGenjuMagic did not read the battle's magic points");
    }
    RecordScenario s;
    s.actor = static_cast<CharacterPropId>(*actor);
    s.recordIndex = *actor;
    s.record.actor = s.actor;
    s.record.esper = esper;
    s.magicPoints = *magicPoints;
    for (std::size_t spell = 0; spell < game::kLearnableSpells; ++spell) {
        s.spells.progress[spell] = input(spellListAt(s.actor) + static_cast<std::uint32_t>(spell)).value_or(0);
    }
    const SpellLearningResult port = portLearnFromEsper(s);
    writeRecordScenario(s);
    overlayWrites();
    SpellLearningResult guest = readSpellLearningResult(s.actor);
    // A spell stored ORs the magicite flag into the win's flags, which writes them whether the party
    // has magicite or not.
    guest.progressed = call.writes.contains(kWinFlags);
    return differences(port, guest);
}

// --- what a won battle gives ---------------------------------------------------------------------

namespace {

// The win's other direct-page cells: the message "gained a level" is told with, 0 once it has been.
constexpr std::uint32_t kLevelMessage = kWorkRam + 0x00F2;
// The win's flags, a bit each.
constexpr std::uint8_t kCurseDispelledBit = 0x01;
constexpr std::uint8_t kMagicPointsEarnedBit = 0x08;
constexpr std::uint8_t kCatHoodCountedBit = 0x10;
constexpr std::uint8_t kTechniqueToldBit = 0x40;
constexpr std::uint8_t kBlitzToldBit = 0x80;
// The four message variables, three bytes each.
constexpr std::uint32_t kMessageVariables = kWorkRam + 0x2F35;
constexpr std::uint32_t kMessageVariableSize = 3;
// The party's cells beyond the records and the spells known: each record's party byte, the Morph
// supply, the SwdTechs and Blitzes known, the rages known (32 bytes from here) and the dances known.
constexpr std::uint32_t kRecordPartyBytes = kWorkRam + 0x1850;
constexpr std::uint32_t kMorphSupply = kWorkRam + 0x1CF6;
constexpr std::uint32_t kSwdtechsKnown = kWorkRam + 0x1CF7;
constexpr std::uint32_t kBlitzesKnown = kWorkRam + 0x1D28;
constexpr std::uint32_t kRagesKnown = kWorkRam + 0x1D2C;
constexpr std::uint32_t kDancesKnown = kWorkRam + 0x1D4C;
constexpr std::uint32_t kGilSize = 3;
// A record's party byte: the party in bits 0-2, the slot in bits 3-4, the back row in bit 5, enabled
// in bit 6 and visible in bit 7.
constexpr std::uint8_t kPartyNumberMask = 0x07;
constexpr int kPartySlotPlace = 3;
constexpr std::uint8_t kPartySlotMask = 0x03;
constexpr std::uint8_t kPartyBackRowBit = 0x20;
constexpr std::uint8_t kPartyEnabledBit = 0x40;
constexpr std::uint8_t kPartyVisibleBit = 0x80;
// The battle switches' twenty variables.
constexpr std::uint32_t kSwitchVariables = kWorkRam + 0x3EB4;
// The value "gained a level" is armed with, and a monster slot's index with no monster in it.
constexpr std::uint8_t kLevelMessageArmed = battle::kMessageGainedALevel;
constexpr std::uint16_t kEmptyMonsterSlot = 0xFFFF;

// One cell of the party's image: where it is and what it holds.
struct PartyCell {
    std::uint32_t at = 0;
    std::uint8_t value = 0;
};

// Each bit `bits` holds, laid into bytes a bit per place: bit n is bit n % 8 of byte n / 8.
template <std::size_t N>
void putBits(std::vector<PartyCell>& out, std::uint32_t at, const std::bitset<N>& bits) {
    for (std::size_t byte = 0; byte < (N + 7) / 8; ++byte) {
        std::uint8_t value = 0;
        for (std::size_t bit = 0; bit < 8 && byte * 8 + bit < N; ++bit) {
            value = static_cast<std::uint8_t>(value | (bits.test(byte * 8 + bit) ? 1u << bit : 0u));
        }
        out.push_back({.at = at + static_cast<std::uint32_t>(byte), .value = value});
    }
}

// Every cell the party's image fills, in address order: the sixteen records, their party bytes, the
// gil, the spells the first twelve characters know, the Morph supply, the SwdTechs, the Blitzes, the
// lores, the rages and the dances known.
std::vector<PartyCell> partyImage(const PartyCells& cells) {
    const game::Party& party = cells.party;
    std::vector<PartyCell> out;
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        const std::array<std::uint8_t, kRecordSize> bytes =
            recordBytes(party.characters[record], cells.hpBoostBits[record], cells.mpBoostBits[record]);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            out.push_back({.at = recordAt(record) + static_cast<std::uint32_t>(i), .value = bytes[i]});
        }
    }
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        const game::PartyMembership& m = party.membership[record];
        out.push_back({.at = kRecordPartyBytes + static_cast<std::uint32_t>(record),
                       .value = static_cast<std::uint8_t>((m.party & kPartyNumberMask) |
                                                          (m.slot & kPartySlotMask) << kPartySlotPlace |
                                                          (m.backRow ? kPartyBackRowBit : 0) |
                                                          (m.enabled ? kPartyEnabledBit : 0) |
                                                          (m.visible ? kPartyVisibleBit : 0))});
    }
    for (std::uint32_t i = 0; i < kGilSize; ++i) {
        out.push_back({.at = kGil + i, .value = static_cast<std::uint8_t>(party.gil >> (8 * i) & 0xFF)});
    }
    for (std::size_t actor = 0; actor < game::kSpellLearners; ++actor) {
        for (std::size_t spell = 0; spell < game::kLearnableSpells; ++spell) {
            out.push_back({.at = spellListAt(static_cast<CharacterPropId>(actor)) + static_cast<std::uint32_t>(spell),
                           .value = party.spells[actor].progress[spell]});
        }
    }
    out.push_back({.at = kMorphSupply, .value = party.morphSupply});
    out.push_back({.at = kSwdtechsKnown, .value = party.swdtechsKnown.bits});
    out.push_back({.at = kBlitzesKnown, .value = party.blitzesKnown.bits});
    putBits(out, kLoresKnown, party.loresKnown);
    putBits(out, kRagesKnown, party.ragesKnown);
    out.push_back({.at = kDancesKnown, .value = static_cast<std::uint8_t>(party.dancesKnown.to_ulong())});
    return out;
}

// The name of the party's cell `at`, for a report.
std::string partyCellName(std::uint32_t at) {
    const auto hex = [](std::uint32_t value) {
        char text[8];
        std::snprintf(text, sizeof text, "%04X", value & 0xFFFF);
        return std::string{text};
    };
    if (at >= kCharacterRecords && at < recordAt(game::kCharacterRecords)) {
        const std::uint32_t offset = at - kCharacterRecords;
        return "record " + std::to_string(offset / kRecordSize) + " byte " + std::to_string(offset % kRecordSize);
    }
    if (at >= kSpellsKnown && at < kSpellsKnown + kSpellsKnownSize) {
        const std::uint32_t offset = at - kSpellsKnown;
        return "actor " + std::to_string(offset / game::kLearnableSpells) + " spell " +
               std::to_string(offset % game::kLearnableSpells);
    }
    return "$7E:" + hex(at);
}

std::uint8_t winFlagsByte(const WinFlags& flags) {
    return static_cast<std::uint8_t>((flags.curseDispelled ? kCurseDispelledBit : 0) |
                                     (flags.magicPointsEarned ? kMagicPointsEarnedBit : 0) |
                                     (flags.catHoodCounted ? kCatHoodCountedBit : 0) |
                                     (flags.techniqueMessageShown ? kTechniqueToldBit : 0) |
                                     (flags.blitzMessageShown ? kBlitzToldBit : 0));
}

// The message variables a message's text reads: none, a byte of one, or the whole of one.
enum class VariableRead : std::uint8_t { None, LowByte, Whole };
struct MessageVariables {
    std::uint8_t message;
    VariableRead first;
    VariableRead second;
};
// The win's messages, each with what its text reads: an item, an attack or a party slot is the low
// byte of its variable; a number the whole of it.
constexpr std::array<MessageVariables, 14> kMessageVariableReads{{
    {battle::kMessageBanonFell, VariableRead::None, VariableRead::None},
    {battle::kMessageGotItem, VariableRead::LowByte, VariableRead::None},
    {battle::kMessageGotItems, VariableRead::LowByte, VariableRead::Whole},
    {battle::kMessageGotGp, VariableRead::None, VariableRead::Whole},
    {battle::kMessageGotExperience, VariableRead::Whole, VariableRead::None},
    {battle::kMessageAnnihilated, VariableRead::None, VariableRead::None},
    {battle::kMessageCurseDispelled, VariableRead::None, VariableRead::None},
    {battle::kMessageLearnedLore, VariableRead::LowByte, VariableRead::None},
    {battle::kMessageGainedALevel, VariableRead::None, VariableRead::LowByte},
    {battle::kMessageLearnedSpell, VariableRead::LowByte, VariableRead::LowByte},
    {battle::kMessageDevisedABlitz, VariableRead::None, VariableRead::None},
    {battle::kMessageGotMagicPoints, VariableRead::Whole, VariableRead::None},
    {battle::kMessageMasteredADance, VariableRead::None, VariableRead::None},
    {battle::kMessageMasteredATechnique, VariableRead::None, VariableRead::None},
}};

std::optional<std::uint32_t> variableRead(const GraphicsCall& call, std::size_t variable, VariableRead read) {
    const std::size_t at = variable * kMessageVariableSize;
    switch (read) {
        case VariableRead::None:
            return std::nullopt;
        case VariableRead::LowByte:
            return call.messageVariables[at];
        case VariableRead::Whole:
            return static_cast<std::uint32_t>(call.messageVariables[at] | call.messageVariables[at + 1] << 8 |
                                              call.messageVariables[at + 2] << 16);
    }
    return std::nullopt;
}

std::string messageText(const MessageShown& shown) {
    std::string text = "message " + std::to_string(shown.message);
    if (shown.first) {
        text += " first " + std::to_string(*shown.first);
    }
    if (shown.second) {
        text += " second " + std::to_string(*shown.second);
    }
    return text;
}

RewardResult startingResult(const RewardScenario& scenario) {
    return RewardResult{.cells = scenario.cells,
                        .switches = scenario.switches,
                        .flags = scenario.flags,
                        .levelMessageToTell = scenario.levelMessageToTell};
}

// A battle holding `result`'s party, for the win's own steps to work on.
battle::BattleState battleHolding(const RewardResult& result) {
    battle::BattleState state;
    state.party = result.cells.party;
    state.version = GameVersion::US_1_1;
    return state;
}

// The win's two once-only messages as `result` keeps them.
battle::AbilityMessagesShown abilityMessagesOf(const RewardResult& result) {
    return {.swdtech = result.flags.techniqueMessageShown, .blitz = result.flags.blitzMessageShown};
}

// `state`'s party, the once-only messages and every message the steps told, put back into `result`.
void takeBack(RewardResult& result, const battle::BattleState& state, const battle::AbilityMessagesShown& shown) {
    result.cells.party = state.party;
    result.flags.techniqueMessageShown = shown.swdtech;
    result.flags.blitzMessageShown = shown.blitz;
    for (const battle::BattleRequest& request : state.requests) {
        const auto* script = std::get_if<battle::RunBattleScript>(&request);
        if (script == nullptr) {
            continue;
        }
        for (const battle::BattleScriptCommand& command : script->commands) {
            if (const auto* message = std::get_if<battle::BattleMessage>(&command)) {
                result.messages.push_back(
                    {.message = message->message, .first = message->firstNumber, .second = message->secondNumber});
            }
        }
    }
}

}  // namespace

std::optional<MessageShown> messageShown(const GraphicsCall& call) {
    if (call.command != kPlayBattleScript || call.script[0] != kShowMessageScriptCommand) {
        return std::nullopt;
    }
    const std::uint8_t message = call.script[1];
    for (const MessageVariables& reads : kMessageVariableReads) {
        if (reads.message == message) {
            return MessageShown{.message = message,
                                .first = variableRead(call, 0, reads.first),
                                .second = variableRead(call, 1, reads.second)};
        }
    }
    throw std::logic_error("no table entry says what message " + std::to_string(message) + "'s text reads");
}

void DamageOracle::writeParty(const PartyCells& cells) {
    for (const PartyCell& cell : partyImage(cells)) {
        writeByte(cell.at, cell.value);
    }
}

PartyCells DamageOracle::readParty() {
    PartyCells cells;
    game::Party& party = cells.party;
    const std::vector<std::uint8_t> records = machine_.read(
        retropp::MemoryRegion{.at = kCharacterRecords, .size = kRecordSize * static_cast<std::uint32_t>(game::kCharacterRecords)});
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        const RecordRead read = recordOf(std::span<const std::uint8_t>(records).subspan(record * kRecordSize, kRecordSize));
        party.characters[record] = read.record;
        cells.hpBoostBits[record] = read.hpBoostBits;
        cells.mpBoostBits[record] = read.mpBoostBits;
        const std::uint8_t value = readByte(kRecordPartyBytes + static_cast<std::uint32_t>(record));
        party.membership[record] =
            game::PartyMembership{.party = static_cast<std::uint8_t>(value & kPartyNumberMask),
                                  .slot = static_cast<std::uint8_t>(value >> kPartySlotPlace & kPartySlotMask),
                                  .backRow = (value & kPartyBackRowBit) != 0,
                                  .enabled = (value & kPartyEnabledBit) != 0,
                                  .visible = (value & kPartyVisibleBit) != 0};
    }
    party.gil = 0;
    for (std::uint32_t i = 0; i < kGilSize; ++i) {
        party.gil |= static_cast<std::uint32_t>(readByte(kGil + i)) << (8 * i);
    }
    const std::vector<std::uint8_t> spells =
        machine_.read(retropp::MemoryRegion{.at = kSpellsKnown, .size = kSpellsKnownSize});
    for (std::size_t actor = 0; actor < game::kSpellLearners; ++actor) {
        std::copy_n(spells.begin() + static_cast<std::ptrdiff_t>(actor * game::kLearnableSpells), game::kLearnableSpells,
                    party.spells[actor].progress.begin());
    }
    party.morphSupply = readByte(kMorphSupply);
    party.swdtechsKnown = AbilityLearnedSet{readByte(kSwdtechsKnown)};
    party.blitzesKnown = AbilityLearnedSet{readByte(kBlitzesKnown)};
    const auto readBits = [&](auto& bits, std::uint32_t at) {
        for (std::size_t n = 0; n < bits.size(); ++n) {
            bits.set(n, (readByte(at + static_cast<std::uint32_t>(n / 8)) >> (n % 8) & 1) != 0);
        }
    };
    readBits(party.loresKnown, kLoresKnown);
    readBits(party.ragesKnown, kRagesKnown);
    party.dancesKnown = std::bitset<8>(readByte(kDancesKnown));
    return cells;
}

void DamageOracle::writeRewardScenario(const RewardScenario& scenario) {
    if (scenario.recordIndex >= game::kCharacterRecords) {
        throw std::logic_error("a reward scenario names one of the sixteen records");
    }
    writeParty(scenario.cells);
    writeBytes(retropp::MemoryRegion{.at = kSwitchVariables, .size = game::kBattleVariables},
               scenario.switches.variables);
    writeByte(kWinFlags, winFlagsByte(scenario.flags));
    writeByte(kPartyHasMagicite, scenario.flags.partyHasMagicite ? kMagiciteBit : 0);
    writeByte(kLevelMessage, scenario.levelMessageToTell ? kLevelMessageArmed : 0);
    writeByte(kMagicPoints, scenario.magicPoints);
    // The message variables as the win leaves them for these steps: the share in the first, the
    // member's party slot in the second, the rest 0.
    std::array<std::uint8_t, 4 * kMessageVariableSize> variables{};
    for (std::size_t i = 0; i < kMessageVariableSize; ++i) {
        variables[i] = static_cast<std::uint8_t>(scenario.share >> (8 * i) & 0xFF);
    }
    variables[kMessageVariableSize] = scenario.partySlot;
    writeBytes(retropp::MemoryRegion{.at = kMessageVariables, .size = static_cast<std::uint32_t>(variables.size())},
               variables);
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        const std::optional<MonsterId>& monster = scenario.monsters[static_cast<std::size_t>(position)];
        writeWord(kMonsterIndex + slotIndex(battle::BattlerSlot::monster(position)),
                  monster ? static_cast<std::uint16_t>(*monster) : kEmptyMonsterSlot);
    }
}

RewardResult DamageOracle::readRewardResult() {
    RewardResult result;
    result.cells = readParty();
    const std::vector<std::uint8_t> switches =
        machine_.read(retropp::MemoryRegion{.at = kSwitchVariables, .size = game::kBattleVariables});
    std::copy(switches.begin(), switches.end(), result.switches.variables.begin());
    const std::uint8_t flags = readByte(kWinFlags);
    result.flags = WinFlags{.curseDispelled = (flags & kCurseDispelledBit) != 0,
                            .magicPointsEarned = (flags & kMagicPointsEarnedBit) != 0,
                            .catHoodCounted = (flags & kCatHoodCountedBit) != 0,
                            .techniqueMessageShown = (flags & kTechniqueToldBit) != 0,
                            .blitzMessageShown = (flags & kBlitzToldBit) != 0,
                            .partyHasMagicite = (readByte(kPartyHasMagicite) & kMagiciteBit) != 0};
    result.levelMessageToTell = readByte(kLevelMessage) != 0;
    for (const GraphicsCall& call : graphicsCalls_) {
        if (const std::optional<MessageShown> shown = messageShown(call)) {
            result.messages.push_back(*shown);
        } else {
            result.otherGraphicsCalls.push_back(call);
        }
    }
    return result;
}

RewardResult DamageOracle::callReward(const std::function<void()>& routine) {
    graphicsCalls_.clear();
    unansweredGraphics_.reset();
    machine_.escapes()[kGraphicsKey].armed(true);
    call(routine);
    machine_.escapes()[kGraphicsKey].armed(false);
    if (unansweredGraphics_) {
        throw std::runtime_error("the routine called graphics command " + std::to_string(*unansweredGraphics_) +
                                 ", which the oracle has no answer for");
    }
    return readRewardResult();
}

const std::vector<GraphicsCall>& DamageOracle::graphicsCommand(std::uint8_t command) {
    graphicsCalls_.clear();
    unansweredGraphics_.reset();
    machine_.escapes()[kGraphicsKey].armed(true);
    call([&] { execBtlGfx_(command, kWideIndexStatus, kBattleDataBank, kBattleDirectPage); });
    machine_.escapes()[kGraphicsKey].armed(false);
    return graphicsCalls_;
}

namespace {

// The win's steps among kTracedRoutines, closed by the oracle when it calls one itself.
const TracedRoutine& kTracedAddExp = kTracedRoutines[15];
const TracedRoutine& kTracedLearnItemMagic = kTracedRoutines[16];
const TracedRoutine& kTracedLearnAbilities = kTracedRoutines[17];
const TracedRoutine& kTracedLearnRage = kTracedRoutines[18];
static_assert(kTracedRoutines[15].entry == kAddExp && kTracedRoutines[16].entry == kLearnItemMagic &&
              kTracedRoutines[17].entry == kLearnAbilities && kTracedRoutines[18].entry == kLearnRage);

}  // namespace

RewardResult DamageOracle::addExperience(const RewardScenario& scenario) {
    writeRewardScenario(scenario);
    const auto record = static_cast<std::uint16_t>(recordAt(scenario.recordIndex) - kCharacterRecords);
    return callReward([&] {
        addExp_(record, kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
        trace_->returnToFrame(kTracedAddExp);
    });
}

RewardResult DamageOracle::learnFromEquipment(const RewardScenario& scenario) {
    const CharacterPropId actor = scenario.cells.party.characters[scenario.recordIndex].actor;
    if (static_cast<std::size_t>(actor) >= game::kSpellLearners) {
        throw std::logic_error("the spells equipment teaches are learned by the first twelve characters only");
    }
    writeRewardScenario(scenario);
    writeWord(kSpellListPlace, static_cast<std::uint16_t>(spellListAt(actor) & 0xFFFF));
    const auto record = static_cast<std::uint16_t>(recordAt(scenario.recordIndex) - kCharacterRecords);
    return callReward([&] {
        learnItemMagic_(record, kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
        trace_->returnToFrame(kTracedLearnItemMagic);
    });
}

RewardResult DamageOracle::levelUps(const RewardScenario& scenario) {
    const game::CharacterRecord& member = scenario.cells.party.characters[scenario.recordIndex];
    if (member.level == 0) {
        throw std::logic_error("a character's level is 1 or more");
    }
    writeRewardScenario(scenario);
    // The place the spells of the record's actor would be, as the win works it out for every actor.
    writeWord(kSpellListPlace, static_cast<std::uint16_t>(spellListAt(member.actor) & 0xFFFF));
    const auto record = static_cast<std::uint16_t>(recordAt(scenario.recordIndex) - kCharacterRecords);
    return callReward( [&] { checkLevelUp_(record, kWideIndexStatus, kBattleDataBank, kBattleDirectPage); });
}

RewardResult DamageOracle::learnAbilities(const RewardScenario& scenario) {
    writeRewardScenario(scenario);
    writeWord(kSpellListPlace, static_cast<std::uint16_t>(spellListAt(scenario.actor) & 0xFFFF));
    const auto actorAndLevel =
        static_cast<std::uint16_t>(static_cast<std::uint8_t>(scenario.actor) | scenario.level << 8);
    return callReward([&] {
        trace_->readForAbilities(static_cast<std::uint8_t>(scenario.actor), scenario.level);
        learnAbilities_(actorAndLevel, kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
        trace_->returnToFrame(kTracedLearnAbilities);
    });
}

RewardResult DamageOracle::learnRages(const RewardScenario& scenario) {
    writeRewardScenario(scenario);
    return callReward([&] {
        learnRage_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage);
        trace_->returnToFrame(kTracedLearnRage);
    });
}

RewardScenario DamageOracle::readRewardScenario() {
    RewardScenario s;
    s.cells = readParty();
    const std::vector<std::uint8_t> switches =
        machine_.read(retropp::MemoryRegion{.at = kSwitchVariables, .size = game::kBattleVariables});
    std::copy(switches.begin(), switches.end(), s.switches.variables.begin());
    const std::uint8_t flags = readByte(kWinFlags);
    s.flags = WinFlags{.curseDispelled = (flags & kCurseDispelledBit) != 0,
                       .magicPointsEarned = (flags & kMagicPointsEarnedBit) != 0,
                       .catHoodCounted = (flags & kCatHoodCountedBit) != 0,
                       .techniqueMessageShown = (flags & kTechniqueToldBit) != 0,
                       .blitzMessageShown = (flags & kBlitzToldBit) != 0,
                       .partyHasMagicite = (readByte(kPartyHasMagicite) & kMagiciteBit) != 0};
    s.levelMessageToTell = readByte(kLevelMessage) != 0;
    s.magicPoints = readByte(kMagicPoints);
    s.share = 0;
    for (std::uint32_t i = 0; i < kMessageVariableSize; ++i) {
        s.share |= static_cast<std::uint32_t>(readByte(kMessageVariables + i)) << (8 * i);
    }
    s.partySlot = readByte(kMessageVariables + kMessageVariableSize);
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        const std::uint16_t monster = readWord(kMonsterIndex + slotIndex(battle::BattlerSlot::monster(position)));
        s.monsters[static_cast<std::size_t>(position)] =
            monster == kEmptyMonsterSlot ? std::nullopt : std::optional<MonsterId>(static_cast<MonsterId>(monster));
    }
    return s;
}

namespace {

// The one character record whose cells `call` read or wrote, or nothing when it touched none.
std::optional<std::size_t> recordTouched(const TracedCall& call) {
    std::optional<std::size_t> index;
    for (const auto* cells : {&call.inputs, &call.writes}) {
        for (const auto& [cell, value] : *cells) {
            if (cell < kCharacterRecords || cell >= recordAt(game::kCharacterRecords)) {
                continue;
            }
            const std::size_t in = (cell - kCharacterRecords) / kRecordSize;
            if (index && *index != in) {
                throw std::runtime_error(call.routine + " touched more than one character record");
            }
            index = in;
        }
    }
    return index;
}

}  // namespace

std::string DamageOracle::replayRewardCall(const TracedCall& call, const TracedRoutine& routine) {
    // The step as a plain scenario, then every cell the routine read as it read it.
    RewardScenario plain;
    const bool onARecord = routine.entry == kAddExp || routine.entry == kLearnItemMagic;
    if (onARecord) {
        const std::optional<std::size_t> record = recordTouched(call);
        if (!record) {
            throw std::runtime_error(call.routine + " touched no character record");
        }
        plain.recordIndex = *record;
    }
    writeRewardScenario(plain);
    for (const auto& [cell, value] : call.inputs) {
        writeByte(cell, value);
    }
    RewardScenario s = readRewardScenario();
    s.recordIndex = plain.recordIndex;

    RewardResult port;
    if (routine.entry == kAddExp) {
        port = portAddExperience(s);
    } else if (routine.entry == kLearnItemMagic) {
        // The spells learned are the list the win's place for them names, whoever the record says it
        // is; a call that never read the place taught nothing, and the list it names does not matter.
        game::CharacterRecord& record = s.cells.party.characters[s.recordIndex];
        const CharacterPropId actor = record.actor;
        if (call.inputs.contains(kSpellListPlace)) {
            const std::uint32_t place = kWorkRam | readWord(kSpellListPlace);
            if (place < kSpellsKnown || (place - kSpellsKnown) % game::kLearnableSpells != 0 ||
                (place - kSpellsKnown) / game::kLearnableSpells >= game::kSpellLearners) {
                throw std::runtime_error("LearnItemMagic's spell list place names no character's spells");
            }
            record.actor = static_cast<CharacterPropId>((place - kSpellsKnown) / game::kLearnableSpells);
        } else if (static_cast<std::size_t>(record.actor) >= game::kSpellLearners) {
            record.actor = CharacterPropId::TERRA;
        }
        port = portLearnFromEquipment(s);
        port.cells.party.characters[s.recordIndex].actor = actor;
    } else if (routine.entry == kLearnAbilities) {
        if (!call.abilitiesActor || !call.abilitiesLevel) {
            throw std::runtime_error("LearnAbilities was entered with no actor and level read before it");
        }
        s.actor = static_cast<CharacterPropId>(*call.abilitiesActor);
        s.level = *call.abilitiesLevel;
        port = portLearnAbilities(s);
    } else {
        port = portLearnRages(s);
    }

    // The cartridge's side: the cells as the call left them, and the messages it showed.
    for (const auto& [cell, value] : call.writes) {
        writeByte(cell, value);
    }
    graphicsCalls_.clear();
    RewardResult guest = readRewardResult();
    for (const LoggedMessage& message : call.messages) {
        guest.messages.push_back(messageShownBy(message));
    }
    return differences(port, guest);
}

RewardResult portAddExperience(const RewardScenario& scenario) {
    RewardResult result = startingResult(scenario);
    battle::addExperience(result.cells.party.characters[scenario.recordIndex], scenario.share);
    return result;
}

RewardResult portLearnFromEquipment(const RewardScenario& scenario) {
    RewardResult result = startingResult(scenario);
    game::Party& party = result.cells.party;
    game::CharacterRecord& record = party.characters[scenario.recordIndex];
    const battle::EquipmentLearning learning = battle::learnFromEquipment(
        record, party.spells[static_cast<std::size_t>(record.actor)], result.switches, scenario.magicPoints);
    result.flags.curseDispelled |= learning.curseDispelled;
    result.flags.magicPointsEarned |= learning.progressed && result.flags.partyHasMagicite;
    return result;
}

RewardResult portLevelUps(const RewardScenario& scenario) {
    RewardResult result = startingResult(scenario);
    battle::BattleState state = battleHolding(result);
    battle::AbilityMessagesShown shown = abilityMessagesOf(result);
    result.levelMessageToTell = battle::reachEarnedLevels(state, state.party.characters[scenario.recordIndex],
                                                          scenario.partySlot, scenario.levelMessageToTell, shown);
    takeBack(result, state, shown);
    return result;
}

RewardResult portLearnAbilities(const RewardScenario& scenario) {
    RewardResult result = startingResult(scenario);
    battle::BattleState state = battleHolding(result);
    battle::AbilityMessagesShown shown = abilityMessagesOf(result);
    battle::learnLevelAbilities(state, scenario.actor, scenario.level, shown);
    takeBack(result, state, shown);
    return result;
}

RewardResult portLearnRages(const RewardScenario& scenario) {
    RewardResult result = startingResult(scenario);
    battle::BattleState state;
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        state.at(battle::BattlerSlot::monster(position)).monster = scenario.monsters[static_cast<std::size_t>(position)];
    }
    battle::learnRages(result.cells.party, state);
    return result;
}

std::string differences(const RewardResult& port, const RewardResult& guest) {
    std::string out;
    const std::vector<PartyCell> ours = partyImage(port.cells);
    const std::vector<PartyCell> theirs = partyImage(guest.cells);
    for (std::size_t i = 0; i < ours.size(); ++i) {
        addLine(out, partyCellName(ours[i].at).c_str(), ours[i].value, theirs[i].value);
    }
    for (std::size_t i = 0; i < port.switches.variables.size(); ++i) {
        const std::string field = "battle variable " + std::to_string(i);
        addLine(out, field.c_str(), port.switches.variables[i], guest.switches.variables[i]);
    }
    addLine(out, "curse dispelled", port.flags.curseDispelled, guest.flags.curseDispelled);
    addLine(out, "magic points earned", port.flags.magicPointsEarned, guest.flags.magicPointsEarned);
    addLine(out, "Cat Hood counted", port.flags.catHoodCounted, guest.flags.catHoodCounted);
    addLine(out, "technique told", port.flags.techniqueMessageShown, guest.flags.techniqueMessageShown);
    addLine(out, "blitz told", port.flags.blitzMessageShown, guest.flags.blitzMessageShown);
    addLine(out, "party has magicite", port.flags.partyHasMagicite, guest.flags.partyHasMagicite);
    addLine(out, "gained a level still to tell", port.levelMessageToTell, guest.levelMessageToTell);
    const std::size_t messages = std::max(port.messages.size(), guest.messages.size());
    for (std::size_t i = 0; i < messages; ++i) {
        const std::string ours = i < port.messages.size() ? messageText(port.messages[i]) : "none";
        const std::string theirs = i < guest.messages.size() ? messageText(guest.messages[i]) : "none";
        if (ours != theirs) {
            out += "message shown " + std::to_string(i) + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    }
    for (const GraphicsCall& call : guest.otherGraphicsCalls) {
        out += "the cartridge called graphics command " + std::to_string(call.command) + ", which the port does not\n";
    }
    return out;
}

namespace {

constexpr std::size_t kItemsBelowEmpty = 255;
constexpr std::size_t kActors = 16;
constexpr std::size_t kMonsterIds = 384;
constexpr std::uint32_t kLargestShare = 0xFFFFFF;

// A spell's learned percentage: below 100, just learned or learned.
std::uint8_t drawProgress(std::mt19937& rng) {
    switch (rng() % 6) {
        case 0:
            return game::SpellProgress::kSpellJustLearned;
        case 1:
            return game::SpellProgress::kSpellLearned;
        case 2:
            return static_cast<std::uint8_t>(90 + upTo(rng, 9));
        default:
            return static_cast<std::uint8_t>(upTo(rng, 99));
    }
}

ItemId drawItem(std::mt19937& rng) {
    return static_cast<ItemId>(rng() % kItemsBelowEmpty);
}

// The experience a character at `level` (1-98) needs for its next level.
std::uint32_t experienceForNextLevel(std::uint8_t level) {
    std::uint32_t steps = 0;
    for (int at = kFirstProgressionLevel; at <= level + 1; ++at) {
        steps += levelUpExp(static_cast<std::uint8_t>(at));
    }
    return steps * 8;
}

PartyCells drawPartyCells(std::mt19937& rng) {
    PartyCells cells;
    game::Party& party = cells.party;
    for (std::size_t r = 0; r < game::kCharacterRecords; ++r) {
        game::CharacterRecord& record = party.characters[r];
        record.actor = static_cast<CharacterPropId>(r);
        record.graphics = static_cast<CharacterGfxId>(rng() % kActors);
        for (std::uint8_t& letter : record.name) {
            letter = byte(rng);
        }
        record.level = static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1));
        record.maxHp = static_cast<std::uint16_t>(upTo(rng, kPartyMaxHp));
        record.hp = static_cast<std::uint16_t>(upTo(rng, record.maxHp));
        record.maxMp = static_cast<std::uint16_t>(upTo(rng, kPartyMaxMp));
        record.mp = static_cast<std::uint16_t>(upTo(rng, record.maxMp));
        record.experience = static_cast<std::uint32_t>(rng() % (kMaxExperience + 1));
        record.status.bytes[0] = sparse(rng);
        record.status.bytes[3] = sparse(rng);
        for (BattleCommandId& command : record.commands) {
            command = static_cast<BattleCommandId>(byte(rng));
        }
        record.vigor = static_cast<std::uint8_t>(upTo(rng, 128));
        record.speed = static_cast<std::uint8_t>(upTo(rng, 128));
        record.stamina = static_cast<std::uint8_t>(upTo(rng, 128));
        record.magicPower = static_cast<std::uint8_t>(upTo(rng, 128));
        if (!oneIn(rng, 4)) {
            record.esper = static_cast<EsperId>(static_cast<std::uint8_t>(EsperId::RAMUH) + rng() % kEspers);
        }
        ItemId* const equipment[] = {&record.weapon, &record.shield, &record.helmet,
                                     &record.armor,  &record.relic1, &record.relic2};
        for (ItemId* slot : equipment) {
            *slot = oneIn(rng, 3) ? ItemId::EMPTY : drawItem(rng);
        }
        cells.hpBoostBits[r] = static_cast<std::uint8_t>(upTo(rng, 3));
        cells.mpBoostBits[r] = static_cast<std::uint8_t>(upTo(rng, 3));
        party.membership[r] = game::PartyMembership{.party = static_cast<std::uint8_t>(upTo(rng, 3)),
                                                    .slot = static_cast<std::uint8_t>(upTo(rng, 3)),
                                                    .backRow = oneIn(rng, 2),
                                                    .enabled = oneIn(rng, 2),
                                                    .visible = oneIn(rng, 2)};
    }
    for (game::SpellProgress& spells : party.spells) {
        for (std::uint8_t& learned : spells.progress) {
            learned = drawProgress(rng);
        }
    }
    party.swdtechsKnown = AbilityLearnedSet{byte(rng)};
    party.blitzesKnown = AbilityLearnedSet{byte(rng)};
    for (std::size_t lore = 0; lore < game::kLores; ++lore) {
        party.loresKnown.set(lore, oneIn(rng, 2));
    }
    party.dancesKnown = std::bitset<8>(byte(rng));
    for (std::size_t rage = 0; rage < party.ragesKnown.size(); ++rage) {
        party.ragesKnown.set(rage, oneIn(rng, 2));
    }
    party.gil = static_cast<std::uint32_t>(rng() % (kMaxGil + 1));
    party.morphSupply = byte(rng);
    return cells;
}

WinFlags drawWinFlags(std::mt19937& rng) {
    return WinFlags{.curseDispelled = oneIn(rng, 2),
                    .magicPointsEarned = oneIn(rng, 2),
                    .catHoodCounted = oneIn(rng, 2),
                    .techniqueMessageShown = oneIn(rng, 2),
                    .blitzMessageShown = oneIn(rng, 2),
                    .partyHasMagicite = oneIn(rng, 2)};
}

RewardScenario drawRewardScenario(std::mt19937& rng) {
    RewardScenario s;
    s.cells = drawPartyCells(rng);
    for (std::uint8_t& variable : s.switches.variables) {
        variable = byte(rng);
    }
    s.recordIndex = rng() % game::kCharacterRecords;
    s.partySlot = static_cast<std::uint8_t>(upTo(rng, battle::kPartySlots - 1));
    s.magicPoints = oneIn(rng, 2) ? static_cast<std::uint8_t>(upTo(rng, 10)) : byte(rng);
    s.flags = drawWinFlags(rng);
    s.levelMessageToTell = oneIn(rng, 2);
    return s;
}

}  // namespace

RewardScenario drawExperienceScenario(std::mt19937& rng) {
    RewardScenario s = drawRewardScenario(rng);
    game::CharacterRecord& record = s.cells.party.characters[s.recordIndex];
    switch (rng() % 4) {
        case 0:
            record.experience = kMaxExperience - static_cast<std::uint32_t>(upTo(rng, 100'000));
            break;
        case 1:
            // Past the cap, and up to the top of the three bytes.
            record.experience = kMaxExperience + static_cast<std::uint32_t>(rng() % (kLargestShare - kMaxExperience + 1));
            break;
        default:
            break;
    }
    s.share = oneIn(rng, 3) ? static_cast<std::uint32_t>(rng() & kLargestShare)
                            : static_cast<std::uint32_t>(upTo(rng, 30'000));
    return s;
}

RewardScenario drawEquipmentScenario(std::mt19937& rng) {
    RewardScenario s = drawRewardScenario(rng);
    game::CharacterRecord& record = s.cells.party.characters[s.recordIndex];
    record.actor = static_cast<CharacterPropId>(rng() % game::kSpellLearners);
    ItemId* const equipment[] = {&record.weapon, &record.shield, &record.helmet,
                                 &record.armor,  &record.relic1, &record.relic2};
    for (ItemId* slot : equipment) {
        switch (rng() % 4) {
            case 0:
                *slot = ItemId::EMPTY;
                break;
            case 1:
                *slot = ItemId::CURSED_SHLD;
                break;
            default:
                *slot = drawItem(rng);
                break;
        }
    }
    s.switches.setCursedShieldBattles(oneIn(rng, 2) ? static_cast<std::uint8_t>(254 + upTo(rng, 1)) : byte(rng));
    return s;
}

RewardScenario drawLevelScenario(std::mt19937& rng) {
    RewardScenario s = drawRewardScenario(rng);
    game::CharacterRecord& record = s.cells.party.characters[s.recordIndex];
    record.actor = static_cast<CharacterPropId>(rng() % kActors);
    record.level = static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1));
    if (record.level < kPartyMaxLevel) {
        if (oneIn(rng, 4)) {
            // Just short of the next level.
            const std::uint32_t need = experienceForNextLevel(record.level);
            record.experience = need - 1 - std::min<std::uint32_t>(need - 1, static_cast<std::uint32_t>(upTo(rng, 100)));
        } else {
            // At the next level or up to five past it.
            const int reached = std::min<int>(record.level + upTo(rng, 5), kPartyMaxLevel - 1);
            record.experience = experienceForNextLevel(static_cast<std::uint8_t>(reached)) +
                                static_cast<std::uint32_t>(upTo(rng, 1000));
        }
    }
    return s;
}

RewardScenario drawRageScenario(std::mt19937& rng) {
    RewardScenario s = drawRewardScenario(rng);
    for (std::optional<MonsterId>& monster : s.monsters) {
        switch (rng() % 4) {
            case 0:
                monster.reset();
                break;
            case 1:
                monster = static_cast<MonsterId>(256 + rng() % (kMonsterIds - 256));
                break;
            default:
                monster = static_cast<MonsterId>(rng() % 256);
                break;
        }
    }
    return s;
}

RewardScenario abilityCase(CharacterPropId actor, std::uint8_t level, int mix, std::mt19937& rng) {
    RewardScenario s = drawRewardScenario(rng);
    s.actor = actor;
    s.level = level;
    if (static_cast<std::size_t>(actor) < game::kSpellLearners) {
        for (std::uint8_t& learned : s.cells.party.spells[static_cast<std::size_t>(actor)].progress) {
            learned = mix == 0   ? static_cast<std::uint8_t>(upTo(rng, 99))
                      : mix == 1 ? game::SpellProgress::kSpellJustLearned
                                 : game::SpellProgress::kSpellLearned;
        }
    }
    return s;
}

std::string describe(const RewardScenario& s) {
    const game::CharacterRecord& r = s.cells.party.characters[s.recordIndex];
    std::string text = "record " + std::to_string(s.recordIndex) + " actor " + std::to_string(static_cast<int>(r.actor)) +
                       " level " + std::to_string(r.level) + " experience " + std::to_string(r.experience) +
                       " share " + std::to_string(s.share) + " magic points " + std::to_string(s.magicPoints) +
                       " flags " + std::to_string(winFlagsByte(s.flags)) + " magicite " +
                       std::to_string(s.flags.partyHasMagicite) + " level message " +
                       std::to_string(s.levelMessageToTell) + " cursed-shield battles " +
                       std::to_string(s.switches.cursedShieldBattles()) + " equipment";
    for (const ItemId item : r.equipment()) {
        text += " " + std::to_string(static_cast<int>(item));
    }
    text += " ability check " + std::to_string(static_cast<int>(s.actor)) + "@" + std::to_string(s.level) + " monsters";
    for (const std::optional<MonsterId>& monster : s.monsters) {
        text += " " + (monster ? std::to_string(static_cast<int>(*monster)) : std::string{"-"});
    }
    return text;
}

// --- a battle's end --------------------------------------------------------------------------------

namespace {

// The cells a battle's end reads and writes: the morphed party member (twice its slot's index, none
// negative) and Morph's meter (a word whose high byte is the meter); per party slot at twice the
// slot, the Morph meter shown and the party member's own bit, the word an item's equippable bits
// are tested against; the battle's flags (bit 7: no battle menu may open); the inventory, 256
// records; and the graphics program's list of items obtained, sixteen records, with its two counts.
constexpr std::uint32_t kMorphedPartyMember = kWorkRam + 0x3EE2;
constexpr std::uint32_t kMorphMeter = kWorkRam + 0x3F30;
constexpr std::uint32_t kMorphMeterShown = kWorkRam + 0x3B04;
constexpr std::uint32_t kPartyMemberBit = kWorkRam + 0x3A20;
constexpr std::uint32_t kEndingFlags = kWorkRam + 0x00B1;
constexpr std::uint8_t kMenusClosedBit = 0x80;
constexpr std::uint32_t kInventoryRecords = kWorkRam + 0x2686;
constexpr std::uint32_t kItemsObtainedRecords = kWorkRam + 0x602D;
constexpr std::uint32_t kItemsAdded = kWorkRam + 0x64DA;
constexpr std::uint32_t kItemsMerged = kWorkRam + 0x64DB;
// The record the battle program hands an item to the graphics program in.
constexpr std::uint32_t kItemHandedOver = kWorkRam + 0x2E72;
// The graphics commands an ending makes beyond the two that keep the items obtained.
constexpr std::uint8_t kWaitForFrame = 0x01;
constexpr std::uint8_t kCloseMenu = 0x03;
// The four equipment-changed flags, one per party slot.
constexpr std::uint32_t kEquipmentChangedFlags = 4;
// The party slots' bits of the byte marking who holds an item.
constexpr std::uint8_t kHoldingItemBits = 0x0F;
// Per party slot at twice the slot: the party member's actor and record (a byte each, none
// negative) and its record's offset from $7E:1600 (a word). Per monster slot at twice the slot, the
// monster's experience and gold (words). Per battler at twice its index, the battlers alive (a word,
// the party's byte first) and each actor's battler, one byte per actor (none negative).
constexpr std::uint32_t kPartyActor = kWorkRam + 0x3ED8;
constexpr std::uint32_t kPartyRecord = kWorkRam + 0x3ED9;
constexpr std::uint32_t kRecordOffset = kWorkRam + 0x3010;
constexpr std::uint32_t kMonsterExperience = kWorkRam + 0x3D8C;
constexpr std::uint32_t kMonsterGold = kWorkRam + 0x3DA0;
constexpr std::uint32_t kBattlersAlive = kWorkRam + 0x3A74;
constexpr std::uint32_t kEnemiesAlive = kWorkRam + 0x3A77;
constexpr std::uint32_t kActorsBattler = kWorkRam + 0x3000;
constexpr std::size_t kActorsWithABattler = 16;
// The battle's: the formation (a word); the colosseum (nonzero), the wager and the prize; the battle's number (a word); the
// field's flags (bit 1 the Veldt, bit 3 the field's music playing on, a word) and the background (a
// word); the formation's four bytes; Doom Gaze's battler; the battles Gau can find on the Veldt, a
// bit each; the message variables.
constexpr std::uint32_t kFormationCell = kWorkRam + 0x11E0;
constexpr std::uint32_t kColosseumCell = kWorkRam + 0x3A97;
constexpr std::uint8_t kInTheColosseum = 0xFF;
constexpr std::uint32_t kWagerCell = kWorkRam + 0x0205;
constexpr std::uint32_t kPrizeCell = kWorkRam + 0x0207;
constexpr std::uint32_t kBattleNumber = kWorkRam + 0x3ED4;
constexpr std::uint32_t kFieldFlags = kWorkRam + 0x11E4;
constexpr std::uint8_t kOnTheVeldtBit = 0x02;
constexpr std::uint32_t kBackgroundCell = kWorkRam + 0x11E2;
constexpr std::uint32_t kFormationBytes = kWorkRam + 0x2F48;
constexpr std::uint32_t kDoomGazeBattler = kWorkRam + 0x33FA;
constexpr std::uint32_t kVeldtBattles = kWorkRam + 0x1DDD;
constexpr std::uint32_t kVeldtBattleBytes = 64;
constexpr std::uint32_t kEndingMessageVariables = kWorkRam + 0x2F35;
constexpr std::uint32_t kMessageVariableBytes = 12;
// What the game keeps: the party's inventory, the items' numbers then their quantities, and the battle
// variables.
constexpr std::uint32_t kSavedItems = kWorkRam + 0x1869;
constexpr std::uint32_t kSavedQuantities = kWorkRam + 0x1969;
constexpr std::uint32_t kSavedVariables = kWorkRam + 0x1DC9;
constexpr std::uint32_t kRecordBytes = battle::kItemRecordBytes;
// The list's last three places, where the title program leaves them.
constexpr std::uint32_t kItemsObtainedTailRecords =
    kItemsObtainedRecords + static_cast<std::uint32_t>(battle::kItemsObtainedEmptiedPlaces) * kRecordBytes;

template <std::size_t N>
std::vector<std::uint8_t> recordsOf(const std::array<battle::BattleItem, N>& items) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(N * kRecordBytes);
    for (const battle::BattleItem& item : items) {
        const battle::ItemRecord record = battle::recordOf(item);
        bytes.insert(bytes.end(), record.begin(), record.end());
    }
    return bytes;
}

template <std::size_t N>
std::array<battle::BattleItem, N> itemsOf(std::span<const std::uint8_t> bytes) {
    std::array<battle::BattleItem, N> items{};
    for (std::size_t i = 0; i < N; ++i) {
        items[i] = battle::battleItemFromRecord(bytes.subspan(i * kRecordBytes).first<battle::kItemRecordBytes>());
    }
    return items;
}

// A record as the comparison reads it: an empty one by its item alone.
std::string recordText(const battle::BattleItem& item) {
    if (item.item == ItemId::EMPTY) {
        return "empty";
    }
    const battle::ItemRecord record = battle::recordOf(item);
    std::string text;
    for (const std::uint8_t b : record) {
        char hex[4];
        std::snprintf(hex, sizeof hex, "%02X ", b);
        text += hex;
    }
    text.pop_back();
    return text;
}

void addRecordLine(std::string& out, const std::string& field, const battle::BattleItem& port,
                   const battle::BattleItem& guest) {
    const std::string ours = recordText(port);
    const std::string theirs = recordText(guest);
    if (ours != theirs) {
        out += field + ": port " + ours + ", cartridge " + theirs + "\n";
    }
}

void addListLines(std::string& out, const battle::ItemsObtained& port, const battle::ItemsObtained& guest) {
    for (std::size_t place = 0; place < port.entries.size(); ++place) {
        addRecordLine(out, "items obtained, place " + std::to_string(place), port.entries[place], guest.entries[place]);
    }
    addLine(out, "items added", port.added, guest.added);
    addLine(out, "items merged", port.merged, guest.merged);
}

template <std::size_t N>
void addInventoryLines(std::string& out, const std::array<battle::BattleItem, N>& port,
                       const std::array<battle::BattleItem, N>& guest) {
    for (std::size_t slot = 0; slot < N; ++slot) {
        addRecordLine(out, "inventory slot " + std::to_string(slot), port[slot], guest[slot]);
    }
}

std::string callText(const EndingCall& call) {
    switch (call.kind) {
        case EndingCall::Kind::CloseMenu:
            return "close menu " + std::to_string(call.slot);
        case EndingCall::Kind::Wait:
            return "wait " + std::to_string(call.frames);
        case EndingCall::Kind::ItemObtained:
            return "item obtained " + std::to_string(static_cast<int>(call.item));
        case EndingCall::Kind::Message:
            return messageText(call.message);
        case EndingCall::Kind::StepBack:
            return "step back";
        case EndingCall::Kind::Celebration:
            return std::string{"celebration"} + (call.fanfare ? ", fanfare" : "") + (call.victoryEvent ? ", event 1" : "");
        case EndingCall::Kind::Other:
            return "other " + std::to_string(call.command);
    }
    return "?";
}

bool isVictoryEvent(const battle::BattleRequest& request) {
    const auto* script = std::get_if<battle::RunBattleScript>(&request);
    if (script == nullptr || script->commands.size() != 1) {
        return false;
    }
    const auto* event = std::get_if<battle::BattleEventPlays>(&script->commands[0]);
    return event != nullptr && event->event == battle::kVictoryEvent;
}

// What the port's requests show, as the cartridge's calls would. In a win, the four menus closing
// after the step back, with the fanfare and battle event 1 after them where they were asked for, are
// the celebration.
std::vector<EndingCall> portCalls(std::span<const battle::BattleRequest> requests, bool win) {
    std::vector<EndingCall> calls;
    bool celebrationNext = false;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const battle::BattleRequest& request = requests[i];
        if (celebrationNext && std::holds_alternative<battle::CloseBattleMenu>(request) &&
            i + battle::kPartySlots <= requests.size()) {
            celebrationNext = false;
            EndingCall celebration{.kind = EndingCall::Kind::Celebration};
            i += battle::kPartySlots;
            if (i < requests.size() && std::holds_alternative<battle::VictoryFanfare>(requests[i])) {
                celebration.fanfare = true;
                ++i;
            }
            if (i < requests.size() && isVictoryEvent(requests[i])) {
                celebration.victoryEvent = true;
                ++i;
            }
            --i;
            calls.push_back(celebration);
            continue;
        }
        if (const auto* close = std::get_if<battle::CloseBattleMenu>(&request)) {
            calls.push_back(EndingCall{.kind = EndingCall::Kind::CloseMenu,
                                       .slot = static_cast<std::uint8_t>(close->character.position())});
        } else if (const auto* obtained = std::get_if<battle::AddItemObtained>(&request)) {
            calls.push_back(EndingCall{.kind = EndingCall::Kind::ItemObtained, .item = obtained->item});
        } else if (const auto* script = std::get_if<battle::RunBattleScript>(&request)) {
            for (const battle::BattleScriptCommand& command : script->commands) {
                if (const auto* wait = std::get_if<battle::Wait>(&command)) {
                    calls.push_back(EndingCall{.kind = EndingCall::Kind::Wait, .frames = wait->frames});
                } else if (const auto* message = std::get_if<battle::BattleMessage>(&command)) {
                    calls.push_back(EndingCall{.kind = EndingCall::Kind::Message,
                                               .message = MessageShown{.message = message->message,
                                                                       .first = message->firstNumber,
                                                                       .second = message->secondNumber}});
                } else if (std::holds_alternative<battle::StepBack>(command)) {
                    calls.push_back(EndingCall{.kind = EndingCall::Kind::StepBack});
                    celebrationNext = win;
                } else {
                    calls.push_back(EndingCall{});
                }
            }
        } else {
            calls.push_back(EndingCall{});
        }
    }
    return calls;
}

// The endings among kSnapshotRoutines, closed by the oracle when it calls one itself.
const SnapshotRoutine& kSnapshotWin = kSnapshotRoutines[0];
const SnapshotRoutine& kSnapshotLoss = kSnapshotRoutines[1];
const SnapshotRoutine& kSnapshotWrapUp = kSnapshotRoutines[2];
static_assert(kSnapshotRoutines[0].entry == kWinBattle && kSnapshotRoutines[1].entry == kLoseBattle &&
              kSnapshotRoutines[2].entry == kWrapUp);

// The graphics command that celebrates a win, and the battle script command a step back is.
constexpr std::uint8_t kVictoryCommand = 0x08;
constexpr std::uint8_t kStepBackScriptCommand = 0x0D;
// The formation's flags bit that turns the fanfare off, and the field's flag that keeps its music
// playing (BtlGfx_08, btlgfx_main.asm:248-261).
constexpr std::uint8_t kFanfareOffBit = 0x02;
constexpr std::uint8_t kFieldMusicPlaysOnBit = 0x08;

// What the cartridge's calls to the graphics program show: frames waited one at a time run together
// into one wait; the hands put away are the items added between the wrap-up's wait and its merge,
// and an item added anywhere else (a drop, the prize) is merged at once and shows only its message;
// a merge shows nothing. The celebration is read with the formation's flags and the field's flags as
// the call found them.
std::vector<EndingCall> guestCalls(std::span<const GraphicsCall> calls, std::uint8_t formationFlags,
                                   std::uint8_t fieldFlags) {
    std::vector<EndingCall> out;
    bool puttingAway = false;
    for (const GraphicsCall& call : calls) {
        switch (call.command) {
            case kWaitForFrame:
                if (!out.empty() && out.back().kind == EndingCall::Kind::Wait) {
                    ++out.back().frames;
                } else {
                    out.push_back(EndingCall{.kind = EndingCall::Kind::Wait, .frames = 1});
                }
                puttingAway = true;
                break;
            case kCloseMenu:
                out.push_back(EndingCall{.kind = EndingCall::Kind::CloseMenu, .slot = call.slot});
                break;
            case kAddItemObtained:
                if (puttingAway) {
                    out.push_back(
                        EndingCall{.kind = EndingCall::Kind::ItemObtained, .item = static_cast<ItemId>(call.item[0])});
                }
                break;
            case kMergeItemsObtained:
                puttingAway = false;
                break;
            case kVictoryCommand: {
                const bool on = (formationFlags & kFanfareOffBit) == 0;
                out.push_back(EndingCall{.kind = EndingCall::Kind::Celebration,
                                         .fanfare = on && (fieldFlags & kFieldMusicPlaysOnBit) == 0,
                                         .victoryEvent = on});
                break;
            }
            case kPlayBattleScript:
                if (call.script[0] == kStepBackScriptCommand) {
                    out.push_back(EndingCall{.kind = EndingCall::Kind::StepBack});
                } else if (const std::optional<MessageShown> message = messageShown(call)) {
                    out.push_back(EndingCall{.kind = EndingCall::Kind::Message, .message = *message});
                } else {
                    out.push_back(EndingCall{.command = call.command});
                }
                break;
            default:
                out.push_back(EndingCall{.command = call.command});
                break;
        }
    }
    return out;
}

// The port's battle for an ending: the scenario's battle with the scenario's party.
battle::BattleState portState(const EndingScenario& scenario) {
    battle::BattleState state = scenario.state;
    state.party = scenario.cells.party;
    state.requests.clear();
    return state;
}

EndingResult portEndingResult(const EndingScenario& scenario, const battle::BattleState& state, bool win) {
    EndingResult result;
    result.cells = scenario.cells;
    result.cells.party = state.party;
    result.switches = state.switches;
    result.rngPosition = state.rng.position();
    result.inventory = state.inventory.items;
    result.itemsObtained = state.itemsObtained;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::Battler& member = state.at(battle::BattlerSlot::character(position));
        result.hands[static_cast<std::size_t>(position)] = member.itemInHand;
        result.morphGauges[static_cast<std::size_t>(position)] = member.morphGauge;
    }
    result.holdingItem = state.holdingItem.characters.bits;
    result.menusDisabled = state.menusDisabled;
    result.calls = portCalls(state.requests, win);
    return result;
}

}  // namespace

void DamageOracle::writeItemsObtained(const battle::ItemsObtained& list) {
    const std::vector<std::uint8_t> bytes = recordsOf(list.entries);
    writeBytes(retropp::MemoryRegion{.at = kItemsObtainedRecords, .size = static_cast<std::uint32_t>(bytes.size())},
               bytes);
    writeByte(kItemsAdded, list.added);
    writeByte(kItemsMerged, list.merged);
}

battle::ItemsObtained DamageOracle::readItemsObtained() {
    const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{
        .at = kItemsObtainedRecords, .size = static_cast<std::uint32_t>(battle::kItemsObtainedPlaces * kRecordBytes)});
    return battle::ItemsObtained{.entries = itemsOf<battle::kItemsObtainedPlaces>(bytes),
                                 .added = readByte(kItemsAdded),
                                 .merged = readByte(kItemsMerged)};
}

void DamageOracle::writeInventory(const std::array<battle::BattleItem, game::kInventorySlots>& inventory) {
    const std::vector<std::uint8_t> bytes = recordsOf(inventory);
    writeBytes(retropp::MemoryRegion{.at = kInventoryRecords, .size = static_cast<std::uint32_t>(bytes.size())}, bytes);
}

std::array<battle::BattleItem, game::kInventorySlots> DamageOracle::readInventory() {
    const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{
        .at = kInventoryRecords, .size = static_cast<std::uint32_t>(game::kInventorySlots * kRecordBytes)});
    return itemsOf<game::kInventorySlots>(bytes);
}

ItemsObtainedResult DamageOracle::callItemsObtained(const ItemsObtainedScenario& scenario, std::uint8_t command) {
    writeItemsObtained(scenario.list);
    writeInventory(scenario.inventory);
    const battle::BattleState noParty;
    const battle::ItemRecord record = battle::recordOf(battle::battleItem(noParty, scenario.item, scenario.quantity));
    writeBytes(retropp::MemoryRegion{.at = kItemHandedOver, .size = kRecordBytes}, record);
    graphicsCommand(command);
    return ItemsObtainedResult{.list = readItemsObtained(), .inventory = readInventory()};
}

ItemsObtainedResult DamageOracle::addItemObtained(const ItemsObtainedScenario& scenario) {
    return callItemsObtained(scenario, kAddItemObtained);
}

ItemsObtainedResult DamageOracle::mergeItemsObtained(const ItemsObtainedScenario& scenario) {
    return callItemsObtained(scenario, kMergeItemsObtained);
}

battle::ItemsObtainedTail DamageOracle::itemsObtainedTailAtPowerOn() {
    call([&] { decompCutsceneProg_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{
        .at = kItemsObtainedTailRecords,
        .size = static_cast<std::uint32_t>(battle::kItemsObtainedTailPlaces * kRecordBytes)});
    return itemsOf<battle::kItemsObtainedTailPlaces>(bytes);
}

void DamageOracle::writeEndingScenario(const EndingScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    writeParty(scenario.cells);
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::BattlerSlot slot = battle::BattlerSlot::character(position);
        const battle::Battler& member = state.at(slot);
        const std::uint32_t at = slotIndex(slot);
        writeWord(kBattlerBit + at, maskOf(battle::BattlerSet::of(slot)));
        writeWord(kPartyMemberBit + at, member.equipsAs.bits());
        writeByte(kItemInHand + at, member.itemInHand ? static_cast<std::uint8_t>(*member.itemInHand) : kNoOne);
        writeByte(kMorphMeterShown + at, member.morphGauge);
    }
    writeByte(kHoldingItem, state.holdingItem.characters.bits);
    writeByte(kMorphedPartyMember, state.morphedBattler ? static_cast<std::uint8_t>(slotIndex(*state.morphedBattler)) : kNoOne);
    writeWord(kMorphMeter, state.morphCounter);
    writeByte(kEndingFlags, state.menusDisabled ? kMenusClosedBit : 0);
    const std::array<std::uint8_t, kEquipmentChangedFlags> unchanged{};
    writeBytes(retropp::MemoryRegion{.at = kEquipmentChanged, .size = kEquipmentChangedFlags}, unchanged);
    writeInventory(state.inventory.items);
    writeItemsObtained(state.itemsObtained);

    // The rest of the battle as the win, the loss and the write-back read it.
    std::array<std::uint8_t, kActorsWithABattler> actorsBattler{};
    actorsBattler.fill(kNoOne);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::uint32_t at = slotIndex(slot);
        writeWord(kBattlerBit + at, maskOf(battle::BattlerSet::of(slot)));
        writeByte(kStatus12 + at, battler.status.bytes[0]);
        writeByte(kStatus12 + at + 1, battler.status.bytes[1]);
        writeByte(kStatus34 + at, battler.status.bytes[2]);
        writeByte(kStatus34 + at + 1, battler.status.bytes[3]);
        writeByte(kRelicEffects4And5 + at, battler.relicEffects4.bits);
        writeByte(kRelicEffects4And5 + at + 1, battler.relicEffects5.bits);
        writeWord(kHp + at, battler.hp);
        writeWord(kMp + at, battler.mp);
        writeWord(kMaxMp + at, battler.maxMp);
        if (slot.isCharacter()) {
            writeByte(kPartyActor + at, battler.actor ? static_cast<std::uint8_t>(*battler.actor) : kNoOne);
            writeByte(kPartyRecord + at, battler.record ? static_cast<std::uint8_t>(*battler.record) : kNoOne);
            writeWord(kRecordOffset + at,
                      static_cast<std::uint16_t>(recordAt(battler.record.value_or(0)) - kCharacterRecords));
            if (battler.actor && static_cast<std::size_t>(*battler.actor) < kActorsWithABattler) {
                actorsBattler[static_cast<std::size_t>(*battler.actor)] = static_cast<std::uint8_t>(at);
            }
        } else {
            const auto monster = static_cast<std::uint32_t>(slot.position() * 2);
            writeWord(kMonsterIndex + at,
                      battler.monster ? static_cast<std::uint16_t>(*battler.monster) : kEmptyMonsterSlot);
            writeWord(kMonsterExperience + monster, battler.experience);
            writeWord(kMonsterGold + monster, battler.gold);
        }
    }
    writeBytes(retropp::MemoryRegion{.at = kActorsBattler, .size = kActorsWithABattler}, actorsBattler);
    writeWord(kBattlersAlive, maskOf(state.alive));
    writeByte(kAllyCount, static_cast<std::uint8_t>(state.allyCount));
    writeByte(kEnemiesAlive, static_cast<std::uint8_t>(state.enemyCount));
    std::array<std::uint8_t, 3> lores{};
    for (std::size_t lore = 0; lore < state.loresLearned.size(); ++lore) {
        if (state.loresLearned.test(lore)) {
            lores[lore / 8] = static_cast<std::uint8_t>(lores[lore / 8] | (1u << (lore % 8)));
        }
    }
    writeBytes(retropp::MemoryRegion{.at = kLoresLearned, .size = 3}, lores);
    writeByte(kColosseumCell, state.colosseum ? kInTheColosseum : 0);
    writeByte(kWagerCell, static_cast<std::uint8_t>(state.colosseumWager.value_or(ItemId::EMPTY)));
    writeByte(kPrizeCell, static_cast<std::uint8_t>(state.colosseumPrize.value_or(ItemId::EMPTY)));
    writeWord(kBattleNumber, static_cast<std::uint16_t>(state.startingFormation));
    writeWord(kFormationCell, static_cast<std::uint16_t>(state.formation));
    writeWord(kFieldFlags, static_cast<std::uint16_t>((state.onTheVeldt ? kOnTheVeldtBit : 0) |
                                                      (state.keepFieldMusic ? kFieldMusicPlaysOnBit : 0)));
    writeWord(kBackgroundCell, static_cast<std::uint16_t>(state.background));
    const FormationAux& aux = getFormationAux(state.formation);
    const std::array<std::uint8_t, 4> formation{
        aux.entranceAndTypes, static_cast<std::uint8_t>(aux.flags | (state.noVictoryFanfare ? kFanfareOffBit : 0)),
        aux.characterAi, aux.audioFlags};
    writeBytes(retropp::MemoryRegion{.at = kFormationBytes, .size = 4}, formation);
    writeByte(kTakenFromTheParty, state.takenFromTheParty.characters.bits);
    writeByte(kDoomGazeBattler, state.doomGaze ? static_cast<std::uint8_t>(slotIndex(*state.doomGaze)) : kNoOne);
    writeBytes(retropp::MemoryRegion{.at = kSwitchVariables, .size = game::kBattleVariables}, state.switches.variables);
    writeByte(kRngPosition, state.rng.position());
    const std::array<std::uint8_t, kMessageVariableBytes> variables{};
    writeInFlightBytes(retropp::MemoryRegion{.at = kEndingMessageVariables, .size = kMessageVariableBytes}, variables);
    const std::array<std::uint8_t, kVeldtBattleBytes> veldt{};
    writeBytes(retropp::MemoryRegion{.at = kVeldtBattles, .size = kVeldtBattleBytes}, veldt);
}

EndingResult DamageOracle::readEndingResult() {
    EndingResult result;
    result.cells = readParty();
    result.inventory = readInventory();
    result.itemsObtained = readItemsObtained();
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const std::uint32_t at = slotIndex(battle::BattlerSlot::character(position));
        const std::uint8_t held = readByte(kItemInHand + at);
        result.hands[static_cast<std::size_t>(position)] =
            held == kNoOne ? std::nullopt : std::optional<ItemId>(static_cast<ItemId>(held));
        result.morphGauges[static_cast<std::size_t>(position)] = readByte(kMorphMeterShown + at);
    }
    result.holdingItem = static_cast<std::uint8_t>(readByte(kHoldingItem) & kHoldingItemBits);
    result.menusDisabled = (readByte(kEndingFlags) & kMenusClosedBit) != 0;
    const std::vector<std::uint8_t> switches =
        machine_.read(retropp::MemoryRegion{.at = kSwitchVariables, .size = game::kBattleVariables});
    std::copy(switches.begin(), switches.end(), result.switches.variables.begin());
    result.rngPosition = readByte(kRngPosition);
    result.calls = guestCalls(graphicsCalls_, readByte(kFormationBytes + 1), readByte(kFieldFlags));
    return result;
}

EndingResult DamageOracle::callEnding(const EndingScenario& scenario, const SnapshotRoutine& snapshot,
                                      const std::function<void()>& routine) {
    writeEndingScenario(scenario);
    graphicsCalls_.clear();
    unansweredGraphics_.reset();
    machine_.escapes()[kGraphicsKey].armed(true);
    call([&] {
        routine();
        trace_->returnFromSnapshot(snapshot);
    });
    machine_.escapes()[kGraphicsKey].armed(false);
    if (unansweredGraphics_) {
        throw std::runtime_error("the ending called graphics command " + std::to_string(*unansweredGraphics_) +
                                 ", which the oracle has no answer for");
    }
    return readEndingResult();
}

EndingResult DamageOracle::wrapUp(const EndingScenario& scenario) {
    return callEnding(scenario, kSnapshotWrapUp,
                      [&] { wrapUp_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
}

EndingResult DamageOracle::win(const EndingScenario& scenario) {
    return callEnding(scenario, kSnapshotWin,
                      [&] { winBattle_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
}

EndingResult DamageOracle::loss(const EndingScenario& scenario, std::optional<std::uint8_t> message) {
    return callEnding(scenario, kSnapshotLoss, [&] {
        trace_->handOverLossMessage(message);
        loseBattle_(message.value_or(kNoOne), kByteWideStatus, kBattleDataBank, kBattleDirectPage);
    });
}

EndingScenario DamageOracle::readEndingScenario() {
    EndingScenario s;
    s.cells = readParty();
    battle::BattleState& state = s.state;
    state.version = GameVersion::US_1_1;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::uint32_t at = slotIndex(slot);
        battler.status = StatusSet{{readByte(kStatus12 + at), readByte(kStatus12 + at + 1), readByte(kStatus34 + at),
                                    readByte(kStatus34 + at + 1)}};
        battler.relicEffects4.bits = readByte(kRelicEffects4And5 + at);
        battler.relicEffects5.bits = readByte(kRelicEffects4And5 + at + 1);
        battler.hp = readWord(kHp + at);
        battler.mp = readWord(kMp + at);
        battler.maxMp = readWord(kMaxMp + at);
        if (slot.isCharacter()) {
            const std::size_t position = static_cast<std::size_t>(slot.position());
            const std::uint8_t actor = readByte(kPartyActor + at);
            const std::uint8_t record = readByte(kPartyRecord + at);
            if (actor != kNoOne) {
                battler.actor = static_cast<CharacterPropId>(actor);
            }
            if (record != kNoOne) {
                battler.record = record;
            }
            battler.present = battler.actor.has_value();
            const std::uint16_t equipsAs = readWord(kPartyMemberBit + at);
            battler.equipsAs.lo = static_cast<std::uint8_t>(equipsAs & 0xFF);
            battler.equipsAs.hi = static_cast<std::uint8_t>(equipsAs >> 8);
            const std::uint8_t held = readByte(kItemInHand + at);
            if (held != kNoOne) {
                battler.itemInHand = static_cast<ItemId>(held);
            }
            battler.morphGauge = readByte(kMorphMeterShown + at);
            if (readByte(kEquipmentChanged + static_cast<std::uint32_t>(position)) != 0) {
                throw std::runtime_error("party slot " + std::to_string(position) +
                                         " has an equipment change pending, which the ending's replay does not rebuild");
            }
        } else {
            const auto monster = static_cast<std::uint32_t>(slot.position() * 2);
            const std::uint16_t index = readWord(kMonsterIndex + at);
            if (index != kEmptyMonsterSlot) {
                battler.monster = static_cast<MonsterId>(index);
            }
            battler.present = battler.monster.has_value();
            battler.experience = readWord(kMonsterExperience + monster);
            battler.gold = readWord(kMonsterGold + monster);
        }
    }
    state.alive = setOf(readWord(kBattlersAlive));
    state.allyCount = readByte(kAllyCount);
    state.enemyCount = readByte(kEnemiesAlive);
    for (std::size_t lore = 0; lore < state.loresLearned.size(); ++lore) {
        state.loresLearned.set(lore, (readByte(kLoresLearned + static_cast<std::uint32_t>(lore / 8)) >> (lore % 8) & 1) != 0);
    }
    state.colosseum = readByte(kColosseumCell) != 0;
    if (state.colosseum) {
        state.colosseumWager = static_cast<ItemId>(readByte(kWagerCell));
        state.colosseumPrize = static_cast<ItemId>(readByte(kPrizeCell));
    }
    state.startingFormation = static_cast<FormationId>(readWord(kBattleNumber));
    state.formation = static_cast<FormationId>(readWord(kFormationCell));
    const std::uint16_t field = readWord(kFieldFlags);
    state.onTheVeldt = (field & kOnTheVeldtBit) != 0;
    state.keepFieldMusic = (field & kFieldMusicPlaysOnBit) != 0;
    state.background = static_cast<BattleBackgroundId>(readWord(kBackgroundCell));
    // The fanfare is off by the formation, or by the battle once Shadow has left.
    state.noVictoryFanfare = (readByte(kFormationBytes + 1) & kFanfareOffBit) != 0 &&
                             (getFormationAux(state.formation).flags & kFanfareOffBit) == 0;
    state.takenFromTheParty = battle::BattlerSet{BattleSlotMask(readByte(kTakenFromTheParty)), BattleSlotMask{}};
    const std::uint8_t doomGaze = readByte(kDoomGazeBattler);
    if (doomGaze != kNoOne) {
        state.doomGaze = battle::BattlerSlot::fromIndex(doomGaze / 2);
    }
    const std::vector<std::uint8_t> switches =
        machine_.read(retropp::MemoryRegion{.at = kSwitchVariables, .size = game::kBattleVariables});
    std::copy(switches.begin(), switches.end(), state.switches.variables.begin());
    state.rng = battle::BattleRng{readByte(kRngPosition)};
    state.holdingItem = battle::BattlerSet{BattleSlotMask(readByte(kHoldingItem)), BattleSlotMask{}};
    const std::uint8_t morphed = readByte(kMorphedPartyMember);
    if (morphed != kNoOne) {
        state.morphedBattler = battle::BattlerSlot::fromIndex(morphed / 2);
    }
    state.morphCounter = readWord(kMorphMeter);
    state.menusDisabled = (readByte(kEndingFlags) & kMenusClosedBit) != 0;
    state.inventory.items = readInventory();
    state.itemsObtained = readItemsObtained();
    state.party = s.cells.party;
    return s;
}

std::string DamageOracle::replayEnding(const TracedEnding& ending) {
    const auto routine = std::find_if(kSnapshotRoutines.begin(), kSnapshotRoutines.end(),
                                      [&](const SnapshotRoutine& r) { return ending.routine == r.name; });
    if (routine == kSnapshotRoutines.end()) {
        throw std::runtime_error("the trace names an ending it does not follow: " + ending.routine);
    }
    if (ending.entry.empty() || ending.left.empty()) {
        throw std::runtime_error(ending.routine + "'s snapshots are not both in the trace");
    }
    for (const auto& [cell, value] : ending.entry) {
        writeByte(cell, value);
    }
    const EndingScenario s = readEndingScenario();
    EndingResult port;
    if (routine->entry == kWinBattle) {
        port = portWin(s);
    } else if (routine->entry == kLoseBattle) {
        // The message its caller hands over: the one logged where the caller said, otherwise as the
        // cartridge's two calls hand it — Banon fell, from his own ending; the party annihilated from
        // the end check, or none in the colosseum.
        const bool banon = ending.returnedTo == routine->returns[1];
        const std::optional<std::uint8_t> handed =
            ending.handedMessage ? *ending.handedMessage
            : banon              ? std::optional<std::uint8_t>{battle::kMessageBanonFell}
            : s.state.colosseum  ? std::nullopt
                                 : std::optional<std::uint8_t>{battle::kMessageAnnihilated};
        port = portLoss(s, handed);
    } else {
        port = portWrapUp(s);
    }

    for (const auto& [cell, value] : ending.left) {
        writeByte(cell, value);
    }
    graphicsCalls_.clear();
    EndingResult guest = readEndingResult();
    // The trace logs what an ending shows as its messages alone, so both sides are compared on those.
    std::erase_if(port.calls, [](const EndingCall& call) { return call.kind != EndingCall::Kind::Message; });
    guest.calls.clear();
    for (const LoggedMessage& message : ending.messages) {
        guest.calls.push_back(EndingCall{.kind = EndingCall::Kind::Message, .message = messageShownBy(message)});
    }
    return differences(port, guest);
}

WriteBackResult DamageOracle::writeBack(const EndingScenario& scenario) {
    writeEndingScenario(scenario);
    call([&] { updateSram_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    WriteBackResult result;
    result.cells = readParty();
    for (std::size_t slot = 0; slot < game::kInventorySlots; ++slot) {
        const auto at = static_cast<std::uint32_t>(slot);
        result.inventory[slot] = game::InventorySlot{.item = static_cast<ItemId>(readByte(kSavedItems + at)),
                                                     .quantity = readByte(kSavedQuantities + at)};
    }
    const std::vector<std::uint8_t> variables =
        machine_.read(retropp::MemoryRegion{.at = kSavedVariables, .size = game::kBattleVariables});
    std::copy(variables.begin(), variables.end(), result.switches.variables.begin());
    for (std::size_t battle = 0; battle < result.veldtBattles.size(); ++battle) {
        result.veldtBattles.set(battle, (readByte(kVeldtBattles + static_cast<std::uint32_t>(battle / 8)) >> (battle % 8) & 1) != 0);
    }
    return result;
}

WriteBackResult portWriteBack(const EndingScenario& scenario) {
    battle::BattleState state = portState(scenario);
    state.outcome = battle::BattleWon{};
    const battle::BattleResult handedBack = battle::writeBack(state);
    WriteBackResult result;
    result.cells = scenario.cells;
    result.cells.party = handedBack.party;
    result.inventory = handedBack.party.inventory;
    result.switches = handedBack.switches;
    if (handedBack.veldtFormation) {
        result.veldtBattles.set(static_cast<std::size_t>(*handedBack.veldtFormation));
    }
    return result;
}

std::string differences(const WriteBackResult& port, const WriteBackResult& guest) {
    std::string out;
    const std::vector<PartyCell> ours = partyImage(port.cells);
    const std::vector<PartyCell> theirs = partyImage(guest.cells);
    for (std::size_t i = 0; i < ours.size(); ++i) {
        addLine(out, partyCellName(ours[i].at).c_str(), ours[i].value, theirs[i].value);
    }
    for (std::size_t slot = 0; slot < port.inventory.size(); ++slot) {
        const std::string field = "saved inventory slot " + std::to_string(slot);
        addLine(out, (field + " item").c_str(), static_cast<int>(port.inventory[slot].item),
                static_cast<int>(guest.inventory[slot].item));
        addLine(out, (field + " quantity").c_str(), port.inventory[slot].quantity, guest.inventory[slot].quantity);
    }
    for (std::size_t i = 0; i < port.switches.variables.size(); ++i) {
        const std::string field = "battle variable " + std::to_string(i);
        addLine(out, field.c_str(), port.switches.variables[i], guest.switches.variables[i]);
    }
    for (std::size_t battle = 0; battle < port.veldtBattles.size(); ++battle) {
        const std::string field = "Veldt battle " + std::to_string(battle);
        addLine(out, field.c_str(), port.veldtBattles.test(battle), guest.veldtBattles.test(battle));
    }
    return out;
}

EndingResult portWrapUp(const EndingScenario& scenario) {
    battle::BattleState state = portState(scenario);
    battle::NullExecutor executor;
    battle::wrapUpBattle(state, executor);
    return portEndingResult(scenario, state, false);
}

EndingResult portWin(const EndingScenario& scenario) {
    battle::BattleState state = portState(scenario);
    battle::NullExecutor executor;
    battle::playBattleEnd(state, executor, battle::BattleWon{});
    return portEndingResult(scenario, state, true);
}

EndingResult portLoss(const EndingScenario& scenario, std::optional<std::uint8_t> message) {
    battle::BattleState state = portState(scenario);
    battle::NullExecutor executor;
    const battle::BattleOutcome outcome = battle::BattleLost{.message = message};
    battle::playBattleEnd(state, executor, outcome);
    state.outcome = outcome;
    EndingResult result = portEndingResult(scenario, state, false);
    // The defeat switch as the write-back sets it; Doom Gaze's HP is the write-back's own, which the
    // cartridge's loss does not run.
    battle::BattleState handedBack = state;
    handedBack.doomGaze.reset();
    result.switches = battle::writeBack(handedBack).switches;
    return result;
}

std::string differences(const EndingResult& port, const EndingResult& guest) {
    std::string out;
    const std::vector<PartyCell> ours = partyImage(port.cells);
    const std::vector<PartyCell> theirs = partyImage(guest.cells);
    for (std::size_t i = 0; i < ours.size(); ++i) {
        addLine(out, partyCellName(ours[i].at).c_str(), ours[i].value, theirs[i].value);
    }
    addInventoryLines(out, port.inventory, guest.inventory);
    addListLines(out, port.itemsObtained, guest.itemsObtained);
    for (std::size_t position = 0; position < port.hands.size(); ++position) {
        const std::string of = " of party slot " + std::to_string(position);
        const auto held = [](const std::optional<ItemId>& item) { return item ? static_cast<int>(*item) : -1; };
        addLine(out, ("item in hand" + of).c_str(), held(port.hands[position]), held(guest.hands[position]));
        addLine(out, ("Morph meter shown" + of).c_str(), port.morphGauges[position], guest.morphGauges[position]);
    }
    addLine(out, "holding an item", port.holdingItem, guest.holdingItem);
    addLine(out, "battle menus closed", port.menusDisabled, guest.menusDisabled);
    for (std::size_t i = 0; i < port.switches.variables.size(); ++i) {
        const std::string field = "battle variable " + std::to_string(i);
        addLine(out, field.c_str(), port.switches.variables[i], guest.switches.variables[i]);
    }
    addLine(out, "random-number position", port.rngPosition, guest.rngPosition);
    const std::size_t calls = std::max(port.calls.size(), guest.calls.size());
    for (std::size_t i = 0; i < calls; ++i) {
        const std::string ours = i < port.calls.size() ? callText(port.calls[i]) : "none";
        const std::string theirs = i < guest.calls.size() ? callText(guest.calls[i]) : "none";
        if (ours != theirs) {
            out += "shown " + std::to_string(i) + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    }
    return out;
}

ItemsObtainedResult portAddItemObtained(const ItemsObtainedScenario& scenario) {
    battle::BattleState state;
    state.itemsObtained = scenario.list;
    state.inventory.items = scenario.inventory;
    battle::obtainItem(state, scenario.item, scenario.quantity);
    return ItemsObtainedResult{.list = state.itemsObtained, .inventory = state.inventory.items};
}

ItemsObtainedResult portMergeItemsObtained(const ItemsObtainedScenario& scenario) {
    battle::BattleState state;
    state.itemsObtained = scenario.list;
    state.inventory.items = scenario.inventory;
    battle::mergeItemsObtained(state);
    return ItemsObtainedResult{.list = state.itemsObtained, .inventory = state.inventory.items};
}

std::string differences(const ItemsObtainedResult& port, const ItemsObtainedResult& guest) {
    std::string out;
    addListLines(out, port.list, guest.list);
    addInventoryLines(out, port.inventory, guest.inventory);
    return out;
}

namespace {

constexpr int kMostOfOneItem = 99;
constexpr int kLargestQuantity = 255;

std::uint8_t drawQuantity(std::mt19937& rng) {
    switch (rng() % 5) {
        case 0:
            return static_cast<std::uint8_t>(kMostOfOneItem - upTo(rng, 4));
        case 1:
            return static_cast<std::uint8_t>(1 + upTo(rng, kLargestQuantity - 1));
        default:
            return static_cast<std::uint8_t>(1 + upTo(rng, kMostOfOneItem - 1));
    }
}

// An inventory as the battle builds one with no one in the party: some slots empty, or with a quarter
// of the draws every slot holding an item. A full one holds only some of the 255 items, a few of them
// twice, so that an item the party does not hold has nowhere to go.
std::array<battle::BattleItem, game::kInventorySlots> drawInventory(std::mt19937& rng) {
    static const battle::BattleState kNoParty;
    std::vector<int> order(kItemsBelowEmpty);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);
    const bool full = oneIn(rng, 4);
    if (full) {
        order.resize(order.size() - static_cast<std::size_t>(1 + upTo(rng, 100)));
    }
    std::array<battle::BattleItem, game::kInventorySlots> inventory{};
    std::size_t next = 0;
    for (battle::BattleItem& slot : inventory) {
        if (!full && oneIn(rng, 3)) {
            slot = battle::BattleItem{};
            continue;
        }
        const auto item = static_cast<ItemId>(order[next++ % order.size()]);
        slot = battle::battleItem(kNoParty, item, drawQuantity(rng));
    }
    return inventory;
}

// A place of the list: empty, or a record with any flags, targeting and quantity.
battle::BattleItem drawPlace(std::mt19937& rng) {
    if (oneIn(rng, 2)) {
        return battle::BattleItem{};
    }
    const std::array<std::uint8_t, battle::kItemRecordBytes> record{
        static_cast<std::uint8_t>(drawItem(rng)), byte(rng), byte(rng), drawQuantity(rng), byte(rng)};
    return battle::battleItemFromRecord(record);
}

// The list, its counts drawn so a merge often starts at or runs past place 12, and now and then where
// the counts wrap.
battle::ItemsObtained drawItemsObtained(std::mt19937& rng) {
    battle::ItemsObtained list;
    for (battle::BattleItem& place : list.entries) {
        place = drawPlace(rng);
    }
    switch (rng() % 4) {
        case 0:
            list.merged = static_cast<std::uint8_t>(11 + upTo(rng, 5));
            break;
        case 1:
            list.merged = static_cast<std::uint8_t>(250 + upTo(rng, 5));
            break;
        default:
            list.merged = static_cast<std::uint8_t>(upTo(rng, 31));
            break;
    }
    list.added = static_cast<std::uint8_t>(list.merged + upTo(rng, 4));
    return list;
}

}  // namespace

EndingScenario drawEndingScenario(std::mt19937& rng) {
    EndingScenario s;
    s.cells = drawPartyCells(rng);
    battle::BattleState& state = s.state;
    state.inventory.items = drawInventory(rng);
    state.itemsObtained = drawItemsObtained(rng);
    for (int position = 0; position < battle::kPartySlots; ++position) {
        battle::Battler& member = state.at(battle::BattlerSlot::character(position));
        if (oneIn(rng, 5)) {
            continue;
        }
        const auto actor = static_cast<int>(rng() % kActors);
        member.actor = static_cast<CharacterPropId>(actor);
        const auto ownBit = static_cast<std::uint16_t>(1u << actor);
        member.equipsAs.lo = oneIn(rng, 4) ? byte(rng) : static_cast<std::uint8_t>(ownBit & 0xFF);
        member.equipsAs.hi = oneIn(rng, 4) ? byte(rng) : static_cast<std::uint8_t>(ownBit >> 8);
        member.morphGauge = byte(rng);
        if (oneIn(rng, 3)) {
            continue;
        }
        // Often an item the inventory already holds, so a merge adds to it.
        const battle::BattleItem& held = state.inventory.items[rng() % state.inventory.items.size()];
        member.itemInHand = oneIn(rng, 2) && held.item != ItemId::EMPTY ? held.item : drawItem(rng);
    }
    state.holdingItem = battle::BattlerSet{BattleSlotMask(static_cast<std::uint8_t>(upTo(rng, 15))), BattleSlotMask{}};
    if (oneIn(rng, 3)) {
        state.morphedBattler = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    state.morphCounter = static_cast<std::uint16_t>(rng() & 0xFFFF);
    state.party = s.cells.party;
    return s;
}

namespace {

constexpr std::size_t kFormationIds = 576;
constexpr int kBackgrounds = 56;
constexpr std::size_t kLores = 24;

// The actor a party member is drawn as: the twelve with spell lists, Gogo, Umaro, Banon and Leo, and
// the two ghosts in Banon's and Leo's records — Terra, Mog, Gau, Gogo, Umaro and the guests often.
CharacterPropId drawActor(std::mt19937& rng, std::size_t record) {
    if (record >= 14 && oneIn(rng, 2)) {
        return record == 14 ? CharacterPropId::GHOST_1 : CharacterPropId::GHOST_2;
    }
    return static_cast<CharacterPropId>(record);
}

std::size_t drawRecord(std::mt19937& rng) {
    static constexpr std::array<std::size_t, 8> kOften{0, 10, 11, 12, 13, 14, 15, 2};
    return oneIn(rng, 2) ? kOften[rng() % kOften.size()] : rng() % game::kCharacterRecords;
}

}  // namespace

EndingScenario drawWinScenario(std::mt19937& rng) {
    EndingScenario s = drawEndingScenario(rng);
    battle::BattleState& state = s.state;
    game::Party& party = s.cells.party;

    // The party: up to four members, each in a record of its own, at least one alive.
    std::array<bool, game::kCharacterRecords> taken{};
    int allies = 0;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        battle::Battler& member = state.at(battle::BattlerSlot::character(position));
        member = battle::Battler{};
        if (position > 0 && oneIn(rng, 4)) {
            continue;
        }
        std::size_t record = drawRecord(rng);
        while (taken[record]) {
            record = (record + 1) % game::kCharacterRecords;
        }
        taken[record] = true;
        const CharacterPropId actor = drawActor(rng, record);
        party.characters[record].actor = actor;
        member.present = true;
        member.actor = actor;
        member.record = record;
        const auto ownBit = static_cast<std::uint16_t>(1u << (static_cast<int>(actor) & 0x0F));
        member.equipsAs.lo = static_cast<std::uint8_t>(ownBit & 0xFF);
        member.equipsAs.hi = static_cast<std::uint8_t>(ownBit >> 8);
        member.morphGauge = byte(rng);
        if (oneIn(rng, 3)) {
            member.itemInHand = drawItem(rng);
        }
        member.relicEffects5.bits = static_cast<std::uint8_t>((oneIn(rng, 4) ? 0x10 : 0) | (oneIn(rng, 4) ? 0x08 : 0));
        member.relicEffects4.bits = byte(rng);
        member.hp = static_cast<std::uint16_t>(upTo(rng, kPartyMaxHp));
        member.maxMp = oneIn(rng, 5) ? 0 : static_cast<std::uint16_t>(1 + upTo(rng, kPartyMaxMp - 1));
        member.mp = static_cast<std::uint16_t>(upTo(rng, member.maxMp));
        const bool alive = allies == 0 || !oneIn(rng, 4);
        member.status.bytes[0] = static_cast<std::uint8_t>(sparse(rng) & ~0xC2);
        member.status.bytes[3] = sparse(rng);
        if (alive) {
            state.alive.add(battle::BattlerSlot::character(position));
            ++allies;
        } else {
            member.status.set(oneIn(rng, 2) ? StatusId::DEAD : StatusId::PETRIFY);
        }
        // Experience often at or past a few levels.
        game::CharacterRecord& r = party.characters[record];
        r.level = static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1));
        if (r.level < kPartyMaxLevel && oneIn(rng, 2)) {
            const int reached = std::min<int>(r.level + upTo(rng, 3), kPartyMaxLevel - 1);
            r.experience = experienceForNextLevel(static_cast<std::uint8_t>(reached)) -
                           static_cast<std::uint32_t>(upTo(rng, 300));
        }
        if (oneIn(rng, 8)) {
            state.takenFromTheParty.add(battle::BattlerSlot::character(position));
        }
    }
    state.allyCount = allies;
    state.enemyCount = 0;

    // The monsters: empty, fallen, or fled and standing.
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        const battle::BattlerSlot slot = battle::BattlerSlot::monster(position);
        battle::Battler& monster = state.at(slot);
        monster = battle::Battler{};
        if (oneIn(rng, 4)) {
            continue;
        }
        monster.present = true;
        monster.monster = static_cast<MonsterId>(rng() % kMonsterIds);
        monster.experience = static_cast<std::uint16_t>(oneIn(rng, 3) ? rng() & 0xFFFF : upTo(rng, 2000));
        monster.gold = static_cast<std::uint16_t>(oneIn(rng, 3) ? rng() & 0xFFFF : upTo(rng, 2000));
        monster.hp = static_cast<std::uint16_t>(rng() & 0xFFFF);
        if (!oneIn(rng, 3)) {
            static constexpr std::array<StatusId, 3> kFallenBy{StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE};
            monster.status.set(kFallenBy[rng() % kFallenBy.size()]);
        } else {
            state.alive.add(slot);
        }
    }
    if (oneIn(rng, 10)) {
        state.doomGaze = battle::BattlerSlot::monster(upTo(rng, battle::kMonsterSlots - 1));
    }

    state.formation = static_cast<FormationId>(rng() % kFormationIds);
    state.startingFormation = oneIn(rng, 4) ? static_cast<FormationId>(rng() % 1024) : state.formation;
    state.onTheVeldt = oneIn(rng, 4);
    state.keepFieldMusic = oneIn(rng, 4);
    state.noVictoryFanfare = oneIn(rng, 8);
    state.background = static_cast<BattleBackgroundId>(upTo(rng, kBackgrounds - 1));
    if (oneIn(rng, 10)) {
        state.colosseum = true;
        state.colosseumPrize = oneIn(rng, 10) ? ItemId::EMPTY : drawItem(rng);
        state.colosseumWager = drawItem(rng);
    }
    if (oneIn(rng, 3)) {
        for (std::size_t lore = 0; lore < kLores; ++lore) {
            state.loresLearned.set(lore, oneIn(rng, 4));
        }
    }
    for (std::uint8_t& variable : state.switches.variables) {
        variable = byte(rng);
    }
    if (oneIn(rng, 2)) {
        state.switches.set(game::kMagicPointsEarnedSwitch);
    } else {
        state.switches.clear(game::kMagicPointsEarnedSwitch);
    }
    state.rng = battle::BattleRng{byte(rng)};
    if (oneIn(rng, 4)) {
        party.gil = kMaxGil - static_cast<std::uint32_t>(upTo(rng, 5000));  // the gold often reaching the cap
    }
    state.party = party;
    return s;
}

ItemsObtainedScenario drawItemsObtainedScenario(std::mt19937& rng) {
    ItemsObtainedScenario s;
    s.list = drawItemsObtained(rng);
    s.inventory = drawInventory(rng);
    s.item = oneIn(rng, 10) ? ItemId::EMPTY : drawItem(rng);
    s.quantity = drawQuantity(rng);
    return s;
}

std::string describe(const EndingScenario& s) {
    const battle::BattleState& state = s.state;
    std::string text = "hands";
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::Battler& member = state.at(battle::BattlerSlot::character(position));
        text += " " + (member.itemInHand ? std::to_string(static_cast<int>(*member.itemInHand)) : std::string{"-"});
    }
    text += " holding " + std::to_string(state.holdingItem.characters.bits) + " morphed " +
            (state.morphedBattler ? std::to_string(state.morphedBattler->position()) : std::string{"-"}) +
            " meter " + std::to_string(state.morphCounter >> 8) + " supply " + std::to_string(s.cells.party.morphSupply) +
            " added " + std::to_string(state.itemsObtained.added) + " merged " +
            std::to_string(state.itemsObtained.merged) + " list";
    for (const battle::BattleItem& place : state.itemsObtained.entries) {
        text += " " + (place.item == ItemId::EMPTY ? std::string{"-"}
                                                   : std::to_string(static_cast<int>(place.item)) + "x" +
                                                         std::to_string(place.quantity));
    }
    const auto empty = std::count_if(state.inventory.items.begin(), state.inventory.items.end(),
                                     [](const battle::BattleItem& slot) { return slot.item == ItemId::EMPTY; });
    text += " empty slots " + std::to_string(empty);
    return text;
}

// --- a monster's AI --------------------------------------------------------------------------------

namespace {

// The script reader's cells: the four bytes of the step it is at (the step's command, then a
// condition's number and two operands), whether the run is restricted (one byte, 0 when it is not),
// the run's targets and the targets it started with.
constexpr std::uint32_t kAiStep = kWorkRam + 0x3A2C;
constexpr std::uint32_t kAiRestricted = kWorkRam + 0x00F8;
constexpr std::uint32_t kAiTargets = kWorkRam + 0x00FC;
constexpr std::uint32_t kAiStartingTargets = kWorkRam + 0x00FE;
// The step command a condition goes out as.
constexpr std::uint8_t kAiConditionStep = 0xFC;
constexpr std::uint8_t kAiRestrictedOn = 0xFF;
// Per party slot, at twice the slot's index: the party member in it, none negative. Per monster
// slot the monster's index is the word at kMonsterIndex, none $FFFF.
constexpr std::uint32_t kActor = kWorkRam + 0x3ED8;
constexpr std::uint16_t kNoMonster = 0xFFFF;
// Per battler, at twice the battler's index: its own timer (a word).
constexpr std::uint32_t kOwnTimer = kWorkRam + 0x3DC0;
// The battle's: the battlers alive (the party's byte, then the monsters'), the monster slots that
// hold a monster, the monsters and party members fighting against the party, the battle's timer (a
// word), the battle's variables followed by the battle switches, the names listed, the formation.
constexpr std::uint32_t kAlive = kWorkRam + 0x3A74;
constexpr std::uint32_t kMonstersHeld = kWorkRam + 0x3A73;
constexpr std::uint32_t kEnemyCount = kWorkRam + 0x3A77;
constexpr std::uint32_t kBattleTimer = kWorkRam + 0x3A44;
constexpr std::uint32_t kBattleVariables = kWorkRam + 0x3EB0;
constexpr std::uint32_t kBattleSwitches = kBattleVariables + kAiBattleVariables;
constexpr std::uint32_t kNamesListed = kWorkRam + 0x3ECA;
constexpr std::uint32_t kFormation = kWorkRam + 0x11E0;
// The story's flags byte with Gau recruited in it.
constexpr std::uint32_t kStoryFlags = kWorkRam + 0x1EDF;
constexpr std::uint8_t kGauRecruitedBit = 0x08;


std::optional<battle::BattlerSlot> slotAt(std::uint8_t value) {
    if (value >= kTopBit) {
        return std::nullopt;
    }
    return battle::BattlerSlot::fromIndex(value / 2);
}

std::string markText(const std::optional<battle::BattlerSlot>& slot) {
    return slot ? std::to_string(slot->index()) : std::string{"none"};
}

}  // namespace

void DamageOracle::writeAiCells(const AiScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        writeWord(kBattlerBit + at, maskOf(battle::BattlerSet::of(slot)));
        if (slot.isCharacter()) {
            writeByte(kActor + at, battler.actor ? static_cast<std::uint8_t>(*battler.actor) : kNoOne);
        } else {
            writeWord(kMonsterIndex + at, battler.monster ? static_cast<std::uint16_t>(*battler.monster) : kNoMonster);
        }
        std::uint8_t field = battler.present ? kPresentBit : 0;
        field |= battler.inBattleMenu ? kInBattleMenuBit : 0;
        field |= battler.menuMayOpen ? kMenuMayOpenBit : 0;
        writeByte(kOnTheField + at, field);
        writeByte(kStatus12 + at, battler.status.bytes[0]);
        writeByte(kStatus12 + at + 1, battler.status.bytes[1]);
        writeByte(kStatus34 + at, battler.status.bytes[2]);
        writeByte(kStatus34 + at + 1, battler.status.bytes[3]);
        writeWord(kHp + at, battler.hp);
        writeWord(kMp + at, battler.mp);
        writeByte(kLevel + at, battler.level);
        writeByte(kWeakHalved + at, battler.elementsWeak.bits);
        writeByte(kLastAttacker + at, markByte(battler));
        writeByte(kTargetingTarget + at, slotByte(battler.targetingTarget));
        const battle::LastHits& last = battler.lastHit;
        const auto by = [](const auto& hit) { return hit ? static_cast<std::uint8_t>(slotIndex(hit->by)) : kNoOne; };
        writeByte(kLastHitCommandBy + at, by(last.command));
        writeByte(kLastHitCommand + at, last.command ? static_cast<std::uint8_t>(last.command->with) : kNoOne);
        writeByte(kLastHitSpellBy + at, by(last.spell));
        writeByte(kLastHitSpell + at, last.spell ? static_cast<std::uint8_t>(last.spell->with) : kNoOne);
        writeByte(kLastHitItemBy + at, by(last.item));
        writeByte(kLastHitItem + at, last.item ? static_cast<std::uint8_t>(last.item->with) : kNoOne);
        writeByte(kLastHitElementBy + at, by(last.element));
        writeByte(kLastHitElement + at, last.element ? last.element->with.bits : 0);
        writeByte(kLastDamagedBy + at, slotByte(battler.lastDamagedBy));
        writeByte(kAiVariable + at, battler.aiVariable);
        writeWord(kOwnTimer + at, battler.turnTimer);
    }

    // The reach check's four cells, written so that what they leave between them is the port's own
    // reachable set (reachableTargets()), and the sign of the word above the counterattack cell the
    // reach flag the last command left.
    writeByte(kReachMonsters, 0xFF);
    writeByte(kReachCharacters, static_cast<std::uint8_t>(~seizedSet(state).characters.bits));
    writeWord(kReachFirst, maskOf(state.targetableOrPresent & state.countedInBattle));
    writeWord(kReachSecond, static_cast<std::uint16_t>(~maskOf(state.aiUntargetable)));
    writeByte(kCountersEnabled, state.scratch.countersEnabled ? kCountersOn : 0);
    writeByte(kReachKeepsAway, state.awayFromTheFieldUnreachable ? kAwayUnreachable : 0);
    writeWord(kAwayFromTheField, maskOf(state.awayFromTheField));

    std::uint8_t held = 0;
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        if (state.at(battle::BattlerSlot::monster(position)).monster) {
            held = static_cast<std::uint8_t>(held | 1u << position);
        }
    }
    writeWord(kAlive, maskOf(state.alive));
    writeByte(kMonstersHeld, held);
    writeByte(kAllyCount, static_cast<std::uint8_t>(state.allyCount));
    writeByte(kEnemyCount, static_cast<std::uint8_t>(state.enemyCount));
    writeWord(kBattleTimer, state.battleCounter);
    writeBytes(retropp::MemoryRegion{.at = kBattleVariables, .size = kAiBattleVariables}, state.battleVariables);
    writeBytes(retropp::MemoryRegion{.at = kBattleSwitches, .size = game::kBattleVariables}, state.switches.variables);
    writeByte(kNamesListed, state.monsterNamesListed);
    const std::uint8_t story = readByte(kStoryFlags);
    writeByte(kStoryFlags, static_cast<std::uint8_t>(state.gauRecruited ? story | kGauRecruitedBit
                                                                        : story & ~kGauRecruitedBit));
    writeWord(kFormation, static_cast<std::uint16_t>(state.formation));
    writeByte(kRngPosition, state.rng.position());

    writeInFlightWord(kAiTargets, maskOf(scenario.run.targets));
    writeInFlightWord(kAiStartingTargets, maskOf(scenario.run.startingTargets));
    writeInFlightByte(kAiRestricted, scenario.run.restricted ? kAiRestrictedOn : 0);
}

std::optional<battle::BattlerSlot> DamageOracle::readTargetingTarget(battle::BattlerSlot actor) {
    return slotAt(readByte(kTargetingTarget + slotIndex(actor)));
}

AiTargetsResult DamageOracle::aiTargets(const AiScenario& scenario, AiTarget target, bool reachableOnly) {
    writeAiCells(scenario);
    std::uint8_t status = 0;
    const std::uint8_t entry = reachableOnly ? kByteWideStatus : static_cast<std::uint8_t>(kByteWideStatus | kCarryBit);
    call([&] {
        status = checkAiTarget_(test::ai_bytes::target(target), slotIndex(scenario.actor), entry, kBattleDataBank,
                                kBattleDirectPage);
    });
    return AiTargetsResult{.targets = setOf(readWord(kTargets)),
                           .any = (status & kCarryBit) != 0,
                           .targetingTarget = readTargetingTarget(scenario.actor),
                           .rngPosition = readByte(kRngPosition)};
}

AiConditionResult DamageOracle::aiCondition(const AiScenario& scenario, const AiCondition& condition) {
    writeAiCells(scenario);
    const test::ai_bytes::ConditionBytes bytes = test::ai_bytes::condition(condition);
    const std::array<std::uint8_t, 4> step{kAiConditionStep, bytes.number, bytes.first, bytes.second};
    writeBytes(retropp::MemoryRegion{.at = kAiStep, .size = static_cast<std::uint32_t>(step.size())}, step);
    std::uint8_t status = 0;
    call([&] {
        status = aiConditions_.at(bytes.number)(slotIndex(scenario.actor), kByteWideStatus, kBattleDataBank,
                                                kBattleDirectPage);
    });
    return AiConditionResult{.holds = (status & kCarryBit) != 0,
                             .targets = setOf(readWord(kAiTargets)),
                             .restricted = readByte(kAiRestricted) != 0,
                             .targetingTarget = readTargetingTarget(scenario.actor),
                             .rngPosition = readByte(kRngPosition)};
}

AiTargetsResult portAiTargets(AiScenario scenario, AiTarget target, bool reachableOnly) {
    const battle::BattlerSet targets = battle::aiTargets(scenario.state, scenario.actor, target, reachableOnly);
    return AiTargetsResult{.targets = targets,
                           .any = !targets.empty(),
                           .targetingTarget = scenario.state.at(scenario.actor).targetingTarget,
                           .rngPosition = scenario.state.rng.position()};
}

AiConditionResult portAiCondition(AiScenario scenario, const AiCondition& condition) {
    const bool holds = battle::aiCondition(scenario.state, scenario.actor, condition, scenario.run);
    return AiConditionResult{.holds = holds,
                             .targets = scenario.run.targets,
                             .restricted = scenario.run.restricted,
                             .targetingTarget = scenario.state.at(scenario.actor).targetingTarget,
                             .rngPosition = scenario.state.rng.position()};
}

std::string differences(const AiTargetsResult& port, const AiTargetsResult& guest) {
    EffectCells ours;
    EffectCells theirs;
    const auto put = [](EffectCells& out, const AiTargetsResult& result) {
        out.put("targets", setText(result.targets));
        out.put("any", result.any);
        out.put("Targetting", markText(result.targetingTarget));
        out.put("random-number position", result.rngPosition);
    };
    put(ours, port);
    put(theirs, guest);
    return cellDifferences(ours.cells, theirs.cells);
}

std::string differences(const AiConditionResult& port, const AiConditionResult& guest) {
    EffectCells ours;
    EffectCells theirs;
    const auto put = [](EffectCells& out, const AiConditionResult& result) {
        out.put("holds", result.holds);
        out.put("targets", setText(result.targets));
        out.put("restricted", result.restricted);
        out.put("Targetting", markText(result.targetingTarget));
        out.put("random-number position", result.rngPosition);
    };
    put(ours, port);
    put(theirs, guest);
    return cellDifferences(ours.cells, theirs.cells);
}

namespace {

// The party members the AI draws pick among: the first sixteen, so a drawn target often names one
// in the party.
constexpr unsigned kDrawnCharacters = 16;
constexpr unsigned kMonsterCount = 384;
constexpr unsigned kFormationCount = 576;
constexpr unsigned kSelectorCount = 30;
constexpr unsigned kBattleSwitchCount = 160;
constexpr int kPartyMaxTimer = 600;
constexpr int kHpStep = 128;

AiTarget anyAiTarget(std::mt19937& rng) {
    if (oneIn(rng, 4)) {
        return AiTarget{static_cast<CharacterPropId>(rng() % kDrawnCharacters)};
    }
    return AiTarget{static_cast<AiTargetSelector>(static_cast<unsigned>(AiTargetSelector::MONSTER_SLOT_1) +
                                                  rng() % kSelectorCount)};
}

// A status, often one a selector or a condition asks about.
StatusId anyStatus(std::mt19937& rng) {
    constexpr std::array<StatusId, 4> asked{StatusId::DEAD, StatusId::REFLECT, StatusId::HIDE, StatusId::DANCE};
    return oneIn(rng, 2) ? asked[rng() % asked.size()] : static_cast<StatusId>(rng() % 32);
}

StatusSet drawnStatuses(std::mt19937& rng) {
    if (oneIn(rng, 3)) {
        return anyStatuses(rng);
    }
    StatusSet statuses{{sparse(rng), sparse(rng), sparse(rng), sparse(rng)}};
    if (oneIn(rng, 3)) {
        statuses.set(anyStatus(rng));
    }
    return statuses;
}

AiVariable anyAiVariable(std::mt19937& rng) {
    return oneIn(rng, 3) ? AiVariable::own() : AiVariable::battle(static_cast<std::uint8_t>(rng() % kAiBattleVariables));
}

AiSwitch anyAiSwitch(std::mt19937& rng) {
    const auto bit = static_cast<std::uint8_t>(rng() % 8);
    switch (rng() % 3) {
    case 0:
        return AiSwitch::own(bit);
    case 1:
        return AiSwitch::battleVariable(static_cast<std::uint8_t>(rng() % kAiBattleVariables), bit);
    default:
        return AiSwitch::battleSwitch(static_cast<int>(rng() % kBattleSwitchCount));
    }
}

AiMonsters anyAiMonsters(std::mt19937& rng, bool selfAllowed) {
    AiMonsters monsters;
    do {
        monsters.slots = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F));
    } while (!selfAllowed && monsters.isSelf());
    if (selfAllowed && oneIn(rng, 4)) {
        monsters = AiMonsters::self();
    }
    return monsters;
}

// A small byte, often at a value a drawn battle holds.
std::uint8_t nearly(std::mt19937& rng, int around) {
    return static_cast<std::uint8_t>(std::clamp(around + upTo(rng, 2) - 1, 0, 255));
}

// HP at a multiple of the HP condition's step, or a little either side of one.
std::uint16_t hpAroundStep(std::mt19937& rng) {
    return static_cast<std::uint16_t>(std::max(0, kHpStep * upTo(rng, 40) + upTo(rng, 2) - 1));
}

}  // namespace

AiScenario drawAiScenario(std::mt19937& rng) {
    AiScenario s;
    battle::BattleState& state = s.state;
    s.actor = anySlot(rng);
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        battle::Battler& battler = state.at(slot);
        battler.present = !oneIn(rng, 4);
        if (slot.isCharacter()) {
            if (!oneIn(rng, 4)) {
                battler.actor = static_cast<CharacterPropId>(rng() % kDrawnCharacters);
            }
        } else if (!oneIn(rng, 4)) {
            battler.monster = static_cast<MonsterId>(rng() % kMonsterCount);
        }
        battler.status = drawnStatuses(rng);
        battler.hp = oneIn(rng, 2) ? hpAroundStep(rng) : static_cast<std::uint16_t>(upTo(rng, kPartyMaxHp));
        battler.mp = static_cast<std::uint16_t>(oneIn(rng, 2) ? upTo(rng, 300) : upTo(rng, kPartyMaxMp));
        battler.level = oneIn(rng, 2) ? static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1)) : byte(rng);
        battler.elementsWeak.bits = sparse(rng);
        if (!oneIn(rng, 3)) {
            battler.lastAttacker = anySlot(rng);
            battler.retaliationPending = oneIn(rng, 3);
        }
        if (oneIn(rng, 3)) {
            battler.targetingTarget = anySlot(rng);
        }
        const auto hit = [&](auto with) {
            using T = decltype(with);
            return oneIn(rng, 2) ? std::optional<battle::Hit<T>>{} : battle::Hit<T>{.by = anySlot(rng), .with = with};
        };
        battler.lastHit.command = hit(static_cast<BattleCommandId>(rng() % 32));
        battler.lastHit.spell = hit(static_cast<AttackId>(byte(rng)));
        battler.lastHit.item = hit(static_cast<ItemId>(byte(rng)));
        battler.lastHit.element = hit(ElementSet{sparse(rng)});
        if (oneIn(rng, 2)) {
            battler.lastDamagedBy = anySlot(rng);
        }
        battler.aiVariable = oneIn(rng, 2) ? static_cast<std::uint8_t>(upTo(rng, 10)) : byte(rng);
        battler.turnTimer = static_cast<std::uint16_t>(upTo(rng, kPartyMaxTimer));
        if (slot.isCharacter() && oneIn(rng, 6)) {
            battler.seizedBy = battle::BattlerSlot::monster(upTo(rng, battle::kMonsterSlots - 1));
        }
    }
    state.targetableOrPresent = oneIn(rng, 3) ? anyBattlers(rng) : battle::BattlerSet::allCharacters() | battle::BattlerSet::allMonsters();
    state.countedInBattle = oneIn(rng, 3) ? anyBattlers(rng) : battle::BattlerSet::allCharacters() | battle::BattlerSet::allMonsters();
    state.aiUntargetable = oneIn(rng, 3) ? anyBattlers(rng).monstersOnly() : battle::BattlerSet{};
    state.awayFromTheField = oneIn(rng, 3) ? anyBattlers(rng) : battle::BattlerSet{};
    state.awayFromTheFieldUnreachable = !oneIn(rng, 3);
    state.alive = anyBattlers(rng);
    state.allyCount = upTo(rng, battle::kPartySlots);
    state.enemyCount = upTo(rng, battle::kMonsterSlots + 2);
    state.battleCounter = static_cast<std::uint16_t>(upTo(rng, kPartyMaxTimer));
    for (std::uint8_t& variable : state.battleVariables) {
        variable = oneIn(rng, 2) ? static_cast<std::uint8_t>(upTo(rng, 10)) : byte(rng);
    }
    for (std::uint8_t& variable : state.switches.variables) {
        variable = byte(rng);
    }
    state.monsterNamesListed = static_cast<std::uint8_t>(upTo(rng, static_cast<int>(battle::kMonsterNameLines)));
    state.gauRecruited = oneIn(rng, 2);
    state.formation = static_cast<FormationId>(rng() % kFormationCount);
    state.rng = battle::BattleRng(byte(rng));
    s.run.targets = anyBattlers(rng);
    s.run.startingTargets = anyBattlers(rng);
    s.run.restricted = oneIn(rng, 3);
    return s;
}

AiTargetsCase drawAiTargetsCase(std::mt19937& rng) {
    AiTargetsCase c{.scenario = drawAiScenario(rng)};
    c.target = anyAiTarget(rng);
    c.reachableOnly = oneIn(rng, 2);
    return c;
}

AiConditionCase drawAiConditionCase(std::mt19937& rng) {
    AiConditionCase c{.scenario = drawAiScenario(rng), .condition = ai::Never{}};
    const battle::BattleState& state = c.scenario.state;
    const battle::Battler& actor = state.at(c.scenario.actor);
    const battle::Battler& other = state.at(anySlot(rng));
    switch (rng() % std::variant_size_v<AiCondition>) {
    case 0:
        c.condition = ai::Never{};
        break;
    case 1: {
        const auto command = actor.lastHit.command && oneIn(rng, 2) ? actor.lastHit.command->with
                                                                   : static_cast<BattleCommandId>(rng() % 32);
        c.condition = ai::HitByCommand{static_cast<BattleCommandId>(rng() % 32), command};
        break;
    }
    case 2: {
        const auto spell = actor.lastHit.spell && oneIn(rng, 2) ? actor.lastHit.spell->with : static_cast<AttackId>(byte(rng));
        c.condition = ai::HitByAttack{spell, static_cast<AttackId>(byte(rng))};
        break;
    }
    case 3: {
        const auto item = actor.lastHit.item && oneIn(rng, 2) ? actor.lastHit.item->with : static_cast<ItemId>(byte(rng));
        c.condition = ai::HitByItem{static_cast<ItemId>(byte(rng)), item};
        break;
    }
    case 4: {
        const auto elements = actor.lastHit.element && oneIn(rng, 2) ? actor.lastHit.element->with : ElementSet{sparse(rng)};
        c.condition = ai::HitByElement{elements};
        break;
    }
    case 5:
        c.condition = ai::Damaged{};
        break;
    case 6:
        c.condition = ai::HpAtMost{.target = anyAiTarget(rng),
                                   .hp = static_cast<std::uint16_t>(kHpStep * (oneIn(rng, 2) ? other.hp / kHpStep + upTo(rng, 1)
                                                                                             : upTo(rng, 255)))};
        break;
    case 7:
        c.condition = ai::MpAtMost{.target = anyAiTarget(rng), .mp = oneIn(rng, 2) ? nearly(rng, other.mp) : byte(rng)};
        break;
    case 8:
        c.condition = ai::HasStatus{.target = anyAiTarget(rng), .status = anyStatus(rng)};
        break;
    case 9:
        c.condition = ai::NoStatus{.target = anyAiTarget(rng), .status = anyStatus(rng)};
        break;
    case 10:
        c.condition = ai::HalfOwnTimerAtLeast{nearly(rng, actor.turnTimer / 2)};
        break;
    case 11:
        c.condition = ai::VariableBelow{.variable = anyAiVariable(rng), .value = static_cast<std::uint8_t>(upTo(rng, 10))};
        break;
    case 12:
        c.condition = ai::VariableAtLeast{.variable = anyAiVariable(rng), .value = static_cast<std::uint8_t>(upTo(rng, 10))};
        break;
    case 13:
        c.condition = ai::LevelBelow{.target = anyAiTarget(rng), .level = oneIn(rng, 2) ? nearly(rng, other.level) : byte(rng)};
        break;
    case 14:
        c.condition = ai::LevelAtLeast{.target = anyAiTarget(rng), .level = oneIn(rng, 2) ? nearly(rng, other.level) : byte(rng)};
        break;
    case 15:
        c.condition = ai::OneKindOfMonster{};
        break;
    case 16:
        c.condition = ai::MonstersAlive{anyAiMonsters(rng, true)};
        break;
    case 17:
        c.condition = ai::MonstersDead{anyAiMonsters(rng, true)};
        break;
    case 18:
        c.condition = ai::PartyAtLeast{static_cast<std::uint8_t>(upTo(rng, battle::kPartySlots + 1))};
        break;
    case 19:
        c.condition = ai::MonstersAtMost{static_cast<std::uint8_t>(upTo(rng, battle::kMonsterSlots + 2))};
        break;
    case 20:
        c.condition = ai::SwitchSet{anyAiSwitch(rng)};
        break;
    case 21:
        c.condition = ai::SwitchClear{anyAiSwitch(rng)};
        break;
    case 22:
        c.condition = ai::HalfBattleTimerAtLeast{nearly(rng, state.battleCounter / 2)};
        break;
    case 23:
        c.condition = ai::TargetValid{anyAiTarget(rng)};
        break;
    case 24:
        c.condition = ai::GauNotRecruited{};
        break;
    case 25:
        c.condition = ai::InSlot{anyAiMonsters(rng, false)};
        break;
    case 26:
        c.condition = ai::WeakTo{.target = anyAiTarget(rng), .elements = ElementSet{sparse(rng)}};
        break;
    case 27:
        c.condition = ai::FormationIs{oneIn(rng, 2) ? state.formation : static_cast<FormationId>(rng() % kFormationCount)};
        break;
    default:
        c.condition = ai::Always{};
        break;
    }
    return c;
}

std::string describe(const AiScenario& s) {
    return "actor " + std::to_string(s.actor.index()) + " random-number position " +
           std::to_string(s.state.rng.position()) + " run targets " + setText(s.run.targets) + " starting " +
           setText(s.run.startingTargets) + (s.run.restricted ? " restricted" : "");
}

// --- a monster's AI script run -------------------------------------------------------------------

namespace {

// Per battler, at twice the battler's index: where its turn part and counterattack part start (a
// word each, $FFFF for none), the blocks each part skipped when it last stopped at a wait (a byte
// each, the turn part's first; $FF for none), and the place each goes on from (a word each).
constexpr std::uint32_t kTurnPlace = kWorkRam + 0x3254;
constexpr std::uint32_t kCounterPlace = kWorkRam + 0x3268;
constexpr std::uint32_t kTurnSkipped = kWorkRam + 0x3240;
constexpr std::uint32_t kCounterSkipped = kWorkRam + 0x3241;
constexpr std::uint32_t kTurnWaitPlace = kWorkRam + 0x3D0C;
constexpr std::uint32_t kCounterWaitPlace = kWorkRam + 0x3D20;
constexpr std::uint16_t kNoPlace = 0xFFFF;
// Per battler, at twice the battler's index: its advance wait ($FF unset), and the first entry of
// its list of actions and of counterattacks ($FF for none).
constexpr std::uint32_t kAdvanceWait = kWorkRam + 0x322C;
constexpr std::uint32_t kActionList = kWorkRam + 0x32CC;
constexpr std::uint32_t kCounterList = kWorkRam + 0x32CD;
constexpr std::uint8_t kUnsetWait = 0xFF;
// The actions waiting (kActionNext, kActionCommand, kActionTargets and kActionMp above): 128 entries.
constexpr std::uint32_t kActionEntries = 128;
constexpr std::uint8_t kNoEntry = 0xFF;
// Where each spell sits in a party member's spell list, one byte per attack ($FF: not in it).
constexpr std::uint32_t kSpellListPlaces = kWorkRam + 0x3084;
constexpr std::uint32_t kSpellListPlaceCount = 256;
// The counterattack line: the battlers in it, at twice their index, and where it starts and ends.
constexpr std::uint32_t kCounterQueue = kWorkRam + 0x3920;
constexpr std::uint32_t kCounterQueueStart = kWorkRam + 0x3A68;
constexpr std::uint32_t kCounterQueueEnd = kWorkRam + 0x3A69;
// The battle program's flags byte: bit 0, a counterattack is running.
constexpr std::uint32_t kBattleFlags = kWorkRam + 0x00B1;
constexpr std::uint8_t kCounterattackRunningBit = 0x01;
// The battlers whose counterattack part has not run this step, the battlers that died this action
// and may answer it, and the battlers not attacked by another this step (a word each).
constexpr std::uint32_t kCounterAiNotRun = kWorkRam + 0x33FC;
constexpr std::uint32_t kDiedAwaitingCounter = kWorkRam + 0x3A56;
constexpr std::uint32_t kNotAttackedThisStep = kWorkRam + 0x33FE;
// Quick's turns left (its battler is kQuickTarget above).
constexpr std::uint32_t kQuickTurnsLeft = kWorkRam + 0x3402;
// Per battler, at twice the battler's index: the statuses it answers an attack with (a word for
// the first two bytes, a word for the last two), and a party member's record among the sixteen.
constexpr std::uint32_t kCounterStatus12 = kWorkRam + 0x3E60;
constexpr std::uint32_t kCounterStatus34 = kWorkRam + 0x3E74;
constexpr std::uint32_t kRecordIndex = kWorkRam + 0x3ED9;
// The commands' cells (and kEndOfBattleEvent above, twice the event's number): each battler's ATB
// rate (a word at twice its index), each character record's party byte (the party in bits 0-2, the
// slot in bits 3-4, the back row in bit 5, enabled in bit 6, shown in bit 7) and the party played.
constexpr std::uint32_t kAtbRate = kWorkRam + 0x3AC8;
constexpr std::uint32_t kPartyBytes = kWorkRam + 0x1850;
constexpr std::uint32_t kPartyPlayed = kWorkRam + 0x1A6D;
constexpr std::uint8_t kPartyNumberBits = 0x07;
constexpr int kPartySlotShift = 3;
constexpr std::uint8_t kPartySlotBits = 0x03;
constexpr std::uint8_t kPartyRowBit = 0x20;
constexpr std::uint8_t kRecordEnabledBit = 0x40;
constexpr std::uint8_t kRecordShownBit = 0x80;
// The monsters an attack may be aimed at as the AI leaves them (a bit set: it may be), and the
// second reach cell, which the AI's effects leave alone.
constexpr std::uint32_t kAiTargetable = kWorkRam + 0x2F46;
constexpr std::uint32_t kReachSecondCell = kWorkRam + 0x3408;
constexpr std::uint32_t kAiRestrictedOnEntry = kWorkRam + 0x3A98;
// The cells a command reads its operands from: the attack byte, then the targets word.
constexpr std::uint32_t kCommandAttack = kWorkRam + 0x00B6;
constexpr std::uint32_t kCommandTargets = kWorkRam + 0x00B8;
constexpr std::uint8_t kMonsterSlotBits = 0x3F;
// A command's start sets the sixteen cells from kLastSpell.
constexpr std::uint32_t kPerCommandCells = 16;

// The command each AI-only action goes out as.
constexpr std::uint8_t kFormationChangeCommand = 0x20;
constexpr std::uint8_t kMonsterDialogCommand = 0x21;
constexpr std::uint8_t kBattleEventCommand = 0x23;
constexpr std::uint8_t kMonsterVisibilityCommand = 0x24;
constexpr std::uint8_t kMonsterAnimationCommand = 0x2B;
constexpr std::uint8_t kVariableCommand = 0x2E;
constexpr std::uint8_t kSwitchCommand = 0x2F;
constexpr std::uint8_t kEffectCommand = 0x30;
// The steps that open with the three commands' own bytes.
constexpr std::uint8_t kVariableStep = 0xF8;
constexpr std::uint8_t kSwitchStep = 0xF9;

// Every step's place in the cartridge's script block, from the fixture's parts and the steps' sizes.
const std::map<const AiStep*, std::uint16_t>& stepPlaces() {
    static const std::map<const AiStep*, std::uint16_t> places = [] {
        std::map<const AiStep*, std::uint16_t> map;
        for (const test::ExpectedAiScript& row : test::kExpectedAiScripts) {
            const AiScript& script = aiScript(row.id);
            const auto add = [&](std::span<const AiStep> part, std::uint16_t start) {
                std::uint16_t at = start;
                for (const AiStep& step : part) {
                    map.emplace(&step, at);
                    at = static_cast<std::uint16_t>(at + test::aiStepBytes(step).size());
                }
            };
            add(script.turn, row.turnOffset);
            if (row.counterOffset) {
                add(script.counter, *row.counterOffset);
            }
        }
        return map;
    }();
    return places;
}

std::uint16_t placeOf(const AiStep* step) {
    if (step == nullptr) {
        return 0;
    }
    const auto found = stepPlaces().find(step);
    if (found == stepPlaces().end()) {
        throw std::logic_error("a script place that is no step of the cartridge's scripts");
    }
    return found->second;
}

std::uint16_t partPlace(std::span<const AiStep> part) {
    return part.empty() ? kNoPlace : placeOf(part.data());
}

// The step at `place` in the cartridge's script block; none for 0, the place of a part that has
// never stopped at a wait (no step after a wait is the block's first).
const AiStep* stepAt(std::uint16_t place) {
    if (place == 0) {
        return nullptr;
    }
    for (const auto& [step, at] : stepPlaces()) {
        if (at == place) {
            return step;
        }
    }
    throw std::runtime_error("a script place, " + std::to_string(place) + ", that is no step of the cartridge's scripts");
}

// The script whose turn part starts at `place`, if one does.
std::optional<std::uint16_t> scriptWithTurnAt(std::uint16_t place) {
    for (const test::ExpectedAiScript& row : test::kExpectedAiScripts) {
        if (row.turnOffset == place) {
            return row.id;
        }
    }
    return std::nullopt;
}

// The script part that starts at `place`: a script's turn part, or its counterattack part; none for
// $FFFF. Mag Roader's counterattack part starts where Wild Cat's turn part does, and is the same steps.
std::span<const AiStep> partAt(std::uint16_t place) {
    if (place == kNoPlace) {
        return {};
    }
    for (const test::ExpectedAiScript& row : test::kExpectedAiScripts) {
        const AiScript& script = aiScript(row.id);
        if (row.turnOffset == place) {
            return script.turn;
        }
        if (row.counterOffset && *row.counterOffset == place) {
            return script.counter;
        }
    }
    throw std::runtime_error("a script place, " + std::to_string(place) + ", where no script part starts");
}

// An action's four values as the cartridge's lists hold them, with none for the bytes its command
// never reads.
struct ActionCells {
    std::uint8_t command = 0;
    std::optional<std::uint8_t> attack;
    std::optional<std::uint8_t> targetsLow;
    std::optional<std::uint8_t> targetsHigh;
    std::uint8_t mpCost = 0;
};

std::string cellsText(const ActionCells& cells) {
    const auto byteText = [](const std::optional<std::uint8_t>& value) {
        return value ? std::to_string(*value) : std::string{"--"};
    };
    return std::to_string(cells.command) + " " + byteText(cells.attack) + " " + byteText(cells.targetsLow) + "/" +
           byteText(cells.targetsHigh) + " " + std::to_string(cells.mpCost);
}

// The cells a command reads, given everything the list holds for it: a dialog and a battle event
// read only the attack byte, a variable change and an effect its low targets byte as well.
ActionCells readCells(std::uint8_t command, std::uint8_t attack, std::uint16_t targets, std::uint8_t mpCost) {
    ActionCells cells{.command = command, .attack = attack, .mpCost = mpCost};
    if (command == kMonsterDialogCommand || command == kBattleEventCommand) {
        return cells;
    }
    cells.targetsLow = static_cast<std::uint8_t>(targets & 0xFF);
    if (command != kVariableCommand && command != kEffectCommand) {
        cells.targetsHigh = static_cast<std::uint8_t>(targets >> 8);
    }
    return cells;
}

// The bytes of the step a script would hold for an action of its own.
std::vector<std::uint8_t> stepBytesFor(const battle::BattleVariableAction& a) {
    switch (a.change) {
    case AiVariableChange::Set:
        return test::aiStepBytes(AiStep::setVariable({.variable = a.variable, .value = a.amount}));
    case AiVariableChange::Add:
        return test::aiStepBytes(AiStep::addToVariable({.variable = a.variable, .amount = a.amount}));
    default:
        return test::aiStepBytes(AiStep::subtractFromVariable({.variable = a.variable, .amount = a.amount}));
    }
}

std::vector<std::uint8_t> stepBytesFor(const battle::BattleSwitchAction& a) {
    return test::aiStepBytes(AiStep{ai::ChangeSwitch{a.which, a.change}});
}

std::vector<std::uint8_t> stepBytesFor(const battle::AiEffectAction& a) {
    return test::aiStepBytes(AiStep{ai::Effect{.effect = a.effect, .target = a.target, .status = a.status}});
}

// A step that queues an action of its own puts its first operand in the attack byte and the next
// two in the targets word.
ActionCells stepCells(std::uint8_t command, const std::vector<std::uint8_t>& bytes) {
    const auto at = [&](std::size_t i) -> std::uint8_t { return i < bytes.size() ? bytes[i] : 0; };
    return readCells(command, at(1), static_cast<std::uint16_t>(at(2) | at(3) << 8), 0);
}

const TextCorpus& noText() {
    static const TextCorpus corpus{std::array<std::vector<std::uint8_t>, kTextClassCount>{}};
    return corpus;
}

AiRunResult portRunResult(const battle::BattleState& state, battle::BattlerSlot actor) {
    const battle::Battler& battler = state.at(actor);
    AiRunResult result;
    for (const battle::Action& action : battler.actions) {
        result.actions.push_back(listedActionText(action));
    }
    for (const battle::Action& action : battler.counterActions) {
        result.counterActions.push_back(listedActionText(action));
    }
    result.advanceWait = battler.advanceWait;
    result.script = portScriptPlaces(state, actor);
    result.targetingTarget = battler.targetingTarget;
    result.counterAiRanThisStep = state.counterAiRanThisStep;
    result.diedAwaitingCounter = state.diedAwaitingCounter;
    result.attackedThisStep = state.attackedThisStep;
    for (std::size_t i = 0; i < state.counterQueue.size(); ++i) {
        result.counterQueue.push_back(state.counterQueue.at(i).index());
    }
    result.rngPosition = state.rng.position();
    return result;
}

}  // namespace

std::string listedActionText(const battle::Action& action) {
    return std::visit(
        [](const auto& a) -> std::string {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, battle::CommandAction>) {
                return cellsText(readCells(static_cast<std::uint8_t>(a.command), static_cast<std::uint8_t>(a.attack),
                                           maskOf(a.targets), a.mpCost));
            } else if constexpr (std::is_same_v<T, battle::FormationChange>) {
                return cellsText(stepCells(kFormationChangeCommand,
                                           test::aiStepBytes(AiStep::changeBattle({.formation = a.formation,
                                                                                    .exit = a.exit,
                                                                                    .restoreMonsters = a.restoreMonsters,
                                                                                    .scrollBackground = a.scrollBackground}))));
            } else if constexpr (std::is_same_v<T, battle::MonsterDialogAction>) {
                return cellsText(stepCells(kMonsterDialogCommand, test::aiStepBytes(AiStep::dialog(a.dialog))));
            } else if constexpr (std::is_same_v<T, battle::BattleEventAction>) {
                return cellsText(stepCells(kBattleEventCommand, test::aiStepBytes(AiStep::battleEvent(a.event))));
            } else if constexpr (std::is_same_v<T, battle::MonsterVisibilityAction>) {
                return cellsText(readCells(kMonsterVisibilityCommand, static_cast<std::uint8_t>(a.type),
                                           static_cast<std::uint16_t>(static_cast<std::uint8_t>(a.mode) |
                                                                      a.monsters.monsters.bits << 8),
                                           0));
            } else if constexpr (std::is_same_v<T, battle::MonsterAnimationAction>) {
                const bool sound = a.animation == MonsterAnimation::SoundEffect;
                const std::uint16_t targets =
                    sound ? static_cast<std::uint16_t>(a.pan | a.soundEffect << 8) : a.monsters.monsters.bits;
                return cellsText(readCells(kMonsterAnimationCommand, static_cast<std::uint8_t>(a.animation), targets, 0));
            } else if constexpr (std::is_same_v<T, battle::BattleVariableAction>) {
                return cellsText(stepCells(kVariableCommand, stepBytesFor(a)));
            } else if constexpr (std::is_same_v<T, battle::BattleSwitchAction>) {
                return cellsText(stepCells(kSwitchCommand, stepBytesFor(a)));
            } else if constexpr (std::is_same_v<T, battle::AiEffectAction>) {
                return cellsText(stepCells(kEffectCommand, stepBytesFor(a)));
            } else {
                return "an action no script queues";
            }
        },
        action);
}

AiScriptPlaces portScriptPlaces(const battle::BattleState& state, battle::BattlerSlot slot) {
    const battle::Battler& battler = state.at(slot);
    return AiScriptPlaces{.turn = partPlace(battler.turnScript),
                          .counter = partPlace(battler.counterScript),
                          .turnSkipped = battler.turnResume.blocksSkipped,
                          .turnWaitPlace = placeOf(battler.turnResume.at),
                          .counterSkipped = battler.counterResume.blocksSkipped,
                          .counterWaitPlace = placeOf(battler.counterResume.at)};
}

std::string differences(const AiScriptPlaces& port, const AiScriptPlaces& guest) {
    EffectCells ours;
    EffectCells theirs;
    const auto put = [](EffectCells& out, const AiScriptPlaces& places) {
        out.put("turn part", places.turn);
        out.put("counterattack part", places.counter);
        out.put("turn's blocks skipped", places.turnSkipped);
        out.put("turn's wait place", places.turnWaitPlace);
        out.put("counterattack's blocks skipped", places.counterSkipped);
        out.put("counterattack's wait place", places.counterWaitPlace);
    };
    put(ours, port);
    put(theirs, guest);
    return cellDifferences(ours.cells, theirs.cells);
}

AiRunResult portAiTurn(battle::BattleState& state, battle::BattlerSlot actor) {
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, noText(), MonsterSprites{}};
    state.counterattackRunning = false;
    battle::runAiTurn(state, executor, actor);
    return portRunResult(state, actor);
}

AiRunResult portAiTurn(AiScenario scenario) {
    return portAiTurn(scenario.state, scenario.actor);
}

AiRunResult portAiCounter(AiScenario scenario) {
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, noText(), MonsterSprites{}};
    scenario.state.counterattackRunning = true;
    battle::runAiCounter(scenario.state, executor, scenario.actor, scenario.answering);
    return portRunResult(scenario.state, scenario.actor);
}

std::string differences(const AiRunResult& port, const AiRunResult& guest) {
    EffectCells ours;
    EffectCells theirs;
    const auto put = [](EffectCells& out, const AiRunResult& result) {
        const auto list = [](const std::vector<std::string>& actions) {
            std::string text;
            for (const std::string& action : actions) {
                text += "[" + action + "]";
            }
            return text;
        };
        out.put("actions", list(result.actions));
        out.put("counterattacks", list(result.counterActions));
        out.put("advance wait", result.advanceWait ? std::to_string(*result.advanceWait) : std::string{"unset"});
        out.put("turn part", result.script.turn);
        out.put("counterattack part", result.script.counter);
        out.put("turn's blocks skipped", result.script.turnSkipped);
        out.put("turn's wait place", result.script.turnWaitPlace);
        out.put("counterattack's blocks skipped", result.script.counterSkipped);
        out.put("counterattack's wait place", result.script.counterWaitPlace);
        out.put("Targetting", markText(result.targetingTarget));
        out.put("counterattack parts run", setText(result.counterAiRanThisStep));
        out.put("died awaiting a counterattack", setText(result.diedAwaitingCounter));
        out.put("attacked this step", setText(result.attackedThisStep));
        std::string line;
        for (const int index : result.counterQueue) {
            line += std::to_string(index) + " ";
        }
        out.put("counterattack line", line);
        out.put("random-number position", result.rngPosition);
    };
    put(ours, port);
    put(theirs, guest);
    return cellDifferences(ours.cells, theirs.cells);
}

void DamageOracle::writeAiScriptCells(const battle::BattleState& state) {
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        const auto at = static_cast<std::uint32_t>(index * 2);
        const AiScriptPlaces places = portScriptPlaces(state, slot);
        writeWord(kTurnPlace + at, places.turn);
        writeWord(kCounterPlace + at, places.counter);
        writeByte(kTurnSkipped + at, places.turnSkipped);
        writeByte(kCounterSkipped + at, places.counterSkipped);
        writeWord(kTurnWaitPlace + at, places.turnWaitPlace);
        writeWord(kCounterWaitPlace + at, places.counterWaitPlace);
    }
}

void DamageOracle::emptyActionLists(battle::BattlerSlot actor) {
    const std::vector<std::uint8_t> free(kActionEntries, kNoEntry);
    writeBytes(retropp::MemoryRegion{.at = kActionNext, .size = kActionEntries}, free);
    const auto at = static_cast<std::uint32_t>(actor.index() * 2);
    writeByte(kActionList + at, kNoEntry);
    writeByte(kCounterList + at, kNoEntry);
    writeByte(kCounterQueueStart, 0);
    writeByte(kCounterQueueEnd, 0);
}

void DamageOracle::writeAiRunCells(const AiScenario& scenario, bool counter) {
    const battle::BattleState& state = scenario.state;
    // The aim's cells first, so the AI's own reach flag and battler cells are the ones that stand.
    writeAimCells(DamageScenario{.state = state, .attacker = scenario.actor});
    writeAiCells(scenario);
    writeAiScriptCells(state);
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::Battler& battler = state.at(battle::BattlerSlot::fromIndex(index));
        const auto at = static_cast<std::uint32_t>(index * 2);
        writeByte(kAdvanceWait + at, battler.advanceWait.value_or(kUnsetWait));
        writeByte(kActionList + at, kNoEntry);
        writeByte(kCounterList + at, kNoEntry);
        writeWord(kCounterStatus12 + at,
                  static_cast<std::uint16_t>(battler.statusForCounter.bytes[0] | battler.statusForCounter.bytes[1] << 8));
        writeWord(kCounterStatus34 + at,
                  static_cast<std::uint16_t>(battler.statusForCounter.bytes[2] | battler.statusForCounter.bytes[3] << 8));
    }
    emptyActionLists(scenario.actor);
    // No party member's spell list holds a spell: a party member's spell costs nothing.
    const std::vector<std::uint8_t> noSpells(kSpellListPlaceCount, kNoEntry);
    writeBytes(retropp::MemoryRegion{.at = kSpellListPlaces, .size = kSpellListPlaceCount}, noSpells);
    writeByte(kBattleFlags, counter ? kCounterattackRunningBit : 0);
    writeWord(kCounterAiNotRun, static_cast<std::uint16_t>(~maskOf(state.counterAiRanThisStep)));
    writeWord(kDiedAwaitingCounter, maskOf(state.diedAwaitingCounter));
    writeWord(kNotAttackedThisStep, static_cast<std::uint16_t>(~maskOf(state.attackedThisStep)));
    writeByte(kQuickTarget, state.quickTarget ? static_cast<std::uint8_t>(slotIndex(*state.quickTarget)) : kNoOne);
    writeByte(kQuickTurnsLeft, state.quickTurnsLeft.value_or(0));
    writeWord(kAiRestrictedOnEntry, 0);
    writeWord(kTargets, counter ? maskOf(scenario.answering) : 0);
}

AiScriptPlaces DamageOracle::readScriptPlaces(battle::BattlerSlot slot) {
    const auto at = static_cast<std::uint32_t>(slot.index() * 2);
    return AiScriptPlaces{.turn = readWord(kTurnPlace + at),
                          .counter = readWord(kCounterPlace + at),
                          .turnSkipped = readByte(kTurnSkipped + at),
                          .turnWaitPlace = readWord(kTurnWaitPlace + at),
                          .counterSkipped = readByte(kCounterSkipped + at),
                          .counterWaitPlace = readWord(kCounterWaitPlace + at)};
}

AiRunResult DamageOracle::readAiRunResult(battle::BattlerSlot actor) {
    const auto at = static_cast<std::uint32_t>(actor.index() * 2);
    const auto list = [&](std::uint32_t first) {
        std::vector<std::string> actions;
        std::uint8_t entry = readByte(first);
        for (std::uint32_t guard = 0; entry != kNoEntry && guard < kActionEntries; ++guard) {
            const std::uint32_t place = static_cast<std::uint32_t>(entry) * 2;
            actions.push_back(cellsText(readCells(readByte(kActionCommand + place), readByte(kActionCommand + place + 1),
                                                  readWord(kActionTargets + place), readByte(kActionMp + place))));
            const std::uint8_t next = readByte(kActionNext + entry);
            if (next == entry) {
                break;
            }
            entry = next;
        }
        return actions;
    };
    AiRunResult result;
    result.actions = list(kActionList + at);
    result.counterActions = list(kCounterList + at);
    const std::uint8_t wait = readByte(kAdvanceWait + at);
    if (wait != kUnsetWait) {
        result.advanceWait = wait;
    }
    result.script = readScriptPlaces(actor);
    result.targetingTarget = readTargetingTarget(actor);
    result.counterAiRanThisStep = setOf(static_cast<std::uint16_t>(~readWord(kCounterAiNotRun)));
    result.diedAwaitingCounter = setOf(readWord(kDiedAwaitingCounter));
    result.attackedThisStep = setOf(static_cast<std::uint16_t>(~readWord(kNotAttackedThisStep)));
    const std::uint8_t end = readByte(kCounterQueueEnd);
    for (std::uint8_t i = readByte(kCounterQueueStart); i < end; ++i) {
        result.counterQueue.push_back(readByte(kCounterQueue + i) / 2);
    }
    result.rngPosition = readByte(kRngPosition);
    return result;
}

AiScriptPlaces DamageOracle::readyAi(const AiScenario& scenario, battle::BattlerSlot slot) {
    writeAiScriptCells(scenario.state);
    const std::uint16_t script = scenario.state.at(slot).aiScript.value_or(0);
    call([&] { initAi_(script, slotIndex(slot), kWideIndexStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedInitAi);
    return readScriptPlaces(slot);
}

AiRunResult DamageOracle::aiTurn(const AiScenario& scenario) {
    writeAiRunCells(scenario, false);
    call([&] { execMonsterAction_(slotIndex(scenario.actor), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedExecMonsterAction);
    return readAiRunResult(scenario.actor);
}

AiRunResult DamageOracle::nextAiTurn(battle::BattlerSlot actor) {
    emptyActionLists(actor);
    writeWord(kTargets, 0);
    writeByte(kBattleFlags, 0);
    call([&] { execMonsterAction_(slotIndex(actor), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedExecMonsterAction);
    return readAiRunResult(actor);
}

AiRunResult DamageOracle::aiCounter(const AiScenario& scenario) {
    writeAiRunCells(scenario, true);
    call([&] { execAiRetal_(slotIndex(scenario.actor), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedExecAiRetal);
    return readAiRunResult(scenario.actor);
}

AiCommandResult portAiCommand(AiScenario scenario, const battle::Action& action) {
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, noText(), MonsterSprites{}};
    // Every action starts with the cells of the one before cleared, as running it clears them.
    scenario.state.scratch = battle::ActionScratch{};
    const std::vector<battle::BattleScriptCommand> script = executor.execute(scenario.state, scenario.actor, action);
    const battle::BattleState& state = scenario.state;
    AiCommandResult result;
    result.attack =
        portAttackCells(state, battle::AttackInFlight{}, script, nullptr, state.awayFromTheFieldUnreachable, false);
    result.battleVariables = state.battleVariables;
    result.switches = state.switches.variables;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        result.ownVariables[i] = battler.aiVariable;
        result.ownTimers[i] = battler.turnTimer;
        result.atbRates[i] = battler.atbRate;
        result.statuses[i] = battler.status;
        result.statusesToSet[i] = battler.statusesToSet;
        result.statusesToClear[i] = battler.statusesToClear;
    }
    result.invincible = state.invincible;
    result.aiUntargetable = state.aiUntargetable;
    result.battleCounter = state.battleCounter;
    result.endOfBattleEvent = state.endOfBattleEvent;
    result.gauRecruited = state.gauRecruited;
    result.membership = state.party.membership;
    result.rngPosition = state.rng.position();
    return result;
}

std::string differences(const AiCommandResult& port, const AiCommandResult& guest) {
    EffectCells ours;
    EffectCells theirs;
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    const auto put = [&](EffectCells& out, const AiCommandResult& result) {
        for (std::size_t i = 0; i < result.battleVariables.size(); ++i) {
            out.put("battle variable " + std::to_string(i), result.battleVariables[i]);
        }
        for (std::size_t i = 0; i < result.switches.size(); ++i) {
            out.put("switch byte " + std::to_string(i), result.switches[i]);
        }
        for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
            const std::string of = " of battler " + std::to_string(i);
            out.put("own variable" + of, result.ownVariables[i]);
            out.put("own timer" + of, result.ownTimers[i]);
            out.put("ATB rate" + of, result.atbRates[i]);
            out.put("statuses" + of, statuses(result.statuses[i]));
            out.put("statuses to set" + of, statuses(result.statusesToSet[i]));
            out.put("statuses to clear" + of, statuses(result.statusesToClear[i]));
        }
        out.put("invincible", setText(result.invincible));
        out.put("AI-untargetable", setText(result.aiUntargetable));
        out.put("battle timer", result.battleCounter);
        out.put("end-of-battle event", result.endOfBattleEvent ? static_cast<int>(*result.endOfBattleEvent) : 0);
        out.put("Gau recruited", result.gauRecruited);
        for (std::size_t i = 0; i < result.membership.size(); ++i) {
            const game::PartyMembership& m = result.membership[i];
            out.put("party of record " + std::to_string(i), std::to_string(m.party) + " slot " + std::to_string(m.slot) +
                                                                (m.backRow ? " back" : " front") +
                                                                (m.enabled ? " enabled" : " disabled") +
                                                                (m.visible ? " visible" : " hidden"));
        }
        out.put("random-number position", result.rngPosition);
    };
    put(ours, port);
    put(theirs, guest);
    return cellDifferences(ours.cells, theirs.cells) + differences(port.attack, guest.attack);
}

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

namespace {

// The battle's own actions' bytes as running a command reads them: which of kOwnActions runs it, the
// command, its attack byte and its targets.
struct OwnActionBytes {
    std::size_t routine = 0;
    std::uint8_t command = 0;
    std::uint8_t attack = 0;
    std::uint16_t targets = 0;
};

constexpr std::uint8_t commandByte(BattleCommandId command) {
    return static_cast<std::uint8_t>(command);
}

// Damage over time names the status that fired (rigenetimer, battle_main.asm:15000-15015): Poison's
// bit of the first status byte, Regen's of the fourth, and the bit Sap and Phantasm share.
std::uint8_t damageOverTimeByte(battle::DamageOverTimeKind kind) {
    switch (kind) {
    case battle::DamageOverTimeKind::Poison:
        return 0x04;
    case battle::DamageOverTimeKind::Regen:
        return 0x02;
    case battle::DamageOverTimeKind::Sap:
        return 0x40;
    }
    return 0;
}

// The timers that ran out, one bit each (timer, battle_main.asm:14933-14959): Stop, Reflect, Freeze
// and Sleep.
constexpr std::uint8_t kStopRanOut = 0x01;
constexpr std::uint8_t kReflectRanOut = 0x02;
constexpr std::uint8_t kFreezeRanOut = 0x04;
constexpr std::uint8_t kSleepRanOut = 0x08;

OwnActionBytes ownActionBytes(const battle::Action& action) {
    if (const auto* dot = std::get_if<battle::DamageOverTime>(&action)) {
        return {.routine = 0, .command = commandByte(battle::kDamageOverTimeCommand), .attack = damageOverTimeByte(dot->kind)};
    }
    if (const auto* spell = std::get_if<battle::SpellWithoutCaster>(&action)) {
        // The battler in the attack byte, the spell in the targets' first byte.
        return {.routine = 1,
                .command = commandByte(battle::kSpellWithoutCasterCommand),
                .attack = static_cast<std::uint8_t>(slotIndex(spell->target)),
                .targets = static_cast<std::uint8_t>(spell->spell)};
    }
    if (const auto* expired = std::get_if<battle::StatusTimersExpired>(&action)) {
        std::uint8_t bits = expired->stop ? kStopRanOut : 0;
        bits |= expired->reflect ? kReflectRanOut : 0;
        bits |= expired->freeze ? kFreezeRanOut : 0;
        bits |= expired->sleep ? kSleepRanOut : 0;
        return {.routine = 2,
                .command = commandByte(battle::kStatusTimersCommand),
                .attack = static_cast<std::uint8_t>(slotIndex(expired->battler)),
                .targets = bits};
    }
    if (const auto* seize = std::get_if<battle::SeizeDrain>(&action)) {
        return {.routine = 3, .command = commandByte(battle::kSeizeDrainCommand), .targets = maskOf(battle::BattlerSet::of(seize->seized))};
    }
    throw std::logic_error("ownAction() runs damage over time, a spell with no caster, status timers running out, "
                           "Seize's drain and a script's effect");
}

// Per battler, at twice its index: Poison's multiplier, which grows at each beat.
constexpr std::uint32_t kPoisonMultiplier = kWorkRam + 0x3E24;
// A battler with a damage-over-time action waiting (the stance byte), and one held by a status from
// taking it (the field byte).
constexpr std::uint8_t kDamageOverTimeWaitingBit = 0x10;
constexpr std::uint8_t kHeldByStatusBit = 0x10;

void putOwnActionCells(AttackResult& result, const std::array<bool, battle::kBattlerSlots>& waiting,
                       const std::array<int, battle::kBattlerSlots>& multiplier) {
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string of = " of battler " + std::to_string(i);
        result.cells["damage over time waiting" + of] = waiting[i] ? "1" : "0";
        result.cells["poison multiplier" + of] = std::to_string(multiplier[i]);
    }
}

}  // namespace

AttackResult DamageOracle::ownAction(const DamageScenario& scenario, const battle::Action& action) {
    if (std::holds_alternative<battle::AiEffectAction>(action)) {
        return aiCommand(AiScenario{.state = scenario.state, .actor = scenario.attacker}, action).attack;
    }
    const OwnActionBytes bytes = ownActionBytes(action);
    const battle::BattleState& state = scenario.state;
    battle::AttackInFlight starting;
    starting.attackerLevel = state.lastAttackLevel;
    writeAttackCells(DamageScenario{.state = state, .attack = starting, .attacker = scenario.attacker});
    writeCommandStart();
    writeByte(kEnemyCount, state.enemyCount);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        writeByte(kStance + at, static_cast<std::uint8_t>(readByte(kStance + at) |
                                                          (battler.dotActionPending ? kDamageOverTimeWaitingBit : 0)));
        writeByte(kOnTheField + at, static_cast<std::uint8_t>(readByte(kOnTheField + at) |
                                                              (battler.heldByStatus ? kHeldByStatusBit : 0)));
        writeByte(kPoisonMultiplier + at, static_cast<std::uint8_t>(battler.poisonMultiplier));
    }
    // The action's bytes where its command reads them, and the command and attack bytes the action
    // leaves for the battle program as it runs.
    writeByte(kCommand, bytes.command);
    writeByte(kCommandAttack, bytes.attack);
    writeWord(kCommandTargets, bytes.targets);
    writeByte(kQueuedCommand, bytes.command);
    writeByte(kQueuedAttack, bytes.attack);
    call([&] {
        ownActions_[bytes.routine](slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage);
    });
    AttackResult result = readAttackResult(false);
    std::array<bool, battle::kBattlerSlots> waiting{};
    std::array<int, battle::kBattlerSlots> multiplier{};
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        waiting[i] = (readByte(kStance + at) & kDamageOverTimeWaitingBit) != 0;
        multiplier[i] = readByte(kPoisonMultiplier + at);
    }
    putOwnActionCells(result, waiting, multiplier);
    return result;
}

AttackResult portOwnAction(DamageScenario scenario, const battle::Action& action) {
    if (std::holds_alternative<battle::AiEffectAction>(action)) {
        return portAiCommand(AiScenario{.state = std::move(scenario.state), .actor = scenario.attacker}, action).attack;
    }
    (void)ownActionBytes(action);
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, noText(), MonsterSprites{}};
    battle::BattleState& state = scenario.state;
    // Every action starts with the cells of the one before cleared, as running it clears them.
    state.scratch = battle::ActionScratch{};
    const std::vector<battle::BattleScriptCommand> script = executor.execute(state, scenario.attacker, action);
    AttackResult result =
        portAttackCells(state, battle::AttackInFlight{}, script, nullptr, state.awayFromTheFieldUnreachable, false);
    std::array<bool, battle::kBattlerSlots> waiting{};
    std::array<int, battle::kBattlerSlots> multiplier{};
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        waiting[i] = state.battlers[i].dotActionPending;
        multiplier[i] = state.battlers[i].poisonMultiplier;
    }
    putOwnActionCells(result, waiting, multiplier);
    return result;
}

AiScenario drawAiRunScenario(std::mt19937& rng, std::uint16_t script, battle::BattlerSlot actor, CounterGuard guard) {
    AiScenario s = drawAiScenario(rng);
    s.actor = actor;
    s.run = {};
    battle::BattleState& state = s.state;
    // The battler's aim reads the reach flag the way the shipped aim does only while no one is
    // away from the field.
    if (!state.awayFromTheFieldUnreachable) {
        state.awayFromTheField = {};
    }
    battle::Battler& battler = state.at(actor);
    battler.present = true;
    if (actor.isMonster() && !battler.monster) {
        battler.monster = static_cast<MonsterId>(rng() % kMonsterCount);
    }
    if (oneIn(rng, 2)) {
        battler.advanceWait = static_cast<std::uint8_t>(upTo(rng, 20));
    }
    battler.aiScript = script;
    battle::readyAi(state, actor);

    // The other battlers' marks in the three sets, and the actor's on the guard's path.
    state.counterAiRanThisStep = anyBattlers(rng).without(battle::BattlerSet::of(actor));
    state.diedAwaitingCounter = anyBattlers(rng).without(battle::BattlerSet::of(actor));
    state.attackedThisStep = anyBattlers(rng).without(battle::BattlerSet::of(actor));
    battle::BattlerSlot other = anySlot(rng);
    while (other == actor) {
        other = anySlot(rng);
    }
    switch (guard) {
    case CounterGuard::AlreadyRan:
        state.counterAiRanThisStep.add(actor);
        break;
    case CounterGuard::DiedAfterAnAttack:
        state.diedAwaitingCounter.add(actor);
        state.attackedThisStep.add(actor);
        break;
    case CounterGuard::DiedUnattacked:
        state.diedAwaitingCounter.add(actor);
        break;
    case CounterGuard::QuickForAnother:
        state.quickTarget = other;
        state.quickTurnsLeft = static_cast<std::uint8_t>(1 + upTo(rng, 2));
        break;
    case CounterGuard::HeldByAStatus: {
        constexpr std::array<StatusId, 5> held{StatusId::SLEEP, StatusId::CONFUSE, StatusId::BERSERK, StatusId::STOP,
                                              StatusId::FROZEN};
        battler.statusForCounter.set(held[rng() % held.size()]);
        break;
    }
    case CounterGuard::Charmed:
        battler.charmedBy = other;
        break;
    case CounterGuard::Free:
        break;
    case CounterGuard::QuickForItself:
        state.quickTarget = actor;
        state.quickTurnsLeft = static_cast<std::uint8_t>(1 + upTo(rng, 2));
        break;
    }
    if (oneIn(rng, 4)) {
        s.answering = {};
    } else {
        s.answering = battle::BattlerSet::of(other);
    }
    return s;
}

AiCommandCase drawAiCommandCase(std::mt19937& rng) {
    AiCommandCase c{.scenario = drawAiScenario(rng), .action = battle::NoAction{}};
    battle::BattleState& state = c.scenario.state;
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        state.party.membership[record] = game::PartyMembership{.party = static_cast<std::uint8_t>(upTo(rng, 3)),
                                                               .slot = static_cast<std::uint8_t>(upTo(rng, 3)),
                                                               .backRow = oneIn(rng, 2),
                                                               .enabled = oneIn(rng, 2),
                                                               .visible = oneIn(rng, 2)};
    }
    state.party.currentParty = static_cast<std::uint8_t>(1 + upTo(rng, 2));
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        battle::Battler& battler = state.at(battle::BattlerSlot::fromIndex(index));
        battler.atbRate = static_cast<std::uint16_t>(rng() & 0xFFFF);
        if (index < battle::kPartySlots && battler.actor) {
            battler.record = rng() % game::kCharacterRecords;
        }
    }
    state.invincible = anyBattlers(rng);
    state.aiUntargetable = anyBattlers(rng).monstersOnly();
    if (oneIn(rng, 3)) {
        state.endOfBattleEvent = static_cast<battle::EndOfBattleEvent>(1 + upTo(rng, 5));
    }
    const auto amount = [&] {
        return oneIn(rng, 4) ? static_cast<std::uint8_t>(upTo(rng, 1)) : static_cast<std::uint8_t>(upTo(rng, 63));
    };
    switch (rng() % 3) {
    case 0:
        c.action = battle::BattleVariableAction{.variable = anyAiVariable(rng),
                                                .change = static_cast<AiVariableChange>(rng() % 3),
                                                .amount = amount()};
        break;
    case 1:
        c.action = battle::BattleSwitchAction{.which = anyAiSwitch(rng), .change = static_cast<AiSwitchChange>(rng() % 3)};
        break;
    default: {
        constexpr std::array<AiEffect, 14> effects{
            AiEffect::ResetOwnTimer,  AiEffect::MakeInvincible, AiEffect::EndBattle,       AiEffect::RecruitGau,
            AiEffect::ResetBattleTimer, AiEffect::EndInvincible, AiEffect::MakeTargetable, AiEffect::MakeUntargetable,
            AiEffect::FillGauge,      AiEffect::EndBattleLearningRages, AiEffect::Nothing, AiEffect::SetStatus,
            AiEffect::ClearStatus,    AiEffect::HideSelf};
        const AiEffect effect = effects[rng() % effects.size()];
        c.action = battle::AiEffectAction{.effect = effect, .target = anyAiTarget(rng), .status = anyStatus(rng)};
        // Gau is recruited by a party member's script: the record it joins is the party member's.
        if (effect == AiEffect::RecruitGau) {
            c.scenario.actor = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
            battle::Battler& gau = state.at(c.scenario.actor);
            gau.actor = gau.actor.value_or(CharacterPropId::GAU);
            gau.record = rng() % game::kCharacterRecords;
        }
        break;
    }
    }
    return c;
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

// --- replaying a battler's AI --------------------------------------------------------------------

AiScenario DamageOracle::readAiScenario(battle::BattlerSlot actor) {
    AiScenario s;
    s.actor = actor;
    battle::BattleState& state = s.state;
    const battle::BattlerSet enemies = setOf(readWord(kEnemyCharacters));
    const std::uint8_t notSeized = readByte(kReachCharacters);
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        battle::Battler& battler = state.at(slot);
        const auto at = static_cast<std::uint32_t>(index * 2);
        if (slot.isCharacter()) {
            // A party slot's member and record, none negative.
            const std::uint8_t member = readByte(kActor + at);
            battler.actor = (member & kTopBit) != 0 ? std::nullopt
                                                    : std::optional<CharacterPropId>(static_cast<CharacterPropId>(member));
            const std::uint8_t record = readByte(kRecordIndex + at);
            battler.record = (record & kTopBit) != 0 ? std::nullopt : std::optional<std::size_t>(record);
            // A party member held in Seize is out of reach; who holds it the reach check does not read.
            if ((notSeized & 1u << slot.position()) == 0) {
                battler.seizedBy = battle::BattlerSlot::monster(0);
            }
            battler.actsAsEnemy = enemies.has(slot);
        } else {
            const std::uint16_t monster = readWord(kMonsterIndex + at);
            battler.monster =
                (monster & 0x8000) != 0 ? std::nullopt : std::optional<MonsterId>(static_cast<MonsterId>(monster));
        }
        const std::uint8_t field = readByte(kOnTheField + at);
        battler.present = (field & kPresentBit) != 0;
        battler.inBattleMenu = (field & kInBattleMenuBit) != 0;
        battler.menuMayOpen = (field & kMenuMayOpenBit) != 0;
        const auto statuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{
                {readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at), readByte(first34 + at + 1)}};
        };
        battler.status = statuses(kStatus12, kStatus34);
        battler.statusesToSet = statuses(kToSet12, kToSet34);
        battler.statusesToClear = statuses(kToClear12, kToClear34);
        battler.statusForCounter = statuses(kCounterStatus12, kCounterStatus34);
        battler.hp = readWord(kHp + at);
        battler.mp = readWord(kMp + at);
        battler.level = readByte(kLevel + at);
        battler.elementsWeak.bits = readByte(kWeakHalved + at);
        const std::uint8_t mark = readByte(kLastAttacker + at);
        const int lastAttacker = mark & ~kWaitingBit;
        battler.lastAttacker = lastAttacker < battle::kBattlerSlots
                                   ? std::optional<battle::BattlerSlot>(battle::BattlerSlot::fromIndex(lastAttacker))
                                   : std::nullopt;
        battler.retaliationPending = (mark & kWaitingBit) != 0;
        battler.targetingTarget = slotOrNone(readByte(kTargetingTarget + at));
        const auto hit = [&](std::uint32_t byCell, std::uint32_t withCell, auto with) {
            using With = decltype(with);
            const std::optional<battle::BattlerSlot> who = slotOrNone(readByte(byCell + at));
            return who ? std::optional<battle::Hit<With>>(battle::Hit<With>{.by = *who, .with = With{readByte(withCell + at)}})
                       : std::nullopt;
        };
        battler.lastHit.command = hit(kLastHitCommandBy, kLastHitCommand, BattleCommandId{});
        battler.lastHit.spell = hit(kLastHitSpellBy, kLastHitSpell, AttackId{});
        battler.lastHit.item = hit(kLastHitItemBy, kLastHitItem, ItemId{});
        if (const std::optional<battle::BattlerSlot> who = slotOrNone(readByte(kLastHitElementBy + at))) {
            battler.lastHit.element = battle::Hit<ElementSet>{.by = *who, .with = ElementSet{readByte(kLastHitElement + at)}};
        }
        battler.lastDamagedBy = slotTimesTwo(readByte(kLastDamagedBy + at));
        battler.aiVariable = readByte(kAiVariable + at);
        battler.turnTimer = readWord(kOwnTimer + at);
        battler.atbRate = readWord(kAtbRate + at);
        battler.charmedBy = slotOrNone(readByte(kCharmedBy + at));
        battler.stamina = readByte(kStamina + at);
        battler.evadeFactor = readByte(kEvadeAndMagicBlock + at);
        battler.magicBlockFactor = readByte(kEvadeAndMagicBlock + at + 1);
        battler.physicalBlockGraphics.bits = readByte(kBlockGraphics + at);
        battler.magicBlockGraphics.bits = readByte(kBlockGraphics + at + 1);
        battler.triggers.retort = (readByte(kRetort + at) & 1) != 0;
        const std::uint8_t wait = readByte(kAdvanceWait + at);
        battler.advanceWait = wait == kUnsetWait ? std::nullopt : std::optional<std::uint8_t>(wait);
        battler.turnScript = partAt(readWord(kTurnPlace + at));
        battler.counterScript = partAt(readWord(kCounterPlace + at));
        battler.hasCounterScript = !battler.counterScript.empty();
        battler.turnResume = battle::AiResume{.blocksSkipped = readByte(kTurnSkipped + at),
                                              .at = stepAt(readWord(kTurnWaitPlace + at))};
        battler.counterResume = battle::AiResume{.blocksSkipped = readByte(kCounterSkipped + at),
                                                 .at = stepAt(readWord(kCounterWaitPlace + at))};
    }

    // The reach check's cells as the game keeps them: on the field or about to be, the second cell
    // (counted in the battle), the monsters the AI has not hidden and the party members not seized
    // (above).
    state.targetableOrPresent = setOf(readWord(kReachFirst));
    state.countedInBattle = setOf(readWord(kReachSecond));
    state.aiUntargetable =
        battle::BattlerSet{BattleSlotMask{}, BattleSlotMask(static_cast<std::uint8_t>(~readByte(kAiTargetable) & kMonsterSlotBits))};
    state.scratch.countersEnabled = (readByte(kCountersEnabled) & kCountersOn) != 0;
    state.awayFromTheFieldUnreachable = (readByte(kReachKeepsAway) & kTopBit) != 0;
    state.awayFromTheField = setOf(readWord(kAwayFromTheField));
    state.battleType = static_cast<battle::BattleType>(readByte(kBattleType));
    state.pincerSides[0] = BattleSlotMask(readByte(kPincerFirstSide));
    state.pincerSides[1] = BattleSlotMask(readByte(kPincerSecondSide));

    state.alive = setOf(readWord(kAlive));
    state.allyCount = readByte(kAllyCount);
    state.enemyCount = readByte(kEnemyCount);
    state.battleCounter = readWord(kBattleTimer);
    for (std::size_t i = 0; i < state.battleVariables.size(); ++i) {
        state.battleVariables[i] = readByte(kBattleVariables + static_cast<std::uint32_t>(i));
    }
    for (std::size_t i = 0; i < state.switches.variables.size(); ++i) {
        state.switches.variables[i] = readByte(kBattleSwitches + static_cast<std::uint32_t>(i));
    }
    state.monsterNamesListed = readByte(kNamesListed);
    state.gauRecruited = (readByte(kStoryFlags) & kGauRecruitedBit) != 0;
    state.formation = static_cast<FormationId>(readWord(kFormation));
    state.rng = battle::BattleRng{readByte(kRngPosition)};
    state.invincible = setOf(readWord(kInvincible));
    const std::uint8_t event = readByte(kEndOfBattleEvent);
    state.endOfBattleEvent =
        event == 0 ? std::nullopt : std::optional<battle::EndOfBattleEvent>(static_cast<battle::EndOfBattleEvent>(event / 2));
    state.party.currentParty = readByte(kPartyPlayed);
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        const std::uint8_t value = readByte(kPartyBytes + static_cast<std::uint32_t>(record));
        state.party.membership[record] =
            game::PartyMembership{.party = static_cast<std::uint8_t>(value & kPartyNumberBits),
                                  .slot = static_cast<std::uint8_t>(value >> kPartySlotShift & kPartySlotBits),
                                  .backRow = (value & kPartyRowBit) != 0,
                                  .enabled = (value & kRecordEnabledBit) != 0,
                                  .visible = (value & kRecordShownBit) != 0};
    }

    state.counterattackRunning = (readByte(kBattleFlags) & kCounterattackRunningBit) != 0;
    state.counterAiRanThisStep = setOf(static_cast<std::uint16_t>(~readWord(kCounterAiNotRun)));
    state.diedAwaitingCounter = setOf(readWord(kDiedAwaitingCounter));
    state.attackedThisStep = setOf(static_cast<std::uint16_t>(~readWord(kNotAttackedThisStep)));
    state.quickTarget = slotOrNone(readByte(kQuickTarget));
    const std::uint8_t quickTurns = readByte(kQuickTurns);
    state.quickTurnsLeft = quickTurns == kNoQuick ? std::nullopt : std::optional<std::uint8_t>(quickTurns);
    const std::uint8_t end = readByte(kCounterQueueEnd);
    for (std::uint8_t i = readByte(kCounterQueueStart); i < end; ++i) {
        state.counterQueue.push(battle::BattlerSlot::fromIndex((readByte(kCounterQueue + i) / 2) % battle::kBattlerSlots));
    }

    s.run = battle::AiRun{.targets = setOf(readWord(kAiTargets)),
                          .startingTargets = setOf(readWord(kAiStartingTargets)),
                          .restricted = readByte(kAiRestricted) != 0};
    s.answering = setOf(readWord(kTargets));
    return s;
}

namespace {

// The battler whose per-battler cell, one word at twice its index from `first`, the call read; none
// when it read none, and an error when it read more than one battler's.
std::optional<battle::BattlerSlot> battlerRead(const TracedCall& call, std::uint32_t first, const char* what) {
    std::optional<battle::BattlerSlot> found;
    for (const auto& [cell, value] : call.inputs) {
        if (cell < first || cell >= first + 2 * battle::kBattlerSlots) {
            continue;
        }
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>((cell - first) / 2));
        if (found && *found != slot) {
            throw std::runtime_error(call.routine + " read more than one battler's " + what);
        }
        found = slot;
    }
    return found;
}

// The actions of `after` the call added: what follows the ones `before` held.
std::vector<std::string> added(std::vector<std::string> after, const std::vector<std::string>& before) {
    if (after.size() >= before.size() && std::equal(before.begin(), before.end(), after.begin())) {
        after.erase(after.begin(), after.begin() + static_cast<std::ptrdiff_t>(before.size()));
    }
    return after;
}

// One step of the scripts that queues one of the three commands, as the action it queues.
struct CommandStep {
    std::size_t command = 0;  // 0 the variable, 1 the switch, 2 the effect
    std::vector<std::uint8_t> bytes;
    battle::Action action;
};

// Every action of the three commands a step can hold: each variable set, raised or lowered by 0 to
// 63; each bit of a variable and each battle switch toggled, set or cleared; and each effect, with
// each target for the five that read one and each status for SetStatus and ClearStatus. Nothing,
// which no script holds, is among them: the cartridge's routine for it runs all the same.
const std::vector<CommandStep>& commandSteps() {
    static const std::vector<CommandStep> steps = [] {
        constexpr std::uint8_t kLargestAmount = 63;
        constexpr std::uint8_t kLastBit = 7;
        constexpr int kBattleSwitchCount = 160;
        constexpr int kStatusCount = 32;
        std::vector<CommandStep> all;
        std::vector<AiVariable> variables{AiVariable::own()};
        for (std::uint8_t index = 0; index < kAiBattleVariables; ++index) {
            variables.push_back(AiVariable::battle(index));
        }
        for (const AiVariable variable : variables) {
            for (const AiVariableChange change : {AiVariableChange::Set, AiVariableChange::Add, AiVariableChange::Subtract}) {
                for (std::uint8_t amount = 0; amount <= kLargestAmount; ++amount) {
                    const battle::BattleVariableAction a{.variable = variable, .change = change, .amount = amount};
                    all.push_back({0, stepBytesFor(a), a});
                }
            }
        }
        std::vector<AiSwitch> switches;
        for (std::uint8_t bit = 0; bit <= kLastBit; ++bit) {
            switches.push_back(AiSwitch::own(bit));
            for (std::uint8_t index = 0; index < kAiBattleVariables; ++index) {
                switches.push_back(AiSwitch::battleVariable(index, bit));
            }
        }
        for (int number = 0; number < kBattleSwitchCount; ++number) {
            switches.push_back(AiSwitch::battleSwitch(number));
        }
        for (const AiSwitch which : switches) {
            for (const AiSwitchChange change : {AiSwitchChange::Toggle, AiSwitchChange::Set, AiSwitchChange::Clear}) {
                const battle::BattleSwitchAction a{.which = which, .change = change};
                all.push_back({1, stepBytesFor(a), a});
            }
        }
        std::vector<AiTarget> targets;
        for (int character = 0; character < static_cast<int>(AiTargetSelector::MONSTER_SLOT_1); ++character) {
            targets.emplace_back(static_cast<CharacterPropId>(character));
        }
        for (int selector = static_cast<int>(AiTargetSelector::MONSTER_SLOT_1);
             selector <= static_cast<int>(AiTargetSelector::TARGETTING_TARGET); ++selector) {
            targets.emplace_back(static_cast<AiTargetSelector>(selector));
        }
        for (int number = 0; number <= static_cast<int>(AiEffect::HideSelf); ++number) {
            const auto effect = static_cast<AiEffect>(number);
            const bool namesATarget = effect == AiEffect::MakeInvincible || effect == AiEffect::EndInvincible ||
                                      effect == AiEffect::MakeTargetable || effect == AiEffect::MakeUntargetable ||
                                      effect == AiEffect::FillGauge;
            const bool namesAStatus = effect == AiEffect::SetStatus || effect == AiEffect::ClearStatus;
            if (namesATarget) {
                for (const AiTarget target : targets) {
                    const battle::AiEffectAction a{.effect = effect, .target = target};
                    all.push_back({2, stepBytesFor(a), a});
                }
            } else if (namesAStatus) {
                for (int status = 0; status < kStatusCount; ++status) {
                    const battle::AiEffectAction a{.effect = effect, .status = static_cast<StatusId>(status)};
                    all.push_back({2, stepBytesFor(a), a});
                }
            } else {
                const battle::AiEffectAction a{.effect = effect};
                all.push_back({2, stepBytesFor(a), a});
            }
        }
        return all;
    }();
    return steps;
}

}  // namespace

std::string DamageOracle::replayAiCall(const TracedCall& call, const TracedRoutine& routine) {
    // The call's reads, and the cells kept with them: a monster slot holds a monster in two cells
    // the battle keeps together, the held set and the high byte of the monster's index, and the
    // one the call did not read follows the one it did.
    const auto overlay = [this](const std::map<std::uint32_t, std::uint8_t>& cells) {
        for (const auto& [cell, value] : cells) {
            writeByte(cell, value);
        }
    };
    const auto overlayReads = [&] {
        overlay(call.inputs);
        const bool heldRead = call.inputs.contains(kMonstersHeld);
        auto held = readByte(kMonstersHeld);
        for (int position = 0; position < battle::kMonsterSlots; ++position) {
            const auto at = static_cast<std::uint32_t>(battle::BattlerSlot::monster(position).index() * 2);
            const bool indexRead = call.inputs.contains(kMonsterIndex + at + 1);
            const auto bit = static_cast<std::uint8_t>(1u << position);
            if (heldRead && !indexRead) {
                writeWord(kMonsterIndex + at, (held & bit) != 0 ? 0 : kNoMonster);
            } else if (indexRead && !heldRead) {
                held = static_cast<std::uint8_t>((readByte(kMonsterIndex + at + 1) & kTopBit) == 0 ? held | bit : held & ~bit);
            }
        }
        writeByte(kMonstersHeld, held);
    };

    if (routine.entry == kInitAi) {
        // The slot is the one whose turn place the call wrote, and the script the one whose turn
        // part starts there.
        std::optional<battle::BattlerSlot> slot;
        for (const auto& [cell, value] : call.writes) {
            if (cell >= kTurnPlace && cell < kTurnPlace + 2 * battle::kBattlerSlots) {
                slot = battle::BattlerSlot::fromIndex(static_cast<int>((cell - kTurnPlace) / 2));
            }
        }
        if (!slot) {
            throw std::runtime_error("InitAI wrote no turn place");
        }
        const auto at = static_cast<std::uint32_t>(slot->index() * 2);
        const auto byteOf = [&](std::uint32_t cell) {
            const auto found = call.writes.find(cell);
            if (found == call.writes.end()) {
                throw std::runtime_error("InitAI wrote half of a turn place");
            }
            return found->second;
        };
        const auto place = static_cast<std::uint16_t>(byteOf(kTurnPlace + at) | byteOf(kTurnPlace + at + 1) << 8);
        const std::optional<std::uint16_t> script = scriptWithTurnAt(place);
        if (!script) {
            throw std::runtime_error("InitAI wrote a turn place, " + std::to_string(place) + ", where no script starts");
        }
        const AiScenario plain;
        writeAiScriptCells(plain.state);
        overlay(call.inputs);
        AiScenario s = readAiScenario(*slot);
        s.state.at(*slot).aiScript = *script;
        battle::readyAi(s.state, *slot);
        const AiScriptPlaces port = portScriptPlaces(s.state, *slot);
        overlay(call.writes);
        return differences(port, readScriptPlaces(*slot));
    }

    if (routine.entry == kExecMonsterAction || routine.entry == kExecAiRetal) {
        const bool counter = routine.entry == kExecAiRetal;
        // The battler whose script place the run read; a counterattack that ran already this step
        // reads only its battler's bit.
        std::optional<battle::BattlerSlot> actor =
            battlerRead(call, counter ? kCounterPlace : kTurnPlace, "script place");
        if (!actor && counter) {
            actor = battlerRead(call, kBattlerBit, "battler bit");
        }
        if (!actor) {
            throw std::runtime_error(call.routine + " read no battler's script place");
        }
        AiScenario plain;
        plain.actor = *actor;
        writeAiRunCells(plain, counter);
        overlayReads();
        const AiRunResult before = readAiRunResult(*actor);
        AiScenario s = readAiScenario(*actor);
        const AiRunResult port = counter ? portAiCounter(s) : portAiTurn(s);
        overlay(call.writes);
        AiRunResult guest = readAiRunResult(*actor);
        guest.actions = added(guest.actions, before.actions);
        guest.counterActions = added(guest.counterActions, before.counterActions);
        return differences(port, guest);
    }

    // One of the three commands. The step is the one a script holds whose bytes are the ones the
    // command read.
    const std::size_t command = static_cast<std::size_t>(
        std::find(kAiCommands.begin(), kAiCommands.end(), routine.entry) - kAiCommands.begin());
    if (!call.inputs.contains(kCommandAttack)) {
        throw std::runtime_error(call.routine + " read no attack byte");
    }
    const CommandStep* step = nullptr;
    for (const CommandStep& candidate : commandSteps()) {
        if (candidate.command != command) {
            continue;
        }
        const auto at = [&](std::size_t i) -> std::uint8_t { return i < candidate.bytes.size() ? candidate.bytes[i] : 0; };
        const auto agrees = [&](std::uint32_t cell, std::uint8_t value) {
            const auto found = call.inputs.find(cell);
            return found == call.inputs.end() || found->second == value;
        };
        if (agrees(kCommandAttack, at(1)) && agrees(kCommandTargets, at(2)) && agrees(kCommandTargets + 1, at(3))) {
            step = &candidate;
            break;
        }
    }
    if (step == nullptr) {
        std::string bytes;
        for (const std::uint32_t cell : {kCommandAttack, kCommandTargets, kCommandTargets + 1}) {
            const auto found = call.inputs.find(cell);
            bytes += found == call.inputs.end() ? " --" : " " + std::to_string(found->second);
        }
        throw std::runtime_error(call.routine + " read bytes no script's step can hold:" + bytes);
    }
    if (const auto* effect = std::get_if<battle::AiEffectAction>(&step->action);
        effect != nullptr && (effect->effect == AiEffect::SetStatus || effect->effect == AiEffect::ClearStatus)) {
        throw std::runtime_error("SetStatus and ClearStatus make an attack, which replays through the attack's own calls");
    }

    // The cells as the oracle writes a default command's, then the call's reads.
    const auto rebuild = [&] {
        const AiScenario plain;
        writeAiCells(plain);
        writeByte(kAiTargetable, 0xFF);
        writeWord(kReachSecondCell, 0xFFFF);
        writeWord(kInvincible, 0);
        writeByte(kEndOfBattleEvent, 0);
        writeByte(kPartyPlayed, 0);
        for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
            writeByte(kPartyBytes + static_cast<std::uint32_t>(record), 0);
        }
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            const auto at = static_cast<std::uint32_t>(index * 2);
            writeWord(kAtbRate + at, 0);
            if (index < battle::kPartySlots) {
                writeByte(kRecordIndex + at, kNoOne);
            }
            writeWord(kToSet12 + at, 0);
            writeWord(kToSet34 + at, 0);
            writeWord(kToClear12 + at, 0);
            writeWord(kToClear34 + at, 0);
        }
        writeCommandStart();
        overlayReads();
    };
    rebuild();
    overlay(call.writes);
    AiCommandResult recorded = readAiCommandResult();

    // The battler is each one the cartridge's command, given the same reads, makes the same writes for.
    std::string out;
    int matched = 0;
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot actor = battle::BattlerSlot::fromIndex(index);
        rebuild();
        DamageOracle::call(
            [&] { aiCommands_[command](slotIndex(actor), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
        if (!differences(readAiCommandResult(), recorded).empty()) {
            continue;
        }
        ++matched;
        rebuild();
        AiScenario s = readAiScenario(actor);
        AiCommandResult port = portAiCommand(s, step->action);
        // No command here makes an attack, so the attack's cells are not compared.
        port.attack = {};
        recorded.attack = {};
        const std::string parted = differences(port, recorded);
        if (!parted.empty()) {
            out += "as battler " + std::to_string(index) + ":\n" + parted;
        }
    }
    if (matched == 0) {
        throw std::runtime_error(call.routine + " makes the call's writes for no battler, given its reads");
    }
    return out;
}

OwnActionCase drawOwnActionCase(std::mt19937& rng) {
    OwnActionCase c{.scenario = drawAttackScenario(rng), .action = battle::NoAction{}};
    battle::BattleState& state = c.scenario.state;
    state.enemyCount = oneIn(rng, 8) ? 0 : static_cast<std::uint8_t>(1 + upTo(rng, battle::kMonsterSlots - 1));
    for (battle::Battler& battler : state.battlers) {
        battler.poisonMultiplier = 2 * upTo(rng, 7);
        battler.heldByStatus = oneIn(rng, 4);
        battler.dotActionPending = oneIn(rng, 4);
    }
    // Three times in four the battler an action acts on can be aimed at: on the field, counted in
    // the battle and targetable by anyone.
    const auto reachableMostly = [&](battle::BattlerSlot slot) {
        if (oneIn(rng, 4)) {
            return;
        }
        state.at(slot).present = true;
        state.targetableOrPresent.add(slot);
        state.countedInBattle.add(slot);
        state.aiUntargetable.remove(slot);
        state.awayFromTheField.remove(slot);
    };
    // Held three times in four: the status an action stands for.
    const auto heldMostly = [&](battle::Battler& battler, StatusId status) {
        if (oneIn(rng, 4)) {
            battler.status.clear(status);
        } else {
            battler.status.set(status);
        }
    };
    switch (rng() % 5) {
    case 0: {
        constexpr std::array<std::pair<battle::DamageOverTimeKind, StatusId>, 3> kinds{{
            {battle::DamageOverTimeKind::Poison, StatusId::POISON},
            {battle::DamageOverTimeKind::Regen, StatusId::REGEN},
            {battle::DamageOverTimeKind::Sap, StatusId::SAP},
        }};
        const auto& [kind, status] = kinds[rng() % kinds.size()];
        battle::Battler& actor = state.at(c.scenario.attacker);
        actor.dotActionPending = true;
        heldMostly(actor, status);
        reachableMostly(c.scenario.attacker);
        c.action = battle::DamageOverTime{.kind = kind};
        break;
    }
    case 1: {
        const bool doom = oneIn(rng, 3);
        const AttackId spell = doom ? AttackId::DOOM : static_cast<AttackId>(upTo(rng, 53));
        if (doom && oneIn(rng, 4)) {
            state.enemyCount = 0;
        }
        const battle::BattlerSlot target = anySlot(rng);
        reachableMostly(target);
        c.action = battle::SpellWithoutCaster{.spell = spell, .target = target};
        break;
    }
    case 2: {
        battle::StatusTimersExpired expired{.battler = anySlot(rng)};
        while (!(expired.stop || expired.reflect || expired.freeze || expired.sleep)) {
            expired.stop = oneIn(rng, 2);
            expired.reflect = oneIn(rng, 2);
            expired.freeze = oneIn(rng, 2);
            expired.sleep = oneIn(rng, 2);
        }
        battle::Battler& battler = state.at(expired.battler);
        reachableMostly(expired.battler);
        for (const auto& [ran, status] : {std::pair{expired.stop, StatusId::STOP}, std::pair{expired.reflect, StatusId::REFLECT},
                                          std::pair{expired.freeze, StatusId::FROZEN},
                                          std::pair{expired.sleep, StatusId::SLEEP}}) {
            if (ran) {
                heldMostly(battler, status);
            }
        }
        c.action = expired;
        break;
    }
    case 3: {
        // A monster holding a party member, the only hold the battle makes.
        for (battle::Battler& battler : state.battlers) {
            battler.seizing.reset();
            battler.seizedBy.reset();
        }
        const battle::BattlerSlot seizer = battle::BattlerSlot::monster(upTo(rng, battle::kMonsterSlots - 1));
        const battle::BattlerSlot seized = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
        c.scenario.attacker = seizer;
        state.at(seizer).seizing = seized;
        state.at(seized).seizedBy = seizer;
        state.at(seizer).dotActionPending = true;
        reachableMostly(seizer);
        reachableMostly(seized);
        c.action = battle::SeizeDrain{.seized = seized};
        break;
    }
    default:
        reachableMostly(c.scenario.attacker);
        c.action = battle::AiEffectAction{.effect = oneIn(rng, 2) ? AiEffect::SetStatus : AiEffect::ClearStatus,
                                          .target = anyAiTarget(rng),
                                          .status = anyStatus(rng)};
        break;
    }
    return c;
}

std::string describe(const OwnActionCase& drawn) {
    std::string action = std::visit(
        [](const auto& a) -> std::string {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, battle::DamageOverTime>) {
                constexpr std::array<const char*, 3> kinds{"Poison", "Regen", "Sap"};
                return std::string{"damage over time, "} + kinds[static_cast<std::size_t>(a.kind)];
            } else if constexpr (std::is_same_v<T, battle::SpellWithoutCaster>) {
                return "spell " + std::to_string(static_cast<int>(a.spell)) + " at " + std::to_string(a.target.index());
            } else if constexpr (std::is_same_v<T, battle::StatusTimersExpired>) {
                return "timers out at " + std::to_string(a.battler.index()) + (a.stop ? " Stop" : "") +
                       (a.reflect ? " Reflect" : "") + (a.freeze ? " Freeze" : "") + (a.sleep ? " Sleep" : "");
            } else if constexpr (std::is_same_v<T, battle::SeizeDrain>) {
                return "Seize's drain of " + std::to_string(a.seized.index());
            } else {
                return listedActionText(a);
            }
        },
        drawn.action);
    return describe(drawn.scenario) + " enemies " + std::to_string(drawn.scenario.state.enemyCount) + "; " + action;
}

// --- a whole battle ------------------------------------------------------------------------------

namespace {

// The graphics commands a whole battle's answers act on: the battle's opening (BtlGfx_00) and one
// frame (BtlGfx_01).
constexpr std::uint8_t kOpenBattleScene = 0x00;
constexpr std::uint8_t kOneFrame = 0x01;

// What the graphics program's opening lays into the battle's cells for the battle program to read
// (BtlGfx_00 → InitBattleGfx → _c10fe0, btlgfx_main.asm:2180-2211): the monsters standing, from the
// formation's monsters present (_c10e86 :2011-2013); the monsters that can be aimed at, every one,
// or the scripted setup's own when the formation has one (_c2bd43 :41253-41267); and the four
// equipment-changed flags, cleared.
constexpr std::uint32_t kMonstersTargetable = kWorkRam + 0x2F46;
constexpr std::uint32_t kEquipmentChangedFlagCells = kWorkRam + 0x2F30;
constexpr std::uint32_t kEquipmentChangedFlagCount = 4;
// The formation's flags LoadBattleProp copies (:7973-7977): the third byte's top bit turns on the
// scripted setup, and the fourth byte names it.
constexpr std::uint32_t kFormationScriptFlags = kWorkRam + 0x2F49;
constexpr std::uint32_t kFormationScript = kWorkRam + 0x2F4A;
constexpr std::uint8_t kFormationScriptOn = 0x80;
constexpr std::uint8_t kPresentMonsters = 0x3F;
constexpr std::uint8_t kEveryMonster = 0xFF;

// The end-of-battle event CheckBattleEnd jumps through (:11892-11897, kEndOfBattleEvent), event 4
// "end the battle now" (BattleEnd_04 :11983-11985) at twice its number, and the cell that turns the
// events on.
constexpr std::uint32_t kEndOfBattleEventsOn = kWorkRam + 0x3EE0;
constexpr std::uint8_t kEndTheBattleNow = 2 * 4;
constexpr std::uint8_t kEventsOn = 0xFF;

// The graphics command that closes the battle (TerminateBattle :150-152): the last one a battle
// that returned asks for.
constexpr std::uint8_t kCloseBattle = 0x09;

}  // namespace

void DamageOracle::answerInBattle(const GraphicsCall& call) {
    StartResult& start = *start_;
    if (call.command == kOpenBattleScene) {
        const auto formation = static_cast<FormationId>(readWord(kFormationCell));
        writeByte(kMonstersStanding, static_cast<std::uint8_t>(getFormation(formation).presentAndBg1 & kPresentMonsters));
        std::uint8_t targetable = kEveryMonster;
        if ((readByte(kFormationScriptFlags) & kFormationScriptOn) != 0) {
            targetable = charAiSetup(static_cast<CharAiId>(readByte(kFormationScript))).validMonsterTargets.bits;
        }
        writeByte(kMonstersTargetable, targetable);
        const std::array<std::uint8_t, kEquipmentChangedFlagCount> unchanged{};
        writeBytes(retropp::MemoryRegion{.at = kEquipmentChangedFlagCells, .size = kEquipmentChangedFlagCount}, unchanged);
        // The monsters on each side of a pincer, which the opening lays out (InitMonsterPos
        // btlgfx_main.asm:2854-2857): from the port's own layout of the monsters the start loaded.
        if (monsterSprites_ == nullptr) {
            throw std::logic_error("a whole battle needs the monsters' sprites (DamageOracle::useMonsterSprites)");
        }
        battle::BattleState placed;
        placed.formation = formation;
        placed.battleType = static_cast<battle::BattleType>(readByte(kBattleType));
        for (int position = 0; position < battle::kMonsterSlots; ++position) {
            const battle::BattlerSlot slot = battle::BattlerSlot::monster(position);
            const std::uint16_t monster = readWord(kMonsterIndex + slotIndex(slot));
            if (monster != kEmptyMonsterSlot) {
                placed.at(slot).monster = static_cast<MonsterId>(monster);
            }
        }
        battle::layOutMonsters(placed, *monsterSprites_);
        writeByte(kPincerFirstSide, placed.pincerSides[0].bits);
        writeByte(kPincerSecondSide, placed.pincerSides[1].bits);
        return;
    }
    if (call.command == kOneFrame && !start.reachedTheLoop) {
        start.reachedTheLoop = true;
        const std::vector<std::uint8_t> cells =
            machine_.read(retropp::MemoryRegion{.at = kWorkRam, .size = static_cast<std::uint32_t>(start.cells.size())});
        std::copy(cells.begin(), cells.end(), start.cells.begin());
        start.unwrittenReads = unwrittenReads();
        writtenSincePoint_.clear();
        writeByte(kEndOfBattleEventsOn, kEventsOn);
        writeByte(kEndOfBattleEvent, kEndTheBattleNow);
    }
}

StartResult DamageOracle::battleStart(const StartScenario& scenario) {
    writeStartScenario(scenario);
    return battleStartAsItStands();
}

StartResult DamageOracle::battleStartAsItStands() {
    start_.emplace();
    writtenSincePoint_.clear();
    writtenBeyond_.clear();
    graphicsCalls_.clear();
    unansweredGraphics_.reset();
    machine_.escapes()[kGraphicsKey].armed(true);
    call([&] { battleMain_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    machine_.escapes()[kGraphicsKey].armed(false);
    StartResult result = std::move(*start_);
    start_.reset();
    result.calls = graphicsCalls_;
    result.returned = !result.calls.empty() && result.calls.back().command == kCloseBattle;
    if (unansweredGraphics_) {
        throw std::runtime_error("the battle called graphics command " + std::to_string(*unansweredGraphics_) +
                                 ", which the oracle has no answer for");
    }
    return result;
}

namespace {

// The field's cells a battle's start reads (battle/battle_main.asm InitBattle :6053-6195 and the
// routines it calls): the formation asked for, the background and the field's flags (:6078,
// LoadBattleProp :7940-7992); the play clock's frames (:6088); the battle settings — wait mode and
// speed, the gauge, the spell order (:6108-6123, :14101); the battle switches (:6083-6087); the story's
// flags for Shadow and Gau (:7775, :11958); the party played (:7761); the inventory (InitInventory
// :13911-13956); the play time at the last save (:14189-14191); and a colosseum match or the final
// battle's line of fighters (InitParty :7724-7756, InitMonsters :7697).
constexpr std::uint32_t kFieldFormation = kWorkRam + 0x11E0;
constexpr std::uint32_t kFieldBackground = kWorkRam + 0x11E2;
constexpr std::uint32_t kFieldBattleFlags = kWorkRam + 0x11E4;
constexpr std::uint8_t kGauMayAppearBit = 0x01;
constexpr std::uint32_t kBattleSettings = kWorkRam + 0x1D4D;
constexpr std::uint8_t kWaitModeBit = 0x08;
constexpr std::uint8_t kSpeedBits = 0x07;
constexpr std::uint32_t kGaugeSetting = kWorkRam + 0x1D4E;
constexpr std::uint8_t kGaugeHiddenBit = 0x80;
constexpr std::uint32_t kSpellOrder = kWorkRam + 0x1D54;
constexpr std::uint8_t kSpellOrderBits = 0x07;
constexpr std::uint32_t kFieldSwitches = kWorkRam + 0x1DC9;
constexpr std::uint32_t kAvailableCharacters = kWorkRam + 0x1EDE;
constexpr std::uint8_t kShadowAvailableBit = 0x08;
constexpr std::uint32_t kFieldItems = kWorkRam + 0x1869;
constexpr std::uint32_t kFieldQuantities = kWorkRam + 0x1969;
constexpr std::uint32_t kSaveHours = kWorkRam + 0x1863;
constexpr std::uint32_t kSaveMinutes = kWorkRam + 0x1864;
constexpr std::uint32_t kColosseumWager = kWorkRam + 0x0205;
constexpr std::uint32_t kColosseumOpponent = kWorkRam + 0x0206;
constexpr std::uint32_t kColosseumPrize = kWorkRam + 0x0207;
constexpr std::uint32_t kColosseumFighter = kWorkRam + 0x0208;
constexpr std::uint32_t kFinalBattleLine = kWorkRam + 0x0205;
constexpr std::uint32_t kFinalBattleLineSize = 12;
constexpr std::uint8_t kEndOfLine = 0xFF;
// The colosseum's two formations, the last two (InitParty :7748-7750).
constexpr std::uint16_t kFirstColosseumFormation = 0x023E;

bool colosseumFormation(FormationRef formation) {
    return static_cast<std::uint16_t>(formation.formationId()) >= kFirstColosseumFormation;
}

}  // namespace

void DamageOracle::writeStartScenario(const StartScenario& scenario) {
    const battle::BattleStart& start = scenario.start;
    const game::Party& party = scenario.cells.party;
    writeParty(scenario.cells);
    writeByte(kPartyPlayed, party.currentParty);
    for (std::size_t slot = 0; slot < game::kInventorySlots; ++slot) {
        writeByte(kFieldItems + static_cast<std::uint32_t>(slot), static_cast<std::uint8_t>(party.inventory[slot].item));
        writeByte(kFieldQuantities + static_cast<std::uint32_t>(slot), party.inventory[slot].quantity);
    }
    std::array<std::uint8_t, 3> steps{};
    writeThreeBytes(steps, party.steps);
    writeBytes(retropp::MemoryRegion{.at = kSteps, .size = 3}, steps);
    writeByte(kSaveHours, party.playTimeAtLastSave.hours);
    writeByte(kSaveMinutes, party.playTimeAtLastSave.minutes);
    writeBytes(retropp::MemoryRegion{.at = kFieldSwitches, .size = game::kBattleVariables}, scenario.switches.variables);

    writeWord(kFieldFormation, start.formation.raw);
    writeWord(kFieldBackground, static_cast<std::uint16_t>(start.background));
    writeByte(kFieldBattleFlags, static_cast<std::uint8_t>((start.gauMayAppear ? kGauMayAppearBit : 0) |
                                                           (start.onTheVeldt ? kOnTheVeldtBit : 0) |
                                                           (start.keepFieldMusic ? kFieldMusicPlaysOnBit : 0)));
    const std::uint8_t story = readByte(kStoryFlags);
    writeByte(kStoryFlags, static_cast<std::uint8_t>(start.gauRecruited ? story | kGauRecruitedBit
                                                                        : story & ~kGauRecruitedBit));
    const std::uint8_t available = readByte(kAvailableCharacters);
    writeByte(kAvailableCharacters, static_cast<std::uint8_t>(start.shadowAvailable ? available | kShadowAvailableBit
                                                                                    : available & ~kShadowAvailableBit));
    writeByte(kPlayClockFrames, start.playTimeFrames);
    writeByte(kBattleSettings, static_cast<std::uint8_t>((start.config.waitMode ? kWaitModeBit : 0) |
                                                         (start.config.speedSetting & kSpeedBits)));
    writeByte(kGaugeSetting, start.config.gaugeShown ? 0 : kGaugeHiddenBit);
    writeByte(kSpellOrder, static_cast<std::uint8_t>(start.config.magicOrder & kSpellOrderBits));

    if (start.colosseum) {
        writeByte(kColosseumWager, static_cast<std::uint8_t>(start.colosseum->wager));
        writeByte(kColosseumOpponent, static_cast<std::uint8_t>(start.colosseum->opponent));
        writeByte(kColosseumPrize, static_cast<std::uint8_t>(start.colosseum->prize));
        writeByte(kColosseumFighter, static_cast<std::uint8_t>(start.colosseum->fighter));
    } else {
        std::array<std::uint8_t, kFinalBattleLineSize> line{};
        line.fill(kEndOfLine);
        for (std::size_t i = 0; i < start.finalBattleOrder.size() && i < line.size(); ++i) {
            line[i] = static_cast<std::uint8_t>(start.finalBattleOrder[i]);
        }
        writeBytes(retropp::MemoryRegion{.at = kFinalBattleLine, .size = kFinalBattleLineSize}, line);
    }
}

StartScenario DamageOracle::readStartScenario() {
    StartScenario s;
    s.cells = readParty();
    game::Party& party = s.cells.party;
    party.currentParty = readByte(kPartyPlayed);
    for (std::size_t slot = 0; slot < game::kInventorySlots; ++slot) {
        party.inventory[slot] = game::InventorySlot{
            .item = static_cast<ItemId>(readByte(kFieldItems + static_cast<std::uint32_t>(slot))),
            .quantity = readByte(kFieldQuantities + static_cast<std::uint32_t>(slot))};
    }
    party.steps = 0;
    for (std::uint32_t i = 0; i < 3; ++i) {
        party.steps |= static_cast<std::uint32_t>(readByte(kSteps + i)) << (8 * i);
    }
    party.playTimeAtLastSave = game::PlayTime{.hours = readByte(kSaveHours), .minutes = readByte(kSaveMinutes)};
    const std::vector<std::uint8_t> switches =
        machine_.read(retropp::MemoryRegion{.at = kFieldSwitches, .size = game::kBattleVariables});
    std::copy(switches.begin(), switches.end(), s.switches.variables.begin());

    battle::BattleStart& start = s.start;
    start.formation = FormationRef{.raw = readWord(kFieldFormation)};
    start.background = static_cast<BattleBackgroundId>(readByte(kFieldBackground));
    const std::uint8_t flags = readByte(kFieldBattleFlags);
    start.gauMayAppear = (flags & kGauMayAppearBit) != 0;
    start.onTheVeldt = (flags & kOnTheVeldtBit) != 0;
    start.keepFieldMusic = (flags & kFieldMusicPlaysOnBit) != 0;
    start.gauRecruited = (readByte(kStoryFlags) & kGauRecruitedBit) != 0;
    start.shadowAvailable = (readByte(kAvailableCharacters) & kShadowAvailableBit) != 0;
    start.playTimeFrames = readByte(kPlayClockFrames);
    const std::uint8_t settings = readByte(kBattleSettings);
    start.config = battle::BattleConfig{.waitMode = (settings & kWaitModeBit) != 0,
                                        .speedSetting = static_cast<std::uint8_t>(settings & kSpeedBits),
                                        .gaugeShown = (readByte(kGaugeSetting) & kGaugeHiddenBit) == 0,
                                        .magicOrder = static_cast<std::uint8_t>(readByte(kSpellOrder) & kSpellOrderBits)};
    start.version = GameVersion::US_1_1;
    if (colosseumFormation(start.formation)) {
        start.colosseum = battle::ColosseumMatch{.wager = static_cast<ItemId>(readByte(kColosseumWager)),
                                                 .prize = static_cast<ItemId>(readByte(kColosseumPrize)),
                                                 .opponent = static_cast<MonsterId>(readByte(kColosseumOpponent)),
                                                 .fighter = static_cast<CharacterPropId>(readByte(kColosseumFighter))};
    } else if (start.formation.raw == FormationRef::of(kFinalBattleFormations[0]).raw) {
        for (std::uint32_t i = 0; i < kFinalBattleLineSize; ++i) {
            const std::uint8_t record = readByte(kFinalBattleLine + i);
            if (record != kEndOfLine) {
                start.finalBattleOrder.push_back(record);
            }
        }
    }
    return s;
}

std::string differences(const StartScenario& a, const StartScenario& b) {
    std::string lines;
    const auto field = [&](const char* name, long ours, long theirs) {
        if (ours != theirs) {
            lines += std::string{name} + " " + std::to_string(ours) + " / " + std::to_string(theirs) + "\n";
        }
    };
    const battle::BattleStart& x = a.start;
    const battle::BattleStart& y = b.start;
    field("formation", x.formation.raw, y.formation.raw);
    field("background", static_cast<long>(x.background), static_cast<long>(y.background));
    field("Gau may appear", x.gauMayAppear, y.gauMayAppear);
    field("on the Veldt", x.onTheVeldt, y.onTheVeldt);
    field("field music plays on", x.keepFieldMusic, y.keepFieldMusic);
    field("Gau recruited", x.gauRecruited, y.gauRecruited);
    field("Shadow available", x.shadowAvailable, y.shadowAvailable);
    field("play clock's frames", x.playTimeFrames, y.playTimeFrames);
    field("wait mode", x.config.waitMode, y.config.waitMode);
    field("battle speed", x.config.speedSetting, y.config.speedSetting);
    field("gauge shown", x.config.gaugeShown, y.config.gaugeShown);
    field("spell order", x.config.magicOrder, y.config.magicOrder);
    field("a colosseum match", x.colosseum.has_value(), y.colosseum.has_value());
    if (x.colosseum && y.colosseum) {
        field("wager", static_cast<long>(x.colosseum->wager), static_cast<long>(y.colosseum->wager));
        field("prize", static_cast<long>(x.colosseum->prize), static_cast<long>(y.colosseum->prize));
        field("opponent", static_cast<long>(x.colosseum->opponent), static_cast<long>(y.colosseum->opponent));
        field("fighter", static_cast<long>(x.colosseum->fighter), static_cast<long>(y.colosseum->fighter));
    }
    field("final battle's line", static_cast<long>(x.finalBattleOrder.size()), static_cast<long>(y.finalBattleOrder.size()));
    for (std::size_t i = 0; i < std::min(x.finalBattleOrder.size(), y.finalBattleOrder.size()); ++i) {
        field("final battle's line place", static_cast<long>(x.finalBattleOrder[i]), static_cast<long>(y.finalBattleOrder[i]));
    }
    for (std::size_t i = 0; i < game::kBattleVariables; ++i) {
        field("battle switches byte", a.switches.variables[i], b.switches.variables[i]);
    }
    const std::vector<PartyCell> ours = partyImage(a.cells);
    const std::vector<PartyCell> theirs = partyImage(b.cells);
    for (std::size_t i = 0; i < std::min(ours.size(), theirs.size()); ++i) {
        if (ours[i].value != theirs[i].value) {
            lines += partyCellName(ours[i].at) + " " + std::to_string(ours[i].value) + " / " +
                     std::to_string(theirs[i].value) + "\n";
        }
    }
    const game::Party& p = a.cells.party;
    const game::Party& q = b.cells.party;
    field("party played", p.currentParty, q.currentParty);
    for (std::size_t slot = 0; slot < game::kInventorySlots; ++slot) {
        field("inventory item", static_cast<long>(p.inventory[slot].item), static_cast<long>(q.inventory[slot].item));
        field("inventory quantity", p.inventory[slot].quantity, q.inventory[slot].quantity);
    }
    field("steps", p.steps, q.steps);
    field("hours at the last save", p.playTimeAtLastSave.hours, q.playTimeAtLastSave.hours);
    field("minutes at the last save", p.playTimeAtLastSave.minutes, q.playTimeAtLastSave.minutes);
    return lines;
}

// --- the state as the cartridge holds it ---------------------------------------------------------

namespace {

// What the start's fill leaves in a cell of $2000-$341F (InitBattle :6063-6067).
constexpr std::uint8_t kFilled = 0xFF;
// An empty inventory record as LoadItemProp leaves it (:13987-13994): no item, unusable, the targeting
// of the record loaded before it, the quantity, and no hand that can hold it.
constexpr std::uint8_t kUnusableItem = 0x80;
constexpr std::uint8_t kNoHandHoldsIt = 0xFF;
constexpr std::size_t kRecordTargeting = 2;
// The empty party slot's record offset, left by the fill (InitParty :7848-7850 skips the slot).
constexpr std::uint16_t kNoRecord = 0xFFFF;
// LoadBattleProp toggles the top four bits of the formation's first byte as it copies it (:7975-7977).
constexpr std::uint8_t kFormationFirstByteToggled = 0xF0;
// $3205,x: bit 7 the battler may act after a Jump (set :264-266, cleared :1797-1799, read :2789, :5758);
// bit 2 Air Anchor is not on it (:1129-1131, :9024-9028, :9743-9745); bit 1 its near-fatal spell has not
// been cast this battle (:1626-1629).
constexpr std::uint8_t kMayActAfterJumpBit = 0x80;
constexpr std::uint8_t kNearFatalSpellAvailableBit = 0x02;
// $3AA0,x, every bit a field: 0 on the field, 1 its commands come from the menu (:1437), 2 its menu is
// open, 3 its gauge is stopped, 4 a status holds it, 5 its action is queued (:2747), 6 control holds it
// (:1386-1409), 7 its menu may open (:1441).
constexpr std::uint8_t kTakesMenuInputBit = 0x02;
constexpr std::uint8_t kAtbStoppedBit = 0x08;
constexpr std::uint8_t kActionQueuedBit = 0x20;
constexpr std::uint8_t kHeldByControlFieldBit = 0x40;
// $B1's bits beyond the counterattack and the run (kCounterattackBit, kCannotRunBit, kMenusClosedBit).
constexpr std::uint8_t kEscapeBlockedBit = 0x02;
constexpr std::uint8_t kBackAttackFlagBit = 0x20;
constexpr std::uint8_t kMimickingBit = 0x40;
// A party slot's place in the monsters' index list (kMonsterIndex), $1FF9-$2000: no monster stands there.
constexpr std::uint32_t kPartySlotsMonsterIndexBytes = 8;

}  // namespace

void DamageOracle::layAsTheCartridgeHoldsIt(std::uint32_t at, std::span<const std::uint8_t> bytes) {
    writeCells(retropp::MemoryRegion{.at = at, .size = static_cast<std::uint32_t>(bytes.size())}, bytes,
               CellWrite::AsTheCartridgeHoldsIt);
}

void DamageOracle::layByteAsTheCartridgeHoldsIt(std::uint32_t at, std::uint8_t value) {
    const std::array<std::uint8_t, 1> bytes{value};
    layAsTheCartridgeHoldsIt(at, bytes);
}

void DamageOracle::layWordAsTheCartridgeHoldsIt(std::uint32_t at, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value & 0xFF), static_cast<std::uint8_t>(value >> 8)};
    layAsTheCartridgeHoldsIt(at, bytes);
}

void DamageOracle::leaveOut(std::uint32_t at, std::uint32_t count) {
    if (capture_ == nullptr) {
        return;
    }
    for (std::uint32_t cell = at; cell < at + count; ++cell) {
        capture_->cells.erase(cell);
        capture_->disagreements.erase(cell);
        capture_->leftOut.insert(cell);
    }
}

void DamageOracle::writeStateAsTheCartridgeHoldsIt(const battle::BattleState& state) {
    // The hands, then the inventory, in the order InitInventory loads them (:13914-13941): each shield
    // and each weapon last party slot first, then the items from the last slot down. An empty record
    // keeps the targeting the record loaded before it left; a hand read through an empty party slot's
    // offset (bank $7F, kCellsNotCompared) leaves it unknown.
    std::optional<std::uint8_t> targeting = kFilled;
    for (const auto* hands : {&state.inventory.shieldHands, &state.inventory.weaponHands}) {
        const std::uint32_t first = hands == &state.inventory.shieldHands ? kShieldHands : kWeaponHands;
        for (int position = battle::kPartySlots - 1; position >= 0; --position) {
            const std::uint32_t at = first + static_cast<std::uint32_t>(position) * kHandItemSize;
            if (!state.at(battle::BattlerSlot::character(position)).actor) {
                leaveOut(at, kHandItemSize);
                targeting.reset();
                continue;
            }
            const battle::BattleItem& hand = (*hands)[static_cast<std::size_t>(position)];
            const battle::ItemRecord record = battle::recordOf(hand);
            layAsTheCartridgeHoldsIt(at, record);
            if (hand.item != ItemId::EMPTY) {
                targeting = record[kRecordTargeting];
            }
        }
    }
    for (std::size_t slot = game::kInventorySlots; slot-- > 0;) {
        const battle::BattleItem& item = state.inventory.items[slot];
        const std::uint32_t at = kInventoryRecords + static_cast<std::uint32_t>(slot) * kRecordBytes;
        if (item.item != ItemId::EMPTY) {
            targeting = battle::recordOf(item)[kRecordTargeting];
            continue;
        }
        const std::array<std::uint8_t, kRecordBytes> empty{static_cast<std::uint8_t>(ItemId::EMPTY), kUnusableItem,
                                                          targeting.value_or(0), item.quantity, kNoHandHoldsIt};
        layAsTheCartridgeHoldsIt(at, empty);
        if (!targeting) {
            leaveOut(at + kRecordTargeting, 1);
        }
    }

    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::BattlerSlot slot = battle::BattlerSlot::character(position);
        const battle::Battler& member = state.at(slot);
        const std::uint32_t at = slotIndex(slot);
        if (!member.actor) {
            layWordAsTheCartridgeHoldsIt(kRecordOffset + at, kNoRecord);
        }
        if (!member.controlling) {
            leaveOut(kControlMenus + static_cast<std::uint32_t>(position) * kControlMenuSize, kControlMenuSize);
        }
    }
    leaveOut(kMonsterIndex, kPartySlotsMonsterIndexBytes);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const auto at = static_cast<std::uint32_t>(i * 2);
        layByteAsTheCartridgeHoldsIt(kToUpdate + at, pendingByte(battler.pendingUpdates));
        layByteAsTheCartridgeHoldsIt(kAnchorCell + at,
                                     static_cast<std::uint8_t>((battler.mayActAfterJump ? kMayActAfterJumpBit : 0) |
                                                               (battler.airAnchored ? 0 : kNotAnchoredBit) |
                                                               (battler.nearFatalSpellAvailable ? kNearFatalSpellAvailableBit : 0)));
        std::uint8_t field = battler.present ? kPresentBit : 0;
        field |= battler.takesMenuInput ? kTakesMenuInputBit : 0;
        field |= battler.inBattleMenu ? kInBattleMenuBit : 0;
        field |= battler.atbStopped ? kAtbStoppedBit : 0;
        field |= battler.heldByStatus ? kHeldByStatusBit : 0;
        field |= battler.actionQueued ? kActionQueuedBit : 0;
        field |= battler.heldByControl ? kHeldByControlFieldBit : 0;
        field |= battler.menuMayOpen ? kMenuMayOpenBit : 0;
        layByteAsTheCartridgeHoldsIt(kOnTheField + at, field);
        if (slot.isMonster() && !battler.monster) {
            layWordAsTheCartridgeHoldsIt(kMonsterIndex + at, kEmptyMonsterSlot);
        }
        if (!battler.monster && !battler.rage) {
            leaveOut(kSpecialAttack + at, 1);
            leaveOut(kNamedMonster + at, 2);
        }
        // A battler with no party member in it holds nothing in its hands: the zero fill
        // (InitBattle :6056-6060), and for a monster whatever LoadMonsterProp leaves.
        if (!battler.actor) {
            leaveOut(kHandItems + at, 2);
        }
        // What a battler was last hit with is read only once it has been hit (AICond_01/_02 :4669-4671,
        // AICond_04 :4693-4695 test the "by" cell first, which the fill leaves empty).
        const battle::LastHits& last = battler.lastHit;
        if (!last.command) {
            leaveOut(kLastHitCommand + at, 1);
        }
        if (!last.spell) {
            leaveOut(kLastHitSpell + at, 1);
        }
        if (!last.item) {
            leaveOut(kLastHitItem + at, 1);
        }
        if (!last.element) {
            leaveOut(kLastHitElement + at, 1);
        }
    }
    // The battle's flags at $B1: 0 a counterattack is running (:5721-5722, :12413-12414), 1 a monster or a
    // party member fighting the party makes running harder (CheckRunAway :15188-15191, :15246-15249),
    // 2 the party cannot run (:15207-15210), 5 a back attack (:7664-7665), 6 the next action is Mimic's
    // (:325-326, :12975-12976), 7 no battle menu may open (:12074-12075). Bit 4 is one command's own
    // (InitTarget_01 :6529-6530, cleared :3963-3964).
    layByteAsTheCartridgeHoldsIt(
        kRunningCell, static_cast<std::uint8_t>((state.counterattackRunning ? kCounterattackBit : 0) |
                                                (state.escapeBlockedByMonster ? kEscapeBlockedBit : 0) |
                                                (state.cannotRun ? kCannotRunBit : 0) |
                                                (state.backAttack ? kBackAttackFlagBit : 0) |
                                                (state.mimicking ? kMimickingBit : 0) |
                                                (state.menusDisabled ? kMenusClosedBit : 0)));
    // Counterattacks are on while $341A is not zero (:12530-12531); every command starts it at $FF
    // (ExecCmd :3106-3109) and a counterattack's end clears it (:9753).
    layByteAsTheCartridgeHoldsIt(kCountersEnabled, state.scratch.countersEnabled ? kFilled : 0);

    // The reach (_c25917 :14635-14642): the monsters the AI leaves in reach in $2F46, the party members
    // not seized in $3403, the targetable or present in $3A78 and those counted in the battle in $3408.
    layByteAsTheCartridgeHoldsIt(kReachMonsters, static_cast<std::uint8_t>(~state.aiUntargetable.monsters.bits));
    layByteAsTheCartridgeHoldsIt(kReachCharacters, static_cast<std::uint8_t>(~seizedSet(state).characters.bits));
    layWordAsTheCartridgeHoldsIt(kReachFirst, maskOf(state.targetableOrPresent));
    layWordAsTheCartridgeHoldsIt(kReachSecond, maskOf(state.countedInBattle));

    const FormationAux& aux = getFormationAux(state.formation);
    layByteAsTheCartridgeHoldsIt(kFormationBytes, static_cast<std::uint8_t>(aux.entranceAndTypes ^ kFormationFirstByteToggled));
    // Which way each battler faces is the graphics program's from the opening on; the battle program
    // only reads it (:3230, :3260).
    leaveOut(kFacingRight, 2);
    if (!state.colosseum) {
        leaveOut(kWagerCell, 1);
        leaveOut(kPrizeCell, 1);
    }
}

}  // namespace ostinato::trace
