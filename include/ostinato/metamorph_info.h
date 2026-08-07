// The packed metamorph byte (+17) of a monster-properties record ($3C94,
// battle-ram.txt:962-964, "pppiiiii"): the low 5 bits select the item pack in
// the metamorph pack table, the high 3 bits select the probability row. The
// metamorph effect decodes it exactly this way (TargetEffect_12,
// battle_main.asm:9385-9409). sizeof == 1 keeps it byte-identical to the ROM
// byte.
#pragma once

#include <cstdint>

namespace ostinato {

// The eight metamorph probability rows, in rate-table order. The names carry
// the documented odds ladder (battle-ram.txt:963): the effect succeeds when a
// random byte compares below the row's threshold in the metamorph rate table
// (src/data/metamorph.h), so row 0 succeeds 255 times in 256 and NEVER's zero
// threshold cannot pass.
enum class MetamorphRate : std::uint8_t {
    ODDS_255_256 = 0,
    ODDS_3_4     = 1,
    ODDS_1_2     = 2,
    ODDS_1_4     = 3,
    ODDS_1_8     = 4,
    ODDS_1_16    = 5,
    ODDS_1_32    = 6,
    NEVER        = 7,
};

struct MetamorphInfo {
    std::uint8_t packed = 0;

    // The item-pack index (low 5 bits) — selects a 4-item row in the
    // metamorph pack table (src/data/metamorph.h). Always 0..31.
    constexpr std::uint8_t packIndex() const { return packed & 0x1F; }

    // The probability row (high 3 bits).
    constexpr MetamorphRate rate() const {
        return static_cast<MetamorphRate>(packed >> 5);
    }

    // The decoded fields, named so every construction site labels both
    // components: MetamorphInfo::of({ .packIndex = 3,
    // .rate = MetamorphRate::ODDS_1_4 }).
    struct Fields {
        std::uint8_t packIndex;
        MetamorphRate rate;
    };

    // Builder from the decoded fields; the pack index is masked to its 5-bit
    // field so every built value round-trips to a valid ROM byte.
    static constexpr MetamorphInfo of(Fields fields) {
        return MetamorphInfo{static_cast<std::uint8_t>(
            (static_cast<std::uint8_t>(fields.rate) << 5) |
            (fields.packIndex & 0x1F))};
    }
};

static_assert(sizeof(MetamorphInfo) == 1,
              "MetamorphInfo must be byte-identical to the ROM metamorph byte");
static_assert(
    MetamorphInfo::of({.packIndex = 12, .rate = MetamorphRate::ODDS_1_4})
            .packed == 0x6C &&
    MetamorphInfo{0x6C}.packIndex() == 12 &&
    MetamorphInfo{0x6C}.rate() == MetamorphRate::ODDS_1_4 &&
    MetamorphInfo::of({.packIndex = 31, .rate = MetamorphRate::NEVER})
            .packed == 0xFF &&
    MetamorphInfo{}.packIndex() == 0 &&
    MetamorphInfo{}.rate() == MetamorphRate::ODDS_255_256,
    "MetamorphInfo must round-trip the effect's pack/rate decode");

}  // namespace ostinato
