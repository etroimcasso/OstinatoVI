// The thrown / weapon-animation byte: field +5 of a weapon (or monster)
// attack-animation record. The throw-command setup (btlgfx_main.asm:28467-28478)
// reads bit 7 to decide whether the weapon was thrown ("branch if weapon was not
// thrown", :28477) and the low 7 bits as a weapon-animation type, special-casing
// type 1 as the "star or gambler type" (:28469). The corpus uses low values 0-4;
// only type 1 is named upstream, so the rest keep UNKNOWN_n names. sizeof == 1
// keeps the wrapper byte-identical to the ROM byte.
#pragma once

#include <cstdint>

namespace ostinato {

// The low-7-bit weapon-animation type. Only value 1 is named upstream
// (btlgfx_main.asm:28469, "star or gambler type"); the corpus uses 0-4.
enum class WeaponAnimationType : std::uint8_t {
    UNKNOWN_0       = 0,
    STAR_OR_GAMBLER = 1,
    UNKNOWN_2       = 2,
    UNKNOWN_3       = 3,
    UNKNOWN_4       = 4,
};

struct ThrownAnimationFlags {
    std::uint8_t bits = 0;

    // Bit 7: the weapon was thrown (btlgfx_main.asm:28477).
    constexpr bool isThrown() const { return (bits & 0x80) != 0; }
    // The low-7-bit weapon-animation type.
    constexpr WeaponAnimationType animationType() const {
        return static_cast<WeaponAnimationType>(bits & 0x7F);
    }

    static constexpr std::uint8_t pack(bool thrown, WeaponAnimationType type) {
        return static_cast<std::uint8_t>(
            (thrown ? 0x80 : 0) | (static_cast<std::uint8_t>(type) & 0x7F));
    }
    // Builders so every row names its weapon-animation type and, when thrown,
    // says so — never a raw byte. Byte-identical to the ROM byte.
    static constexpr ThrownAnimationFlags of(WeaponAnimationType type) {
        return ThrownAnimationFlags{pack(false, type)};
    }
    static constexpr ThrownAnimationFlags thrown(WeaponAnimationType type) {
        return ThrownAnimationFlags{pack(true, type)};
    }
};

static_assert(sizeof(ThrownAnimationFlags) == 1,
              "ThrownAnimationFlags must be byte-identical to the ROM byte");
static_assert(
    ThrownAnimationFlags::of(WeaponAnimationType::STAR_OR_GAMBLER).bits == 0x01 &&
        !ThrownAnimationFlags::of(WeaponAnimationType::STAR_OR_GAMBLER)
             .isThrown() &&
        ThrownAnimationFlags::of(WeaponAnimationType::STAR_OR_GAMBLER)
                .animationType() == WeaponAnimationType::STAR_OR_GAMBLER &&
        ThrownAnimationFlags::thrown(WeaponAnimationType::UNKNOWN_2).bits ==
            0x82 &&
        ThrownAnimationFlags::thrown(WeaponAnimationType::UNKNOWN_2).isThrown() &&
        ThrownAnimationFlags::thrown(WeaponAnimationType::UNKNOWN_2)
                .animationType() == WeaponAnimationType::UNKNOWN_2,
    "ThrownAnimationFlags builders must round-trip the thrown flag and type");

}  // namespace ostinato
