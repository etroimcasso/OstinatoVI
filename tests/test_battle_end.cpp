// A battle's end (docs/contracts/battle_rewards.md § C), worked out by hand from the original: the
// wrap-up every ending but two runs — the Morph supply kept, the menus closed last slot first, no
// menu opening again, 32 frames, every hand put away, the items obtained merged, and equipment the
// Item menu changed taking effect.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "battle/battle_end.h"
#include "battle/battle_inventory.h"
#include "battle/items_obtained.h"
#include "game/character_record.h"
#include "ostinato/item_id.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using battle::BattleItem;
using battle::BattlerSlot;
using battle::BattleState;

BattleItem itemOf(ItemId id, std::uint8_t quantity) {
    static const BattleState noParty;
    return battle::battleItem(noParty, id, quantity);
}

BattleState wrappedUp(BattleState state) {
    battle::NullExecutor executor;
    battle::wrapUpBattle(state, executor);
    return state;
}

TEST(WrapUp, ItClosesEveryMenuLastSlotFirstAndWaits32Frames) {
    // _c24903 :12065-12080: slots 3 to 0, each meter shown emptied and its menu closed ($03), menus kept
    // from opening ($b1.7), then 32 waits for a frame.
    BattleState state;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        state.at(BattlerSlot::character(position)).morphGauge = 200;
    }
    state = wrappedUp(state);
    ASSERT_EQ(state.requests.size(), 5u);
    for (int i = 0; i < 4; ++i) {
        const auto& closed = std::get<battle::CloseBattleMenu>(state.requests[static_cast<std::size_t>(i)]);
        EXPECT_EQ(closed.character, BattlerSlot::character(3 - i));
    }
    const auto& script = std::get<battle::RunBattleScript>(state.requests[4]);
    ASSERT_EQ(script.commands.size(), 1u);
    EXPECT_EQ(std::get<battle::Wait>(script.commands[0]).frames, 32);
    EXPECT_TRUE(state.menusDisabled);
    for (int position = 0; position < battle::kPartySlots; ++position) {
        EXPECT_EQ(state.at(BattlerSlot::character(position)).morphGauge, 0) << position;
    }
}

TEST(WrapUp, EveryHandIsPutAwayWhetherOrNotItWasMarked) {
    // `lda #$0f; tsb $3a8c` (:12081-12082) marks all four before _c262c7 runs: an item chosen for Item
    // or Throw and never used comes back. Last slot first.
    BattleState state;
    state.at(BattlerSlot::character(0)).itemInHand = ItemId::POTION;
    state.at(BattlerSlot::character(3)).itemInHand = ItemId::ETHER;
    state = wrappedUp(state);
    ASSERT_EQ(state.requests.size(), 7u);
    EXPECT_EQ(std::get<battle::AddItemObtained>(state.requests[5]).item, ItemId::ETHER);
    EXPECT_EQ(std::get<battle::AddItemObtained>(state.requests[6]).item, ItemId::POTION);
    EXPECT_FALSE(state.at(BattlerSlot::character(0)).itemInHand.has_value());
    EXPECT_FALSE(state.at(BattlerSlot::character(3)).itemInHand.has_value());
    EXPECT_TRUE(state.holdingItem.empty());
    // ... and the list is merged ($0A, :12084-12085): both are in the inventory.
    EXPECT_EQ(state.inventory.items[0].item, ItemId::ETHER);
    EXPECT_EQ(state.inventory.items[1].item, ItemId::POTION);
    EXPECT_EQ(state.itemsObtained.merged, 2);
}

TEST(WrapUp, ItemsStillWaitingInTheListReachTheInventory) {
    BattleState state;
    state.inventory.items[0] = itemOf(ItemId::POTION, 98);
    battle::obtainItem(state, ItemId::POTION, 3);
    state = wrappedUp(state);
    EXPECT_EQ(state.inventory.items[0].quantity, 99);
    EXPECT_EQ(state.itemsObtained.merged, 1);
}

TEST(WrapUp, TheMorphSupplyIsKeptAsTheMeterStands) {
    // SaveMorphCounter :1762-1771: the supply times the meter's high byte, over 256.
    BattleState state;
    state.party.morphSupply = 200;
    state.morphedBattler = BattlerSlot::character(1);
    state.morphCounter = 0x80FF;
    EXPECT_EQ(wrappedUp(state).party.morphSupply, 100);
    state.morphedBattler.reset();
    EXPECT_EQ(wrappedUp(state).party.morphSupply, 200);
}

TEST(WrapUp, EquipmentTheItemMenuChangedTakesEffect) {
    // UpdateCharProp :5595-5631: the record takes the hands' items.
    BattleState state = battleWithParty({CharacterPropId::TERRA});
    battle::Battler& terra = state.at(BattlerSlot::character(0));
    terra.record = 0;
    terra.actor = CharacterPropId::TERRA;
    terra.present = true;
    terra.equipmentChanged = true;
    state.inventory.weaponHands[0] = itemOf(ItemId::MITHRILBLADE, 1);
    state = wrappedUp(state);
    EXPECT_EQ(state.party.characters[0].weapon, ItemId::MITHRILBLADE);
    EXPECT_FALSE(state.at(BattlerSlot::character(0)).equipmentChanged);
}

}  // namespace
}  // namespace ostinato::test
