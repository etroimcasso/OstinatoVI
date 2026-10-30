// The battle's HUD, message and numeral components, read back from what they return; and the
// damage numerals' art decoded from the cartridge.
//
// Sheets are handles carved as an upload would carve them; the cartridge supplies the text, the
// font and a real battle for the party's records. Fails without a cartridge, as every
// cartridge-reading test does.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "retropp/draw_state.h"
#include "retropp/image.h"

#include "assets/cartridge.h"
#include "assets/cartridge_routines.h"
#include "assets/tile_planes.h"
#include "battle/action_executor.h"
#include "battle/battle_sim.h"
#include "battle/battle_start.h"
#include "battle/presentation.h"
#include "data/battle_gfx_tables.h"
#include "data/cartridge_color.h"
#include "data/damage_numerals.h"
#include "game/character_record.h"
#include "ostinato/formation_id.h"
#include "ostinato/formation_ref.h"
#include "render/battle/battle_message.h"
#include "render/battle/damage_numbers.h"
#include "render/battle/hud_text.h"
#include "render/battle/hud_windows.h"
#include "render/battle/layout.h"
#include "render/battle/menu_window.h"
#include "render/battle/monster_death_fade.h"
#include "render/battle/monster_name_list.h"
#include "render/battle/party_status.h"
#include "render/layers.h"
#include "text/font.h"
#include "text/small_font.h"
#include "vanilla_rom.h"

namespace ostinato::render {
namespace {

using battle::BattlerSlot;

retropp::AtlasManifest carve(int width, int height, int cell, retropp::ContentKind kind, int count, std::uint16_t id) {
    return retropp::AtlasManifest{
        .atlasId = retropp::AtlasId{id},
        .slots = retropp::sliceLayout(retropp::PixelSize{.width = width, .height = height},
                                      retropp::AssetDimensions{cell, cell}, kind, retropp::ReadOrder::LeftRightThenDown,
                                      count),
    };
}

const retropp::AtlasManifest& smallFontSheet() {
    static const retropp::AtlasManifest sheet = carve(128, 128, 8, retropp::ContentKind::Tileset, 256, 40);
    return sheet;
}

SmallFontPaletteIds fontPalettes() {
    SmallFontPaletteIds ids{};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ids[i] = static_cast<retropp::PaletteId>(100 + i);
    }
    return ids;
}

class BattleHudTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cartridge_ = new test::IngestedCartridge{test::ingestVanilla()}; }
    static void TearDownTestSuite() {
        delete cartridge_;
        cartridge_ = nullptr;
    }

    void SetUp() override {
        OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
        content_ = &*cartridge_->content;
        game::Party party;
        const CharacterPropId actors[]{CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR};
        for (std::size_t i = 0; i < 3; ++i) {
            party.characters[i] = game::startingRecord(actors[i]);
            const std::span<const std::uint8_t> name = content_->text.charName(static_cast<std::size_t>(actors[i]));
            std::copy_n(name.begin(), party.characters[i].name.size(), party.characters[i].name.begin());
            party.membership[i] = {.party = 1, .slot = static_cast<std::uint8_t>(i)};
        }
        party.currentParty = 1;
        sim_.emplace(battle::BattleStart{.formation = FormationRef::of(FormationId::LOBO)}, party, game::BattleSwitches{},
                     content_->text, content_->monsterSprites, executor_);
    }

    battle::BattleState& state() { return sim_->state(); }

    static test::IngestedCartridge* cartridge_;
    const assets::IngestedContent* content_ = nullptr;
    battle::ActionExecutor executor_;
    std::optional<battle::BattleSim> sim_;
};

test::IngestedCartridge* BattleHudTest::cartridge_ = nullptr;

std::vector<int> xs(const Sprites& sprites) {
    std::vector<int> out;
    for (const retropp::Sprite& sprite : sprites) {
        out.push_back(sprite.x);
    }
    return out;
}

// --- HUD text ----------------------------------------------------------------------------------

TEST(BattleHudText, EachCodeIsOneGlyphEightPixelsOnAndTheBlankDrawsNothing) {
    const std::vector<std::uint8_t> codes{0x80, kHudBlank, 0x81};
    const Sprites sprites = HudText("hud:x:0", codes, 40, 176, retropp::PaletteId{7}, smallFontSheet());
    ASSERT_EQ(sprites.size(), 2u);
    EXPECT_EQ(sprites[0].key.value, "hud:x:0:0");
    EXPECT_EQ(sprites[1].key.value, "hud:x:0:2");
    EXPECT_EQ(xs(sprites), (std::vector<int>{40, 56}));
    EXPECT_EQ(sprites[0].y, 176);
    EXPECT_EQ(sprites[0].tile, smallFontSheet()[0x80].tile);
    EXPECT_EQ(sprites[1].tile, smallFontSheet()[0x81].tile);
    EXPECT_EQ(sprites[0].palette, retropp::PaletteId{7});
    EXPECT_EQ(sprites[0].size.width, 8);
}

