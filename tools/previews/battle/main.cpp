// ostinato-preview-battle [--capture <dir>] [--party A,B,C] [--background <n>] <formation>... — show
// battles' standing scenes from the installed cartridge.
//
// Nobody drives it. Each formation is started as a battle — the battle lays out its monsters and
// the party as it would in the game — and its scene stands as it does after the entrance: the
// background, every monster where the formation puts it, the party in its idle pose. In a window the
// scenes show one after another for a few seconds each and the window closes itself after the last.
// With --capture nothing opens: each scene is written to <dir> as battle-<formation>-scene.ppm. The
// party is Terra, Locke and Edgar at level 1 unless --party names others; the background is the
// first field one unless --background names another.
//
// The wiring only: the engine, the cartridge read, a battle started per formation, its art decoded
// and uploaded once per background, monster and party member, and the screen (battle_preview.h)
// assembled for each picture. Install a cartridge
// first with `ostinato-vi --install-rom <path>`; this reads the same one.

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
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
#include "assets/tile_planes.h"
#include "battle/action_executor.h"
#include "battle/battle_sim.h"
#include "battle/battle_start.h"
#include "battle_preview.h"
#include "data/battle_background_art.h"
#include "data/battle_backgrounds.h"
#include "data/battle_gfx_tables.h"
#include "data/character_battle_sheet.h"
#include "data/formations.h"
#include "data/monster_image.h"
#include "data/monster_sprites.h"
#include "game/character_record.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/formation_ref.h"
#include "ostinato/ostinato.h"
#include "text/layout.h"
#include "text/small_font.h"

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

// The font color is the player's to choose; this is its default, white. The backdrop takes the first
// color of the first font palette, as the battle's does.
constexpr CartridgeColor kFontColor{0x7FFF};

// In the window: how long each scene stands, and the empty pause between one and the next.
constexpr int kWindowSceneHoldFrames = 3 * text::kSecondFrames;
constexpr int kWindowSceneGapFrames = text::kSecondFrames / 2;

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

game::Party partyOf(std::span<const CharacterPropId> actors) {
    game::Party party;
    for (std::size_t i = 0; i < actors.size(); ++i) {
        party.characters[i] = game::startingRecord(actors[i]);
        party.membership[i] = {.party = 1, .slot = static_cast<std::uint8_t>(i)};
    }
    party.currentParty = 1;
    return party;
}

// A battle of `formation` on `background`, laid out as it starts. The executor does nothing: no
// action is ever taken.
struct StandingBattle {
    battle::ActionExecutor executor;
    battle::BattleSim sim;

