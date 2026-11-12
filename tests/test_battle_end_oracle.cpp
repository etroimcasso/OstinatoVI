// A battle's end on the oracle (docs/contracts/battle_rewards.md § C, § D): the wrap-up and the title
// program's laying out are where the US cartridges hold them and an image without them is refused;
// the last three places a console just switched on holds, read three ways — by hand, by the port out
// of the cartridge's own title program, and off the cartridge after it lays the program out; the list's
// hand-worked cases run on the graphics program's own commands, the port matching both; then 500 drawn
// scenarios through each of the two commands and the wrap-up, with every cell the wrap-up read written
// first; then the win, the loss and the write-back the same way, by hand and over 500 drawn battles
// each.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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
#include "ostinato/character_prop_id.h"
#include "ostinato/status_id.h"
#include "party_test_support.h"
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
constexpr std::uint32_t kWinBattle = 0xC25D57;
constexpr std::uint32_t kLoseBattle = 0xC25FCA;
constexpr std::uint32_t kUpdateSram = 0xC24936;

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
        EXPECT_EQ(bytes(kWinBattle), (std::vector<std::uint8_t>{0x08, 0x20, 0x67, 0x02}));   // php; jsr EndAction
        EXPECT_EQ(bytes(kLoseBattle), (std::vector<std::uint8_t>{0x48, 0xA9, 0x01, 0x0C}));  // pha; lda #$01; tsb
        EXPECT_EQ(bytes(kUpdateSram), (std::vector<std::uint8_t>{0xA2, 0x06, 0xBD, 0xD8}));  // ldx #$06; lda $3ed8,x
    });
}

TEST_F(BattleEndOracleTest, AnImageWithoutThemIsRefused) {
    for (const std::uint32_t routine : {kWrapUp, kDecompCutsceneProg, kWinBattle, kLoseBattle, kUpdateSram}) {
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

// --- the win, the loss and the write-back ---------------------------------------------------------

// Every cell the last call read was written first, but for the listed ones.
void expectEveryReadWritten(const DamageOracle& oracle, const std::string& what) {
    for (const trace::UnwrittenRead& read : oracle.unwrittenReads()) {
        EXPECT_TRUE(trace::leftAsTheMachineHasIt(read.at)) << what << " read " << std::hex << read.at;
    }
}

// Terra a level short with a Cat Hood, Locke fallen, Gau standing; two monsters fallen, one fled; on
// the Veldt; Strago's lores; the RNG somewhere.
EndingScenario byHand() {
    EndingScenario s;
    battle::BattleState& state = s.state;
    state = battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::GAU});
    const CharacterPropId actors[] = {CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::GAU};
    for (int position = 0; position < 3; ++position) {
        battle::Battler& member = state.at(battle::BattlerSlot::character(position));
        member.present = true;
        member.actor = actors[position];
        member.record = static_cast<std::size_t>(position);
        member.hp = 100;
        if (position != 1) {
            state.alive.add(battle::BattlerSlot::character(position));
        } else {
            member.status.set(StatusId::DEAD);
        }
    }
    state.at(battle::BattlerSlot::character(0)).relicEffects5.bits = 0x10;  // a Cat Hood
    state.allyCount = 2;
    state.party.characters[0].experience = 500;
    state.party.morphSupply = 40;
    state.switches.set(game::kMagicPointsEarnedSwitch);
    const auto fallen = [&](int position, int id, std::uint16_t experience, std::uint16_t gold, bool fell) {
        battle::Battler& monster = state.at(battle::BattlerSlot::monster(position));
        monster.present = true;
        monster.monster = static_cast<MonsterId>(id);
        monster.experience = experience;
        monster.gold = gold;
        if (fell) monster.status.set(StatusId::DEAD);
    };
    fallen(0, 7, 120, 40, true);
    fallen(2, 7, 120, 40, true);
    fallen(4, 30, 900, 900, false);
    state.loresLearned.set(3);
    state.rng = battle::BattleRng(77);
    state.startingFormation = static_cast<FormationId>(5);
    state.formation = static_cast<FormationId>(5);
    s.cells.party = state.party;
    return s;
}

