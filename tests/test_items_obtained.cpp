// The items a battle gives the party, waiting in their list before the inventory
// (docs/contracts/battle_rewards.md § D), worked out by hand from the original: the sixteen places
// and their two counts, the merge into the inventory with its 99 cap and its stop at a full
// inventory, the battle's start that empties thirteen places, the last three carried in and out of
// a battle, and the cartridge's form of a place.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "battle/battle_inventory.h"
#include "battle/battle_start.h"
#include "battle/items_obtained.h"
#include "data/battle_tables.h"
#include "data/formations.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "game/character_record.h"
#include "ostinato/item_id.h"
#include "ostinato/text_class.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using battle::BattleItem;
using battle::BattlerSlot;
using battle::BattleState;
using battle::ItemsObtained;
using battle::ItemsObtainedTail;
using Inventory = std::array<BattleItem, game::kInventorySlots>;

// An item as the battle works it out with no one in the party.
BattleItem itemOf(ItemId id, std::uint8_t quantity) {
    static const BattleState noParty;
    return battle::battleItem(noParty, id, quantity);
}

// An inventory holding `held` in its first slots, the rest empty.
Inventory inventoryHolding(std::initializer_list<BattleItem> held) {
    Inventory inventory{};
    std::size_t slot = 0;
    for (const BattleItem& item : held) {
        inventory[slot++] = item;
    }
    return inventory;
}

// An inventory with every slot taken by one Tonic after another.
Inventory fullOfTonics() {
    Inventory inventory{};
    inventory.fill(itemOf(ItemId::TONIC, 1));
    return inventory;
}

// The fifteen bytes a console just switched on holds in the last three places: the title program's
// Magitek armor frame (cutscene/opening.asm:1309-1314).
constexpr std::array<std::uint8_t, 15> kPowerOnTailBytes{0x04, 0x98, 0x08, 0x6C, 0x70, 0x88, 0x08, 0x6E,
                                                         0x70, 0x98, 0x18, 0x80, 0x70, 0x88, 0x18};

ItemsObtainedTail powerOnTail() {
    ItemsObtainedTail tail;
    for (std::size_t i = 0; i < tail.size(); ++i) {
        tail[i] = battle::battleItemFromRecord(std::span<const std::uint8_t>(kPowerOnTailBytes).subspan(i * 5).first<5>());
    }
    return tail;
}

// A battle with `inventory` and the list as a battle starts with `tail`.
BattleState battleWith(const Inventory& inventory, const ItemsObtainedTail& tail = {}) {
    BattleState state;
    state.inventory.items = inventory;
    state.itemsObtained = battle::itemsObtainedAtBattleStart(tail);
    return state;
}

// Obtain one of `item` and merge, as each drop and the colosseum's prize do.
void obtainAndMerge(BattleState& state, ItemId item, std::uint8_t quantity = 1) {
    battle::obtainItem(state, item, quantity);
    battle::mergeItemsObtained(state);
}

const BattleItem* slotHolding(const BattleState& state, ItemId item) {
    for (const BattleItem& slot : state.inventory.items) {
        if (slot.item == item) {
            return &slot;
        }
    }
    return nullptr;
}

// --- the list ------------------------------------------------------------------------------------

TEST(ItemsObtained, ABattleStartsWithThirteenEmptyPlacesAndItsTail) {
    // G:43456-43460 zeroes both counts; G:2475-2480 writes $FF over 64 bytes: places 0-11 whole and
    // place 12's first four bytes, which reads as empty. Places 13-15 are not reached.
    const ItemsObtained list = battle::itemsObtainedAtBattleStart(powerOnTail());
    EXPECT_EQ(list.added, 0);
    EXPECT_EQ(list.merged, 0);
    for (std::size_t place = 0; place < battle::kItemsObtainedEmptiedPlaces; ++place) {
        EXPECT_EQ(list.entries[place].item, ItemId::EMPTY) << place;
    }
    EXPECT_EQ(list.entries[13].item, ItemId::THIEFKNIFE);
    EXPECT_EQ(list.entries[14].item, ItemId::SILK_ROBE);
    EXPECT_EQ(list.entries[15].item, ItemId::EXCALIBUR);
    EXPECT_EQ(list.entries[15].quantity, 136);
}

