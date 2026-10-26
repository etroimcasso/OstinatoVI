// The 32 status effects packed into four bytes. StatusId is the sequential
// 0..31 order (status_id.h); this wrapper maps id -> (byte id/8, bit id%8).
// That mapping is the contract — the enum generator asserts the upstream
// STATUS1..STATUS4 bit layouts align with StatusId order and hard-errors
// otherwise — so this type is the sole source of the packing rule, and the
// upstream's combined 16-bit status views (STATUS12/23/34/14) are expressible
// as accessors here rather than separate enums. sizeof == 4 keeps it
// byte-identical to the ROM's four status bytes.
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
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

    // Whether any of `other`'s statuses are set here, or all of them. An empty `other` has none
    // set and all set: hasAny is false and hasAll is true.
    constexpr bool hasAny(const StatusSet& other) const {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if ((bytes[i] & other.bytes[i]) != 0) {
                return true;
            }
        }
        return false;
    }

    // This set with every status in `other` cleared.
    constexpr StatusSet without(const StatusSet& other) const {
        StatusSet result = *this;
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            result.bytes[i] = static_cast<std::uint8_t>(bytes[i] & ~other.bytes[i]);
        }
        return result;
    }

    constexpr bool hasAll(const StatusSet& other) const {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if ((bytes[i] & other.bytes[i]) != other.bytes[i]) {
                return false;
            }
        }
        return true;
    }

    // OR-together builder: StatusSet::of(StatusId::SLEEP,
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
