#include "text/dialogue.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "data/text_offsets.h"

namespace ostinato::text {

namespace {

bool isEnglish(const DialogueContext& context) {
    return context.language == TextLanguage::EN;
}

// --- the cursor --------------------------------------------------------------------------------

// One glyph read out of a glyph run, and how many of the run's bytes it spans. In Japanese a
// byte below kFirstSingleByteGlyph opens a two-byte glyph (field/text.asm:481-498).
struct RunGlyph {
    std::uint16_t glyph;
    std::size_t bytes;
};

RunGlyph glyphAt(const GlyphRun& run, std::size_t offset, TextLanguage language) {
    const std::uint8_t first = run.glyphs[offset];
    if (language == TextLanguage::JP && first < kFirstSingleByteGlyph &&
        offset + 1 < run.glyphs.size()) {
        const auto high = static_cast<std::uint16_t>(first - kTwoByteGlyphBias);
        return {static_cast<std::uint16_t>(high << 8 | run.glyphs[offset + 1]), 2};
    }
    return {first, 1};
}

void advancePastToken(DialogueState& state) {
    ++state.token;
    state.glyphOffset = 0;
}

void advanceInRun(DialogueState& state, const GlyphRun& run, std::size_t bytes) {
    state.glyphOffset += bytes;
    if (state.glyphOffset >= run.glyphs.size()) {
        advancePastToken(state);
    }
}

bool spliceDraining(const DialogueState& state) {
    return state.spliceNext < state.splice.size();
}

// --- lines and pages (field/text.asm:966-1018) ---------------------------------------------------

void armPageEnd(DialogueState& state) {
    state.pageClearFrames = kPageClearFrames;
    state.keyState = KeyState::WaitingForRelease;
}

void breakLine(DialogueState& state) {
    state.x = kTextLeft;
    state.line = (state.line + 1) % kLinesPerPage;
    if (state.line == 0) {
        armPageEnd(state);
    }
}

void endPage(DialogueState& state) {
    state.x = kTextLeft;
    state.line = 0;
    armPageEnd(state);
}

// The wipe is over: the page and everything on it are gone.
void clearPage(DialogueState& state) {
    state.placed.clear();
    state.cursor.reset();
    ++state.page;
}

// Draw one glyph at the pen, unless it would reach the right margin — then the line breaks and
// the glyph is lost (field/text.asm:926-934; docs/Bugs.md).
void drawGlyph(DialogueState& state, std::uint16_t glyph, const DialogueContext& context) {
    const int width = context.metrics.width(glyph);
    const PlacedGlyph placement{.page = state.page, .line = state.line, .x = state.x, .glyph = glyph};
    if (state.x + width >= kTextRight) {
        breakLine(state);
        state.droppedAtMargin.push_back(placement);
        return;
    }
    state.placed.push_back(placement);
    state.x += width;
}

// --- splices (field/text.asm:560-812) ------------------------------------------------------------

// A name byte as a dialogue glyph: English names are stored in the menu font's encoding.
std::uint16_t nameGlyph(std::uint8_t byte, const DialogueContext& context) {
    return isEnglish(context) ? static_cast<std::uint8_t>(byte - kMenuToDialogueOffset) : byte;
}

// The glyphs of a name: up to `limit` bytes of `bytes`, ending early at kNameEnd.
std::vector<std::uint16_t> nameGlyphs(std::span<const std::uint8_t> bytes, std::size_t limit,
                                      const DialogueContext& context) {
    std::vector<std::uint16_t> glyphs;
    for (std::size_t i = 0; i < std::min(limit, bytes.size()) && bytes[i] != kNameEnd; ++i) {
        glyphs.push_back(nameGlyph(bytes[i], context));
    }
    return glyphs;
}

std::vector<std::uint16_t> characterNameGlyphs(std::uint8_t character,
                                               const DialogueContext& context) {
    assert(character < kCharacterCount && "character name splice outside the character table");
    const std::span<const std::uint8_t> name = context.names[character];
    if (isEnglish(context)) {
        return nameGlyphs(name, kCharacterNameGlyphs, context);
    }
    // Japanese names splice as two-byte glyphs, each name byte behind the name prefix
    // (field/text.asm:588-601).
    std::vector<std::uint16_t> glyphs;
    for (std::size_t i = 0; i < std::min(kCharacterNameGlyphs, name.size()) && name[i] != kNameEnd;
         ++i) {
        const auto high = static_cast<std::uint16_t>(kJapaneseNameGlyphPrefix - kTwoByteGlyphBias);
        glyphs.push_back(static_cast<std::uint16_t>(high << 8 | name[i]));
    }
    return glyphs;
}

// The item's name record without its first byte, which is the item's icon.
std::span<const std::uint8_t> itemNameLetters(const DialogueContext& context) {
    assert(context.corpus != nullptr && "the item splice needs a corpus");
    return context.corpus->itemName(context.item).subspan(1);
}

std::vector<std::uint16_t> itemNameGlyphs(const DialogueContext& context) {
    return nameGlyphs(itemNameLetters(context), kItemNameGlyphs, context);
}

// The spell splice reads the spell-name table at a stride of 4 and copies 4 bytes; upstream notes
// both should be 7 (field/text.asm:783, :804). Nothing in the shipped game reaches it.
std::vector<std::uint16_t> spellNameGlyphs(const DialogueContext& context) {
    assert(context.corpus != nullptr && "the spell splice needs a corpus");
    const std::span<const std::uint8_t> table = context.corpus->rawBytes(TextClass::MAGIC_NAME);
    const std::size_t start = static_cast<std::size_t>(context.spellIndex) * kSpellNameStride + 1;
    assert(start + kSpellNameGlyphs <= table.size() && "spell splice reads past the table");
    return nameGlyphs(table.subspan(start, kSpellNameGlyphs), kSpellNameGlyphs, context);
}

// The amount's decimal digits, the low kGoldDigits places, without leading zeros
// (field/text.asm:714-739).
std::vector<std::uint16_t> goldGlyphs(const DialogueContext& context) {
    const std::uint16_t zero = isEnglish(context) ? kEnglishDigitGlyph : kJapaneseDigitGlyph;
    std::vector<std::uint16_t> glyphs;
    std::uint32_t place = 1;
    for (std::size_t i = 1; i < kGoldDigits; ++i) {
        place *= 10;
    }
    bool started = false;
    for (std::size_t i = 0; i < kGoldDigits; ++i, place /= 10) {
        const auto digit = static_cast<std::uint16_t>(context.goldAmount / place % 10);
        started = started || digit != 0;
        if (started) {
            glyphs.push_back(static_cast<std::uint16_t>(zero + digit));
        }
    }
    return glyphs;
}

std::vector<std::uint16_t> tabGlyphs(std::uint8_t count, const DialogueContext& context) {
    return std::vector<std::uint16_t>(count, isEnglish(context) ? kSpaceGlyph : kWideSpaceGlyph);
}

// Draw the next queued glyph. Drawing the last moves the cursor past the control that queued them.
void drawSpliceGlyph(DialogueState& state, const DialogueContext& context) {
    const std::uint16_t glyph = state.splice[state.spliceNext++];
    if (!spliceDraining(state)) {
        state.splice.clear();
        state.spliceNext = 0;
        advancePastToken(state);
    }
    drawGlyph(state, glyph, context);
}

// Queue a splice's glyphs and draw the first. A splice with nothing in it draws nothing and the
// frame is spent (docs/contracts/text_engine.md § One step).
void splice(DialogueState& state, std::vector<std::uint16_t> glyphs,
            const DialogueContext& context) {
    if (glyphs.empty()) {
        advancePastToken(state);
        return;
    }
    state.splice = std::move(glyphs);
    state.spliceNext = 0;
    drawSpliceGlyph(state, context);
}

// --- the English word wrap (field/text.asm:189-318) ----------------------------------------------

int glyphsWidth(const std::vector<std::uint16_t>& glyphs, const DialogueContext& context) {
    int width = 0;
    for (const std::uint16_t glyph : glyphs) {
        width += context.metrics.width(glyph);
    }
    return width;
}

// The width of the word at the cursor: the glyphs from the next undrawn one to the next space or
// control, plus a character or item name's own glyphs when the cursor is on the control that
// splices it. While a splice drains the width is 0 (docs/contracts/text_engine.md § Word wrap).
int measureWord(const DialogueState& state, const DialogueContext& context) {
    if (spliceDraining(state)) {
        return 0;
    }
    int width = 0;
    std::size_t offset = state.glyphOffset;
    for (std::size_t t = state.token; t < state.tokens.size(); ++t, offset = 0) {
        if (const auto* run = std::get_if<GlyphRun>(&state.tokens[t])) {
            for (std::size_t i = offset; i < run->glyphs.size(); ++i) {
                if (run->glyphs[i] == kSpaceGlyph) {
                    return width;
                }
                width += context.metrics.width(run->glyphs[i]);
            }
            continue;
        }
        const auto& control = std::get<FieldControl>(state.tokens[t]);
        if (control.command == FieldTextCommand::CHARACTER_NAME) {
            width += glyphsWidth(characterNameGlyphs(control.operand, context), context);
        } else if (control.command == FieldTextCommand::ITEM_NAME) {
            width += glyphsWidth(itemNameGlyphs(context), context);
        }
        return width;
    }
    return width;
}

// --- one step (field/text.asm:438-878) ----------------------------------------------------------

void finishMessage(DialogueState& state) {
    endPage(state);
    state.status = MessageStatus::Finished;
}

void runControl(DialogueState& state, const FieldControl& control,
                const DialogueContext& context) {
    switch (control.command) {
        case FieldTextCommand::END:
            finishMessage(state);
            return;
        case FieldTextCommand::NEWLINE:
            breakLine(state);
            advancePastToken(state);
            return;
        case FieldTextCommand::CHARACTER_NAME:
            splice(state, characterNameGlyphs(control.operand, context), context);
            return;
        case FieldTextCommand::WAIT:
            state.pauseFrames = kSecondFrames;
            advancePastToken(state);
            return;
        case FieldTextCommand::WAIT_FRAMES:
            state.pauseFrames = kQuarterSecondFrames * control.operand;
            advancePastToken(state);
            return;
        case FieldTextCommand::KEY:
            state.keyTimerFrames = kKeyTimerFrames;
            state.keyTimerArmed = true;
            advancePastToken(state);
            return;
        case FieldTextCommand::PAGE:
            endPage(state);
            advancePastToken(state);
            return;
        case FieldTextCommand::TAB: {
            // The fill glyphs draw from the next frame on; this frame only queues them.
            std::vector<std::uint16_t> fill = tabGlyphs(control.operand, context);
            if (fill.empty()) {
                advancePastToken(state);
                return;
            }
            state.splice = std::move(fill);
            state.spliceNext = 0;
            return;
        }
        case FieldTextCommand::CHOICE: {
            DialogueChoices& choices = state.choices;
            const ChoicePosition position{.line = state.line, .x = state.x / kCellPx * kCellPx};
            if (choices.count < choices.positions.size()) {
                choices.positions[choices.count] = position;
            } else {
                choices.positions.push_back(position);
            }
            ++choices.count;
            advancePastToken(state);
            drawGlyph(state, kWideSpaceGlyph, context);
            return;
        }
        case FieldTextCommand::KEY_TIMED:
            state.keyTimerFrames = kQuarterSecondFrames * control.operand;
            state.keyTimerArmed = true;
            state.keyState = KeyState::WaitingForPress;
            advancePastToken(state);
            return;
        case FieldTextCommand::GP:
            splice(state, goldGlyphs(context), context);
            return;
        case FieldTextCommand::ITEM_NAME:
            splice(state, itemNameGlyphs(context), context);
            return;
        case FieldTextCommand::SPELL_NAME:
            splice(state, spellNameGlyphs(context), context);
            return;
    }
}

// Consume one glyph or one control.
void step(DialogueState& state, const DialogueContext& context) {
    if (isEnglish(context) && state.x + measureWord(state, context) >= kTextRight) {
        breakLine(state);
        return;
    }
    if (spliceDraining(state)) {
        drawSpliceGlyph(state, context);
        return;
    }
    if (state.token >= state.tokens.size()) {
        finishMessage(state);
        return;
    }
    if (const auto* run = std::get_if<GlyphRun>(&state.tokens[state.token])) {
        const RunGlyph next = glyphAt(*run, state.glyphOffset, context.language);
        advanceInRun(state, *run, next.bytes);
        drawGlyph(state, next.glyph, context);
        return;
    }
    runControl(state, std::get<FieldControl>(state.tokens[state.token]), context);
}

// --- waiting on the player (field/text.asm:366-426) ----------------------------------------------

// One way the player can move the choice cursor: the buttons that do it and the step it takes.
// Listed in the order the original tests them; the first whose buttons are held is the only one
// tried.
struct ChoiceMove {
    bool (*held)(const DialogueInput&);
    int step;
};

constexpr ChoiceMove kChoiceMoves[] = {
    {[](const DialogueInput& in) { return in.up || in.left; }, -1},
    {[](const DialogueInput& in) { return in.down || in.right; }, +1},
};

bool anyDirection(const DialogueInput& input) {
    return input.up || input.down || input.left || input.right;
}

// Move the selection by the held direction, one step per press, then put the cursor on it.
void moveChoiceCursor(DialogueState& state, const DialogueInput& input) {
    DialogueChoices& choices = state.choices;
    if (!anyDirection(input)) {
        choices.selectionChanging = false;
    } else if (choices.selectionChanging) {
        return;
    } else {
        for (const ChoiceMove& move : kChoiceMoves) {
            if (!move.held(input)) {
                continue;
            }
            const auto target = static_cast<long>(choices.selection) + move.step;
            // Blocked below the first choice and at the count itself (field/text.asm:386-401).
            if (target >= 0 && static_cast<std::size_t>(target) != choices.count &&
                static_cast<std::size_t>(target) < choices.positions.size()) {
                choices.selection = static_cast<std::size_t>(target);
                choices.selectionChanging = true;
            }
            break;
        }
    }
    if (choices.selection < choices.positions.size()) {
        state.cursor = choices.positions[choices.selection];
    }
}

void disarmKeyTimer(DialogueState& state) {
    state.keyTimerArmed = false;
    state.keyTimerFrames = 0;
}

void respondToPlayer(DialogueState& state, const DialogueInput& input) {
    if (state.choices.count >= 2) {
        moveChoiceCursor(state, input);
    }
    if (state.keyState == KeyState::WaitingForRelease) {
        if (!input.confirmHeld) {
            state.keyState = KeyState::WaitingForPress;
        }
        return;
    }
    if (input.confirmHeld) {
        state.keyState = KeyState::Typing;
        state.choices.count = 0;
        disarmKeyTimer(state);
    }
}

// Start typing `tokens` from the top, as a message or a title.
void loadMessage(DialogueState& state, std::vector<FieldTextToken> tokens) {
    state.tokens = std::move(tokens);
    state.token = 0;
    state.glyphOffset = 0;
    state.splice.clear();
    state.spliceNext = 0;
}

bool endsWithEnd(const std::vector<FieldTextToken>& tokens) {
    if (tokens.empty()) {
        return false;
    }
    const auto* last = std::get_if<FieldControl>(&tokens.back());
    return last != nullptr && last->command == FieldTextCommand::END;
}

// The width the original centres a title by (field/text.asm:123-156).
int titleWidth(std::span<const std::uint8_t> record, const DialogueContext& context) {
    int width = 0;
    if (isEnglish(context)) {
        for (std::size_t i = 0; i < record.size() && record[i] != 0; ++i) {
            width += context.metrics.width(record[i]);
        }
        return width;
    }
    for (std::size_t i = 0; i < record.size() && record[i] != 0; ++i) {
        if (record[i] < kFirstSingleByteGlyph) {
            ++i;
        }
        width += kJapaneseTitleGlyphWidth;
    }
    return width;
}

}  // namespace

void initDialogue(DialogueState& state) {
    // The page counter runs on so nothing typed after the reset shares a key with what was
    // there before.
    const std::uint32_t nextPage = state.page + 1;
    state = DialogueState{};
    state.page = nextPage;
}

void startDialogue(DialogueState& state, std::size_t index, const DialogueContext& context) {
    assert(context.corpus != nullptr && "startDialogue needs a corpus");
    const TextCorpus& corpus = *context.corpus;
    const std::size_t count = dialogueOffsets().size();
    assert(index < count && "dialogue index past the script");

    // Read on from the record's first byte to an END, the way the original follows its pointer:
    // an empty record shows the next one's text, and a record with no END continues into the next.
    std::vector<std::uint8_t> bytes;
    std::vector<FieldTextToken> tokens;
    for (std::size_t i = index; i < count; ++i) {
        const std::span<const std::uint8_t> record = corpus.dialogue(i);
        bytes.insert(bytes.end(), record.begin(), record.end());
        tokens = tokenizeDialogue(bytes, corpus.dte(), context.language, context.mte);
        if (endsWithEnd(tokens)) {
            break;
        }
    }
    startDialogue(state, std::move(tokens));
}

void startDialogue(DialogueState& state, std::vector<FieldTextToken> tokens) {
    loadMessage(state, std::move(tokens));
    state.status = MessageStatus::Showing;
}

void tickDialogue(DialogueState& state, const DialogueInput& input,
                  const DialogueContext& context) {
    if (state.titleFrames > 0 && --state.titleFrames == 0) {
        closeMapTitle(state);
    }
    if (state.status == MessageStatus::Idle) {
        return;
    }
    if (state.pauseFrames > 0) {
        --state.pauseFrames;
        return;
    }
    if (state.keyTimerArmed) {
        if (state.keyTimerFrames == 0) {
            disarmKeyTimer(state);
            state.keyState = KeyState::Typing;
            state.choices.count = 0;
        } else {
            --state.keyTimerFrames;
        }
    }
    if (state.keyState != KeyState::Typing) {
        respondToPlayer(state, input);
        return;
    }
    if (state.pageClearFrames > 0) {
        if (--state.pageClearFrames == 0) {
            clearPage(state);
        }
        return;
    }
    if (state.status == MessageStatus::Finished) {
        state.windowCloseRequest = true;
        state.status = MessageStatus::Idle;
        return;
    }
    step(state, context);
}

void showMapTitle(DialogueState& state, std::span<const std::uint8_t> record,
                  const DialogueContext& context) {
    state.titleShowing = true;
    state.titleFrames = kTitleFrames;
    state.x = (kTextRight - titleWidth(record, context)) / 2;

    const DteTable dte = context.corpus != nullptr ? context.corpus->dte() : DteTable{};
    loadMessage(state, tokenizeDialogue(record, dte, context.language, context.mte));
    state.status = MessageStatus::Showing;
    while (state.status != MessageStatus::Finished) {
        step(state, context);
    }
    // The title stays up: no key wait and no wipe. Its END asks for no window close
    // (docs/contracts/text_engine.md § Map title).
    state.keyState = KeyState::Typing;
    state.pageClearFrames = 0;
    state.status = MessageStatus::Idle;
}

void closeMapTitle(DialogueState& state) {
    if (!state.titleShowing) {
        return;
    }
    state.titleShowing = false;
    state.titleFrames = 0;
    state.status = MessageStatus::Idle;
    state.pageClearFrames = kPageClearFrames;
}

bool awaitingKeypress(const DialogueState& state) {
    return state.keyState == KeyState::WaitingForPress;
}

bool windowCloseRequested(DialogueState& state) {
    return std::exchange(state.windowCloseRequest, false);
}

std::size_t selection(const DialogueState& state) {
    return state.choices.selection;
}

void clearSelection(DialogueState& state) {
    state.choices.selection = 0;
}

int halfLinesCleared(const DialogueState& state) {
    if (state.pageClearFrames < 1 || state.pageClearFrames > kWipeSteps) {
        return 0;
    }
    return kPageClearFrames - state.pageClearFrames;
}

bool isWiped(const DialogueState& state, int line, int x) {
    const int halfLine = line * 2 + (x >= kHalfLinePx ? 1 : 0);
    return halfLine < halfLinesCleared(state);
}

}  // namespace ostinato::text