TEST(ItemsObtained, AnItemGoesInThePlaceItsCountNames) {
    // BtlGfx_05 G:357-373: the record goes to place `added & 15`, and the count goes up.
    BattleState state = battleWith({});
    state.itemsObtained.added = 17;
    battle::obtainItem(state, ItemId::POTION, 3);
    EXPECT_EQ(state.itemsObtained.added, 18);
    EXPECT_EQ(state.itemsObtained.entries[1].item, ItemId::POTION);
    EXPECT_EQ(state.itemsObtained.entries[1].quantity, 3);
    EXPECT_EQ(battle::recordOf(state.itemsObtained.entries[1]), battle::recordOf(itemOf(ItemId::POTION, 3)));
}

TEST(ItemsObtained, TheSeventeenthItemGoesBackToTheFirstPlace) {
    BattleState state = battleWith({});
    for (int n = 0; n < 16; ++n) {
        battle::obtainItem(state, ItemId::POTION, 1);
    }
    battle::obtainItem(state, ItemId::ETHER, 1);
    EXPECT_EQ(state.itemsObtained.added, 17);
    EXPECT_EQ(state.itemsObtained.entries[0].item, ItemId::ETHER);
    EXPECT_EQ(state.itemsObtained.entries[1].item, ItemId::POTION);
}

// --- the merge -----------------------------------------------------------------------------------

TEST(ItemsObtained, AMergeAddsTheQuantityToAHeldItemHeldTo99) {
    // _c14445 G:9482-9488: the add, then `cmp #$64; bcc; lda #$63`.
    struct Case {
        std::uint8_t held, obtained, after;
    };
    for (const Case c : {Case{98, 1, 99}, Case{98, 5, 99}, Case{40, 59, 99}, Case{40, 58, 98}, Case{1, 99, 99}}) {
        BattleState state = battleWith(inventoryHolding({itemOf(ItemId::POTION, c.held)}));
        obtainAndMerge(state, ItemId::POTION, c.obtained);
        EXPECT_EQ(state.inventory.items[0].quantity, c.after) << int{c.held} << " + " << int{c.obtained};
        EXPECT_EQ(state.inventory.items[1].item, ItemId::EMPTY);
    }
}

TEST(ItemsObtained, AQuantitySumKeepsItsLowByteBeforeItIsHeld) {
    // The add is eight bits wide and its carry unread (G:9482-9485): 200 + 100 is 44, 250 + 10 is 4.
    struct Case {
        std::uint8_t held, obtained, after;
    };
    for (const Case c : {Case{200, 100, 44}, Case{250, 10, 4}, Case{200, 156, 99}}) {
        BattleState state = battleWith(inventoryHolding({itemOf(ItemId::POTION, c.held)}));
        obtainAndMerge(state, ItemId::POTION, c.obtained);
        EXPECT_EQ(state.inventory.items[0].quantity, c.after) << int{c.held} << " + " << int{c.obtained};
    }
}

TEST(ItemsObtained, AnItemNotHeldGoesWholeIntoTheFirstEmptySlot) {
    // G:9469-9479: CheckInventoryFull finds the first empty slot and the place's five bytes are copied
    // into it — the quantity as it is, past 99 too.
    Inventory inventory = inventoryHolding({itemOf(ItemId::POTION, 5), itemOf(ItemId::ETHER, 5)});
    inventory[0] = BattleItem{};  // the first slot empty, the second held
    BattleState state = battleWith(inventory);
    obtainAndMerge(state, ItemId::THIEFKNIFE, 108);
    EXPECT_EQ(battle::recordOf(state.inventory.items[0]), battle::recordOf(itemOf(ItemId::THIEFKNIFE, 108)));
    EXPECT_EQ(state.inventory.items[1].item, ItemId::ETHER);
}

