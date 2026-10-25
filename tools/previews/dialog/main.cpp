// ostinato-preview-dialog [--capture <dir>] <index>... — play field dialog messages from the
// installed cartridge.
//
// Nobody drives it. A scripted player plays every message to its end: it holds each page that
// waits for A, moves the choice arrow down once on the first page that offers a choice, then
// presses A. In a window the messages play one after another at the field rate and the window
// closes itself after the last one. With --capture nothing opens: each message is played straight
// through and the screen is written to <dir> at the moments worth looking at (see runCapture).
//
// The wiring only: the engine, the cartridge's font uploaded once, the dialog engine and its window
// advanced one field frame at a time in the field's order — the text, then its close request, then
// the window — and the screen (dialog_preview.h) assembled for each picture. Every message opens
// in a box at the top of the screen and is finished once its window has closed. Install a
// cartridge first with `ostinato-vi --install-rom <path>`; this reads the same one.

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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
#include "data/text_offsets.h"
#include "dialog_preview.h"
#include "ostinato/game_version.h"
#include "ostinato/item_id.h"
#include "ostinato/ostinato.h"
#include "text/dialog.h"
#include "text/dialog_window.h"
#include "text/font.h"
#include "text/layout.h"

namespace {

using namespace ostinato;

// --- what a message splices -------------------------------------------------------------------

// A message that names a found item or amount splices these: the first item, and the smallest
// amount any chest in the game holds. Every character's name is their default one.
constexpr ItemId kPreviewItem = ItemId::MITHRILKNIFE;
constexpr std::uint32_t kPreviewGold = 500;

// --- colors -----------------------------------------------------------------------------------

// The font color is the player's to choose; this is its default, white. The shadow is always
// black.
constexpr retropp::Rgba8 kFontColor{.r = 255, .g = 255, .b = 255};
constexpr retropp::Rgba8 kShadowColor{.r = 0, .g = 0, .b = 0};

// --- pacing -----------------------------------------------------------------------------------

// In the window: how long each waiting page stays up before A is pressed, and the empty pause
// between one message and the next.
constexpr int kWindowPageHoldFrames = 2 * text::kSecondFrames;
constexpr int kWindowMessageGapFrames = text::kSecondFrames;

// With --capture: the shortest hold that still leaves a frame each for showing the page, moving
// the arrow, showing where it went, and pressing A.
constexpr int kCapturePageHoldFrames = 4;

// The longest a message may take before the run is called stuck: ten minutes of frames, far
// beyond any message in the script.
constexpr int kMessageFrameLimit = 10 * 60 * text::kSecondFrames;

// --- the scripted player ----------------------------------------------------------------------

// What the scripted player did on one frame: the buttons it holds, and whether this frame shows
// something worth recording.
struct PlayerFrame {
    text::DialogInput input{};
    bool pageShown = false;        // a page has just finished typing and waits for A
    bool selectionMoved = false;   // the arrow has just moved to the next choice
};

// Plays the player's part in a message. On every page that waits for A it waits `holdFrames`
// frames, then presses A. On the first page with two or more choices it presses down for one
// frame halfway through the wait. Between pages it holds nothing — the engine needs A let go
// before it takes the next press.
class ScriptedPlayer {
public:
    explicit ScriptedPlayer(int holdFrames) : holdFrames_(holdFrames) {}

