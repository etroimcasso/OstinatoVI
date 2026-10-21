// The screen, the depth every layer draws at, and the names its sprites and regions go by.
//
// Depth is one table so two layers never collide: the field's world and sprite layers take the
// bands below kDialogueTextZ, and every later layer allocates its own depth here. A key is what the
// engine recognises an object by from one frame to the next — an object keeping its key eases to
// its new place, a new key appears where it is put — so every key names the thing it is, never its
// position in a list.
#pragma once

#include <cstdint>
#include <string_view>

#include "retropp/geometry.h"
#include "retropp/viewport.h"

namespace ostinato::render {

// --- the screen ---------------------------------------------------------------------------------

// Every sprite layer covers the whole screen.
inline constexpr retropp::PixelSize kScreenSize = retropp::ViewportResolution::Snes.size();

// --- depth --------------------------------------------------------------------------------------

// The dialogue text, its choice arrow, and the map title.
inline constexpr std::int32_t kDialogueTextZ = 30;

// Within a sprite layer: every drop shadow beneath every piece of ink, so no shadow — a letter's own
// or a neighbour's — ever darkens a letter.
inline constexpr std::int32_t kShadowSpriteZ = 0;
inline constexpr std::int32_t kInkSpriteZ = 1;

// --- keys ---------------------------------------------------------------------------------------

// A dialogue glyph: dlg:<page>:<line>:<n>, the n-th glyph of a line of a page. The page count runs
// on for the life of the dialogue state, so nothing on a new page eases from the old page.
inline constexpr std::string_view kDialogueGlyphKey = "dlg";

// A map title glyph: title:<n>.
inline constexpr std::string_view kTitleGlyphKey = "title";

// The choice arrow: dlgc:<page>:<choice>. Keyed by the choice it points at, so moving it puts it
// straight at the new choice rather than sliding it there.
inline constexpr std::string_view kDialogueCursorKey = "dlgc";

// A wiped half-line: dlgw:<line>:<half>.
inline constexpr std::string_view kDialogueWipeKey = "dlgw";

// What a sprite's shadow adds to the key of the sprite it falls from.
inline constexpr std::string_view kShadowKeySuffix = ":shadow";

}  // namespace ostinato::render
