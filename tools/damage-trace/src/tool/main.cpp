// ostinato-damage-trace — the cartridge's damage routines watched as the game runs them, or checked
// against the port's formulas. A developer tool for seeing exactly what the game's damage code
// reads and writes, and for proving the port's numbers are the game's; it is not part of the game.
//
//     ostinato-damage-trace <rom> <directory> [--from <state>] [--quit-after-frames <n>]
//     ostinato-damage-trace <rom> <directory> --headless --frames <n> [--from <state>] [--inputs <script>]
//                                             [--picture-at <frame>]... [--write-at <frame> <address> <bytes>]...
//                                             [--watch <address> <count>]... [--seam]
//     ostinato-damage-trace <rom> <directory> --to-battle <inputs> [--twice]
//     ostinato-damage-trace <rom> <directory> --check-state <ticks>
//     ostinato-damage-trace <rom> <directory> --check <draws> [--only <routine>[,<routine>]...] [--seed <n>]
//                                             [--replay-traced]
//     ostinato-damage-trace <rom> <directory> --replay [--log <file>]
//
// Without another mode the cartridge is run on the engine's SNES machine in a window, played from
// the keyboard or a gamepad, and nineteen of its battle routines are traced (trace::DamageTrace):
// escapes on each routine's entry and on the instructions its callers return to open and close a
// call, the attack's context logged at each entry, and watches over every cell the routines read or
// write log each access made inside a call, in order, with its address and byte. The win, the loss
// and the wrap-up are traced whole, as every cell of their ranges on entry and on return, and every
// battle message shown inside a traced call or ending is logged with its variables. Every call is
// logged — nothing is filtered — to `damage-trace.log` under <directory>, in the lines
// trace/trace_log.h names.
//
// The routines: the base damage (`CalcDmg`, $C2:2B9D, returning to $C2:322E), the damage to one
// target (`CalcTargetDmg`, $C2:0B83, returning to $C2:346C from the attack and $C2:3509 from a
// bounce), taking and showing it (`_c262ef`, $C2:62EF, returning to $C2:3473 and $C2:3528), the
// statuses taking effect (`UpdateStatus`, $C2:4391, returning to $C2:324D from the attack, $C2:24A7
// from the battle's start and $C2:50AA from a formation change) and an attack's statuses on one
// target (`MagicStatusEffect`, $C2:4406, returning to $C2:345C and $C2:34F9), and an attack's
// special effect on its attacker (`DoAttackerEffect`, $C2:3E7D, returning to $C2:33A9) and on one
// target (`DoTargetEffect`, $C2:387E, returning to $C2:345F and $C2:34FC); and at a won battle's
// end, the spells an equipped esper teaches learned further (`LearnGenjuMagic`, $C2:602A, returning
// to $C2:5E72) and a level-up (`DoLevelUp`, $C2:60C2, returning to $C2:60B4); and a battler's AI:
// its script readied (`InitAI`, $C2:2D71, returning to $C2:2C3D as a monster is loaded and $C2:3041
// as a scripted battle seats a party member), its turn (`ExecMonsterAction`, $C2:02DC, returning to
// $C2:013C and, for a party member with a script, $C2:21A1), its counterattack (`ExecAIRetal`,
// $C2:4BF4, returning to $C2:4B9A) and the three commands only a script queues (`Cmd_2e`, `Cmd_2f`,
// `Cmd_30`, $C2:1DFA, $C2:1E1A and $C2:1E5E, returning to the command dispatch at $C2:1401); and at
// a won battle's end, the experience added (`AddExp`, $C2:6235, returning to $C2:5E4F and
// $C2:5E59), the spells equipment teaches (`LearnItemMagic`, $C2:5FEF, returning to $C2:5E6A), the
// abilities a level teaches (`LearnAbilities`, $C2:61B6, returning to $C2:60BF) and the rages
// (`LearnRage`, $C2:4A07, returning to $C2:48EE). The addresses are the US cartridges'.
// --quit-after-frames closes the window after that many frames; otherwise close it to quit.
//
// With --from the trace starts at a state written by --to-battle instead of at power-on, on a
// machine with no battery file. A state from another cartridge revision is refused.
//
// With --to-battle nothing opens. The cartridge runs from power-on holding the buttons <inputs>
// names (machine/machine_state.h has the format), and the machine as it stood on the tick before
// the battle program was entered is written to `battle.state` under <directory>, so a trace started
// from it sees the whole battle. A run that passes the script's last line by kRunOutFrames with no
// battle writes the picture it last finished to `to-battle-end.ppm` and fails. --twice runs it
// again and fails if the two states differ by a byte.
//
// With --headless nothing opens. The trace runs for --frames frames from power-on or --from,
// holding the buttons --inputs names, on a machine with no battery file, and writes the picture of
// each --picture-at frame to `frame-<n>.ppm`. Each --write-at writes its bytes into work RAM before
// its frame runs (machine/machine_state.h has the form), and the trace is told of them. Each
// --watch logs the cartridge's every write to <count> cells of work RAM from <address> on, and
// their bytes after every frame, to `cells.log` (CellLog, tool/cell_log.h, has the lines). With
// --seam the run logs, in place of the trace, what the graphics program hands the battle program to
// `seam.log` (SeamLog, tool/seam_log.cpp, has the lines).
//
// With --check-state nothing opens. A machine is run --check-state ticks from power-on, and its
// state is written, read back and restored on a second machine; both run on, and their memory and
// pictures are compared.
//
// With --replay nothing opens. Every call in <directory>/damage-trace.log, or the file --log names,
// is rebuilt as a battle and worked out by the port, and the port's answer is compared with every
// cell the cartridge wrote; every ending is rebuilt from the cells it found and played out by the
// port, compared with the cells it left and the messages it showed. Each is logged to `replay.log`
// with its differences, and any difference fails the run.
//
// With --check nothing opens. The cartridge is started as far as its reset code, its routines are
// bound where it holds them, and <draws> scenarios drawn from a fixed seed (or --seed) are run
// through each routine (or those --only names) and through the port. Every scenario is logged to
// `check.log`; a disagreement is printed and fails the run. --replay-traced also traces the
// cartridge's calls and endings in each draw and replays them against the port.
//
// This file reads the command line and runs the mode it names: the trace in a window is in
// starting_state.cpp, --to-battle in pictures.cpp, --headless and --check-state in seam_log.cpp,
// and --check and --replay in check.cpp.
//
// Keys: arrows = d-pad, X = A, Z = B, S = X, A = Y, Q/W = L/R, Enter = Start, Right Shift = Select.

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "assets/cartridge.h"
#include "machine/machine_state.h"
#include "oracle/damage_oracle.h"
#include "ostinato/game_version.h"
#include "tool/cell_log.h"
#include "tool/check.h"
#include "tool/pictures.h"
#include "tool/seam_log.h"
#include "tool/starting_state.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

