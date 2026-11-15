// The last three places of the battle's items-obtained list a console just switched on holds
// (docs/contracts/battle_rewards.md § C), read by hand, by the port out of the cartridge's own title
// program, and off the cartridge after it lays the program out where the run asks it
// (tests/with_cartridge.h). The battle's end on the oracle is in tests/oracle/test_battle_end_oracle.cpp.

#include <gtest/gtest.h>

#include <cstddef>

#include "assets/cartridge.h"
#include "assets/cartridge_routines.h"
#include "battle/items_obtained.h"
#include "battle_end_oracle_cases.h"
#include "oracle/damage_oracle.h"
#include "ostinato/item_id.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

// The last three places the cartridge lays out at power-on are the port's, where the run asks it.
void expectTheCartridgesTail(const Cartridge& cartridge, const battle::ItemsObtainedTail& port) {
    if (!cartridge) {
        return;
    }
    const battle::ItemsObtainedTail guest = cartridge.oracle().itemsObtainedTailAtPowerOn();
    for (std::size_t place = 0; place < guest.size(); ++place) {
        EXPECT_EQ(battle::recordOf(guest[place]), battle::recordOf(port[place])) << place;
    }
}

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
    withCartridge(image(), [&](const Cartridge& cartridge) { expectTheCartridgesTail(cartridge, port); });
}

}  // namespace
}  // namespace ostinato::test