TEST(BattleHudText, TheLinesFallTwelvePixelsApartAndEachLosesItsLastRow) {
    EXPECT_EQ(hudLineY(0), 164);
    EXPECT_EQ(hudLineY(3), 200);
    const Regions clip = HudTextClip();
    ASSERT_EQ(clip.size(), 4u);
    for (int line = 0; line < 4; ++line) {
        const retropp::Region& region = clip[static_cast<std::size_t>(line)];
        ASSERT_EQ(region.effects.size(), 1u);
        EXPECT_EQ(region.effects[0].kind, retropp::ScreenSpaceEffectKind::Transparency);
        float top = 1e9f;
        float bottom = -1e9f;
        for (const retropp::Point& point : region.shape.points) {
            top = std::min(top, point.y);
            bottom = std::max(bottom, point.y);
        }
        EXPECT_EQ(top, static_cast<float>(171 + 12 * line)) << line;
        EXPECT_EQ(bottom - top, 1.0f) << line;
    }
}

// The blank code, which also stands in a number's place for a leading zero (TrimZeroes4
// :3646-3657), has no ink, so drawing nothing for it is the same picture.
TEST_F(BattleHudTest, TheBlankGlyphHasNoInk) {
    const std::span<const std::uint8_t> bytes =
        content_->smallFont.subspan(std::size_t{kHudBlank} * text::kSmallFontGlyphBytes, text::kSmallFontGlyphBytes);
    EXPECT_TRUE(std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0; }));
}

// --- the list of monster names --------------------------------------------------------------------

// "Lobo": 8b a8 9b a8, then the name record's padding.
TEST_F(BattleHudTest, TheMonsterListShowsEachNameOnItsLineAtTheMonsterColumn) {
    const battle::MonsterNameList names{MonsterId::LOBO, std::nullopt, MonsterId::LOBO, std::nullopt};
    const Sprites sprites = MonsterNameList(names, content_->text, Language::EN, retropp::PaletteId{3}, smallFontSheet());
    ASSERT_EQ(sprites.size(), 8u);
    EXPECT_EQ(xs(Sprites(sprites.begin(), sprites.begin() + 4)), (std::vector<int>{16, 24, 32, 40}));
    EXPECT_EQ(sprites[0].y, 164);
    EXPECT_EQ(sprites[4].y, 188);
    EXPECT_EQ(sprites[0].tile, smallFontSheet()[0x8b].tile);
    EXPECT_EQ(sprites[1].tile, smallFontSheet()[0xa8].tile);
    EXPECT_EQ(sprites[2].tile, smallFontSheet()[0x9b].tile);
    EXPECT_EQ(sprites[0].key.value, "hud:mon:0:0");
}

// --- the party's names, HP and gauges --------------------------------------------------------------

std::vector<const retropp::Sprite*> part(const Sprites& sprites, const std::string& prefix) {
    std::vector<const retropp::Sprite*> out;
    for (const retropp::Sprite& sprite : sprites) {
        if (sprite.key.value.rfind(prefix, 0) == 0) {
            out.push_back(&sprite);
        }
    }
    return out;
}

TEST_F(BattleHudTest, EachPartyMembersNameSitsOnItsSlotsLine) {
    const Sprites sprites = PartyStatus(state(), fontPalettes(), smallFontSheet());
    const auto locke = part(sprites, "hud:name:1:");
    ASSERT_FALSE(locke.empty());
    EXPECT_EQ(locke[0]->x, 112);
    EXPECT_EQ(locke[0]->y, 176);
    EXPECT_EQ(locke[0]->tile, smallFontSheet()[state().party.characters[1].name[0]].tile);
    EXPECT_EQ(locke[0]->palette, fontPalettes()[0]);
    EXPECT_TRUE(part(sprites, "hud:name:3:").empty());  // no fourth member
}

