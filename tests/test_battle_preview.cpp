// The battle preview's standing scene, read back from the layers it returns.
//
// No window and no renderer: the sheets are handles carved as an upload would carve them, and a
// real battle is started from the cartridge's formations so the scene is the one the game lays
// out. Fails without a cartridge, as every cartridge-reading test does.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "retropp/draw_state.h"
#include "retropp/image.h"

#include "assets/cartridge.h"
#include "battle/action_executor.h"
#include "battle/battle_sim.h"
#include "battle/battle_start.h"
#include "battle_preview.h"
#include "data/attack_animations.h"
#include "data/battle_background_art.h"
#include "data/battle_gfx_tables.h"
#include "data/character_battle_sheet.h"
#include "data/formations.h"
#include "data/monster_sprites.h"
#include "game/character_record.h"
#include "ostinato/formation_id.h"
#include "ostinato/formation_ref.h"
#include "render/battle/battle_placement.h"
#include "render/battle/layout.h"
#include "render/layers.h"
#include "vanilla_rom.h"

namespace ostinato::preview {
namespace {

using battle::BattlerSlot;

class BattlePreviewTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cartridge_ = new test::IngestedCartridge{test::ingestVanilla()}; }
    static void TearDownTestSuite() {
        delete cartridge_;
        cartridge_ = nullptr;
    }

    static test::IngestedCartridge* cartridge_;
};

test::IngestedCartridge* BattlePreviewTest::cartridge_ = nullptr;

retropp::AtlasManifest carve(int width, int height, retropp::AssetDimensions cell, retropp::ContentKind kind, int count,
                             std::uint16_t id) {
    return retropp::AtlasManifest{
        .atlasId = retropp::AtlasId{id},
        .slots = retropp::sliceLayout(retropp::PixelSize{.width = width, .height = height}, cell, kind,
                                      retropp::ReadOrder::LeftRightThenDown, count),
    };
}

game::Party terraLockeEdgar() {
    game::Party party;
    const CharacterPropId actors[]{CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR};
    for (std::size_t i = 0; i < 3; ++i) {
        party.characters[i] = game::startingRecord(actors[i]);
        party.membership[i] = {.party = 1, .slot = static_cast<std::uint8_t>(i)};
    }
    party.currentParty = 1;
    return party;
}

// The sheets as handles: the background's tiles and three palettes, each present monster's image at
// its size, and each party member's two sheets.
BattleSheets handles(const battle::BattleState& state, const assets::IngestedContent& content) {
    BattleSheets sheets;
    sheets.backdrop = carve(8, 8, {8, 8}, retropp::ContentKind::Tileset, 1, 1);
    sheets.backdropColor = retropp::PaletteId{1};
    sheets.picture = content.battleBackgrounds.load(state.background, *content.routines);
    const auto tiles = static_cast<int>(sheets.picture.tileCount());
    sheets.background = {
        .tiles = carve(8 * (tiles + 1), 8, {8, 8}, retropp::ContentKind::Tileset, tiles + 1, 2),
        .blankTile = static_cast<std::uint16_t>(tiles),
        .palettes = {retropp::PaletteId{5}, retropp::PaletteId{6}, retropp::PaletteId{7}},
    };
    const Formation& formation = getFormation(state.formation);
    std::uint16_t next = 10;
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        const battle::Battler& battler = state.at(slot);
        if (!battler.present) {
            continue;
        }
        BattlerSheet sheet;
        if (slot.isMonster()) {
            const std::optional<MonsterSpriteSize> box =
                monsterSpriteBox(formation.vramMap(), static_cast<std::size_t>(slot.position()));
            sheet.size = content.monsterSprites.size(*battler.monster, *box);
            sheet.image = carve(sheet.size.width * 8, sheet.size.height * 8, {sheet.size.width * 8, sheet.size.height * 8},
                                retropp::ContentKind::Single, 1, next++);
        } else {
            sheet.sheet = {
                .standing = carve(16, 32 * 32, {16, 32}, retropp::ContentKind::SpriteSeries, 32, next++),
                .fallen = carve(32, 16, {32, 16}, retropp::ContentKind::Single, 1, next++),
            };
        }
        sheet.palette = retropp::PaletteId{next++};
        sheets.battlers[static_cast<std::size_t>(index)] = sheet;
    }
    return sheets;
}

const retropp::SpriteContent& spritesOf(const retropp::DrawLayer& layer) {
    return std::get<retropp::SpriteContent>(layer.content);
}

