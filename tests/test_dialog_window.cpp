// The field dialog window, checked against docs/contracts/dialog_window.md.
//
// Most cases drive the window frame by frame and read back how many of the box's rows show, where
// the box is, and how far apart the text's lines are. One case runs a short message through the
// dialog text engine beside the window, in the order the field runs them, and the corpus case runs
// the opening frames of every English message in the cartridge.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "data/text_corpus.h"
#include "data/text_offsets.h"
#include "ostinato/field_text_command.h"
#include "ostinato/text_token.h"
#include "text/dialog.h"
#include "text/dialog_window.h"
#include "text/layout.h"
#include "vanilla_rom.h"

namespace ostinato::text {
namespace {

// A window as an event leaves it on the frame it shows a message, before the frame's step.
DialogWindowState opening(WindowPlacement placement = WindowPlacement::Top,
                          bool textOnly = false) {
    DialogWindowState state;
    initDialogWindow(state);
    openDialogWindow(state, placement, textOnly);
    return state;
}

// A window that has finished opening.
DialogWindowState open() {
    DialogWindowState state = opening();
    for (int frame = 0; frame < kWindowOpenFrames; ++frame) {
        tickDialogWindow(state);
    }
    return state;
}

// --- the states --------------------------------------------------------------------------------

TEST(DialogWindowInit, ClosedWithNothingShowing) {
    DialogWindowState state = open();
    openMapTitleWindow(state);
    initDialogWindow(state);
    EXPECT_EQ(state.phase, WindowPhase::Closed);
    EXPECT_EQ(state.step, 0);
    EXPECT_FALSE(state.titleShowing);
    EXPECT_TRUE(isClosed(state));
    EXPECT_EQ(rowsShown(state), 0);
}

TEST(DialogWindowInit, AClosedWindowStaysClosed) {
    DialogWindowState state;
    initDialogWindow(state);
    for (int frame = 0; frame < 10; ++frame) {
        tickDialogWindow(state);
        EXPECT_TRUE(isClosed(state));
        EXPECT_EQ(rowsShown(state), 0);
    }
}

// --- opening -----------------------------------------------------------------------------------

// Before its first step the window is opening with nothing shown; it then shows 1, 3, 5, 7 and 9
// rows on the five frames from the message's start, and stays open.
TEST(DialogWindowOpen, ShowsOneThreeFiveSevenNineRowsThenStaysOpen) {
    DialogWindowState state = opening();
    EXPECT_EQ(state.phase, WindowPhase::Opening);
    EXPECT_EQ(rowsShown(state), 0);
    EXPECT_FALSE(isClosed(state));

    const std::vector<int> expected{1, 3, 5, 7, 9};
    for (std::size_t frame = 0; frame < expected.size(); ++frame) {
        tickDialogWindow(state);
        EXPECT_EQ(rowsShown(state), expected[frame]) << "frame " << frame;
    }
    EXPECT_EQ(state.phase, WindowPhase::Open);
    for (int frame = 0; frame < 10; ++frame) {
        tickDialogWindow(state);
        EXPECT_EQ(state.phase, WindowPhase::Open);
        EXPECT_EQ(rowsShown(state), kDialogWindowRows);
    }
}

TEST(DialogWindowOpen, IsStillOpeningUntilTheFifthFrame) {
    DialogWindowState state = opening();
    for (int frame = 1; frame < kWindowOpenFrames; ++frame) {
        tickDialogWindow(state);
        EXPECT_EQ(state.phase, WindowPhase::Opening) << "frame " << frame;
    }
    tickDialogWindow(state);
    EXPECT_EQ(state.phase, WindowPhase::Open);
}

TEST(DialogWindowOpen, AnOpenWindowAskedToOpenStaysOpen) {
    DialogWindowState state = open();
    openDialogWindow(state, WindowPlacement::Top, false);
    EXPECT_EQ(state.phase, WindowPhase::Open);
    EXPECT_EQ(rowsShown(state), kDialogWindowRows);
    tickDialogWindow(state);
    EXPECT_EQ(rowsShown(state), kDialogWindowRows);
}

TEST(DialogWindowOpen, RecordsPlacementAndTextOnly) {
    const DialogWindowState state = opening(WindowPlacement::Bottom, true);
    EXPECT_EQ(state.placement, WindowPlacement::Bottom);
    EXPECT_TRUE(state.textOnly);
}

// --- closing -----------------------------------------------------------------------------------

// A close on frame N shows 7, 5, 3, 1 and 0 rows on frames N to N + 4 and is reported closed on
// frame N + 5, one frame after the last row has gone.
TEST(DialogWindowClose, ShowsSevenFiveThreeOneNoneThenIsClosedOneFrameLater) {
    DialogWindowState state = open();
    closeDialogWindow(state);
    EXPECT_EQ(state.phase, WindowPhase::Closing);

    const std::vector<int> expected{7, 5, 3, 1, 0};
    for (std::size_t frame = 0; frame < expected.size(); ++frame) {
        tickDialogWindow(state);
        EXPECT_EQ(rowsShown(state), expected[frame]) << "frame " << frame;
        EXPECT_FALSE(isClosed(state)) << "frame " << frame;
    }
    tickDialogWindow(state);
    EXPECT_TRUE(isClosed(state));
    EXPECT_EQ(state.phase, WindowPhase::Closed);
}

// A window asked to open while it closes grows again from the rows it is showing.
TEST(DialogWindowClose, ReopeningGrowsFromTheRowsShowing) {
    DialogWindowState state = open();
    closeDialogWindow(state);
    tickDialogWindow(state);
    tickDialogWindow(state);
    ASSERT_EQ(rowsShown(state), 5);

    openDialogWindow(state, WindowPlacement::Top, false);
    EXPECT_EQ(state.phase, WindowPhase::Opening);
    EXPECT_EQ(rowsShown(state), 5);
    tickDialogWindow(state);
    EXPECT_EQ(rowsShown(state), 7);
    tickDialogWindow(state);
    EXPECT_EQ(rowsShown(state), 9);
    EXPECT_EQ(state.phase, WindowPhase::Open);
}

// A close asked for while closed spends one frame closing before the window is closed again.
TEST(DialogWindowClose, ClosingAClosedWindowTakesOneFrame) {
    DialogWindowState state;
    initDialogWindow(state);
    closeDialogWindow(state);
    EXPECT_FALSE(isClosed(state));
    EXPECT_EQ(rowsShown(state), 0);
    tickDialogWindow(state);
    EXPECT_TRUE(isClosed(state));
}

// --- text-only ---------------------------------------------------------------------------------

TEST(DialogWindowTextOnly, OpensAndClosesOnTheSameFrames) {
    DialogWindowState boxed = opening(WindowPlacement::Top, false);
    DialogWindowState textOnly = opening(WindowPlacement::Top, true);
    for (int frame = 0; frame < 8; ++frame) {
        tickDialogWindow(boxed);
        tickDialogWindow(textOnly);
        EXPECT_EQ(rowsShown(textOnly), rowsShown(boxed)) << "opening frame " << frame;
        EXPECT_EQ(linePitch(textOnly), linePitch(boxed)) << "opening frame " << frame;
    }
    closeDialogWindow(boxed);
    closeDialogWindow(textOnly);
    for (int frame = 0; frame < 6; ++frame) {
        tickDialogWindow(boxed);
        tickDialogWindow(textOnly);
        EXPECT_EQ(rowsShown(textOnly), rowsShown(boxed)) << "closing frame " << frame;
        EXPECT_EQ(isClosed(textOnly), isClosed(boxed)) << "closing frame " << frame;
    }
    EXPECT_TRUE(isClosed(textOnly));
}

// --- placement ---------------------------------------------------------------------------------

TEST(DialogWindowPlacement, TopBoxAtRowOneBottomBoxAtRowEighteen) {
    EXPECT_EQ(boxTopRow(opening(WindowPlacement::Top)), 1);
    EXPECT_EQ(boxTopRow(opening(WindowPlacement::Bottom)), 18);
    EXPECT_EQ(boxTopRow(opening(WindowPlacement::Top)) * kWindowCellPx, 8);
    EXPECT_EQ(boxTopRow(opening(WindowPlacement::Bottom)) * kWindowCellPx, 144);
    EXPECT_EQ(kDialogWindowRows * kWindowCellPx, 72);
}

TEST(DialogWindowPlacement, TextAreaIsFourRowsIntoTheBox) {
    EXPECT_EQ(textAreaTop(opening(WindowPlacement::Top)), 12);
    EXPECT_EQ(textAreaTop(opening(WindowPlacement::Bottom)), 148);
    EXPECT_EQ(kTextAreaLeft, 16);
    // The pen starts kTextLeft in, so the first ink is at x 20.
    EXPECT_EQ(kTextAreaLeft + kTextLeft, 20);
}

// --- the map-title box -------------------------------------------------------------------------

TEST(DialogWindowTitle, InstantFourRowsAtTheTop) {
    DialogWindowState state;
    initDialogWindow(state);
    state.placement = WindowPlacement::Bottom;  // the title box ignores the message placement
    openMapTitleWindow(state);
    EXPECT_TRUE(state.titleShowing);
    EXPECT_EQ(rowsShown(state), 4);
    EXPECT_EQ(boxTopRow(state), kDialogWindowTopRow);
    tickDialogWindow(state);
    EXPECT_EQ(rowsShown(state), 4);
}

TEST(DialogWindowTitle, CloseIsInstantAndDoesNothingWhenNoTitleIsUp) {
    DialogWindowState state;
    initDialogWindow(state);
    openMapTitleWindow(state);
    closeMapTitleWindow(state);
    EXPECT_FALSE(state.titleShowing);
    EXPECT_EQ(rowsShown(state), 0);

    DialogWindowState dialog = open();
    closeMapTitleWindow(dialog);
    EXPECT_EQ(dialog.phase, WindowPhase::Open);
    EXPECT_EQ(rowsShown(dialog), kDialogWindowRows);
}

// The title's ink is on rows 10-20 of its box, the shadow on row 21.
TEST(DialogWindowTitle, TitleInkStartsTenRowsIntoItsBox) {
    const int boxTop = kDialogWindowTopRow * kWindowCellPx;
    const int inkTop = titleTextAreaTop() + kGlyphRowOffset - boxTop;
    EXPECT_EQ(inkTop, 10);
    EXPECT_EQ(inkTop + kGlyphRows - 1, 20);
    EXPECT_EQ(inkTop + kGlyphRows - 1 + kShadowDy, 21);
}

// --- where the text sits -----------------------------------------------------------------------

// The row of the box line `line`'s ink starts on, for a window as it is now.
int inkRowInBox(const DialogWindowState& state, int line) {
    return kTextAreaTopInBox + kGlyphRowOffset + line * linePitch(state);
}

TEST(DialogWindowText, LinesAreFifteenApartOnceOpen) {
    const DialogWindowState state = open();
    EXPECT_EQ(linePitch(state), 15);
    EXPECT_EQ(inkRowInBox(state, 0), 8);
    EXPECT_EQ(inkRowInBox(state, 1), 23);
    EXPECT_EQ(inkRowInBox(state, 2), 38);
    EXPECT_EQ(inkRowInBox(state, 3), 53);
}

TEST(DialogWindowText, LinesAreSixteenApartWhileOpening) {
    DialogWindowState state = opening();
    EXPECT_EQ(linePitch(state), 16);
    for (int frame = 1; frame < kWindowOpenFrames; ++frame) {
        tickDialogWindow(state);
        EXPECT_EQ(linePitch(state), 16) << "frame " << frame;
        EXPECT_EQ(inkRowInBox(state, 0), 8);
        EXPECT_EQ(inkRowInBox(state, 3), 56);
    }
    tickDialogWindow(state);
    EXPECT_EQ(linePitch(state), 15);
}

TEST(DialogWindowText, LinesStayFifteenApartWhileClosing) {
    DialogWindowState state = open();
    closeDialogWindow(state);
    for (int frame = 0; frame < kWindowOpenFrames; ++frame) {
        tickDialogWindow(state);
        EXPECT_EQ(linePitch(state), 15) << "frame " << frame;
    }
}

// The fourth line's shadow row is the first row of the bottom border.
TEST(DialogWindowText, FourthLineShadowLiesOnTheBottomBorder) {
    const DialogWindowState state = open();
    const int shadowRow = inkRowInBox(state, 3) + kGlyphRows - 1 + kShadowDy;
    EXPECT_EQ(shadowRow, (kDialogWindowRows - 1) * kWindowCellPx);
}

// --- beside the text engine --------------------------------------------------------------------

// A message and its window run in the field's order: the event starts both, then each frame the
// text steps, its close request reaches the window, and the window steps. The box is fully open
// before the message asks to close, and the event is released five frames after the request.
TEST(DialogWindowWithText, AShortMessageClosesAFullyOpenBox) {
    std::vector<std::uint8_t> widths(256, 8);
    DialogContext context;
    context.metrics = FontMetrics{widths};
    DialogState text;
    initDialog(text);
    DialogWindowState window;
    initDialogWindow(window);

    startDialog(text, {GlyphRun{{0x20}}, FieldControl{FieldTextCommand::END, 0}});
    openDialogWindow(window, WindowPlacement::Top, false);

    int requestFrame = -1;
    int closedFrame = -1;
    for (int frame = 0; frame < 100 && closedFrame < 0; ++frame) {
        tickDialog(text, DialogInput{.confirmHeld = awaitingKeypress(text)}, context);
        if (windowCloseRequested(text)) {
            EXPECT_EQ(window.phase, WindowPhase::Open);
            closeDialogWindow(window);
            requestFrame = frame;
        }
        tickDialogWindow(window);
        if (isClosed(window)) {
            closedFrame = frame;
        }
    }
    ASSERT_GE(requestFrame, 0);
    EXPECT_GE(requestFrame, 10);
    EXPECT_EQ(closedFrame, requestFrame + 5);
}

// --- the whole English script ------------------------------------------------------------------

class DialogWindowCorpusTest : public ::testing::Test {
protected:
    inline static test::IngestedCartridge cartridge_;

