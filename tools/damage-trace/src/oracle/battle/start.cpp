// The cartridge's battle program called whole, as the field calls it, and its start written and
// read back as a scenario.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/battle_inventory.h"
#include "battle/battle_layout.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/battle_tables.h"
#include "data/char_ai.h"
#include "data/formations.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/game_version.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

#include "cells/ai/ai.h"
#include "cells/battle/state.h"
#include "cells/damage/per_target.h"
#include "cells/ending/ending.h"
#include "cells/ending/party.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"
#include "oracle/routines.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

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

}  // namespace

void DamageOracle::answerInBattle(const GraphicsCall& call) {
    StartResult& start = *start_;
    if (call.command == kOpenBattleScene) {
        layTheOpening();
        return;
    }
    if (call.command == kOneFrame && !start.reachedTheLoop) {
        start.reachedTheLoop = true;
        const std::vector<std::uint8_t> cells =
            machine_.read(retropp::MemoryRegion{.at = kWorkRam, .size = static_cast<std::uint32_t>(start.cells.size())});
        std::copy(cells.begin(), cells.end(), start.cells.begin());
        start.unwrittenReads = unwrittenReads();
        writtenSincePoint_.clear();
        // Ended by event 4 with nothing run before it: the interrupting actions the start left (an
        // opening message, a pose reset) run in the loop before its end check (:78-80, :111-113), so
        // their list is emptied. The battle running them is the whole battle's to compare.
        writeByte(kFirstImmediateAction, kFreeSlot);
        writeByte(kEndOfBattleEventsOn, kEventsOnByte);
        writeByte(kEndOfBattleEvent, kEndTheBattleNow);
    }
}

void DamageOracle::layTheOpening() {
    const auto formation = static_cast<FormationId>(readWord(kFormationCell));
    writeByte(kMonstersStanding, static_cast<std::uint8_t>(getFormation(formation).presentAndBg1 & kPresentMonsters));
    std::uint8_t targetable = kEveryMonster;
    if ((readByte(kFormationScriptFlags) & kFormationScriptOn) != 0) {
        targetable = charAiSetup(static_cast<CharAiId>(readByte(kFormationScript))).validMonsterTargets.bits;
    }
    writeByte(kMonstersTargetable, targetable);
    const std::array<std::uint8_t, kEquipmentChangedFlagCount> unchanged{};
    writeBytes(retropp::MemoryRegion{.at = kEquipmentChangedFlagCells, .size = kEquipmentChangedFlagCount}, unchanged);
    // The items obtained: the graphics program's start zeroes the two counts (btlgfx_main.asm:43456-43460)
    // and empties the first thirteen places, leaving the last three as they are (:2472-2480).
    writeByte(kItemsAdded, 0);
    writeByte(kItemsMerged, 0);
    std::array<std::uint8_t, kItemsObtainedEmptiedBytes> emptied{};
    emptied.fill(kNoItemObtained);
    writeBytes(retropp::MemoryRegion{.at = kItemsObtainedRecords, .size = kItemsObtainedEmptiedBytes}, emptied);
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
    if (!beyondArmed_) {
        machine_.watches()[kBeyondKey].armed(true);
        beyondArmed_ = true;
    }
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
// LoadBattleProp :7940-7992); the play clock's frames (:6088); the frame count the clock's first
// update waits on (UpdateBattleTime :2675-2681); the battle settings — wait mode and
// speed, the gauge, the spell order (:6108-6123, :14101); the battle switches (:6083-6087); the story's
// flags for Shadow and Gau (:7775, :11958); the party played (:7761); the inventory (InitInventory
// :13911-13956); the play time at the last save (:14189-14191); and a colosseum match or the final
// battle's line of fighters (InitParty :7724-7756, InitMonsters :7697).
constexpr std::uint32_t kFieldFormation = kWorkRam + 0x11E0;
constexpr std::uint32_t kFieldBackground = kWorkRam + 0x11E2;
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
constexpr std::uint32_t kColosseumWager = kWagerCell;
constexpr std::uint32_t kColosseumOpponent = kWorkRam + 0x0206;
constexpr std::uint32_t kColosseumPrize = kPrizeCell;
constexpr std::uint32_t kColosseumFighter = kWorkRam + 0x0208;
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
    writeByte(kFieldFlags, static_cast<std::uint8_t>((start.gauMayAppear ? kGauMayAppearBit : 0) |
                                                           (start.onTheVeldt ? kOnTheVeldtBit : 0) |
                                                           (start.keepFieldMusic ? kFieldMusicPlaysOnBit : 0)));
    const std::uint8_t story = readByte(kStoryFlags);
    writeByte(kStoryFlags, static_cast<std::uint8_t>(start.gauRecruited ? story | kGauRecruitedBit
                                                                        : story & ~kGauRecruitedBit));
    const std::uint8_t available = readByte(kAvailableCharacters);
    writeByte(kAvailableCharacters, static_cast<std::uint8_t>(start.shadowAvailable ? available | kShadowAvailableBit
                                                                                    : available & ~kShadowAvailableBit));
    writeByte(kPlayClockFrames, start.playTimeFrames);
    writeByte(kFrameCounter, start.frameCount);
    writeItemsObtainedTail(start.itemsObtainedTail);
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
    const std::uint8_t flags = readByte(kFieldFlags);
    start.gauMayAppear = (flags & kGauMayAppearBit) != 0;
    start.onTheVeldt = (flags & kOnTheVeldtBit) != 0;
    start.keepFieldMusic = (flags & kFieldMusicPlaysOnBit) != 0;
    start.gauRecruited = (readByte(kStoryFlags) & kGauRecruitedBit) != 0;
    start.shadowAvailable = (readByte(kAvailableCharacters) & kShadowAvailableBit) != 0;
    start.playTimeFrames = readByte(kPlayClockFrames);
    start.frameCount = readByte(kFrameCounter);
    start.itemsObtainedTail = readItemsObtainedTail();
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

}  // namespace ostinato::trace
