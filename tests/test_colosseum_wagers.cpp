// Full-corpus test of the colosseum-wager table. The byte-equivalence test
// asserts EVERY one of the 256 packed records is byte-identical to the ROM's
// 4-byte record (no subset), that every entry's identity field matches its
// position, and that every record carries the dead $40 byte; the semantic
// tests exercise the lookup and the record accessors.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/colosseum_wagers.h"

#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

#include "fixtures/colosseum_prop_expected.h"

namespace {

// Full corpus: identity fields on both sides match the position, one memcmp
// per packed record catches field-order, padding, and resolution drift in a
// single whole-record comparison against the ROM, and the dead +1 byte is
// $40 on every record.
TEST(ColosseumWagers, AllRecordsAreByteIdenticalToRom) {
    const auto table = ostinato::colosseumWagers();
    ASSERT_EQ(table.size(), ostinato::test::kExpectedColosseumEntries.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& expected = ostinato::test::kExpectedColosseumEntries[i];
        EXPECT_EQ(expected.id, i) << "fixture entry " << i;
        EXPECT_EQ(static_cast<std::size_t>(table[i].id), i)
            << "table entry " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, &expected.record, 4), 0)
            << "wagered item index " << i;
        EXPECT_EQ(table[i].record.unused40, ostinato::kColosseumUnusedByte)
            << "wagered item index " << i;
    }
}

// The lookup indexes by the wagered item. Spot-check records hand-traced
// from the ROM table: an unwagerable item's default row (Dirk — Chupon,
// Elixir, shown), a plain wager (Thiefknife — Wart Puck for a Thief Glove),
// and two hide-prize rows (Ragnarok — Didalos for the Illumina; Striker —
// Chupon for another Striker).
TEST(ColosseumWagers, LookupSemanticSurface) {
    using ostinato::ItemId;
    using ostinato::MonsterId;

    const auto& dirk = ostinato::getColosseumWager(ItemId::DIRK);
    EXPECT_EQ(dirk.monsterId(), MonsterId::CHUPON_COLOSSEUM);
    EXPECT_EQ(dirk.prize, ItemId::ELIXIR);
    EXPECT_FALSE(dirk.prizeHidden());

    const auto& thiefknife = ostinato::getColosseumWager(ItemId::THIEFKNIFE);
    EXPECT_EQ(thiefknife.monsterId(), MonsterId::WART_PUCK);
    EXPECT_EQ(thiefknife.prize, ItemId::THIEF_GLOVE);
    EXPECT_FALSE(thiefknife.prizeHidden());

    const auto& ragnarok = ostinato::getColosseumWager(ItemId::RAGNAROK);
    EXPECT_EQ(ragnarok.monsterId(), MonsterId::DIDALOS);
    EXPECT_EQ(ragnarok.prize, ItemId::ILLUMINA);
    EXPECT_TRUE(ragnarok.prizeHidden());
    EXPECT_EQ(ragnarok.hidePrizeFlag, ostinato::kHidePrize);

    const auto& striker = ostinato::getColosseumWager(ItemId::STRIKER);
    EXPECT_EQ(striker.monsterId(), MonsterId::CHUPON_COLOSSEUM);
    EXPECT_EQ(striker.prize, ItemId::STRIKER);
    EXPECT_TRUE(striker.prizeHidden());
}

// wagerMonster narrows a MonsterId to the record's stored byte and
// monsterId() recovers it — the round-trip the generated rows depend on.
TEST(ColosseumWagers, WagerMonsterRoundTrip) {
    using ostinato::MonsterId;
    using ostinato::wagerMonster;

    EXPECT_EQ(wagerMonster(MonsterId::CHUPON_COLOSSEUM), 0x40u);
    constexpr ostinato::ColosseumWager wager{
        .monster = ostinato::wagerMonster(MonsterId::DIDALOS),
        .unused40 = ostinato::kColosseumUnusedByte,
        .prize = ostinato::ItemId::ILLUMINA,
        .hidePrizeFlag = ostinato::kHidePrize,
    };
    EXPECT_EQ(wager.monsterId(), MonsterId::DIDALOS);
    EXPECT_TRUE(wager.prizeHidden());
}

}  // namespace
