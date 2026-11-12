// A battle's end on the oracle (docs/contracts/battle_rewards.md § C, § D): the wrap-up and the title
// program's laying out are where the US cartridges hold them and an image without them is refused;
// the last three places a console just switched on holds, read three ways — by hand, by the port out
// of the cartridge's own title program, and off the cartridge after it lays the program out; the list's
// hand-worked cases run on the graphics program's own commands, the port matching both; then 500 drawn
// scenarios through each of the two commands and the wrap-up, with every cell the wrap-up read written
// first.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "assets/cartridge_routines.h"
#include "battle/battle_inventory.h"
#include "battle/items_obtained.h"
#include "damage_oracle.h"
#include "game/character_record.h"
#include "ostinato/item_id.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattleItem;
using trace::DamageOracle;
using trace::EndingScenario;
using trace::ItemsObtainedScenario;

constexpr std::uint32_t kWrapUp = 0xC24903;
constexpr std::uint32_t kDecompCutsceneProg = 0xC26828;

constexpr int kDraws = 500;

constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

class BattleEndOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    template <typename Body>
    static void withOracle(Body body) {
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            body(oracle);
        });
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* BattleEndOracleTest::image_ = nullptr;

BattleItem itemOf(ItemId id, std::uint8_t quantity) {
    static const battle::BattleState noParty;
    return battle::battleItem(noParty, id, quantity);
}

// --- where the routines are ----------------------------------------------------------------------

TEST_F(BattleEndOracleTest, TheRoutinesAreWhereTheUsCartridgesHoldThem) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const auto bytes = [&](std::uint32_t at) {
            return oracle.machine().read(retropp::MemoryRegion{.at = at, .size = 4});
        };
        EXPECT_EQ(bytes(kWrapUp), (std::vector<std::uint8_t>{0x20, 0x36, 0x0B, 0xA2}));  // jsr SaveMorphCounter
        EXPECT_EQ(bytes(kDecompCutsceneProg), (std::vector<std::uint8_t>{0x08, 0xC2, 0x30, 0x48}));  // php; rep
    });
}

TEST_F(BattleEndOracleTest, AnImageWithoutThemIsRefused) {
    for (const std::uint32_t routine : {kWrapUp, kDecompCutsceneProg}) {
        trace::runOnLoop([&] {
            const std::span<const std::uint8_t> original = image();
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(routine)] ^= 0xFF;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << std::hex << routine;
        });
    }
}

// --- a console just switched on ------------------------------------------------------------------

TEST_F(BattleEndOracleTest, TheLastThreePlacesAtPowerOnAreThreeItemsInNumbersPastTheMost) {
    // By hand from cutscene/opening.asm:1309-1314 laid at $7E:5000: 108 Thief Knives, 112 Silk Robes,
    // 136 Excaliburs.
    const assets::IngestedContent content = assets::ingestCartridge(image());
    ASSERT_NE(content.routines, nullptr);
    const battle::ItemsObtainedTail port = battle::itemsObtainedTailAtPowerOn(
        content.routines->decompress(assets::compressedStream(content.titleProgram)));
    EXPECT_EQ(port[0].item, ItemId::THIEFKNIFE);
    EXPECT_EQ(port[0].quantity, 108);
    EXPECT_EQ(port[1].item, ItemId::SILK_ROBE);
    EXPECT_EQ(port[1].quantity, 112);
    EXPECT_EQ(port[2].item, ItemId::EXCALIBUR);
    EXPECT_EQ(port[2].quantity, 136);
    withOracle([&](DamageOracle& oracle) {
        const battle::ItemsObtainedTail guest = oracle.itemsObtainedTailAtPowerOn();
        for (std::size_t place = 0; place < guest.size(); ++place) {
            EXPECT_EQ(battle::recordOf(guest[place]), battle::recordOf(port[place])) << place;
        }
    });
}

// --- the list, by hand ---------------------------------------------------------------------------

ItemsObtainedScenario merging(std::array<BattleItem, game::kInventorySlots> inventory, battle::ItemsObtained list) {
    return ItemsObtainedScenario{.list = list, .inventory = inventory};
}

void expectAgreement(DamageOracle& oracle, const ItemsObtainedScenario& s, const char* what) {
    EXPECT_EQ(trace::differences(trace::portMergeItemsObtained(s), oracle.mergeItemsObtained(s)), "") << what;
}

