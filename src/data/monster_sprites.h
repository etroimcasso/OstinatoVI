// How big each monster's sprite is on the battlefield.
//
// A monster's size is measured from its stencil — the map of which 8×8-pixel tiles its sprite
// fills — and held to the box its formation slot gives it. Sizes are in tiles.
//
// The stencils, and the graphics properties that pick one for each monster, are cartridge content:
// they are read in place out of the player's ROM image (RomAsset::MONSTER_GFX_PROP,
// MONSTER_STENCIL_SMALL and MONSTER_STENCIL_LARGE) through the views below, never copied and never
// compiled in. The boxes are a short table of numbers per formation layout, so they compile in
// (src/data/generated/monster_sprite_boxes_data.inc).
//
//     const MonsterSprites& sprites = content.monsterSprites;   // assets::IngestedContent
//     const Formation& formation = getFormation(id);
//     if (const auto box = monsterSpriteBox(formation.vramMap(), slot)) {
//         const MonsterSpriteSize size = sprites.size(formation.monsterId(slot), *box);
//         // size.width tiles across, size.height tiles down
//     }
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "ostinato/monster_id.h"

namespace ostinato {

// A sprite's size, in tiles of 8 pixels.
struct MonsterSpriteSize {
    int width = 0;
    int height = 0;

    friend bool operator==(const MonsterSpriteSize&, const MonsterSpriteSize&) = default;
};

// The largest sprite one slot of a formation layout shows. A formation names its layout
// (Formation::vramMap()); slot 0 takes the layout's first box, slot 1 the next, and so on. Every
// slot any formation fills has a box.
struct MonsterSpriteBox {
    std::uint8_t layout;
    std::uint8_t slot;
    std::uint8_t width;   // in tiles
    std::uint8_t height;  // in tiles
};

// Every box, layout by layout, slots in order.
std::span<const MonsterSpriteBox> monsterSpriteBoxes();

// The box slot `slot` of layout `layout` gives its monster, or nothing past the layout's last box.
std::optional<MonsterSpriteSize> monsterSpriteBox(std::uint8_t layout, std::size_t slot);

// One stencil, read in place: which tiles of the sprite's square are filled. A small stencil is
// 8×8 tiles, a large one 16×16.
class MonsterStencil {
public:
    MonsterStencil() = default;

    // A stencil over `bytes`, which must hold the whole stencil: 8 bytes for a small one (one per
    // row), 32 for a large one (two per row). Anything else yields an empty stencil
    // (valid() == false).
    MonsterStencil(std::span<const std::uint8_t> bytes, bool large);

    bool valid() const { return !bytes_.empty(); }

    // The side of the stencil's square in tiles: 8 or 16, or 0 when empty.
    int side() const;

    // Whether the tile at `row`, `column` (counted from the top left) is part of the sprite.
    // False outside the square.
    bool filled(int row, int column) const;

    // How much of the square the sprite takes up. Its height is the rows filled from the top,
    // stopping at the first empty row; its width reaches the rightmost filled tile in those rows.
    // A stencil whose top row is empty measures 0 × 0 (no cartridge stencil does).
    MonsterSpriteSize extent() const;

private:
    std::span<const std::uint8_t> bytes_{};
    bool large_ = false;
};

// Every monster's stencil, over the cartridge regions holding the graphics properties and both
// stencil families.
class MonsterSprites {
public:
    MonsterSprites() = default;

    // The sprites over `properties` (MONSTER_GFX_PROP, five bytes per monster graphic),
    // `smallStencils` and `largeStencils` (MONSTER_STENCIL_SMALL and _LARGE).
    MonsterSprites(std::span<const std::uint8_t> properties, std::span<const std::uint8_t> smallStencils,
                   std::span<const std::uint8_t> largeStencils);

    // Whether all three regions were supplied.
    bool valid() const;

    // How many monster graphics the properties hold. More than there are monsters: the rest are
    // the graphics other battlers borrow.
    std::size_t graphicsCount() const;

    // The stencil `monster` is drawn through, or an empty one when the properties do not reach it.
    MonsterStencil stencil(MonsterId monster) const;

    // How big `monster` stands in a slot whose box is `box`: its stencil's extent, cut down to the
    // box. The Ghost Train always stands 16 × 14, whatever its stencil and box.
    MonsterSpriteSize size(MonsterId monster, MonsterSpriteSize box) const;

private:
    std::span<const std::uint8_t> properties_{};
    std::span<const std::uint8_t> smallStencils_{};
    std::span<const std::uint8_t> largeStencils_{};
};

}  // namespace ostinato
