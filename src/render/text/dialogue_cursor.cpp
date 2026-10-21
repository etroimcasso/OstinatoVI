#include "render/text/dialogue_cursor.h"

#include <string>

#include "render/layers.h"
#include "text/layout.h"

namespace ostinato::render {

Sprites DialogueCursor(const text::DialogueState& state, int x, int y,
                       const retropp::AtlasManifest& arrow, retropp::PaletteId ink,
                       retropp::PaletteId shadow) {
    if (!state.cursor || arrow.tileCount() == 0) {
        return {};
    }
    const text::ChoicePosition& at = *state.cursor;
    const retropp::AssetSlot& cell = arrow[0];
    const int arrowX = x + at.x;
    const int arrowY = y + at.line * text::kLineHeight + text::kGlyphRowOffset;

    const std::string key = std::string{kDialogueCursorKey} + ":" + std::to_string(state.page) +
                            ":" + std::to_string(state.choices.selection);
    return {
        retropp::Sprite{.key = key + std::string{kShadowKeySuffix},
                        .x = arrowX + text::kCursorShadowDx,
                        .y = arrowY + text::kCursorShadowDy,
                        .z = kShadowSpriteZ,
                        .size = cell.dimensions,
                        .atlas = arrow.atlasId,
                        .tile = cell.tile,
                        .palette = shadow},
        retropp::Sprite{.key = key,
                        .x = arrowX,
                        .y = arrowY,
                        .z = kInkSpriteZ,
                        .size = cell.dimensions,
                        .atlas = arrow.atlasId,
                        .tile = cell.tile,
                        .palette = ink},
    };
}

}  // namespace ostinato::render
