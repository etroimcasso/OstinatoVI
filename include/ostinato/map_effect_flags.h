// The map's effect-flag byte (MapProperties +1, copied to $0521). A packed
// carrier of the per-map screen/menu effect toggles. Its bits are read by
// several field consumers:
//   * bits 0-1 gate the field menu's X-Zone and Warp commands. The main menu
//     splits them individually: field_menu.asm:2404-2413 tests bit 0 for x-zone
//     ("branch if x-zone is disabled") and bit 1 for warp ("branch if warp is
//     disabled"). menu.asm:232-235 masks both (and #$03) into the menu-flag byte.
//   * bit 2 selects the bg3 wavy-alt HDMA effect (hdma.asm), bit 3 the wavy bg2,
//     bit 4 the wavy bg1, bit 5 the spotlights effect (screen.asm:20-22).
//   * bit 7 requests the timer-graphics load (obj.asm:3105-3107, tested via bmi).
//   * bit 6 has no consumer in the field code; it is preserved raw.
// sizeof == 1 keeps it byte-identical to the ROM byte.
#pragma once

#include <cstdint>

namespace ostinato {

struct MapEffectFlags {
    std::uint8_t bits = 0;

    constexpr MapEffectFlags() = default;
    explicit constexpr MapEffectFlags(std::uint8_t raw) : bits(raw) {}

    // Bit 0: the field menu's X-Zone command is enabled on this map.
    constexpr bool xZoneEnabled() const { return (bits & 0x01) != 0; }
    // Bit 1: the field menu's Warp command is enabled on this map.
    constexpr bool warpEnabled() const { return (bits & 0x02) != 0; }
    // Bit 2: the bg3 wavy-alt HDMA effect is active.
    constexpr bool bg3WavyAlt() const { return (bits & 0x04) != 0; }
    // Bit 3: the bg2 wavy HDMA effect is active.
    constexpr bool bg2Wavy() const { return (bits & 0x08) != 0; }
    // Bit 4: the bg1 wavy HDMA effect is active.
    constexpr bool bg1Wavy() const { return (bits & 0x10) != 0; }
    // Bit 5: the spotlights HDMA effect is active.
    constexpr bool spotlights() const { return (bits & 0x20) != 0; }
    // Bit 7: load the timer (countdown) graphics on this map.
    constexpr bool timerGfx() const { return (bits & 0x80) != 0; }
};

static_assert(sizeof(MapEffectFlags) == 1,
              "MapEffectFlags must be byte-identical to the ROM byte");

}  // namespace ostinato
