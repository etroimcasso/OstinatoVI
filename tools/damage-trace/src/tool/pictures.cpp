// writePicture() and runToBattle(): pictures written to files, and a scripted run from power-on to
// a battle's entry.

#include "tool/pictures.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "retropp/guest_escape.h"
#include "retropp/raster_content.h"
#include "retropp/snes.h"
#include "retropp/vm.h"

#include "machine/machine_state.h"
#include "oracle/damage_oracle.h"
#include "ostinato/game_version.h"
#include "tool/starting_state.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

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
namespace {

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

}  // namespace

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

}  // namespace damage_trace
