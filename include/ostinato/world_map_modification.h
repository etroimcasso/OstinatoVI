// The world map's tile-modification records: the story-gated patches that edit
// the overworld tilemap as the game progresses (a bridge collapsing, a town
// burning). ModifyMap (world/init.asm:1913-1989) walks a world's list once at
// load time, applies every chunk whose event bit is set, and skips the rest.
//
// A chunk is four bytes: the event bit that gates it, and the offset of the tile
// patch it applies. The patch itself lives in a separate pool and is not
// referenced here — WorldTilePatchRef names a location without resolving it.
#pragma once

#include <array>
#include <cstdint>

namespace ostinato {

// A reference to one of the game's event bits — the flags that record story
// progress. The stored word is the ROM's raw value; the consumer splits it into
// a byte index and a mask (world/init.asm:1940-1949) rather than treating it as
// a flat bit number.
//
// Bit 15 is masked off before the split (init.asm:1945) and is set by no row in
// the corpus, so index() and the raw word agree on every shipped entry.
struct EventBitRef {
    std::array<std::uint8_t, 2> bytes = {0, 0};

    // The raw ROM word (little-endian byte pair).
    constexpr std::uint16_t raw() const {
        return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
    }

    // The event bit's number, with the consumer's mask applied.
    constexpr std::uint16_t index() const {
        return static_cast<std::uint16_t>(raw() & 0x7FFF);
    }

    // Where the bit lives in the event-bit array: which byte, and which bit of
    // that byte. Together these reproduce the consumer's test.
    constexpr std::uint16_t byteIndex() const {
        return static_cast<std::uint16_t>(index() >> 3);
    }
    constexpr std::uint8_t bitMask() const {
        return static_cast<std::uint8_t>(1u << (raw() & 0x07));
    }

    // Build from an event bit number.
    static constexpr EventBitRef of(std::uint16_t bit) {
        return EventBitRef{{static_cast<std::uint8_t>(bit & 0xFF),
                            static_cast<std::uint8_t>((bit >> 8) & 0xFF)}};
    }
};

static_assert(sizeof(EventBitRef) == 2,
              "EventBitRef must be byte-identical to the ROM's event-bit word");
static_assert(alignof(EventBitRef) == 1,
              "EventBitRef must be alignment-1 to sit at any packed-record byte "
              "offset");
static_assert(EventBitRef::of(267).raw() == 267
                  && EventBitRef::of(267).bytes[0] == 0x0B
                  && EventBitRef::of(267).bytes[1] == 0x01,
              "EventBitRef::of must round-trip the little-endian word");
static_assert(EventBitRef::of(267).byteIndex() == 33
                  && EventBitRef::of(267).bitMask() == 0x08,
              "EventBitRef must split a bit number the way the consumer does");

// Where a tile patch begins, as a byte offset from the start of the
// modification block — not from the patch pool, which starts partway into it
// (world/init.asm:1954 adds the block base, not the pool base).
//
// This type is deliberately opaque: it names where a patch lives, not what it
// contains. Resolving it against the pool and decoding the patch is a separate
// concern; nothing here reads the bytes.
struct WorldTilePatchRef {
    std::array<std::uint8_t, 2> bytes = {0, 0};

    // The offset from the start of the modification block.
    constexpr std::uint16_t offsetFromBlockBase() const {
        return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
    }

    // Build from a raw offset, so every construction site names one value
    // rather than two loose bytes.
    static constexpr WorldTilePatchRef at(std::uint16_t offset) {
        return WorldTilePatchRef{{static_cast<std::uint8_t>(offset & 0xFF),
                                  static_cast<std::uint8_t>((offset >> 8)
                                                            & 0xFF)}};
    }
};

static_assert(sizeof(WorldTilePatchRef) == 2,
              "WorldTilePatchRef must be byte-identical to the ROM's offset "
              "word");
static_assert(alignof(WorldTilePatchRef) == 1,
              "WorldTilePatchRef must be alignment-1 to sit at any "
              "packed-record byte offset");
static_assert(WorldTilePatchRef::at(0x0048).offsetFromBlockBase() == 0x0048
                  && WorldTilePatchRef::at(0x0048).bytes[0] == 0x48
                  && WorldTilePatchRef::at(0x0048).bytes[1] == 0x00,
              "WorldTilePatchRef::at must round-trip the little-endian offset");

// One entry of a world's modification list: apply the patch at .patch when the
// event bit at .bit is set. A clear bit skips the chunk (world/init.asm:1951).
struct WorldMapModification {
    EventBitRef bit;
    WorldTilePatchRef patch;
};

static_assert(sizeof(WorldMapModification) == 4,
              "WorldMapModification must be byte-identical to the ROM's 4-byte "
              "chunk");
static_assert(alignof(WorldMapModification) == 1,
              "WorldMapModification must be alignment-1 so an array of them is "
              "byte-identical to the ROM's contiguous chunk list");

}  // namespace ostinato