    static void SetUpTestSuite() { cartridge_ = test::ingestVanilla(); }

    static DialogContext context() {
        DialogContext c;
        c.metrics = cartridge_.content->dialogFont.metrics;
        c.corpus = &cartridge_.content->text;
        for (std::size_t i = 0; i < kCharacterCount; ++i) {
            c.names[i] = cartridge_.content->text.charName(i);
        }
        return c;
    }
};

// How many English messages type below their first line while the box is still opening — the
// messages whose lower lines visibly rise when it finishes.
constexpr std::size_t kMessagesTypingBelowLineZeroWhileOpening = 205;

TEST_F(DialogWindowCorpusTest, MessagesTypingBelowTheFirstLineWhileTheBoxOpens) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const DialogContext c = context();
    std::size_t messages = 0;
    for (std::size_t index = 0; index < dialogOffsets().size(); ++index) {
        DialogState text;
        initDialog(text);
        DialogWindowState window;
        initDialogWindow(window);
        startDialog(text, index, c);
        openDialogWindow(window, WindowPlacement::Top, false);
        bool below = false;
        while (!below && window.phase == WindowPhase::Opening) {
            tickDialog(text, DialogInput{}, c);
            tickDialogWindow(window);
            if (window.phase != WindowPhase::Opening) {
                break;  // this frame's text is drawn 15 apart
            }
            for (const PlacedGlyph& g : text.placed) {
                below = below || g.line > 0;
            }
        }
        messages += below ? 1 : 0;
    }
    EXPECT_EQ(messages, kMessagesTypingBelowLineZeroWhileOpening);
}

}  // namespace
}  // namespace ostinato::text
