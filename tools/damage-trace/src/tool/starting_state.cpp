// readRom() and runTrace(): the cartridge image read from a file, and the trace run in a window.

#include "tool/starting_state.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include "retropp/clock.h"
#include "retropp/draw_state.h"
#include "retropp/engine_config.h"
#include "retropp/input.h"
#include "retropp/input_actions.h"
#include "retropp/renderer.h"
#include "retropp/run_loop.h"
#include "retropp/sdl_platform.h"
#include "retropp/snes.h"
#include "retropp/timing.h"
#include "retropp/viewport.h"
#include "retropp/vm.h"
#include "retropp/windowed_host.h"

#include "oracle/damage_oracle.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

std::vector<std::uint8_t> readRom(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

int runTrace(std::span<const std::uint8_t> image, const std::filesystem::path& directory,
             std::optional<std::uint64_t> quitAfterFrames, const std::optional<StartingState>& from) {
    std::ofstream log{directory / "damage-trace.log", std::ios::app};

    const EngineConfig config{
        .identity     = {.organization = "OstinatoVI", .application = "OstinatoVI"},
        .window       = {.title = "ostinato-damage-trace — the cartridge's damage routines, watched"},
        .viewport     = ViewportResolution::Snes,
        .timing       = TimingProfile::Snes,
        .enhancements = {.windowScale = 3}};
    EngineConfig::setActive(config);
    SteadyClock clock;
    RunLoop     loop{clock};
    SdlPlatform platform;
    Renderer    renderer{platform.device(), platform.sdlWindow()};

    // A restored machine writes the state's save over its battery file, so a machine started from a
    // state keeps none.
    Vm::SNES machine{from ? VmConfig{.video = true} : VmConfig{.key = "damage-trace", .video = true}};
    machine.hostRom(image);
    machine.video(true);

    std::uint64_t frame = 0;

    // The trace's lines go to the log; each call's entry and return is also printed.
    const trace::DamageTrace traced{
        machine,
        [&](const std::string& line) {
            log << line << '\n';
            if (line.find(" enter ") != std::string::npos || line.find(" return ") != std::string::npos) {
                log.flush();
                std::printf("%s\n", line.c_str());
                std::fflush(stdout);
            }
        },
        [&] { return frame; }};

    if (from) {
        if (!machine.restore(from->state.bytes)) {
            std::fprintf(stderr, "%s is not a state this machine takes\n", from->file.string().c_str());
            return 1;
        }
        log << "state restored from " << from->file.string() << '\n';
        log.flush();
        std::printf("Starting from the state in %s\n", from->file.string().c_str());
    }

    ActionMap controls{
        {snes::Button::A,      {SDL_SCANCODE_X, PadButton::FaceLabelA}},
        {snes::Button::B,      {SDL_SCANCODE_Z, PadButton::FaceLabelB}},
        {snes::Button::X,      {SDL_SCANCODE_S, PadButton::FaceLabelX}},
        {snes::Button::Y,      {SDL_SCANCODE_A, PadButton::FaceLabelY}},
        {snes::Button::L,      {SDL_SCANCODE_Q, PadButton::ShoulderL}},
        {snes::Button::R,      {SDL_SCANCODE_W, PadButton::ShoulderR}},
        {snes::Button::Select, {SDL_SCANCODE_RSHIFT, PadButton::Select}},
        {snes::Button::Start,  {SDL_SCANCODE_RETURN, PadButton::Start}},
        {snes::Button::Up,     {SDL_SCANCODE_UP, PadButton::DpadUp}},
        {snes::Button::Down,   {SDL_SCANCODE_DOWN, PadButton::DpadDown}},
        {snes::Button::Left,   {SDL_SCANCODE_LEFT, PadButton::DpadLeft}},
        {snes::Button::Right,  {SDL_SCANCODE_RIGHT, PadButton::DpadRight}},
    };
    platform.actions(controls);

    machine.run(Vm::Advance::OnTick);

    loop.simTick([&](const InputState& input) {
        machine.buttons(snes::Ports{.one = snes::held(input), .two = std::nullopt});
        machine.advanceTick();
        ++frame;
        if (quitAfterFrames && frame >= *quitAfterFrames) {
            loop.exitRequest();
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

    std::printf("Tracing the battle routines to %s\n", (directory / "damage-trace.log").string().c_str());
    std::fflush(stdout);

    loop.exitAction([&] {
        log << "window closed at frame " << frame << '\n';
        log.flush();
        std::printf("window closed at frame %llu\n", static_cast<unsigned long long>(frame));
        return ExitVerdict::Proceed;
    });

    WindowedHost{loop, platform}.run();
    machine.stop();
    return 0;
}

}  // namespace damage_trace