// HP 7 shows as three blanks and a 7 ($b4 + 7) in the last of the four places.
TEST_F(BattleHudTest, HpShowsFourPlacesWithLeadingZerosBlank) {
    state().at(BattlerSlot::character(0)).hp = 7;
    state().at(BattlerSlot::character(1)).hp = 0;
    state().at(BattlerSlot::character(2)).hp = 1020;
    const Sprites sprites = PartyStatus(state(), fontPalettes(), smallFontSheet());
    const auto terra = part(sprites, "hud:hp:0:");
    ASSERT_EQ(terra.size(), 1u);
    EXPECT_EQ(terra[0]->x, 168 + 24);
    EXPECT_EQ(terra[0]->tile, smallFontSheet()[0xbb].tile);
    const auto locke = part(sprites, "hud:hp:1:");
    ASSERT_EQ(locke.size(), 1u);  // 0 still shows its last digit
    EXPECT_EQ(locke[0]->tile, smallFontSheet()[0xb4].tile);
    const auto edgar = part(sprites, "hud:hp:2:");
    ASSERT_EQ(edgar.size(), 4u);  // a zero inside the number shows
    EXPECT_EQ(edgar[1]->tile, smallFontSheet()[0xb4].tile);
}

// A filling gauge: the caps around four cells in palette 5; a full one in palette 2, every cell full.
TEST_F(BattleHudTest, TheGaugeIsCapsAndFourCellsColoredByWhetherItIsFull) {
    battle::Battler& terra = state().at(BattlerSlot::character(0));
    terra.gauge.setMeter(0x51);  // fill level 0x50: step 10, the second cell filled two columns
    battle::Battler& locke = state().at(BattlerSlot::character(1));
    locke.gauge.setMeter(0);     // full
    const Sprites sprites = PartyStatus(state(), fontPalettes(), smallFontSheet());
    const auto filling = part(sprites, "hud:gauge:0:");
    ASSERT_EQ(filling.size(), 6u);
    EXPECT_EQ(filling[0]->x, 200);
    EXPECT_EQ(filling[0]->tile, smallFontSheet()[0xf9].tile);
    EXPECT_EQ(filling[1]->tile, smallFontSheet()[0xf8].tile);
    EXPECT_EQ(filling[2]->tile, smallFontSheet()[0xf0 + 3].tile);
    EXPECT_EQ(filling[3]->tile, smallFontSheet()[0xf0].tile);
    EXPECT_EQ(filling[5]->tile, smallFontSheet()[0xfa].tile);
    EXPECT_EQ(filling[0]->palette, fontPalettes()[5]);
    const auto full = part(sprites, "hud:gauge:1:");
    ASSERT_EQ(full.size(), 6u);
    for (std::size_t cell = 1; cell <= 4; ++cell) {
        EXPECT_EQ(full[cell]->tile, smallFontSheet()[0xf8].tile) << cell;
    }
    EXPECT_EQ(full[0]->palette, fontPalettes()[2]);
}

TEST_F(BattleHudTest, WithTheGaugeOffTheGaugesPlaceShowsATildeAndTheMaxHp) {
    state().config.gaugeShown = false;
    state().at(BattlerSlot::character(0)).maxHp = 63;
    const Sprites sprites = PartyStatus(state(), fontPalettes(), smallFontSheet());
    const auto terra = part(sprites, "hud:gauge:0:");
    ASSERT_EQ(terra.size(), 3u);  // "~", then "  63"
    EXPECT_EQ(terra[0]->tile, smallFontSheet()[0xce].tile);
    EXPECT_EQ(terra[1]->x, 200 + 24);
    EXPECT_EQ(terra[1]->tile, smallFontSheet()[0xb4 + 6].tile);
    EXPECT_EQ(terra[0]->palette, fontPalettes()[0]);
}

TEST_F(BattleHudTest, AMemberInMorphShowsItsMorphGaugeInPaletteSix) {
    state().at(BattlerSlot::character(0)).morphGauge = 0xff;
    const Sprites sprites = PartyStatus(state(), fontPalettes(), smallFontSheet());
    const auto terra = part(sprites, "hud:gauge:0:");
    ASSERT_EQ(terra.size(), 6u);
    EXPECT_EQ(terra[1]->tile, smallFontSheet()[0xf8].tile);
    EXPECT_EQ(terra[0]->palette, fontPalettes()[6]);
}

// --- the HUD's windows -------------------------------------------------------------------------

