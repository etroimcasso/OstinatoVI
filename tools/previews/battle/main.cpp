// ostinato-preview-battle [--capture <dir>] [--party A,B,C] [--background <n>] [--message-speed <n>]
// <formation>... — fight battles from the installed cartridge and show their picture.
//
// Nobody drives it. Each formation is started as a battle and fought to its end: a scripted player
// answers every battle menu with Fight at the first monster still standing and confirms a message
// that waits for a key once it has been up for the message time; every hit takes a fixed amount of
// HP (preview_damage.h). In a window the battles play one after another with a second's pause
// between them and the window closes itself after the last. With --capture nothing opens: for each
// formation the moments worth checking are written to <dir> as battle-<formation>-<moment>.ppm —
// `scene` (the battle's first frame), `name` and `message` (the first name and the first message
// once their text is all up), `hit` (the first numeral at the top of its bounce), `hp` (the first
// frame a party member's HP has changed), `death` (a monster halfway through fading out) and `end`
// (the frame the scene closes) — each written the first time it happens, so a battle without a
// name or a message writes none. A battle that runs ten minutes of frames without ending fails the
// run.
//
// The party is Terra, Locke and Edgar at level 1 unless --party names others; the background is
// the first field one unless --background names another; the message speed is the new game's
// unless --message-speed names another, 0 (fastest) to 5.
//
// The wiring only: the engine, the cartridge read, a battle per formation with its presentation,
// the art decoded and uploaded once per background, monster and party member, and the screen
// (battle_preview.h) assembled each frame. Install a cartridge first with
// `ostinato-vi --install-rom <path>`; this reads the same one.

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "retropp/clock.h"
#include "retropp/draw_state.h"
#include "retropp/engine_config.h"
#include "retropp/image.h"
#include "retropp/input.h"
#include "retropp/palette.h"
#include "retropp/renderer.h"
#include "retropp/run_loop.h"
#include "retropp/sdl_platform.h"
#include "retropp/timing.h"
#include "retropp/viewport.h"
#include "retropp/windowed_host.h"

#include "assets/asset_root.h"
#include "assets/cartridge.h"
#include "assets/cartridge_routines.h"
#include "assets/tile_planes.h"
#include "battle/battle_sim.h"
#include "battle/battle_start.h"
#include "battle/command_executor.h"
#include "battle/presentation.h"
#include "battle_preview.h"
#include "data/battle_background_art.h"
#include "data/battle_backgrounds.h"
#include "data/battle_gfx_tables.h"
#include "data/character_battle_sheet.h"
#include "data/damage_numerals.h"
#include "data/formations.h"
#include "data/monster_image.h"
#include "data/monster_sprites.h"
#include "game/character_record.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/formation_ref.h"
#include "ostinato/ostinato.h"
#include "ostinato/window_tile.h"
#include "ostinato/wallpaper_id.h"
#include "preview_damage.h"
#include "render/battle/layout.h"
#include "render/screen_border.h"
#include "scripted_player.h"
#include "text/font.h"
#include "text/layout.h"
#include "text/small_font.h"
#include "text/window_art.h"

