// The field dialogue text engine, checked against docs/contracts/text_engine.md.
//
// Most cases drive the engine frame by frame over a small hand-built message, with a synthetic
// font in which every glyph is 8 pixels wide except the ones the contract names (the space $7F
// is 5, the wide space $FF is 6) and the digit 1, made 12 wide so a gold amount can land a glyph
// exactly on the margin; pen positions can then be read off directly.
// The corpus cases then run every English message in the cartridge to completion with the real
// font widths and check what holds across the whole script.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "data/text_codec.h"
#include "data/text_corpus.h"
#include "data/text_offsets.h"
#include "ostinato/field_text_command.h"
#include "ostinato/item_id.h"
#include "ostinato/rom_asset.h"
#include "ostinato/text_class.h"
#include "ostinato/text_token.h"
#include "text/dialogue.h"
#include "text/layout.h"
#include "vanilla_rom.h"

namespace ostinato::text {
namespace {

// --- a synthetic font and message builders -------------------------------------------------------

constexpr int kGlyphWidth = 8;
constexpr int kSpaceWidth = 5;
constexpr int kWideSpaceWidth = 6;
constexpr int kWideDigitWidth = 12;

// Every glyph used below. Their values only matter where the contract names them.
constexpr std::uint8_t kA = 0x20;
constexpr std::uint8_t kB = 0x21;
constexpr std::uint8_t kC = 0x22;
constexpr std::uint8_t kApostrophe = 0x40;
constexpr std::uint8_t kS = 0x41;

std::vector<std::uint8_t> syntheticWidths(std::size_t count) {
    std::vector<std::uint8_t> widths(count, kGlyphWidth);
    widths[kSpaceGlyph] = kSpaceWidth;
    widths[kWideSpaceGlyph] = kWideSpaceWidth;
    widths[kEnglishDigitGlyph + 1] = kWideDigitWidth;
    return widths;
}

FieldTextToken run(std::vector<std::uint8_t> glyphs) { return GlyphRun{std::move(glyphs)}; }
FieldTextToken control(FieldTextCommand command, std::uint8_t operand = 0) {
    return FieldControl{command, operand};
}
FieldTextToken end() { return control(FieldTextCommand::END); }

// `count` copies of glyph A.
std::vector<std::uint8_t> repeated(std::size_t count, std::uint8_t glyph = kA) {
    return std::vector<std::uint8_t>(count, glyph);
}

const DialogueInput kNothing{};
const DialogueInput kConfirm{.confirmHeld = true};
const DialogueInput kUp{.up = true};
const DialogueInput kDown{.down = true};
const DialogueInput kLeft{.left = true};
const DialogueInput kRight{.right = true};

// A dialogue engine over the synthetic font, plus the bits of context a case sets.
struct Harness {
    std::vector<std::uint8_t> widths = syntheticWidths(256);
    DialogueContext context;
    DialogueState state;

    explicit Harness(TextLanguage language = TextLanguage::EN) {
        context.language = language;
        if (language == TextLanguage::JP) {
            widths = syntheticWidths(1280);
        }
        context.metrics = FontMetrics{widths};
        initDialogue(state);
    }

    void start(std::vector<FieldTextToken> tokens) { startDialogue(state, std::move(tokens)); }
    void tick(const DialogueInput& input = kNothing) { tickDialogue(state, input, context); }
    void ticks(int count, const DialogueInput& input = kNothing) {
        for (int i = 0; i < count; ++i) {
            tick(input);
        }
    }

    // Release then press A: the two frames a page end needs.
    void dismissPage() {
        tick(kNothing);
        tick(kConfirm);
    }

