#include "battle_preview.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "data/attack_animations.h"
#include "data/battle_gfx_tables.h"
#include "data/formations.h"
#include "data/monster_align.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/game_version.h"
#include "render/battle/battle_message.h"
#include "render/battle/battle_placement.h"
#include "render/battle/hud_text.h"
#include "render/battle/hud_windows.h"
#include "render/battle/layout.h"
#include "render/battle/monster_death_fade.h"
#include "render/battle/monster_name_list.h"
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

// Where each battler shown stands, or nothing for one not on the field.
struct Standing {
    std::optional<render::MonsterStanding> monster;
    std::optional<render::PartyStanding> member;
};

std::array<Standing, battle::kBattlerSlots> standings(const battle::BattleState& state,
                                                      const battle::BattlePresentationState& presentation,
                                                      const BattleSheets& sheets) {
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
            if (!battler.monster || !presentation.monstersShown.has(slot)) {
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

// The battlers, one layer each in the draw order.
void addBattlers(render::Layers& layers, const battle::BattleState& state,
                 const battle::BattlePresentationState& presentation, const BattleSheets& sheets,
                 const std::array<Standing, battle::kBattlerSlots>& where) {
    std::array<std::optional<int>, battle::kBattlerSlots> bottomEdges{};
    for (std::size_t index = 0; index < where.size(); ++index) {
        if (where[index].monster) {
            bottomEdges[index] = where[index].monster->bottomEdge;
        } else if (where[index].member) {
            bottomEdges[index] = where[index].member->bottomEdge;
        }
    }

    const std::optional<battle::DeathFade>& fade = presentation.deathFade;
    std::int32_t depth = render::kBattlerBaseZ;
    for (const BattlerSlot slot : render::battlerDrawOrder(bottomEdges)) {
        const auto index = static_cast<std::size_t>(slot.index());
        const BattlerSheet& sheet = *sheets.battlers[index];
        const std::string key = std::string{slot.isMonster() ? render::kMonsterKey : render::kCharacterKey} + ":" +
                                std::to_string(slot.position());
        if (where[index].monster) {
            const render::MonsterStanding& at = *where[index].monster;
            const bool dying = fade && fade->dying.has(slot) && fade->inDeathPalette();
            render::Sprites monster = render::MonsterSprite(key, at.x, at.y, at.mirrored, sheet.image,
                                                            dying ? sheets.deathColors : sheet.palette, 0);
            if (dying) {
                monster = render::MonsterDeathFade(std::move(monster), *fade);
            }
            layers.push_back(render::SpriteLayer(key, depth, {monster}));
        } else {
            const render::PartyStanding& at = *where[index].member;
            const auto position = static_cast<std::size_t>(slot.position());
            const std::uint8_t pose = battle::partyMemberPose(state.at(slot), presentation.waitingPoses[position]);
            const PoseFrame step =
                characterPoseFrames(pose)[static_cast<std::size_t>(battle::poseStep(presentation.poseCounters[position]))];
            if (step.isNone()) {
                ++depth;
                continue;
            }
            layers.push_back(render::SpriteLayer(
                key, depth,
                {render::CharacterSprite(key, at.x, at.y, step.frameIndex(), at.mirrored != step.isMirrored(),
                                         sheet.sheet, sheet.palette, 0)}));
        }
        ++depth;
    }
}

}  // namespace

render::Layers BattleScreen(const battle::BattleState& state, const battle::BattlePresentationState& presentation,
                            const TextCorpus& text, const BattleSheets& sheets) {
    const Language lang = language(state.version);
    render::Layers layers{
        render::TileLayer(render::kBattleBackdropKey, render::kBattleBackdropZ,
                          Backdrop(sheets.backdrop, sheets.backdropColor), kBackdropColumns, kBackdropRows),
        render::TileLayer(render::kBattlefieldKey, render::kBattlefieldZ,
                          render::BattleBackground(sheets.picture, sheets.background), kBackgroundColumns,
                          kBackgroundRowsShown, {.scroll = {.y = render::kBattlefieldScrollPx}}),
    };

    const std::array<Standing, battle::kBattlerSlots> where = standings(state, presentation, sheets);
    addBattlers(layers, state, presentation, sheets, where);

    if (presentation.message) {
        const battle::ShownMessage& shown = presentation.message->shown;
        layers.push_back(render::TileLayer(
            render::kMessageWindowKey, render::kMessageWindowZ,
            render::MessageWindowBox(shown.window, lang, sheets.wallpaper, sheets.windowColors),
            render::messageWindowColumns(shown.window, lang), render::messageWindowRows(shown.window, lang),
            {.scroll = render::messageWindowScroll(shown.window, lang), .regions = render::MessageShading(shown.window, lang)}));
        layers.push_back(render::SpriteLayer(render::kMessageTextKey, render::kMessageTextZ,
                                             {render::MessageText(shown, sheets.glyphs, sheets.ink, sheets.shadow)},
                                             {.regions = render::MessageTextClip(shown)}));
    }

    layers.push_back(render::TileLayer(render::kHudWindowKey, render::kHudWindowZ,
                                       render::HudWindows(lang, sheets.wallpaper, sheets.windowColors),
                                       render::hudWindowsColumns(lang), render::kHudWindowRows,
                                       {.scroll = render::hudWindowsScroll(lang), .regions = render::HudShading(lang)}));
    layers.push_back(render::SpriteLayer(
        render::kHudTextKey, render::kHudTextZ,
        {render::MonsterNameList(presentation.monsterNames, text, lang, sheets.fontPalettes[render::kHudTextPalette],
                                 sheets.smallFont),
         render::PartyStatus(state, sheets.fontPalettes, sheets.smallFont)}));

    render::NumeralAnchors anchors{};
    for (std::size_t index = 0; index < where.size(); ++index) {
        if (where[index].monster) {
            anchors[index] = render::numeralAnchor(*where[index].monster);
        } else if (where[index].member) {
            anchors[index] = render::numeralAnchor(*where[index].member);
        }
    }
    layers.push_back(render::SpriteLayer(render::kDamageNumbersKey, render::kDamageNumbersZ,
                                         {render::DamageNumbers(presentation, anchors, sheets.numerals)}));
    return layers;
}

}  // namespace ostinato::preview
