// Hand-written port-design (PLAN phase-1.A D6). Not parser-emitted.
//
// The packed 22nd byte of a char_prop record — bucket 2 (multi-component). Three
// disjoint fields share one byte:
//
//   * run factor      bits 0-1  (CHAR_RUN_FACTOR, mask 0x03)
//   * level modifier  bits 2-3  (CHAR_LEVEL_MOD,  mask 0x0c)
//   * fixed-equip flag  bit 4   (CHAR_PROP_FIXED_EQUIP, 0x10)
//
// The CHAR_RUN_FACTOR / CHAR_LEVEL_MOD enumerators already carry their values in
// place (RunFactor 0..3; LevelMod 0/4/8/12), so packing is a plain OR and each
// accessor masks its field back out. sizeof == 1 keeps this byte-identical to the
// single trait byte the ROM stores as the last byte of each 22-byte record.
#pragma once

#include <cstdint>

#include "ostinato/level_mod.h"
#include "ostinato/run_factor.h"

namespace ostinato {

struct CharacterTraits {
    std::uint8_t packed = 0;

    static constexpr std::uint8_t kRunFactorMask = 0x03;
    static constexpr std::uint8_t kLevelModMask = 0x0c;
    static constexpr std::uint8_t kFixedEquipBit = 0x10;

    constexpr CharacterTraits() = default;

    // Byte-in constructor: preserves the exact ROM trait byte verbatim.
    constexpr explicit CharacterTraits(std::uint8_t raw) : packed(raw) {}

    // Component constructor: how the generated table rows read. Packs to exactly
    // the byte the assembler would emit (run_factor | level_mod | fixed_equip).
    constexpr CharacterTraits(RunFactor runFactor, LevelMod levelMod, bool fixedEquip)
        : packed(static_cast<std::uint8_t>(
              static_cast<std::uint8_t>(runFactor)
              | static_cast<std::uint8_t>(levelMod)
              | (fixedEquip ? kFixedEquipBit : 0))) {}

    constexpr RunFactor runFactor() const {
        return static_cast<RunFactor>(packed & kRunFactorMask);
    }
    constexpr LevelMod levelMod() const {
        return static_cast<LevelMod>(packed & kLevelModMask);
    }
    constexpr bool fixedEquip() const { return (packed & kFixedEquipBit) != 0; }
};

static_assert(sizeof(CharacterTraits) == 1,
              "CharacterTraits must be byte-identical to the packed char_prop trait byte");

}  // namespace ostinato
