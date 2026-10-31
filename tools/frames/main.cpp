// ostinato-frames — the cartridge run on the engine's SNES machine, driven by whoever holds the pad,
// with its frames written out 1:1 from the moments the triggers named on the command line mark. A
// developer tool for measuring the picture the console draws; it is not part of the game.
//
//     ostinato-frames <rom> <directory> [--battles] [--gameloaded] [--fieldmenu]
//
// At least one trigger is named; both may be. Each is an escape on the cartridge's own code:
//
//   --battles     every battle, from the battle program's entry (`Battle_ext`, $C2:0000 — the
//                 field's `jsl` lands there) to the `rtl` that returns to the field
//                 (`TerminateBattle`, $C2:00CB). Written as `battle<n>-f<offset>.ppm`, each with a
//                 line in `frames.log` naming the frame and the scroll words the battle keeps in work
//                 RAM at that moment; the battle's last frame is written with `-end`.
//   --gameloaded  everything from the moment the field is set up from a loaded save
//                 (`InitSavedGame`, $C0:BD20 — reached from the title screen's Continue and from the
//                 in-game restore, never from the load screen's slot previews) until the window is
//                 closed. Written as `game<n>-f<offset>.ppm`, each with a line in `frames.log`.
//   --fieldmenu   every opening of the field's main menu (X on the field), from the moment it is
//                 opened ($C0:C636, in `OpenMainMenu`) to its return to the field ($C0:C651).
//                 Written as `menu<n>-f<offset>.ppm`, each with a line in `frames.log`; the menu's
//                 last frame is written with `-end`.
//
// The addresses are the US cartridges'. The machine's picture is shown in a window at 3×, exactly
// as it is drawn; every fifteenth frame after a trigger fires is written under the directory as a
// 256 × 224 PPM at one byte per channel. Every armed escape is announced as it fires — its key, its
// address and the frame — and the start of each battle, loaded game and menu, and the end of each
// battle and menu, are logged and printed. Nothing is asked of the player.
//
// Keys: arrows = d-pad, X = A, Z = B, S = X, A = Y, Q/W = L/R, Enter = Start, Right Shift = Select.
// Close the window to quit.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include "retropp/clock.h"
#include "retropp/draw_state.h"
#include "retropp/engine_config.h"
#include "retropp/geometry.h"
#include "retropp/guest_escape.h"
#include "retropp/input.h"
#include "retropp/input_actions.h"
#include "retropp/memory_region.h"
#include "retropp/raster_content.h"
#include "retropp/renderer.h"
#include "retropp/run_loop.h"
#include "retropp/sdl_platform.h"
#include "retropp/snes.h"
#include "retropp/timing.h"
#include "retropp/viewport.h"
#include "retropp/vm.h"
#include "retropp/windowed_host.h"

