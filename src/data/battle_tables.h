// The battle engine's formula-support and mapping tables: the probability
// ladders behind dance steps and Umaro's attacks, the evade boost equipment
// grants, the final-battle chain, the dance/background pairing, the AI script
// command sizes, the thrown-tool conversions, and the slot reel outcomes. The
// row data is generated (src/data/generated/battle_formula_tables_data.inc);
// this header owns the entry types and the accessors.
//
// These are the data half of formulas whose arithmetic lives in the runtime
// battle engine — this layer answers "what are the numbers", not "how are they
// applied".
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "ostinato/ai_script_command.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/battle_slot_mask.h"
#include "ostinato/character_id.h"
#include "ostinato/dance_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/item_id.h"
#include "ostinato/item_type.h"
#include "ostinato/item_type_battle_flags.h"
#include "ostinato/random_threshold.h"

namespace ostinato {

// One rung of the dance-step probability ladder (DanceRateTbl).
struct DanceStepThresholdEntry {
    std::uint8_t index;
    RandomThreshold threshold;
};

// The evade / magic-block boost for one evade rating (EquipEvadeTbl). The boost
// is signed: the upper half of the range subtracts.
struct EquipEvadeBoostEntry {
    std::uint8_t index;
    std::int16_t boost;
};

// One row of cumulative weights over four choices (RandBitRateTbl).
struct RandomBitRateEntry {
    std::uint8_t index;
    std::array<RandomThreshold, 4> weights;
};

// The background scroll position for one step of the final-battle chain.
struct FinalBattleScrollEntry {
    std::uint8_t index;
    std::uint8_t scroll;
};

// A thrown tool and the offset that converts its item id to the attack it
// performs (ThrowToolsItemTbl/ThrowToolsOffsetTbl).
struct ThrowToolsConversion {
    ItemId item;
    std::uint8_t attackOffset;
};

// The attack one slot reel outcome performs (SlotAttackTbl).
struct SlotOutcomeEntry {
    std::uint8_t index;
    AttackId attack;
};

// The battle slots a joker-doom outcome targets (JokerTargetTbl).
struct JokerDoomTargetEntry {
    std::uint8_t index;
    BattleSlotMask targets;
};

// The background a dance switches the battle to (DanceBG).
struct DanceBackgroundEntry {
    DanceId dance;
    BattleBackgroundId background;
};

// The dance that matches a battle background (BattleBGDance).
struct BackgroundDanceEntry {
    BattleBackgroundId background;
    DanceId dance;
};

// How many bytes an AI script command occupies (AICmdSizeTbl).
struct AiCommandSizeEntry {
    AiScriptCommand command;
    std::uint8_t size;
};

// The command an AI-chosen attack belongs to (AttackForAITbl/CmdForAITbl).
struct AiCommandForAttack {
    AttackId attack;
    BattleCommandId command;
};

// The battle-usability bits an item type contributes (ItemTypeMaskTbl).
struct ItemTypeFlagsEntry {
    ItemType type;
    ItemTypeBattleFlags flags;
};

// Where a magic band lands in the spell list under one spell-order setting.
struct MagicOrderOffsetEntry {
    std::uint8_t setting;
    std::int8_t offset;
};

// A character's desperation attack (unused upstream; carried as contract).
struct DesperationAttackEntry {
    CharacterId character;
    AttackId attack;
};

// The tables themselves. Public constants; consumed by the battle engine and
// verified in full by the tests.
#include "data/generated/battle_formula_tables_data.inc"

// --- accessors ---------------------------------------------------------------

// The background a dance switches to.
BattleBackgroundId danceBackground(DanceId dance);

// The dance that matches a background — dancing it keeps the background.
DanceId backgroundDance(BattleBackgroundId background);

// The evade / magic-block boost for an evade rating (0-10).
std::int16_t equipEvadeBoost(std::uint8_t rating);

// How many bytes an AI script command occupies, including the command byte.
std::uint8_t aiCommandSize(AiScriptCommand command);

// The battle-usability bits an item type contributes.
ItemTypeBattleFlags itemTypeBattleFlags(ItemType type);

// A character's desperation attack.
AttackId desperationAttack(CharacterId character);

}  // namespace ostinato
