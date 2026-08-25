// World-map terrain and presentation data: what each overworld tile does, which
// song plays on the overworld and its vehicles, the small curves the world
// program steps through for movement and screen effects, and the Magitek train
// ride's per-layer tile geometry. The row data is generated
// (src/data/generated/*.inc); this header owns the entry types and accessors.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/song_id.h"
#include "ostinato/world_map_id.h"
#include "ostinato/world_tile_properties.h"

namespace ostinato {

// Tile indices per world tile-property table, and the number of worlds that
// have one. Only the World of Balance and the World of Ruin do; the Serpent
// Trench reuses neither (world/move.asm:1366-1393).
inline constexpr std::size_t kWorldTilePropertyCount = 256;
inline constexpr std::size_t kWorldTilePropertyTables = 2;

// Magitek train graphics layers (world/train_init.asm:4-14).
inline constexpr std::size_t kTrainLayerCount = 13;

// --- entry types --------------------------------------------------------------

// One tile of a world's terrain table: the tile index the world tilemap
// stores, and what that terrain does.
struct WorldTilePropertiesEntry {
    std::uint16_t index;
    WorldTileProperties properties;
};

// One slot of a song table: the slot the consumer indexes with, and the track
// it selects.
struct WorldSongEntry {
    std::uint8_t index;
    SongId song;
};

// One step of a flat curve: the step, and the value read at it.
struct WorldCurveEntry {
    std::uint16_t index;
    std::uint8_t value;
};

// One entry of an h-flip table: the character action, and whether that half of
// the sprite is drawn mirrored.
struct WorldSpriteFlipEntry {
    std::uint16_t index;
    bool flipped;
};

// One step of the battle-transition zoom: the Mode 7 zoom level and the screen
// brightness held for that frame (world/move.asm:1414-1417).
struct BattleZoomEntry {
    std::uint8_t index;
    std::uint8_t zoomLevel;
    std::uint8_t screenBrightness;
};

// One Magitek train graphics layer's geometry.
struct TrainLayerSizeEntry {
    std::uint8_t index;
    std::uint16_t value;
};

// --- terrain ------------------------------------------------------------------

// The terrain properties of one tile of a world.
//
// The world program reaches this two ways, and they disagree. GetWorldTileProp
// (world/move.asm:1366-1393) offsets by the map index, so it reads the table
// belonging to the world the party is on — that is what this accessor does.
// MovePlayer's own direct read (world/move.asm:823-828) omits the offset and
// therefore always lands in the World of Balance table; see
// worldOfBalanceTileProperties() for that path.
WorldTileProperties worldTileProperties(WorldMapId world, std::uint8_t tile);

// The terrain properties MovePlayer's direct read returns: always the World of
// Balance table, whatever world the party is actually on.
//
// This is not a convenience wrapper. The original reads the wrong table there
// and the port reproduces it rather than correcting it; see
// docs/Bugs.md "World-map movement reads terrain from the wrong world" before
// changing anything that calls this. Movement code ported from MovePlayer calls
// this; everything else calls worldTileProperties().
WorldTileProperties worldOfBalanceTileProperties(std::uint8_t tile);

// A whole world's terrain table.
std::span<const WorldTilePropertiesEntry> worldTilePropertyTable(
    WorldMapId world);

// --- songs --------------------------------------------------------------------

// The airship's song for a world (world/init.asm:189).
SongId airshipSong(std::uint8_t slot);
// The chocobo's song for a world (world/init.asm:431).
SongId chocoboSong(std::uint8_t slot);
// The overworld's song for a world (world/init.asm:749).
SongId worldSong(std::uint8_t slot);
// The Magitek train ride's song (world/init.asm:982 reads slot 0 only).
SongId trainSong(std::uint8_t slot);
// The Serpent Trench's song (world/init.asm:1113).
SongId serpentTrenchSong(std::uint8_t slot);

std::span<const WorldSongEntry> airshipSongs();
std::span<const WorldSongEntry> chocoboSongs();
std::span<const WorldSongEntry> worldSongs();
std::span<const WorldSongEntry> trainSongs();
std::span<const WorldSongEntry> serpentTrenchSongs();

// --- movement and presentation curves -----------------------------------------

// Mosaic strength stepped through when a battle starts on the train ride.
std::span<const WorldCurveEntry> trainBattleMosaicCurve();

// The battle-transition zoom, one entry per frame.
std::span<const BattleZoomEntry> battleZoomSteps();

// Frame offsets for the airship's facing: four rows of four, indexed
// row * 4 + column. Rows are not turning, turning right, turning left, and an
// unused fourth; columns are unused, straight, up, down
// (world/sprite.asm:798-804).
std::span<const WorldCurveEntry> airshipDirectionAnimationOffsets();

// The sprite frame at each step of the character's walk cycle, four frames per
// facing.
std::span<const WorldCurveEntry> characterMoveFrames();

// Whether each half of the character sprite is drawn mirrored, per action.
std::span<const WorldSpriteFlipEntry> characterTopHalfFlips();
std::span<const WorldSpriteFlipEntry> characterBottomHalfFlips();

// The size and position curve for the airship sitting on the ground.
std::span<const WorldCurveEntry> groundedAirshipSizeCurve();

// --- Magitek train tile geometry ----------------------------------------------

// Pixels per tile in a train graphics layer, and the layer's tile side. The
// first is the square of the second on every layer.
std::span<const TrainLayerSizeEntry> trainLayerPixelCounts();
std::span<const TrainLayerSizeEntry> trainLayerTileSides();

// Where a train tile's pixels begin in the graphics buffer.
//
// The ROM ships a precomputed table of these offsets. It carries no authored
// information — it is exactly the running sum of the layer pixel counts,
// walked largest-first and cycled across the tiles — so the port computes it
// instead of shipping a copy.
inline constexpr std::uint16_t kTrainTileBufferBase = 0x2000;
inline constexpr std::size_t kTrainTileCount = 29;
inline constexpr std::size_t kTrainTileOffsetCount = 348;

std::uint16_t trainTileOffset(std::size_t step);

}  // namespace ostinato
