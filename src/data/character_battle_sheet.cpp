#include "data/character_battle_sheet.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/tile_planes.h"
#include "data/battle_gfx_tables.h"

namespace ostinato {

namespace {

constexpr assets::TileDepth kDepth = assets::TileDepth::FourPlanes;

// A battle palette is 32 bytes of which the first 24 — twelve colors — are read.
constexpr std::size_t kBattlePaletteBytes = 32;

// A frame's eight tiles are laid two across and four down; the fallen frame's are two 16 x 16
// halves side by side, tiles 0-3 the left (0-1 above 2-3) and 4-7 the right.
constexpr int kFrameColumns = 2;

// The two leg tiles of a frame, mirrored in place for the frames in kFramesWithMirroredLegs.
constexpr std::size_t kFirstLegTile = 6;

std::uint8_t mirroredByte(std::uint8_t byte) {
    std::uint8_t out = 0;
    for (int bit = 0; bit < 8; ++bit) {
        out = static_cast<std::uint8_t>((out << 1) | ((byte >> bit) & 1));
    }
    return out;
}

}  // namespace

CharacterBattleSheets::CharacterBattleSheets(std::span<const std::uint8_t> spriteSheets,
                                             std::span<const std::uint8_t> palettes)
    : spriteSheets_(spriteSheets), palettes_(palettes) {}

bool CharacterBattleSheets::valid() const { return !spriteSheets_.empty() && !palettes_.empty(); }

CharacterBattleSheet CharacterBattleSheets::sheet(CharacterGfxId graphics) const {
    const SheetPlace place = characterSheet(graphics);
    CharacterBattleSheet out;
    out.standing = assets::blankImage(kCharacterFrameWidthPx,
                                      kCharacterFrameHeightPx * static_cast<int>(kCharacterFrameCount));
    out.fallen = assets::blankImage(kFallenFrameWidthPx, kFallenFrameHeightPx);

    const auto tileAt = [&](SheetTile tile) {
        const std::size_t at = place.offset + static_cast<std::size_t>(tile.raw);
        if (at + SheetTile::kTileBytes > spriteSheets_.size()) {
            throw std::out_of_range("a frame of graphics " + std::to_string(static_cast<int>(graphics)) +
                                    " reads past the sprite sheets");
        }
        return spriteSheets_.subspan(at, SheetTile::kTileBytes);
    };

    for (int frame = 0; frame < static_cast<int>(kCharacterFrameCount); ++frame) {
        const auto tiles = characterFrameTiles(frame);
        const bool mirroredLegs = std::find(kFramesWithMirroredLegs.begin(),
                                            kFramesWithMirroredLegs.end(),
                                            frame) != kFramesWithMirroredLegs.end();
        for (std::size_t t = 0; t < kCharacterFrameTileCount; ++t) {
            if (tiles[t].isBlank()) {
                continue;
            }
            std::vector<std::uint8_t> bytes(tileAt(tiles[t]).begin(), tileAt(tiles[t]).end());
            if (mirroredLegs && t >= kFirstLegTile) {
                for (std::uint8_t& byte : bytes) {
                    byte = mirroredByte(byte);
                }
            }
            if (frame == 0) {
                // The fallen frame: tiles 0-3 the left half, 4-7 the right, each half 0-1 above 2-3.
                const int half = static_cast<int>(t / 4);
                const int within = static_cast<int>(t % 4);
                assets::drawTile(out.fallen, bytes, kDepth,
                                 (half * kFrameColumns + within % kFrameColumns) * assets::kTilePx,
                                 (within / kFrameColumns) * assets::kTilePx);
            } else {
                assets::drawTile(out.standing, bytes, kDepth,
                                 static_cast<int>(t % kFrameColumns) * assets::kTilePx,
                                 frame * kCharacterFrameHeightPx +
                                     static_cast<int>(t / kFrameColumns) * assets::kTilePx);
            }
        }
    }
    return out;
}

std::array<CartridgeColor, kCharacterPaletteColors> CharacterBattleSheets::paletteColors(
    CharacterGfxId graphics, bool greenSoldier) const {
    const BattleCharacterPalette palette =
        greenSoldier ? BattleCharacterPalette::GREEN_SOLDIER : battleCharacterPalette(graphics);
    const std::size_t at = static_cast<std::size_t>(palette) * kBattlePaletteBytes;
    if (at + kCharacterPaletteColors * 2 > palettes_.size()) {
        throw std::out_of_range("battle palette " + std::to_string(static_cast<int>(palette)) +
                                " is not in the battle palettes");
    }
    std::array<CartridgeColor, kCharacterPaletteColors> colors{};
    for (std::size_t color = 0; color < colors.size(); ++color) {
        colors[color] = colorAt(palettes_.data() + at, static_cast<unsigned>(color));
    }
    return colors;
}

std::array<CartridgeColor, kCharacterColorCount> characterColors(const CharacterBattleSheets& sheets,
                                                                 CharacterGfxId graphics,
                                                                 std::size_t partySlot,
                                                                 bool greenSoldier) {
    std::array<CartridgeColor, kCharacterColorCount> colors{};
    const auto palette = sheets.paletteColors(graphics, greenSoldier);
    std::copy(palette.begin(), palette.end(), colors.begin());
    const auto status = statusColors(partySlot);
    std::copy(status.begin(), status.end(), colors.begin() + static_cast<std::ptrdiff_t>(palette.size()));
    return colors;
}

}  // namespace ostinato
