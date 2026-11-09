// ostinato-damage-trace — the cartridge's damage routines watched as the game runs them, or checked
// against the port's formulas. A developer tool for seeing exactly what the game's damage code reads
// and writes, and for proving the port's numbers are the game's; it is not part of the game.
//
//     ostinato-damage-trace <rom> <directory> [--from <state>] [--quit-after-frames <n>]
//     ostinato-damage-trace <rom> <directory> --headless --frames <n> [--from <state>] [--inputs <script>]
//                                             [--picture-at <frame>]... [--write-at <frame> <address> <bytes>]...
//     ostinato-damage-trace <rom> <directory> --to-battle <inputs> [--twice]
//     ostinato-damage-trace <rom> <directory> --check-state <ticks>
//     ostinato-damage-trace <rom> <directory> --check <draws> [--only <routine>[,<routine>]...] [--seed <n>]
//                                             [--replay-traced]
//     ostinato-damage-trace <rom> <directory> --replay [--log <file>]
//
// Without another mode the cartridge is run on the engine's SNES machine in a window, played from the
// keyboard or a gamepad, and nine of its battle routines are traced (trace::DamageTrace): escapes on each
// routine's entry and on the instructions its callers return to open and close a call, the attack's
// context logged at each entry, and watches over every cell the routines read or write log each
// access made inside a call, in order, with its address and byte. Every call is logged — nothing is
// filtered — to `damage-trace.log` under <directory>, in the lines damage_oracle.h names.
//
// The routines: the base damage (`CalcDmg`, $C2:2B9D, returning to $C2:322E), the damage to one
// target (`CalcTargetDmg`, $C2:0B83, returning to $C2:346C from the attack and $C2:3509 from a
// bounce), taking and showing it (`_c262ef`, $C2:62EF, returning to $C2:3473 and $C2:3528), the
// statuses taking effect (`UpdateStatus`, $C2:4391, returning to $C2:324D from the attack, $C2:24A7
// from the battle's start and $C2:50AA from a formation change) and an attack's statuses on one
// target (`MagicStatusEffect`, $C2:4406, returning to $C2:345C and $C2:34F9), and an attack's special
// effect on its attacker (`DoAttackerEffect`, $C2:3E7D, returning to $C2:33A9) and on one target
// (`DoTargetEffect`, $C2:387E, returning to $C2:345F and $C2:34FC); and at a won battle's end, the
// spells an equipped esper teaches learned further (`LearnGenjuMagic`, $C2:602A, returning to
// $C2:5E72) and a level-up (`DoLevelUp`, $C2:60C2, returning to $C2:60B4). The addresses are the US
// cartridges'. --quit-after-frames closes the window after that many frames;
// otherwise close it to quit.
//
// With --from the trace starts at a state written by --to-battle instead of at power-on, on a machine
// with no battery file. A state from another cartridge revision is refused.
//
// With --to-battle nothing opens. The cartridge runs from power-on holding the buttons <inputs>
// names (machine_state.h has the format), and the machine as it stood on the tick before the battle
// program was entered is written to `battle.state` under <directory>, so a trace started from it sees
// the whole battle. A run that passes the script's last line by kRunOutFrames with no battle writes
// the picture it last finished to `to-battle-end.ppm` and fails. --twice runs it again and fails if the
// two states differ by a byte.
//
// With --headless nothing opens. The trace runs for --frames frames from power-on or --from, holding
// the buttons --inputs names, on a machine with no battery file, and writes the picture of each
// --picture-at frame to `frame-<n>.ppm`. Each --write-at writes its bytes into work RAM before its frame
// runs (machine_state.h has the form).
//
// With --check-state nothing opens. A machine is run --check-state ticks from power-on, and its state
// is written, read back and restored on a second machine; both run on, and their memory and pictures
// are compared.
//
// With --replay nothing opens. Every call in <directory>/damage-trace.log, or the file --log names, is
// rebuilt as a battle and worked out by the port, and the port's answer is compared with every cell the
// cartridge wrote; each call is logged to `replay.log` with its differences, and any difference fails
// the run.
//
// With --check nothing opens. The cartridge is started as far as its reset code, its routines are bound
// where it holds them, and <draws> scenarios drawn from a fixed seed (or --seed) are run through each
// routine (or those --only names) and through the port. Every scenario is logged to `check.log`; a
// disagreement is printed and fails the run. --replay-traced also traces the cartridge's calls in each
// draw and replays them against the port.
//
// Keys: arrows = d-pad, X = A, Z = B, S = X, A = Y, Q/W = L/R, Enter = Start, Right Shift = Select.

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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
#include "machine_state.h"
#include "ostinato/attack_flags.h"
#include "ostinato/game_version.h"

