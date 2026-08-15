// Text codecs: turn a raw text record (the glyph + control byte stream returned
// by TextCorpus) into a token stream. Three codec families cover the corpus:
//
//   - Field dialogue (dlg1, dlg2, map titles): glyphs, control codes below
//     0x20, and DTE-compressed byte pairs at 0x80 and above.
//   - Battle text (battle dialogue, attack messages, monster dialogue): glyphs
//     and control codes below 0x20; no DTE.
//   - Menu descriptions (item/magic/lore/... descriptions): plain glyph bytes
//     up to a 0x00 terminator; no control codes, no DTE.
//
// Each Japanese-language branch is built but its corpus validation waits on a
// Japanese ROM (the U-ROM rip ships no Japanese text bytes).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "data/text_corpus.h"  // DteTable
#include "ostinato/text_token.h"

namespace ostinato {

// Which ROM language a record was ripped from. Selects the glyph-range and
// escape grammar the dialogue and battle codecs apply.
enum class TextLanguage : std::uint8_t { EN, JP };

// The Japanese multi-tile encoding table: variable-length glyph expansions, one
// per MTE code, located by an offset table (same shape as the pointer text
// classes). Empty on the English path.
class MteTable {
public:
    MteTable() = default;
    MteTable(std::span<const std::uint8_t> bytes,
             std::span<const std::uint32_t> offsets)
        : bytes_(bytes), offsets_(offsets) {}

    bool loaded() const { return !offsets_.empty(); }
    std::size_t size() const { return offsets_.size(); }

    // The glyph bytes MTE code `index` expands to. PRECONDITION (asserted):
    // the table is loaded and index < size().
    std::span<const std::uint8_t> expand(std::size_t index) const;

private:
    std::span<const std::uint8_t> bytes_{};
    std::span<const std::uint32_t> offsets_{};
};

// Tokenize a field-dialogue record. DTE codes are expanded through `dte`; on
// the Japanese path multi-tile codes are expanded through `mte`. Stops at the
// end-of-record control code (or the end of the span). Throws
// std::runtime_error on a control byte the grammar does not define.
std::vector<FieldTextToken> tokenizeDialogue(
    std::span<const std::uint8_t> record, const DteTable& dte,
    TextLanguage lang = TextLanguage::EN, const MteTable& mte = MteTable{});

// Tokenize a battle-text record (no DTE). Stops at the end-of-record control
// code (or the end of the span). Throws std::runtime_error on a control byte
// the grammar does not define for `lang`.
std::vector<BattleTextToken> tokenizeBattle(
    std::span<const std::uint8_t> record, TextLanguage lang = TextLanguage::EN);

// Decode a menu-description record to its glyph bytes, up to (not including) the
// 0x00 terminator. There are no control codes or DTE in this family.
std::vector<std::uint8_t> decodeMenuDescription(
    std::span<const std::uint8_t> record);

}  // namespace ostinato
