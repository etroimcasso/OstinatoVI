// A reference into the event-script block: the 24-bit little-endian offset of an
// event script from the block base (EventScript, ca/0000). Field triggers, world
// triggers, map-init pointers, and NPC records all store one of these; the
// consumer re-adds the base to reach the script (field/event.asm:5799
// `adc #^EventScript`, world/move.asm:1331, field/init.asm:504).
//
// This type is deliberately opaque: it names *where* a script lives, not what it
// does. Decoding the script bodies is a later phase — nothing here interprets the
// offset. Valid offsets fall in [0, 0x2e600), the size of the event-script block.
//
// Stored as an explicit 3-byte little-endian array (matching the ROM `.faraddr`)
// with alignment 1, so it sits inside packed records at any byte offset without
// forcing padding.
#pragma once

#include <array>
#include <cassert>
#include <cstdint>

namespace ostinato {

// The size of the event-script block (EventScript .. EventScript + $2e600).
inline constexpr std::uint32_t kEventScriptBlockSize = 0x2E600;

struct EventScriptRef {
    std::array<std::uint8_t, 3> bytes = {0, 0, 0};

    // The 24-bit little-endian offset into the event-script block.
    constexpr std::uint32_t offset() const {
        return static_cast<std::uint32_t>(bytes[0])
               | (static_cast<std::uint32_t>(bytes[1]) << 8)
               | (static_cast<std::uint32_t>(bytes[2]) << 16);
    }

    // Build from a raw offset, so every construction site names a hex offset
    // rather than three loose bytes. Byte-identical to the ROM `.faraddr`.
    static constexpr EventScriptRef at(std::uint32_t offset) {
        assert(offset < kEventScriptBlockSize
               && "event-script offset out of range");
        return EventScriptRef{{static_cast<std::uint8_t>(offset & 0xFF),
                               static_cast<std::uint8_t>((offset >> 8) & 0xFF),
                               static_cast<std::uint8_t>((offset >> 16) & 0xFF)}};
    }
};

static_assert(sizeof(EventScriptRef) == 3,
              "EventScriptRef must be byte-identical to a 3-byte ROM faraddr");
static_assert(alignof(EventScriptRef) == 1,
              "EventScriptRef must be alignment-1 to sit at any packed-record "
              "byte offset");
static_assert(EventScriptRef::at(0x010BB7).offset() == 0x010BB7
                  && EventScriptRef::at(0x010BB7).bytes[0] == 0xB7
                  && EventScriptRef::at(0x010BB7).bytes[1] == 0x0B
                  && EventScriptRef::at(0x010BB7).bytes[2] == 0x01,
              "EventScriptRef::at must round-trip the little-endian offset");

}  // namespace ostinato
