// What a whole battle's graphics program does with time, drawn once and applied to the cartridge's battle
// program and to the port's BattleSim alike: how many frames the opening and each battle script take,
// whether battle time runs on each, and the frame a story countdown runs out on.
//
// A frame is one run of the graphics program's interrupt: the frame counter ($0E) and the play clock's
// frames ($021E) step, and the battle program's clock (UpdateBattleTime) is called unless battle time is
// stopped. On the port's side a frame is one BattleSim::tick, its BattleInput::timeRunning the frame's
// clock.
//
//     const PresentationModel model = drawPresentationModel(seed);
//     for (const Frame frame : model.openingFrames()) { ... }               // the opening ($00)
//     for (const Frame frame : model.scriptFrames(3, stopsTime)) { ... }    // the fourth script ($04)
//
// The loop's frame wait (graphics command $01 from BattleLoop) is always one frame with the clock called.
// The wrap-up's frame waits and the celebration take no frame.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ostinato::trace {

// One run of the interrupt's body, and whether the battle program's clock is called in it.
struct Frame {
    bool clockCalled = true;
    friend bool operator==(const Frame&, const Frame&) = default;
};

struct PresentationModel {
    // What every script's frames are drawn from: a script's frames depend on this and its place among
    // the battle's scripts only.
    std::uint64_t seed = 0;
    // The opening: frames with the interrupt running and no clock call, then frames with the clock
    // called, `openingWaitsMissed` of them falling where a wait took two frames (no clock in the second).
    int openingFramesBeforeClock = 0;
    int openingClockCalls = 0;
    int openingWaitsMissed = 0;
    // The frame, counted from the battle's first, on which a story countdown runs out ($1DD1 bit 5 and
    // BattleState::eventTimerExpired set as the frame begins); none for a battle with no countdown.
    std::optional<std::uint64_t> timerRunsOutAt;

    // The opening's frames, in order.
    [[nodiscard]] std::vector<Frame> openingFrames() const;

    // The frames of the battle's `index`th script (from 0): 1 to kLongestScript of them, the clock called
    // on none when `stopsTime` (a script that plays a battle event other than Gau's and Wrexsoul's,
    // GfxCmd_0f btlgfx_main.asm:38200-38216) and on every one otherwise.
    [[nodiscard]] std::vector<Frame> scriptFrames(std::size_t index, bool stopsTime) const;
};

// The most frames a script takes. Four of them are the script's tail (btlgfx_main.asm:22413-22418), so a
// script under four frames stands for one with the tail turned off.
inline constexpr int kLongestScript = 12;

// The opening as the seven recorded fights measured it (docs/contracts/battle_core.md § What the
// graphics program hands the battle program): 38 runs of the interrupt's body before the first clock
// call, the 38th the first call's own frame, so 37 frames with no clock; then 36 clock calls, two of the
// waits taking two frames; 75 frames in all.
inline constexpr int kMeasuredFramesBeforeClock = 37;
inline constexpr int kMeasuredClockCalls = 36;
inline constexpr int kMeasuredWaitsMissed = 2;

// The opening as measured, and scripts drawn from `seed`: no countdown.
[[nodiscard]] PresentationModel measuredPresentationModel(std::uint64_t seed);

// Every count drawn from `seed`: each of the opening's counts from 0 to twice the measured one, and a
// countdown on one battle in eight, running out within `framesAtMost` frames.
[[nodiscard]] PresentationModel drawPresentationModel(std::uint64_t seed, std::uint64_t framesAtMost);

}  // namespace ostinato::trace
