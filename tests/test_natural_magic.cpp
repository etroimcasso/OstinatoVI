// Full-corpus test of the natural-magic tables (PLAN phase-1.B D6 +
// Amendment B1). The byte-equivalence test asserts EVERY one of the 2x16
// packed pairs is byte-identical to the ROM (no subset) and that every row's
// slot field matches its position; the semantic tests pin the spell-first
// pair order and the ROM's own out-of-sorted-order Celes entry, preserved
// verbatim. There is deliberately no character-dispatch accessor to test —
// half selection is consumer logic.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/natural_magic.h"

#include "ostinato/attack_id.h"

#include "fixtures/natural_magic_expected.h"

namespace {

void expectHalfMatchesRom(
    const std::array<ostinato::NaturalMagicSlot, 16>& table,
    const std::array<ostinato::test::ExpectedNaturalMagicSlot, 16>& expected,
    const char* half) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(expected[i].slot, i) << half << " fixture slot " << i;
        EXPECT_EQ(table[i].slot, i) << half << " table slot " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, &expected[i].record, 2), 0)
            << half << " slot " << i;
    }
}

// Full corpus, both halves: slot identities match positions and one memcmp
// per packed pair catches order and symbol-resolution drift byte-for-byte.
TEST(NaturalMagic, AllPairsAreByteIdenticalToRom) {
    expectHalfMatchesRom(ostinato::kNaturalMagicTerra,
                         ostinato::test::kExpectedNaturalMagicTerra, "terra");
    expectHalfMatchesRom(ostinato::kNaturalMagicCeles,
                         ostinato::test::kExpectedNaturalMagicCeles, "celes");
}

// Boundary rows of each half, hand-traced from event.asm: the pair order is
// spell first, level second.
TEST(NaturalMagic, BoundaryRows) {
    using ostinato::AttackId;

    EXPECT_EQ(ostinato::kNaturalMagicTerra[0].record.spell, AttackId::CURE);
    EXPECT_EQ(ostinato::kNaturalMagicTerra[0].record.level, 1u);
    EXPECT_EQ(ostinato::kNaturalMagicTerra[15].record.spell, AttackId::ULTIMA);
    EXPECT_EQ(ostinato::kNaturalMagicTerra[15].record.level, 99u);

    EXPECT_EQ(ostinato::kNaturalMagicCeles[0].record.spell, AttackId::ICE);
    EXPECT_EQ(ostinato::kNaturalMagicCeles[0].record.level, 1u);
    EXPECT_EQ(ostinato::kNaturalMagicCeles[15].record.spell, AttackId::METEOR);
    EXPECT_EQ(ostinato::kNaturalMagicCeles[15].record.level, 98u);
}

// The ROM's own quirk, preserved verbatim: Celes's list holds MUDDLE at
// level 32 AFTER BSERK at level 40 — out of sorted order. A port that
// re-sorted the list would pass a set-equality test but break the ROM's
// slot-order contract; this test pins the order.
TEST(NaturalMagic, CelesOutOfOrderMuddleIsPreserved) {
    using ostinato::AttackId;

    EXPECT_EQ(ostinato::kNaturalMagicCeles[8].record.spell, AttackId::BSERK);
    EXPECT_EQ(ostinato::kNaturalMagicCeles[8].record.level, 40u);
    EXPECT_EQ(ostinato::kNaturalMagicCeles[9].record.spell, AttackId::MUDDLE);
    EXPECT_EQ(ostinato::kNaturalMagicCeles[9].record.level, 32u);
}

}  // namespace