namespace {

using namespace retropp;
using namespace ostinato;

// The battle program's entry — `Battle_ext: jmp BattleMain` (battle/battle_main.asm:39-40; the program
// sits at $C2:0000).
constexpr std::uint32_t kBattleEntry = 0xC20000;

// How many frames a scripted run goes on past its script's last line looking for a battle.
constexpr std::uint64_t kRunOutFrames = 600;

std::vector<std::uint8_t> readRom(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// --- the trace, in a window -------------------------------------------------------------------

// A state the machine carries on from, in place of power-on.
struct StartingState {
    std::filesystem::path file;
    trace::MachineState state;
};

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

    std::printf("Tracing CalcDmg, CalcTargetDmg, the apply-and-show routine, UpdateStatus and MagicStatusEffect to %s\n",
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

// --- the scripted run to the first battle, with no window ------------------------------------

bool writePicture(const std::filesystem::path& file, const RasterContent& picture) {
    if (picture.format != RasterPixelFormat::Rgba8888 || picture.width <= 0 || picture.height <= 0) {
        return false;
    }
    std::ofstream out{file, std::ios::binary};
    out << "P6\n" << picture.width << ' ' << picture.height << "\n255\n";
    const std::size_t pixels = static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height);
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t* p = picture.pixels.data() + i * 4;
        out.put(static_cast<char>(p[0])).put(static_cast<char>(p[1])).put(static_cast<char>(p[2]));
    }
    return static_cast<bool>(out);
}

// The state from the tick before the battle program's entry, or nothing with no battle by the script's
// end; the run's frame and its last picture are reported either way.
std::optional<std::vector<std::uint8_t>> runScriptToBattle(std::span<const std::uint8_t> image,
                                                           const std::filesystem::path& endPicture,
                                                           std::span<const trace::InputStep> script) {
    const std::uint64_t lastFrame = (script.empty() ? 0 : script.back().frame) + kRunOutFrames;
    std::optional<std::vector<std::uint8_t>> state;

    trace::runOnLoop([&] {
        Vm::SNES machine{VmConfig{.video = true}};
        machine.hostRom(image);
        bool entered = false;
        machine.registerEscapes(escapes(GuestEscape{.key     = "battle entry",
                                                    .at      = kBattleEntry,
                                                    .handler = [&](Vm&, std::uint32_t) { entered = true; }}));
        machine.run(Vm::Advance::OnTick);

        // A state cannot be taken inside the escape, and the escape's tick has already run past the
        // entry, so each tick's state is kept until the next tick shows it was the last one before
        // the battle. Finishing a frame to take the state can reach the entry too.
        std::vector<std::uint8_t> kept;
        std::uint64_t frame = 0;
        while (!entered && frame <= lastFrame) {
            machine.buttons(snes::Ports{.one = trace::buttonsAt(script, frame), .two = std::nullopt});
            machine.advanceTick();
            ++frame;
            if (!entered) {
                std::vector<std::uint8_t> next = machine.state();
                if (!entered) {
                    kept = std::move(next);
                }
            }
        }

        if (entered) {
            std::printf("battle entered on frame %llu\n", static_cast<unsigned long long>(frame - 1));
            state = std::move(kept);
        } else {
            const bool written = writePicture(endPicture, machine.video());
            std::printf("no battle by frame %llu%s%s\n", static_cast<unsigned long long>(frame),
                        written ? "; " : "", written ? endPicture.string().c_str() : "");
        }
        machine.stop();
    });
    return state;
}

int runToBattle(std::span<const std::uint8_t> image, GameVersion cartridge, const std::filesystem::path& directory,
                std::span<const trace::InputStep> script, bool twice) {
    const std::filesystem::path stateFile = directory / "battle.state";
    const std::filesystem::path endPicture = directory / "to-battle-end.ppm";
    const std::optional<std::vector<std::uint8_t>> state = runScriptToBattle(image, endPicture, script);
    if (!state) {
        return 1;
    }
    trace::writeMachineState(stateFile, cartridge, *state);
    std::printf("%s\n", stateFile.string().c_str());
    if (twice) {
        const std::optional<std::vector<std::uint8_t>> again = runScriptToBattle(image, endPicture, script);
        const bool same = again && *again == *state;
        std::printf("two runs: %s\n", same ? "the same state, byte for byte" : "DIFFERENT STATES");
        return same ? 0 : 1;
    }
    return 0;
}

// --- a trace with no window ------------------------------------------------------------------

// The machine's work RAM, declared so it can be read while the machine runs.
struct WorkRam {
    MemoryRegion cells;
};

RegionMapId<WorkRam> declareWorkRam(Vm& machine) {
    return machine.registerRegions(regions(region(&WorkRam::cells, MemoryRegion{.at = 0x7E0000, .size = 0x10000}, "work RAM")));
}

int runHeadless(std::span<const std::uint8_t> image, const std::filesystem::path& directory, std::uint64_t frames,
                const std::optional<StartingState>& from, std::span<const trace::InputStep> script,
                std::span<const std::uint64_t> pictureFrames, std::span<const trace::CellWrite> writes) {
    std::ofstream log{directory / "damage-trace.log", std::ios::app};
    int result = 0;
    trace::runOnLoop([&] {
        Vm::SNES machine{VmConfig{.video = true}};
        machine.hostRom(image);
        std::uint64_t frame = 0;
        const trace::DamageTrace traced{machine,
                                        [&](const std::string& line) {
                                            log << line << '\n';
                                            if (line.find(" enter ") != std::string::npos) {
                                                std::printf("%s\n", line.c_str());
                                            }
                                        },
                                        [&] { return frame; }};
        if (from) {
            if (!machine.restore(from->state.bytes)) {
                std::fprintf(stderr, "%s is not a state this machine takes\n", from->file.string().c_str());
                result = 1;
                return;
            }
            log << "state restored from " << from->file.string() << '\n';
        }
        const trace::WorkRamBytes::Map ram = trace::declareWorkRamBytes(machine);
        machine.run(Vm::Advance::OnTick);
        for (; frame < frames; ++frame) {
            trace::writeCells(machine, ram, writes, frame);
            machine.buttons(snes::Ports{.one = trace::buttonsAt(script, frame), .two = std::nullopt});
            machine.advanceTick();
            if (std::find(pictureFrames.begin(), pictureFrames.end(), frame) != pictureFrames.end()) {
                const std::filesystem::path file = directory / ("frame-" + std::to_string(frame) + ".ppm");
                if (!writePicture(file, machine.video())) {
                    std::fprintf(stderr, "%s could not be written\n", file.string().c_str());
                    result = 1;
                }
            }
        }
        machine.stop();
        log << "stopped at frame " << frame << '\n';
    });
    std::printf("%llu frames traced to %s\n", static_cast<unsigned long long>(frames),
                (directory / "damage-trace.log").string().c_str());
    return result;
}

// --- a state carried on from, with no window -------------------------------------------------

// A machine run for `ticks` from power-on and one restored from its state through a file, both run on
// past the state's moment by kCarryOnTicks: the two must stand on the same memory and picture, and one
// tick short of that they must not, so the comparison is seen to see a tick.
int runStateCheck(std::span<const std::uint8_t> image, GameVersion cartridge, const std::filesystem::path& directory,
                  std::uint64_t ticks) {
    constexpr std::uint64_t kCarryOnTicks = 240;
    const std::filesystem::path file = directory / "check.state";
    int result = 1;
    trace::runOnLoop([&] {
        Vm::SNES source{VmConfig{.video = true}};
        source.hostRom(image);
        const RegionMapId<WorkRam> sourceRam = declareWorkRam(source);
        source.run(Vm::Advance::OnTick);
        for (std::uint64_t tick = 0; tick < ticks; ++tick) {
            source.advanceTick();
        }
        trace::writeMachineState(file, cartridge, source.state());

        const trace::MachineState saved = trace::readMachineState(file);
        Vm::SNES restored{VmConfig{.video = true}};
        restored.hostRom(image);
        const RegionMapId<WorkRam> restoredRam = declareWorkRam(restored);
        if (!restored.restore(saved.bytes)) {
            std::fprintf(stderr, "%s is not a state this machine takes\n", file.string().c_str());
            return;
        }
        restored.run(Vm::Advance::OnTick);
        for (std::uint64_t tick = 0; tick < kCarryOnTicks; ++tick) {
            restored.advanceTick();
        }
        for (std::uint64_t tick = 0; tick + 1 < kCarryOnTicks; ++tick) {
            source.advanceTick();
        }
        const std::vector<std::uint8_t> restoredCells = restored.read(restoredRam, &WorkRam::cells);
        const bool shortDiffers = source.read(sourceRam, &WorkRam::cells) != restoredCells;
        source.advanceTick();
        const bool memorySame = source.read(sourceRam, &WorkRam::cells) == restoredCells;
        const RasterContent theirs = source.video();
        const RasterContent ours = restored.video();
        const bool pictureSame = std::equal(theirs.pixels.begin(), theirs.pixels.end(), ours.pixels.begin(), ours.pixels.end());
        source.stop();
        restored.stop();
        std::printf("state of %llu bytes: one tick short the memory %s; at %llu ticks on, memory %s, picture %s\n",
                    static_cast<unsigned long long>(saved.bytes.size()), shortDiffers ? "differs" : "DOES NOT DIFFER",
                    static_cast<unsigned long long>(kCarryOnTicks), memorySame ? "same" : "DIFFERS",
                    pictureSame ? "same" : "DIFFERS");
        result = shortDiffers && memorySame && pictureSame ? 0 : 1;
    });
    return result;
}

// --- the check, with no window ---------------------------------------------------------------

// One draw of one sweep: what was drawn, both answers where they are one number, every cell where
// the port's answer parts from the cartridge's, and for an attack's special effect on one target, the
// target it was worked out for (the oracle calls the routine with no statuses worked out before it, so
// its trace does not show the target).
struct DrawResult {
    std::string described;
    std::string answers;
    std::string differences;
    std::optional<battle::BattlerSlot> target{};
};

using SweepDraw = DrawResult (*)(trace::DamageOracle&, std::mt19937&);

struct Sweep {
    std::string_view name;
    SweepDraw draw;
};

std::string numberDifference(long port, long cartridge) {
    return port == cartridge ? std::string{} : "port " + std::to_string(port) + ", cartridge " + std::to_string(cartridge) + "\n";
}

std::string numberAnswers(long port, long cartridge) {
    return " port=" + std::to_string(port) + " cartridge=" + std::to_string(cartridge);
}

// Every routine the oracle runs, by the name --only takes, in the order --check runs them.
constexpr std::array<Sweep, 14> kSweeps{{
    {.name = "base-damage",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawScenario(rng);
         const std::uint16_t port = s.attack.record.traits.has(AttackTrait::PHYSICAL)
                                        ? battle::physicalDamage(s.state, s.attack, s.attacker)
                                        : battle::magicDamage(s.attack.record.power, s.attack.attackerStat, s.attack.attackerLevel);
         const std::uint16_t guest = oracle.calcDamage(s);
         return DrawResult{trace::describe(s), numberAnswers(port, guest), numberDifference(port, guest)};
     }},
    {.name = "relic-bonus",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawScenario(rng);
         const auto damage = static_cast<std::uint16_t>(s.attack.damage);
         const std::uint16_t port = battle::relicBonus(s.state, s.attack, s.attacker, damage);
         const std::uint16_t guest = oracle.relicBonus(s, damage);
         return DrawResult{trace::describe(s), numberAnswers(port, guest), numberDifference(port, guest)};
     }},
    {.name = "multiplier",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         trace::DamageScenario s = trace::drawScenario(rng);
         const trace::MultiplierResult guest = oracle.applyMultiplier(s);
         battle::scaleDamage(s.attack);
         return DrawResult{trace::describe(s), numberAnswers(s.attack.damage, guest.damage),
                           numberDifference(s.attack.damage, guest.damage) +
                               numberDifference(s.attack.multiplier, guest.stepsLeft)};
     }},
    {.name = "target-damage",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::TargetScenario d = trace::drawTargetScenario(rng);
         return DrawResult{trace::describe(d), "",
                           trace::differences(trace::portTargetDamage(d.scenario, d.target),
                                              oracle.targetDamage(d.scenario, d.target))};
     }},
    {.name = "apply-damage",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawApplyScenario(rng);
         return DrawResult{trace::describe(s), "", trace::differences(trace::portApplyDamage(s), oracle.applyDamage(s))};
     }},
    {.name = "reflected-attack",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawReflectScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portReflectedAttack(s), oracle.reflectedAttack(s))};
     }},
    {.name = "status-changes",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawStatusChangesScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portStatusChanges(s), oracle.statusChanges(s))};
     }},
    {.name = "status-effect",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::TargetScenario d = trace::drawStatusEffectScenario(rng);
         return DrawResult{trace::describe(d), "",
                           trace::differences(trace::portStatusEffect(d.scenario, d.target),
                                              oracle.statusEffect(d.scenario, d.target))};
     }},
    {.name = "attacker-effect",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawAttackerEffectScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::attackerEffectResult(trace::runAttackerEffect(s)),
                                              oracle.attackerEffect(s))};
     }},
    {.name = "target-effect",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::TargetScenario d = trace::drawTargetEffectScenario(rng);
         return DrawResult{trace::describe(d), "",
                           trace::differences(trace::targetEffectResult(trace::runTargetEffect(d.scenario, d.target)),
                                              oracle.targetEffect(d.scenario, d.target)),
                           d.target};
     }},
    {.name = "params",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawAttackerEffectScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portInitParams(s), oracle.initParams(s))};
     }},
    {.name = "cover",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawCoverScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portCoverTarget(s), oracle.coverEffect(s))};
     }},
    {.name = "esper-learning",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::RecordScenario s = trace::drawLearningScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portLearnFromEsper(s), oracle.learnFromEsper(s))};
     }},
    {.name = "level-up",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::RecordScenario s = trace::drawLevelUpScenario(rng);
         return DrawResult{trace::describe(s), "", trace::differences(trace::portLevelUp(s), oracle.levelUp(s))};
     }},
}};

