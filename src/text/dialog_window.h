// The field dialog window: the box a message types into, how many of its rows are showing as it
// opens and closes, where it sits, and the map-title box.
//
// This is logic only. It records what the player sees of the box — closed, opening, open or
// closing, how far, at the top or the bottom of the screen, with or without a box behind the
// text — and the rendering components read that. Advance it with tickDialogWindow() once per
// field frame, after the dialog text.
//
//     DialogWindowState window;
//     initDialogWindow(window);                                 // at map load
//     startDialog(text, 1203, context);                         // an event shows message 1203...
//     openDialogWindow(window, WindowPlacement::Bottom, false); // ...in a box at the bottom
//     while (...) {
//         tickDialog(text, input, context);
//         if (windowCloseRequested(text)) closeDialogWindow(window);
//         tickDialogWindow(window);
//         if (isClosed(window)) { ... }                         // the event moves on
//     }
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/dialog_window.md.
#pragma once

#include <cstdint>

namespace ostinato::text {

// What the window is doing.
enum class WindowPhase : std::uint8_t {
    Closed,
    Opening,  // showing two more rows each frame
    Open,
    Closing,  // showing two fewer rows each frame
};

// Where a message's window goes.
enum class WindowPlacement : std::uint8_t {
    Top,
    Bottom,
};

// The whole of the window's state.
struct DialogWindowState {
    WindowPhase phase = WindowPhase::Closed;

    // The opening steps taken, 0 to kWindowOpenFrames. Each opening frame takes one and each
    // closing frame undoes one; rowsShown() is read from it.
    int step = 0;

    WindowPlacement placement = WindowPlacement::Top;

    // The message shows no box: the text sits where it would, over whatever is behind it.
    bool textOnly = false;

    // The map-title box is up.
    bool titleShowing = false;
};

// Reset, as on entering a map: closed, nothing showing.
void initDialogWindow(DialogWindowState& state);

// Open the window for a message, at `placement`, with no box behind the text when `textOnly`.
// The steps already taken carry on: an open window stays open, and a closing one grows again from
// the rows it is showing.
void openDialogWindow(DialogWindowState& state, WindowPlacement placement, bool textOnly);

// Start closing the window, as the text engine asks at the end of a message.
void closeDialogWindow(DialogWindowState& state);

// Advance the window by one field frame.
void tickDialogWindow(DialogWindowState& state);

// Whether the window is closed — what an event waits for before it moves on.
bool isClosed(const DialogWindowState& state);

// Put the map-title box up, or take it down. Both are instant; taking it down does nothing unless
// it is up.
void openMapTitleWindow(DialogWindowState& state);
void closeMapTitleWindow(DialogWindowState& state);

// How many of the box's rows are showing, always the middle ones: 0 to kDialogWindowRows for a
// message's box, kMapTitleWindowRows while the title box is up.
int rowsShown(const DialogWindowState& state);

// The cell row of the box's top on screen: kDialogWindowTopRow or kDialogWindowBottomRow. The
// title box is always at the top.
int boxTopRow(const DialogWindowState& state);

// The screen y of the top of the text area in this window's box.
int textAreaTop(const DialogWindowState& state);

// The screen y of the top of the map title's text area.
int titleTextAreaTop();

// How far apart the text's lines are while the window is as it is now: kOpeningLinePitch while it
// opens, kLinePitch otherwise.
int linePitch(const DialogWindowState& state);

}  // namespace ostinato::text
