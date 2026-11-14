// The pieces of a whole attack's comparison that the cartridge's side of the attack, the AI's
// commands and the battle's own actions read too: the params block and numerals as text, the
// attack's own cells, and the port's state put into the cells it is compared as, in namespace
// detail.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/items_obtained.h"

#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/attack.h"
#include "scenarios/damage/damage.h"
#include "scenarios/damage/damage_shared.h"
#include "scenarios/status/attacker_effects_shared.h"
#include "scenarios/status/status.h"
#include "scenarios/status/status_shared.h"

namespace ostinato::trace::detail {

using namespace cells;

// A params block as the frame's animations read it: paramsText()'s fields with the battlers a spell
// bounced off and the battlers stepping in as the one set they share, then the critical flash, a
// monster's special attack, the covered ally's byte (every attack's params start cleared, so 0 with
// none covered) and each party slot's block.
std::string frameParamsText(battle::AttackAnimationParams params);

// Whether an animation's own handler reads the attack and second value after its command: Mimic's,
// Defend's and the two AI commands' are a bare return (btlgfx CmdAnim_12), Runic's reads its params
// block alone (CmdAnim_25), and a command with its top bit set plays nothing (GfxCmd_06).
bool animationReadsItsAttack(std::uint8_t what);

std::string numeralText(const battle::DamageNumber& number);

// The keys of the cells that belong to the attack itself, which end with the command.
bool attackOwnCell(const std::string& cell);

void putApplied(EffectCells& out, const ApplyDamageResult& applied);

void putStatusChanges(EffectCells& out, const StatusChangesResult& changes);

// The cells a target's special effect is read back by, with the script's own keys left to the whole
// script.
// A whole attack's params block is in the script; the direct-page copy it was written from is cleared
// at every attack's start (ClearGfxParams) and read by nothing after the frame, and after a bounce off
// Reflect it holds the bounce's block.
void putEffectCells(EffectCells& out, const std::map<std::string, std::string>& cells);

std::uint8_t firstFlagsByte(const battle::AttackInFlight& attack);

std::uint8_t secondFlagsByte(const battle::AttackInFlight& attack);

// What the command's next attack starts from, field by field, out of the two flag bytes.
void putNextAttack(EffectCells& out, std::uint8_t first, std::uint8_t second);

// The port's cells after a whole attack. With `attackCells` false the attack's own cells are left out:
// those are the cells of a command run whole, whose attack ends with it.
AttackResult portAttackCells(const battle::BattleState& state, const battle::AttackInFlight& attack,
                             const std::vector<battle::BattleScriptCommand>& commands,
                             const std::deque<battle::AttackAnimationParams>* params, bool awayUnreachable,
                             bool attackCells);

}  // namespace ostinato::trace::detail