TEST(BattleHudWindows, TheTwoWindowsSitSideBySideAtTheHudsTopLine) {
    const retropp::AtlasManifest wallpaper = carve(64, 64, 8, retropp::ContentKind::Tileset, 28, 50);
    EXPECT_EQ(hudWindowsColumns(Language::EN), 30);
    EXPECT_EQ(hudWindowsScroll(Language::EN), (retropp::LayerScroll{.x = -8, .y = -152}));
    const Cells cells = HudWindows(Language::EN, wallpaper, retropp::PaletteId{9});
    ASSERT_EQ(cells.size(), 30u * 8u);
    const Cells left = MenuWindow(12, 8, wallpaper, retropp::PaletteId{9});
    const Cells right = MenuWindow(18, 8, wallpaper, retropp::PaletteId{9});
    for (int row = 0; row < 8; ++row) {
        EXPECT_EQ(cells[static_cast<std::size_t>(row * 30)].tile, left[static_cast<std::size_t>(row * 12)].tile);
        EXPECT_EQ(cells[static_cast<std::size_t>(row * 30 + 11)].tile, left[static_cast<std::size_t>(row * 12 + 11)].tile);
        EXPECT_EQ(cells[static_cast<std::size_t>(row * 30 + 12)].tile, right[static_cast<std::size_t>(row * 18)].tile);
        EXPECT_EQ(cells[static_cast<std::size_t>(row * 30 + 29)].tile, right[static_cast<std::size_t>(row * 18 + 17)].tile);
    }
}

float leftOf(const retropp::Region& region) {
    float left = 1e9f;
    for (const retropp::Point& point : region.shape.points) {
        left = std::min(left, point.x);
    }
    return left;
}

float widthOf(const retropp::Region& region) {
    float left = 1e9f;
    float right = -1e9f;
    for (const retropp::Point& point : region.shape.points) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
    }
    return right - left;
}

// From line 151, a 31st more every three lines over the windows to their last line, 215: the first
// band takes nothing and gets no region, the last is two lines at 21 31sts.
TEST(BattleHudWindows, TheShadingDarkensAThirtyFirstMoreEveryThreeLinesFromLine151) {
    const Regions shading = HudShading(Language::EN);
    ASSERT_EQ(shading.size(), 21u);
    EXPECT_EQ(shading.front().key.value, "hudsh:3");
    EXPECT_EQ(shading.front().blend, retropp::BlendMode::Subtract);
    EXPECT_EQ(shading.back().key.value, "hudsh:63");
    EXPECT_EQ(shading.back().effects[0].fill.r, CartridgeColor::expandChannel(21));
    EXPECT_EQ(leftOf(shading.front()), 8.0f);
    EXPECT_EQ(widthOf(shading.front()), 240.0f);
}

// --- the windows at the top of the screen --------------------------------------------------------

TEST(BattleMessageWindow, TheWideWindowIs30By4AtRowOneAndTheNarrow18By4AtColumnSeven) {
    EXPECT_EQ(messageWindowColumns(battle::MessageWindow::Wide, Language::EN), 30);
    EXPECT_EQ(messageWindowRows(battle::MessageWindow::Wide, Language::EN), 4);
    EXPECT_EQ(messageWindowScroll(battle::MessageWindow::Wide, Language::EN), (retropp::LayerScroll{.x = -8, .y = -8}));
    EXPECT_EQ(messageWindowColumns(battle::MessageWindow::Narrow, Language::EN), 18);
    EXPECT_EQ(messageWindowScroll(battle::MessageWindow::Narrow, Language::EN), (retropp::LayerScroll{.x = -56, .y = -8}));
}

TEST(BattleMessageWindow, TheShadingDarkensAThirtyFirstMoreEveryTwoLinesFromLine8OverTheWindow) {
    const Regions wide = MessageShading(battle::MessageWindow::Wide, Language::EN);
    ASSERT_EQ(wide.size(), 15u);
    EXPECT_EQ(wide.front().key.value, "msgsh:2");
    EXPECT_EQ(wide.back().effects[0].fill.r, CartridgeColor::expandChannel(15));
    EXPECT_EQ(leftOf(wide.front()), 8.0f);
    EXPECT_EQ(widthOf(wide.front()), 240.0f);
    const Regions narrow = MessageShading(battle::MessageWindow::Narrow, Language::EN);
    EXPECT_EQ(leftOf(narrow.front()), 56.0f);
    EXPECT_EQ(widthOf(narrow.front()), 144.0f);
}