struct CheckOptions {
    int draws = 0;
    std::uint32_t seed = trace::kSweepSeed;
    std::vector<std::string> only;  // the sweeps to run, by name; every one when empty
    bool replayTraced = false;      // also replay against the port the calls the cartridge made in each draw
};

int runCheck(std::span<const std::uint8_t> image, const std::filesystem::path& directory, const CheckOptions& options) {
    std::ofstream log{directory / "check.log"};
    int mismatches = 0;
    int sweeps = 0;
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image};
        oracle.start();
        oracle.trace(options.replayTraced);
        for (const Sweep& sweep : kSweeps) {
            if (!options.only.empty() && std::find(options.only.begin(), options.only.end(), sweep.name) == options.only.end()) {
                continue;
            }
            ++sweeps;
            std::mt19937 rng{options.seed};
            for (int draw = 0; draw < options.draws; ++draw) {
                DrawResult result = sweep.draw(oracle, rng);
                if (options.replayTraced) {
                    std::stringstream traced;
                    for (const std::string& line : oracle.takeTrace()) {
                        traced << line << '\n';
                    }
                    for (trace::TracedCall call : trace::parseTrace(traced)) {
                        if (!call.target) {
                            call.target = result.target;
                        }
                        const std::string parted = oracle.replay(call);
                        if (!parted.empty()) {
                            result.differences += "replayed " + call.routine + ":\n" + parted;
                        }
                    }
                }
                const bool same = result.differences.empty();
                log << sweep.name << ' ' << draw << ": " << result.described << result.answers
                    << (same ? " agrees\n" : " MISMATCH\n") << result.differences;
                if (!same) {
                    ++mismatches;
                    std::printf("%s draw %d: %s\n%s", std::string{sweep.name}.c_str(), draw, result.described.c_str(),
                                result.differences.c_str());
                }
            }
        }
    });
    std::printf("%d draws through each of %d routines: %d mismatch%s; %s\n", options.draws, sweeps, mismatches,
                mismatches == 1 ? "" : "es", (directory / "check.log").string().c_str());
    return mismatches == 0 ? 0 : 1;
}

