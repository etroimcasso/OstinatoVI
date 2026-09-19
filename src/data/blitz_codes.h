// Blitz input sequences: the eight BlitzCode records and the mask each input
// accepts. The rows are generated (src/data/generated/blitz_code_data.inc,
// blitz_button_mask_data.inc); this header owns the record type and the
// accessors.
//
// A blitz is performed by entering its sequence and then confirming. Each
// record is twelve bytes (btlgfx_main.asm:17012-17015, ROM C4/7A40): up to
// eleven inputs, padded with NONE, then the number of inputs doubled. The
// battle reads that last byte as the extent of the sequence; the menu's
// blitz list draws the first ten inputs.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>

#include "ostinato/attack_id.h"
#include "ostinato/blitz_button_mask.h"
#include "ostinato/blitz_code_input.h"

namespace ostinato {

// How many blitzes there are, and how many inputs a record can hold.
inline constexpr std::size_t kBlitzCodeCount = 8;
inline constexpr std::size_t kBlitzCodeInputSlots = 11;

// How many BlitzCodeInput values there are (NONE included) — one mask each.
inline constexpr std::size_t kBlitzCodeInputCount = 15;

// One blitz's twelve ROM bytes.
struct BlitzCode {
    // The sequence, then NONE to the end of the record.
    std::array<BlitzCodeInput, kBlitzCodeInputSlots> inputs{};
    // The number of inputs in the sequence, times two.
    std::uint8_t doubledInputCount = 0;

    // The record for this sequence: padded with NONE, count stored doubled.
    // A sequence longer than the record holds does not compile.
    static constexpr BlitzCode of(std::initializer_list<BlitzCodeInput> sequence) {
        if (sequence.size() > kBlitzCodeInputSlots) {
            throw std::length_error("a blitz sequence holds at most eleven inputs");
        }
        BlitzCode code;
        std::size_t i = 0;
        for (BlitzCodeInput input : sequence) {
            code.inputs[i++] = input;
        }
        code.doubledInputCount = static_cast<std::uint8_t>(sequence.size() * 2);
        return code;
    }

    constexpr std::size_t inputCount() const { return doubledInputCount / 2u; }

    // The inputs to enter, in order, ending with the confirming A_BUTTON.
    constexpr std::span<const BlitzCodeInput> sequence() const {
        return {inputs.data(), inputCount()};
    }
};

static_assert(sizeof(BlitzCode) == 12,
              "BlitzCode must stay byte-identical to a ROM blitz record");

// One table entry: the blitz's identity as a typed field (its AttackId) and
// its record.
struct BlitzCodeEntry {
    AttackId id;
    BlitzCode record;
};

// The mask one input accepts.
struct BlitzButtonMaskEntry {
    BlitzCodeInput input;
    BlitzButtonMask mask;
};

// The sequence for one blitz. PRECONDITION (asserted): `blitz` is one of the
// eight blitz attacks, PUMMEL through BUM_RUSH.
const BlitzCode& blitzCode(AttackId blitz);

// All eight blitzes in learning order.
std::span<const BlitzCodeEntry> blitzCodes();

// The mask an input accepts. PRECONDITION (asserted): `input` is a
// BlitzCodeInput member.
BlitzButtonMask blitzButtonMask(BlitzCodeInput input);

// All fifteen masks in BlitzCodeInput order.
std::span<const BlitzButtonMaskEntry> blitzButtonMasks();

}  // namespace ostinato
