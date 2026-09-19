// Full-corpus tests for the blitz input tables. All eight records and all
// fifteen masks are compared against the generated fixture (the ROM-assembled
// values), independent of the typed rows. On top of that: the traced
// sequences, the shape every sequence shares, the diagonal and NONE masks, and
// the builders.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/blitz_codes.h"
#include "data/level_up.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/blitz_button_mask.h"
#include "ostinato/blitz_code_input.h"
#include "ostinato/pad_input.h"

#include "fixtures/blitz_code_expected.h"

namespace {

using namespace ostinato;

// --- full corpus -----------------------------------------------------------------

TEST(BlitzCodes, EveryRecordMatchesRom) {
    const auto table = blitzCodes();
    ASSERT_EQ(table.size(), kBlitzCodeCount);
    ASSERT_EQ(table.size(), test::kExpectedBlitzCodes.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& expected = test::kExpectedBlitzCodes[i];
        EXPECT_EQ(expected.id, i) << "fixture row " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, &expected.record, sizeof(BlitzCode)), 0)
            << "record " << i;
    }
}

// The blitz identities agree with the independently generated learn-level
// table, which names the same eight attacks in the same order.
TEST(BlitzCodes, IdsMatchTheLearnLevelTable) {
    const auto table = blitzCodes();
    const auto learned = abilityLearnLevels(BattleCommandId::BLITZ);
    ASSERT_EQ(learned.size(), table.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(table[i].id, learned[i].ability) << "blitz " << i;
    }
}

TEST(BlitzCodes, AccessorReturnsTheRowForEachBlitz) {
    for (const BlitzCodeEntry& entry : blitzCodes()) {
        EXPECT_EQ(std::memcmp(&blitzCode(entry.id), &entry.record, sizeof(BlitzCode)), 0);
    }
}

TEST(BlitzButtonMasks, EveryMaskMatchesRom) {
    const auto table = blitzButtonMasks();
    ASSERT_EQ(table.size(), kBlitzCodeInputCount);
    ASSERT_EQ(table.size(), test::kExpectedBlitzButtonMasks.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& expected = test::kExpectedBlitzButtonMasks[i];
        EXPECT_EQ(expected.input, i) << "fixture row " << i;
        EXPECT_EQ(static_cast<std::size_t>(table[i].input), i) << "row " << i;
        EXPECT_EQ(table[i].mask.bits, expected.mask) << "mask " << i;
        EXPECT_EQ(blitzButtonMask(table[i].input), table[i].mask) << "accessor " << i;
    }
}

// --- traced sequences -------------------------------------------------------------

// Pummel (btlgfx_main.asm:17044-17049): left, right, left, confirm — four
// inputs, stored as eight, the rest NONE.
TEST(BlitzCodes, PummelIsLeftRightLeftConfirm) {
    const BlitzCode& code = blitzCode(AttackId::PUMMEL);
    EXPECT_EQ(code.doubledInputCount, 8);
    ASSERT_EQ(code.inputCount(), 4u);
    const BlitzCodeInput expected[] = {BlitzCodeInput::LEFT, BlitzCodeInput::RIGHT,
                                       BlitzCodeInput::LEFT, BlitzCodeInput::A_BUTTON};
    EXPECT_TRUE(std::equal(code.sequence().begin(), code.sequence().end(),
                           std::begin(expected), std::end(expected)));
    for (std::size_t i = 4; i < kBlitzCodeInputSlots; ++i) {
        EXPECT_EQ(code.inputs[i], BlitzCodeInput::NONE) << "slot " << i;
    }
}

// Bum Rush (:17127-17138) is the longest sequence: ten inputs, one pad slot.
TEST(BlitzCodes, BumRushIsTheLongestSequence) {
    const BlitzCode& code = blitzCode(AttackId::BUM_RUSH);
    EXPECT_EQ(code.inputCount(), 10u);
    EXPECT_EQ(code.inputs[10], BlitzCodeInput::NONE);
    for (const BlitzCodeEntry& entry : blitzCodes()) {
        EXPECT_LE(entry.record.inputCount(), code.inputCount());
    }
}

