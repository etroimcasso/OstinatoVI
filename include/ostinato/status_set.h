// Hand-written port-design (PLAN phase-1.A D5). Not parser-emitted.
//
// The 32 status effects packed into four bytes — bucket 2 (multi-component).
// StatusId is the sequential 0..31 order (status_id.h); this wrapper maps id ->
// (byte id/8, bit id%8). That mapping is the contract: the enum parser
// structurally asserts STATUS1..STATUS4's bit layouts align with StatusId order
// and hard-errors otherwise, so this is the sole source of the packing rule and
// the combined 16-bit status views (STATUS12/23/34/14) become accessors here
// rather than separate enums. sizeof == 4 keeps it byte-identical to the ROM's
// four status bytes.
#pragma once

#include <array>
#include <concepts>
#include <cstdint>

#include "ostinato/status_id.h"

namespace ostinato {

struct StatusSet {
    std::array<std::uint8_t, 4> bytes{};

    constexpr bool has(StatusId id) const {
        const auto i = static_cast<std::uint8_t>(id);
        return (bytes[i / 8] & static_cast<std::uint8_t>(1u << (i % 8))) != 0;
    }

    constexpr void set(StatusId id) {
        const auto i = static_cast<std::uint8_t>(id);
        bytes[i / 8] |= static_cast<std::uint8_t>(1u << (i % 8));
    }

    constexpr void clear(StatusId id) {
        const auto i = static_cast<std::uint8_t>(id);
        bytes[i / 8] &= static_cast<std::uint8_t>(~(1u << (i % 8)));
    }

    // OR-together builder (added in phase 1.B): StatusSet::of(StatusId::SLEEP,
    // StatusId::STOP). Zero arguments yields the empty set (all four status
    // bytes zero).
    static constexpr StatusSet of(std::same_as<StatusId> auto... ids) {
        StatusSet result{};
        (result.set(ids), ...);
        return result;
    }
};

static_assert(sizeof(StatusSet) == 4, "StatusSet must be byte-identical to the ROM's four status bytes");

}  // namespace ostinato