    PlayerFrame next(const text::DialogState& state) {
        PlayerFrame frame;
        if (!text::awaitingKeypress(state)) {
            waited_ = 0;
            return frame;
        }

        ++waited_;
        const int moveAt = holdFrames_ / 2;
        frame.pageShown = waited_ == 1;
        if (!moved_ && state.choices.count >= 2 && waited_ == moveAt) {
            frame.input.down = true;
            moved_ = true;
            movedOnFrame_ = waited_;
        } else if (movedOnFrame_ != 0 && waited_ == movedOnFrame_ + 1) {
            frame.selectionMoved = true;
        }
        if (waited_ >= holdFrames_) {
            frame.input.confirmHeld = true;
            waited_ = 0;
            movedOnFrame_ = 0;
        }
        return frame;
    }

private:
    int holdFrames_;
    int waited_ = 0;
    bool moved_ = false;
    int movedOnFrame_ = 0;
};

// --- the cartridge ----------------------------------------------------------------------------

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

text::DialogContext dialogContext(const assets::IngestedContent& content) {
    text::DialogContext context{
        .language = language(content.version) == Language::JP ? TextLanguage::JP
                                                               : TextLanguage::EN,
        .metrics = content.dialogFont.metrics,
        .corpus = &content.text,
        .item = kPreviewItem,
        .goldAmount = kPreviewGold,
    };
    for (std::size_t i = 0; i < text::kCharacterCount; ++i) {
        context.names[i] = content.text.charName(i);
    }
    return context;
}

// Upload the font's sheets and the two palettes. Pixel value kBlankPixel is a hole in both sheets,
// so only each palette's kInkPixel entry is ever drawn.
preview::DialogSheets uploadSheets(retropp::Renderer& renderer, const text::DialogFont& font) {
    const auto hole = retropp::TransparentIndices::of({text::kBlankPixel});

    const text::IndexedImage glyphs = text::buildGlyphSheet(font.glyphArt, font.glyphCount());
    preview::DialogSheets sheets{
        .glyphs = renderer.uploadAtlas(
            glyphs.pixels.data(), glyphs.width, glyphs.height,
            retropp::AssetDimensions{text::kGlyphCellPx, text::kGlyphCellPx},
            retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown,
            static_cast<int>(font.glyphCount()), hole),
    };

    if (font.cursor.loaded()) {
        const text::IndexedImage arrow = text::buildCursorSheet(font.cursor);
        sheets.arrow = renderer.uploadAtlas(
            arrow.pixels.data(), arrow.width, arrow.height,
            retropp::AssetDimensions{text::kCursorWidth, text::kCursorCellHeight},
            retropp::ContentKind::Single, retropp::ReadOrder::LeftRightThenDown, 0, hole);
    }

    std::array<retropp::Rgba8, 2> ink{};
    ink[text::kInkPixel] = kFontColor;
    std::array<retropp::Rgba8, 2> shadow{};
    shadow[text::kInkPixel] = kShadowColor;
    sheets.ink = renderer.uploadPalette(ink);
    sheets.shadow = renderer.uploadPalette(shadow);
    return sheets;
}

// --- the field frame ----------------------------------------------------------------------------

// Start message `index` with its window opening, as the event that shows a message does.
void startMessage(text::DialogState& state, text::DialogWindowState& window, std::size_t index,
                  const text::DialogContext& context) {
    text::initDialog(state);
    text::initDialogWindow(window);
    text::startDialog(state, index, context);
    text::openDialogWindow(window, text::WindowPlacement::Top, false);
}

// One field frame of the message and its window. Returns whether the message is finished: its
// window has closed.
bool stepMessage(text::DialogState& state, text::DialogWindowState& window,
                 const text::DialogInput& input, const text::DialogContext& context) {
    text::tickDialog(state, input, context);
    if (text::windowCloseRequested(state)) {
        text::closeDialogWindow(window);
    }
    text::tickDialogWindow(window);
    return text::isClosed(window);
}

// --- capture ----------------------------------------------------------------------------------

// Write a captured screen as a binary PPM, which needs no encoder: a short text header, then the
// pixels' red, green and blue bytes row by row.
bool writeCapture(const std::filesystem::path& file, std::span<const retropp::Rgba8> pixels,
                  retropp::PixelSize size) {
    std::ofstream out(file, std::ios::binary);
    out << "P6\n" << size.width << ' ' << size.height << "\n255\n";
    for (const retropp::Rgba8& pixel : pixels) {
        const std::array<char, 3> rgb{static_cast<char>(pixel.r), static_cast<char>(pixel.g),
                                      static_cast<char>(pixel.b)};
        out.write(rgb.data(), rgb.size());
    }
    return static_cast<bool>(out);
}

// Compose the screen for `state` and write it to `file`, saying which.
bool capture(retropp::Renderer& renderer, const text::DialogState& state,
             const text::DialogWindowState& window, const preview::DialogSheets& sheets,
             const std::filesystem::path& file, retropp::PixelSize screen) {
    const retropp::FrameDrawState frame{
        .layers = preview::DialogPreviewScreen(state, window, sheets)};
    if (!writeCapture(file, renderer.captureViewport(frame), screen)) {
        std::cerr << "Could not write " << file.string() << "\n";
        return false;
    }
    std::cout << "Wrote " << file.string() << "\n";
    return true;
}

// --- the two ways to run ----------------------------------------------------------------------

// Play each message straight through with no window, writing the screen to `directory` as
// dialog-<index>-page<n>.ppm when page n waits for A, dialog-<index>-page<n>-moved.ppm once the
// arrow has moved on the first page with a choice, and dialog-<index>-wipe.ppm when the first page
// wipe has taken its first half-line. The renderer composes off screen.
int runCapture(std::span<const std::size_t> indices, const assets::IngestedContent& content,
               const text::DialogContext& context, const std::filesystem::path& directory,
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
        const preview::DialogSheets sheets = uploadSheets(renderer, content.dialogFont);

        for (const std::size_t index : indices) {
            text::DialogState state;
            text::DialogWindowState window;
            startMessage(state, window, index, context);
            ScriptedPlayer player{kCapturePageHoldFrames};

            const std::string stem = "dialog-" + std::to_string(index);
            int page = 0;
            bool capturedWipe = false;
            bool finished = false;
            const auto save = [&](const std::string& name) {
                if (!capture(renderer, state, window, sheets, directory / (name + ".ppm"),
                             screen)) {
                    status = 1;
                }
            };

            for (int frame = 0; frame < kMessageFrameLimit && !finished; ++frame) {
                const PlayerFrame step = player.next(state);
                if (step.pageShown) {
                    save(stem + "-page" + std::to_string(page));
                }
                if (step.selectionMoved) {
                    save(stem + "-page" + std::to_string(page) + "-moved");
                }
                if (step.input.confirmHeld) {
                    ++page;
                }
                finished = stepMessage(state, window, step.input, context);

                if (!capturedWipe && text::halfLinesCleared(state) == 1) {
                    save(stem + "-wipe");
                    capturedWipe = true;
                }
            }

            if (!finished) {
                std::cerr << "Dialog " << index << " did not finish within " << kMessageFrameLimit
                          << " frames.\n";
                status = 1;
            } else {
                std::cout << "Dialog " << index << " finished after " << page
                          << " page(s) that waited for A.\n";
            }
        }
    }

    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    return status;
}

