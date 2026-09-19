// Trig lookup tables for battle graphics: an arctangent grid and two sine
// tables. The rows are generated (src/data/generated/arc_tangent_data.inc,
// sine16_data.inc, sine8_data.inc); this header owns the entry types and the
// accessors.
//
// Angles are in 256ths of a turn, so a quarter turn is 64 and an angle held in
// a std::uint8_t wraps the way a full turn does.
//
// The values are the game's own, not a fresh computation: each table follows a
// formula closely but not exactly (btlgfx_main.asm:42588-42590, :48733-48783).
//   - arcTangent(y, x) is arctan(y / x) rounded to the nearest 256th, or one
//     below it — 323 of the 1,024 entries are the lower value. Column x = 0 is
//     a quarter turn, including (0, 0).
//   - sine16(angle) is sin * 32767 rounded, with 31 entries one away from that.
//   - sine8(angle) is sin * 127 rounded, exactly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ostinato {

// The arctangent grid is 32 by 32.
inline constexpr std::size_t kArcTangentSide = 32;
inline constexpr std::size_t kSineAngles = 256;

// One grid cell: the angle of the direction (x, y), both 0..31.
struct ArcTangentEntry {
    std::uint8_t y;
    std::uint8_t x;
    std::uint8_t angle;
};

struct Sine16Entry {
    std::uint8_t angle;
    std::int16_t value;
};

struct Sine8Entry {
    std::uint8_t angle;
    std::int8_t value;
};

// The angle of (x, y) in 256ths of a turn, 0..64. The battle scales a distance
// of 0..255 down to 0..31 before looking it up, and works out the quadrant from
// the signs itself. PRECONDITION (asserted): x and y are below 32.
std::uint8_t arcTangent(std::uint8_t y, std::uint8_t x);

// sin(angle) scaled to ±32767.
std::int16_t sine16(std::uint8_t angle);

// sin(angle) scaled to ±127.
std::int8_t sine8(std::uint8_t angle);

// The whole tables, for iteration and full-corpus tests. The arctangent grid is
// row y then column x.
std::span<const ArcTangentEntry> arcTangentTable();
std::span<const Sine16Entry> sine16Table();
std::span<const Sine8Entry> sine8Table();

}  // namespace ostinato
