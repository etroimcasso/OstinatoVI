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
// A frame wait (graphics command $01) is always one frame with the clock called: the loop's, and each of
// the wrap-up's 32, whose port side is one Wait script of 32 frames taking no place among the scripts.
// The celebration ($08), when the fanfare plays, takes the menus' closing frames and the fanfare's two
// with the clock called on each, then battle event 1's script frames with time stopped, in its place among
// the scripts; the port's side gives the same frames to the script holding event 1 that a won battle's end
// asks for. When the fanfare does not play the celebration takes no frames on either side. A script
// holding the final battle's scroll stops time, as one playing a battle event does.
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
    // The frames the celebration's menus take to close (_c10105 btlgfx_main.asm:229-237): a frame wait,
    // again while a menu is still open, so at least one.
    int celebrationMenuCloseFrames = 1;
    // The frame, counted from the battle's first, on which a story countdown runs out ($1DD1 bit 5 and
    // BattleState::eventTimerExpired set as the frame begins); none for a battle with no countdown.
    std::optional<std::uint64_t> timerRunsOutAt;
    // The frame, counted from the battle's first, from which the player holds the run buttons (L and R)
    // to the battle's end; none for a battle in which they are never held.
    std::optional<std::uint64_t> runButtonsHeldFrom;

    // Whether the party runs on `frame`, as the graphics program's interrupt sets $2F45
    // (btlgfx_main.asm:1603-1614) and BattleState::partyRunning stands for it: the run buttons are held,
    // the formation allows running (FormationAux::runningDisabled(), $2F4B bit 0), the graphics program
    // has not stopped the running (`runningStopped`, w7e629a: from the celebration's first frame to the
    // battle's close, btlgfx_main.asm:245, and from the final battle's scroll to the next stage's first loop
    // step, :137 to :22102), and battle time runs on the frame.
    [[nodiscard]] bool partyRuns(std::uint64_t frame, bool runningDisabled, bool runningStopped, Frame played) const {
        return runButtonsHeldFrom && frame >= *runButtonsHeldFrom && !runningDisabled && !runningStopped &&
               played.clockCalled;
    }

    // The opening's frames, in order.
    [[nodiscard]] std::vector<Frame> openingFrames() const;

    // The frames of the battle's `index`th script (from 0): 1 to kLongestScript of them, the clock called
    // on none when `stopsTime` — a script that plays a battle event other than Gau's and Wrexsoul's
    // (GfxCmd_0f btlgfx_main.asm:38200-38216), one holding the final battle's scroll (BtlGfx_0c :135 to
    // GfxCmd_12's :22104), and the celebration — and on every one otherwise.
    [[nodiscard]] std::vector<Frame> scriptFrames(std::size_t index, bool stopsTime) const;

    // The celebration's frames before battle event 1, the clock called on each: the menus closing, then
    // the two frame waits as the fanfare starts (BtlGfx_08 btlgfx_main.asm:246, :257-258).
    [[nodiscard]] std::vector<Frame> celebrationFramesBeforeEvent() const;
};

// The frame waits the celebration makes as the fanfare starts.
inline constexpr int kCelebrationFanfareFrames = 2;
// The most frames the drawn model gives the celebration's menus to close.
inline constexpr int kLongestMenuClose = 8;

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

// The opening as measured, scripts drawn from `seed`, the celebration's menus closed in one frame, and no
// countdown.
[[nodiscard]] PresentationModel measuredPresentationModel(std::uint64_t seed);

// Every count drawn from `seed`: each of the opening's counts from 0 to twice the measured one, the
// celebration's menus closing in 1 to kLongestMenuClose frames, and a countdown on one battle in eight,
// running out within `framesAtMost` frames.
[[nodiscard]] PresentationModel drawPresentationModel(std::uint64_t seed, std::uint64_t framesAtMost);

}  // namespace ostinato::trace
