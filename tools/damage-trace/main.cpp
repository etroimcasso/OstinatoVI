// ostinato-damage-trace — the cartridge's damage routines watched as the game runs them, or checked
// against the port's formulas. A developer tool for seeing exactly what the game's damage code reads
// and writes, and for proving the port's numbers are the game's; it is not part of the game.
//
//     ostinato-damage-trace <rom> <directory> [--quit-after-frames <n>]
//     ostinato-damage-trace <rom> <directory> --check <draws>
//     ostinato-damage-trace <rom> <directory> --replay
//
// Without --check or --replay the cartridge is run on the engine's SNES machine in a window, played
// from the keyboard, and three of its damage routines are traced (trace::DamageTrace): escapes on each
// routine's entry and on the instructions its callers return to open and close a call, the attack's
// context logged at each entry, and watches over every cell the routines read or write log each
// access made inside a call, in order, with its address and byte. Every call is logged — nothing is
// filtered — to `damage-trace.log` under <directory>, in the lines damage_oracle.h names.
//
// The routines: the base damage (`CalcDmg`, $C2:2B9D, returning to $C2:322E), the damage to one
// target (`CalcTargetDmg`, $C2:0B83, returning to $C2:346C from the attack and $C2:3509 from a
// bounce) and taking and showing it (`_c262ef`, $C2:62EF, returning to $C2:3473 and $C2:3528). The
// addresses are the US cartridges'. --quit-after-frames closes the window after that many frames;
// otherwise close it to quit.
//
// With --replay nothing opens. Every call in <directory>/damage-trace.log is rebuilt as a battle and
// worked out by the port, and the port's answer is compared with every cell the cartridge wrote;
// each call is logged to `replay.log` with its differences, and any difference fails the run.
//
// With --check nothing opens. The cartridge is started as far as its reset code, its base damage,
// relic bonus and multiplier routines are bound where it holds them, and <draws> scenarios drawn
// from a fixed seed are run through each routine and through the port's formula for it. Every
// scenario is logged to `check.log` with both answers; a disagreement is printed and fails the run.
//
// Keys: arrows = d-pad, X = A, Z = B, S = X, A = Y, Q/W = L/R, Enter = Start, Right Shift = Select.

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include "retropp/clock.h"
#include "retropp/draw_state.h"
#include "retropp/engine_config.h"
#include "retropp/guest_escape.h"
#include "retropp/guest_watch.h"
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

#include "assets/cartridge.h"
#include "battle/damage.h"
#include "damage_oracle.h"
#include "ostinato/attack_flags.h"

