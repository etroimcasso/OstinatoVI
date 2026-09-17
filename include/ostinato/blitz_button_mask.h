// The controller inputs a blitz input accepts (btlgfx_main.asm:16993). A set of
// PadInput bits in one 16-bit word; sizeof == 2 keeps it identical to the ROM
// word.
//
// A press counts toward a blitz input when it shares at least one bit with the
// input's mask, so a diagonal's mask holds both of its directions.
#pragma once

#include <cstdint>
#include <initializer_list>

#include "ostinato/pad_input.h"

namespace ostinato {

struct BlitzButtonMask {
    std::uint16_t bits = 0;

    constexpr BlitzButtonMask() = default;
    explicit constexpr BlitzButtonMask(std::uint16_t raw) : bits(raw) {}

    // The mask holding exactly these inputs.
    static constexpr BlitzButtonMask of(std::initializer_list<PadInput> inputs) {
        std::uint16_t raw = 0;
        for (PadInput input : inputs) {
            raw = static_cast<std::uint16_t>(raw | static_cast<std::uint16_t>(input));
        }
        return BlitzButtonMask{raw};
    }
    template <typename... More>
    static constexpr BlitzButtonMask of(PadInput first, More... more) {
        return of({first, more...});
    }

    // Every bit set, including the four no input uses. BlitzCodeInput::NONE
    // carries this mask.
    static constexpr BlitzButtonMask allButtons() { return BlitzButtonMask{0xFFFF}; }

    constexpr bool isAllButtons() const { return bits == 0xFFFF; }

    // Whether this mask includes the input.
    constexpr bool has(PadInput input) const {
        return (bits & static_cast<std::uint16_t>(input)) != 0;
    }

    // Whether a press — a mask of the inputs held — counts toward this input.
    constexpr bool acceptsPress(BlitzButtonMask press) const {
        return (bits & press.bits) != 0;
    }

    friend constexpr bool operator==(BlitzButtonMask, BlitzButtonMask) = default;
};

static_assert(sizeof(BlitzButtonMask) == 2,
              "BlitzButtonMask must be identical to the ROM word");

}  // namespace ostinato