// Lobo alone, in a front attack: the backdrop, the background, then the four battlers from the one
// whose feet are highest on the screen to the lowest. Terra's feet are at 84, Locke's at 102,
// Edgar's at 120; Lobo's depend on its height and its overlap, read from the data.
TEST_F(BattlePreviewTest, LobosSceneIsTheBackdropTheBackgroundAndFourBattlersBackToFront) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    battle::ActionExecutor executor;
    battle::BattleSim sim(battle::BattleStart{.formation = FormationRef::of(FormationId::LOBO)}, terraLockeEdgar(), {},
                          content.text, content.monsterSprites, executor);
    const battle::BattleState& state = sim.state();
    ASSERT_EQ(state.battleType, battle::BattleType::Front);
    const BattleSheets sheets = handles(state, content);
    const render::Layers layers = BattleScreen(state, sheets);
    ASSERT_EQ(layers.size(), 6u);

    EXPECT_EQ(layers[0].key.value, render::kBattleBackdropKey);
    EXPECT_EQ(layers[0].z, render::kBattleBackdropZ);
    EXPECT_EQ(layers[1].key.value, render::kBattlefieldKey);
    EXPECT_EQ(layers[1].z, render::kBattlefieldZ);
    ASSERT_EQ(layers[1].regions.size(), 1u);
    EXPECT_EQ(layers[1].regions[0].key.value, render::kBattlefieldClipKey);
    const auto& field = std::get<retropp::TileContent>(layers[1].content);
    EXPECT_EQ(field.widthInTiles, kBackgroundColumns);
    EXPECT_EQ(field.heightInTiles, kBackgroundRowsShown);

    // Terra's feet at 84, Lobo's at 72 + 32 - 8 + its overlap, Locke's at 102, Edgar's at 120.
    const Formation& formation = getFormation(FormationId::LOBO);
    const MonsterSpriteSize size = sheets.battlers[4]->size;
    const int loboFeet = formation.positionY(0) + size.height * 8 - 8 + monsterOverlap(MonsterId::LOBO);
    EXPECT_EQ(layers[2].key.value, "chr:0");
    const std::string third = loboFeet < 102 ? "mon:0" : "chr:1";
    EXPECT_EQ(layers[3].key.value, third);
    for (std::size_t i = 2; i < layers.size(); ++i) {
        EXPECT_EQ(layers[i].z, render::kBattlerBaseZ + static_cast<std::int32_t>(i - 2));
        EXPECT_EQ(spritesOf(layers[i]).sprites.size(), 1u);
    }
}

TEST_F(BattlePreviewTest, LoboStandsWhereTheFormationPutsItFacingRight) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    battle::ActionExecutor executor;
    battle::BattleSim sim(battle::BattleStart{.formation = FormationRef::of(FormationId::LOBO)}, terraLockeEdgar(), {},
                          content.text, content.monsterSprites, executor);
    const BattleSheets sheets = handles(sim.state(), content);
    const render::Layers layers = BattleScreen(sim.state(), sheets);
    const Formation& formation = getFormation(FormationId::LOBO);
    for (const retropp::DrawLayer& layer : layers) {
        if (layer.key.value != "mon:0") {
            continue;
        }
        const retropp::Sprite& lobo = spritesOf(layer).sprites[0];
        EXPECT_EQ(lobo.x, formation.positionX(0));
        EXPECT_EQ(lobo.y, formation.positionY(0));
        EXPECT_FALSE(lobo.flipX);
        EXPECT_EQ(lobo.size.width, sheets.battlers[4]->size.width * 8);
        EXPECT_EQ(lobo.atlas, sheets.battlers[4]->image.atlasId);
        return;
    }
    FAIL() << "no layer for Lobo";
}

TEST_F(BattlePreviewTest, ThePartyStandsOnTheRightInItsIdlePose) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    battle::ActionExecutor executor;
    battle::BattleSim sim(battle::BattleStart{.formation = FormationRef::of(FormationId::LOBO)}, terraLockeEdgar(), {},
                          content.text, content.monsterSprites, executor);
    const BattleSheets sheets = handles(sim.state(), content);
    const render::Layers layers = BattleScreen(sim.state(), sheets);
    const PoseFrame idle = characterPoseFrames(kIdlePose)[0];
    int found = 0;
    for (const retropp::DrawLayer& layer : layers) {
        for (std::size_t slot = 0; slot < 3; ++slot) {
            if (layer.key.value != "chr:" + std::to_string(slot)) {
                continue;
            }
            ++found;
            const retropp::Sprite& member = spritesOf(layer).sprites[0];
            const render::PartyStanding standing =
                render::partyMemberStanding(battle::BattleType::Front, slot, false, false, false, false);
            EXPECT_EQ(member.x, standing.x) << slot;
            EXPECT_EQ(member.y, standing.y - render::kStandingFrameRaisePx) << slot;
            EXPECT_EQ(member.flipX, idle.isMirrored()) << slot;
            EXPECT_EQ(member.tile, sheets.battlers[slot]->sheet.standing[static_cast<std::size_t>(idle.frameIndex())].tile)
                << slot;
            EXPECT_EQ(member.palette, sheets.battlers[slot]->palette) << slot;
        }
    }
    EXPECT_EQ(found, 3);
}

// A pincer formation: the battle lays the two Lobos out on both sides, one mirrored.
TEST_F(BattlePreviewTest, APincerFormationMirrorsTheRightGroup) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    battle::ActionExecutor executor;
    battle::BattleSim sim(battle::BattleStart{.formation = FormationRef::of(FormationId::LOBO_GUARD_X2)}, terraLockeEdgar(),
                          {}, content.text, content.monsterSprites, executor);
    ASSERT_EQ(sim.state().battleType, battle::BattleType::Pincer);
    const BattleSheets sheets = handles(sim.state(), content);
    const render::Layers layers = BattleScreen(sim.state(), sheets);
    int mirrored = 0;
    int monsters = 0;
    for (const retropp::DrawLayer& layer : layers) {
        if (layer.key.value.rfind("mon:", 0) != 0) {
            continue;
        }
        ++monsters;
        const retropp::Sprite& monster = spritesOf(layer).sprites[0];
        mirrored += monster.flipX ? 1 : 0;
        EXPECT_LT(monster.y + monster.size.height, render::kBattlefieldHeightPx + 32);
    }
    EXPECT_EQ(monsters, 3);
    EXPECT_GT(mirrored, 0);
    EXPECT_LT(mirrored, monsters);
    // The party stands down the middle.
    for (const retropp::DrawLayer& layer : layers) {
        if (layer.key.value.rfind("chr:", 0) == 0) {
            EXPECT_EQ(spritesOf(layer).sprites[0].x, render::kPartyCenterXPx);
        }
    }
}

}  // namespace
}  // namespace ostinato::preview