// --- the replay, with no window --------------------------------------------------------------

int runReplay(std::span<const std::uint8_t> image, const std::filesystem::path& directory,
              const std::filesystem::path& recording) {
    std::ifstream recorded{recording};
    if (!recorded) {
        std::fprintf(stderr, "%s could not be read\n", recording.string().c_str());
        return 1;
    }
    const std::vector<trace::TracedCall> calls = trace::parseTrace(recorded);
    std::ofstream log{directory / "replay.log"};
    int parted = 0;
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image};
        oracle.start();
        for (const trace::TracedCall& call : calls) {
            std::string differences;
            try {
                differences = oracle.replay(call);
            } catch (const std::runtime_error& error) {
                differences = std::string{"could not be rebuilt: "} + error.what() + "\n";
            }
            log << "f" << call.frame << ' ' << call.routine << (differences.empty() ? " agrees\n" : " PARTS\n") << differences;
            if (!differences.empty()) {
                ++parted;
                std::printf("frame %llu, %s:\n%s", static_cast<unsigned long long>(call.frame), call.routine.c_str(),
                            differences.c_str());
            }
        }
    });
    std::printf("%zu calls replayed, %d differing from the cartridge; %s\n", calls.size(), parted,
                (directory / "replay.log").string().c_str());
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