TEST(BattleMessageText, LettersSitFourRowsIntoTheLineFromTheWindowsTextColumn) {
    const retropp::AtlasManifest glyphs = carve(256, 256, 16, retropp::ContentKind::Tileset, 256, 60);
    battle::ShownMessage wide{.window = battle::MessageWindow::Wide,
                              .glyphs = {{.glyph = 0x25, .x = 0}, {.glyph = 0x42, .x = 7, .rise = 3}}};
    const Sprites sprites = MessageText(wide, glyphs, retropp::PaletteId{1}, retropp::PaletteId{2});
    ASSERT_EQ(sprites.size(), 4u);  // a shadow and a letter each
    EXPECT_EQ(sprites[1].key.value, "msg:0");
    EXPECT_EQ(sprites[1].x, 12);  // the artwork starts four left of the pen at column 2
    EXPECT_EQ(sprites[1].y, 20);
    EXPECT_EQ(sprites[3].x, 19);
    EXPECT_EQ(sprites[3].y, 17);
    EXPECT_EQ(sprites[0].palette, retropp::PaletteId{2});
    battle::ShownMessage narrow{.window = battle::MessageWindow::Narrow, .centering = 30, .glyphs = {{.glyph = 0x25}}};
    EXPECT_EQ(MessageText(narrow, glyphs, retropp::PaletteId{1}, retropp::PaletteId{2})[1].x, 72 + 30 - 4);
}

TEST(BattleMessageText, TheClipKeepsOnlyTheLinesTiles) {
    const battle::ShownMessage narrow{.window = battle::MessageWindow::Narrow, .centering = 10};
    const Regions clip = MessageTextClip(narrow);
    ASSERT_FALSE(clip.empty());
    EXPECT_EQ(clip[0].key.value, kMessageTextClipKey);
    EXPECT_EQ(clip[0].effects[0].stencil, retropp::StencilMode::TransparentOutside);
    float left = 1e9f;
    float right = -1e9f;
    float top = 1e9f;
    float bottom = -1e9f;
    for (const retropp::Point& point : clip[0].shape.points) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    EXPECT_EQ(left, 82.0f);
    EXPECT_EQ(right - left, 22.0f * 8.0f);
    EXPECT_EQ(top, 16.0f);
    EXPECT_EQ(bottom, 32.0f);
}

// --- damage numerals -------------------------------------------------------------------------------

TEST(BattleDamageNumbers, ABattlersAnchorIsItsCenterAtItsFeetOrBottomRow) {
    const MonsterStanding monster{.x = 40, .y = 32, .size = {.width = 6, .height = 4}};
    EXPECT_EQ(numeralAnchor(monster), (NumeralAnchor{.x = 64, .y = 56}));
    const PartyStanding member{.x = 200, .y = 68};
    EXPECT_EQ(numeralAnchor(member), (NumeralAnchor{.x = 208, .y = 84}));
}

TEST(BattleDamageNumbers, DigitsStartTheOffsetLeftOfTheAnchorLiftedByTheBounce) {
    const NumeralSheet sheet{.tiles = carve(96, 8, 8, retropp::ContentKind::Tileset, 12, 70),
                             .white = retropp::PaletteId{11}, .green = retropp::PaletteId{12}};
    battle::BattlePresentationState shown;
    shown.numeralsTogether[4] = battle::ShownNumeral{.target = BattlerSlot::monster(0), .amount = 1234, .offset = 16,
                                                     .frame = 12};
    shown.numeralsTogether[1] = battle::ShownNumeral{.target = BattlerSlot::character(1), .miss = true, .heals = true,
                                                     .offset = 0, .frame = 0};
    NumeralAnchors anchors{};
    anchors[4] = NumeralAnchor{.x = 64, .y = 56};
    anchors[1] = NumeralAnchor{.x = 208, .y = 102};
    const Sprites sprites = DamageNumbers(shown, anchors, sheet);
    ASSERT_EQ(sprites.size(), 6u);
    // The miss first (battler 1), then the 1234 (battler 4).
    EXPECT_EQ(sprites[0].x, 208 + 8);
    EXPECT_EQ(sprites[0].tile, sheet.tiles[kMissCell].tile);
    EXPECT_EQ(sprites[1].tile, sheet.tiles[kMissCell + 1].tile);
    EXPECT_EQ(sprites[0].palette, sheet.green);
    EXPECT_EQ(sprites[0].y, 102 - damageNumberLift(0));
    EXPECT_EQ(xs(Sprites(sprites.begin() + 2, sprites.end())), (std::vector<int>{48, 56, 64, 72}));
    EXPECT_EQ(sprites[2].y, 56 - damageNumberLift(12));
    EXPECT_EQ(sprites[2].tile, sheet.tiles[1].tile);
    EXPECT_EQ(sprites[5].tile, sheet.tiles[4].tile);
    EXPECT_EQ(sprites[2].palette, sheet.white);
    EXPECT_EQ(sprites[2].key.value, "dmg:4:0");
}

