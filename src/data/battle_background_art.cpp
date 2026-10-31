#include "data/battle_background_art.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "data/battle_backgrounds.h"

namespace ostinato {

namespace {

constexpr std::size_t kGraphicsPointerBytes = 3;
constexpr std::size_t kTilemapPointerBytes = 2;
constexpr std::size_t kPaletteBytes = kBackgroundColorCount * 2;

std::string hex(std::uint32_t address) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text = "$";
    for (int shift = 20; shift >= 0; shift -= 4) {
        text += kDigits[(address >> shift) & 0xF];
    }
    return text;
}

// The bytes of `stretch` from `address` on. Throws when the address lies outside it.
std::span<const std::uint8_t> from(const CartridgeStretch& stretch, std::uint32_t address) {
    if (address < stretch.at || address - stretch.at >= stretch.bytes.size()) {
        throw std::out_of_range("a battle background pointer to " + hex(address) + " lies outside " +
                                hex(stretch.at));
    }
    return stretch.bytes.subspan(address - stretch.at);
}

}  // namespace

std::optional<std::size_t> BattleBackgroundPicture::sheetTile(unsigned tileNumber) const {
    constexpr std::array<unsigned, 3> kFirstNumber{0, kBackgroundBlock2FirstTile,
                                                   kBackgroundBlock3FirstTile};
    // The last block loaded over a number is the one that shows there.
    for (std::size_t block = kFirstNumber.size(); block-- > 0;) {
        if (!blockFirstTile[block] || tileNumber < kFirstNumber[block] ||
            tileNumber >= kFirstNumber[block] + blockTiles[block]) {
            continue;
        }
        return *blockFirstTile[block] + (tileNumber - kFirstNumber[block]);
    }
    return std::nullopt;
}

BattleBackgroundArt::BattleBackgroundArt(std::span<const std::uint8_t> graphicsPointers,
                                         std::span<const std::uint8_t> tilemapPointers,
                                         CartridgeStretch graphics, CartridgeStretch tilemaps,
                                         CartridgeStretch mapGraphics,
                                         std::span<const std::uint8_t> palettes)
    : graphicsPointers_(graphicsPointers),
      tilemapPointers_(tilemapPointers),
      graphics_(graphics),
      tilemaps_(tilemaps),
      mapGraphics_(mapGraphics),
      palettes_(palettes) {}

bool BattleBackgroundArt::valid() const {
    return !graphicsPointers_.empty() && !tilemapPointers_.empty() && !graphics_.bytes.empty() &&
           !tilemaps_.bytes.empty() && !mapGraphics_.bytes.empty() && !palettes_.empty();
}

BattleBackgroundPicture BattleBackgroundArt::load(BattleBackgroundId background,
                                                  assets::CartridgeRoutines& routines) const {
    const BattleBackgroundProperties& record = battleBackgroundProperties(background);
    BattleBackgroundPicture picture;

    // The three graphics blocks, each decoded into the same buffer and taken from its start. A
    // pointer into the battle backgrounds' own blocks names a compressed stream; one into the
    // field's map graphics names a block copied whole.
    std::array<std::uint8_t, kBackgroundBufferBytes> buffer{};
    const auto loadBlock = [&](std::size_t block, BattleBackgroundGraphicsId id, std::size_t tiles) {
        if (id == BattleBackgroundGraphicsId::NONE) {
            return;
        }
        const auto entry = static_cast<std::size_t>(id) * kGraphicsPointerBytes;
        if (entry + kGraphicsPointerBytes > graphicsPointers_.size()) {
            throw std::out_of_range("no graphics pointer " + std::to_string(static_cast<int>(id)));
        }
        const std::uint32_t pointer = graphicsPointers_[entry] | graphicsPointers_[entry + 1] << 8 |
                                      graphicsPointers_[entry + 2] << 16;
        std::size_t own = 0;
        if ((pointer >> 16) >= (graphics_.at >> 16)) {
            const std::vector<std::uint8_t> art =
                routines.decompress(assets::compressedStream(from(graphics_, pointer)));
            own = std::min(art.size(), buffer.size());
            std::copy_n(art.begin(), static_cast<std::ptrdiff_t>(own), buffer.begin());
        } else {
            const std::span<const std::uint8_t> whole = from(mapGraphics_, pointer);
            if (whole.size() < buffer.size()) {
                throw std::out_of_range("a map graphics block at " + hex(pointer) + " runs past the end");
            }
            own = buffer.size();
            std::copy_n(whole.begin(), static_cast<std::ptrdiff_t>(own), buffer.begin());
        }
        picture.blockFirstTile[block] = picture.tileCount();
        picture.blockTiles[block] = tiles;
        picture.blockOwnTiles[block] = own / kBackgroundTileBytes;
        picture.tiles.insert(picture.tiles.end(), buffer.begin(),
                             buffer.begin() + static_cast<std::ptrdiff_t>(tiles * kBackgroundTileBytes));
    };
    // Every block the record names is loaded, in order; a record whose first block is double-width
    // leaves its second slot empty.
    loadBlock(0, record.graphics1.id(),
              record.graphics1.isDoubleWidth() ? kBackgroundDoubleBlockTiles : kBackgroundBlockTiles);
    loadBlock(1, record.graphics2, kBackgroundBlockTiles);
    loadBlock(2, record.graphics3, kBackgroundBlockTiles);

    // The tilemap: a pointer within the tilemaps' own bank.
    const auto tilemapEntry = static_cast<std::size_t>(record.tilemap1) * kTilemapPointerBytes;
    if (tilemapEntry + kTilemapPointerBytes > tilemapPointers_.size()) {
        throw std::out_of_range("no tilemap pointer " + std::to_string(static_cast<int>(record.tilemap1)));
    }
    const std::uint32_t tilemapPointer = (tilemaps_.at & 0xFF0000u) | tilemapPointers_[tilemapEntry] |
                                         tilemapPointers_[tilemapEntry + 1] << 8;
    const std::vector<std::uint8_t> map =
        routines.decompress(assets::compressedStream(from(tilemaps_, tilemapPointer)));
    if (map.size() != kBackgroundTilemapBytes) {
        throw std::runtime_error("a battle background tilemap decoded to " + std::to_string(map.size()) +
                                 " bytes, not " + std::to_string(kBackgroundTilemapBytes));
    }
    for (std::size_t cell = 0; cell < picture.cells.size(); ++cell) {
        picture.cells[cell].word = static_cast<std::uint16_t>(map[2 * cell] | map[2 * cell + 1] << 8);
    }

    // The 48 colors.
    const auto paletteAt = static_cast<std::size_t>(record.palette.id()) * kPaletteBytes;
    if (paletteAt + kPaletteBytes > palettes_.size()) {
        throw std::out_of_range("no battle background palette " +
                                std::to_string(static_cast<int>(record.palette.id())));
    }
    for (std::size_t color = 0; color < kBackgroundColorCount; ++color) {
        picture.colors[color] = colorAt(palettes_.data() + paletteAt, static_cast<unsigned>(color));
    }
    return picture;
}

}  // namespace ostinato
