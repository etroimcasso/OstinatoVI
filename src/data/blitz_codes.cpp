#include "data/blitz_codes.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The eight blitzes in learning order.
constexpr std::array<BlitzCodeEntry, kBlitzCodeCount> kBlitzCodes = {{
#include "data/generated/blitz_code_data.inc"
}};

// One mask per BlitzCodeInput, in value order.
constexpr std::array<BlitzButtonMaskEntry, kBlitzCodeInputCount> kBlitzButtonMasks = {{
#include "data/generated/blitz_button_mask_data.inc"
}};

constexpr bool inputMatchesPosition() {
    for (std::size_t i = 0; i < kBlitzButtonMasks.size(); ++i) {
        if (static_cast<std::size_t>(kBlitzButtonMasks[i].input) != i) {
            return false;
        }
    }
    return true;
}

static_assert(inputMatchesPosition(),
              "kBlitzButtonMasks input fields must match array positions");

}  // namespace

const BlitzCode& blitzCode(AttackId blitz) {
    for (const BlitzCodeEntry& entry : kBlitzCodes) {
        if (entry.id == blitz) {
            return entry.record;
        }
    }
    assert(false && "attack is not a blitz");
    return kBlitzCodes.front().record;
}

std::span<const BlitzCodeEntry> blitzCodes() { return kBlitzCodes; }

BlitzButtonMask blitzButtonMask(BlitzCodeInput input) {
    const auto index = static_cast<std::size_t>(input);
    assert(index < kBlitzButtonMasks.size() && "not a blitz input");
    return kBlitzButtonMasks[index].mask;
}

std::span<const BlitzButtonMaskEntry> blitzButtonMasks() { return kBlitzButtonMasks; }

}  // namespace ostinato
