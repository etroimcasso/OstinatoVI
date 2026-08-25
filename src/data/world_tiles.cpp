#include "data/world_tiles.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The terrain of every tile index, one table per world with terrain.
constexpr std::array<WorldTilePropertiesEntry, kWorldTilePropertyCount>
    kWorldOfBalanceTileProperties = {{
#include "data/generated/world_tile_prop_balance_data.inc"
}};

constexpr std::array<WorldTilePropertiesEntry, kWorldTilePropertyCount>
    kWorldOfRuinTileProperties = {{
#include "data/generated/world_tile_prop_ruin_data.inc"
}};

// The five song tables, each indexed by its own consumer.
constexpr std::array<WorldSongEntry, 4> kAirshipSongs = {{
#include "data/generated/world_song_airship_data.inc"
}};

constexpr std::array<WorldSongEntry, 4> kChocoboSongs = {{
#include "data/generated/world_song_chocobo_data.inc"
}};

constexpr std::array<WorldSongEntry, 4> kWorldSongs = {{
#include "data/generated/world_song_world_data.inc"
}};

constexpr std::array<WorldSongEntry, 2> kTrainSongs = {{
#include "data/generated/world_song_train_data.inc"
}};

constexpr std::array<WorldSongEntry, 2> kSerpentTrenchSongs = {{
#include "data/generated/world_song_serpent_trench_data.inc"
}};

// The movement and presentation curves.
constexpr std::array<WorldCurveEntry, 41> kTrainBattleMosaicCurve = {{
#include "data/generated/train_battle_mosaic_data.inc"
}};

constexpr std::array<BattleZoomEntry, 34> kBattleZoomSteps = {{
#include "data/generated/battle_zoom_data.inc"
}};

constexpr std::array<WorldCurveEntry, 16> kAirshipDirectionAnimationOffsets = {{
#include "data/generated/airship_dir_anim_offset_data.inc"
}};

constexpr std::array<WorldCurveEntry, 16> kCharacterMoveFrames = {{
#include "data/generated/char_move_frame_data.inc"
}};

constexpr std::array<WorldSpriteFlipEntry, 128> kCharacterTopHalfFlips = {{
#include "data/generated/char_top_hflip_data.inc"
}};

constexpr std::array<WorldSpriteFlipEntry, 128> kCharacterBottomHalfFlips = {{
#include "data/generated/char_btm_hflip_data.inc"
}};

constexpr std::array<WorldCurveEntry, 178> kGroundedAirshipSizeCurve = {{
#include "data/generated/grounded_airship_size_data.inc"
}};

// The Magitek train's per-layer tile geometry.
constexpr std::array<TrainLayerSizeEntry, kTrainLayerCount>
    kTrainLayerPixelCounts = {{
#include "data/generated/train_layer_pixel_count_data.inc"
}};

constexpr std::array<TrainLayerSizeEntry, kTrainLayerCount>
    kTrainLayerTileSides = {{
#include "data/generated/train_layer_tile_side_data.inc"
}};

template <typename Table>
constexpr bool indexMatchesPosition(const Table& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].index != i) {
            return false;
        }
    }
    return true;
}

static_assert(indexMatchesPosition(kWorldOfBalanceTileProperties));
static_assert(indexMatchesPosition(kWorldOfRuinTileProperties));
static_assert(indexMatchesPosition(kAirshipSongs));
static_assert(indexMatchesPosition(kChocoboSongs));
static_assert(indexMatchesPosition(kWorldSongs));
static_assert(indexMatchesPosition(kTrainSongs));
static_assert(indexMatchesPosition(kSerpentTrenchSongs));
static_assert(indexMatchesPosition(kTrainBattleMosaicCurve));
static_assert(indexMatchesPosition(kBattleZoomSteps));
static_assert(indexMatchesPosition(kAirshipDirectionAnimationOffsets));
static_assert(indexMatchesPosition(kCharacterMoveFrames));
static_assert(indexMatchesPosition(kCharacterTopHalfFlips));
static_assert(indexMatchesPosition(kCharacterBottomHalfFlips));
static_assert(indexMatchesPosition(kGroundedAirshipSizeCurve));
static_assert(indexMatchesPosition(kTrainLayerPixelCounts));
static_assert(indexMatchesPosition(kTrainLayerTileSides));

// A layer's pixel count is the square of its tile side, on every layer.
constexpr bool layerGeometryIsSquare() {
    for (std::size_t i = 0; i < kTrainLayerCount; ++i) {
        const std::uint32_t side = kTrainLayerTileSides[i].value;
        if (kTrainLayerPixelCounts[i].value != side * side) {
            return false;
        }
    }
    return true;
}

static_assert(layerGeometryIsSquare(),
              "each train layer's pixel count must be its tile side squared");

