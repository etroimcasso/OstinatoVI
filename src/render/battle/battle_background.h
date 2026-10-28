// BattleBackground — the battlefield's tiles, and the line beneath them the background does not reach.
//
//     TileLayer(kBattlefieldKey, kBattlefieldZ, BattleBackground(picture, sheet), kBackgroundColumns,
//               kBackgroundRowsShown, {.regions = BattlefieldClip()});
//
// The background's tilemap as tile cells, the 19 rows the battlefield shows, every cell drawn from
// `sheet` — the background's decoded tiles uploaded as one sheet — through the one of its three
// palettes the cell names, flipped as the cell says. A cell naming a tile none of the background's
// blocks holds draws the sheet's blank tile. The nineteenth row is a line too tall for the
// battlefield: BattlefieldClip makes its last line see-through, so the backdrop shows there.
#pragma once

#include <array>
#include <cstdint>

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "data/battle_background_art.h"
#include "render/types.h"

namespace ostinato::render {

// A background's decoded tiles as uploaded: the sheet, with one blank tile after the background's
// own (`blankTile`), and the three palettes the tilemap's cells name, in order.
struct BackgroundSheet {
    retropp::AtlasManifest tiles;
    std::uint16_t blankTile = 0;
    std::array<retropp::PaletteId, kBackgroundPalettes> palettes{};
};

// PRECONDITION (asserted): every shown cell names one of the three palettes, as every cartridge
// background's does.
[[nodiscard]] Cells BattleBackground(const BattleBackgroundPicture& picture, const BackgroundSheet& sheet);

// The see-through line between the battlefield and the HUD.
[[nodiscard]] Regions BattlefieldClip();

}  // namespace ostinato::render
