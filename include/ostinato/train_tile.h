#pragma once

#include <cstdint>

namespace ostinato {

// One tile of a Magitek train tunnel: where it sits in the arrangement and
// which graphics tile fills it.
//
// The four bytes are stored exactly as the cartridge holds them, so an
// arrangement round-trips through this type unchanged. The graphics tile index
// spans two of them and comes back through tileIndex().
struct TrainTile {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t tileIndexLow = 0;
    std::uint8_t tileIndexHigh = 0;

    // The graphics tile this slot draws — the two stored bytes read as one
    // value, which is the read the tunnel builder makes
    // (world/train_script.asm:178).
    [[nodiscard]] constexpr std::uint16_t tileIndex() const {
        return static_cast<std::uint16_t>(
            tileIndexLow | (static_cast<std::uint16_t>(tileIndexHigh) << 8));
    }

    // True when the slot draws nothing. The tunnel builder tests the leading
    // two bytes and writes an empty tile instead of placing this one
    // (world/train_script.asm:176-185).
    [[nodiscard]] constexpr bool unused() const { return x == 0 && y == 0; }
};

static_assert(sizeof(TrainTile) == 4);
static_assert(alignof(TrainTile) == 1);

}  // namespace ostinato