namespace {

using namespace ostinato;

// --- what the run shows -------------------------------------------------------------------------

// The party unless --party says otherwise.
constexpr std::array<CharacterPropId, 3> kDefaultParty{CharacterPropId::TERRA, CharacterPropId::LOCKE,
                                                       CharacterPropId::EDGAR};

// The characters --party may name.
constexpr std::array<std::pair<std::string_view, CharacterPropId>, 16> kPartyNames{{
    {"TERRA", CharacterPropId::TERRA},   {"LOCKE", CharacterPropId::LOCKE},   {"CYAN", CharacterPropId::CYAN},
    {"SHADOW", CharacterPropId::SHADOW}, {"EDGAR", CharacterPropId::EDGAR},   {"SABIN", CharacterPropId::SABIN},
    {"CELES", CharacterPropId::CELES},   {"STRAGO", CharacterPropId::STRAGO}, {"RELM", CharacterPropId::RELM},
    {"SETZER", CharacterPropId::SETZER}, {"MOG", CharacterPropId::MOG},       {"GAU", CharacterPropId::GAU},
    {"GOGO", CharacterPropId::GOGO},     {"UMARO", CharacterPropId::UMARO},   {"BANON", CharacterPropId::BANON},
    {"LEO", CharacterPropId::LEO},
}};

// The font color and the window's wallpaper are the player's to choose; these are a new game's.
// The dialog font's shadow is always black.
constexpr CartridgeColor kFontColor{0x7FFF};
constexpr retropp::Rgba8 kShadowColor{.r = 0, .g = 0, .b = 0};
constexpr WallpaperId kPreviewWallpaper = WallpaperId::WALLPAPER_0;

// In the window: the empty pause between one battle and the next.
constexpr int kWindowBattleGapFrames = text::kSecondFrames;

// The longest a battle may run before the run is called stuck: ten minutes of frames.
constexpr int kBattleFrameLimit = 10 * 60 * text::kSecondFrames;

// The numeral's frame at the top of its bounce, and the death fade's halfway frame: the moments
// the hit and death captures show.
constexpr int kNumeralPeakFrame = 12;
constexpr int kDeathCaptureFrame = 16;

// A background's tiles are laid out sixteen to a row for upload.
constexpr int kBackgroundSheetColumns = 16;

// --- the cartridge -------------------------------------------------------------------------------

std::optional<std::filesystem::path> bakedProjectRoot() {
#ifdef OSTINATO_PROJECT_ROOT
    return std::filesystem::path{OSTINATO_PROJECT_ROOT};
#else
    return std::nullopt;
#endif
}

std::optional<assets::IngestedContent> readCartridge() {
    try {
        return assets::ingestCartridge();
    } catch (const std::exception& error) {
        std::cerr << "Could not read the cartridge: " << error.what()
                  << "\nInstall one with `ostinato-vi --install-rom <path>`.\n";
        return std::nullopt;
    }
}

// --- the battle ----------------------------------------------------------------------------------

game::Party partyOf(std::span<const CharacterPropId> actors, const TextCorpus& text) {
    game::Party party;
    for (std::size_t i = 0; i < actors.size(); ++i) {
        game::CharacterRecord& record = party.characters[i];
        record = game::startingRecord(actors[i]);
        const std::span<const std::uint8_t> name = text.charName(static_cast<std::size_t>(actors[i]));
        std::copy_n(name.begin(), std::min(name.size(), record.name.size()), record.name.begin());
        party.membership[i] = {.party = 1, .slot = static_cast<std::uint8_t>(i)};
    }
    party.currentParty = 1;
    return party;
}

// One battle being fought, frame by frame: the battle, its presentation, the stand-in damage and the
// scripted player.
struct Fight {
    preview::PreviewDamage hooks;
    battle::CommandExecutor executor;
    battle::BattleSim sim;
    battle::BattlePresentationState shown;
    preview::ScriptedPlayer player;
    std::vector<battle::PlayerAction> answers;
    int frames = 0;

    Fight(FormationId formation, BattleBackgroundId background, std::span<const CharacterPropId> actors,
          const assets::IngestedContent& content, std::uint8_t messageSpeed)
        : executor(hooks, content.text, content.monsterSprites),
          sim(battle::BattleStart{.formation = FormationRef::of(formation), .background = background},
              partyOf(actors, content.text), {}, content.text, content.monsterSprites, executor),
          player(messageHoldFrames(messageSpeed)) {}

    void tick(const battle::BattlePresentationContext& context) {
        battle::BattleInput input = battle::battleInput(shown);
        input.playerActions = std::exchange(answers, {});
        sim.tick(input);
        const std::vector<battle::BattleRequest> requests = sim.takeRequests();
        answers = player.answer(requests, sim.state());
        const bool confirm = player.confirms(shown);
        battle::tickBattlePresentation(shown, sim.state(), requests, context, confirm);
        ++frames;
    }

