// The windows at the top of the screen: a name in the narrow one, a message in the wide one.
//
//     const battle::ShownMessage& shown = presentation.message->shown;
//     TileLayer(kMessageWindowKey, kMessageWindowZ, MessageWindowBox(shown.window, lang, wallpaper, colors),
//               messageWindowColumns(shown.window, lang), messageWindowRows(shown.window, lang),
//               {.scroll = messageWindowScroll(shown.window, lang), .regions = MessageShading(shown.window, lang)});
//     SpriteLayer(kMessageTextKey, kMessageTextZ, {MessageText(shown, glyphs, ink, shadow)},
//                 {.regions = MessageTextClip(shown)});
//
// MessageWindowBox is the window's cells: a MenuWindow at the size and place the battle's window
// table gives it, drawn from the battle's wallpaper through the battle's window colors.
// MessageShading darkens it a 31st more every two lines from line 8, over the window's own
// rectangle.
//
// MessageText is the text shown, each letter a dialog-font Glyph with its shadow placed against the
// pen as the dialog's are: the wide window's pen from tile column 2, the narrow window's from column
// 9 moved right by the message's centering, a letter's artwork four rows into the 16-pixel line,
// raised by however far a line break has scrolled it. Glyph n is keyed msg:<n>.
//
// MessageTextClip hides everything of the text layer outside the line's tiles, so a line scrolling
// up disappears at the top of its line and nothing reaches past the window's text columns.
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/draw_state.h"
#include "retropp/palette.h"

#include "battle/presentation.h"
#include "ostinato/game_version.h"
#include "render/types.h"

namespace ostinato::render {

[[nodiscard]] Cells MessageWindowBox(battle::MessageWindow window, Language language,
                                     const retropp::AtlasManifest& wallpaper, retropp::PaletteId colors);

int messageWindowColumns(battle::MessageWindow window, Language language);
int messageWindowRows(battle::MessageWindow window, Language language);
retropp::LayerScroll messageWindowScroll(battle::MessageWindow window, Language language);

[[nodiscard]] Regions MessageShading(battle::MessageWindow window, Language language);

// `glyphs` is the dialog font's uploaded glyph sheet (text::buildGlyphSheet); `ink` the player's
// font color, `shadow` black.
[[nodiscard]] Sprites MessageText(const battle::ShownMessage& message, const retropp::AtlasManifest& glyphs,
                                  retropp::PaletteId ink, retropp::PaletteId shadow);

[[nodiscard]] Regions MessageTextClip(const battle::ShownMessage& message);

}  // namespace ostinato::render