// Play the messages one after another in a window at the field rate, then close it.
int runWindow(std::span<const std::size_t> indices, const assets::IngestedContent& content,
              const text::DialogContext& context) {
    retropp::SteadyClock clock;
    retropp::RunLoop loop{clock};
    retropp::SdlPlatform platform;
    retropp::Renderer renderer{platform.device(), platform.sdlWindow()};
    const preview::DialogSheets sheets = uploadSheets(renderer, content.dialogFont);

    std::size_t current = 0;
    int gapFrames = 0;
    int messageFrames = 0;
    int status = 0;
    text::DialogState state;
    text::DialogWindowState window;
    ScriptedPlayer player{kWindowPageHoldFrames};

    const auto start = [&](std::size_t index) {
        startMessage(state, window, index, context);
        player = ScriptedPlayer{kWindowPageHoldFrames};
        messageFrames = 0;
        std::cout << "Dialog " << index << "\n";
    };
    start(indices[current]);

    loop.simTick([&](const retropp::InputState&) {
        if (gapFrames > 0) {
            if (--gapFrames == 0) {
                start(indices[current]);
            }
            return;
        }

        const bool finished = stepMessage(state, window, player.next(state).input, context);
        ++messageFrames;
        if (!finished && messageFrames < kMessageFrameLimit) {
            return;
        }
        if (!finished) {
            std::cerr << "Dialog " << indices[current] << " did not finish within "
                      << kMessageFrameLimit << " frames.\n";
            status = 1;
        }

        text::initDialog(state);
        text::initDialogWindow(window);
        if (++current == indices.size()) {
            loop.exitRequest();
        } else {
            gapFrames = kWindowMessageGapFrames;
        }
    });

    retropp::FrameDrawState frame;
    loop.renderLoop([&] {
        frame.layers = preview::DialogPreviewScreen(state, window, sheets);
        renderer.renderFrame(frame);
    });

    retropp::WindowedHost{loop, platform}.run();
    return status;
}

std::optional<std::size_t> parseIndex(std::string_view arg) {
    std::size_t index = 0;
    const auto [end, error] = std::from_chars(arg.data(), arg.data() + arg.size(), index);
    if (error != std::errc{} || end != arg.data() + arg.size()) {
        return std::nullopt;
    }
    return index;
}

constexpr std::string_view kUsage =
    "usage: ostinato-preview-dialog [--capture <dir>] <index>...\n";

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

    const std::size_t messages = dialogOffsets().size();
    std::vector<std::size_t> indices;
    for (int i = first; i < argc; ++i) {
        const std::optional<std::size_t> index = parseIndex(argv[i]);
        if (!index) {
            std::cerr << kUsage;
            return 1;
        }
        if (*index >= messages) {
            std::cerr << "There is no dialog " << *index << ": the script's messages are 0 to "
                      << messages - 1 << ".\n";
            return 1;
        }
        indices.push_back(*index);
    }

    // The game's own identity, so the per-user directory — and the cartridge in it — is the one the
    // game installed into. The screen and the tick are the game's: the SNES picture, one field per
    // tick.
    const retropp::EngineConfig config{
        .identity = {.organization = "OstinatoVI", .application = "OstinatoVI"},
        .window = {.title = "Ostinato VI — dialog preview"},
        .viewport = retropp::ViewportResolution::Snes,
        .timing = retropp::TimingProfile{
            static_cast<retropp::TickPeriodNs>(kFieldTickPeriod.count())},
    };
    retropp::EngineConfig::setActive(config);
    assets::configureAssetRoot(bakedProjectRoot());

    const std::optional<assets::IngestedContent> content = readCartridge();
    if (!content) {
        return 1;
    }
    const text::DialogContext context = dialogContext(*content);

    if (captureDirectory) {
        return runCapture(indices, *content, context, *captureDirectory, config.viewport.size());
    }
    return runWindow(indices, *content, context);
}
