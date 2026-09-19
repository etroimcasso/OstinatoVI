#include "data/trig_tables.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

constexpr std::array<ArcTangentEntry, kArcTangentSide * kArcTangentSide> kArcTangent = {{
#include "data/generated/arc_tangent_data.inc"
}};

constexpr std::array<Sine16Entry, kSineAngles> kSine16 = {{
#include "data/generated/sine16_data.inc"
}};

constexpr std::array<Sine8Entry, kSineAngles> kSine8 = {{
#include "data/generated/sine8_data.inc"
}};

constexpr bool cellsMatchPositions() {
    for (std::size_t i = 0; i < kArcTangent.size(); ++i) {
        if (kArcTangent[i].y * kArcTangentSide + kArcTangent[i].x != i) {
            return false;
        }
    }
    return true;
}

constexpr bool anglesMatchPositions(const auto& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].angle != i) {
            return false;
        }
    }
    return true;
}

static_assert(cellsMatchPositions(), "kArcTangent y/x fields must match array positions");
static_assert(anglesMatchPositions(kSine16), "kSine16 angle fields must match array positions");
static_assert(anglesMatchPositions(kSine8), "kSine8 angle fields must match array positions");

}  // namespace

std::uint8_t arcTangent(std::uint8_t y, std::uint8_t x) {
    assert(y < kArcTangentSide && x < kArcTangentSide && "arctangent cell out of range");
    return kArcTangent[y * kArcTangentSide + x].angle;
}

std::int16_t sine16(std::uint8_t angle) { return kSine16[angle].value; }

std::int8_t sine8(std::uint8_t angle) { return kSine8[angle].value; }

std::span<const ArcTangentEntry> arcTangentTable() { return kArcTangent; }
std::span<const Sine16Entry> sine16Table() { return kSine16; }
std::span<const Sine8Entry> sine8Table() { return kSine8; }

}  // namespace ostinato