    bool over() const { return shown.sceneClosed; }
};

// --- uploading the art ---------------------------------------------------------------------------

retropp::PaletteId uploadColors(retropp::Renderer& renderer, std::span<const CartridgeColor> colors) {
    std::vector<retropp::Rgba8> rgba;
    rgba.reserve(colors.size());
    for (const CartridgeColor& color : colors) {
        rgba.push_back(color.toRgba8());
    }
    return renderer.uploadPalette(rgba);
}

std::vector<CartridgeColor> colorsOf(std::span<const std::uint8_t> bytes) {
    std::vector<CartridgeColor> colors;
    for (std::size_t at = 0; at + 1 < bytes.size(); at += 2) {
        colors.push_back(colorAt(bytes.data(), static_cast<unsigned>(at)));
    }
    return colors;
}

// Pixel value 0 is a hole in every sheet, so the backdrop shows through the background and the
// battlers and letters show only their own pixels.
const auto kHole = retropp::TransparentIndices::of({0});

// The background's tiles as one sheet, sixteen tiles a row, with one blank tile after them.
render::BackgroundSheet uploadBackground(retropp::Renderer& renderer, const BattleBackgroundPicture& picture) {
    const auto tiles = static_cast<int>(picture.tileCount());
    const int rows = (tiles + 1 + kBackgroundSheetColumns - 1) / kBackgroundSheetColumns;
    text::IndexedImage sheet = assets::blankImage(kBackgroundSheetColumns * assets::kTilePx, rows * assets::kTilePx);
    for (int tile = 0; tile < tiles; ++tile) {
        const std::span<const std::uint8_t> bytes{picture.tiles.data() + tile * kBackgroundTileBytes,
                                                  kBackgroundTileBytes};
        assets::drawTile(sheet, bytes, assets::TileDepth::FourPlanes, tile % kBackgroundSheetColumns * assets::kTilePx,
                         tile / kBackgroundSheetColumns * assets::kTilePx);
    }
    render::BackgroundSheet out{
        .tiles = renderer.uploadAtlas(sheet.pixels.data(), sheet.width, sheet.height,
                                      retropp::AssetDimensions{assets::kTilePx, assets::kTilePx},
                                      retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown, tiles + 1,
                                      kHole),
        .blankTile = static_cast<std::uint16_t>(tiles),
    };
    for (std::size_t palette = 0; palette < kBackgroundPalettes; ++palette) {
        out.palettes[palette] = uploadColors(
            renderer, std::span<const CartridgeColor>{picture.colors}.subspan(palette * kBackgroundPaletteColors,
                                                                               kBackgroundPaletteColors));
    }
    return out;
}

preview::BattlerSheet uploadMonster(retropp::Renderer& renderer, const assets::IngestedContent& content,
                                    MonsterId monster, MonsterSpriteSize size) {
    const MonsterImage image = content.monsterImages.image(monster, size);
    return preview::BattlerSheet{
        .image = renderer.uploadAtlas(image.pixels.pixels.data(), image.pixels.width, image.pixels.height,
                                      retropp::AssetDimensions{image.pixels.width, image.pixels.height},
                                      retropp::ContentKind::Single, retropp::ReadOrder::LeftRightThenDown, 1, kHole),
        .size = size,
        .palette = uploadColors(renderer, image.colors),
    };
}

preview::BattlerSheet uploadCharacter(retropp::Renderer& renderer, const assets::IngestedContent& content,
                                      CharacterGfxId graphics, std::size_t partySlot) {
    const CharacterBattleSheet sheet = content.characterSheets.sheet(graphics);
    return preview::BattlerSheet{
        .sheet = {
            .standing = renderer.uploadAtlas(sheet.standing.pixels.data(), sheet.standing.width, sheet.standing.height,
                                             retropp::AssetDimensions{kCharacterFrameWidthPx, kCharacterFrameHeightPx},
                                             retropp::ContentKind::SpriteSeries, retropp::ReadOrder::LeftRightThenDown,
                                             static_cast<int>(kCharacterFrameCount), kHole),
            .fallen = renderer.uploadAtlas(sheet.fallen.pixels.data(), sheet.fallen.width, sheet.fallen.height,
                                           retropp::AssetDimensions{kFallenFrameWidthPx, kFallenFrameHeightPx},
                                           retropp::ContentKind::Single, retropp::ReadOrder::LeftRightThenDown, 1,
                                           kHole),
        },
        .palette = uploadColors(renderer, characterColors(content.characterSheets, graphics, partySlot)),
    };
}

// A numeral palette: sixteen colors, the last four a party slot's status colors.
retropp::PaletteId uploadNumeralColors(retropp::Renderer& renderer, std::size_t slot) {
    std::vector<CartridgeColor> colors(kNumeralOutlineColor);
    const std::span<const CartridgeColor, kStatusColorCount> status = statusColors(slot);
    colors.insert(colors.end(), status.begin(), status.end());
    return uploadColors(renderer, colors);
}

// Everything the screen draws that does not change from battle to battle: the backdrop, the death
// colors, the small font and its palettes, the windows' wallpaper and colors, the dialog font and
// the numerals.
preview::BattleSheets uploadFixedSheets(retropp::Renderer& renderer, const assets::IngestedContent& content) {
    preview::BattleSheets sheets;
    const text::SmallFontPalettes fontPalettes = text::smallFontPalettes(content.smallFontColors, kFontColor);

    const std::vector<std::uint8_t> solid(static_cast<std::size_t>(assets::kTilePx * assets::kTilePx), 1);
    sheets.backdrop = renderer.uploadAtlas(solid.data(), assets::kTilePx, assets::kTilePx,
                                           retropp::AssetDimensions{assets::kTilePx, assets::kTilePx},
                                           retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown, 1);
    const std::array<retropp::Rgba8, 2> backdrop{retropp::Rgba8{}, fontPalettes[0][0].toRgba8()};
    sheets.backdropColor = renderer.uploadPalette(backdrop);

    sheets.deathColors = uploadColors(renderer, colorsOf(content.monsterDeathColors));

    const text::IndexedImage font = text::buildSmallFontSheet(content.smallFont);
    sheets.smallFont = renderer.uploadAtlas(font.pixels.data(), font.width, font.height,
                                            retropp::AssetDimensions{text::kSmallGlyphPx, text::kSmallGlyphPx},
                                            retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown,
                                            static_cast<int>(text::kSmallFontGlyphCount), kHole);
    for (std::size_t palette = 0; palette < fontPalettes.size(); ++palette) {
        sheets.fontPalettes[palette] = uploadColors(renderer, fontPalettes[palette]);
    }

    const text::IndexedImage wallpaper = text::buildWindowSheet(content.windowArt, kPreviewWallpaper);
    sheets.wallpaper = renderer.uploadAtlas(wallpaper.pixels.data(), wallpaper.width, wallpaper.height,
                                            retropp::AssetDimensions{text::kWindowTilePx, text::kWindowTilePx},
                                            retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown,
                                            static_cast<int>(kWindowTileCount));
    const text::WindowColors colors =
        text::battleWindowColors(text::defaultWindowColors(content.windowArt, kPreviewWallpaper));
    std::array<retropp::Rgba8, text::kWindowColorCount + 1> windowPalette{};
    for (std::size_t i = 0; i < colors.size(); ++i) {
        windowPalette[i + 1] = colors[i];
    }
    sheets.windowColors = renderer.uploadPalette(windowPalette);

    const text::DialogFont& dialog = content.dialogFont;
    const auto glyphHole = retropp::TransparentIndices::of({text::kBlankPixel});
    const text::IndexedImage glyphs = text::buildGlyphSheet(dialog.glyphArt, dialog.glyphCount());
    sheets.glyphs = renderer.uploadAtlas(glyphs.pixels.data(), glyphs.width, glyphs.height,
                                         retropp::AssetDimensions{text::kGlyphCellPx, text::kGlyphCellPx},
                                         retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown,
                                         static_cast<int>(dialog.glyphCount()), glyphHole);
    std::array<retropp::Rgba8, 2> ink{};
    ink[text::kInkPixel] = kFontColor.toRgba8();
    std::array<retropp::Rgba8, 2> shadow{};
    shadow[text::kInkPixel] = kShadowColor;
    sheets.ink = renderer.uploadPalette(ink);
    sheets.shadow = renderer.uploadPalette(shadow);

    const DamageNumeralArt numerals =
        damageNumeralArt(content.routines->decompress(assets::compressedStream(content.statusArt)));
    constexpr int kNumeralCells = static_cast<int>(kNumeralDigits + kMissTiles);
    text::IndexedImage numeralSheet = assets::blankImage(kNumeralCells * assets::kTilePx, assets::kTilePx);
    for (int digit = 0; digit < static_cast<int>(kNumeralDigits); ++digit) {
        assets::drawTile(numeralSheet, numerals.digit(digit), assets::TileDepth::FourPlanes, digit * assets::kTilePx, 0);
    }
    for (std::size_t tile = 0; tile < kMissTiles; ++tile) {
        assets::drawTile(numeralSheet, numerals.miss[tile], assets::TileDepth::FourPlanes,
                         static_cast<int>(render::kMissCell + tile) * assets::kTilePx, 0);
    }
    sheets.numerals = render::NumeralSheet{
        .tiles = renderer.uploadAtlas(numeralSheet.pixels.data(), numeralSheet.width, numeralSheet.height,
                                      retropp::AssetDimensions{assets::kTilePx, assets::kTilePx},
                                      retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown,
                                      kNumeralCells, kHole),
        .white = uploadNumeralColors(renderer, 0),
        .green = uploadNumeralColors(renderer, 1),
    };
    return sheets;
}

// What has been uploaded so far. A sheet holds its room in the renderer's store until it is
// released, so each background, monster and party member is uploaded once and reused across the
// formations a run shows rather than uploaded again for every battle.
struct Uploaded {
    struct Background {
        BattleBackgroundPicture picture;
        render::BackgroundSheet sheet;
    };
    std::map<BattleBackgroundId, Background> backgrounds;
    std::map<std::tuple<MonsterId, int, int>, preview::BattlerSheet> monsters;      // by size too
    std::map<std::pair<CharacterGfxId, std::size_t>, preview::BattlerSheet> party;  // by party slot too
};

// The battle's own sheets for `state` on top of the fixed ones: the background decoded through the
// cartridge's decompressor, and every battler present — each uploaded the first time it is seen.
void addBattleSheets(preview::BattleSheets& sheets, retropp::Renderer& renderer, const assets::IngestedContent& content,
                     const battle::BattleState& state, Uploaded& uploaded) {
    auto background = uploaded.backgrounds.find(state.background);
    if (background == uploaded.backgrounds.end()) {
        Uploaded::Background decoded{.picture = content.battleBackgrounds.load(state.background, *content.routines)};
        decoded.sheet = uploadBackground(renderer, decoded.picture);
        background = uploaded.backgrounds.emplace(state.background, std::move(decoded)).first;
    }
    sheets.picture = background->second.picture;
    sheets.background = background->second.sheet;

    sheets.battlers = {};
    const Formation& formation = getFormation(state.formation);
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        const battle::Battler& battler = state.at(slot);
        if (!battler.present) {
            continue;
        }
        if (slot.isMonster()) {
            const std::optional<MonsterSpriteSize> box =
                monsterSpriteBox(formation.vramMap(), static_cast<std::size_t>(slot.position()));
            if (!battler.monster || !box) {
                continue;
            }
            const MonsterSpriteSize size = content.monsterSprites.size(*battler.monster, *box);
            const auto key = std::make_tuple(*battler.monster, size.width, size.height);
            auto found = uploaded.monsters.find(key);
            if (found == uploaded.monsters.end()) {
                found = uploaded.monsters.emplace(key, uploadMonster(renderer, content, *battler.monster, size)).first;
            }
            sheets.battlers[static_cast<std::size_t>(index)] = found->second;
        } else {
            const auto position = static_cast<std::size_t>(slot.position());
            const auto key = std::make_pair(battler.graphics, position);
            auto found = uploaded.party.find(key);
            if (found == uploaded.party.end()) {
                found = uploaded.party.emplace(key, uploadCharacter(renderer, content, battler.graphics, position)).first;
            }
            sheets.battlers[static_cast<std::size_t>(index)] = found->second;
        }
    }
}

