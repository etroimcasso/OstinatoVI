#include "render/battle/monster_name_list.h"

#include <cstddef>
#include <optional>
#include <string>

#include "data/battle_gfx_tables.h"
#include "render/battle/hud_text.h"
#include "render/battle/layout.h"
#include "render/layers.h"

namespace ostinato::render {

Sprites MonsterNameList(const battle::MonsterNameList& names, const TextCorpus& text, Language language,
                        retropp::PaletteId palette, const retropp::AtlasManifest& smallFont) {
    const BattleTextPlacementEntry& place = battleTextPlacement(BattleTextPlacementId::MONSTER_NAMES, language);
    const int x = place.column * kBattleCellPx;
    Sprites sprites;
    for (int line = 0; line < static_cast<int>(names.size()); ++line) {
        const std::optional<MonsterId>& name = names[static_cast<std::size_t>(line)];
        if (!name) {
            continue;
        }
        const Sprites letters = HudText(std::string{kHudGlyphKey} + ":mon:" + std::to_string(line),
                                        text.monsterName(*name), x, hudLineY(line), palette, smallFont);
        sprites.insert(sprites.end(), letters.begin(), letters.end());
    }
    return sprites;
}

}  // namespace ostinato::render
