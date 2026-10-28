// The battle preview: a battle's standing scene from the installed cartridge — the background, the
// monsters and the party where the battle puts them, drawn by their components. A developer tool for
// watching the battle's picture on real formations; nobody drives it. It is its own program and is
// not part of the game.
//
//     ostinato-preview-battle 0 2 59                     # show each formation's scene, then close
//     ostinato-preview-battle --capture shots 0 2 59     # no window: each scene written to shots/
//
// main.cpp wires the program; this is its screen and the sheets the screen draws from.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/battle_background_art.h"
#include "data/monster_sprites.h"
#include "render/battle/battle_background.h"
#include "render/battle/character_sprite.h"
#include "render/types.h"

namespace ostinato::preview {

// The pose a party member stands in when nothing is happening to it.
inline constexpr int kIdlePose = 6;

// One battler's picture as uploaded: a monster's whole image and its size, or a party member's two
// sheets, and the palette it draws through.
struct BattlerSheet {
    retropp::AtlasManifest image;      // a monster
    MonsterSpriteSize size;            // a monster, in tiles
    render::CharacterSheet sheet;      // a party member
    retropp::PaletteId palette;
};

// The sheets and palettes the screen draws with, as uploaded to the renderer for one battle.
struct BattleSheets {
    retropp::AtlasManifest backdrop;  // one solid tile
    retropp::PaletteId backdropColor; // the color the background's see-through pixels show
    BattleBackgroundPicture picture;  // the background as decoded
    render::BackgroundSheet background;
    std::array<std::optional<BattlerSheet>, battle::kBattlerSlots> battlers;  // every battler present
};

// The preview's screen, back to front: the backdrop; the background on its own tile layer, cut off
// one line above the HUD; then one sprite layer per battler standing, in the battle's draw order,
// each where the battle puts it. The party stands in its idle pose.
[[nodiscard]] render::Layers BattleScreen(const battle::BattleState& state, const BattleSheets& sheets);

}  // namespace ostinato::preview
