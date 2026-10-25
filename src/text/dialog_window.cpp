#include "text/dialog_window.h"

#include "text/layout.h"

namespace ostinato::text {

void initDialogWindow(DialogWindowState& state) {
    state = DialogWindowState{};
}

void openDialogWindow(DialogWindowState& state, WindowPlacement placement, bool textOnly) {
    state.placement = placement;
    state.textOnly = textOnly;
    state.phase = state.step == kWindowOpenFrames ? WindowPhase::Open : WindowPhase::Opening;
}

void closeDialogWindow(DialogWindowState& state) {
    state.phase = WindowPhase::Closing;
}

// One frame of UpdateDlgWindow (field/window.asm:238-439).
void tickDialogWindow(DialogWindowState& state) {
    switch (state.phase) {
    case WindowPhase::Closed:
    case WindowPhase::Open:
        return;
    case WindowPhase::Opening:
        if (++state.step == kWindowOpenFrames) {
            state.phase = WindowPhase::Open;
        }
        return;
    case WindowPhase::Closing:
        if (state.step == 0) {
            state.phase = WindowPhase::Closed;
        } else {
            --state.step;
        }
        return;
    }
}

bool isClosed(const DialogWindowState& state) {
    return state.phase == WindowPhase::Closed;
}

void openMapTitleWindow(DialogWindowState& state) {
    state.titleShowing = true;
}

void closeMapTitleWindow(DialogWindowState& state) {
    state.titleShowing = false;
}

int rowsShown(const DialogWindowState& state) {
    if (state.titleShowing) {
        return kMapTitleWindowRows;
    }
    // The first step shows the middle row; each one after it adds a row above and a row below.
    return state.step == 0 ? 0 : 2 * state.step - 1;
}

int boxTopRow(const DialogWindowState& state) {
    if (state.titleShowing || state.placement == WindowPlacement::Top) {
        return kDialogWindowTopRow;
    }
    return kDialogWindowBottomRow;
}

int textAreaTop(const DialogWindowState& state) {
    const int row = state.placement == WindowPlacement::Top ? kDialogWindowTopRow
                                                             : kDialogWindowBottomRow;
    return row * kWindowCellPx + kTextAreaTopInBox;
}

int titleTextAreaTop() {
    return kDialogWindowTopRow * kWindowCellPx + kTitleTextAreaTopInBox;
}

int linePitch(const DialogWindowState& state) {
    return state.phase == WindowPhase::Opening ? kOpeningLinePitch : kLinePitch;
}

}  // namespace ostinato::text