namespace {

using namespace retropp;

// The battle program's entry — `Battle_ext: jmp BattleMain` — and the `rtl` of `TerminateBattle`
// that hands the field back control once the battle has faded out (battle/battle_main.asm:39-40,
// 150-154; the program sits at $C2:0000).
constexpr std::uint32_t kBattleEntry  = 0xC20000;
constexpr std::uint32_t kBattleReturn = 0xC200CB;

// `InitSavedGame`, which copies a loaded save's party into the field's objects and asks for its map
// (field/init.asm:26-72; the US cartridges place it at $C0:BD20).
constexpr std::uint32_t kSavedGameLoaded = 0xC0BD20;

// The field's main menu, inside `OpenMainMenu` (field/menu.asm:220-242): the `stz $59` reached only
// once every check to open it has passed, and the `lda $0205` the `jsr OpenMenu` returns to when the
// menu closes. Shops, name entry and the load screen open the menu program without passing either.
constexpr std::uint32_t kFieldMenuOpened = 0xC0C636;
constexpr std::uint32_t kFieldMenuClosed = 0xC0C651;

// How many frames apart the frames are written.
constexpr std::uint64_t kFramePitch = 15;

// The work-RAM words logged beside every frame written, declared as places before the machine runs
// (a running machine answers reads of declared places only): the three per-line scroll tables'
// first entries (horizontal, vertical), the background's scroll positions, the first per-strip
// offsets, the animation scroll flag and the three table selectors.
struct Places {
    MemoryRegion bg1Entry0;   // w7e43f5: bg1 scroll entry 0 (h, v)
    MemoryRegion bg2Entry0;   // w7e4775: bg2 scroll entry 0 (h, v)
    MemoryRegion bg3Entry0;   // w7e4af5: bg3 scroll entry 0 (h, v)
    MemoryRegion bg2Entry16;  // w7e4775 + 64: bg2 scroll entry 16 (h, v)
    MemoryRegion bgScroll;    // w7e64b0..b7
    MemoryRegion bg2Strip0;   // w7e6330: bg2 per-strip offset 0 (h, v)
    MemoryRegion animScroll;  // w7e62a9
    MemoryRegion tables;      // w7e800c/d/e: the three scroll table selectors
    MemoryRegion hdma;        // w7e7bef: the HDMA channels enabled
};

RegionMapId<Places> declarePlaces(Vm& machine) {
    return machine.registerRegions(regions(
        region(&Places::bg1Entry0, MemoryRegion{.at = 0x7E43F5, .size = 4}, "bg1 scroll entry 0"),
        region(&Places::bg2Entry0, MemoryRegion{.at = 0x7E4775, .size = 4}, "bg2 scroll entry 0"),
        region(&Places::bg3Entry0, MemoryRegion{.at = 0x7E4AF5, .size = 4}, "bg3 scroll entry 0"),
        region(&Places::bg2Entry16, MemoryRegion{.at = 0x7E4775 + 64, .size = 4}, "bg2 scroll entry 16"),
        region(&Places::bgScroll, MemoryRegion{.at = 0x7E64B0, .size = 8}, "bg scroll positions"),
        region(&Places::bg2Strip0, MemoryRegion{.at = 0x7E6330, .size = 4}, "bg2 strip 0"),
        region(&Places::animScroll, MemoryRegion{.at = 0x7E62A9, .size = 1}, "anim scroll flag"),
        region(&Places::tables, MemoryRegion{.at = 0x7E800C, .size = 3}, "scroll tables"),
        region(&Places::hdma, MemoryRegion{.at = 0x7E7BEF, .size = 1}, "hdma enable")));
}

struct Logged {
    const char*            name;
    MemoryRegion Places::* place;
};
constexpr Logged kLogged[] = {
    {"bg1 entry 0 (h,v)", &Places::bg1Entry0}, {"bg2 entry 0 (h,v)", &Places::bg2Entry0},
    {"bg3 entry 0 (h,v)", &Places::bg3Entry0}, {"bg2 entry 16 (h,v)", &Places::bg2Entry16},
    {"w7e64b0..b7", &Places::bgScroll},        {"w7e6330 (h,v)", &Places::bg2Strip0},
    {"w7e62a9", &Places::animScroll},          {"w7e800c/d/e", &Places::tables},
    {"w7e7bef", &Places::hdma},
};

std::vector<std::uint8_t> readRom(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// One escape the tool watches for: where it is, and whether it has fired since the tick last looked.
struct Mark {
    const char*       key;
    std::uint32_t     at;
    std::atomic<bool> fired{false};

    GuestEscape escape(bool armed) {
        return GuestEscape{.key     = key,
                           .at      = at,
                           .handler = [this](Vm&, std::uint32_t) { fired.store(true, std::memory_order_release); },
                           .armed   = armed};
    }
    bool take() { return fired.exchange(false, std::memory_order_acq_rel); }
};

bool writePpm(const std::filesystem::path& file, const RasterContent& picture) {
    if (picture.format != RasterPixelFormat::Rgba8888 || picture.width <= 0 || picture.height <= 0) {
        return false;
    }
    std::ofstream out{file, std::ios::binary};
    if (!out) {
        return false;
    }
    out << "P6\n" << picture.width << ' ' << picture.height << "\n255\n";
    const std::size_t pixels = static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height);
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t* p = picture.pixels.data() + i * 4;
        out.put(static_cast<char>(p[0])).put(static_cast<char>(p[1])).put(static_cast<char>(p[2]));
    }
    return static_cast<bool>(out);
}

