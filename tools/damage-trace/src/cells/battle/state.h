// The cells a battle's state is held in that no routine's scenario writes: the battle's own flags and
// settings, its lines and the player's records, each battler's clocks and counters, the party's
// command menus, spell lists and skills, and the memory Mimic keeps.
//
// The addresses are $7E:xxxx cells in namespace cells: a file that names them says
// `using namespace cells;` inside ostinato::trace.
#pragma once

#include <cstdint>

#include "cells/damage/per_target.h"

namespace ostinato::trace::cells {

// --- the battle's flags and settings -------------------------------------------------------------

// $B0: bit 7 the battler has more of its turn to take (set :269-270, cleared :286-287, read
// :12458-12459), bit 6 a preemptive strike (:7639-7640), bit 5 the monsters' names are to be shown
// again (set :269-270, cleared :105-106), bit 2 the party's run action is waiting (:15090-15091,
// cleared :111-112).
inline constexpr std::uint32_t kBattleFlags0 = kWorkRam + 0x00B0;
inline constexpr std::uint8_t kStillActingBit = 0x80;
inline constexpr std::uint8_t kPreemptiveBit = 0x40;
inline constexpr std::uint8_t kMonsterNamesChangedBit = 0x20;
inline constexpr std::uint8_t kRunActionWaitingBit = 0x04;
// The level of the last attack made (InitBattle sets 20, :6130-6131).
inline constexpr std::uint32_t kLastAttackLevel = kWorkRam + 0x11AF;
// The gauge setting: $FF shown, 0 hidden (:6121-6123).
inline constexpr std::uint32_t kGaugeShown = kWorkRam + 0x2021;
inline constexpr std::uint8_t kGaugeShownByte = 0xFF;
// Time stopped by a sub-menu: 0 running (:6124), the graphics program's own value while a sub-menu is
// open (btlgfx_main.asm:18875, :18912).
inline constexpr std::uint32_t kTimeStoppedByMenu = kWorkRam + 0x2F41;
// Roulette's choice: a word naming one battler, $FFFF none (GlobalCounter_00 :15041-15048).
inline constexpr std::uint32_t kRouletteTarget = kWorkRam + 0x2F42;
// The run buttons held (btlgfx_main.asm:1603, :1614).
inline constexpr std::uint32_t kPartyRunning = kWorkRam + 0x2F45;
// The battlers to be on the field once the action is over (UpdateDead :12293-12297).
inline constexpr std::uint32_t kJoining = kWorkRam + 0x2F4E;
// The battler acting and the battler counterattacking, $FF none (:289, :12461).
inline constexpr std::uint32_t kActingBattler = kWorkRam + 0x3406;
inline constexpr std::uint32_t kCounteringBattler = kWorkRam + 0x3407;
// The characters whose escape has just succeeded (:15086).
inline constexpr std::uint32_t kPartyJustEscaped = kWorkRam + 0x3A38;
// The time updates so far, a word (:2747-2748 and on).
inline constexpr std::uint32_t kTurnCounter = kWorkRam + 0x3A3E;
// The party members fighting against the party who are still fighting (UpdateDead :12299-12321).
inline constexpr std::uint32_t kAliveEnemyCharacters = kWorkRam + 0x3A42;
// The party members whose menus are to be refreshed (UpdateCmdList :13568-13569).
inline constexpr std::uint32_t kMenusToRefresh = kWorkRam + 0x3A58;
// The three lines: each a ring of 256 slots holding twice a battler's index, with where the line
// starts and where it ends, a byte each (QueueAction :469-573).
inline constexpr std::uint32_t kAdvanceWaitLine = kWorkRam + 0x3720;
inline constexpr std::uint32_t kActionLine = kWorkRam + 0x3820;
inline constexpr std::uint32_t kCounterLine = kWorkRam + 0x3920;
inline constexpr std::uint32_t kAdvanceWaitLineStart = kWorkRam + 0x3A64;
inline constexpr std::uint32_t kActionLineStart = kWorkRam + 0x3A66;
inline constexpr std::uint32_t kCounterLineStart = kWorkRam + 0x3A68;
// The player's records: four of eight bytes in a ring, read from the word at kRecordsNext
// (GetPlayerAction :12620-12653): the party slot ($FF none waiting), the command, the attack, the
// targets, X-Magic's second attack and its targets.
inline constexpr std::uint32_t kPlayerRecords = kWorkRam + 0x2BAE;
inline constexpr std::uint32_t kRecordsNext = kWorkRam + 0x3A6A;
inline constexpr std::uint32_t kPlayerRecordSize = 8;
inline constexpr std::uint32_t kPlayerRecordCount = 4;
inline constexpr std::uint8_t kNoRecordWaiting = 0xFF;
// The frame counter as the clock last ran (UpdateBattleTime :2675-2681, :2717-2718).
inline constexpr std::uint32_t kLastClockFrame = kWorkRam + 0x3A6C;
inline constexpr std::uint32_t kFrameCounter = kWorkRam + 0x000E;
// Every party member's second relic effects together.
inline constexpr std::uint32_t kPartyRelicEffects2 = kWorkRam + 0x3A6D;
// The dance the menu last chose, 0 before one is (:12736-12737, read :3715).
inline constexpr std::uint32_t kLastDance = kWorkRam + 0x3A6F;
// The party members present as the battle starts.
inline constexpr std::uint32_t kPartyPresentAtStart = kWorkRam + 0x3A8D;
// Wait mode: 1 on, 0 off (:6111-6113). The monsters' speed factor (:6114-6120). Which of the sixteen
// timer jobs runs next (:2730 and on). The party member who last used Rage, 0 before one has (:3346).
// A hidden monster keeps the battle going: $FF, else 0 (:13090-13091, :13136).
inline constexpr std::uint32_t kWaitMode = kWorkRam + 0x3A8F;
inline constexpr std::uint32_t kMonsterSpeedFactor = kWorkRam + 0x3A90;
inline constexpr std::uint32_t kCounterPhase = kWorkRam + 0x3A91;
inline constexpr std::uint32_t kLastRager = kWorkRam + 0x3A93;
inline constexpr std::uint32_t kBattleMayNotEnd = kWorkRam + 0x3A95;
inline constexpr std::uint8_t kBattleMayNotEndByte = 0xFF;
// The tools the party owns, a bit each from the first tool (InitInventory :13943-13954).
inline constexpr std::uint32_t kToolsOwned = kWorkRam + 0x3A9B;
inline constexpr std::uint8_t kFirstToolItem = 0xA3;
inline constexpr std::uint8_t kToolCount = 8;
// The end-of-battle events: $FF on, 0 off for the final battle's first stage (:6078-6082, :6326);
// where the final battle's background scrolls to, $FF none (:6326, :12255).
inline constexpr std::uint32_t kEndOfBattleEventsOn = kWorkRam + 0x3EE0;
inline constexpr std::uint32_t kFinalBattleScroll = kWorkRam + 0x3EE1;
inline constexpr std::uint8_t kEventsOnByte = 0xFF;
// What Mimic repeats (SaveForMimic :333-368, _c201d9 :298-327, the start :6329-6331): the last
// command and attack, its targets; X-Magic's second, Mimic's own command where there is none; a Jump's
// command, Mimic's where the last was not a Jump, and its targets.
inline constexpr std::uint32_t kMimicLast = kWorkRam + 0x3F20;
inline constexpr std::uint32_t kMimicLastTargets = kWorkRam + 0x3F22;
inline constexpr std::uint32_t kMimicSecond = kWorkRam + 0x3F24;
inline constexpr std::uint32_t kMimicSecondTargets = kWorkRam + 0x3F26;
inline constexpr std::uint32_t kMimicJump = kWorkRam + 0x3F28;
inline constexpr std::uint32_t kMimicJumpTargets = kWorkRam + 0x3F2A;
// The party members with an esper equipped, and those who have used their desperation attack.
inline constexpr std::uint32_t kEspersEquipped = kWorkRam + 0x3F2E;
inline constexpr std::uint32_t kDesperationUsed = kWorkRam + 0x3F2F;
// Morph's meter's speed, a word.
inline constexpr std::uint32_t kMorphCounterSpeed = kWorkRam + 0x3F32;
// Which battlers' counterattack AI has not run this step, and which have not been attacked, a word
// each with a bit clear for each (:116, :12473).
inline constexpr std::uint32_t kCounterAiNotRunState = kWorkRam + 0x33FC;
inline constexpr std::uint32_t kNotAttackedThisStepState = kWorkRam + 0x33FE;

// --- each battler, a byte or a word at twice its index -------------------------------------------

inline constexpr std::uint32_t kGauge = kWorkRam + 0x3218;              // the ATB gauge, a word
inline constexpr std::uint32_t kAdvanceWaitDuration = kWorkRam + 0x322C;  // $FF none chosen
inline constexpr std::uint8_t kNoAdvanceWait = 0xFF;
inline constexpr std::uint32_t kFirstCounterAction = kWorkRam + 0x32CD;  // $FF none waiting
inline constexpr std::uint32_t kDanceDanced = kWorkRam + 0x32E1;        // $FF none chosen
inline constexpr std::uint32_t kEsperIndex = kWorkRam + 0x3344;
inline constexpr std::uint32_t kEsperTargeting = kWorkRam + 0x3345;
inline constexpr std::uint32_t kNameShownAs = kWorkRam + 0x3380;        // a word, $FFFF none
inline constexpr std::uint32_t kAdvanceWaitCounter = kWorkRam + 0x3AB4;  // a word
inline constexpr std::uint32_t kAtbRateCell = kWorkRam + 0x3AC8;         // a word
inline constexpr std::uint32_t kStatusTimer = kWorkRam + 0x3ADC;
inline constexpr std::uint32_t kStatusTimerStep = kWorkRam + 0x3ADD;
inline constexpr std::uint32_t kDamageOverTimePhase = kWorkRam + 0x3AF0;
inline constexpr std::uint32_t kMorphGauge = kWorkRam + 0x3B04;
inline constexpr std::uint32_t kCondemnedNumber = kWorkRam + 0x3B05;
inline constexpr std::uint32_t kSpeed = kWorkRam + 0x3B19;
// After the vigor: a party member's speed laid a second time (:6758-6760); for a monster, the high
// byte of the vigor LoadMonsterProp stores sixteen bits wide, 0 (:7424-7431).
inline constexpr std::uint32_t kSpeedAgain = kWorkRam + 0x3B2D;
inline constexpr std::uint32_t kEquipmentStatus23 = kWorkRam + 0x3C6C;  // statuses 2 and 3
inline constexpr std::uint32_t kRelics = kWorkRam + 0x3CD0;
inline constexpr std::uint32_t kSpellCount = kWorkRam + 0x3CF8;
inline constexpr std::uint32_t kRunProgress = kWorkRam + 0x3D70;
inline constexpr std::uint32_t kRunFactor = kWorkRam + 0x3D71;
inline constexpr std::uint32_t kExperience = kWorkRam + 0x3D84;          // a word
inline constexpr std::uint32_t kPoisonMultiplier = kWorkRam + 0x3E24;

// --- the party's menus and skills ----------------------------------------------------------------

// Each party slot's four commands, three bytes each: the command ($FF removed), the command again with
// bit 7 set where it is shown gray (UpdateCmdList :13610-13611 rotates the verdict in), and its
// targeting, 0 for a removed command (InitCmdList :13785-13793).
inline constexpr std::uint32_t kCommandMenus = kWorkRam + 0x202E;
inline constexpr std::uint32_t kCommandMenuSize = 12;
inline constexpr std::uint32_t kCommandRowSize = 3;
inline constexpr std::uint8_t kGrayBit = 0x80;
// Each party slot's spell list (InitSpellList :14049-14200, ValidateSpellList :14206-14296): 79
// entries of four bytes — the esper, then the spells by their places, then the lores — each the spell
// ($FF none; a lore by its own number), bit 7 of the second byte set where it cannot be cast
// (UpdateEnabledMagic :14387-14393), the targeting and the MP cost.
inline constexpr std::uint32_t kSpellLists = kWorkRam + 0x208E;
inline constexpr std::uint32_t kSpellListSize = 0x13C;
inline constexpr std::uint32_t kSpellEntrySize = 4;
inline constexpr std::uint8_t kNotCastableBit = 0x80;
inline constexpr std::uint8_t kNoSpell = 0xFF;
inline constexpr std::uint8_t kFirstLoreAttack = 0x8B;
// Where each party slot's spell list starts, a word each (InitBattle's table copy, :6074-6077).
inline constexpr std::uint32_t kSpellListPlaces = kWorkRam + 0x302C;
// The party's spell list as InitSpellList lays it out: 54 spells by place, then 24 lores by number
// (:14124-14148, :14068-14076); and for every attack, its place in that list plus one, 0 for an esper
// and $FF where the party has none (:14052-14054, :14149-14158).
inline constexpr std::uint32_t kPartySpells = kWorkRam + 0x3034;
inline constexpr std::uint32_t kPartyLores = kWorkRam + 0x306A;
inline constexpr std::uint32_t kSpellPlaces = kWorkRam + 0x3084;
inline constexpr std::uint32_t kAttackCount = 256;
inline constexpr std::uint32_t kFirstEsperAttack = 0x36;
inline constexpr std::uint32_t kEspers = 27;
// The SwdTechs known less one (InitSkills :14475-14478), the Blitzes and lores known (:14480-14482,
// InitSpellList :14070), the rages known in order, $FF after the last (:14493-14512), and the dances,
// each by its own number where known and $FF where not (:14483-14492).
inline constexpr std::uint32_t kSwdtechsKnownLessOne = kWorkRam + 0x2020;
inline constexpr std::uint32_t kBlitzCount = kWorkRam + 0x3A80;
inline constexpr std::uint32_t kLoreCount = kWorkRam + 0x3A87;
inline constexpr std::uint32_t kRageCount = kWorkRam + 0x3A9A;
inline constexpr std::uint32_t kRageList = kWorkRam + 0x257E;
inline constexpr std::uint32_t kDanceList = kWorkRam + 0x267E;
inline constexpr std::uint8_t kNotKnown = 0xFF;
// The party members wearing a Genji Glove: $10 each, 0 for one without (UpdateEquipBattle
// :6746-6751).
inline constexpr std::uint32_t kGenjiGlove = kWorkRam + 0x2E6E;
inline constexpr std::uint8_t kGenjiGloveByte = 0x10;

}  // namespace ostinato::trace::cells