TEST_F(BattleEndOracleTest, TheListByHand) {
    withOracle([](DamageOracle& oracle) {
        {
            // An item added in the place its count names, the count past 16.
            ItemsObtainedScenario s;
            s.list.added = 17;
            s.item = ItemId::POTION;
            s.quantity = 3;
            EXPECT_EQ(trace::differences(trace::portAddItemObtained(s), oracle.addItemObtained(s)), "");
        }
        {
            // Onto a held item: 98 + 5 held to 99, and 200 + 100 keeping its low byte, 44.
            std::array<BattleItem, game::kInventorySlots> inventory{};
            inventory[0] = itemOf(ItemId::POTION, 98);
            inventory[1] = itemOf(ItemId::ETHER, 200);
            battle::ItemsObtained list;
            list.entries[0] = itemOf(ItemId::POTION, 5);
            list.entries[1] = itemOf(ItemId::ETHER, 100);
            expectAgreement(oracle, merging(inventory, list), "the 99 cap and the low byte");
        }
        {
            // A new item into the first empty slot, quantity and all.
            std::array<BattleItem, game::kInventorySlots> inventory{};
            inventory[1] = itemOf(ItemId::POTION, 1);
            battle::ItemsObtained list;
            list.entries[0] = itemOf(ItemId::THIEFKNIFE, 108);
            expectAgreement(oracle, merging(inventory, list), "a new item whole");
        }
        {
            // A full inventory stopping the merge, a held item waiting behind the stuck one.
            std::array<BattleItem, game::kInventorySlots> inventory{};
            inventory.fill(itemOf(ItemId::TONIC, 1));
            battle::ItemsObtained list;
            list.entries[0] = itemOf(ItemId::POTION, 1);
            list.entries[1] = itemOf(ItemId::TONIC, 1);
            expectAgreement(oracle, merging(inventory, list), "a full inventory");
        }
        {
            // The thirteenth merged: the walk on through the last three places to place 0.
            std::array<BattleItem, game::kInventorySlots> inventory{};
            inventory[0] = itemOf(ItemId::THIEFKNIFE, 50);
            battle::ItemsObtained list = battle::itemsObtainedAtBattleStart(
                {itemOf(ItemId::THIEFKNIFE, 108), itemOf(ItemId::SILK_ROBE, 112), itemOf(ItemId::EXCALIBUR, 136)});
            list.entries[12] = itemOf(ItemId::POTION, 1);
            list.added = 13;
            list.merged = 12;
            expectAgreement(oracle, merging(inventory, list), "the walk past place 12");
        }
    });
}

TEST_F(BattleEndOracleTest, TheWrapUpByHand) {
    // Two hands holding, one marked and one not, someone morphed, an item still waiting in the list.
    EndingScenario s;
    battle::BattleState& state = s.state;
    state.inventory.items[0] = itemOf(ItemId::POTION, 98);
    state.at(battle::BattlerSlot::character(0)).itemInHand = ItemId::POTION;
    state.at(battle::BattlerSlot::character(2)).itemInHand = ItemId::ETHER;
    state.holdingItem.add(battle::BattlerSlot::character(0));
    state.itemsObtained.entries[0] = itemOf(ItemId::ELIXIR, 2);
    state.itemsObtained.added = 1;
    state.morphedBattler = battle::BattlerSlot::character(1);
    state.morphCounter = 0x80FF;
    s.cells.party.morphSupply = 200;
    withOracle([&](DamageOracle& oracle) {
        EXPECT_EQ(trace::differences(trace::portWrapUp(s), oracle.wrapUp(s)), "");
        for (const trace::UnwrittenRead& read : oracle.unwrittenReads()) {
            EXPECT_TRUE(trace::leftAsTheMachineHasIt(read.at)) << std::hex << read.at;
        }
    });
}

// --- drawn ---------------------------------------------------------------------------------------

TEST_F(BattleEndOracleTest, ItemsAddedSweep) {
    std::mt19937 rng{trace::kSweepSeed};
    withOracle([&](DamageOracle& oracle) {
        for (int draw = 0; draw < kDraws; ++draw) {
            const ItemsObtainedScenario s = trace::drawItemsObtainedScenario(rng);
            const std::string differs = trace::differences(trace::portAddItemObtained(s), oracle.addItemObtained(s));
            ASSERT_EQ(differs, "") << "draw " << draw;
        }
    });
}

TEST_F(BattleEndOracleTest, ItemsMergedSweep) {
    std::mt19937 rng{trace::kSweepSeed};
    withOracle([&](DamageOracle& oracle) {
        for (int draw = 0; draw < kDraws; ++draw) {
            const ItemsObtainedScenario s = trace::drawItemsObtainedScenario(rng);
            const std::string differs =
                trace::differences(trace::portMergeItemsObtained(s), oracle.mergeItemsObtained(s));
            ASSERT_EQ(differs, "") << "draw " << draw;
        }
    });
}

TEST_F(BattleEndOracleTest, WrapUpSweep) {
    std::mt19937 rng{trace::kSweepSeed};
    withOracle([&](DamageOracle& oracle) {
        for (int draw = 0; draw < kDraws; ++draw) {
            const EndingScenario s = trace::drawEndingScenario(rng);
            const std::string differs = trace::differences(trace::portWrapUp(s), oracle.wrapUp(s));
            ASSERT_EQ(differs, "") << "draw " << draw << ": " << trace::describe(s);
            for (const trace::UnwrittenRead& read : oracle.unwrittenReads()) {
                ASSERT_TRUE(trace::leftAsTheMachineHasIt(read.at)) << "draw " << draw << " read " << std::hex << read.at;
            }
        }
    });
}

}  // namespace
}  // namespace ostinato::test