std::vector<std::string> splitNames(std::string_view names) {
    std::vector<std::string> split;
    while (true) {
        const std::size_t comma = names.find(',');
        split.emplace_back(names.substr(0, comma));
        if (comma == std::string_view::npos) {
            return split;
        }
        names.remove_prefix(comma + 1);
    }
}

// What the command line asked for.
struct Options {
    // The modes. At most one; with none, the trace in a window.
    std::optional<std::uint64_t> draws;
    std::optional<std::filesystem::path> inputsToBattle;
    std::optional<std::uint64_t> stateCheckTicks;
    bool replay = false;
    bool headless = false;

    std::optional<std::filesystem::path> from;           // a window or --headless
    std::optional<std::uint64_t> quitAfterFrames;        // a window
    std::optional<std::uint64_t> frames;                 // --headless
    std::optional<std::filesystem::path> inputs;         // --headless
    std::vector<std::uint64_t> pictureFrames;            // --headless
    std::vector<trace::CellWrite> writes;                // --headless
    std::optional<std::filesystem::path> recording;      // --replay
    bool twice = false;                                  // --to-battle
    std::uint32_t seed = trace::kSweepSeed;              // --check
    bool seeded = false;                                 // --check
    std::vector<std::string> only;                       // --check
    bool replayTraced = false;                           // --check
};

