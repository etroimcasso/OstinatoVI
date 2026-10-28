#include "render/battle/monster_sprite.h"

#include <cassert>

namespace ostinato::render {

Sprites MonsterSprite(std::string_view key, int x, int y, bool mirrored,
                      const retropp::AtlasManifest& image, retropp::PaletteId palette, std::int32_t z) {
    assert(image.tileCount() == 1 && "a monster's image is one cell");
    const retropp::AssetSlot& cell = image[0];
    return {retropp::Sprite{.key = key,
                            .x = x,
                            .y = y,
                            .z = z,
                            .size = cell.dimensions,
                            .atlas = image.atlasId,
                            .tile = cell.tile,
                            .palette = palette,
                            .flipX = mirrored}};
}

}  // namespace ostinato::render