namespace {

using namespace retropp;
using namespace ostinato;

std::vector<std::uint8_t> readRom(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// --- the trace, in a window -------------------------------------------------------------------

int runTrace(std::span<const std::uint8_t> image, const std::filesystem::path& directory,
             std::optional<std::uint64_t> quitAfterFrames) {
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

    Vm::SNES machine{VmConfig{.key = "damage-trace", .video = true}};
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

    std::printf("Tracing CalcDmg, CalcTargetDmg and the apply-and-show routine to %s\n",
                (directory / "damage-trace.log").string().c_str());
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

// --- the check, with no window ---------------------------------------------------------------

int runCheck(std::span<const std::uint8_t> image, const std::filesystem::path& directory, int draws) {
    std::ofstream log{directory / "check.log"};
    trace::DamageOracle oracle{image};
    std::mt19937 rng{trace::kSweepSeed};
    int mismatches = 0;
    int done = 0;  // scenarios run through a routine so far

    const auto compare = [&](const char* routine, int draw, const trace::DamageScenario& s, long ours, long theirs) {
        const bool same = ours == theirs;
        log << routine << ' ' << draw << ": " << trace::describe(s) << " port=" << ours << " cartridge=" << theirs
            << (same ? "" : " MISMATCH") << '\n';
        if (!same) {
            ++mismatches;
            std::printf("%s draw %d: port %ld, cartridge %ld — %s\n", routine, draw, ours, theirs,
                        trace::describe(s).c_str());
        }
    };

    // The check runs from one tick of a loop on the console's cadence, stepped as fast as the host
    // allows; the tick asks for the exit when it is done, and the exit action reports the result.
    trace::TickClock clock{TimingProfile::Snes.tickPeriod()};
    RunLoop loop{clock, TimingProfile::Snes};
    loop.simTick([&](const InputState&) {
        if (done > 0) {
            return;
        }
        oracle.start();
        for (int draw = 0; draw < draws; ++draw) {
            const trace::DamageScenario s = trace::drawScenario(rng);
            const std::uint16_t base =
                s.attack.record.traits.has(AttackTrait::PHYSICAL)
                    ? battle::physicalDamage(s.state, s.attack, s.attacker)
                    : battle::magicDamage(s.attack.record.power, s.attack.attackerStat, s.attack.attackerLevel);
            compare("CalcDmg", draw, s, base, oracle.calcDamage(s));
        }
        rng.seed(trace::kSweepSeed);
        for (int draw = 0; draw < draws; ++draw) {
            const trace::DamageScenario s = trace::drawScenario(rng);
            const auto damage = static_cast<std::uint16_t>(s.attack.damage);
            compare("RelicDmgEffect", draw, s, battle::relicBonus(s.state, s.attack, s.attacker, damage),
                    oracle.relicBonus(s, damage));
        }
        rng.seed(trace::kSweepSeed);
        for (int draw = 0; draw < draws; ++draw) {
            trace::DamageScenario s = trace::drawScenario(rng);
            const trace::MultiplierResult theirs = oracle.applyMultiplier(s);
            battle::scaleDamage(s.attack);
            compare("ApplyDmgMult", draw, s, s.attack.damage, theirs.damage);
            compare("ApplyDmgMult steps", draw, s, s.attack.multiplier, theirs.stepsLeft);
        }
        done = 3 * draws;
        loop.exitRequest();
    });
    loop.renderLoop([] {});
    loop.exitAction([&] {
        std::printf("%d draws through each of three routines: %d mismatch%s; %s\n", draws, mismatches,
                    mismatches == 1 ? "" : "es", (directory / "check.log").string().c_str());
        return ExitVerdict::Proceed;
    });
    loop.run();
    return mismatches == 0 ? 0 : 1;
}

// --- the replay, with no window --------------------------------------------------------------

int runReplay(std::span<const std::uint8_t> image, const std::filesystem::path& directory) {
    std::ifstream recorded{directory / "damage-trace.log"};
    if (!recorded) {
        std::fprintf(stderr, "%s could not be read\n", (directory / "damage-trace.log").string().c_str());
        return 1;
    }
    const std::vector<trace::TracedCall> calls = trace::parseTrace(recorded);
    std::ofstream log{directory / "replay.log"};
    trace::DamageOracle oracle{image};
    int parted = 0;

    trace::TickClock clock{TimingProfile::Snes.tickPeriod()};
    RunLoop loop{clock, TimingProfile::Snes};
    bool done = false;
    loop.simTick([&](const InputState&) {
        if (done) {
            return;
        }
        done = true;
        oracle.start();
        for (const trace::TracedCall& call : calls) {
            const std::string differences = oracle.replay(call);
            log << "f" << call.frame << ' ' << call.routine << (differences.empty() ? " agrees\n" : " PARTS\n")
                << differences;
            if (!differences.empty()) {
                ++parted;
                std::printf("frame %llu, %s:\n%s", static_cast<unsigned long long>(call.frame), call.routine.c_str(),
                            differences.c_str());
            }
        }
        loop.exitRequest();
    });
    loop.renderLoop([] {});
    loop.exitAction([&] {
        std::printf("%zu calls replayed, %d differing from the cartridge; %s\n", calls.size(), parted,
                    (directory / "replay.log").string().c_str());
        return ExitVerdict::Proceed;
    });
    loop.run();
    return parted == 0 ? 0 : 1;
}

std::optional<std::uint64_t> parseCount(std::string_view arg) {
    std::uint64_t count = 0;
    const auto [end, error] = std::from_chars(arg.data(), arg.data() + arg.size(), count);
    if (error != std::errc{} || end != arg.data() + arg.size()) {
        return std::nullopt;
    }
    return count;
}

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* kUsage =
        "usage: ostinato-damage-trace <rom> <directory> [--quit-after-frames <n>]\n"
        "       ostinato-damage-trace <rom> <directory> --check <draws>\n"
        "       ostinato-damage-trace <rom> <directory> --replay\n";
    if (argc < 3) {
        std::fprintf(stderr, "%s", kUsage);
        return 2;
    }
    std::optional<std::uint64_t> quitAfterFrames;
    std::optional<std::uint64_t> draws;
    bool replay = false;
    for (int i = 3; i < argc; ++i) {
        const std::string_view flag{argv[i]};
        if (flag == "--replay") {
            replay = true;
        } else if (flag == "--quit-after-frames" && i + 1 < argc) {
            quitAfterFrames = parseCount(argv[++i]);
            if (!quitAfterFrames) {
                std::fprintf(stderr, "--quit-after-frames takes a number of frames\n%s", kUsage);
                return 2;
            }
        } else if (flag == "--check" && i + 1 < argc) {
            draws = parseCount(argv[++i]);
            if (!draws || *draws == 0) {
                std::fprintf(stderr, "--check takes a number of draws\n%s", kUsage);
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option %s\n%s", argv[i], kUsage);
            return 2;
        }
    }
    const std::filesystem::path romFile{argv[1]};
    const std::filesystem::path directory{argv[2]};
    const std::vector<std::uint8_t> rom = readRom(romFile);
    if (rom.empty()) {
        std::fprintf(stderr, "%s could not be read\n", romFile.string().c_str());
        return 1;
    }
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const std::span<const std::uint8_t> image = assets::stripCopierHeader(rom);

    if (replay) {
        return runReplay(image, directory);
    }
    if (draws) {
        return runCheck(image, directory, static_cast<int>(*draws));
    }
    return runTrace(image, directory, quitAfterFrames);
}
