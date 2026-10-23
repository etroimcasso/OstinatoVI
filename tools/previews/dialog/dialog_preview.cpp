#include "dialog_preview.h"

#include <string_view>

#include "render/layers.h"
#include "render/sprite_layer.h"
#include "render/text/dialog_cursor.h"
#include "render/text/dialog_text.h"
#include "render/text/dialog_wipe.h"
#include "render/text/map_title.h"

namespace ostinato::preview {

namespace {

// The one layer the screen has.
constexpr std::string_view kDialogLayerKey = "dialog";

}  // namespace

render::Layers DialogPreviewScreen(const text::DialogState& state, const DialogSheets& sheets) {
    return {
        render::SpriteLayer(
            kDialogLayerKey, render::kDialogTextZ,
            {render::DialogText(state, kPreviewTextX, kPreviewTextY, sheets.glyphs, sheets.ink,
                                sheets.shadow),
             render::DialogCursor(state, kPreviewTextX, kPreviewTextY, sheets.arrow, sheets.ink,
                                  sheets.shadow),
             render::MapTitle(state, kPreviewTextX, kPreviewTextY, sheets.glyphs, sheets.ink,
                              sheets.shadow)},
            {.regions = render::DialogWipe(state, kPreviewTextX, kPreviewTextY)}),
    };
}

}  // namespace ostinato::preview
