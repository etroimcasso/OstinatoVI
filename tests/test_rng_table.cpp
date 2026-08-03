// Full-corpus test of the RNG table (PLAN phase-1.A D8). Asserts all 256
// entries match the ROM (no subset) — index and value — and that the byte
// accessor reads by table position.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "data/rng_table.h"

#include "fixtures/rng_tbl_expected.h"

namespace {

TEST(RngTable, AllEntriesMatchRom) {
    ASSERT_EQ(ostinato::kRngTable.size(), ostinato::test::kRngTblExpected.size());
    for (std::size_t i = 0; i < ostinato::kRngTable.size(); ++i) {
        EXPECT_EQ(ostinato::kRngTable[i].index,
                  ostinato::test::kRngTblExpected[i].index)
            << "rng index " << i;
        EXPECT_EQ(ostinato::kRngTable[i].value,
                  ostinato::test::kRngTblExpected[i].value)
            << "rng index " << i;
    }
}

TEST(RngTable, ByteAccessorReadsByTablePosition) {
    EXPECT_EQ(ostinato::rngByte(0x00), ostinato::test::kRngTblExpected[0x00].value);
    EXPECT_EQ(ostinato::rngByte(0x80), ostinato::test::kRngTblExpected[0x80].value);
    EXPECT_EQ(ostinato::rngByte(0xFF), ostinato::test::kRngTblExpected[0xFF].value);
}

}  // namespace