TEST_F(BattleEndOracleTest, TheWinByHand) {
    const EndingScenario s = byHand();
    withOracle([&](DamageOracle& oracle) {
        EXPECT_EQ(trace::differences(trace::portWin(s), oracle.win(s)), "");
        expectEveryReadWritten(oracle, "the win");
        EndingScenario veldt = s;
        veldt.state.onTheVeldt = true;
        EXPECT_EQ(trace::differences(trace::portWin(veldt), oracle.win(veldt)), "") << "on the Veldt";
        EndingScenario colosseum = s;
        colosseum.state.colosseum = true;
        colosseum.state.colosseumPrize = ItemId::ELIXIR;
        EXPECT_EQ(trace::differences(trace::portWin(colosseum), oracle.win(colosseum)), "") << "the colosseum";
    });
}

TEST_F(BattleEndOracleTest, TheLossByHand) {
    const EndingScenario s = byHand();
    withOracle([&](DamageOracle& oracle) {
        for (const std::optional<std::uint8_t> message :
             {std::optional<std::uint8_t>{}, std::optional<std::uint8_t>{battle::kMessageAnnihilated},
              std::optional<std::uint8_t>{battle::kMessageBanonFell}}) {
            EXPECT_EQ(trace::differences(trace::portLoss(s, message), oracle.loss(s, message)), "")
                << (message ? int{*message} : -1);
            expectEveryReadWritten(oracle, "the loss");
        }
    });
}

TEST_F(BattleEndOracleTest, TheWriteBackByHand) {
    // Locke taken out of the party: the cartridge clears his whole party byte.
    EndingScenario s = byHand();
    s.state.takenFromTheParty.add(battle::BattlerSlot::character(1));
    s.cells.party.membership[1].backRow = true;
    s.state.party = s.cells.party;
    withOracle([&](DamageOracle& oracle) {
        EXPECT_EQ(trace::differences(trace::portWriteBack(s), oracle.writeBack(s)), "");
        expectEveryReadWritten(oracle, "the write-back");
    });
}

template <typename Port, typename Guest>
void sweep(DamageOracle& oracle, Port port, Guest guest) {
    std::mt19937 rng{trace::kSweepSeed};
    for (int draw = 0; draw < kDraws; ++draw) {
        const EndingScenario s = trace::drawWinScenario(rng);
        const std::string differs = trace::differences(port(s), guest(oracle, s));
        ASSERT_EQ(differs, "") << "draw " << draw << ": " << trace::describe(s);
        for (const trace::UnwrittenRead& read : oracle.unwrittenReads()) {
            ASSERT_TRUE(trace::leftAsTheMachineHasIt(read.at)) << "draw " << draw << " read " << std::hex << read.at;
        }
    }
}

TEST_F(BattleEndOracleTest, WinSweep) {
    withOracle([](DamageOracle& oracle) {
        sweep(oracle, [](const EndingScenario& s) { return trace::portWin(s); },
              [](DamageOracle& o, const EndingScenario& s) { return o.win(s); });
    });
}

TEST_F(BattleEndOracleTest, LossSweep) {
    withOracle([](DamageOracle& oracle) {
        sweep(oracle, [](const EndingScenario& s) { return trace::portLoss(s, battle::kMessageAnnihilated); },
              [](DamageOracle& o, const EndingScenario& s) { return o.loss(s, battle::kMessageAnnihilated); });
    });
}

TEST_F(BattleEndOracleTest, WriteBackSweep) {
    withOracle([](DamageOracle& oracle) {
        sweep(oracle, [](const EndingScenario& s) { return trace::portWriteBack(s); },
              [](DamageOracle& o, const EndingScenario& s) { return o.writeBack(s); });
    });
}

}  // namespace
}  // namespace ostinato::test