// --- capture ----------------------------------------------------------------------------------

// Write a captured screen as a binary PPM, which needs no encoder: a short text header, then the
// pixels' red, green and blue bytes row by row.
bool writeCapture(const std::filesystem::path& file, std::span<const retropp::Rgba8> pixels, retropp::PixelSize size) {
    std::ofstream out(file, std::ios::binary);
    out << "P6\n" << size.width << ' ' << size.height << "\n255\n";
    for (const retropp::Rgba8& pixel : pixels) {
        const std::array<char, 3> rgb{static_cast<char>(pixel.r), static_cast<char>(pixel.g),
                                      static_cast<char>(pixel.b)};
        out.write(rgb.data(), rgb.size());
    }
    return static_cast<bool>(out);
}

// Which of the moments worth checking this frame of `fight` is, the first time each happens.
std::vector<std::string_view> momentsOf(const Fight& fight, const battle::BattleState& start,
                                        std::set<std::string_view>& seen) {
    std::vector<std::string_view> moments;
    const auto mark = [&](std::string_view moment, bool now) {
        if (now && seen.insert(moment).second) {
            moments.push_back(moment);
        }
    };
    const battle::BattlePresentationState& shown = fight.shown;
    const battle::BattleState& state = fight.sim.state();
    mark("scene", shown.sceneOpen);
    if (shown.message) {
        const bool allUp = shown.message->textDone || shown.message->awaitingKey;
        mark(shown.message->window == battle::MessageWindow::Narrow ? "name" : "message", allUp);
    }
    const auto atPeak = [](const std::optional<battle::ShownNumeral>& n) { return n && n->frame == kNumeralPeakFrame; };
    mark("hit", std::any_of(shown.numeralsTogether.begin(), shown.numeralsTogether.end(), atPeak) ||
                    std::any_of(shown.singleNumerals.begin(), shown.singleNumerals.end(), atPeak));
    bool hpChanged = false;
    for (int slot = 0; slot < battle::kPartySlots; ++slot) {
        const battle::BattlerSlot member = battle::BattlerSlot::character(slot);
        hpChanged = hpChanged || (state.at(member).present && state.at(member).hp != start.at(member).hp);
    }
    mark("hp", hpChanged);
    mark("death", shown.deathFade && shown.deathFade->frame == kDeathCaptureFrame);
    mark("end", fight.over());
    return moments;
}

