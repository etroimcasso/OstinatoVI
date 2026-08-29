// Text-codec tests (Approach B): the three tokenizers turned loose on the whole
// corpus, plus targeted grammar checks.
//
//   1. Full-corpus tokenize — every dialogue, battle, and menu-description
//      record tokenizes without throwing. Because the tokenizers throw on any
//      control byte the grammar does not define, a clean sweep of all ~3,850
//      records is a proof that the grammar is complete over the real corpus.
//   2. Byte-exact round-trip — for the DTE-free families (battle text, menu
//      descriptions) the token stream reassembles to the original record bytes.
//   3. DTE expansion, operand consumption, and unknown-byte rejection — pinned
//      with synthetic records so the exact grammar contract is nailed down
//      independent of what the corpus happens to contain.
//   4. Menu-description decode cross-check against the upstream reference text
//      (parser-emitted), for the records whose glyph bytes are unambiguous.
//
// The real-corpus sweeps read the cartridge FF6_VANILLA_ROM names; without one,
// or without the machine that reads one, they skip and say which is missing. JP
// validation waits on a Japanese cartridge.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "data/text_codec.h"
#include "data/text_corpus.h"
#include "data/text_metadata.h"
#include "vanilla_rom.h"
#include "fixtures/text_menu_desc_expected.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_text_command.h"
#include "ostinato/item_id.h"
#include "ostinato/field_text_command.h"
#include "ostinato/text_class.h"
#include "ostinato/text_token.h"

