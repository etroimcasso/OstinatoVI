// The esper properties table. The rows are generated
// (src/data/generated/genju_prop_data.inc); this header owns the record
// types, the entry type, the array, and the accessor.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ostinato/attack_id.h"
#include "ostinato/esper_bonus.h"
#include "ostinato/esper_id.h"

namespace ostinato {

// One learn-spell slot of an esper record. ROM byte order within the pair is
// rate first, spell second (the opposite of the natural-magic pairs — do not
// conflate). An empty slot is { .learnRate = 0, .spell = AttackId::NONE }.
struct EsperSpell {
    std::uint8_t learnRate;
    AttackId spell;
};

static_assert(sizeof(EsperSpell) == 2,
              "EsperSpell must be byte-identical to a ROM {rate, spell} pair");
static_assert(offsetof(EsperSpell, learnRate) == 0);
static_assert(offsetof(EsperSpell, spell) == 1);

// One 11-byte esper record (genju_prop.asm): five learn-spell pairs then the
// level-up bonus byte. A record with no bonus stores EsperBonus::NONE.
struct EsperProperties {
    std::array<EsperSpell, 5> spells;
    EsperBonus bonus;
};

static_assert(sizeof(EsperProperties) == 11,
              "EsperProperties must be byte-identical to an 11-byte esper record");
static_assert(offsetof(EsperProperties, spells) == 0);
static_assert(offsetof(EsperProperties, bonus) == 10);

// One table entry: the record's identity as a typed field (the EsperId
// enumerator — identity is a field, never a comment) alongside the packed
// record, which stays sizeof-locked to the ROM bytes. EsperId values occupy
// $36..$50 (the esper block of the unified actor space), so the position law
// is id == position + EsperId::RAMUH — checked at compile time below.
struct EsperPropertiesEntry {
    EsperId id;
    EsperProperties record;
};

// kEsperProperties — the esper properties table (GenjuProp, ROM D8/6E00),
// 27 entries in GENJU index order ($36..$50).
inline constexpr std::array<EsperPropertiesEntry, 27> kEsperProperties = {{
#include "data/generated/genju_prop_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal its
// array position offset by the first esper id, checked at compile time.
static_assert([] {
    constexpr auto base = static_cast<std::size_t>(EsperId::RAMUH);
    for (std::size_t i = 0; i < kEsperProperties.size(); ++i) {
        if (static_cast<std::size_t>(kEsperProperties[i].id) != base + i) {
            return false;
        }
    }
    return true;
}(), "kEsperProperties entry id fields must match array positions + EsperId::RAMUH");

// The record the original reads at GenjuProp + (esper index)*11. The accessor
// takes the EsperId ($36..$50) and debug-asserts the bound.
const EsperProperties& getEsperProperties(EsperId id);

}  // namespace ostinato
