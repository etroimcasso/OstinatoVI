// The three blocked-status bytes (+20..+22) of a monster-properties record.
// The record blocks statuses from status bytes 1-3 only — no fourth immunity
// byte exists; the loader substitutes a constant for byte 4
// (battle_main.asm:7515-7517) — so this type stores exactly three bytes and
// only statuses homed there (StatusId 0..23) are legal arguments. Same
// id -> (byte id/8, bit id%8) packing rule as StatusSet (status_set.h);
// sizeof == 3 keeps it byte-identical to the ROM's three blocked-status
// bytes.
#pragma once

#include <array>
#include <cassert>
#include <concepts>
#include <cstdint>

#include "ostinato/status_id.h"

namespace ostinato {

struct BlockedStatusSet {
    std::array<std::uint8_t, 3> bytes{};

    // The number of statuses the record can block: status bytes 1-3 hold
    // StatusId 0..23. Ids beyond this have no home byte here — the debug
    // assert catches a runtime violation, and a constant-evaluated violation
    // fails to compile on the out-of-range array access.
    static constexpr std::uint8_t kStatusCount = 24;

    constexpr bool has(StatusId id) const {
        const auto i = static_cast<std::uint8_t>(id);
        assert(i < kStatusCount && "status has no blocked-status byte (only "
                                   "status bytes 1-3 exist in the record)");
        return (bytes[i / 8] & static_cast<std::uint8_t>(1u << (i % 8))) != 0;
    }

    constexpr void set(StatusId id) {
        const auto i = static_cast<std::uint8_t>(id);
        assert(i < kStatusCount && "status has no blocked-status byte (only "
                                   "status bytes 1-3 exist in the record)");
        bytes[i / 8] |= static_cast<std::uint8_t>(1u << (i % 8));
    }

    constexpr void clear(StatusId id) {
        const auto i = static_cast<std::uint8_t>(id);
        assert(i < kStatusCount && "status has no blocked-status byte (only "
                                   "status bytes 1-3 exist in the record)");
        bytes[i / 8] &= static_cast<std::uint8_t>(~(1u << (i % 8)));
    }

    // OR-together builder: BlockedStatusSet::of(StatusId::BLIND,
    // StatusId::SLEEP). Zero arguments yields the empty set (all three
    // blocked-status bytes zero).
    static constexpr BlockedStatusSet of(std::same_as<StatusId> auto... ids) {
        BlockedStatusSet result{};
        (result.set(ids), ...);
        return result;
    }
};

static_assert(sizeof(BlockedStatusSet) == 3,
              "BlockedStatusSet must be byte-identical to the ROM's three "
              "blocked-status bytes");

}  // namespace ostinato