// Where each train tile's pixels begin, computed from the layer pixel counts
// rather than transcribed: the graphics loader fills the buffer largest layer
// first and cycles that sequence across the tiles, so the offsets are the
// running total of the non-zero layer sizes in descending order.
constexpr std::array<std::uint16_t, kTrainTileOffsetCount>
makeTrainTileOffsets() {
    std::array<std::uint16_t, kTrainLayerCount> steps{};
    std::size_t stepCount = 0;
    for (const auto& layer : kTrainLayerPixelCounts) {
        if (layer.value != 0) {
            steps[stepCount++] = layer.value;
        }
    }
    for (std::size_t i = 1; i < stepCount; ++i) {
        const std::uint16_t key = steps[i];
        std::size_t j = i;
        while (j > 0 && steps[j - 1] < key) {
            steps[j] = steps[j - 1];
            --j;
        }
        steps[j] = key;
    }

    std::array<std::uint16_t, kTrainTileOffsetCount> offsets{};
    std::uint16_t running = kTrainTileBufferBase;
    std::size_t next = 0;
    for (std::size_t tile = 0; tile < kTrainTileCount; ++tile) {
        for (std::size_t step = 0; step < stepCount; ++step) {
            offsets[next++] = running;
            running = static_cast<std::uint16_t>(running + steps[step]);
        }
    }
    return offsets;
}

constexpr std::array<std::uint16_t, kTrainTileOffsetCount> kTrainTileOffsets =
    makeTrainTileOffsets();

static_assert(kTrainTileOffsets.front() == kTrainTileBufferBase,
              "the first train tile begins at the buffer base");

}  // namespace

WorldTileProperties worldTileProperties(WorldMapId world, std::uint8_t tile) {
    return worldTilePropertyTable(world)[tile].properties;
}

WorldTileProperties worldOfBalanceTileProperties(std::uint8_t tile) {
    return kWorldOfBalanceTileProperties[tile].properties;
}

std::span<const WorldTilePropertiesEntry> worldTilePropertyTable(
    WorldMapId world) {
    switch (world) {
        case WorldMapId::WORLD_OF_BALANCE:
            return kWorldOfBalanceTileProperties;
        case WorldMapId::WORLD_OF_RUIN:
            return kWorldOfRuinTileProperties;
        default:
            break;
    }
    assert(false && "world has no terrain table");
    return kWorldOfBalanceTileProperties;
}

SongId airshipSong(std::uint8_t slot) {
    assert(slot < kAirshipSongs.size() && "airship song slot out of range");
    return kAirshipSongs[slot].song;
}

SongId chocoboSong(std::uint8_t slot) {
    assert(slot < kChocoboSongs.size() && "chocobo song slot out of range");
    return kChocoboSongs[slot].song;
}

SongId worldSong(std::uint8_t slot) {
    assert(slot < kWorldSongs.size() && "world song slot out of range");
    return kWorldSongs[slot].song;
}

SongId trainSong(std::uint8_t slot) {
    assert(slot < kTrainSongs.size() && "train song slot out of range");
    return kTrainSongs[slot].song;
}

SongId serpentTrenchSong(std::uint8_t slot) {
    assert(slot < kSerpentTrenchSongs.size()
           && "serpent trench song slot out of range");
    return kSerpentTrenchSongs[slot].song;
}

std::span<const WorldSongEntry> airshipSongs() { return kAirshipSongs; }
std::span<const WorldSongEntry> chocoboSongs() { return kChocoboSongs; }
std::span<const WorldSongEntry> worldSongs() { return kWorldSongs; }
std::span<const WorldSongEntry> trainSongs() { return kTrainSongs; }
std::span<const WorldSongEntry> serpentTrenchSongs() {
    return kSerpentTrenchSongs;
}

std::span<const WorldCurveEntry> trainBattleMosaicCurve() {
    return kTrainBattleMosaicCurve;
}

std::span<const BattleZoomEntry> battleZoomSteps() { return kBattleZoomSteps; }

std::span<const WorldCurveEntry> airshipDirectionAnimationOffsets() {
    return kAirshipDirectionAnimationOffsets;
}

std::span<const WorldCurveEntry> characterMoveFrames() {
    return kCharacterMoveFrames;
}

std::span<const WorldSpriteFlipEntry> characterTopHalfFlips() {
    return kCharacterTopHalfFlips;
}

std::span<const WorldSpriteFlipEntry> characterBottomHalfFlips() {
    return kCharacterBottomHalfFlips;
}

std::span<const WorldCurveEntry> groundedAirshipSizeCurve() {
    return kGroundedAirshipSizeCurve;
}

std::span<const TrainLayerSizeEntry> trainLayerPixelCounts() {
    return kTrainLayerPixelCounts;
}

std::span<const TrainLayerSizeEntry> trainLayerTileSides() {
    return kTrainLayerTileSides;
}

std::uint16_t trainTileOffset(std::size_t step) {
    assert(step < kTrainTileOffsets.size() && "train tile step out of range");
    return kTrainTileOffsets[step];
}

}  // namespace ostinato
