// CharacterSprite — one party member standing, or fallen, on the battlefield.
//
//     Sprites s = CharacterSprite("chr:0", standing.x, standing.y, frame, standing.mirrored, sheet,
//                                 palette, z);
//
// A party member is drawn in one of its 32 frames (data/battle_gfx_tables.h, data/character_battle_sheet.h).
// A standing frame is one 16 x 32 sprite placed kStandingFrameRaisePx above (x, y), so its blank top
// row sits above the point and its 16 x 24 of art below it. The fallen frame, frame 0, is one 32 x 16
// sprite kFallenFrameDropPx below the point, kFallenFrameLeftPx to its left — or, mirrored,
// kFallenFrameMirroredLeftPx to its left. Either frame is mirrored left to right when `mirrored`;
// unmirrored art faces left.
#pragma once

#include <cstdint>
#include <string_view>

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"

namespace ostinato::render {

// A party member's two sheets as uploaded: the standing frames, one cell per frame, and the fallen
// frame as one cell.
struct CharacterSheet {
    retropp::AtlasManifest standing;
    retropp::AtlasManifest fallen;
};

// How the frames sit about the point a party member stands at.
inline constexpr int kStandingFrameRaisePx = 8;
inline constexpr int kFallenFrameDropPx = 8;
inline constexpr int kFallenFrameLeftPx = 4;
inline constexpr int kFallenFrameMirroredLeftPx = 12;

// PRECONDITION (asserted): `frame` is below 32 and the sheets have its cell.
[[nodiscard]] Sprites CharacterSprite(std::string_view key, int x, int y, int frame, bool mirrored,
                                      const CharacterSheet& sheet, retropp::PaletteId palette,
                                      std::int32_t z);

}  // namespace ostinato::render