// --- the two ways to run ----------------------------------------------------------------------

struct Run {
    std::vector<FormationId> formations;
    std::vector<CharacterPropId> party;
    BattleBackgroundId background = BattleBackgroundId::FIELD_WOB;
    std::uint8_t messageSpeed = battle::kNewGameMessageSpeed;
};

battle::BattlePresentationContext contextFor(const Run& run, const assets::IngestedContent& content) {
    return {.text = content.text, .metrics = content.dialogFont.metrics, .messageSpeed = run.messageSpeed};
}

// Fight each formation to its end and write its moments to `directory`. The renderer composes off
// screen.
int runCapture(const Run& run, const assets::IngestedContent& content, const std::filesystem::path& directory,
               retropp::PixelSize screen) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "Could not start SDL video: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_GPUDevice* device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB,
        /*debug_mode=*/false, /*name=*/nullptr);
    if (device == nullptr) {
        std::cerr << "Could not create a GPU device: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    int status = 0;
    {
        retropp::Renderer renderer{device, nullptr};
        preview::BattleSheets sheets = uploadFixedSheets(renderer, content);
        Uploaded uploaded;
        const battle::BattlePresentationContext context = contextFor(run, content);
        for (const FormationId formation : run.formations) {
            Fight fight{formation, run.background, run.party, content, run.messageSpeed};
            const battle::BattleState start = fight.sim.state();
            addBattleSheets(sheets, renderer, content, fight.sim.state(), uploaded);
            std::set<std::string_view> seen;
            const std::string stem = "battle-" + std::to_string(static_cast<int>(formation)) + "-";
            while (!fight.over() && fight.frames < kBattleFrameLimit) {
                fight.tick(context);
                for (const std::string_view moment : momentsOf(fight, start, seen)) {
                    const retropp::FrameDrawState frame{
                        .layers = preview::BattleScreen(fight.sim.state(), fight.shown, content.text, sheets),
                        .regions = render::ScreenBorder(render::kBattleTopBlankRows, render::kBattleBottomBlankRows)};
                    const std::filesystem::path file = directory / (stem + std::string{moment} + ".ppm");
                    if (!writeCapture(file, renderer.captureViewport(frame), screen)) {
                        std::cerr << "Could not write " << file.string() << "\n";
                        status = 1;
                        continue;
                    }
                    std::cout << "Wrote " << file.string() << " (frame " << fight.frames << ")\n";
                }
            }
            if (!fight.over()) {
                std::cerr << "Formation " << static_cast<int>(formation) << " did not end in ten minutes of frames.\n";
                status = 1;
            }
        }
    }

    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    return status;
}

