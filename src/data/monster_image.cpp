#include "data/monster_image.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "assets/tile_planes.h"

namespace ostinato {

namespace {

// One graphics-properties record: the graphics word, whose low fifteen bits times eight are the
// tiles' offset into the family and whose top bit marks three planes; the palette word, stored
// high byte first, whose low ten bits are the palette index (its top bit, the large-stencil flag,
// is the stencil side's); and the stencil number.
constexpr std::size_t kGraphicsWordAt = 0;
constexpr std::uint16_t kGraphicsOffsetMask = 0x7FFF;
constexpr std::uint16_t kThreePlanesFlag = 0x8000;
constexpr std::size_t kGraphicsOffsetUnit = 8;
constexpr std::size_t kPaletteHighByteAt = 2;
constexpr std::size_t kPaletteLowByteAt = 3;
constexpr std::uint16_t kPaletteIndexMask = 0x03FF;

// A palette index counts eight-color steps; a monster takes sixteen colors from there.
constexpr std::size_t kPaletteIndexBytes = 8 * 2;
constexpr std::size_t kPaletteBytes = kMonsterPaletteColors * 2;

std::span<const std::uint8_t> recordOf(const MonsterSprites& sprites, MonsterId monster) {
    const std::span<const std::uint8_t> record = sprites.properties(monster);
    if (record.empty()) {
        throw std::out_of_range("no monster graphics properties for monster " +
                                std::to_string(static_cast<int>(monster)));
    }
    return record;
}

}  // namespace

MonsterImages::MonsterImages(MonsterSprites sprites, std::span<const std::uint8_t> graphics,
                             std::span<const std::uint8_t> palettes)
    : sprites_(std::move(sprites)), graphics_(graphics), palettes_(palettes) {}

bool MonsterImages::valid() const {
    return sprites_.valid() && !graphics_.empty() && !palettes_.empty();
}

std::uint16_t MonsterImages::paletteIndex(MonsterId monster) const {
    const std::span<const std::uint8_t> record = recordOf(sprites_, monster);
    return static_cast<std::uint16_t>(
        (record[kPaletteHighByteAt] << 8 | record[kPaletteLowByteAt]) & kPaletteIndexMask);
}

MonsterImage MonsterImages::image(MonsterId monster, MonsterSpriteSize size) const {
    const std::span<const std::uint8_t> record = recordOf(sprites_, monster);
    const auto graphicsWord = static_cast<std::uint16_t>(record[kGraphicsWordAt] |
                                                         record[kGraphicsWordAt + 1] << 8);
    const bool threePlanes = (graphicsWord & kThreePlanesFlag) != 0;
    const assets::TileDepth depth = threePlanes ? assets::TileDepth::ThreePlanes
                                                : assets::TileDepth::FourPlanes;
    const std::size_t tileBytes = assets::tileBytes(depth);
    std::size_t next = static_cast<std::size_t>(graphicsWord & kGraphicsOffsetMask) * kGraphicsOffsetUnit;

    MonsterImage image;
    image.size = size;
    image.threePlanes = threePlanes;
    image.pixels = assets::blankImage(size.width * assets::kTilePx, size.height * assets::kTilePx);

    const MonsterStencil stencil = sprites_.stencil(monster);
    for (int row = 0; row < size.height; ++row) {
        for (int column = 0; column < size.width; ++column) {
            if (!stencil.filled(row, column)) {
                continue;
            }
            if (next + tileBytes > graphics_.size()) {
                throw std::out_of_range("monster " + std::to_string(static_cast<int>(monster)) +
                                        "'s tiles run past the monster graphics");
            }
            assets::drawTile(image.pixels, graphics_.subspan(next, tileBytes), depth,
                             column * assets::kTilePx, row * assets::kTilePx);
            next += tileBytes;
            image.tileBytesRead += tileBytes;
        }
    }

    const std::size_t paletteAt = static_cast<std::size_t>(paletteIndex(monster)) * kPaletteIndexBytes;
    if (paletteAt + kPaletteBytes > palettes_.size()) {
        throw std::out_of_range("monster " + std::to_string(static_cast<int>(monster)) +
                                "'s palette runs past the monster palettes");
    }
    for (std::size_t color = 0; color < kMonsterPaletteColors; ++color) {
        image.colors[color] = colorAt(palettes_.data() + paletteAt, static_cast<unsigned>(color));
    }
    return image;
}

}  // namespace ostinato
