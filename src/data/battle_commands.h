// Battle commands: the properties of each of the 30 player battle commands
// (FIGHT, MAGIC, STEAL, ...) plus the satellite tables the battle engine keys
// by command — which commands a status allows, which retarget, which carry an
// attack number, the per-command ATB delay and targeting-init byte, and the
// relic command swaps. The row data is generated (src/data/generated/*.inc);
// this header owns the record types, the entry structs, and the accessors.
//
// Every table is a simple mapping from a named BattleCommandId to its value;
// the command IS the identity. The dispatch/jump tables that consume these
// (RandCmdTbl, InitTargetTbl, ...) are the runtime battle engine — Phase 3.
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "ostinato/attack_id.h"
#include "ostinato/battle_command_flags.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/battle_command_set.h"
#include "ostinato/command_targeting_init.h"
#include "ostinato/flag_set.h"
#include "ostinato/target_flags.h"
#include "ostinato/targeting.h"

namespace ostinato {

// One command's properties (BattleCmdProp, ROM cf/fe00, two bytes): the flag
// mask of who can use it (Gogo/Mimic/Imp) and its default targeting mode.
struct BattleCommandProperties {
    FlagSet<BattleCommandFlags> flags;
    Targeting targeting;
};
static_assert(sizeof(BattleCommandProperties) == 2,
              "BattleCommandProperties must be byte-identical to the two ROM "
              "bytes");

// The table rows: each maps a command to its value. The command is a typed
// BattleCommandId field — the identity, never a comment or a bare index.
struct BattleCommandPropertiesEntry {
    BattleCommandId command;
    BattleCommandProperties record;
};

// Per-command ATB advance-wait (CmdDelayTbl): the wait in ticks the command
// adds before its action runs.
struct CommandAdvanceWaitEntry {
    BattleCommandId command;
    std::uint8_t wait;
};

// Per-command targeting-init byte (CmdTargetTbl): see CommandTargetingInit.
struct CommandTargetingInitEntry {
    BattleCommandId command;
    CommandTargetingInit init;
};

// A relic command swap (RelicCmdTbl1/2): the relic replaces `from` with `to`.
struct RelicCommandSwap {
    BattleCommandId from;
    BattleCommandId to;
};

// The base ATTACK id an attack-carrying command counts up from
// (CmdWithAttackTbl/CmdAttackOffsetTbl).
struct CommandAttackBase {
    BattleCommandId command;
    AttackId attackBase;
};

// The command-domain satellite tables (masks, id lists, the two per-command
// mappings, the two pair tables). Public constants; consumed by the battle
// engine and verified in full by the tests.
#include "data/generated/battle_cmd_tables_data.inc"

// --- accessors ---------------------------------------------------------------

// The properties of a command. cmd must be a real command (FIGHT..MAGITEK).
const BattleCommandProperties& battleCommandProperties(BattleCommandId cmd);
std::span<const BattleCommandPropertiesEntry> battleCommandProperties();

// The ATB advance-wait a command adds, in ticks.
std::uint8_t commandAdvanceWait(BattleCommandId cmd);

// The packed targeting-init byte a command starts targeting from.
CommandTargetingInit commandTargetingInit(BattleCommandId cmd);

}  // namespace ostinato