// Fight each formation in a window, one after another, then close.
int runWindow(const Run& run, const assets::IngestedContent& content) {
    retropp::SteadyClock clock;
    retropp::RunLoop loop{clock};
    retropp::SdlPlatform platform;
    retropp::Renderer renderer{platform.device(), platform.sdlWindow()};

    const battle::BattlePresentationContext context = contextFor(run, content);
    preview::BattleSheets sheets = uploadFixedSheets(renderer, content);
    Uploaded uploaded;
    std::size_t current = 0;
    int gapFrames = 0;
    int status = 0;
    std::unique_ptr<Fight> fight;

    const auto start = [&](FormationId formation) {
        fight = std::make_unique<Fight>(formation, run.background, run.party, content, run.messageSpeed);
        addBattleSheets(sheets, renderer, content, fight->sim.state(), uploaded);
        std::cout << "Formation " << static_cast<int>(formation) << "\n";
    };
    start(run.formations[current]);

    loop.simTick([&](const retropp::InputState&) {
        if (gapFrames > 0) {
            if (--gapFrames == 0) {
                start(run.formations[current]);
            }
            return;
        }
        fight->tick(context);
        if (!fight->over() && fight->frames < kBattleFrameLimit) {
            return;
        }
        if (!fight->over()) {
            std::cerr << "Formation " << static_cast<int>(run.formations[current])
                      << " did not end in ten minutes of frames.\n";
            status = 1;
        }
        if (++current == run.formations.size()) {
            loop.exitRequest();
        } else {
            fight.reset();
            gapFrames = kWindowBattleGapFrames;
        }
    });

    retropp::FrameDrawState frame{
        .regions = render::ScreenBorder(render::kBattleTopBlankRows, render::kBattleBottomBlankRows)};
    loop.renderLoop([&] {
        frame.layers = fight ? preview::BattleScreen(fight->sim.state(), fight->shown, content.text, sheets)
                             : render::Layers{};
        renderer.renderFrame(frame);
    });

    retropp::WindowedHost{loop, platform}.run();
    return status;
}

