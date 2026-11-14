// The work-RAM cells the special effects read and write, on the attacker and on a target: the params
// block's flags, the command's cells, Quick, the end-of-battle event, the battlers leaving, the links
// between battlers, the hands, gold and gil, the actions waiting and the control menus, in namespace cells. A file that names them says `using namespace cells;` inside ostinato::trace.
#pragma once

#include <cstdint>

#include "cells/damage/per_target.h"

namespace ostinato::trace::cells {

// The direct page: the params block ($A0-$AF), the swirl's suppression ($B0 bit 4), the command's
// running state ($B1: bit 0 a counterattack, bit 2 the party cannot run).
inline constexpr std::uint32_t kParamsFlags = kWorkRam + 0x00A0;
inline constexpr std::uint32_t kParamsReflectedOff = kWorkRam + 0x00A6;
inline constexpr std::uint32_t kSwirlCell = kWorkRam + 0x00B0;
inline constexpr std::uint32_t kRunningCell = kWorkRam + 0x00B1;
// The command's cells: the spell to cast next, the message to show, the attacks still to make, a
// Dragon Horn's jump, Mind Blast's four picks.
inline constexpr std::uint32_t kFollowUpSpell = kWorkRam + 0x3400;
inline constexpr std::uint32_t kMessage = kWorkRam + 0x3401;
inline constexpr std::uint32_t kAttacksLeft = kWorkRam + 0x3A70;
inline constexpr std::uint32_t kDragonHorn = kWorkRam + 0x3A8E;
inline constexpr std::uint32_t kMindBlastPicks = kWorkRam + 0x3A5C;
// The battle's cells: Quick's turns left and its target, the end-of-battle event (its index times
// two), the battlers leaving after the action, the allies still fighting.
inline constexpr std::uint32_t kQuickTurns = kWorkRam + 0x3402;
inline constexpr std::uint32_t kQuickTarget = kWorkRam + 0x3404;
inline constexpr std::uint32_t kEndOfBattleEvent = kWorkRam + 0x3A6E;
inline constexpr std::uint32_t kLeaving = kWorkRam + 0x2F4C;
inline constexpr std::uint32_t kAllyCount = kWorkRam + 0x3A76;
// Per battler, at twice the battler's index: the hands' power (a party member's weapon hand, then
// its shield hand), Seize's hold, the monster's battle flags and its gold.
inline constexpr std::uint32_t kHandPower = kWorkRam + 0x3B68;
inline constexpr std::uint32_t kSeizing = kWorkRam + 0x3358;
inline constexpr std::uint32_t kMonsterBattleFlags = kWorkRam + 0x3C80;
inline constexpr std::uint32_t kGold = kWorkRam + 0x3D98;
// The battle's copy of each party member's hands, five bytes an item: the weapon hands, then the
// shield hands. The item is the first byte, the quantity the fourth.
inline constexpr std::uint32_t kWeaponHands = kWorkRam + 0x2B86;
inline constexpr std::uint32_t kShieldHands = kWorkRam + 0x2B9A;
inline constexpr std::uint32_t kHandItemSize = 5;
inline constexpr std::uint32_t kHandQuantity = 3;
// The party's gil and steps, three bytes each, and the play clock's frames (1-60, stepped by the
// graphics program's interrupt, IncGameTime menu_common.asm:3488-3519).
inline constexpr std::uint32_t kGil = kWorkRam + 0x1860;
inline constexpr std::uint32_t kSteps = kWorkRam + 0x1866;
inline constexpr std::uint32_t kPlayClockFrames = kWorkRam + 0x021E;
inline constexpr std::uint8_t kSwirlSuppressedBit = 0x10;
inline constexpr std::uint8_t kCounterattackBit = 0x01;
inline constexpr std::uint8_t kCannotRunBit = 0x04;
inline constexpr std::uint8_t kInBattleMenuBit = 0x04;
inline constexpr std::uint8_t kAutomaticCriticalClearBit = 0x02;  // $B3 bit 1: clear for an automatic critical
inline constexpr std::uint8_t kRandomTargetBit = 0x40;
inline constexpr std::uint8_t kFallBackToEarlierTargetsBit = 0x20;
inline constexpr std::uint8_t kOpposingSideBit = 0x02;
inline constexpr std::uint8_t kEndTurnBit = 0x10;                  // $3A46 bit 4
inline constexpr std::uint8_t kNoQuick = 0xFF;

// Per battler, at twice the battler's index: the Control link the other way from kController, the
// battler holding it in Seize, the battler it charms, Love Token's two, Targetting's target, the item
// it holds, its AI variable, its two items to steal, its metamorph byte, its monster (a word), the
// byte holding Air Anchor (bit 2, clear while anchored), and the first of its actions waiting.
inline constexpr std::uint32_t kControlling = kWorkRam + 0x32B8;
inline constexpr std::uint32_t kSeizedBy = kWorkRam + 0x3359;
inline constexpr std::uint32_t kCharming = kWorkRam + 0x3394;
inline constexpr std::uint32_t kBodyguard = kWorkRam + 0x336C;
inline constexpr std::uint32_t kGuarding = kWorkRam + 0x336D;
inline constexpr std::uint32_t kItemInHand = kWorkRam + 0x32F4;
inline constexpr std::uint32_t kTargetingTarget = kWorkRam + 0x32F5;
inline constexpr std::uint32_t kAiVariable = kWorkRam + 0x3DAC;
inline constexpr std::uint32_t kRareSteal = kWorkRam + 0x3308;
inline constexpr std::uint32_t kCommonSteal = kWorkRam + 0x3309;
inline constexpr std::uint32_t kMetamorph = kWorkRam + 0x3C94;
inline constexpr std::uint32_t kMonsterIndex = kWorkRam + 0x1FF9;
inline constexpr std::uint32_t kAnchorCell = kWorkRam + 0x3205;
inline constexpr std::uint32_t kFirstAction = kWorkRam + 0x32CC;
// The battle's: the formation's flags (bit 3: Leap disabled), the party members holding an item,
// engulfed, taken from the party and gone from the battle, the monsters drawn flipped, Zinger's
// monster and the party member it swallowed, the monster Sketch caught, the message's item and
// amount, and whether an attack with no one left is shown (negative: shown).
inline constexpr std::uint32_t kFormationFlags = kWorkRam + 0x2F49;
inline constexpr std::uint32_t kHoldingItem = kWorkRam + 0x3A8C;
inline constexpr std::uint32_t kEngulfed = kWorkRam + 0x3A8A;
inline constexpr std::uint32_t kTakenFromTheParty = kWorkRam + 0x3A88;
inline constexpr std::uint32_t kPartyLeft = kWorkRam + 0x3A39;
// The battlers drawn facing the other way, a word as the statuses and Control set it: party members
// in its low byte (Confuse), monsters in its high byte (kFlipped).
inline constexpr std::uint32_t kFlippedSet = kWorkRam + 0x2F53;
inline constexpr std::uint32_t kZingerMonster = kWorkRam + 0x33F8;
inline constexpr std::uint32_t kZingerSwallowed = kWorkRam + 0x33F9;
inline constexpr std::uint32_t kSketched = kWorkRam + 0x3417;
inline constexpr std::uint32_t kMessageItem = kWorkRam + 0x2F35;
inline constexpr std::uint32_t kMessageAmount = kWorkRam + 0x2F38;
inline constexpr std::uint32_t kShownWithoutTargets = kWorkRam + 0x341D;
// The battle's one list of actions: each slot's next slot (its own at the list's end, negative when
// free), and its command and attack, targets and MP cost as words at twice the slot; the first of
// the actions that interrupt the line.
inline constexpr std::uint32_t kActionNext = kWorkRam + 0x3184;
inline constexpr std::uint32_t kActionCommand = kWorkRam + 0x3420;
inline constexpr std::uint32_t kActionTargets = kWorkRam + 0x3520;
inline constexpr std::uint32_t kActionMp = kWorkRam + 0x3620;
inline constexpr std::uint32_t kFirstImmediateAction = kWorkRam + 0x340A;
inline constexpr std::uint32_t kActionSlots = 128;
// Each party member's control menu: four rows of three bytes — the attack, a byte whose top bit is
// set when it cannot be cast, and its targeting.
inline constexpr std::uint32_t kControlMenus = kWorkRam + 0x205E;
inline constexpr std::uint32_t kControlMenuSize = 12;
inline constexpr std::uint32_t kControlRowSize = 3;

inline constexpr std::uint8_t kEndingBit = 0x80;
inline constexpr std::uint8_t kNotAnchoredBit = 0x04;
inline constexpr std::uint8_t kControlMenuBit = 0x01;
inline constexpr std::uint8_t kUncastableBit = 0x80;
inline constexpr std::uint8_t kShownBit = 0x80;
inline constexpr std::uint8_t kFreeSlot = 0xFF;
inline constexpr std::uint8_t kMessageCommand = 0x02;
// The commands the interrupting actions an effect makes carry: a monster's exit or entrance, Scan.
inline constexpr std::uint8_t kVisibilityCommand = 0x24;
inline constexpr std::uint8_t kScanCommand = 0x27;

}  // namespace ostinato::trace::cells
