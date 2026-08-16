// Field-map entrance triggers: the tile positions that transfer the party to
// another map. Two flavours share the same destination word:
//   * LongEntrance  — triggers anywhere along a run of tiles (entrance.asm:63-165)
//   * ShortEntrance — triggers on a single tile (entrance.asm:283-373)
// A map's entrances are located through the per-map offset tables in
// src/data/map_triggers.h. Both record types are sizeof-locked to the ROM.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ostinato/event_dir.h"

namespace ostinato {

// The destination-map sentinel meaning "return to the stored parent map"
// (entrance.asm:123 / :303 test the 9-bit map id == $01ff).
inline constexpr std::uint16_t kParentMapSentinel = 0x01FF;

// The entrance destination word (LongEntrance +3..+4 / ShortEntrance +2..+3):
// the 9-bit destination map id plus transfer flags packed into the high byte.
// The consumer reads the low 9 bits as the map id and the high byte's bits 1-5
// as flags (entrance.asm:117-158 / :297-334). Stored as an alignment-1 byte pair
// so the enclosing record stays sizeof-locked.
struct EntranceDestination {
    std::array<std::uint8_t, 2> bytes = {0, 0};

    // The two bytes as a 16-bit little-endian value.
    constexpr std::uint16_t raw() const {
        return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
    }

    // Bits 0-8: destination map id (and #$01ff). kParentMapSentinel returns to
    // the parent map; ids 0-2 are world maps (DestPos read as an xy word).
    constexpr std::uint16_t destMap() const { return raw() & 0x01FF; }
    constexpr bool isParentReturn() const {
        return destMap() == kParentMapSentinel;
    }
    // Bit 9: store the current map as parent before transferring
    // (and #$0200 -> SetParentMap, entrance.asm:117-120 / :297-300).
    constexpr bool setsParentMap() const { return (raw() & 0x0200) != 0; }
    // Bit 10 (high byte bit 2): destination z-level is lower (Flags & #$04 ->
    // $0744, entrance.asm:154-158 / :330-334).
    constexpr bool zLevelLower() const { return (raw() & 0x0400) != 0; }
    // Bit 11 (high byte bit 3): show the map-name window (Flags & #$08 -> $0745,
    // entrance.asm:149-151 / :325-327).
    constexpr bool showMapName() const { return (raw() & 0x0800) != 0; }
    // Bits 12-13 (high byte bits 4-5): party facing after transfer. The consumer
    // shifts these to a 0-3 value and stores it to $0743 as the facing direction
    // ((Flags & #$30) >> 4, entrance.asm:145-148 / :321-324) — an EventDir value.
    constexpr EventDir facing() const {
        return static_cast<EventDir>((raw() >> 12) & 0x03);
    }
};

static_assert(sizeof(EntranceDestination) == 2,
              "EntranceDestination must be byte-identical to the 2-byte ROM word");
static_assert(alignof(EntranceDestination) == 1,
              "EntranceDestination must be alignment-1 to sit inside the packed "
              "entrance records");

// The long-entrance run byte (LongEntrance +2): the trigger spans a run of tiles
// along a row or column (entrance.asm:65-102).
struct EntranceRun {
    std::uint8_t bits = 0;

    // Bit 7: the run is vertical (bmi -> Vertical); otherwise horizontal.
    constexpr bool isVertical() const { return (bits & 0x80) != 0; }
    // Bits 0-6: run length in tiles (and #$7f).
    constexpr std::uint8_t length() const { return bits & 0x7F; }
};

static_assert(sizeof(EntranceRun) == 1,
              "EntranceRun must be byte-identical to the ROM byte");

// One 7-byte long-entrance record. The trigger fires when the party crosses any
// tile in the run from src, transferring to (destMap, destX, destY).
struct LongEntrance {
    std::uint8_t srcX;                // +0
    std::uint8_t srcY;                // +1
    EntranceRun run;                  // +2
    EntranceDestination destination;  // +3..+4
    std::uint8_t destX;               // +5
    std::uint8_t destY;               // +6
};

static_assert(sizeof(LongEntrance) == 7,
              "LongEntrance must be byte-identical to a 7-byte ROM record");
static_assert(alignof(LongEntrance) == 1,
              "LongEntrance must be alignment-1 to stay packed in the array");
static_assert(offsetof(LongEntrance, srcX) == 0);
static_assert(offsetof(LongEntrance, srcY) == 1);
static_assert(offsetof(LongEntrance, run) == 2);
static_assert(offsetof(LongEntrance, destination) == 3);
static_assert(offsetof(LongEntrance, destX) == 5);
static_assert(offsetof(LongEntrance, destY) == 6);

// One 6-byte short-entrance record. The trigger fires on the single tile at src,
// transferring to (destMap, destX, destY). No run byte.
struct ShortEntrance {
    std::uint8_t srcX;                // +0
    std::uint8_t srcY;                // +1
    EntranceDestination destination;  // +2..+3
    std::uint8_t destX;               // +4
    std::uint8_t destY;               // +5
};

static_assert(sizeof(ShortEntrance) == 6,
              "ShortEntrance must be byte-identical to a 6-byte ROM record");
static_assert(alignof(ShortEntrance) == 1,
              "ShortEntrance must be alignment-1 to stay packed in the array");
static_assert(offsetof(ShortEntrance, srcX) == 0);
static_assert(offsetof(ShortEntrance, srcY) == 1);
static_assert(offsetof(ShortEntrance, destination) == 2);
static_assert(offsetof(ShortEntrance, destX) == 4);
static_assert(offsetof(ShortEntrance, destY) == 5);

}  // namespace ostinato
