#include "data/colosseum_wagers.h"

#include <array>
#include <cstddef>

namespace ostinato {

namespace {

// The 256-entry table. The generated rows carry the ROM record data;
// designated initializers at every row keep each field self-labeling, and
// each entry carries its identity as the wagered item's ItemId enumerator.
constexpr std::array<ColosseumWagerEntry, 256> kColosseumWagers = {{
#include "data/generated/colosseum_prop_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal
// its array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kColosseumWagers.size(); ++i) {
        if (static_cast<std::size_t>(kColosseumWagers[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kColosseumWagers entry id fields must match array positions");

}  // namespace

const ColosseumWager& getColosseumWager(ItemId wagered) {
    // ItemId is uint8_t, so every value indexes the 256-entry table by
    // construction.
    return kColosseumWagers[static_cast<std::size_t>(wagered)].record;
}

std::span<const ColosseumWagerEntry> colosseumWagers() {
    return kColosseumWagers;
}

}  // namespace ostinato