TEST(ItemsObtained, TheMergeEmptiesWhatItMergedAndStopsAtTheFirstEmptyPlace) {
    BattleState state = battleWith({});
    battle::obtainItem(state, ItemId::POTION, 1);
    battle::obtainItem(state, ItemId::ETHER, 2);
    battle::mergeItemsObtained(state);
    EXPECT_EQ(state.itemsObtained.merged, 2);
    EXPECT_EQ(state.itemsObtained.entries[0].item, ItemId::EMPTY);
    EXPECT_EQ(state.itemsObtained.entries[1].item, ItemId::EMPTY);
    ASSERT_NE(slotHolding(state, ItemId::ETHER), nullptr);
    EXPECT_EQ(slotHolding(state, ItemId::ETHER)->quantity, 2);
    // Nothing more waits: a second merge changes nothing.
    battle::mergeItemsObtained(state);
    EXPECT_EQ(state.itemsObtained.merged, 2);
}

TEST(ItemsObtained, AFullInventoryStopsTheMergeAndEverythingBehindItWaits) {
    // `bcs @44a1` (G:9469-9470): no empty slot ends the merge with the place kept and `merged` where it
    // was, so a held item obtained after it waits too.
    BattleState state = battleWith(fullOfTonics());
    battle::obtainItem(state, ItemId::POTION, 1);
    battle::obtainItem(state, ItemId::TONIC, 1);
    battle::mergeItemsObtained(state);
    EXPECT_EQ(state.itemsObtained.merged, 0);
    EXPECT_EQ(state.itemsObtained.entries[0].item, ItemId::POTION);
    EXPECT_EQ(state.itemsObtained.entries[1].item, ItemId::TONIC);
    EXPECT_EQ(state.inventory.items[0].quantity, 1);
    // A later merge stops at the same place.
    battle::mergeItemsObtained(state);
    EXPECT_EQ(state.itemsObtained.merged, 0);
}

// --- the last three places -------------------------------------------------------------------------

TEST(ItemsObtained, TheThirteenthItemBringsTheLastThreePlacesIntoTheInventory) {
    // Merged after each item, as drops are: the thirteenth leaves `merged` at 13, and the merge walks
    // on through places 13-15, which the battle's start left as they were, to place 0, emptied.
    BattleState state = battleWith(inventoryHolding({itemOf(ItemId::THIEFKNIFE, 50)}), powerOnTail());
    for (int n = 0; n < 13; ++n) {
        obtainAndMerge(state, ItemId::POTION);
    }
    EXPECT_EQ(state.itemsObtained.merged, 16);
    EXPECT_EQ(state.inventory.items[0].quantity, 99);  // 50 + 108, held to 99
    ASSERT_NE(slotHolding(state, ItemId::SILK_ROBE), nullptr);
    EXPECT_EQ(slotHolding(state, ItemId::SILK_ROBE)->quantity, 112);
    ASSERT_NE(slotHolding(state, ItemId::EXCALIBUR), nullptr);
    EXPECT_EQ(slotHolding(state, ItemId::EXCALIBUR)->quantity, 136);
    for (std::size_t place = 13; place < 16; ++place) {
        EXPECT_EQ(state.itemsObtained.entries[place].item, ItemId::EMPTY) << place;
    }
}

TEST(ItemsObtained, AnEmptyFourteenthPlaceStopsTheWalkAtTheThirteenth) {
    BattleState state = battleWith({}, ItemsObtainedTail{BattleItem{}, itemOf(ItemId::SILK_ROBE, 112), BattleItem{}});
    for (int n = 0; n < 13; ++n) {
        obtainAndMerge(state, ItemId::POTION);
    }
    EXPECT_EQ(state.itemsObtained.merged, 13);
    EXPECT_EQ(slotHolding(state, ItemId::SILK_ROBE), nullptr);
}

