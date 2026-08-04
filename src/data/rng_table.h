// The RNG table. The rows are generated
// (src/data/generated/rng_tbl_data.inc); this header owns the entry type, the
// array, and the accessor.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ostinato {

// One entry of the RNG table: its position and the ROM byte stored there.
// Identity is a field, never a comment — every generated row reads
// { .index = N, .value = 0xNN } (decimal identity, hex ROM byte), so no
// value is positionally opaque.
struct RngTableEntry {
    std::uint8_t index;
    std::uint8_t value;
};

// kRngTable — the RNG table (rng_tbl.dat, ROM c0/fd00), 256 entries in index
// order. Battle/field consumers read it via rngByte() in later phases.
inline constexpr std::array<RngTableEntry, 256> kRngTable = {{
#include "data/generated/rng_tbl_data.inc"
}};

// Self-consistency of the emitted rows: every entry's index field must equal
// its array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kRngTable.size(); ++i) {
        if (kRngTable[i].index != i) {
            return false;
        }
    }
    return true;
}(), "kRngTable entry index fields must match array positions");

// The byte the original reads at RNGTbl+index. index is uint8_t, so every
// argument value is in range by construction.
std::uint8_t rngByte(std::uint8_t index);

}  // namespace ostinato
