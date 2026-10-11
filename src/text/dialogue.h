// The field dialogue text engine: the state machine that types a message into the dialogue
// window one glyph per frame, breaks its lines, pauses where the script says, waits on the
// player between pages, runs the multiple-choice cursor, and types the map title when a map
// opens.
//
// This is logic only. It holds no picture: it records where each glyph went (`placed`), where
// the choice cursor is, and how far the page wipe has got, and the rendering components read
// that. Advance it with tickDialogue() once per field frame — nothing about dialogue changes
// anywhere else.
//
//     DialogueState state;
//     initDialogue(state);                          // at map load
//     startDialogue(state, 1203, context);          // an event shows message 1203
//     while (...) {
//         tickDialogue(state, input, context);      // once per frame
//         if (windowCloseRequested(state)) { ... }  // the message is done
//     }
//
// The behaviour, and the line in the original each rule comes from, is specified in
// docs/contracts/text_engine.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "data/text_codec.h"
#include "data/text_corpus.h"
#include "ostinato/item_id.h"
#include "ostinato/text_token.h"
#include "text/font_metrics.h"
#include "text/layout.h"

namespace ostinato::text {

// What the engine is waiting for from the player.
enum class KeyState : std::uint8_t {
    Typing,             // not waiting: the message types
    WaitingForPress,    // A must be pressed to continue
    WaitingForRelease,  // A must be released first, then pressed
};

// Whether a message is being shown.
enum class MessageStatus : std::uint8_t {
    Idle,      // nothing to type
    Showing,   // a message is typing, paused, or waiting on the player
    Finished,  // the message's END has been reached; the next frame asks for the window to close
};

// Where a choice's cursor arrow goes: the line, and the pen x of the left edge of the 16-pixel
// column the choice marker was in. The arrow snaps to the column, not to the marker's own pixel.
struct ChoicePosition {
    int line = 0;
    int x = 0;

    friend bool operator==(const ChoicePosition&, const ChoicePosition&) = default;
};

// The multiple-choice state. Only the first `count` positions belong to the message showing now.
struct DialogueChoices {
    std::size_t count = 0;      // choices recorded since the last time the count was cleared
    std::size_t selection = 0;  // which one is selected; kept until clearSelection()
    std::vector<ChoicePosition> positions;
    bool selectionChanging = false;  // a direction is held and has already moved the selection
};

// One glyph on the current page: which page, line and pen x it was drawn at, and its code.
struct PlacedGlyph {
    std::uint32_t page = 0;
    int line = 0;
    int x = 0;
    std::uint16_t glyph = 0;

    friend bool operator==(const PlacedGlyph&, const PlacedGlyph&) = default;
};

// The player's buttons this frame, as held levels (not presses). `confirmHeld` is A after the
// player's button mapping; the directions are the D-pad.
struct DialogueInput {
    bool confirmHeld = false;
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
};

// Everything a message reads from outside the engine: the language, the font's widths, the
// text corpus, and what the splices insert.
struct DialogueContext {
    TextLanguage language = TextLanguage::EN;
    FontMetrics metrics;

    // The corpus messages, item names and spell names come from. Required by startDialogue()
    // and by the item and spell splices.
    const TextCorpus* corpus = nullptr;

    // The Japanese multi-tile table; unused in English.
    MteTable mte;

    // Each character's current name, in the menu font's encoding, up to kCharacterNameGlyphs
    // bytes and ended early by kNameEnd. A CHARACTER_NAME control splices `names[n]`.
    std::array<std::span<const std::uint8_t>, kCharacterCount> names{};

    // The item an ITEM_NAME control names — the one just found in a chest.
    ItemId item{};

    // The index a SPELL_NAME control reads the spell-name table at.
    std::uint8_t spellIndex = 0;

    // The amount a GP control prints — the gold just found, not the party's total. Seven digits
    // at most; an amount of 0 is the empty splice (docs/contracts/text_engine.md § One step).
    std::uint32_t goldAmount = 0;
};

// The whole of the engine's state. Every field is read by the rendering components or by the
// event system; only the functions below write it.
struct DialogueState {
    // --- the message ---------------------------------------------------------------------------
    std::vector<FieldTextToken> tokens;  // the message being typed
    std::size_t token = 0;               // the cursor: which token...
    std::size_t glyphOffset = 0;         // ...and, in a glyph run, which byte of it

