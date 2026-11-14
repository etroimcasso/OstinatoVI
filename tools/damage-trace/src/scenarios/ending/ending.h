// A battle's end: the wrap-up, the win and the loss, the write-back of what the game keeps, and the
// items obtained, each with what it leaves, the port's answer, the comparison and the sweeps'
// draws.
#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/item_id.h"

#include "scenarios/ending/rewards.h"

namespace ostinato::trace {

// A battle at its end as an ending reads it: the party (its game::Party is the battle's party) and
// the battle.
struct EndingScenario {
    PartyCells cells;
    battle::BattleState state;
};

// Something an ending shows, as both sides are compared: a party member's battle menu closing, a
// wait of so many frames, an item a party member put away, a battle message, the acting battler
// stepping back, or the party celebrating a win. On the cartridge a wait is that many calls to wait
// for a frame, a merge of the items obtained shows nothing, an item added and merged at once (a drop,
// the colosseum's prize) shows only its message, and the celebration is one call — read with whether
// the fanfare plays and whether battle event 1 does, as the graphics program's command decides them
// from the formation's flags and the field's music. The port's celebration is every menu closing,
// then the fanfare and the event as it asks for them.
struct EndingCall {
    enum class Kind : std::uint8_t { CloseMenu, Wait, ItemObtained, Message, StepBack, Celebration, Other };
    Kind kind = Kind::Other;
    std::uint8_t slot = 0;       // CloseMenu: the party slot
    int frames = 0;              // Wait
    ItemId item = ItemId::EMPTY; // ItemObtained
    MessageShown message;        // Message
    bool fanfare = false;        // Celebration: the fanfare starts
    bool victoryEvent = false;   // Celebration: battle event 1 plays
    std::uint8_t command = 0;    // Other: the graphics command, or 0 for a request of the port's own
    friend bool operator==(const EndingCall&, const EndingCall&) = default;
};

// What an ending leaves, as the same fields on both sides: the party read back, the battle switches,
// the inventory, the items obtained with both counts, what each party member holds in hand and who
// is marked as holding something, the Morph meter shown per party slot, whether battle menus may
// open, the random-number position, and the calls in order.
struct EndingResult {
    PartyCells cells;
    game::BattleSwitches switches;
    std::array<battle::BattleItem, game::kInventorySlots> inventory{};
    battle::ItemsObtained itemsObtained;
    std::array<std::optional<ItemId>, battle::kPartySlots> hands{};
    std::uint8_t holdingItem = 0;
    std::array<std::uint8_t, battle::kPartySlots> morphGauges{};
    bool menusDisabled = false;
    std::uint8_t rngPosition = 0;
    std::vector<EndingCall> calls;
};

// The port's side of each ending over the scenario: the wrap-up (battle::wrapUpBattle), the win
// (battle::playBattleEnd with BattleWon), and the loss with `message` (BattleLost, then the switches
// as battle::writeBack() hands them back).
[[nodiscard]] EndingResult portWrapUp(const EndingScenario& scenario);
[[nodiscard]] EndingResult portWin(const EndingScenario& scenario);
[[nodiscard]] EndingResult portLoss(const EndingScenario& scenario, std::optional<std::uint8_t> message);

// What the game keeps from a battle, as the cartridge writes it to the party's cells: the party
// (each record's statuses, HP and MP, and who is in the party), the inventory, the battle switches,
// and the battles Gau can find on the Veldt (a bit per battle below 512).
struct WriteBackResult {
    PartyCells cells;
    std::array<game::InventorySlot, game::kInventorySlots> inventory{};
    game::BattleSwitches switches;
    std::bitset<512> veldtBattles;
};

// The port's side: battle::writeBack() over the scenario, the Veldt's battles being the scenario's
// with the result's veldtFormation added.
[[nodiscard]] WriteBackResult portWriteBack(const EndingScenario& scenario);
[[nodiscard]] std::string differences(const WriteBackResult& port, const WriteBackResult& guest);

// Every field where the two parted, one per line; empty when they agree. An inventory slot or a place
// of the list whose item is empty on both sides agrees whatever else it holds.
[[nodiscard]] std::string differences(const EndingResult& port, const EndingResult& guest);

// The items obtained and the inventory, as the graphics program's two commands for them read them:
// the list with both counts, the inventory, and the item a command adds and how many — handed over as
// the battle works its record out (battle::battleItem) with no one in the party.
struct ItemsObtainedScenario {
    battle::ItemsObtained list;
    std::array<battle::BattleItem, game::kInventorySlots> inventory{};
    ItemId item = ItemId::EMPTY;
    std::uint8_t quantity = 0;
};

struct ItemsObtainedResult {
    battle::ItemsObtained list;
    std::array<battle::BattleItem, game::kInventorySlots> inventory{};
};

// The port's side: the scenario's item put in the list as battle::obtainItem() puts it, and the list
// merged (battle::mergeItemsObtained).
[[nodiscard]] ItemsObtainedResult portAddItemObtained(const ItemsObtainedScenario& scenario);
[[nodiscard]] ItemsObtainedResult portMergeItemsObtained(const ItemsObtainedScenario& scenario);

[[nodiscard]] std::string differences(const ItemsObtainedResult& port, const ItemsObtainedResult& guest);

// A battle at its end, for the wrap-up: up to four party members, each holding an item in hand or
// not and marked as holding one or not, someone morphed or not; an inventory with room, with items
// the hands hold near 99, or full; and the items obtained empty, part-full or near the end of their
// sixteen places, with both counts drawn so a merge often runs past place 12 into the last three.
[[nodiscard]] EndingScenario drawEndingScenario(std::mt19937& rng);

// A battle won or lost, for the win, the loss and the write-back: one to four party members with a mix
// of actors (Terra, Mog, Gau, Gogo, Umaro and the guests often), alive or fallen, Cat Hoods and Exp.
// Eggs, espers, experience near the next levels; one to six monsters fallen, fled or standing; on the
// Veldt or not; the colosseum now and then; lores learned; every background; the fanfare on and off;
// the wrap-up's own draws besides.
[[nodiscard]] EndingScenario drawWinScenario(std::mt19937& rng);
[[nodiscard]] std::string describe(const EndingScenario& scenario);

// The list and the inventory for the graphics program's two commands, drawn the same way, with an
// item and a quantity to add.
[[nodiscard]] ItemsObtainedScenario drawItemsObtainedScenario(std::mt19937& rng);

}  // namespace ostinato::trace