    StandingBattle(FormationId formation, BattleBackgroundId background, std::span<const CharacterPropId> actors,
                   const assets::IngestedContent& content)
        : sim(battle::BattleStart{.formation = FormationRef::of(formation), .background = background},
              partyOf(actors), {}, content.text, content.monsterSprites, executor) {}
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

// Pixel value 0 is a hole in every sheet, so the backdrop shows through the background and the
// battlers show only their own pixels.
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

// What has been uploaded so far. A sheet holds its room in the renderer's store until it is
// released, so each background, monster and party member is uploaded once and reused across the
// formations a run shows rather than uploaded again for every scene.
struct Uploaded {
    struct Background {
        BattleBackgroundPicture picture;
        render::BackgroundSheet sheet;
    };
    std::optional<retropp::AtlasManifest> backdrop;
    retropp::PaletteId backdropColor;
    std::map<BattleBackgroundId, Background> backgrounds;
    std::map<std::tuple<MonsterId, int, int>, preview::BattlerSheet> monsters;      // by size too
    std::map<std::pair<CharacterGfxId, std::size_t>, preview::BattlerSheet> party;  // by party slot too
};

// Everything the screen draws for `state`: the backdrop, the background decoded through the
// cartridge's decompressor, and every battler present — each uploaded the first time it is seen.
preview::BattleSheets sheetsFor(retropp::Renderer& renderer, const assets::IngestedContent& content,
                                const battle::BattleState& state, Uploaded& uploaded) {
    preview::BattleSheets sheets;

    if (!uploaded.backdrop) {
        const std::vector<std::uint8_t> solid(static_cast<std::size_t>(assets::kTilePx * assets::kTilePx), 1);
        uploaded.backdrop = renderer.uploadAtlas(solid.data(), assets::kTilePx, assets::kTilePx,
                                                 retropp::AssetDimensions{assets::kTilePx, assets::kTilePx},
                                                 retropp::ContentKind::Tileset,
                                                 retropp::ReadOrder::LeftRightThenDown, 1);
        const text::SmallFontPalettes fontPalettes = text::smallFontPalettes(content.smallFontColors, kFontColor);
        const std::array<retropp::Rgba8, 2> backdrop{retropp::Rgba8{}, fontPalettes[0][0].toRgba8()};
        uploaded.backdropColor = renderer.uploadPalette(backdrop);
    }
    sheets.backdrop = *uploaded.backdrop;
    sheets.backdropColor = uploaded.backdropColor;

    auto background = uploaded.backgrounds.find(state.background);
    if (background == uploaded.backgrounds.end()) {
        Uploaded::Background decoded{.picture = content.battleBackgrounds.load(state.background, *content.routines)};
        decoded.sheet = uploadBackground(renderer, decoded.picture);
        background = uploaded.backgrounds.emplace(state.background, std::move(decoded)).first;
    }
    sheets.picture = background->second.picture;
    sheets.background = background->second.sheet;

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
    return sheets;
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

// --- the two ways to run ----------------------------------------------------------------------

struct Run {
    std::vector<FormationId> formations;
    std::vector<CharacterPropId> party;
    BattleBackgroundId background = BattleBackgroundId::FIELD_WOB;
};

// Start each formation's battle and write its standing scene to `directory` as
// battle-<formation>-scene.ppm. The renderer composes off screen.
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
        Uploaded uploaded;
        for (const FormationId formation : run.formations) {
            const StandingBattle battle{formation, run.background, run.party, content};
            const preview::BattleSheets sheets = sheetsFor(renderer, content, battle.sim.state(), uploaded);
            const retropp::FrameDrawState frame{.layers = preview::BattleScreen(battle.sim.state(), sheets)};
            const std::filesystem::path file =
                directory / ("battle-" + std::to_string(static_cast<int>(formation)) + "-scene.ppm");
            if (!writeCapture(file, renderer.captureViewport(frame), screen)) {
                std::cerr << "Could not write " << file.string() << "\n";
                status = 1;
                continue;
            }
            std::cout << "Wrote " << file.string() << "\n";
        }
    }

    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    return status;
}

// Show each formation's scene in a window for a few seconds, then close.
int runWindow(const Run& run, const assets::IngestedContent& content) {
    retropp::SteadyClock clock;
    retropp::RunLoop loop{clock};
    retropp::SdlPlatform platform;
    retropp::Renderer renderer{platform.device(), platform.sdlWindow()};

    std::size_t current = 0;
    int heldFrames = 0;
    int gapFrames = 0;
    std::optional<StandingBattle> battle;
    preview::BattleSheets sheets;
    Uploaded uploaded;

    const auto start = [&](FormationId formation) {
        battle.emplace(formation, run.background, run.party, content);
        sheets = sheetsFor(renderer, content, battle->sim.state(), uploaded);
        heldFrames = 0;
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
        if (++heldFrames < kWindowSceneHoldFrames) {
            return;
        }
        if (++current == run.formations.size()) {
            loop.exitRequest();
        } else {
            battle.reset();
            gapFrames = kWindowSceneGapFrames;
        }
    });

    retropp::FrameDrawState frame;
    loop.renderLoop([&] {
        frame.layers = battle ? preview::BattleScreen(battle->sim.state(), sheets) : render::Layers{};
        renderer.renderFrame(frame);
    });

    retropp::WindowedHost{loop, platform}.run();
    return 0;
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
    "usage: ostinato-preview-battle [--capture <dir>] [--party A,B,C] [--background <n>] <formation>...\n";

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