    std::size_t placedCount() const { return state.placed.size(); }
    const PlacedGlyph& lastPlaced() const { return state.placed.back(); }
};

// --- the per-frame order and pacing ----------------------------------------------------------------

TEST(DialogueInit, ResetsPenAndText) {
    Harness h;
    EXPECT_EQ(h.state.status, MessageStatus::Idle);
    EXPECT_EQ(h.state.x, kTextLeft);
    EXPECT_EQ(h.state.line, 0);
    EXPECT_TRUE(h.state.placed.empty());
    EXPECT_EQ(h.state.keyState, KeyState::Typing);
    EXPECT_EQ(h.state.pageClearFrames, 0);
}

TEST(DialoguePacing, OneGlyphPerFrameAdvancingByWidth) {
    Harness h;
    h.start({run({kA, kB, kC}), end()});
    EXPECT_EQ(h.state.status, MessageStatus::Showing);

    h.tick();
    ASSERT_EQ(h.placedCount(), 1u);
    EXPECT_EQ(h.lastPlaced(), (PlacedGlyph{.page = h.state.page, .line = 0, .x = 4, .glyph = kA}));
    h.tick();
    EXPECT_EQ(h.lastPlaced().x, 4 + kGlyphWidth);
    h.tick();
    EXPECT_EQ(h.lastPlaced().x, 4 + 2 * kGlyphWidth);
    EXPECT_EQ(h.state.x, 4 + 3 * kGlyphWidth);
}

TEST(DialoguePacing, IdleEngineDoesNothing) {
    Harness h;
    h.ticks(10);
    EXPECT_TRUE(h.state.placed.empty());
    EXPECT_FALSE(windowCloseRequested(h.state));
}

// The full end-of-message sequence: END, release, press, nine wipe frames, then the close
// request on the frame after.
TEST(DialoguePacing, EndWaitsForReleaseThenPressThenWipesThenRequestsClose) {
    Harness h;
    h.start({run({kA}), end()});
    h.tick();  // A
    h.tick();  // END
    EXPECT_EQ(h.state.status, MessageStatus::Finished);
    EXPECT_EQ(h.state.keyState, KeyState::WaitingForRelease);
    EXPECT_EQ(h.state.pageClearFrames, kPageClearFrames);
    EXPECT_FALSE(awaitingKeypress(h.state));

    h.tick(kNothing);
    EXPECT_EQ(h.state.keyState, KeyState::WaitingForPress);
    EXPECT_TRUE(awaitingKeypress(h.state));
    h.tick(kConfirm);
    EXPECT_EQ(h.state.keyState, KeyState::Typing);
    EXPECT_EQ(h.state.pageClearFrames, kPageClearFrames);  // the press frame clears nothing

    for (int remaining = kPageClearFrames - 1; remaining >= 0; --remaining) {
        h.tick();
        EXPECT_EQ(h.state.pageClearFrames, remaining);
        EXPECT_FALSE(windowCloseRequested(h.state));
    }
    EXPECT_TRUE(h.state.placed.empty());

    h.tick();
    EXPECT_TRUE(windowCloseRequested(h.state));
    EXPECT_FALSE(windowCloseRequested(h.state));  // read once
    EXPECT_EQ(h.state.status, MessageStatus::Idle);
}

TEST(DialoguePacing, HeldConfirmDoesNotDismissAPage) {
    Harness h;
    h.start({run({kA}), end()});
    h.ticks(2, kConfirm);  // A held all the way through the page end
    h.ticks(20, kConfirm);
    EXPECT_EQ(h.state.keyState, KeyState::WaitingForRelease);
    EXPECT_EQ(h.state.pageClearFrames, kPageClearFrames);
}

TEST(DialoguePacing, WaitPausesSixtyFrames) {
    Harness h;
    h.start({control(FieldTextCommand::WAIT), run({kA}), end()});
    h.tick();  // WAIT
    h.ticks(kSecondFrames);
    EXPECT_TRUE(h.state.placed.empty());
    h.tick();
    EXPECT_EQ(h.placedCount(), 1u);
}

TEST(DialoguePacing, WaitFramesPausesFifteenTimesItsOperand) {
    Harness h;
    h.start({control(FieldTextCommand::WAIT_FRAMES, 3), run({kA}), end()});
    h.tick();
    h.ticks(3 * kQuarterSecondFrames);
    EXPECT_TRUE(h.state.placed.empty());
    h.tick();
    EXPECT_EQ(h.placedCount(), 1u);
}

// --- lines, pages and the wipe ------------------------------------------------------------------

TEST(DialogueLines, NewlineResetsPenAndStepsLine) {
    Harness h;
    h.start({run({kA}), control(FieldTextCommand::NEWLINE), run({kB}), end()});
    h.ticks(3);
    EXPECT_EQ(h.lastPlaced(), (PlacedGlyph{.page = h.state.page, .line = 1, .x = 4, .glyph = kB}));
}

TEST(DialogueLines, BreakingPastTheFourthLineEndsThePage) {
    Harness h;
    std::vector<FieldTextToken> tokens;
    for (int line = 0; line < kLinesPerPage; ++line) {
        tokens.push_back(run({kA}));
        tokens.push_back(control(FieldTextCommand::NEWLINE));
    }
    tokens.push_back(run({kB}));
    tokens.push_back(end());
    h.start(tokens);
    h.ticks(2 * kLinesPerPage);
    EXPECT_EQ(h.state.line, 0);
    EXPECT_EQ(h.state.keyState, KeyState::WaitingForRelease);
    EXPECT_EQ(h.state.pageClearFrames, kPageClearFrames);
    EXPECT_EQ(h.state.status, MessageStatus::Showing);  // a page end, not the message end
}

TEST(DialogueLines, NextPageTypesOnTheFrameAfterTheWipe) {
    Harness h;
    h.start({run({kA}), control(FieldTextCommand::NEWLINE), run({kA}),
             control(FieldTextCommand::PAGE), run({kB}), end()});
    h.ticks(4);  // A, NEWLINE, A, PAGE
    EXPECT_EQ(h.state.line, 0);
    const std::uint32_t firstPage = h.state.page;
    h.dismissPage();
    h.ticks(kPageClearFrames);
    EXPECT_TRUE(h.state.placed.empty());
    EXPECT_EQ(h.state.page, firstPage + 1);
    h.tick();
    EXPECT_EQ(h.lastPlaced(), (PlacedGlyph{.page = firstPage + 1, .line = 0, .x = 4, .glyph = kB}));
}

// The wipe clears one half-line per frame, top line first, left half before right.
TEST(DialogueLines, WipeClearsHalfLinesTopDown) {
    Harness h;
    h.start({run({kA}), end()});
    h.ticks(2);
    EXPECT_EQ(halfLinesCleared(h.state), 0);
    h.dismissPage();
    EXPECT_EQ(halfLinesCleared(h.state), 0);
    for (int cleared = 1; cleared <= kWipeSteps; ++cleared) {
        h.tick();
        EXPECT_EQ(halfLinesCleared(h.state), cleared);
    }
    h.tick();
    EXPECT_EQ(halfLinesCleared(h.state), 0);
    EXPECT_TRUE(h.state.placed.empty());
}

// --- word wrap and the margin -------------------------------------------------------------------

// 25 glyphs take the pen to 204; a space to 209; a three-glyph word (24) would reach 233, so the
// line breaks instead — one frame, nothing drawn, the cursor unmoved — and the word types on the
// next line.
TEST(DialogueWrap, WordThatDoesNotFitBreaksTheLineFirst) {
    Harness h;
    std::vector<std::uint8_t> first = repeated(25);
    first.push_back(static_cast<std::uint8_t>(kSpaceGlyph));
    first.insert(first.end(), {kB, kB, kB});
    h.start({run(first), end()});
    h.ticks(26);
    EXPECT_EQ(h.state.x, 209);
    const std::size_t placedBefore = h.placedCount();

    h.tick();  // the wrap frame
    EXPECT_EQ(h.placedCount(), placedBefore);
    EXPECT_EQ(h.state.line, 1);
    EXPECT_EQ(h.state.x, kTextLeft);

    h.tick();
    EXPECT_EQ(h.lastPlaced(), (PlacedGlyph{.page = h.state.page, .line = 1, .x = 4, .glyph = kB}));
}

// The measure stops at the space, so a short word fits even when the text after it would not.
TEST(DialogueWrap, MeasureStopsAtTheSpace) {
    Harness h;
    std::vector<std::uint8_t> glyphs = repeated(26);  // pen to 212
    glyphs.push_back(kB);                              // 212 + 8 = 220: fits
    glyphs.push_back(static_cast<std::uint8_t>(kSpaceGlyph));
    glyphs.insert(glyphs.end(), {kC, kC, kC});
    h.start({run(glyphs), end()});
    h.ticks(27);
    EXPECT_EQ(h.lastPlaced().glyph, kB);
    EXPECT_EQ(h.lastPlaced().line, 0);
}

// A word's width counts only its undrawn glyphs, so a word that started on a line is never
// pulled off it partway through.
TEST(DialogueWrap, MeasureStartsAtTheNextUndrawnGlyph) {
    Harness h;
    std::vector<std::uint8_t> glyphs = repeated(20);  // pen to 164
    glyphs.push_back(static_cast<std::uint8_t>(kSpaceGlyph));  // 169
    const std::vector<std::uint8_t> word = repeated(6, kB);    // 48: 169 + 48 = 217 fits
    glyphs.insert(glyphs.end(), word.begin(), word.end());
    h.start({run(glyphs), end()});
    h.ticks(27);
    ASSERT_EQ(h.placedCount(), 27u);
    EXPECT_TRUE(std::all_of(h.state.placed.begin(), h.state.placed.end(),
                            [](const PlacedGlyph& g) { return g.line == 0; }));
}

// A glyph that would cross the margin is not drawn: the line breaks and the glyph is lost. The
// gold amount is never measured, so digits typed at the end of a line reach the margin glyph by
// glyph.
TEST(DialogueWrap, GlyphCrossingTheMarginIsDropped) {
    Harness h;
    h.context.goldAmount = 234;
    h.start({run(repeated(26)), control(FieldTextCommand::GP), end()});
    h.ticks(26);
    EXPECT_EQ(h.state.x, 212);

    h.tick();  // '2' at 212: 220 < 224, drawn
    EXPECT_EQ(h.lastPlaced().glyph, kEnglishDigitGlyph + 2);
    EXPECT_EQ(h.lastPlaced().x, 212);
    h.tick();  // '3' at 220: 228 >= 224, dropped and the line breaks
    EXPECT_EQ(h.lastPlaced().glyph, kEnglishDigitGlyph + 2);
    EXPECT_EQ(h.state.line, 1);
    ASSERT_EQ(h.state.droppedAtMargin.size(), 1u);
    EXPECT_EQ(h.state.droppedAtMargin[0],
              (PlacedGlyph{.page = h.state.page, .line = 0, .x = 220,
                           .glyph = static_cast<std::uint16_t>(kEnglishDigitGlyph + 3)}));
    h.tick();  // '4' at the start of the next line
    EXPECT_EQ(h.lastPlaced(),
              (PlacedGlyph{.page = h.state.page, .line = 1, .x = 4,
                           .glyph = static_cast<std::uint16_t>(kEnglishDigitGlyph + 4)}));
}

// The margin is exclusive: a glyph whose right edge lands exactly on 224 is dropped. (A gold
// amount is never measured, and the synthetic digit 1 is 12 wide.)
TEST(DialogueWrap, GlyphEndingExactlyOnTheMarginIsDropped) {
    Harness h;
    h.context.goldAmount = 1;
    h.start({run(repeated(26)), control(FieldTextCommand::GP), run({kA}), end()});
    h.ticks(26);
    EXPECT_EQ(h.state.x, 212);
    h.tick();  // 212 + 12 = 224
    EXPECT_EQ(h.state.line, 1);
    EXPECT_EQ(h.lastPlaced().glyph, kA);
    ASSERT_EQ(h.state.droppedAtMargin.size(), 1u);
    EXPECT_EQ(h.state.droppedAtMargin[0].glyph, kEnglishDigitGlyph + 1);
    h.tick();
    EXPECT_EQ(h.lastPlaced(), (PlacedGlyph{.page = h.state.page, .line = 1, .x = 4, .glyph = kA}));
}

// A name is measured as one word and the measure stops after it, so letters glued to it are
// measured on their own when the cursor reaches them.
TEST(DialogueWrap, NameIsMeasuredAloneAndGluedLettersSeparately) {
    Harness h;
    const std::array<std::uint8_t, 6> name{0x80, 0x81, 0x82, kNameEnd, kNameEnd, kNameEnd};
    h.context.names[0] = name;
    // Pen to 196; the name (24) reaches 220 and fits; "'s" (16) after it would not.
    h.start({run(repeated(24)), control(FieldTextCommand::CHARACTER_NAME, 0),
             run({kApostrophe, kS}), end()});
    h.ticks(24 + 3);
    EXPECT_EQ(h.state.x, 220);
    EXPECT_EQ(h.lastPlaced().line, 0);

    h.tick();  // "'s" does not fit: the line breaks with the name left behind on line 0
    EXPECT_EQ(h.state.line, 1);
    h.tick();
    EXPECT_EQ(h.lastPlaced(),
              (PlacedGlyph{.page = h.state.page, .line = 1, .x = 4, .glyph = kApostrophe}));
}

// --- control codes ---------------------------------------------------------------------------

TEST(DialogueControls, TabQueuesFillGlyphsWithoutDrawingThisFrame) {
    Harness h;
    h.start({control(FieldTextCommand::TAB, 3), run({kA}), end()});
    h.tick();  // TAB
    EXPECT_TRUE(h.state.placed.empty());
    EXPECT_EQ(h.state.token, 0u);  // the cursor holds on the TAB while its fill drains
    h.ticks(3);
    ASSERT_EQ(h.placedCount(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(h.state.placed[i].glyph, kSpaceGlyph);
        EXPECT_EQ(h.state.placed[i].x, 4 + static_cast<int>(i) * kSpaceWidth);
    }
    EXPECT_EQ(h.state.token, 1u);
    h.tick();
    EXPECT_EQ(h.lastPlaced().glyph, kA);
}

// While a tab's fill drains, nothing is measured: a long word after it is not looked at until
// the fill is done.
TEST(DialogueControls, TabFillIsNotMeasured) {
    Harness h;
    // Pen to 196; tab 4 (20) reaches 216; the word after (16) does not fit at 196 + 20 + 16.
    h.start({run(repeated(24)), control(FieldTextCommand::TAB, 4), run({kB, kB}), end()});
    h.ticks(24 + 1 + 4);
    ASSERT_EQ(h.placedCount(), 28u);
    EXPECT_TRUE(std::all_of(h.state.placed.begin(), h.state.placed.end(),
                            [](const PlacedGlyph& g) { return g.line == 0; }));
    EXPECT_EQ(h.state.x, 216);
    h.tick();  // now the word is measured, and wraps
    EXPECT_EQ(h.state.line, 1);
}

// KEY arms the key timer without waiting: typing continues, and two frames later the timer runs
// out and clears the choice count.
TEST(DialogueControls, KeyDoesNotWaitButClearsChoicesTwoFramesLater) {
    Harness h;
    h.start({control(FieldTextCommand::CHOICE), control(FieldTextCommand::CHOICE),
             control(FieldTextCommand::KEY), run({kA, kB, kC}), end()});
    h.ticks(3);  // CHOICE, CHOICE, KEY
    EXPECT_EQ(h.state.choices.count, 2u);
    EXPECT_EQ(h.state.keyState, KeyState::Typing);
    h.tick();  // timer 1 -> 0, A drawn
    EXPECT_EQ(h.lastPlaced().glyph, kA);
    EXPECT_EQ(h.state.choices.count, 2u);
    h.tick();  // timer runs out: count cleared, B drawn
    EXPECT_EQ(h.state.choices.count, 0u);
    EXPECT_EQ(h.lastPlaced().glyph, kB);
}

TEST(DialogueControls, KeyTimedAdvancesWhenItsTimerRunsOut) {
    Harness h;
    h.start({control(FieldTextCommand::KEY_TIMED, 2), run({kA}), end()});
    h.tick();
    EXPECT_EQ(h.state.keyState, KeyState::WaitingForPress);
    h.ticks(2 * kQuarterSecondFrames);
    EXPECT_TRUE(h.state.placed.empty());
    EXPECT_EQ(h.state.keyState, KeyState::WaitingForPress);
    h.tick();  // the timer is spent: no press needed
    EXPECT_EQ(h.state.keyState, KeyState::Typing);
    EXPECT_EQ(h.placedCount(), 1u);
}

TEST(DialogueControls, KeyTimedAdvancesEarlyOnAPress) {
    Harness h;
    h.start({control(FieldTextCommand::KEY_TIMED, 8), run({kA}), end()});
    h.tick();
    h.ticks(5);
    h.tick(kConfirm);
    EXPECT_EQ(h.state.keyState, KeyState::Typing);
    EXPECT_FALSE(h.state.keyTimerArmed);
    h.tick();
    EXPECT_EQ(h.placedCount(), 1u);
}

// --- splices ---------------------------------------------------------------------------------

// English names are converted from the menu font, end at $FF, and the first letter draws on the
// same frame the control is read.
TEST(DialogueSplices, CharacterNameConvertsAndStopsAtItsEnd) {
    Harness h;
    const std::array<std::uint8_t, 6> name{0x80, 0x8E, 0x91, kNameEnd, 0x99, 0x99};
    h.context.names[3] = name;
    h.start({control(FieldTextCommand::CHARACTER_NAME, 3), run({kA}), end()});
    h.tick();
    ASSERT_EQ(h.placedCount(), 1u);
    EXPECT_EQ(h.lastPlaced().glyph, 0x80 - kMenuToDialogueOffset);
    h.ticks(2);
    ASSERT_EQ(h.placedCount(), 3u);
    EXPECT_EQ(h.state.placed[1].glyph, 0x8E - kMenuToDialogueOffset);
    EXPECT_EQ(h.state.placed[2].glyph, 0x91 - kMenuToDialogueOffset);
    h.tick();
    EXPECT_EQ(h.lastPlaced().glyph, kA);
}

TEST(DialogueSplices, CharacterNameTakesAtMostSixGlyphs) {
    Harness h;
    const std::array<std::uint8_t, 8> name{0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87};
    h.context.names[0] = name;
    h.start({control(FieldTextCommand::CHARACTER_NAME, 0), run({kA}), end()});
    h.ticks(kCharacterNameGlyphs + 1);
    ASSERT_EQ(h.placedCount(), kCharacterNameGlyphs + 1);
    EXPECT_EQ(h.lastPlaced().glyph, kA);
}

// A corpus holding only an item-name table and a spell-name table, for the two splices that read
// one.
TextCorpus spliceCorpus(ItemId item, std::span<const std::uint8_t> itemRecord,
                        std::span<const std::uint8_t> spellTable) {
    std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
    const auto& itemMeta = textClassMetadata(TextClass::ITEM_NAME);
    auto& items = buffers[static_cast<std::size_t>(TextClass::ITEM_NAME)];
    items.assign(static_cast<std::size_t>(itemMeta.recordCount) * itemMeta.recordSize, kNameEnd);
    std::copy(itemRecord.begin(), itemRecord.end(),
              items.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(item) *
                                                          itemMeta.recordSize));
    buffers[static_cast<std::size_t>(TextClass::MAGIC_NAME)].assign(spellTable.begin(),
                                                                     spellTable.end());
    return TextCorpus{std::move(buffers)};
}

// The item splice skips the record's first byte (the icon) and takes up to 12 letters.
TEST(DialogueSplices, ItemNameSkipsTheIconByte) {
    const std::array<std::uint8_t, 13> record{0xD8, 0x80, 0x81, 0x82, kNameEnd, 0x90, 0x90,
                                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    const TextCorpus corpus = spliceCorpus(ItemId::MITHRILKNIFE, record, {});
    Harness h;
    h.context.corpus = &corpus;
    h.context.item = ItemId::MITHRILKNIFE;
    h.start({control(FieldTextCommand::ITEM_NAME), end()});
    h.ticks(3);
    ASSERT_EQ(h.placedCount(), 3u);
    EXPECT_EQ(h.state.placed[0].glyph, 0x80 - kMenuToDialogueOffset);
    EXPECT_EQ(h.state.placed[2].glyph, 0x82 - kMenuToDialogueOffset);
    h.tick();  // END, not a fourth letter
    EXPECT_EQ(h.placedCount(), 3u);
    EXPECT_EQ(h.state.status, MessageStatus::Finished);
}

// The spell splice reads at a stride of 4, one byte in, and copies 4 bytes — so spell 1 starts at
// byte 5 of the table, inside spell 0's 7-byte record.
TEST(DialogueSplices, SpellNameReadsAtStrideFourAndCopiesFour) {
    std::vector<std::uint8_t> table(64, 0x90);
    for (std::size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<std::uint8_t>(0x80 + i);
    }
    const TextCorpus corpus = spliceCorpus(ItemId::DIRK, {}, table);
    Harness h;
    h.context.corpus = &corpus;
    h.context.spellIndex = 1;
    h.start({control(FieldTextCommand::SPELL_NAME), run({kA}), end()});
    h.ticks(kSpellNameGlyphs + 1);
    ASSERT_EQ(h.placedCount(), kSpellNameGlyphs + 1);
    for (std::size_t i = 0; i < kSpellNameGlyphs; ++i) {
        EXPECT_EQ(h.state.placed[i].glyph, 0x80 + 5 + i - kMenuToDialogueOffset);
    }
    EXPECT_EQ(h.lastPlaced().glyph, kA);
}

TEST(DialogueSplices, GoldSuppressesLeadingZeros) {
    Harness h;
    h.context.goldAmount = 2050;
    h.start({control(FieldTextCommand::GP), end()});
    h.ticks(4);
    ASSERT_EQ(h.placedCount(), 4u);
    const std::array<int, 4> digits{2, 0, 5, 0};
    for (std::size_t i = 0; i < digits.size(); ++i) {
        EXPECT_EQ(h.state.placed[i].glyph, kEnglishDigitGlyph + digits[i]);
    }
    h.tick();
    EXPECT_EQ(h.state.status, MessageStatus::Finished);
}

TEST(DialogueSplices, GoldPrintsSevenDigitsAtMost) {
    Harness h;
    h.context.goldAmount = 9'999'999;
    h.start({control(FieldTextCommand::GP), end()});
    h.ticks(kGoldDigits + 1);
    EXPECT_EQ(h.placedCount(), kGoldDigits);
    EXPECT_EQ(h.state.status, MessageStatus::Finished);
}

// A splice with nothing in it draws nothing; the frame is spent and the message goes on.
TEST(DialogueSplices, EmptySpliceDrawsNothing) {
    Harness h;
    h.context.goldAmount = 0;
    h.start({control(FieldTextCommand::GP), run({kA}), end()});
    h.tick();
    EXPECT_TRUE(h.state.placed.empty());
    EXPECT_EQ(h.state.x, kTextLeft);
    h.tick();
    ASSERT_EQ(h.placedCount(), 1u);
    EXPECT_EQ(h.lastPlaced().glyph, kA);
}

// --- choices -------------------------------------------------------------------------------------

// A choice records the left edge of the 16-pixel column the pen is in — the arrow snaps to the
// column, not the marker's pixel — and draws the 6-pixel wide space.
TEST(DialogueChoices, ChoiceRecordsTheColumnAndDrawsAWideSpace) {
    Harness h;
    h.start({run(repeated(3)), control(FieldTextCommand::CHOICE), run({kA}),
             control(FieldTextCommand::NEWLINE), control(FieldTextCommand::CHOICE), run({kB}),
             end()});
    h.ticks(4);  // pen 28 when the choice is read: column 1
    EXPECT_EQ(h.state.choices.count, 1u);
    EXPECT_EQ(h.state.choices.positions[0], (ChoicePosition{.line = 0, .x = 16}));
    EXPECT_EQ(h.lastPlaced().glyph, kWideSpaceGlyph);
    EXPECT_EQ(h.state.x, 28 + kWideSpaceWidth);
    h.ticks(3);
    EXPECT_EQ(h.state.choices.count, 2u);
    EXPECT_EQ(h.state.choices.positions[1], (ChoicePosition{.line = 1, .x = 0}));
}

// Three choices on three lines, then the end of the message.
Harness threeChoices() {
    Harness h;
    h.start({control(FieldTextCommand::CHOICE), run({kA}), control(FieldTextCommand::NEWLINE),
             control(FieldTextCommand::CHOICE), run({kB}), control(FieldTextCommand::NEWLINE),
             control(FieldTextCommand::CHOICE), run({kC}), end()});
    h.ticks(9);
    return h;
}

TEST(DialogueChoices, CursorAppearsOnceTheEngineWaits) {
    Harness h = threeChoices();
    EXPECT_FALSE(h.state.cursor.has_value());
    h.tick();
    ASSERT_TRUE(h.state.cursor.has_value());
    EXPECT_EQ(*h.state.cursor, (ChoicePosition{.line = 0, .x = 0}));
}

TEST(DialogueChoices, OneMovePerPress) {
    Harness h = threeChoices();
    h.tick();
    h.tick(kDown);
    EXPECT_EQ(h.state.choices.selection, 1u);
    h.ticks(5, kDown);  // still held: no further moves
    EXPECT_EQ(h.state.choices.selection, 1u);
    h.tick();
    h.tick(kRight);  // right moves forward too
    EXPECT_EQ(h.state.choices.selection, 2u);
    EXPECT_EQ(*h.state.cursor, (ChoicePosition{.line = 2, .x = 0}));
}

TEST(DialogueChoices, BlockedStepsLeaveTheSelectionFreeToMove) {
    Harness h = threeChoices();
    h.tick();
    h.tick(kUp);  // already on the first: blocked
    EXPECT_EQ(h.state.choices.selection, 0u);
    EXPECT_FALSE(h.state.choices.selectionChanging);
    h.tick(DialogueInput{.up = true, .down = true});  // up/left is tested first, and blocks
    EXPECT_EQ(h.state.choices.selection, 0u);
    h.tick(kNothing);
    h.tick(kDown);
    h.tick(kNothing);
    h.tick(kDown);
    EXPECT_EQ(h.state.choices.selection, 2u);
    h.tick(kNothing);
    h.tick(kDown);  // already on the last: blocked
    EXPECT_EQ(h.state.choices.selection, 2u);
    h.tick(kNothing);
    h.tick(kLeft);
    EXPECT_EQ(h.state.choices.selection, 1u);
}

// The press stops the cursor following the selection, but it stays on screen until the wipe takes
// its half-line, and goes with the page.
TEST(DialogueChoices, CursorStaysUntilTheWipeReachesIt) {
    Harness h = threeChoices();
    h.tick();
    h.tick(kDown);
    h.tick(kNothing);
    h.tick(kConfirm);
    EXPECT_EQ(h.state.choices.count, 0u);
    ASSERT_TRUE(h.state.cursor.has_value());
    EXPECT_EQ(*h.state.cursor, (ChoicePosition{.line = 1, .x = 0}));
    h.ticks(2);  // line 0's two halves are gone; the cursor on line 1 is still shown
    EXPECT_FALSE(isWiped(h.state, h.state.cursor->line, h.state.cursor->x));
    h.tick();    // line 1 left: the cursor's half-line
    EXPECT_TRUE(isWiped(h.state, h.state.cursor->line, h.state.cursor->x));
    h.ticks(kPageClearFrames - 4);
    EXPECT_TRUE(h.state.cursor.has_value());
    h.tick();
    EXPECT_FALSE(h.state.cursor.has_value());
}

TEST(DialogueChoices, SelectionPersistsUntilCleared) {
    Harness h = threeChoices();
    h.tick();
    h.tick(kDown);
    h.tick(kNothing);
    h.tick(kConfirm);
    h.ticks(kPageClearFrames + 1);
    EXPECT_EQ(h.state.status, MessageStatus::Idle);
    EXPECT_EQ(selection(h.state), 1u);
    clearSelection(h.state);
    EXPECT_EQ(selection(h.state), 0u);
}

TEST(DialogueChoices, OneChoiceShowsNoCursor) {
    Harness h;
    h.start({control(FieldTextCommand::CHOICE), run({kA}), end()});
    h.ticks(3);
    h.ticks(4);
    EXPECT_FALSE(h.state.cursor.has_value());
}

// --- the map title -------------------------------------------------------------------------------

// English centering adds the record's byte widths: three 8-pixel glyphs are 24 wide, so the
// title starts at (224 - 24) / 2 = 100. It is placed whole, at once, and asks for no window close.
TEST(DialogueTitle, CenteredAndPlacedAtOnce) {
    Harness h;
    const std::array<std::uint8_t, 4> record{kA, kB, kC, 0x00};
    showMapTitle(h.state, record, h.context);
    ASSERT_EQ(h.placedCount(), 3u);
    EXPECT_EQ(h.state.placed[0], (PlacedGlyph{.page = h.state.page, .line = 0, .x = 100,
                                              .glyph = kA}));
    EXPECT_EQ(h.state.placed[2].x, 116);
    EXPECT_TRUE(h.state.titleShowing);
    EXPECT_EQ(h.state.titleFrames, kTitleFrames);
    EXPECT_EQ(h.state.keyState, KeyState::Typing);
    EXPECT_EQ(h.state.pageClearFrames, 0);
    h.tick();
    EXPECT_FALSE(windowCloseRequested(h.state));
}

TEST(DialogueTitle, TimerClosesTheTitleAndTheNextMessageTypesNineFramesLate) {
    Harness h;
    const std::array<std::uint8_t, 2> record{kA, 0x00};
    showMapTitle(h.state, record, h.context);
    h.ticks(kTitleFrames - 1);
    EXPECT_TRUE(h.state.titleShowing);
    h.tick();
    EXPECT_FALSE(h.state.titleShowing);
    EXPECT_EQ(h.state.pageClearFrames, kPageClearFrames);
    h.ticks(50);  // idle: the wipe does not run and the title's glyph stays
    EXPECT_EQ(h.state.pageClearFrames, kPageClearFrames);
    EXPECT_EQ(h.placedCount(), 1u);

    h.start({run({kB}), end()});
    h.ticks(kPageClearFrames);
    EXPECT_TRUE(h.state.placed.empty());
    h.tick();
    EXPECT_EQ(h.lastPlaced(), (PlacedGlyph{.page = h.state.page, .line = 0, .x = 4, .glyph = kB}));
}

TEST(DialogueTitle, CloseDoesNothingWithoutATitle) {
    Harness h;
    h.start({run({kA, kB}), end()});
    h.tick();
    closeMapTitle(h.state);
    EXPECT_EQ(h.state.status, MessageStatus::Showing);
    EXPECT_EQ(h.state.pageClearFrames, 0);
}

TEST(DialogueTitle, EarlyCloseStopsAShowingMessage) {
    Harness h;
    const std::array<std::uint8_t, 2> record{kA, 0x00};
    showMapTitle(h.state, record, h.context);
    h.start({run({kB, kC}), end()});
    h.tick();
    closeMapTitle(h.state);
    EXPECT_EQ(h.state.status, MessageStatus::Idle);
    EXPECT_EQ(h.state.titleFrames, 0);
    EXPECT_EQ(h.state.pageClearFrames, kPageClearFrames);
}

// --- Japanese --------------------------------------------------------------------------------------

// Japanese has no word wrap: a word that would not fit is typed until a glyph reaches the margin,
// which is dropped.
TEST(DialogueJapanese, NeverWordWraps) {
    Harness h{TextLanguage::JP};
    std::vector<std::uint8_t> glyphs = repeated(25);  // pen to 204
    glyphs.insert(glyphs.end(), {kB, kB, kB});
    h.start({run(glyphs), end()});
    h.ticks(27);  // 204 and 212 fit; the third B would reach 228
    EXPECT_EQ(h.state.line, 0);
    EXPECT_EQ(h.state.x, 220);
    h.tick();
    EXPECT_EQ(h.state.line, 1);
    ASSERT_EQ(h.state.droppedAtMargin.size(), 1u);
    EXPECT_EQ(h.state.droppedAtMargin[0].glyph, kB);
}

// A byte below $20 in a Japanese run opens a two-byte glyph.
TEST(DialogueJapanese, TwoByteGlyphs) {
    Harness h{TextLanguage::JP};
    h.start({run({0x1C, 0x05, kA}), end()});
    h.tick();
    EXPECT_EQ(h.lastPlaced().glyph, 0x0105);
    h.tick();
    EXPECT_EQ(h.lastPlaced().glyph, kA);
}

TEST(DialogueJapanese, TabFillsWithTheWideSpaceAndDigitsUseTheirOwnBase) {
    Harness h{TextLanguage::JP};
    h.context.goldAmount = 7;
    h.start({control(FieldTextCommand::TAB, 1), control(FieldTextCommand::GP), end()});
    h.ticks(3);
    ASSERT_EQ(h.placedCount(), 2u);
    EXPECT_EQ(h.state.placed[0].glyph, kWideSpaceGlyph);
    EXPECT_EQ(h.state.placed[1].glyph, kJapaneseDigitGlyph + 7);
}

// Japanese centering counts glyphs at 12 pixels each, whatever their real widths: two 8-pixel
// glyphs count as 24, so the title starts at 100.
TEST(DialogueJapanese, TitleCentersByGlyphCount) {
    Harness h{TextLanguage::JP};
    const std::array<std::uint8_t, 3> record{kA, kB, 0x00};
    showMapTitle(h.state, record, h.context);
    ASSERT_EQ(h.placedCount(), 2u);
    EXPECT_EQ(h.state.placed[0].x, 100);
    EXPECT_EQ(h.state.placed[1].x, 108);
}

// --- the whole English script ----------------------------------------------------------------------

class DialogueCorpusTest : public ::testing::Test {
protected:
    inline static test::IngestedCartridge cartridge_;
    inline static std::vector<std::uint8_t> widths_;

    static void SetUpTestSuite() {
        cartridge_ = test::ingestVanilla();
        if (cartridge_.available()) {
            // The width table sits immediately before the font's graphics.
            const auto place = romRegion(RomAsset::LARGE_FONT, Language::EN);
            const std::size_t fontOffset =
                static_cast<std::size_t>((place->at >> 16) & 0x3F) << 16 | (place->at & 0xFFFF);
            const std::size_t widthCount = 256;
            widths_.assign(cartridge_.image.begin() +
                               static_cast<std::ptrdiff_t>(fontOffset - widthCount),
                           cartridge_.image.begin() + static_cast<std::ptrdiff_t>(fontOffset));
        }
    }

    static const TextCorpus& corpus() { return cartridge_.content->text; }

    static DialogueContext context() {
        DialogueContext c;
        c.metrics = FontMetrics{widths_};
        c.corpus = &corpus();
        for (std::size_t i = 0; i < kCharacterCount; ++i) {
            c.names[i] = corpus().charName(i);  // everyone's default name
        }
        c.item = ItemId::MITHRILKNIFE;
        c.goldAmount = 500;  // the smallest chest
        return c;
    }
};

// What one full run of a message observed.
struct MessageRun {
    bool finished = false;
    std::size_t choicesRecorded = 0;
    std::size_t mostChoices = 0;
    bool glyphPastMargin = false;
};

// Drive message `index` to its window-close request, pressing A whenever the engine waits for a
// press and releasing it otherwise.
MessageRun runMessage(std::size_t index, const DialogueContext& context) {
    constexpr int kFrameLimit = 1'000'000;
    MessageRun result;
    DialogueState state;
    initDialogue(state);
    startDialogue(state, index, context);
    for (int frame = 0; frame < kFrameLimit; ++frame) {
        const DialogueInput input{.confirmHeld = state.keyState == KeyState::WaitingForPress};
        const std::size_t before = state.choices.count;
        tickDialogue(state, input, context);
        if (state.choices.count > before) {
            result.choicesRecorded += state.choices.count - before;
        }
        result.mostChoices = std::max(result.mostChoices, state.choices.count);
        if (!state.placed.empty()) {
            const PlacedGlyph& g = state.placed.back();
            if (g.x + context.metrics.width(g.glyph) >= kTextRight) {
                result.glyphPastMargin = true;
            }
        }
        if (windowCloseRequested(state)) {
            result.finished = true;
            break;
        }
    }
    return result;
}

// Every English message types to the end, records 328 choices in all — 148 messages with two or
// more — and never places a glyph past the margin.
TEST_F(DialogueCorpusTest, EveryMessageRunsToItsEnd) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const DialogueContext c = context();
    const std::size_t count = dialogueOffsets().size();
    ASSERT_EQ(count, 3084u);

    std::size_t choicesRecorded = 0;
    std::map<std::size_t, std::size_t> messagesByChoiceCount;
    for (std::size_t index = 0; index < count; ++index) {
        const MessageRun r = runMessage(index, c);
        EXPECT_TRUE(r.finished) << "message " << index << " did not finish";
        EXPECT_FALSE(r.glyphPastMargin) << "message " << index;
        if (!corpus().dialogue(index).empty()) {
            choicesRecorded += r.choicesRecorded;
            if (r.mostChoices >= 2) {
                ++messagesByChoiceCount[r.mostChoices];
            }
        }
    }
    EXPECT_EQ(choicesRecorded, 328u);
    const std::map<std::size_t, std::size_t> expected{{2, 125}, {3, 18}, {4, 2}, {5, 2}, {6, 1}};
    EXPECT_EQ(messagesByChoiceCount, expected);
}

// What the shipped script loses at the right margin with everyone's default name. A space that
// ends a line is dropped routinely and shows nothing; any other glyph is text the player never
// sees. These are the facts docs/Bugs.md reports.
constexpr std::size_t kExpectedDroppedSpaces = 305;
constexpr std::size_t kExpectedDroppedVisibleGlyphs = 0;
constexpr std::size_t kExpectedMessagesLosingText = 0;

TEST_F(DialogueCorpusTest, GlyphsDroppedAtTheMarginWithDefaultNames) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const DialogueContext c = context();
    std::size_t spaces = 0;
    std::size_t visible = 0;
    std::size_t messagesLosingText = 0;
    for (std::size_t index = 0; index < dialogueOffsets().size(); ++index) {
        DialogueState state;
        initDialogue(state);
        startDialogue(state, index, c);
        for (int frame = 0; frame < 1'000'000 && !windowCloseRequested(state); ++frame) {
            tickDialogue(state,
                         DialogueInput{.confirmHeld = state.keyState == KeyState::WaitingForPress},
                         c);
        }
        bool lostText = false;
        for (const PlacedGlyph& g : state.droppedAtMargin) {
            if (g.glyph == kSpaceGlyph) {
                ++spaces;
            } else {
                ++visible;
                lostText = true;
                RecordProperty("lost_glyph", "message " + std::to_string(index) + " glyph " +
                                                 std::to_string(g.glyph) + " at x " +
                                                 std::to_string(g.x));
            }
        }
        messagesLosingText += lostText ? 1 : 0;
    }
    RecordProperty("spaces_dropped", static_cast<int>(spaces));
    RecordProperty("visible_glyphs_dropped", static_cast<int>(visible));
    RecordProperty("messages_losing_text", static_cast<int>(messagesLosingText));
    EXPECT_EQ(spaces, kExpectedDroppedSpaces);
    EXPECT_EQ(visible, kExpectedDroppedVisibleGlyphs);
    EXPECT_EQ(messagesLosingText, kExpectedMessagesLosingText);
}

// Empty records show the record after them, and a record with no END runs on into the next.
TEST_F(DialogueCorpusTest, RecordsRunOnUntilAnEnd) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const DialogueContext c = context();
    for (const std::size_t index : {std::size_t{0}, std::size_t{1422}, std::size_t{1655},
                                    std::size_t{2125}}) {
        DialogueState runOn;
        startDialogue(runOn, index, c);
        DialogueState next;
        startDialogue(next, index + 1, c);
        // None of these records ends itself, so the message is its own tokens followed by the
        // next record's.
        const std::vector<FieldTextToken> own =
            tokenizeDialogue(corpus().dialogue(index), corpus().dte());
        const auto endsMessage = [](const FieldTextToken& token) {
            const auto* control = std::get_if<FieldControl>(&token);
            return control != nullptr && control->command == FieldTextCommand::END;
        };
        ASSERT_TRUE(std::none_of(own.begin(), own.end(), endsMessage)) << "record " << index;
        EXPECT_EQ(runOn.tokens.size(), own.size() + next.tokens.size()) << "record " << index;
    }
}

// Every map title is centred inside the window.
TEST_F(DialogueCorpusTest, EveryMapTitleFits) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const DialogueContext c = context();
    const std::size_t titles = pointerOffsets(TextClass::MAP_TITLE).size();
    for (std::size_t index = 0; index < titles; ++index) {
        DialogueState state;
        initDialogue(state);
        showMapTitle(state, corpus().mapTitle(index), c);
        for (const PlacedGlyph& g : state.placed) {
            EXPECT_EQ(g.line, 0) << "title " << index;
            EXPECT_GE(g.x, kTextLeft) << "title " << index;
            EXPECT_LT(g.x + c.metrics.width(g.glyph), kTextRight) << "title " << index;
        }
        EXPECT_TRUE(state.droppedAtMargin.empty()) << "title " << index;
    }
}

TEST(DialogueJapaneseCorpus, EveryMessageRunsToItsEnd) {
    GTEST_SKIP() << "Needs a Japanese cartridge (FF6 1.0 J); none is available to validate the "
                    "Japanese script against.";
}

}  // namespace
}  // namespace ostinato::text