TEST(ItemsObtained, AFullInventoryStopsTheWalkAtTheLastThreePlaces) {
    // The thirteenth (a Tonic) adds to the Tonics; the Thief Knife has nowhere to go.
    BattleState state = battleWith(fullOfTonics(), powerOnTail());
    for (int n = 0; n < 13; ++n) {
        obtainAndMerge(state, ItemId::TONIC);
    }
    EXPECT_EQ(state.itemsObtained.merged, 13);
    EXPECT_EQ(state.itemsObtained.entries[13].item, ItemId::THIEFKNIFE);
}

TEST(ItemsObtained, TheFourteenthToSixteenthWaitUntilTheTwentyNinth) {
    // With `merged` at 16 the merge looks at place 0, so the items in places 13-15 wait; the 29th
    // fills place 12 and the walk runs on through them.
    // The nth item obtained is item n, so each shows in the inventory on its own.
    BattleState full = battleWith({}, powerOnTail());
    for (int n = 1; n <= 16; ++n) {
        obtainAndMerge(full, static_cast<ItemId>(n));
    }
    EXPECT_EQ(full.itemsObtained.merged, 16);
    for (int n = 14; n <= 16; ++n) {
        EXPECT_EQ(slotHolding(full, static_cast<ItemId>(n)), nullptr) << n;
    }
    for (int n = 17; n <= 28; ++n) {
        obtainAndMerge(full, static_cast<ItemId>(n));
    }
    EXPECT_EQ(slotHolding(full, static_cast<ItemId>(14)), nullptr);
    obtainAndMerge(full, static_cast<ItemId>(29));
    for (int n = 14; n <= 16; ++n) {
        EXPECT_NE(slotHolding(full, static_cast<ItemId>(n)), nullptr) << n;
    }
}

TEST(ItemsObtained, WithAnEmptyTailTheFourteenthToSixteenthMergeAsTheyCome) {
    BattleState state = battleWith({}, ItemsObtainedTail{});
    for (int n = 1; n <= 16; ++n) {
        obtainAndMerge(state, static_cast<ItemId>(n));
        EXPECT_NE(slotHolding(state, static_cast<ItemId>(n)), nullptr) << n;
    }
    EXPECT_EQ(state.itemsObtained.merged, 16);
}

// --- carried in and out of a battle ------------------------------------------------------------------

// The monster-name table: 384 names of ten bytes.
constexpr std::size_t kMonsterNameBytes = 3840;

const TextCorpus& noNames() {
    static const TextCorpus corpus = [] {
        std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
        buffers[static_cast<std::size_t>(TextClass::MONSTER_NAME)].assign(kMonsterNameBytes, 0xFF);
        return TextCorpus{std::move(buffers)};
    }();
    return corpus;
}

const MonsterSprites& noSprites() {
    static const MonsterSprites sprites;
    return sprites;
}

game::Party threeMembers() {
    return battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR}).party;
}

TEST(ItemsObtained, ABattleStartsWithTheLastThreePlacesItWasHanded) {
    battle::NullExecutor executor;
    battle::BattleStart start{.formation = FormationRef::of(FormationId::LOBO), .itemsObtainedTail = powerOnTail()};
    const BattleState state = battle::startBattle(start, threeMembers(), {}, noNames(), noSprites(), executor);
    EXPECT_EQ(state.itemsObtained.added, 0);
    EXPECT_EQ(state.itemsObtained.merged, 0);
    EXPECT_EQ(state.itemsObtained.entries[12].item, ItemId::EMPTY);
    EXPECT_EQ(battle::itemsObtainedTail(state.itemsObtained)[0].item, ItemId::THIEFKNIFE);
    EXPECT_EQ(battle::itemsObtainedTail(state.itemsObtained)[2].quantity, 136);
}

