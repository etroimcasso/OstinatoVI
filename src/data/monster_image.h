// A monster's picture, decoded from the player's cartridge: its tiles laid through its stencil, and
// its sixteen colors.
//
// A monster's graphics properties name where its tiles begin in the monster graphics family, how
// many planes they are stored in, which stencil they fill and which palette they draw through
// (data/monster_sprites.h holds the stencil side of the same record). The tiles run through the
// stencil's filled cells row by row, each filled cell taking the next tile; an empty cell shows
// nothing.
//
//     const MonsterImage image = content.monsterImages.image(MonsterId::LOBO, size);
//     image.pixels;  // size.width * 8 by size.height * 8, every pixel a color 0-15, 0 see-through
//     image.colors;  // its sixteen colors
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "data/cartridge_color.h"
#include "data/monster_sprites.h"
#include "ostinato/monster_id.h"
#include "text/font.h"

namespace ostinato {

inline constexpr std::size_t kMonsterPaletteColors = 16;

// A monster decoded.
struct MonsterImage {
    MonsterSpriteSize size;         // in tiles
    text::IndexedImage pixels;      // size.width * 8 by size.height * 8
    std::array<CartridgeColor, kMonsterPaletteColors> colors{};
    bool threePlanes = false;       // stored in three planes: eight colors rather than sixteen
    std::size_t tileBytesRead = 0;  // how many bytes of the graphics family the tiles took

    bool valid() const { return pixels.width > 0; }
};

// Every monster's picture, over the cartridge regions holding the graphics properties, the tiles,
// the palettes and the stencils. The bytes must outlive this object.
class MonsterImages {
public:
    MonsterImages() = default;

    // `graphics` is MONSTER_GFX_TRIMMED and `palettes` MONSTER_GFX_PAL; `sprites` is the stencils
    // over the graphics properties, which name each monster's tiles and palette too.
    MonsterImages(MonsterSprites sprites, std::span<const std::uint8_t> graphics,
                  std::span<const std::uint8_t> palettes);

    bool valid() const;

    // `monster`'s picture at `size`, the size it stands at (MonsterSprites::size — its stencil's
    // extent cut down to its formation slot's box). The tiles beyond `size` are left where they
    // are, as the loader leaves them.
    //
    // Throws std::out_of_range when the properties do not reach the monster or its tiles or
    // palette run past the family they are in.
    MonsterImage image(MonsterId monster, MonsterSpriteSize size) const;

    // Which palette `monster` draws through: an index into the palettes, in eight-color steps.
    std::uint16_t paletteIndex(MonsterId monster) const;

private:
    MonsterSprites sprites_;
    std::span<const std::uint8_t> graphics_;
    std::span<const std::uint8_t> palettes_;
};

}  // namespace ostinato
