// The battle preview: battles fought from the installed cartridge, shown as the battle's picture —
// the background, the monsters and the party where the battle puts them, the windows at the top of
// the screen, the HUD, the damage numerals and the monsters' deaths, all drawn by their
// components. A developer tool for watching the battle's picture on real formations; a scripted
// player fights each battle to its end, and nobody drives it. It is its own program and is not part
// of the game.
//
//     ostinato-preview-battle 0 2 59                     # fight each formation in a window, then close
//     ostinato-preview-battle --capture shots 0 2 59     # no window: the moments worth checking written to shots/
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
#include "battle/presentation.h"
#include "data/battle_background_art.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "render/battle/battle_background.h"
#include "render/battle/character_sprite.h"
#include "render/battle/damage_numbers.h"
#include "render/battle/party_status.h"
#include "render/types.h"

namespace ostinato::preview {

// One battler's picture as uploaded: a monster's whole image and its size, or a party member's two
// sheets, and the palette it draws through.
struct BattlerSheet {
    retropp::AtlasManifest image;      // a monster
    MonsterSpriteSize size;            // a monster, in tiles
    render::CharacterSheet sheet;      // a party member
    retropp::PaletteId palette;
};

// The sheets and palettes the screen draws with, as uploaded to the renderer.
struct BattleSheets {
    retropp::AtlasManifest backdrop;  // one solid tile
    retropp::PaletteId backdropColor; // the color the background's see-through pixels show
    BattleBackgroundPicture picture;  // the background as decoded
    render::BackgroundSheet background;
    std::array<std::optional<BattlerSheet>, battle::kBattlerSlots> battlers;  // every battler present

    // The colors a dying monster fades out in.
    retropp::PaletteId deathColors;

    // The HUD's small font and its eight palettes; the battle windows' wallpaper and colors.
    retropp::AtlasManifest smallFont;
    render::SmallFontPaletteIds fontPalettes{};
    retropp::AtlasManifest wallpaper;
    retropp::PaletteId windowColors;

    // The dialog font the windows at the top of the screen write in, and its two palettes.
    retropp::AtlasManifest glyphs;
    retropp::PaletteId ink;
    retropp::PaletteId shadow;

    render::NumeralSheet numerals;
};

// The preview's screen, back to front: the backdrop; the background on its own tile layer, from its
// second pixel row down to the HUD; one sprite layer per battler shown, in the battle's draw order, each
// where the battle puts it — a party member in the pose it stands in, a dying monster fading out;
// the window at the top of the screen and its text while one is up; the HUD's windows, the list of
// monster names and the party's names, HP and gauges; and the damage numerals. The frame they go on
// carries the battle's screen border (render::ScreenBorder), which main.cpp sets.
[[nodiscard]] render::Layers BattleScreen(const battle::BattleState& state,
                                          const battle::BattlePresentationState& presentation, const TextCorpus& text,
                                          const BattleSheets& sheets);

}  // namespace ostinato::preview