void logFrame(std::ofstream& log, Vm& machine, const RegionMapId<Places>& places, const std::string& name,
              std::uint64_t frame) {
    log << name << " frame " << frame;
    for (const Logged& item : kLogged) {
        const std::vector<std::uint8_t> bytes = machine.read(places, item.place);
        log << " | " << item.name << ':';
        char hex[8];
        for (const std::uint8_t b : bytes) {
            std::snprintf(hex, sizeof hex, " %02x", b);
            log << hex;
        }
    }
    log << '\n';
    log.flush();
}

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* kUsage =
        "usage: ostinato-frames <rom> <directory> [--battles] [--gameloaded] [--fieldmenu]\n";
    bool battles    = false;
    bool gameLoaded = false;
    bool fieldMenu  = false;
    for (int i = 3; i < argc; ++i) {
        const std::string_view flag{argv[i]};
        if (flag == "--battles") {
            battles = true;
        } else if (flag == "--gameloaded") {
            gameLoaded = true;
        } else if (flag == "--fieldmenu") {
            fieldMenu = true;
        } else {
            std::fprintf(stderr, "unknown option %s\n%s", argv[i], kUsage);
            return 2;
        }
    }
    if (!battles && !gameLoaded && !fieldMenu) {
        std::fprintf(stderr, "%s", kUsage);
        return 2;
    }
    const std::filesystem::path romFile{argv[1]};
    const std::filesystem::path directory{argv[2]};
    const std::vector<std::uint8_t> rom = readRom(romFile);
    if (rom.empty()) {
        std::fprintf(stderr, "%s could not be read\n", romFile.string().c_str());
        return 1;
    }
    std::filesystem::create_directories(directory);
    std::ofstream log{directory / "frames.log", std::ios::app};

    const EngineConfig config{
        .identity     = {.organization = "OstinatoVI", .application = "OstinatoVI"},
        .window       = {.title = "ostinato-frames — the cartridge on the engine's SNES machine"},
        .viewport     = ViewportResolution::Snes,
        .timing       = TimingProfile::Snes,
        .enhancements = {.windowScale = 3}};
    EngineConfig::setActive(config);
    SteadyClock clock;
    RunLoop     loop{clock};
    SdlPlatform platform;
    Renderer    renderer{platform.device(), platform.sdlWindow()};

    Vm::SNES machine{VmConfig{.key = "frames", .video = true}};
    machine.hostRom(std::span<const std::uint8_t>(rom));
    machine.video(true);
    const RegionMapId<Places> places = declarePlaces(machine);

    // The escapes fire on the thread that steps the machine — the tick below — so a flag each is enough;
    // the tick announces each one that fired. Only the triggers named on the command line are armed.
    Mark entered{.key = "battle entry", .at = kBattleEntry};
    Mark returned{.key = "battle return", .at = kBattleReturn};
    Mark loaded{.key = "saved game loaded", .at = kSavedGameLoaded};
    Mark opened{.key = "field menu opened", .at = kFieldMenuOpened};
    Mark closed{.key = "field menu closed", .at = kFieldMenuClosed};
    machine.registerEscapes(escapes(entered.escape(battles), returned.escape(battles), loaded.escape(gameLoaded),
                                    opened.escape(fieldMenu), closed.escape(fieldMenu)));

    ActionMap controls{
        {snes::Button::A,      {SDL_SCANCODE_X, PadButton::FaceLabelA}},
        {snes::Button::B,      {SDL_SCANCODE_Z, PadButton::FaceLabelB}},
        {snes::Button::X,      {SDL_SCANCODE_S, PadButton::FaceLabelX}},
        {snes::Button::Y,      {SDL_SCANCODE_A, PadButton::FaceLabelY}},
        {snes::Button::L,      {SDL_SCANCODE_Q, PadButton::ShoulderL}},
        {snes::Button::R,      {SDL_SCANCODE_W, PadButton::ShoulderR}},
        {snes::Button::Select, {SDL_SCANCODE_RSHIFT, PadButton::Select}},
        {snes::Button::Start,  {SDL_SCANCODE_RETURN, PadButton::Start}},
    };
    controls.add(presets::directional(snes::Button::Up, snes::Button::Down, snes::Button::Left, snes::Button::Right));
    platform.actions(controls);

    machine.run(Vm::Advance::OnTick);

    std::uint64_t frame       = 0;
    std::uint64_t battleFrame = 0;
    int           battleCount = 0;
    bool          inBattle    = false;
    std::uint64_t gameFrame   = 0;
    int           gameCount   = 0;
    bool          inGame      = false;
    std::uint64_t menuFrame   = 0;
    int           menuCount   = 0;
    bool          inMenu      = false;

    // A battle's frame is logged with the battle's scroll words; a gameplay frame with its name alone.
    const auto write = [&](const std::string& name, bool withBattleWords) {
        const RasterContent picture = machine.video();
        const std::filesystem::path file = directory / (name + ".ppm");
        if (!writePpm(file, picture)) {
            std::printf("could not write %s\n", file.string().c_str());
            std::fflush(stdout);
        } else if (withBattleWords) {
            logFrame(log, machine, places, name, frame);
        } else {
            log << name << " frame " << frame << '\n';
            log.flush();
        }
    };
    const auto note = [&](const std::string& line) {
        log << line << '\n';
        log.flush();
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    };
    // Every escape that fired is announced, by its key and address, with the frame it fired on.
    const auto fired = [&](Mark& mark) {
        if (!mark.take()) {
            return false;
        }
        char at[16];
        std::snprintf(at, sizeof at, "$%02X:%04X", mark.at >> 16, mark.at & 0xFFFF);
        note(std::string{"escape \""} + mark.key + "\" (" + at + ") fired at frame " + std::to_string(frame));
        return true;
    };
    const auto offset = [](std::uint64_t now, std::uint64_t since) { return std::to_string(now - since); };

    loop.simTick([&](const InputState& input) {
        machine.buttons(snes::Ports{.one = snes::held(input), .two = std::nullopt});
        machine.advanceTick();
        ++frame;

        if (fired(loaded)) {
            ++gameCount;
            gameFrame = frame;
            inGame    = true;
            note("saved game " + std::to_string(gameCount) + " loaded at frame " + std::to_string(frame));
        }
        if (inGame && (frame - gameFrame) % kFramePitch == 0) {
            write("game" + std::to_string(gameCount) + "-f" + offset(frame, gameFrame), false);
        }

        if (fired(entered)) {
            ++battleCount;
            battleFrame = frame;
            inBattle    = true;
            note("battle " + std::to_string(battleCount) + " began at frame " + std::to_string(frame));
        }
        if (inBattle && (frame - battleFrame) % kFramePitch == 0) {
            write("battle" + std::to_string(battleCount) + "-f" + offset(frame, battleFrame), true);
        }
        if (fired(returned) && inBattle) {
            write("battle" + std::to_string(battleCount) + "-f" + offset(frame, battleFrame) + "-end", true);
            inBattle = false;
            note("battle " + std::to_string(battleCount) + " ended at frame " + std::to_string(frame) + " (" +
                 offset(frame, battleFrame) + " frames)");
        }

        if (fired(opened)) {
            ++menuCount;
            menuFrame = frame;
            inMenu    = true;
            note("menu " + std::to_string(menuCount) + " opened at frame " + std::to_string(frame));
        }
        if (inMenu && (frame - menuFrame) % kFramePitch == 0) {
            write("menu" + std::to_string(menuCount) + "-f" + offset(frame, menuFrame), false);
        }
        if (fired(closed) && inMenu) {
            write("menu" + std::to_string(menuCount) + "-f" + offset(frame, menuFrame) + "-end", false);
            inMenu = false;
            note("menu " + std::to_string(menuCount) + " closed at frame " + std::to_string(frame) + " (" +
                 offset(frame, menuFrame) + " frames)");
        }
    });

    FrameDrawState drawn;
    loop.renderLoop([&] {
        drawn.layers.clear();
        DrawLayer screen{.key = "screen"};
        screen.z       = 0;
        screen.size    = config.viewport.size();
        screen.content = machine.video();
        drawn.layers.push_back(screen);
        renderer.renderFrame(drawn);
    });

    std::printf("Frames are written under %s once a trigger fires:%s%s%s\n", directory.string().c_str(),
                battles ? " --battles" : "", gameLoaded ? " --gameloaded" : "", fieldMenu ? " --fieldmenu" : "");
    std::fflush(stdout);
    WindowedHost{loop, platform}.run();
    machine.stop();
    return 0;
}
