// Map metadata: the per-map properties record and its simplest satellites. Each
// of the 415 defined maps has a 33-byte MapProperties record (LoadMapProp copies
// it whole to $0520-$0540, map.asm:150-169); a parallax record selected by the
// record's +22 index; and the game shares one 128-byte initial-NPC-switch block
// that seeds the new-game event-bit array. The row data is generated
// (src/data/generated/*.inc); this header owns the record types and accessors.
//
// The map id and the default-song id (MapProperties +28) are numeric indices
// here — there is no in-corpus name source for either. They are placeholders:
// giving the maps and the music real names is a planned later cleanup, so the
// index/song surfaces are intentionally reversible.
//
// The 9-bit map-address space is 512 slots wide (map ids are masked & $01ff by
// every consumer), but only 415 maps are defined; kMapCount is the defined
// count. The animation tables (parallax's siblings — palette and bg animation)
// live in src/data/map_animations.h.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/map_animation_indexes.h"
#include "ostinato/map_battle_background.h"
#include "ostinato/map_battle_flags.h"
#include "ostinato/map_bg_sizes.h"
#include "ostinato/map_effect_flags.h"
#include "ostinato/map_graphics_ids.h"
#include "ostinato/map_layout_ids.h"

namespace ostinato {

// The number of defined maps (map_prop.dat is 415 x 33 bytes).
inline constexpr std::size_t kMapCount = 415;

// The width of the addressable map-id space (9-bit; entrance tables cover all
// 512 slots, map_prop covers the 415 defined maps).
inline constexpr std::size_t kMapAddressSpace = 512;

// One 33-byte map-properties record. The field order and byte offsets ARE the
// contract — LoadMapProp copies the record whole and the field consumers read it
// at $0520+offset. Every member is alignment-1, so the record stays sizeof-
// locked at 33 with no padding; offsetof pins each field.
struct MapProperties {
    std::uint8_t titleIndex;             // +0  -> TextCorpus::mapTitle (73 shared)
    MapEffectFlags effectFlags;          // +1
    MapBattleBackground battleBackground;  // +2
    std::uint8_t unknown3;               // +3  no consumer; preserved raw
    std::uint8_t tilePropIndex;          // +4  -> map_tile_prop set (F1)
    MapBattleFlags battleFlags;          // +5
    std::uint8_t windowMask;             // +6  2-bit WindowSelectTbl row
    MapGraphicsIds graphics;             // +7..+12
    MapLayoutIds layouts;                // +13..+16
    std::uint8_t overlayIndex;           // +17 -> overlay_prop set (F1)
    std::uint8_t bg2ScrollX;             // +18
    std::uint8_t bg2ScrollY;             // +19
    std::uint8_t bg3ScrollX;             // +20
    std::uint8_t bg3ScrollY;             // +21
    std::uint8_t parallaxIndex;          // +22 -> MapParallax (0-20)
    MapBgSizes bgSizes;                  // +23..+24
    std::uint8_t paletteIndex;           // +25 -> MapPal set (F1)
    std::uint8_t palAnimIndex;           // +26 -> palette animation; 0 = none
    MapAnimationIndexes animation;       // +27
    std::uint8_t songId;                 // +28 default song (placeholder id)
    std::uint8_t unknown29;              // +29 no consumer; preserved raw
    std::uint8_t scrollRangeWidth;       // +30 tiles; 0 = unclamped
    std::uint8_t scrollRangeHeight;      // +31 tiles; 0 = unclamped
    std::uint8_t colorMathMode;          // +32 bg2/bg3 color-math mode
};

static_assert(sizeof(MapProperties) == 33,
              "MapProperties must be byte-identical to a 33-byte map_prop record");
static_assert(offsetof(MapProperties, titleIndex) == 0);
static_assert(offsetof(MapProperties, effectFlags) == 1);
static_assert(offsetof(MapProperties, battleBackground) == 2);
static_assert(offsetof(MapProperties, unknown3) == 3);
static_assert(offsetof(MapProperties, tilePropIndex) == 4);
static_assert(offsetof(MapProperties, battleFlags) == 5);
static_assert(offsetof(MapProperties, windowMask) == 6);
static_assert(offsetof(MapProperties, graphics) == 7);
static_assert(offsetof(MapProperties, layouts) == 13);
static_assert(offsetof(MapProperties, overlayIndex) == 17);
static_assert(offsetof(MapProperties, bg2ScrollX) == 18);
static_assert(offsetof(MapProperties, bg2ScrollY) == 19);
static_assert(offsetof(MapProperties, bg3ScrollX) == 20);
static_assert(offsetof(MapProperties, bg3ScrollY) == 21);
static_assert(offsetof(MapProperties, parallaxIndex) == 22);
static_assert(offsetof(MapProperties, bgSizes) == 23);
static_assert(offsetof(MapProperties, paletteIndex) == 25);
static_assert(offsetof(MapProperties, palAnimIndex) == 26);
static_assert(offsetof(MapProperties, animation) == 27);
static_assert(offsetof(MapProperties, songId) == 28);
static_assert(offsetof(MapProperties, unknown29) == 29);
static_assert(offsetof(MapProperties, scrollRangeWidth) == 30);
static_assert(offsetof(MapProperties, scrollRangeHeight) == 31);
static_assert(offsetof(MapProperties, colorMathMode) == 32);

// One table entry: the map's identity as a decimal index (a placeholder id;
// there is no corpus name source for maps) alongside the packed record. A
// compile-time assert verifies index == array position.
struct MapPropertiesEntry {
    std::uint16_t index;
    MapProperties record;
};

// One 8-byte parallax record (MapParallax, selected by MapProperties +22). The
// scroll speeds are signed i8 values sign-extended x16 to 16-bit fixed point
// (scroll.asm:117-180, the bmi two's-complement path); the multipliers are u8
// (scroll.asm:181-188).
struct MapParallax {
    std::int8_t bg2SpeedX;        // +0
    std::int8_t bg2SpeedY;        // +1
    std::int8_t bg3SpeedX;        // +2
    std::int8_t bg3SpeedY;        // +3
    std::uint8_t bg2MultiplierX;  // +4
    std::uint8_t bg2MultiplierY;  // +5
    std::uint8_t bg3MultiplierX;  // +6
    std::uint8_t bg3MultiplierY;  // +7
};

static_assert(sizeof(MapParallax) == 8,
              "MapParallax must be byte-identical to an 8-byte parallax record");
static_assert(offsetof(MapParallax, bg2SpeedX) == 0);
static_assert(offsetof(MapParallax, bg2MultiplierX) == 4);

// One parallax entry: its 0-20 index alongside the record.
struct MapParallaxEntry {
    std::uint16_t index;
    MapParallax record;
};

// The number of parallax records (map_parallax.dat is 21 x 8 bytes).
inline constexpr std::size_t kParallaxCount = 21;

// The size of the initial-NPC-switch block (init_npc_switch.dat, 128 bytes =
// 1024 NPC event bits).
inline constexpr std::size_t kInitialNpcSwitchBytes = 128;

// --- accessors ---------------------------------------------------------------

// The properties record for a map (0-414). index must be in range.
const MapProperties& mapProperties(std::uint16_t index);
std::span<const MapPropertiesEntry> mapProperties();

// The parallax record for a parallax index (0-20). index must be in range.
const MapParallax& mapParallax(std::uint8_t index);
std::span<const MapParallaxEntry> mapParallax();

// The new-game initial values for the NPC event-bit array ($1ee0-$1f5f). The
// per-bit meanings are event-domain state ported in a later phase; this is the
// raw 128-byte seed block copied at new game.
std::span<const std::uint8_t> initialNpcSwitches();

}  // namespace ostinato
