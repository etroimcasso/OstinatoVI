// MonsterNameList — the list of monster names in the HUD's left window.
//
//     SpriteLayer(kHudTextKey, kHudTextZ,
//                 {MonsterNameList(shown.monsterNames, content.text, Language::EN, palettes[0], font)});
//
// The list's lines from the first HUD text line down, each the monster's name from the cartridge's
// monster names in the small font (HudText), at the column the battle's text placements give the
// monster names. An empty line draws nothing; the name's padding draws nothing.
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "battle/presentation.h"
#include "data/text_corpus.h"
#include "ostinato/game_version.h"
#include "render/types.h"

namespace ostinato::render {

[[nodiscard]] Sprites MonsterNameList(const battle::MonsterNameList& names, const TextCorpus& text, Language language,
                                      retropp::PaletteId palette, const retropp::AtlasManifest& smallFont);

}  // namespace ostinato::render