// --- the command line ---------------------------------------------------------------------------

std::optional<std::size_t> parseIndex(std::string_view arg) {
    std::size_t index = 0;
    const auto [end, error] = std::from_chars(arg.data(), arg.data() + arg.size(), index);
    if (error != std::errc{} || end != arg.data() + arg.size()) {
        return std::nullopt;
    }
    return index;
}

std::optional<std::vector<CharacterPropId>> parseParty(std::string_view arg) {
    std::vector<CharacterPropId> party;
    while (!arg.empty()) {
        const std::size_t comma = arg.find(',');
        const std::string_view name = arg.substr(0, comma);
        arg = comma == std::string_view::npos ? std::string_view{} : arg.substr(comma + 1);
        bool found = false;
        for (const auto& [known, actor] : kPartyNames) {
            if (name == known) {
                party.push_back(actor);
                found = true;
            }
        }
        if (!found) {
            std::cerr << "No character is called " << name << ".\n";
            return std::nullopt;
        }
    }
    if (party.empty() || party.size() > battle::kPartySlots) {
        std::cerr << "--party names one to four characters.\n";
        return std::nullopt;
    }
    return party;
}

constexpr std::string_view kUsage =
    "usage: ostinato-preview-battle [--capture <dir>] [--party A,B,C] [--background <n>] [--message-speed <n>] "
    "<formation>...\n";

}  // namespace

