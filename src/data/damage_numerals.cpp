#include "data/damage_numerals.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace ostinato {

namespace {

// Where the battle copies the numerals from once the status art is unpacked at $7F:A400: "Miss" at
// $7F:BC00 and digit d at $7F:BC40 + 32·d (btlgfx GfxCmd_0b :24711, _c1a735 :24989-24991).
constexpr std::size_t kMissOffset = 0x1800;
constexpr std::size_t kFirstDigitOffset = 0x1840;

}  // namespace

std::span<const std::uint8_t> DamageNumeralArt::digit(int digit) const {
    assert(digit >= 0 && static_cast<std::size_t>(digit) < kNumeralDigits && "a numeral digit is 0 to 9");
    return digits[static_cast<std::size_t>(digit)];
}

DamageNumeralArt damageNumeralArt(std::span<const std::uint8_t> statusArt) {
    if (statusArt.size() != kStatusArtBytes) {
        throw std::invalid_argument("The status art does not decode to its 8,192 bytes.");
    }
    DamageNumeralArt art;
    for (std::size_t tile = 0; tile < kMissTiles; ++tile) {
        const std::span<const std::uint8_t> bytes = statusArt.subspan(kMissOffset + tile * kNumeralTileBytes, kNumeralTileBytes);
        std::copy(bytes.begin(), bytes.end(), art.miss[tile].begin());
    }
    for (std::size_t digit = 0; digit < kNumeralDigits; ++digit) {
        const std::span<const std::uint8_t> bytes =
            statusArt.subspan(kFirstDigitOffset + digit * kNumeralTileBytes, kNumeralTileBytes);
        std::copy(bytes.begin(), bytes.end(), art.digits[digit].begin());
    }
    return art;
}

}  // namespace ostinato
