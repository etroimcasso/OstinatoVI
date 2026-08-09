// A formation-reference word: a 16-bit value that names a formation plus one
// flag bit. The conditional-battle table and the field-encounter group tables
// store formations this way rather than as a bare FormationId, because bit 15
// carries the "randomize" flag LoadBattleProp reads
// (battle_main.asm:7956-7965): when set, the game adds a random 0..3 to the
// formation index before loading it. Bits 0-14 are the FormationId.
#pragma once

#include <cstdint>

#include "ostinato/formation_id.h"

namespace ostinato {

struct FormationRef {
    std::uint16_t raw = 0;

    // The referenced formation (low 15 bits).
    constexpr FormationId formationId() const {
        return static_cast<FormationId>(raw & 0x7FFF);
    }

    // Bit 15: add a random 0..3 to the formation index at load time.
    constexpr bool randomizePlus3() const { return (raw & 0x8000) != 0; }

    // Builder from the named formation, so every construction site labels the
    // formation instead of writing a raw index:
    // FormationRef::of(FormationId::SRBEHEMOTH). Byte-identical to the ROM word.
    static constexpr FormationRef of(FormationId id,
                                     bool randomizePlus3 = false) {
        return FormationRef{static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(id) |
            (randomizePlus3 ? 0x8000u : 0u))};
    }
};

static_assert(sizeof(FormationRef) == 2,
              "FormationRef must be byte-identical to a ROM formation word");
static_assert(
    FormationRef::of(FormationId{}).raw == 0 &&
        FormationRef::of(FormationId{}, true).raw == 0x8000 &&
        FormationRef{0x8000u | 42u}.formationId() == FormationId{42} &&
        FormationRef{0x8000u | 42u}.randomizePlus3() &&
        !FormationRef{42u}.randomizePlus3(),
    "FormationRef::of must round-trip the formation index and randomize flag");

}  // namespace ostinato
