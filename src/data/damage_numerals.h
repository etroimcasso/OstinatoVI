// The damage numerals' artwork, as the player's cartridge holds it: the ten digits and the word
// "Miss" the numerals over a battler are made of.
//
// They are part of the status art, a compressed stream the battle unpacks with the cartridge's own
// decompressor when it starts. Each digit is one four-plane 8 x 8 tile, "Miss" is two, and their
// pixels use only colors 12 and 13 — the first two of the four status colors a party member's
// palette ends in (statusColors, data/battle_gfx_tables.h). A white numeral draws through party
// slot 0's colors, a green one through slot 1's.
//
//     const std::vector<std::uint8_t> status = routines.decompress(assets::compressedStream(content.statusArt));
//     const DamageNumeralArt art = damageNumeralArt(status);
//     drawTile(sheet, art.digit(7), assets::TileDepth::FourPlanes, x, y);
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ostinato {

// The status art as it decodes: 256 four-plane tiles.
inline constexpr std::size_t kStatusArtBytes = 8192;

inline constexpr std::size_t kNumeralTileBytes = 32;
inline constexpr std::size_t kNumeralDigits = 10;
inline constexpr std::size_t kMissTiles = 2;

// The palette colors a numeral's pixels use.
inline constexpr std::uint8_t kNumeralOutlineColor = 12;
inline constexpr std::uint8_t kNumeralFillColor = 13;

// The damage numerals' tiles, copied out of the decoded status art.
struct DamageNumeralArt {
    std::array<std::array<std::uint8_t, kNumeralTileBytes>, kNumeralDigits> digits{};
    std::array<std::array<std::uint8_t, kNumeralTileBytes>, kMissTiles> miss{};

    // The tile of `digit`. PRECONDITION (asserted): digit is below 10.
    std::span<const std::uint8_t> digit(int digit) const;
};

// The numerals' tiles out of `statusArt`, the status art as it decodes.
// Throws std::invalid_argument when `statusArt` is not kStatusArtBytes long.
[[nodiscard]] DamageNumeralArt damageNumeralArt(std::span<const std::uint8_t> statusArt);

}  // namespace ostinato