int main(int argc, char** argv) {
    int first = 1;
    std::optional<std::filesystem::path> captureDirectory;
    if (argc > 2 && std::string_view{argv[1]} == "--capture") {
        captureDirectory = std::filesystem::path{argv[2]};
        first = 3;
    }
    if (first >= argc) {
        std::cerr << kUsage;
        return 1;
    }

    Run run;
    run.party.assign(kDefaultParty.begin(), kDefaultParty.end());
    for (int i = first; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--party" && i + 1 < argc) {
            const std::optional<std::vector<CharacterPropId>> party = parseParty(argv[++i]);
            if (!party) {
                return 1;
            }
            run.party = *party;
            continue;
        }
        if (arg == "--background" && i + 1 < argc) {
            const std::optional<std::size_t> background = parseIndex(argv[++i]);
            if (!background || *background >= kBattleBackgroundCount) {
                std::cerr << "There is no background " << argv[i] << ": they are 0 to " << kBattleBackgroundCount - 1
                          << ".\n";
                return 1;
            }
            run.background = static_cast<BattleBackgroundId>(*background);
            continue;
        }
        if (arg == "--message-speed" && i + 1 < argc) {
            const std::optional<std::size_t> speed = parseIndex(argv[++i]);
            if (!speed || *speed > battle::kSlowestMessageSpeed) {
                std::cerr << "The message speed is 0 (fastest) to " << int{battle::kSlowestMessageSpeed} << ".\n";
                return 1;
            }
            run.messageSpeed = static_cast<std::uint8_t>(*speed);
            continue;
        }
        const std::optional<std::size_t> index = parseIndex(arg);
        if (!index) {
            std::cerr << kUsage;
            return 1;
        }
        if (*index >= formations().size()) {
            std::cerr << "There is no formation " << *index << ": they are 0 to " << formations().size() - 1 << ".\n";
            return 1;
        }
        run.formations.push_back(static_cast<FormationId>(*index));
    }
    if (run.formations.empty()) {
        std::cerr << kUsage;
        return 1;
    }

    // The game's own identity, so the per-user directory — and the cartridge in it — is the one the
    // game installed into. The screen and the tick are the game's: the SNES picture, one field per
    // tick.
    const retropp::EngineConfig config{
        .identity = {.organization = "OstinatoVI", .application = "OstinatoVI"},
        .window = {.title = "Ostinato VI — battle preview"},
        .viewport = retropp::ViewportResolution::Snes,
        .timing = retropp::TimingProfile{static_cast<retropp::TickPeriodNs>(kFieldTickPeriod.count())},
    };
    retropp::EngineConfig::setActive(config);
    assets::configureAssetRoot(bakedProjectRoot());

    const std::optional<assets::IngestedContent> content = readCartridge();
    if (!content) {
        return 1;
    }
    if (content->routines == nullptr) {
        std::cerr << "This cartridge's decompressor has no known place, so its backgrounds cannot be decoded.\n";
        return 1;
    }

    if (captureDirectory) {
        return runCapture(run, *content, *captureDirectory, config.viewport.size());
    }
    return runWindow(run, *content);
}