    // A splice's glyphs waiting to be drawn, one per frame. While any wait, the cursor stays on
    // the control that queued them.
    std::vector<std::uint16_t> splice;
    std::size_t spliceNext = 0;

    MessageStatus status = MessageStatus::Idle;
    KeyState keyState = KeyState::Typing;

    // Frames left of a WAIT / WAIT_FRAMES pause.
    int pauseFrames = 0;

    // The key timer KEY and KEY_TIMED arm. When it runs out while armed, the engine stops
    // waiting on the player and clears the choice count.
    int keyTimerFrames = 0;
    bool keyTimerArmed = false;

    // Frames left until a dismissed page's text is gone and the next page may type,
    // kPageClearFrames down to 0. The text leaves half a line at a time on the way; see
    // halfLinesCleared() and isWiped().
    int pageClearFrames = 0;

    // --- the pen -------------------------------------------------------------------------------
    int x = kTextLeft;
    int line = 0;
    std::uint32_t page = 0;  // counts every page this state has shown

    // --- what is on screen ---------------------------------------------------------------------
    std::vector<PlacedGlyph> placed;  // every glyph on the current page, in drawing order

    DialogueChoices choices;

    // Where the choice cursor is, if it is on screen. It appears once the engine waits on the
    // player with two or more choices, follows the selection, and goes with the page's wipe.
    std::optional<ChoicePosition> cursor;

    // --- the map title -------------------------------------------------------------------------
    bool titleShowing = false;
    int titleFrames = 0;

    // --- for the event system ------------------------------------------------------------------
    bool windowCloseRequest = false;  // read and cleared by windowCloseRequested()

    // Every glyph dropped at the right margin since the last initDialogue(), with where it would
    // have gone (docs/Bugs.md). A space ending a line is dropped this way routinely and shows
    // nothing; anything else is text the player does not see.
    std::vector<PlacedGlyph> droppedAtMargin;
};

// Reset everything, as on entering a map: no message, an empty text area, the pen at the top
// left, no choices.
void initDialogue(DialogueState& state);

// Begin typing a message. Only the message changes: the pen, the page and the choice selection
// carry on from wherever the last message left them (a finished message has already reset the
// pen to the top left).
//
// The index form reads dialogue `index` from `context.corpus` the way the original does — from
// the record's first byte until an END, running on into the following record when a record has
// none. The token form takes an already-decoded message.
void startDialogue(DialogueState& state, std::size_t index, const DialogueContext& context);
void startDialogue(DialogueState& state, std::vector<FieldTextToken> tokens);

// Advance the engine by one field frame.
void tickDialogue(DialogueState& state, const DialogueInput& input,
                  const DialogueContext& context);

// Type a map title, once the caller has decided one is shown. The whole title is placed at
// once, centred on line 0, and stays up for kTitleFrames frames. `record` is the title's raw
// bytes (TextCorpus::mapTitle()).
void showMapTitle(DialogueState& state, std::span<const std::uint8_t> record,
                  const DialogueContext& context);

// Take the map title down early, as any event starting does. Does nothing unless a title is
// showing. Stops any message, and leaves the text area to be wiped when the next message
// starts.
void closeMapTitle(DialogueState& state);

// Whether the message has fully displayed the current page and is waiting for A.
bool awaitingKeypress(const DialogueState& state);

// Whether a message finished this frame and wants its window closed. Reading it clears it.
bool windowCloseRequested(DialogueState& state);

// The choice the player selected, and forgetting it once acted on.
std::size_t selection(const DialogueState& state);
void clearSelection(DialogueState& state);

// How many half-lines of a dismissed page have gone so far, in the order they go: line 0 left,
// line 0 right, line 1 left, … line 3 right. 0 while nothing is being wiped.
int halfLinesCleared(const DialogueState& state);

// Whether the wipe has already taken the half-line that pen x on `line` falls in — that is,
// whether a glyph or cursor placed there is no longer shown.
bool isWiped(const DialogueState& state, int line, int x);

}  // namespace ostinato::text
