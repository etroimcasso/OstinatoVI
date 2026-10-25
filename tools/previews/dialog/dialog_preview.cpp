#include "dialog_preview.h"

#include <string_view>

#include "render/layers.h"
#include "render/sprite_layer.h"
#include "render/text/dialog_cursor.h"
#include "render/text/dialog_text.h"
#include "render/text/dialog_wipe.h"
#include "render/text/map_title.h"
#include "text/layout.h"

namespace ostinato::preview {

namespace {

// The one layer the screen has.
constexpr std::string_view kDialogLayerKey = "dialog";

}  // namespace

render::Layers DialogPreviewScreen(const text::DialogState& state,
                                   const text::DialogWindowState& window,
                                   const DialogSheets& sheets) {
    const int x = text::kTextAreaLeft;
    const int y = text::textAreaTop(window);
    const int pitch = text::linePitch(window);
    return {
        render::SpriteLayer(
            kDialogLayerKey, render::kDialogTextZ,
            {render::DialogText(state, x, y, pitch, sheets.glyphs, sheets.ink, sheets.shadow),
             render::DialogCursor(state, x, y, pitch, sheets.arrow, sheets.ink, sheets.shadow),
             render::MapTitle(state, x, text::titleTextAreaTop(), sheets.glyphs, sheets.ink,
                              sheets.shadow)},
            {.regions = render::DialogWipe(state, x, y, pitch)}),
    };
}

}  // namespace ostinato::preview
