// Per-map event data: the tile-triggered events placed on each field map, and
// the startup event that runs when each map loads.
//
//   * EventTrigger    — a tile position that fires an event script when the
//                       party steps on it (event/event_trigger.asm). A map's
//                       triggers are located through the per-map offset table in
//                       src/data/event_triggers.h. Consumers: field
//                       CheckEventTriggers (field/event.asm:5730) and world
//                       CheckEvent (world/move.asm:1309), which compare the
//                       party's tile against posX/posY.
//   * MapInitEventEntry — one map's startup-event reference (event/
//                       map_init_event.asm), read by field/init.asm:485 when a
//                       map loads. kEventReturnScript means "no startup event".
//
// The event references are opaque EventScriptRef offsets; the script bodies are a
// later phase. EventTrigger is sizeof-locked to its 5 ROM bytes; MapInitEventEntry
// is a wrapper-entry that carries a port-side map-id identity alongside the ROM
// reference.
#pragma once

#include <cstddef>
#include <cstdint>

#include "ostinato/event_script_ref.h"

namespace ostinato {

// The "no startup event" reference (the EventReturn label): a script whose first
// opcode is $fe/return, which field/init.asm:509 detects and skips. Used by
// map-init entries (and, later, defaulted NPC events). Its offset is resolved
// from the ROM, so the value is generated rather than hand-typed.
inline constexpr EventScriptRef kEventReturnScript =
#include "data/generated/event_return_script.inc"
    ;

// One 5-byte event trigger. posX/posY place the trigger tile on the map; event is
// the script that runs when the party steps on it. The field consumer compares
// the x/y pair against the party position as a single 16-bit word
// (field/event.asm:5776); the surface keeps them as the two named position bytes.
struct EventTrigger {
    std::uint8_t posX;     // +0
    std::uint8_t posY;     // +1
    EventScriptRef event;  // +2..+4
};

static_assert(sizeof(EventTrigger) == 5,
              "EventTrigger must be byte-identical to a 5-byte ROM record");
static_assert(alignof(EventTrigger) == 1,
              "EventTrigger must be alignment-1 to stay packed in the array");
static_assert(offsetof(EventTrigger, posX) == 0);
static_assert(offsetof(EventTrigger, posY) == 1);
static_assert(offsetof(EventTrigger, event) == 2);

// One map's startup event. The map id is a port-side identity field (a
// placeholder decimal id — real map names welcome at post-port tidy-up); record
// is the 3-byte ROM reference (kEventReturnScript when the map has no startup
// event). A compile-time assert in the .cpp verifies index == array position.
struct MapInitEventEntry {
    std::uint16_t index;    // map id (0-511)
    EventScriptRef record;  // startup-event reference
};

}  // namespace ostinato