namespace {

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
    std::vector<WatchedCells> watched;                   // --headless
    bool seam = false;                                   // --headless
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
    "                                               [--watch <address> <count>]... [--seam]\n"
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
    if (!o.headless && (o.inputs || !o.pictureFrames.empty() || !o.writes.empty() || !o.watched.empty() || o.seam)) {
        return "--inputs, --picture-at, --write-at, --watch and --seam go with --headless";
    }
    if (o.seam && !o.watched.empty()) {
        return "--seam watches every cell --watch would, so they go apart";
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
    return runHeadless(image, directory, *options.frames, from, script, options.pictureFrames, options.writes,
                       options.watched, options.seam);
}

}  // namespace

}  // namespace damage_trace

using namespace damage_trace;

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
        } else if (flag == "--seam") {
            options.seam = true;
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
        } else if (flag == "--watch" && i + 2 < argc) {
            const std::optional<std::uint32_t> at = parseWorkRamAddress(argv[i + 1]);
            const std::optional<std::uint64_t> size = parseCount(argv[i + 2]);
            if (!at || !size || *size == 0 || *at + *size > 0x800000) {
                std::fprintf(stderr, "--watch takes a work RAM address and a number of cells\n%s", kUsage);
                return 2;
            }
            options.watched.push_back(WatchedCells{.at = *at, .size = static_cast<std::uint32_t>(*size)});
            i += 2;
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
