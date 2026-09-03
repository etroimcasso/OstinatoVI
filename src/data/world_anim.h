// World-map animation frames: the sprite arrangements the overworld draws for
// vehicles, chocobos, characters and wildlife, and the small tables that step
// through them.
//
// The arrangements are cartridge content. They are read in place out of the
// player's ROM image (RomAsset::WORLD_ANIM_FRAMES) and decoded through the views
// below — nothing is copied and nothing is compiled in. The frame-sequence
// tables hold frame numbers rather than arrangements, so they are generated
// (src/data/generated/world_anim_*_frames_data.inc) and this header owns their
// entry type and accessors.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/world_anim_frame_id.h"
#include "ostinato/world_anim_sprite.h"

namespace ostinato {

// Bytes per pointer-table entry: one 16-bit offset per frame.
inline constexpr std::size_t kWorldAnimPointerBytes = 2;

// Frames per altitude row of the smoking-airship table, and the step at which
// its second cartridge label sits.
//
// The damaged airship's altitude picks a row in steps of six
// (world/sprite.asm:1841-1854) while the step within a row runs 0-6 — one wider
// than the row — so a row's last read falls through onto the next row's leading
// blank frame (world/sprite.asm:1876-1888). The table's final entry exists to
// serve that read for the last row.
inline constexpr std::size_t kSmokingAirshipFrameRowStride = 6;
inline constexpr std::size_t kSmokingAirshipSecondLabelStep = 18;

// --- entry types ---------------------------------------------------------------

// One step of a frame-sequence table: the step, and the frame shown at it.
struct WorldAnimFrameStep {
    std::uint16_t index;
    WorldAnimFrameId frame;
};

// --- the cartridge views -------------------------------------------------------

// One animation frame, read in place.
//
// A frame is a count byte followed by four bytes per sprite
// (world/world_anim.asm:14-24). The bytes stay where they are — this is a view
// over the cartridge, not a copy — so it is only as good as the span it was
// built from.
//
// Eight of the frames store more sprite rows than their count byte declares.
// Both world draw routines read exactly the declared count, so those rows never
// reach the screen; spriteCount() is what the game draws and storedRows() is
// what the record physically holds. Drawing storedRows() worth of sprites would
// put arrows and rocks on screen that the original never shows — see
// docs/Bugs.md "World animation frames store sprite rows the game never draws".
class WorldAnimFrame {
public:
    WorldAnimFrame() = default;

    // A frame over `bytes`, which must begin at the count byte and run to the
    // end of the record. A span too short to hold the count byte yields an
    // empty frame (valid() == false) rather than reading past the end.
    explicit WorldAnimFrame(std::span<const std::uint8_t> bytes);

    // Whether the span held a record. Everything below reads as zero or empty
    // when it did not.
    bool valid() const { return !bytes_.empty(); }

    // How many sprites the game draws for this frame.
    std::uint8_t spriteCount() const;

    // How many whole sprite rows the record stores. Never less than
    // spriteCount(); greater for the eight frames noted above.
    std::size_t storedRows() const;

    // The sprite at `row`, or a zeroed sprite when `row` is past storedRows().
    WorldAnimSprite sprite(std::size_t row) const;

    // The record's bytes, exactly as the cartridge holds them.
    std::span<const std::uint8_t> bytes() const { return bytes_; }

private:
    std::span<const std::uint8_t> bytes_{};
};

// Every animation frame, over the cartridge region holding them.
//
// The region is one extent: a pointer table of one 16-bit offset per frame,
// then the records those offsets address. Two frames can share a record — the
// blank frame sits at the same offset as the first airship frame, and two of
// the esper frames are one record under two names — so offsets repeat and are
// never deduplicated.
class WorldAnimFrames {
public:
    WorldAnimFrames() = default;

    // The frames over `region`, which must be the whole family: the pointer
    // table followed by the records. A span too short for the pointer table
    // yields an empty set (valid() == false).
    explicit WorldAnimFrames(std::span<const std::uint8_t> region);

    // Whether the span held the whole family.
    bool valid() const { return !region_.empty(); }

    // Where `frame`'s record begins, counted from the first record.
    std::uint16_t offsetOf(WorldAnimFrameId frame) const;

    // The record `frame` names, bounded by the next record that starts after
    // it — so a frame's storedRows() covers everything the record holds,
    // including rows the game does not draw.
    WorldAnimFrame frameAt(WorldAnimFrameId frame) const;

    // The two halves of the region: the offsets, and the records they address.
    std::span<const std::uint8_t> pointerTable() const;
    std::span<const std::uint8_t> records() const;

private:
    std::span<const std::uint8_t> region_{};
};

// --- frame sequences -----------------------------------------------------------

// The frames of the dismounting-chocobo cycle, in the order it steps through
// them (world/sprite.asm:1700-1702).
std::span<const WorldAnimFrameStep> dismountChocoboFrames();

// The frames the damaged airship shows, six per altitude row plus the trailing
// blank the last row's overrun reads.
std::span<const WorldAnimFrameStep> smokingAirshipFrames();

// The bird's four-step cycle (world/sprite.asm:2311-2320).
std::span<const WorldAnimFrameStep> birdFrames();

}  // namespace ostinato