constexpr const char* kUsage =
    "usage: ostinato-damage-trace <rom> <directory> [--from <state>] [--quit-after-frames <n>]\n"
    "       ostinato-damage-trace <rom> <directory> --headless --frames <n> [--from <state>] [--inputs <script>]\n"
    "                                               [--picture-at <frame>]... [--write-at <frame> <address> <bytes>]...\n"
    "       ostinato-damage-trace <rom> <directory> --to-battle <inputs> [--twice]\n"
    "       ostinato-damage-trace <rom> <directory> --check-state <ticks>\n"
    "       ostinato-damage-trace <rom> <directory> --check <draws> [--only <routine>[,<routine>]...] [--seed <n>]\n"
    "                                               [--replay-traced]\n"
    "       ostinato-damage-trace <rom> <directory> --replay [--log <file>]\n";

// Why these options cannot go together, or nothing.
std::optional<std::string> refusal(const Options& o) {
    const int modes = (o.draws ? 1 : 0) + (o.inputsToBattle ? 1 : 0) + (o.stateCheckTicks ? 1 : 0) + (o.replay ? 1 : 0) +
                      (o.headless ? 1 : 0);
    if (modes > 1) {
        return "--check, --check-state, --headless, --replay and --to-battle go alone";
    }
    if (o.from && modes > 0 && !o.headless) {
        return "--from goes with the window or --headless";
    }
    if (o.quitAfterFrames && modes > 0) {
        return "--quit-after-frames goes with the window; --headless takes --frames";
    }
    if (o.headless != o.frames.has_value()) {
        return "--headless and --frames go together";
    }
    if (!o.headless && (o.inputs || !o.pictureFrames.empty() || !o.writes.empty())) {
        return "--inputs, --picture-at and --write-at go with --headless";
    }
    if (o.recording && !o.replay) {
        return "--log goes with --replay";
    }
    if (o.twice && !o.inputsToBattle) {
        return "--twice goes with --to-battle";
    }
    if (!o.draws && (o.seeded || !o.only.empty() || o.replayTraced)) {
        return "--only, --seed and --replay-traced go with --check";
    }
    for (const std::string& name : o.only) {
        const bool known = std::any_of(kSweeps.begin(), kSweeps.end(), [&](const Sweep& sweep) { return sweep.name == name; });
        if (!known) {
            std::string message = "no routine is called " + name + "; --only takes";
            for (const Sweep& sweep : kSweeps) {
                message += " ";
                message += sweep.name;
            }
            return message;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<trace::InputStep>> readScript(const std::filesystem::path& file) {
    std::ifstream in{file};
    if (!in) {
        std::fprintf(stderr, "%s could not be read\n", file.string().c_str());
        return std::nullopt;
    }
    try {
        return trace::readInputScript(in);
    } catch (const std::runtime_error& error) {
        std::fprintf(stderr, "%s: %s\n", file.string().c_str(), error.what());
        return std::nullopt;
    }
}

// Runs the mode the options name. A refusal prints one line and answers 1.
int runMode(const Options& options, std::span<const std::uint8_t> image, const std::filesystem::path& romFile,
            const std::filesystem::path& directory) {
    if (options.replay) {
        return runReplay(image, directory, options.recording.value_or(directory / "damage-trace.log"));
    }
    if (options.draws) {
        return runCheck(image, directory,
                        CheckOptions{.draws = static_cast<int>(*options.draws),
                                     .seed = options.seed,
                                     .only = options.only,
                                     .replayTraced = options.replayTraced});
    }
    if (!options.from && !options.inputsToBattle && !options.stateCheckTicks && !options.headless) {
        return runTrace(image, directory, options.quitAfterFrames, std::nullopt);
    }

    const std::optional<GameVersion> cartridge = assets::identifyRom(image);
    if (!cartridge) {
        std::fprintf(stderr, "%s is not a cartridge revision this tool knows\n", romFile.string().c_str());
        return 1;
    }
    if (options.stateCheckTicks) {
        return runStateCheck(image, *cartridge, directory, *options.stateCheckTicks);
    }
    if (options.inputsToBattle) {
        const std::optional<std::vector<trace::InputStep>> script = readScript(*options.inputsToBattle);
        if (!script) {
            return 1;
        }
        return runToBattle(image, *cartridge, directory, *script, options.twice);
    }
    std::optional<StartingState> from;
    if (options.from) {
        from = StartingState{.file = *options.from, .state = trace::readMachineState(*options.from)};
        if (!trace::stateIsOf(from->state, *cartridge)) {
            std::fprintf(stderr, "%s is a state of %s; %s is %s\n", from->file.string().c_str(),
                         std::string{trace::cartridgeName(from->state.cartridge)}.c_str(), romFile.string().c_str(),
                         std::string{trace::cartridgeName(*cartridge)}.c_str());
            return 1;
        }
    }
    if (!options.headless) {
        return runTrace(image, directory, options.quitAfterFrames, from);
    }
    std::vector<trace::InputStep> script;
    if (options.inputs) {
        std::optional<std::vector<trace::InputStep>> read = readScript(*options.inputs);
        if (!read) {
            return 1;
        }
        script = std::move(*read);
    }
    return runHeadless(image, directory, *options.frames, from, script, options.pictureFrames, options.writes);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "%s", kUsage);
        return 2;
    }
    Options options;
    const auto count = [&](int& i, const char* flag) -> std::optional<std::uint64_t> {
        const std::optional<std::uint64_t> parsed = ++i < argc ? parseCount(argv[i]) : std::nullopt;
        if (!parsed) {
            std::fprintf(stderr, "%s takes a number\n%s", flag, kUsage);
        }
        return parsed;
    };
    for (int i = 3; i < argc; ++i) {
        const std::string_view flag{argv[i]};
        if (flag == "--replay") {
            options.replay = true;
        } else if (flag == "--headless") {
            options.headless = true;
        } else if (flag == "--twice") {
            options.twice = true;
        } else if (flag == "--replay-traced") {
            options.replayTraced = true;
        } else if (flag == "--quit-after-frames") {
            if (!(options.quitAfterFrames = count(i, argv[i]))) {
                return 2;
            }
        } else if (flag == "--frames") {
            if (!(options.frames = count(i, argv[i]))) {
                return 2;
            }
        } else if (flag == "--check-state") {
            if (!(options.stateCheckTicks = count(i, argv[i]))) {
                return 2;
            }
        } else if (flag == "--check") {
            if (!(options.draws = count(i, argv[i])) || *options.draws == 0) {
                std::fprintf(stderr, "--check takes a number of draws\n%s", kUsage);
                return 2;
            }
        } else if (flag == "--picture-at") {
            const std::optional<std::uint64_t> frame = count(i, argv[i]);
            if (!frame) {
                return 2;
            }
            options.pictureFrames.push_back(*frame);
        } else if (flag == "--write-at" && i + 3 < argc) {
            try {
                options.writes.push_back(trace::readCellWrite(argv[i + 1], argv[i + 2], argv[i + 3]));
            } catch (const std::runtime_error& error) {
                std::fprintf(stderr, "--write-at: %s\n%s", error.what(), kUsage);
                return 2;
            }
            i += 3;
        } else if (flag == "--seed") {
            const std::optional<std::uint64_t> seed = count(i, argv[i]);
            if (!seed) {
                return 2;
            }
            options.seed = static_cast<std::uint32_t>(*seed);
            options.seeded = true;
        } else if (flag == "--from" && i + 1 < argc) {
            options.from = argv[++i];
        } else if (flag == "--to-battle" && i + 1 < argc) {
            options.inputsToBattle = argv[++i];
        } else if (flag == "--inputs" && i + 1 < argc) {
            options.inputs = argv[++i];
        } else if (flag == "--log" && i + 1 < argc) {
            options.recording = argv[++i];
        } else if (flag == "--only" && i + 1 < argc) {
            options.only = splitNames(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown option %s\n%s", argv[i], kUsage);
            return 2;
        }
    }
    if (const std::optional<std::string> why = refusal(options)) {
        std::fprintf(stderr, "%s\n%s", why->c_str(), kUsage);
        return 2;
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
    try {
        return runMode(options, assets::stripCopierHeader(rom), romFile, directory);
    } catch (const std::runtime_error& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