TEST(ItemsObtained, TheBattleHandsBackTheLastThreePlacesAsItLeavesThem) {
    BattleState state = battleWith({}, powerOnTail());
    state.itemsObtained.entries[14] = BattleItem{};  // a merge emptied it
    state.itemsObtained.entries[15] = itemOf(ItemId::ELIXIR, 1);  // the sixteenth item obtained
    state.outcome = battle::BattleWon{};
    const battle::BattleResult result = battle::writeBack(state);
    EXPECT_EQ(result.itemsObtainedTail[0].item, ItemId::THIEFKNIFE);
    EXPECT_EQ(result.itemsObtainedTail[1].item, ItemId::EMPTY);
    EXPECT_EQ(result.itemsObtainedTail[2].item, ItemId::ELIXIR);
}

TEST(ItemsObtained, AFinalBattleStageKeepsTheWholeListAndBothCounts) {
    // InitBattle :6180-6193: a stage that scrolls the background in plays script command $12 in place of
    // the graphics program's start, which zeroes the counts and empties the list.
    battle::NullExecutor executor;
    battle::BattleStart start{.formation = FormationRef::of(kFinalBattleFormations[0])};
    start.finalBattleOrder = {0, 1, 2};
    BattleState finished = battle::startBattle(start, threeMembers(), {}, noNames(), noSprites(), executor);
    battle::obtainItem(finished, ItemId::POTION, 1);
    battle::obtainItem(finished, ItemId::ETHER, 1);
    finished.itemsObtained.merged = 1;
    finished.outcome = battle::BattleEnded{.reason = battle::BattleEndReason::FinalBattleNextStage,
                                           .nextFormation = kFinalBattleFormations[1]};
    const BattleState next = battle::startNextFinalBattleStage(finished, battle::writeBack(finished), start, noNames(),
                                                               noSprites(), executor);
    EXPECT_EQ(next.itemsObtained.added, 2);
    EXPECT_EQ(next.itemsObtained.merged, 1);
    EXPECT_EQ(next.itemsObtained.entries[0].item, ItemId::POTION);
    EXPECT_EQ(next.itemsObtained.entries[1].item, ItemId::ETHER);
}

// --- the hands -------------------------------------------------------------------------------------

TEST(ItemsObtained, AHandAtAnActionsEndJoinsTheListUnmergedAndIsStillShown) {
    // _c262c7 :16168-16187 adds one of each marked hand's item to the list ($05) and empties the hand;
    // nothing merges until a menu opens or the battle ends.
    BattleState state = battleWith({});
    state.at(BattlerSlot::character(2)).itemInHand = ItemId::POTION;
    state.holdingItem.add(BattlerSlot::character(2));
    battle::putAwayItemsObtained(state);
    ASSERT_EQ(state.requests.size(), 1u);
    EXPECT_EQ(std::get<battle::AddItemObtained>(state.requests[0]).item, ItemId::POTION);
    EXPECT_EQ(state.itemsObtained.entries[0].item, ItemId::POTION);
    EXPECT_EQ(state.itemsObtained.entries[0].quantity, 1);
    EXPECT_EQ(state.itemsObtained.added, 1);
    EXPECT_EQ(state.itemsObtained.merged, 0);
    EXPECT_EQ(slotHolding(state, ItemId::POTION), nullptr);
    EXPECT_FALSE(state.at(BattlerSlot::character(2)).itemInHand.has_value());
}

// --- the cartridge's form of a place -------------------------------------------------------------------

