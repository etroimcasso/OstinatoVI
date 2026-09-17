// Full-corpus tests for the battle graphics trig tables. Every entry of all
// three tables is compared against the generated fixture (the raw cartridge
// values), and each table is re-checked against the formula upstream documents
// beside it, with the number of entries that sit off it pinned.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include <gtest/gtest.h>

#include "data/trig_tables.h"

#include "fixtures/trig_tables_expected.h"

namespace {

using namespace ostinato;

long roundedArcTangent(std::size_t y, std::size_t x) {
    if (x == 0) {
        return 64;
    }
    return std::lround(std::atan2(static_cast<double>(y), static_cast<double>(x)) * 128.0
                       / std::numbers::pi);
}

long roundedSine(std::size_t angle, double amplitude) {
    return std::lround(std::sin(2.0 * std::numbers::pi * static_cast<double>(angle) / 256.0)
                       * amplitude);
}

// --- full corpus -----------------------------------------------------------------

TEST(TrigTables, EveryArcTangentCellMatchesRom) {
    const auto table = arcTangentTable();
    ASSERT_EQ(table.size(), kArcTangentSide * kArcTangentSide);
    ASSERT_EQ(table.size(), test::kExpectedArcTangent.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(test::kExpectedArcTangent[i].index, i);
        EXPECT_EQ(table[i].y, i / kArcTangentSide) << "cell " << i;
        EXPECT_EQ(table[i].x, i % kArcTangentSide) << "cell " << i;
        EXPECT_EQ(table[i].angle, test::kExpectedArcTangent[i].value) << "cell " << i;
        EXPECT_EQ(arcTangent(table[i].y, table[i].x), table[i].angle) << "cell " << i;
    }
}

TEST(TrigTables, EverySine16EntryMatchesRom) {
    const auto table = sine16Table();
    ASSERT_EQ(table.size(), kSineAngles);
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(test::kExpectedSine16[i].index, i);
        EXPECT_EQ(table[i].angle, i);
        EXPECT_EQ(static_cast<std::uint16_t>(table[i].value), test::kExpectedSine16[i].value)
            << "angle " << i;
        EXPECT_EQ(sine16(static_cast<std::uint8_t>(i)), table[i].value) << "angle " << i;
    }
}

TEST(TrigTables, EverySine8EntryMatchesRom) {
    const auto table = sine8Table();
    ASSERT_EQ(table.size(), kSineAngles);
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(test::kExpectedSine8[i].index, i);
        EXPECT_EQ(table[i].angle, i);
        EXPECT_EQ(static_cast<std::uint8_t>(table[i].value), test::kExpectedSine8[i].value)
            << "angle " << i;
        EXPECT_EQ(sine8(static_cast<std::uint8_t>(i)), table[i].value) << "angle " << i;
    }
}

// --- the documented formulas -----------------------------------------------------

// btlgfx_main.asm:42588-42590: "up to rounding errors" — every cell is the
// rounded arctangent or one below it, and 323 are the lower value.
TEST(TrigTables, ArcTangentIsTheRoundedAngleOrOneBelow) {
    int below = 0;
    for (const ArcTangentEntry& cell : arcTangentTable()) {
        const long diff = static_cast<long>(cell.angle) - roundedArcTangent(cell.y, cell.x);
        EXPECT_TRUE(diff == 0 || diff == -1) << "cell (" << +cell.y << ", " << +cell.x << ")";
        below += diff == -1;
    }
    EXPECT_EQ(below, 323);
}

// :48735-48742: the 16-bit table is "almost" round(sin * 32767) — 13 entries
// are one below and 18 one above.
TEST(TrigTables, Sine16IsWithinOneOfTheFormula) {
    int below = 0;
    int above = 0;
    for (const Sine16Entry& entry : sine16Table()) {
        const long diff = static_cast<long>(entry.value) - roundedSine(entry.angle, 32767.0);
        EXPECT_LE(std::labs(diff), 1) << "angle " << +entry.angle;
        below += diff == -1;
        above += diff == 1;
    }
    EXPECT_EQ(below, 13);
    EXPECT_EQ(above, 18);
}

// :48780-48783: the 8-bit table is round(sin * 127) exactly.
TEST(TrigTables, Sine8IsTheFormulaExactly) {
    for (const Sine8Entry& entry : sine8Table()) {
        EXPECT_EQ(entry.value, roundedSine(entry.angle, 127.0)) << "angle " << +entry.angle;
    }
}

// --- traced values ---------------------------------------------------------------

// ArcTanTbl rows 0 and 1 (:42592-42595).
TEST(TrigTables, ArcTangentCornersAreTraced) {
    EXPECT_EQ(arcTangent(0, 0), 64);   // $40 — the x = 0 column is a quarter turn
    EXPECT_EQ(arcTangent(0, 5), 0);    // along the x axis
    EXPECT_EQ(arcTangent(1, 1), 32);   // $20 — the diagonal is an eighth of a turn
    EXPECT_EQ(arcTangent(1, 2), 18);   // $12 — round would give 19
    EXPECT_EQ(arcTangent(31, 31), 32);
}

// SineTbl16 (:48745, :48753, :48757, :48769).
TEST(TrigTables, Sine16ExtremesAreTraced) {
    EXPECT_EQ(sine16(0), 0);
    EXPECT_EQ(sine16(64), 32767);      // $7FFF
    EXPECT_EQ(sine16(99), 21402);      // $539A — the formula gives $539B
    EXPECT_EQ(sine16(192), -32767);    // $8001
}

TEST(TrigTables, Sine8IsAntisymmetric) {
    EXPECT_EQ(sine8(64), 127);
    EXPECT_EQ(sine8(192), -127);
    for (int angle = 1; angle < 256; ++angle) {
        EXPECT_EQ(sine8(static_cast<std::uint8_t>(angle)),
                  -sine8(static_cast<std::uint8_t>(256 - angle)))
            << "angle " << angle;
    }
}

}  // namespace
