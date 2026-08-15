// The kind of palette animation a slot runs. UpdatePalAnim (anim.asm:74-118)
// reads the slot's control byte, masks the type field, and dispatches to one of
// four handlers — the upstream comments name the paths "counter only", "cycle",
// "rom", and "pulse".
#pragma once

#include <cstdint>

namespace ostinato {

enum class PaletteAnimationType : std::uint8_t {
    COUNTER = 0,     // counter only — advances the frame counter, no color write
    CYCLE = 1,       // cycles the slot's colors
    ROM_COLORS = 2,  // loads colors from the MapPalAnimColors rom table
    PULSE = 3,       // pulses (brightens/dims) the slot's colors
};

}  // namespace ostinato