// Every sequence ends on the confirming A button, holds no NONE, and is padded
// with NONE to the end of the record.
TEST(BlitzCodes, EverySequenceEndsOnConfirmAndIsPaddedWithNone) {
    for (const BlitzCodeEntry& entry : blitzCodes()) {
        const BlitzCode& code = entry.record;
        ASSERT_GT(code.inputCount(), 0u);
        ASSERT_LE(code.inputCount(), kBlitzCodeInputSlots);
        EXPECT_EQ(code.doubledInputCount % 2, 0);
        const auto sequence = code.sequence();
        EXPECT_EQ(sequence.back(), BlitzCodeInput::A_BUTTON);
        EXPECT_EQ(std::count(sequence.begin(), sequence.end(), BlitzCodeInput::NONE), 0);
        for (std::size_t i = code.inputCount(); i < kBlitzCodeInputSlots; ++i) {
            EXPECT_EQ(code.inputs[i], BlitzCodeInput::NONE);
        }
    }
}

// --- masks ------------------------------------------------------------------------

// hardware.inc:254-266 — the controller's bits.
TEST(PadInput, BitsAreTheControllersOwn) {
    EXPECT_EQ(static_cast<std::uint16_t>(PadInput::A), 0b0000000010000000);
    EXPECT_EQ(static_cast<std::uint16_t>(PadInput::B), 0b1000000000000000);
    EXPECT_EQ(static_cast<std::uint16_t>(PadInput::DOWN), 0b0000010000000000);
    EXPECT_EQ(static_cast<std::uint16_t>(PadInput::LEFT), 0b0000001000000000);
}

// DOWN_LEFT's mask is $0600 (btlgfx_main.asm:17001): either direction alone is
// enough for a press to count.
TEST(BlitzButtonMasks, DiagonalAcceptsEitherOfItsDirections) {
    const BlitzButtonMask mask = blitzButtonMask(BlitzCodeInput::DOWN_LEFT);
    EXPECT_EQ(mask.bits, 0x0600);
    EXPECT_TRUE(mask.has(PadInput::DOWN));
    EXPECT_TRUE(mask.has(PadInput::LEFT));
    EXPECT_TRUE(mask.acceptsPress(BlitzButtonMask::of(PadInput::DOWN)));
    EXPECT_TRUE(mask.acceptsPress(BlitzButtonMask::of(PadInput::LEFT)));
    EXPECT_FALSE(mask.acceptsPress(BlitzButtonMask::of(PadInput::UP)));
    EXPECT_FALSE(mask.acceptsPress(BlitzButtonMask::of(PadInput::A)));
}

// NONE's mask is every bit (:16994). It only fills the padding, which the
// battle never compares — no sequence contains NONE.
TEST(BlitzButtonMasks, NoneAcceptsEverythingAndOnlyPads) {
    const BlitzButtonMask mask = blitzButtonMask(BlitzCodeInput::NONE);
    EXPECT_TRUE(mask.isAllButtons());
    EXPECT_EQ(mask.bits, 0xFFFF);
    EXPECT_TRUE(mask.acceptsPress(BlitzButtonMask::of(PadInput::SELECT)));
    for (const BlitzButtonMaskEntry& entry : blitzButtonMasks()) {
        if (entry.input != BlitzCodeInput::NONE) {
            EXPECT_FALSE(entry.mask.isAllButtons());
        }
    }
}

// --- builders ---------------------------------------------------------------------

TEST(BlitzCodes, BuilderPadsAndDoublesTheCount) {
    constexpr BlitzCode code = BlitzCode::of({BlitzCodeInput::UP, BlitzCodeInput::A_BUTTON});
    EXPECT_EQ(code.doubledInputCount, 4);
    EXPECT_EQ(code.inputs[0], BlitzCodeInput::UP);
    EXPECT_EQ(code.inputs[1], BlitzCodeInput::A_BUTTON);
    const std::uint8_t expected[12] = {12, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4};
    EXPECT_EQ(std::memcmp(&code, expected, sizeof(expected)), 0);
}

TEST(BlitzButtonMasks, BuilderOrsItsInputs) {
    EXPECT_EQ(BlitzButtonMask::of(PadInput::UP, PadInput::RIGHT).bits, 0x0900);
    EXPECT_EQ(BlitzButtonMask::of({PadInput::A, PadInput::X}).bits, 0x00C0);
    EXPECT_EQ(BlitzButtonMask::of(PadInput::R), BlitzButtonMask{0x0010});
}

}  // namespace