namespace ostinato {
namespace {

// A 256-byte DTE buffer whose code C (>= 0x80) expands to the pair
// {C, C ^ 0xFF} — enough to check the expansion wiring without the real table.
std::vector<std::uint8_t> syntheticDteBytes() {
    std::vector<std::uint8_t> bytes(256);
    for (std::size_t code = 0x80; code <= 0xFF; ++code) {
        const std::size_t idx = (code - 0x80) * 2;
        bytes[idx] = static_cast<std::uint8_t>(code);
        bytes[idx + 1] = static_cast<std::uint8_t>(code ^ 0xFF);
    }
    return bytes;
}

// The battle commands that consume one following operand byte (test-side copy
// of the grammar, so the round-trip encodes the contract independently).
bool battleCarriesOperand(BattleTextCommand c) {
    switch (c) {
        case BattleTextCommand::CHARACTER_NAME:
        case BattleTextCommand::WAIT_FRAMES:
        case BattleTextCommand::COMMAND_NAME:
        case BattleTextCommand::ITEM_NAME:
        case BattleTextCommand::ATTACK_NAME:
        case BattleTextCommand::VAR_STRING:
        case BattleTextCommand::KANJI_PLANE_1:
        case BattleTextCommand::KANJI_PLANE_2:
        case BattleTextCommand::KANJI_PLANE_3:
        case BattleTextCommand::KANJI_PLANE_4:
            return true;
        default:
            return false;
    }
}

// Reassemble a battle token stream back to record bytes.
std::vector<std::uint8_t> reassembleBattle(
    const std::vector<BattleTextToken>& tokens) {
    std::vector<std::uint8_t> out;
    for (const auto& tok : tokens) {
        if (const auto* run = std::get_if<GlyphRun>(&tok)) {
            out.insert(out.end(), run->glyphs.begin(), run->glyphs.end());
        } else {
            const auto& ctrl = std::get<BattleControl>(tok);
            out.push_back(static_cast<std::uint8_t>(ctrl.command));
            if (battleCarriesOperand(ctrl.command)) {
                out.push_back(ctrl.operand);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Synthetic grammar checks — no corpus.
// ---------------------------------------------------------------------------

TEST(TextCodecDialogue, DteCodeExpandsToTwoGlyphs) {
    const std::vector<std::uint8_t> dteBytes = syntheticDteBytes();
    const DteTable dte(dteBytes);
    // 0x80 expands to {0x80, 0x7F}; then a direct glyph 'A' (0x41 is < 0x80 and
    // >= 0x20 so it is a literal), then END.
    const std::vector<std::uint8_t> record{0x80, 0x41, 0x00};
    const auto tokens = tokenizeDialogue(record, dte);
    ASSERT_EQ(tokens.size(), 2u);  // one glyph run, one END control
    const auto& run = std::get<GlyphRun>(tokens[0]);
    ASSERT_EQ(run.glyphs.size(), 3u);
    EXPECT_EQ(run.glyphs[0], 0x80);
    EXPECT_EQ(run.glyphs[1], 0x7F);
    EXPECT_EQ(run.glyphs[2], 0x41);
    const auto& end = std::get<FieldControl>(tokens[1]);
    EXPECT_EQ(end.command, FieldTextCommand::END);
}

TEST(TextCodecDialogue, OperandCarryingControlsConsumeOneByte) {
    const DteTable dte(syntheticDteBytes());
    // WAIT_FRAMES(0x11) op=5, TAB(0x14) op=8, KEY_TIMED(0x16) op=30, END.
    const std::vector<std::uint8_t> record{0x11, 0x05, 0x14, 0x08,
                                           0x16, 0x1E, 0x00};
    const auto tokens = tokenizeDialogue(record, dte);
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(std::get<FieldControl>(tokens[0]).command,
              FieldTextCommand::WAIT_FRAMES);
    EXPECT_EQ(std::get<FieldControl>(tokens[0]).operand, 5);
    EXPECT_EQ(std::get<FieldControl>(tokens[1]).command, FieldTextCommand::TAB);
    EXPECT_EQ(std::get<FieldControl>(tokens[1]).operand, 8);
    EXPECT_EQ(std::get<FieldControl>(tokens[2]).command,
              FieldTextCommand::KEY_TIMED);
    EXPECT_EQ(std::get<FieldControl>(tokens[2]).operand, 30);
    EXPECT_EQ(std::get<FieldControl>(tokens[3]).command, FieldTextCommand::END);
}

TEST(TextCodecDialogue, CharacterNameCodeCarriesMemberInLowNibble) {
    const DteTable dte(syntheticDteBytes());
    // 0x02..0x0f splice a party member; operand is (code - 0x02).
    const std::vector<std::uint8_t> record{0x02, 0x05, 0x00};
    const auto tokens = tokenizeDialogue(record, dte);
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(std::get<FieldControl>(tokens[0]).command,
              FieldTextCommand::CHARACTER_NAME);
    EXPECT_EQ(std::get<FieldControl>(tokens[0]).operand, 0);
    EXPECT_EQ(std::get<FieldControl>(tokens[1]).command,
              FieldTextCommand::CHARACTER_NAME);
    EXPECT_EQ(std::get<FieldControl>(tokens[1]).operand, 3);  // 0x05 - 0x02
}

TEST(TextCodecDialogue, UnknownControlByteThrows) {
    const DteTable dte(syntheticDteBytes());
    // 0x17 is not defined in the field grammar (and not a name-splice code).
    const std::vector<std::uint8_t> record{0x41, 0x17, 0x00};
    EXPECT_THROW(tokenizeDialogue(record, dte), std::runtime_error);
}

TEST(TextCodecDialogue, MissingOperandThrows) {
    const DteTable dte(syntheticDteBytes());
    const std::vector<std::uint8_t> record{0x11};  // WAIT_FRAMES with no operand
    EXPECT_THROW(tokenizeDialogue(record, dte), std::runtime_error);
}

TEST(TextCodecBattle, OperandCarryingControlsConsumeOneByte) {
    // command(0x0c) op=3, item(0x0e) op=7, attack(0x0f) op=9, END.
    const std::vector<std::uint8_t> record{0x0C, 0x03, 0x0E, 0x07,
                                           0x0F, 0x09, 0x00};
    const auto tokens = tokenizeBattle(record);
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(std::get<BattleControl>(tokens[0]).command,
              BattleTextCommand::COMMAND_NAME);
    EXPECT_EQ(std::get<BattleControl>(tokens[0]).operand, 3);
    EXPECT_EQ(std::get<BattleControl>(tokens[1]).command,
              BattleTextCommand::ITEM_NAME);
    EXPECT_EQ(std::get<BattleControl>(tokens[1]).operand, 7);
    EXPECT_EQ(std::get<BattleControl>(tokens[2]).command,
              BattleTextCommand::ATTACK_NAME);
    EXPECT_EQ(std::get<BattleControl>(tokens[2]).operand, 9);
}

TEST(TextCodecBattle, KanjiCodesRejectedOutsideJapanese) {
    const std::vector<std::uint8_t> record{0x1C, 0x05, 0x00};
    EXPECT_THROW(tokenizeBattle(record, TextLanguage::EN), std::runtime_error);
    // In Japanese the same code is a kanji-plane selector with one operand.
    const auto tokens = tokenizeBattle(record, TextLanguage::JP);
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(std::get<BattleControl>(tokens[0]).command,
              BattleTextCommand::KANJI_PLANE_1);
    EXPECT_EQ(std::get<BattleControl>(tokens[0]).operand, 5);
}

TEST(TextCodecBattle, UnknownControlByteThrows) {
    const std::vector<std::uint8_t> record{0x41, 0x03, 0x00};  // 0x03 undefined
    EXPECT_THROW(tokenizeBattle(record), std::runtime_error);
}

TEST(TextCodecMenuDescription, StopsAtTerminator) {
    const std::vector<std::uint8_t> record{0x9A, 0x9B, 0x00, 0x9C};
    const auto glyphs = decodeMenuDescription(record);
    ASSERT_EQ(glyphs.size(), 2u);
    EXPECT_EQ(glyphs[0], 0x9A);
    EXPECT_EQ(glyphs[1], 0x9B);
}

// ---------------------------------------------------------------------------
// Real corpus — full-corpus tokenize + byte-exact round-trip.
// ---------------------------------------------------------------------------

class TextCodecRipTest : public ::testing::Test {
protected:
    inline static test::IngestedCartridge cartridge_;

    static void SetUpTestSuite() { cartridge_ = test::ingestVanilla(); }

    static const TextCorpus& corpus() { return cartridge_.content->text; }

    // The public accessor for one menu-description class's record.
    static std::span<const std::uint8_t> menuRecord(TextClass klass,
                                                     std::size_t i) {
        switch (klass) {
            case TextClass::ITEM_DESC:
                return corpus().itemDescription(static_cast<ItemId>(i));
            case TextClass::MAGIC_DESC:
                return corpus().magicDescription(i);
            case TextClass::LORE_DESC:
                return corpus().loreDescription(i);
            case TextClass::BLITZ_DESC:
                return corpus().blitzDescription(i);
            case TextClass::BUSHIDO_DESC:
                return corpus().bushidoDescription(i);
            case TextClass::GENJU_ATTACK_DESC:
                return corpus().genjuAttackDescription(i);
            case TextClass::GENJU_BONUS_DESC:
                return corpus().genjuBonusDescription(i);
            default:
                return corpus().rareItemDescription(i);
        }
    }
};

// Every dialogue record tokenizes without throwing -> the field grammar is
// complete over the whole dialogue corpus (any undefined control byte would
// throw). DTE codes expand through the real table.
TEST_F(TextCodecRipTest, EveryDialogueRecordTokenizes) {
    if (!cartridge_.available()) GTEST_SKIP() << cartridge_.skipReason;
    const DteTable dte = corpus().dte();
    ASSERT_TRUE(dte.loaded());
    const std::size_t count = textClassMetadata(TextClass::DLG1).recordCount +
                              textClassMetadata(TextClass::DLG2).recordCount;
    for (std::size_t i = 0; i < count; ++i) {
        EXPECT_NO_THROW({ tokenizeDialogue(corpus().dialogue(i), dte); })
            << "dialogue " << i;
    }
}

// Every battle-text record tokenizes and reassembles byte-for-byte (no DTE in
// this family, so the token stream is a lossless split of the record bytes).
TEST_F(TextCodecRipTest, BattleRecordsTokenizeAndRoundTrip) {
    if (!cartridge_.available()) GTEST_SKIP() << cartridge_.skipReason;
    const TextClass fams[] = {TextClass::ATTACK_MSG, TextClass::BATTLE_DLG,
                              TextClass::MONSTER_DLG};
    for (const TextClass klass : fams) {
        const std::size_t count = textClassMetadata(klass).recordCount;
        for (std::size_t i = 0; i < count; ++i) {
            std::span<const std::uint8_t> record;
            switch (klass) {
                case TextClass::ATTACK_MSG:
                    record = corpus().attackMessage(static_cast<AttackId>(i));
                    break;
                case TextClass::BATTLE_DLG:
                    record = corpus().battleDialogue(i);
                    break;
                default:
                    record = corpus().monsterDialogue(i);
                    break;
            }
            std::vector<BattleTextToken> tokens;
            ASSERT_NO_THROW({ tokens = tokenizeBattle(record); })
                << static_cast<int>(klass) << " record " << i;
            const std::vector<std::uint8_t> back = reassembleBattle(tokens);
            // The tokenizer stops at the END terminator; the reassembly must
            // reproduce the record bytes up to and including that point.
            ASSERT_LE(back.size(), record.size())
                << static_cast<int>(klass) << " record " << i;
            EXPECT_TRUE(std::equal(back.begin(), back.end(), record.begin()))
                << static_cast<int>(klass) << " record " << i;
        }
    }
}

// Every menu-description record decodes to exactly its bytes up to the 0x00
// terminator.
TEST_F(TextCodecRipTest, MenuDescriptionsDecodeToTerminator) {
    if (!cartridge_.available()) GTEST_SKIP() << cartridge_.skipReason;
    const TextClass fams[] = {
        TextClass::ITEM_DESC,      TextClass::MAGIC_DESC,
        TextClass::LORE_DESC,      TextClass::BLITZ_DESC,
        TextClass::BUSHIDO_DESC,   TextClass::GENJU_ATTACK_DESC,
        TextClass::GENJU_BONUS_DESC, TextClass::RARE_ITEM_DESC};
    for (const TextClass klass : fams) {
        const std::size_t count = textClassMetadata(klass).recordCount;
        for (std::size_t i = 0; i < count; ++i) {
            std::span<const std::uint8_t> record;
            switch (klass) {
                case TextClass::ITEM_DESC:
                    record = corpus().itemDescription(static_cast<ItemId>(i));
                    break;
                case TextClass::MAGIC_DESC:
                    record = corpus().magicDescription(i);
                    break;
                case TextClass::LORE_DESC:
                    record = corpus().loreDescription(i);
                    break;
                case TextClass::BLITZ_DESC:
                    record = corpus().blitzDescription(i);
                    break;
                case TextClass::BUSHIDO_DESC:
                    record = corpus().bushidoDescription(i);
                    break;
                case TextClass::GENJU_ATTACK_DESC:
                    record = corpus().genjuAttackDescription(i);
                    break;
                case TextClass::GENJU_BONUS_DESC:
                    record = corpus().genjuBonusDescription(i);
                    break;
                default:
                    record = corpus().rareItemDescription(i);
                    break;
            }
            const auto glyphs = decodeMenuDescription(record);
            // Expected length: up to the first 0x00 (or the whole record).
            std::size_t k = 0;
            while (k < record.size() && record[k] != 0x00) ++k;
            ASSERT_EQ(glyphs.size(), k)
                << static_cast<int>(klass) << " record " << i;
            EXPECT_TRUE(std::equal(glyphs.begin(), glyphs.end(), record.begin()))
                << static_cast<int>(klass) << " record " << i;
        }
    }
}

// Every menu-description record whose bytes are all unambiguous decodes — via
// the codec's glyph output and the parser-emitted char map — to exactly the
// upstream reference text. Records with a list-valued glyph byte or a
// shared-pointer alias carry no independently checkable decode and are counted
// but skipped (see tools/asm_parser/parse_text_meta.py). This proves the
// shipped `.dat` reads back as the real English game text.
TEST_F(TextCodecRipTest, MenuDescriptionsCrossCheckUpstreamText) {
    if (!cartridge_.available()) GTEST_SKIP() << cartridge_.skipReason;
    struct Fam {
        TextClass klass;
        const test::ExpectedMenuDesc* expected;
    };
    const Fam fams[] = {
        {TextClass::ITEM_DESC, test::kItemDescExpected},
        {TextClass::MAGIC_DESC, test::kMagicDescExpected},
        {TextClass::LORE_DESC, test::kLoreDescExpected},
        {TextClass::BLITZ_DESC, test::kBlitzDescExpected},
        {TextClass::BUSHIDO_DESC, test::kBushidoDescExpected},
        {TextClass::GENJU_ATTACK_DESC, test::kGenjuAttackDescExpected},
        {TextClass::GENJU_BONUS_DESC, test::kGenjuBonusDescExpected},
        {TextClass::RARE_ITEM_DESC, test::kRareItemDescExpected},
    };
    std::size_t checked = 0;
    std::size_t skipped = 0;
    for (const Fam& fam : fams) {
        const std::size_t count = textClassMetadata(fam.klass).recordCount;
        for (std::size_t i = 0; i < count; ++i) {
            if (!fam.expected[i].unambiguous) {
                ++skipped;
                continue;
            }
            const auto glyphs = decodeMenuDescription(menuRecord(fam.klass, i));
            std::string got;
            for (const std::uint8_t b : glyphs) {
                const char* g = test::kEnGlyph[b];
                ASSERT_NE(g, nullptr)
                    << static_cast<int>(fam.klass) << " record " << i
                    << " byte 0x" << std::hex << static_cast<int>(b);
                got += g;
            }
            EXPECT_EQ(got, std::string(fam.expected[i].text))
                << static_cast<int>(fam.klass) << " record " << i;
            ++checked;
        }
    }
    // The unambiguous majority (322 of 426 across the eight classes) is exactly
    // cross-checked; the rest are skipped, not silently passed.
    EXPECT_GT(checked, 300u);
    EXPECT_EQ(checked + skipped, 426u);
}

// ---------------------------------------------------------------------------
// JP posture — tokenizer branches built, corpus validation deferred to a J ROM.
// ---------------------------------------------------------------------------

TEST(TextCodecJp, TokenizerBranchesValidationDeferred) {
    GTEST_SKIP() << "JP dialogue/battle corpus not ripped (U-ROM only); "
                    "tokenizer branches (MTE, kanji planes, name splices) "
                    "built, byte validation deferred to a J ROM";
}

}  // namespace
}  // namespace ostinato
