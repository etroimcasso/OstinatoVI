// An animation-reference word: a 16-bit value that names an animation (one of
// the AttackAnimScript / AttackAnimFrames entries) with bit 15 as a live flag.
// AttackAnimProp stores four of these per row — sprite, bg1, and bg3 animation
// plus the special-graphics word. InitAnimProp (btlgfx_main.asm:23579-23652)
// loads each word and treats $ffff as "no animation" (its cpx #$ffff / "branch
// if unused" checks). The low 15 bits are the animation index (<= 594 after
// & $7fff); bit 15 is set on a minority of rows and its runtime meaning is the
// battle animation player's.
//
// Stored as an explicit little-endian byte pair rather than a std::uint16_t so
// the type has alignment 1: AttackAnimationProperties places the special-
// graphics AnimationRef at the odd ROM offset +11, which a 2-aligned member
// could not occupy without padding the packed 14-byte record.
#pragma once

#include <array>
#include <cstdint>

namespace ostinato {

struct AnimationRef {
    std::array<std::uint8_t, 2> bytes = {0, 0};

    // The raw ROM word (little-endian byte pair).
    constexpr std::uint16_t raw() const {
        return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
    }
    // $ffff: no animation (InitAnimProp's cpx #$ffff / "branch if unused").
    constexpr bool isNone() const { return raw() == 0xFFFF; }
    // The animation index (low 15 bits).
    constexpr std::uint16_t index() const {
        return static_cast<std::uint16_t>(raw() & 0x7FFF);
    }
    // Bit 15: a live flag set on a minority of rows; the runtime meaning is
    // the battle animation player's.
    constexpr bool hasHighBit() const { return (raw() & 0x8000) != 0; }

    // Build from a raw little-endian word.
    static constexpr AnimationRef fromRaw(std::uint16_t word) {
        return AnimationRef{{static_cast<std::uint8_t>(word & 0xFF),
                             static_cast<std::uint8_t>((word >> 8) & 0xFF)}};
    }
    // Builders so every construction site names a decimal index and, where the
    // flag is set, says so — never a raw hex word. Byte-identical to the ROM.
    static constexpr AnimationRef of(std::uint16_t index) {
        return fromRaw(index);
    }
    static constexpr AnimationRef withHighBit(std::uint16_t index) {
        return fromRaw(static_cast<std::uint16_t>(index | 0x8000));
    }
    // The $ffff sentinel, so a "no animation" row is a named value, not a word.
    static const AnimationRef NONE;
};

inline constexpr AnimationRef AnimationRef::NONE = AnimationRef::fromRaw(0xFFFF);

static_assert(sizeof(AnimationRef) == 2,
              "AnimationRef must be byte-identical to a ROM animation word");
static_assert(alignof(AnimationRef) == 1,
              "AnimationRef must be alignment-1 to sit at any packed-record "
              "offset (AttackAnimProp's special graphics is at odd offset +11)");
static_assert(
    AnimationRef::of(0x0192).raw() == 0x0192 &&
        AnimationRef::of(0x0192).index() == 0x0192 &&
        !AnimationRef::of(0x0192).hasHighBit() &&
        AnimationRef::withHighBit(0x0012).raw() == 0x8012 &&
        AnimationRef::withHighBit(0x0012).index() == 0x0012 &&
        AnimationRef::withHighBit(0x0012).hasHighBit() &&
        AnimationRef::NONE.isNone(),
    "AnimationRef builders must round-trip the index and the high bit");

}  // namespace ostinato
