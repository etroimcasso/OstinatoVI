// The work-RAM cells a battler's AI reads and writes: the run's targets and restriction, the actor,
// the battle's timer, variables and switches, the names listed, the formation, the scripts' places,
// the counterattack line and the party slots.
//
// The addresses are $7E:xxxx cells in namespace cells: a file that names them says
// `using namespace cells;` inside ostinato::trace.
#pragma once

#include <cstdint>

#include "data/ai_scripts.h"
#include "game/battle_switches.h"

#include "cells/damage/per_target.h"

namespace ostinato::trace::cells {

inline constexpr std::uint32_t kAiRestricted = kWorkRam + 0x00F8;
inline constexpr std::uint32_t kAiTargets = kWorkRam + 0x00FC;
inline constexpr std::uint32_t kAiStartingTargets = kWorkRam + 0x00FE;
// Per party slot, at twice the slot's index: the party member in it, none negative. Per monster
// slot the monster's index is the word at kMonsterIndex, none $FFFF.
inline constexpr std::uint32_t kActor = kWorkRam + 0x3ED8;
inline constexpr std::uint16_t kNoMonster = 0xFFFF;
// Per battler, at twice the battler's index: its own timer (a word).
inline constexpr std::uint32_t kOwnTimer = kWorkRam + 0x3DC0;
// The battle's: the battlers alive (the party's byte, then the monsters'), the monster slots that
// hold a monster, the monsters and party members fighting against the party, the battle's timer (a
// word), the battle's variables followed by the battle switches, the names listed, the formation.
inline constexpr std::uint32_t kAlive = kWorkRam + 0x3A74;
inline constexpr std::uint32_t kMonstersHeld = kWorkRam + 0x3A73;
inline constexpr std::uint32_t kEnemyCount = kWorkRam + 0x3A77;
inline constexpr std::uint32_t kBattleTimer = kWorkRam + 0x3A44;
inline constexpr std::uint32_t kBattleVariables = kWorkRam + 0x3EB0;
inline constexpr std::uint32_t kBattleSwitches = kBattleVariables + kAiBattleVariables;
inline constexpr std::uint32_t kNamesListed = kWorkRam + 0x3ECA;
inline constexpr std::uint32_t kFormation = kWorkRam + 0x11E0;
// The story's flags byte with Gau recruited in it.
inline constexpr std::uint32_t kStoryFlags = kWorkRam + 0x1EDF;
inline constexpr std::uint8_t kGauRecruitedBit = 0x08;
inline constexpr unsigned kMonsterCount = 384;
inline constexpr unsigned kBattleSwitchCount = 160;

// Per battler, at twice the battler's index: where its turn part and counterattack part start (a
// word each, $FFFF for none), the blocks each part skipped when it last stopped at a wait (a byte
// each, the turn part's first; $FF for none), and the place each goes on from (a word each).
inline constexpr std::uint32_t kTurnPlace = kWorkRam + 0x3254;
inline constexpr std::uint32_t kCounterPlace = kWorkRam + 0x3268;
inline constexpr std::uint32_t kTurnSkipped = kWorkRam + 0x3240;
inline constexpr std::uint32_t kCounterSkipped = kWorkRam + 0x3241;
inline constexpr std::uint32_t kTurnWaitPlace = kWorkRam + 0x3D0C;
inline constexpr std::uint32_t kCounterWaitPlace = kWorkRam + 0x3D20;
inline constexpr std::uint16_t kNoPlace = 0xFFFF;
// Per battler, at twice the battler's index: its advance wait ($FF unset), and the first entry of
// its list of actions and of counterattacks ($FF for none).
inline constexpr std::uint32_t kAdvanceWait = kWorkRam + 0x322C;
inline constexpr std::uint8_t kUnsetWait = 0xFF;
// The counterattack line: the battlers in it, at twice their index, and where it starts and ends.
inline constexpr std::uint32_t kCounterQueue = kWorkRam + 0x3920;
inline constexpr std::uint32_t kCounterQueueStart = kWorkRam + 0x3A68;
inline constexpr std::uint32_t kCounterQueueEnd = kWorkRam + 0x3A69;
// The battle program's flags byte: bit 0, a counterattack is running.
inline constexpr std::uint32_t kBattleFlags = kWorkRam + 0x00B1;
inline constexpr std::uint8_t kCounterattackRunningBit = 0x01;
// The battlers whose counterattack part has not run this step, the battlers that died this action
// and may answer it, and the battlers not attacked by another this step (a word each).
inline constexpr std::uint32_t kCounterAiNotRun = kWorkRam + 0x33FC;
inline constexpr std::uint32_t kDiedAwaitingCounter = kWorkRam + 0x3A56;
inline constexpr std::uint32_t kNotAttackedThisStep = kWorkRam + 0x33FE;
// Per battler, at twice the battler's index: the statuses it answers an attack with (a word for
// the first two bytes, a word for the last two), and a party member's record among the sixteen.
inline constexpr std::uint32_t kCounterStatus12 = kWorkRam + 0x3E60;
inline constexpr std::uint32_t kCounterStatus34 = kWorkRam + 0x3E74;
inline constexpr std::uint32_t kRecordIndex = kWorkRam + 0x3ED9;
// The commands' cells (and kEndOfBattleEvent above, twice the event's number): each battler's ATB
// rate (a word at twice its index), each character record's party byte (the party in bits 0-2, the
// slot in bits 3-4, the back row in bit 5, enabled in bit 6, shown in bit 7) and the party played.
inline constexpr std::uint32_t kAtbRate = kWorkRam + 0x3AC8;
inline constexpr std::uint32_t kPartyBytes = kWorkRam + 0x1850;
inline constexpr std::uint32_t kPartyPlayed = kWorkRam + 0x1A6D;
inline constexpr std::uint8_t kPartyNumberBits = 0x07;
inline constexpr int kPartySlotShift = 3;
inline constexpr std::uint8_t kPartySlotBits = 0x03;
inline constexpr std::uint8_t kPartyRowBit = 0x20;
inline constexpr std::uint8_t kRecordEnabledBit = 0x40;
inline constexpr std::uint8_t kRecordShownBit = 0x80;
// The monsters an attack may be aimed at as the AI leaves them (a bit set: it may be), and the
// second reach cell, which the AI's effects leave alone.
inline constexpr std::uint32_t kAiTargetable = kWorkRam + 0x2F46;
inline constexpr std::uint32_t kReachSecondCell = kWorkRam + 0x3408;
// The cells a command reads its operands from: the attack byte, then the targets word.
inline constexpr std::uint32_t kCommandAttack = kWorkRam + 0x00B6;
inline constexpr std::uint32_t kCommandTargets = kWorkRam + 0x00B8;
inline constexpr std::uint8_t kMonsterSlotBits = 0x3F;
inline constexpr std::uint8_t kHeldByStatusBit = 0x10;

}  // namespace ostinato::trace::cells
