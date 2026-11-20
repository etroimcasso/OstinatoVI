// The port's endings, write-back and items obtained, their differences, and the ending sweeps'
// draws.

#include "scenarios/ending/ending.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "battle/battle_end.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/command_executor.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/game_limits.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"

#include "cells/ending/party.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/ending/rewards.h"
#include "scenarios/ending/rewards_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {

// A record as the comparison reads it: an empty one by its item alone.
std::string recordText(const battle::ItemRecord& record) {
    if (battle::itemIn(record) == ItemId::EMPTY) {
        return "empty";
    }
    std::string text;
    for (const std::uint8_t b : record) {
        char hex[4];
        std::snprintf(hex, sizeof hex, "%02X ", b);
        text += hex;
    }
    text.pop_back();
    return text;
}

void addRecordLine(std::string& out, const std::string& field, const battle::ItemRecord& port,
                   const battle::ItemRecord& guest) {
    const std::string ours = recordText(port);
    const std::string theirs = recordText(guest);
    if (ours != theirs) {
        out += field + ": port " + ours + ", cartridge " + theirs + "\n";
    }
}

// An inventory slot by what a BattleItem holds of its record, on both sides: the flags' low three bits
// and the fifth byte's high four, which no item's record sets, are left out — a place no record made
// carries them into the slot it is merged into, and what reads them there is the battle menus'.
void addRecordLine(std::string& out, const std::string& field, const battle::BattleItem& port,
                   const battle::BattleItem& guest) {
    addRecordLine(out, field, battle::recordOf(port), battle::recordOf(guest));
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

// A place of the list: empty, or a record with any flags, targeting, quantity and fifth byte.
battle::ItemRecord drawPlace(std::mt19937& rng) {
    if (oneIn(rng, 2)) {
        return battle::kEmptyItemRecord;
    }
    return battle::ItemRecord{static_cast<std::uint8_t>(drawItem(rng)), byte(rng), byte(rng), drawQuantity(rng),
                              byte(rng)};
}

// The list, its counts drawn so a merge often starts at or runs past place 12, and now and then where
// the counts wrap.
battle::ItemsObtained drawItemsObtained(std::mt19937& rng) {
    battle::ItemsObtained list;
    for (battle::ItemRecord& place : list.entries) {
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
    for (const battle::ItemRecord& place : state.itemsObtained.entries) {
        text += " " + (battle::itemIn(place) == ItemId::EMPTY ? std::string{"-"}
                                                              : std::to_string(static_cast<int>(battle::itemIn(place))) +
                                                                    "x" + std::to_string(battle::quantityIn(place)));
    }
    const auto empty = std::count_if(state.inventory.items.begin(), state.inventory.items.end(),
                                     [](const battle::BattleItem& slot) { return slot.item == ItemId::EMPTY; });
    text += " empty slots " + std::to_string(empty);
    return text;
}

}  // namespace ostinato::trace
