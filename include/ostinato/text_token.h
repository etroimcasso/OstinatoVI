// The token stream a text record decodes into. Each record becomes a sequence
// of tokens: runs of glyphs interleaved with control tokens. This is the
// structural decode — it identifies glyphs and commands but does not resolve
// spliced content (item/character/spell names) or apply any presentation.
#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "ostinato/battle_text_command.h"
#include "ostinato/field_text_command.h"

namespace ostinato {

// A run of consecutive glyphs. Each byte is a glyph index into the game's font;
// no readable-character mapping is shipped. Dialogue DTE codes are expanded
// into their glyph bytes here, so a run holds only plain glyph indices.
struct GlyphRun {
    std::vector<std::uint8_t> glyphs;
};

// A field-dialogue control token: the command and its operand byte (0 when the
// command carries none).
struct FieldControl {
    FieldTextCommand command;
    std::uint8_t operand;
};

// A battle-text control token: the command and its operand byte (0 when the
// command carries none).
struct BattleControl {
    BattleTextCommand command;
    std::uint8_t operand;
};

// One token of a decoded field-dialogue record: a glyph run or a control token.
using FieldTextToken = std::variant<GlyphRun, FieldControl>;

// One token of a decoded battle-text record: a glyph run or a control token.
using BattleTextToken = std::variant<GlyphRun, BattleControl>;

}  // namespace ostinato
