// The metamorph tables: 32 four-item packs (metamorph_prop.dat, ROM C4/7F40)
// and the eight probability bytes (MetamorphRateTbl, battle_main.asm:
// 10008-10009, ROM c2/3dc5). The metamorph effect (TargetEffect_12,
// battle_main.asm:9385-9409) keys both off a monster's packed MetamorphInfo
// byte: the pack index selects a row here (the effect then picks one of the
// row's four items at random), and the rate selects the threshold a random
// byte must compare below for the effect to land. The rows are generated
// (src/data/generated/metamorph_prop_data.inc, metamorph_rate_data.inc);
// this header owns the entry types, the arrays, and the accessors.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ostinato/item_id.h"
#include "ostinato/metamorph_info.h"

namespace ostinato {

// One 4-item metamorph pack; sizeof == 4 keeps it byte-identical to a ROM
// pack row.
struct MetamorphPack {
    std::array<ItemId, 4> items;
};

static_assert(sizeof(MetamorphPack) == 4,
              "MetamorphPack must be byte-identical to a ROM metamorph pack");

// One pack-table entry: its position and the pack stored there. No upstream
// index enum exists for packs, so identity is the decimal index — a typed
// field, never a comment; every generated row reads { .index = N,
// .record = { ... } }.
struct MetamorphPackEntry {
    std::uint8_t index;
    MetamorphPack record;
};

// One rate-table entry: its identity as the MetamorphRate enumerator (the
// documented odds ladder — metamorph_info.h) and the threshold a random byte
// is compared against, a magnitude on a 0-255 scale.
struct MetamorphRateEntry {
    MetamorphRate id;
    std::uint8_t value;
};

// kMetamorphPacks — the pack table (32 packs in pack-index order).
inline constexpr std::array<MetamorphPackEntry, 32> kMetamorphPacks = {{
#include "data/generated/metamorph_prop_data.inc"
}};

// kMetamorphRates — the probability table (8 thresholds in rate order).
inline constexpr std::array<MetamorphRateEntry, 8> kMetamorphRates = {{
#include "data/generated/metamorph_rate_data.inc"
}};

// Self-consistency of the emitted rows: every entry's identity field must
// equal its array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kMetamorphPacks.size(); ++i) {
        if (kMetamorphPacks[i].index != i) {
            return false;
        }
    }
    return true;
}(), "kMetamorphPacks entry index fields must match array positions");
static_assert([] {
    for (std::size_t i = 0; i < kMetamorphRates.size(); ++i) {
        if (static_cast<std::size_t>(kMetamorphRates[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kMetamorphRates entry id fields must match array positions");

// The item pack a monster's metamorph byte selects. packIndex() is 5 bits,
// so every value indexes the 32-entry table by construction.
const MetamorphPack& getMetamorphPack(MetamorphInfo info);

// The probability threshold a monster's metamorph byte selects: the effect
// lands when a random byte compares below it. rate() is 3 bits, so every
// value indexes the 8-entry table by construction.
std::uint8_t metamorphRate(MetamorphInfo info);

}  // namespace ostinato
