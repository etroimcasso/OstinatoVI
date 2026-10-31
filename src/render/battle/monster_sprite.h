// MonsterSprite — one monster standing on the battlefield.
//
//     Sprites s = MonsterSprite("mon:2", standing.x, standing.y, standing.mirrored, image, palette, z);
//
// A monster's picture is one sprite: its whole image (data/monster_image.h, uploaded as one sheet of
// one cell) at (x, y), through its sixteen colors, mirrored left to right when `mirrored`. Unmirrored
// art faces right.
#pragma once

#include <cstdint>
#include <string_view>

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"

namespace ostinato::render {

// PRECONDITION (asserted): `image` holds one cell.
[[nodiscard]] Sprites MonsterSprite(std::string_view key, int x, int y, bool mirrored,
                                    const retropp::AtlasManifest& image, retropp::PaletteId palette,
                                    std::int32_t z);

}  // namespace ostinato::render
