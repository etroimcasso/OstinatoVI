#include "battle_preview.h"

#include <cstddef>
#include <string>
#include <vector>

#include "data/attack_animations.h"
#include "data/battle_gfx_tables.h"
#include "data/formations.h"
#include "data/monster_align.h"
#include "ostinato/battle_background_id.h"
#include "render/battle/battle_placement.h"
#include "render/battle/layout.h"
#include "render/battle/monster_sprite.h"
#include "render/layers.h"
#include "render/sprite_layer.h"
#include "render/tile_layer.h"

namespace ostinato::preview {

namespace {

using battle::BattlerSlot;

// The backdrop covers the screen in cells.
constexpr int kBackdropColumns = render::kScreenSize.width / render::kBattleCellPx;
constexpr int kBackdropRows = render::kScreenSize.height / render::kBattleCellPx;

render::Cells Backdrop(const retropp::AtlasManifest& sheet, retropp::PaletteId color) {
    return render::Cells(static_cast<std::size_t>(kBackdropColumns * kBackdropRows),
                         retropp::TileCell{.atlas = sheet.atlasId, .tile = sheet[0].tile, .palette = color});
}

// Where each battler standing in `state` stands, or nothing for one not on the field.
struct Standing {
    std::optional<render::MonsterStanding> monster;
    std::optional<render::PartyStanding> member;
};

std::array<Standing, battle::kBattlerSlots> standings(const battle::BattleState& state, const BattleSheets& sheets) {
    std::array<Standing, battle::kBattlerSlots> out{};
    const Formation& formation = getFormation(state.formation);
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        const battle::Battler& battler = state.at(slot);
        const std::optional<BattlerSheet>& sheet = sheets.battlers[static_cast<std::size_t>(index)];
        if (!battler.present || !sheet) {
            continue;
        }
        if (slot.isMonster()) {
            if (!battler.monster || !state.monstersStanding.has(slot)) {
                continue;
            }
            const auto position = static_cast<std::size_t>(slot.position());
            const std::uint8_t overlap = monsterOverlap(*battler.monster);
            out[static_cast<std::size_t>(index)].monster =
                state.colosseum
                    ? render::colosseumMonsterStanding(sheet->size, getMonsterAlignment(*battler.monster), overlap)
                    : render::monsterStanding(state.battleType, formation.positionX(position),
                                              formation.positionY(position), sheet->size, overlap);
        } else {
            out[static_cast<std::size_t>(index)].member = render::partyMemberStanding(
                state.battleType, static_cast<std::size_t>(slot.position()), battler.backRow, battler.actsAsEnemy,
                state.background == BattleBackgroundId::MAGITEK_TRAIN, state.colosseum);
        }
    }
    return out;
}

}  // namespace

render::Layers BattleScreen(const battle::BattleState& state, const BattleSheets& sheets) {
    render::Layers layers{
        render::TileLayer(render::kBattleBackdropKey, render::kBattleBackdropZ,
                          Backdrop(sheets.backdrop, sheets.backdropColor), kBackdropColumns, kBackdropRows),
        render::TileLayer(render::kBattlefieldKey, render::kBattlefieldZ,
                          render::BattleBackground(sheets.picture, sheets.background), kBackgroundColumns,
                          kBackgroundRowsShown, {.regions = render::BattlefieldClip()}),
    };

    const std::array<Standing, battle::kBattlerSlots> where = standings(state, sheets);
    std::array<std::optional<int>, battle::kBattlerSlots> bottomEdges{};
    for (std::size_t index = 0; index < where.size(); ++index) {
        if (where[index].monster) {
            bottomEdges[index] = where[index].monster->bottomEdge;
        } else if (where[index].member) {
            bottomEdges[index] = where[index].member->bottomEdge;
        }
    }

    const PoseFrame idle = characterPoseFrames(kIdlePose)[0];
    std::int32_t depth = render::kBattlerBaseZ;
    for (const BattlerSlot slot : render::battlerDrawOrder(bottomEdges)) {
        const auto index = static_cast<std::size_t>(slot.index());
        const BattlerSheet& sheet = *sheets.battlers[index];
        const std::string key = std::string{slot.isMonster() ? render::kMonsterKey : render::kCharacterKey} + ":" +
                                std::to_string(slot.position());
        if (where[index].monster) {
            const render::MonsterStanding& at = *where[index].monster;
            layers.push_back(render::SpriteLayer(
                key, depth, {render::MonsterSprite(key, at.x, at.y, at.mirrored, sheet.image, sheet.palette, 0)}));
        } else {
            const render::PartyStanding& at = *where[index].member;
            layers.push_back(render::SpriteLayer(
                key, depth,
                {render::CharacterSprite(key, at.x, at.y, idle.frameIndex(), at.mirrored != idle.isMirrored(),
                                         sheet.sheet, sheet.palette, 0)}));
        }
        ++depth;
    }
    return layers;
}

}  // namespace ostinato::preview