TEST(ItemRecord, EachBitReadsAsTheBattleWorksItOut) {
    // LoadItemProp :13981-14037: $80 set when the item cannot be used in battle, $20 throwable, and the
    // item type's bits shifted once — $40 a tool, $10 a weapon, $08 a shield; the fifth byte one bit per
    // party slot, set where that member cannot hold it.
    const std::array<std::uint8_t, 5> record{0x2A, 0x80 | 0x40 | 0x20 | 0x10 | 0x08, 0x41, 7, 0x05};
    const BattleItem item = battle::battleItemFromRecord(record);
    EXPECT_EQ(item.item, static_cast<ItemId>(0x2A));
    EXPECT_FALSE(item.usableInBattle);
    EXPECT_TRUE(item.isTool);
    EXPECT_TRUE(item.throwable);
    EXPECT_TRUE(item.isWeapon);
    EXPECT_TRUE(item.isShield);
    EXPECT_EQ(item.targeting.bits, 0x41);
    EXPECT_EQ(item.quantity, 7);
    EXPECT_EQ(item.equippableBy.bits, 0x0A);  // slots 1 and 3 can hold it
    const BattleItem plain = battle::battleItemFromRecord(std::array<std::uint8_t, 5>{0x2A, 0, 0, 1, 0x0F});
    EXPECT_TRUE(plain.usableInBattle);
    EXPECT_FALSE(plain.isTool || plain.throwable || plain.isWeapon || plain.isShield);
    EXPECT_EQ(plain.equippableBy.bits, 0);
}

TEST(ItemRecord, EveryItemTheBattleWorksOutRoundTripsThroughItsRecord) {
    BattleState state = battleWithParty({CharacterPropId::TERRA, CharacterPropId::GAU, CharacterPropId::CYAN,
                                         CharacterPropId::UMARO});
    const std::array<CharacterPropId, 4> actors{CharacterPropId::TERRA, CharacterPropId::GAU, CharacterPropId::CYAN,
                                                CharacterPropId::UMARO};
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const auto bit = static_cast<std::uint16_t>(1u << static_cast<int>(actors[static_cast<std::size_t>(position)]));
        state.at(BattlerSlot::character(position)).equipsAs =
            EquipPermissions{static_cast<std::uint8_t>(bit & 0xFF), static_cast<std::uint8_t>(bit >> 8)};
    }
    for (int id = 0; id < 255; ++id) {
        const BattleItem item = battle::battleItem(state, static_cast<ItemId>(id), 42);
        const BattleItem back = battle::battleItemFromRecord(battle::recordOf(item));
        EXPECT_EQ(battle::recordOf(back), battle::recordOf(item)) << id;
        EXPECT_EQ(back.usableInBattle, item.usableInBattle) << id;
        EXPECT_EQ(back.equippableBy.bits, item.equippableBy.bits) << id;
    }
}

TEST(ItemRecord, ThePowerOnTailIsReadFromTheTitleProgram) {
    // The list's fourteenth place, $7E:602D + 65, less where the title program is laid, $7E:5000.
    std::vector<std::uint8_t> program(0x106E + 15, 0);
    std::copy(kPowerOnTailBytes.begin(), kPowerOnTailBytes.end(), program.begin() + 0x106E);
    const ItemsObtainedTail read = battle::itemsObtainedTailAtPowerOn(program);
    EXPECT_EQ(read[0].item, ItemId::THIEFKNIFE);
    EXPECT_EQ(read[0].quantity, 108);
    EXPECT_EQ(read[1].item, ItemId::SILK_ROBE);
    EXPECT_EQ(read[1].quantity, 112);
    EXPECT_EQ(read[2].item, ItemId::EXCALIBUR);
    EXPECT_EQ(read[2].quantity, 136);
    EXPECT_EQ(battle::recordOf(read[2])[1], 0x80);
}

TEST(ItemRecord, ATitleProgramTooShortToHoldTheTailThrows) {
    const std::vector<std::uint8_t> program(0x106E + 14, 0);
    EXPECT_THROW((void)battle::itemsObtainedTailAtPowerOn(program), std::out_of_range);
}

}  // namespace
}  // namespace ostinato::test
