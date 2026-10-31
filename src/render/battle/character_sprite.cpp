#include "render/battle/character_sprite.h"

#include <cassert>
#include <cstddef>

#include "data/battle_gfx_tables.h"

namespace ostinato::render {

Sprites CharacterSprite(std::string_view key, int x, int y, int frame, bool mirrored,
                        const CharacterSheet& sheet, retropp::PaletteId palette, std::int32_t z) {
    assert(frame >= 0 && static_cast<std::size_t>(frame) < kCharacterFrameCount && "no such frame");
    if (frame == 0) {
        assert(sheet.fallen.tileCount() == 1 && "the fallen frame is one cell");
        const retropp::AssetSlot& cell = sheet.fallen[0];
        return {retropp::Sprite{.key = key,
                                .x = x - (mirrored ? kFallenFrameMirroredLeftPx : kFallenFrameLeftPx),
                                .y = y + kFallenFrameDropPx,
                                .z = z,
                                .size = cell.dimensions,
                                .atlas = sheet.fallen.atlasId,
                                .tile = cell.tile,
                                .palette = palette,
                                .flipX = mirrored}};
    }
    assert(static_cast<std::size_t>(frame) < sheet.standing.tileCount() && "the sheet has no such frame");
    const retropp::AssetSlot& cell = sheet.standing[static_cast<std::size_t>(frame)];
    return {retropp::Sprite{.key = key,
                            .x = x,
                            .y = y - kStandingFrameRaisePx,
                            .z = z,
                            .size = cell.dimensions,
                            .atlas = sheet.standing.atlasId,
                            .tile = cell.tile,
                            .palette = palette,
                            .flipX = mirrored}};
}

}  // namespace ostinato::render
