// Hand-written port-design (PLAN phase-1.B D9). Not parser-emitted.
//
// The targeting byte of an attack-properties record (record byte +0, $11A0) —
// a one-byte carrier over the TargetFlags bit values, deliberately WITHOUT
// semantic read accessors: the byte embeds a 2-bit INIT_* sub-field plus the
// $FF MENU sentinel, and the read-side wrapper is deferred to the battle
// targeting consumer (Phase 3.B forward-pointer in the checklist) where that
// sub-field's semantics have their consumer context. sizeof == 1 keeps it
// byte-identical to the ROM targeting byte.
#pragma once

#include <concepts>
#include <cstdint>

#include "ostinato/target_flags.h"

namespace ostinato {

struct Targeting {
    std::uint8_t bits = 0;

    constexpr Targeting() = default;
    explicit constexpr Targeting(std::uint8_t raw) : bits(raw) {}

    // OR-together builder over TargetFlags. INIT_HALF == INIT_ALL|INIT_GROUP
    // and MENU == $FF are plain values here — composition semantics stay with
    // the deferred read-side wrapper.
    static constexpr Targeting of(std::same_as<TargetFlags> auto... flags) {
        Targeting result{};
        ((result.bits |= static_cast<std::uint8_t>(flags)), ...);
        return result;
    }
};

static_assert(sizeof(Targeting) == 1,
              "Targeting must be byte-identical to the ROM targeting byte");

}  // namespace ostinato