TEST(BattleDamageNumbers, ABattlerWithNoAnchorShowsNoNumeral) {
    const NumeralSheet sheet{.tiles = carve(96, 8, 8, retropp::ContentKind::Tileset, 12, 70)};
    battle::BattlePresentationState shown;
    shown.singleNumerals[2] = battle::ShownNumeral{.target = BattlerSlot::monster(1), .amount = 5, .offset = 4};
    EXPECT_TRUE(DamageNumbers(shown, NumeralAnchors{}, sheet).empty());
    NumeralAnchors anchors{};
    anchors[5] = NumeralAnchor{.x = 10, .y = 10};
    const Sprites one = DamageNumbers(shown, anchors, sheet);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0].key.value, "dmg1:2:0");
    EXPECT_EQ(one[0].x, 6);
}

// The digits and "Miss" from the status art: 0 is "..cccc.." on its top row, and every pixel of
// the twelve tiles is see-through or one of the two numeral colors.
TEST_F(BattleHudTest, TheNumeralArtComesOutOfTheStatusArtThroughTheDecompressor) {
    ASSERT_NE(content_->routines, nullptr);
    const std::vector<std::uint8_t> status = content_->routines->decompress(assets::compressedStream(content_->statusArt));
    ASSERT_EQ(status.size(), kStatusArtBytes);
    const DamageNumeralArt art = damageNumeralArt(status);
    text::IndexedImage image = assets::blankImage(8 * 12, 8);
    for (int digit = 0; digit < 10; ++digit) {
        assets::drawTile(image, art.digit(digit), assets::TileDepth::FourPlanes, digit * 8, 0);
    }
    assets::drawTile(image, art.miss[0], assets::TileDepth::FourPlanes, 80, 0);
    assets::drawTile(image, art.miss[1], assets::TileDepth::FourPlanes, 88, 0);
    const std::vector<std::uint8_t> topOfZero{0, 0, 12, 12, 12, 12, 0, 0};
    for (int x = 0; x < 8; ++x) {
        EXPECT_EQ(image.at(x, 0), topOfZero[static_cast<std::size_t>(x)]) << x;
    }
    std::set<std::uint8_t> colors(image.pixels.begin(), image.pixels.end());
    EXPECT_EQ(colors, (std::set<std::uint8_t>{0, kNumeralOutlineColor, kNumeralFillColor}));
}

TEST(BattleDamageNumbers, TheNumeralArtNeedsTheWholeStatusArt) {
    const std::vector<std::uint8_t> short_(100);
    EXPECT_THROW((void)damageNumeralArt(short_), std::invalid_argument);
}

// --- a monster's death -------------------------------------------------------------------------

TEST(BattleDeathFade, ADyingMonsterIsAddedOverTheSceneAndDarkenedAThirtyFirstAFrame) {
    battle::DeathFade fade{.dying = battle::BattlerSet::of(BattlerSlot::monster(0)), .frame = 1};
    const Sprites monster{retropp::Sprite{.key = "mon:0", .x = 40}, retropp::Sprite{.key = "mon:0:b", .x = 48}};
    Sprites fading = MonsterDeathFade(monster, fade);
    ASSERT_EQ(fading.size(), 2u);
    for (const retropp::Sprite& sprite : fading) {
        EXPECT_EQ(sprite.blend, retropp::BlendMode::Add);
        ASSERT_EQ(sprite.regions.size(), 1u);
        EXPECT_EQ(sprite.regions[0].key.value, kDeathFadeKey);
        EXPECT_EQ(sprite.regions[0].blend, retropp::BlendMode::Subtract);
        EXPECT_EQ(sprite.regions[0].effects[0].kind, retropp::ScreenSpaceEffectKind::ColorFill);
        EXPECT_EQ(sprite.regions[0].effects[0].fill.r, 0);
    }
    EXPECT_EQ(fading[1].x, 48);
    fade.frame = 32;
    EXPECT_EQ(MonsterDeathFade(monster, fade)[0].regions[0].effects[0].fill.g, 255);
    fade.frame = 17;
    EXPECT_EQ(MonsterDeathFade(monster, fade)[0].regions[0].effects[0].fill.b, CartridgeColor::expandChannel(16));
}

}  // namespace
}  // namespace ostinato::render
