// The cartridge's endings, write-back and items obtained run over a battle, and a traced ending
// replayed.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/formations.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/game_version.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_set.h"

#include "cells/battle/state.h"
#include "cells/damage/frame.h"
#include "cells/damage/per_target.h"
#include "cells/ending/ending.h"
#include "cells/ending/party.h"
#include "cells/ending/records.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"
#include "oracle/routines.h"
#include "scenarios/common/bytes.h"
#include "scenarios/ending/records_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

// The cells a battle's end reads and writes besides the morphed party member and Morph's meter
// (cells/battle/state.h): per party slot at twice the slot, the Morph meter shown and the party member's
// own bit, the word an item's equippable bits are tested against; the battle's flags (bit 7: no battle
// menu may open); the inventory, 256 records; and the graphics program's list of items obtained, sixteen
// records, with its two counts.
constexpr std::uint32_t kMorphMeterShown = kWorkRam + 0x3B04;
constexpr std::uint32_t kPartyMemberBit = kWorkRam + 0x3A20;
constexpr std::uint32_t kEndingFlags = kWorkRam + 0x00B1;
// The party slots' bits of the byte marking who holds an item.
constexpr std::uint8_t kHoldingItemBits = 0x0F;
// Per party slot at twice the slot: the party member's actor and record (a byte each, none
// negative) and its record's offset from $7E:1600 (a word). Per monster slot at twice the slot, the
// monster's experience and gold (words). Per battler at twice its index, the battlers alive (a word,
// the party's byte first) and each actor's battler, one byte per actor (none negative).
constexpr std::uint32_t kPartyActor = kWorkRam + 0x3ED8;
constexpr std::uint32_t kPartyRecord = kWorkRam + 0x3ED9;
constexpr std::uint32_t kMonsterExperience = kWorkRam + 0x3D8C;
constexpr std::uint32_t kMonsterGold = kWorkRam + 0x3DA0;
constexpr std::uint32_t kBattlersAlive = kWorkRam + 0x3A74;
constexpr std::uint32_t kEnemiesAlive = kWorkRam + 0x3A77;
constexpr std::uint32_t kActorsBattler = kWorkRam + 0x3000;
constexpr std::size_t kActorsWithABattler = 16;
constexpr std::uint32_t kColosseumCell = kWorkRam + 0x3A97;
constexpr std::uint8_t kInTheColosseum = 0xFF;
constexpr std::uint32_t kBattleNumber = kWorkRam + 0x3ED4;
constexpr std::uint32_t kBackgroundCell = kWorkRam + 0x11E2;
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

// The places of the items obtained, as the cartridge holds them: every byte of each record.
template <std::size_t N>
std::vector<std::uint8_t> recordsOf(const std::array<battle::ItemRecord, N>& places) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(N * kRecordBytes);
    for (const battle::ItemRecord& place : places) {
        bytes.insert(bytes.end(), place.begin(), place.end());
    }
    return bytes;
}

template <std::size_t N>
std::array<battle::ItemRecord, N> placesOf(std::span<const std::uint8_t> bytes) {
    std::array<battle::ItemRecord, N> places{};
    for (std::size_t i = 0; i < N; ++i) {
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(i * kRecordBytes), kRecordBytes, places[i].begin());
    }
    return places;
}

// The endings among kSnapshotRoutines, closed by the oracle when it calls one itself.
const SnapshotRoutine& kSnapshotWin = kSnapshotRoutines[0];
const SnapshotRoutine& kSnapshotLoss = kSnapshotRoutines[1];
const SnapshotRoutine& kSnapshotWrapUp = kSnapshotRoutines[2];
static_assert(kSnapshotRoutines[0].entry == kWinBattle && kSnapshotRoutines[1].entry == kLoseBattle &&
              kSnapshotRoutines[2].entry == kWrapUp);

// The battle script command a step back is.
constexpr std::uint8_t kStepBackScriptCommand = 0x0D;

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
            case graphics::kOneFrame:
                if (!out.empty() && out.back().kind == EndingCall::Kind::Wait) {
                    ++out.back().frames;
                } else {
                    out.push_back(EndingCall{.kind = EndingCall::Kind::Wait, .frames = 1});
                }
                puttingAway = true;
                break;
            case graphics::kCloseMenu:
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
            case graphics::kVictory: {
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
    return battle::ItemsObtained{.entries = placesOf<battle::kItemsObtainedPlaces>(bytes),
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
    const battle::ItemRecord record = battle::itemRecordFor(noParty, scenario.item, scenario.quantity);
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
    return readItemsObtainedTail();
}

void DamageOracle::writeItemsObtainedTail(const battle::ItemsObtainedTail& tail) {
    const std::vector<std::uint8_t> bytes = recordsOf(tail);
    writeBytes(retropp::MemoryRegion{.at = kItemsObtainedTailRecords, .size = static_cast<std::uint32_t>(bytes.size())},
               bytes);
}

battle::ItemsObtainedTail DamageOracle::readItemsObtainedTail() {
    const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{
        .at = kItemsObtainedTailRecords,
        .size = static_cast<std::uint32_t>(battle::kItemsObtainedTailPlaces * kRecordBytes)});
    return placesOf<battle::kItemsObtainedTailPlaces>(bytes);
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
        // Whether the member's equipment is to be worked out again (the Item menu, Imp on or off, a Rage
        // come off, an Ogre Nix broken), and above the fourth slot's flag the windows' wallpaper.
        writeByte(kEquipmentChanged + static_cast<std::uint32_t>(position),
                  state.equipmentChangeFlags[static_cast<std::size_t>(position)]);
    }
    writeByte(kHoldingItem, state.holdingItem.characters.bits);
    writeByte(kMorphedPartyMember, state.morphedBattler ? static_cast<std::uint8_t>(slotIndex(*state.morphedBattler)) : kNoOne);
    writeWord(kMorphMeter, state.morphCounter);
    writeByte(kEndingFlags, state.menusDisabled ? kMenusClosedBit : 0);
    writeInventory(state.inventory.items);
    writeItemsObtained(state.itemsObtained);
    writeByte(kLastRecordTargeting,
              state.inventory.lastRecordTargeting ? state.inventory.lastRecordTargeting->bits : kNoRecordMadeYet);

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
    // The field's own record of the battles Gau can find, which no BattleState field holds: the
    // ending's scenario starts it empty.
    const std::array<std::uint8_t, kVeldtBattleBytes> veldt{};
    writeInFlightBytes(retropp::MemoryRegion{.at = kVeldtBattles, .size = kVeldtBattleBytes}, veldt);
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
    return readWriteBack();
}

WriteBackResult DamageOracle::readWriteBack() {
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

}  // namespace ostinato::trace
