// The dance attack table. The rows are generated
// (src/data/generated/dance_prop_data.inc); this header owns the record type,
// the entry type, the array, and the accessor.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ostinato/attack_id.h"
#include "ostinato/dance_id.h"

namespace ostinato {

// One 4-byte dance record (dance_prop.asm): the four candidate attacks in
// slot order. Slot position is the consumer's probability tier (7/16, 3/8,
// 1/8, 1/16 — the random-dance battle routine); the rate thresholds
// themselves are battle-logic data, not part of this record.
struct DanceProperties {
    std::array<AttackId, 4> attacks;
};

static_assert(sizeof(DanceProperties) == 4,
              "DanceProperties must be byte-identical to a 4-byte dance record");

// One table entry: the record's identity as a typed field (the DanceId
// enumerator — identity is a field, never a comment) alongside the packed
// record, which stays sizeof-locked to the ROM bytes. Every generated row
// reads { .id = DanceId::NAME, .record = { ... } }; a compile-time assert
// verifies id == array position for every entry.
struct DancePropertiesEntry {
    DanceId id;
    DanceProperties record;
};

// kDanceProperties — the dance attack table (DanceProp in dance_prop.asm),
// 8 entries in DANCE index order.
inline constexpr std::array<DancePropertiesEntry, 8> kDanceProperties = {{
#include "data/generated/dance_prop_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal its
// array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kDanceProperties.size(); ++i) {
        if (static_cast<std::size_t>(kDanceProperties[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kDanceProperties entry id fields must match array positions");

// The record the original reads at DanceProp + dance*4. The accessor
// debug-asserts the 0..7 bound (DanceId is uint8_t; not every byte value is
// a dance).
const DanceProperties& getDanceProperties(DanceId id);

}  // namespace ostinato
