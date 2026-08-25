// What the world map's terrain does at a given tile: whether it can be walked
// or landed on, whether random battles happen there, and which of a handful of
// named places it is. The world program reads one of these for the tile under
// the party on every step (world/move.asm:1366-1393).
//
// The bits live in a single ROM word. This type stores that word untouched and
// names the bits through accessors, so a caller asks a question instead of
// masking.
#pragma once

#include <array>
#include <cstdint>

namespace ostinato {

// One tile's terrain properties, as the packed word the ROM stores.
//
// Three bits the corpus sets have no consumer anywhere in the world module —
// they are preserved in the stored word and reachable through raw(), but this
// type does not name them, because nothing reads them to name them after.
struct WorldTileProperties {
    std::array<std::uint8_t, 2> bytes = {0, 0};

    // The raw ROM word (little-endian byte pair).
    constexpr std::uint16_t raw() const {
        return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
    }

    // The airship refuses to touch down here (world/init.asm:1831).
    constexpr bool airshipCannotLand() const { return (raw() & 0x0002) != 0; }

    // On foot, the party cannot enter this tile (world/move.asm:1015, :1047,
    // :1079, :1111 — one per facing).
    constexpr bool impassableOnFoot() const { return (raw() & 0x0010) != 0; }

    // Forest cover: the party's sprite is drawn translucent while standing
    // here (world/move.asm:846-850).
    constexpr bool isForest() const { return (raw() & 0x0020) != 0; }

    // Random battles can trigger on this tile (world/move.asm:878-879).
    constexpr bool battlesEnabled() const { return (raw() & 0x0040) != 0; }

    // Which battle background and encounter group the tile uses. The world
    // program passes the high byte to the battle setup, which masks it to
    // three bits (world/move.asm:880-881 -> field/battle.asm:103-107) and
    // indexes the per-world background table with it.
    constexpr std::uint8_t battleBackgroundSelector() const {
        return static_cast<std::uint8_t>((raw() >> 8) & 0x07);
    }

    // The Veldt (world/init.asm:1872).
    constexpr bool isVeldt() const { return (raw() & 0x2000) != 0; }

    // The Phoenix Cave entrance (world/init.asm:1853).
    constexpr bool isPhoenixCaveEntrance() const {
        return (raw() & 0x4000) != 0;
    }

    // Kefka's Tower entrance (world/init.asm:1837).
    constexpr bool isKefkasTowerEntrance() const {
        return (raw() & 0x8000) != 0;
    }

    // Build from the raw word, so every construction site names one value
    // rather than two loose bytes.
    static constexpr WorldTileProperties of(std::uint16_t word) {
        return WorldTileProperties{{static_cast<std::uint8_t>(word & 0xFF),
                                    static_cast<std::uint8_t>((word >> 8)
                                                              & 0xFF)}};
    }
};

static_assert(sizeof(WorldTileProperties) == 2,
              "WorldTileProperties must be byte-identical to the ROM's "
              "property word");
static_assert(alignof(WorldTileProperties) == 1,
              "WorldTileProperties must be alignment-1 so an array of them "
              "matches the ROM's contiguous word table");
static_assert(WorldTileProperties::of(0x2644).raw() == 0x2644
                  && WorldTileProperties::of(0x2644).bytes[0] == 0x44
                  && WorldTileProperties::of(0x2644).bytes[1] == 0x26,
              "WorldTileProperties::of must round-trip the little-endian word");
static_assert(WorldTileProperties::of(0x2644).battlesEnabled()
                  && WorldTileProperties::of(0x2644).isVeldt()
                  && !WorldTileProperties::of(0x2644).impassableOnFoot()
                  && WorldTileProperties::of(0x2644).battleBackgroundSelector()
                         == 6,
              "WorldTileProperties must decode a Veldt tile the way the world "
              "program reads it");
static_assert(WorldTileProperties::of(0x0366).isForest()
                  && WorldTileProperties::of(0x0366).airshipCannotLand()
                  && WorldTileProperties::of(0x0366).battlesEnabled(),
              "WorldTileProperties must decode a forest tile the way the world "
              "program reads it");

}  // namespace ostinato
