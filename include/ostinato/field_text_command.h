// Control codes that appear in field (map) dialogue records. A dialogue byte
// below 0x20 is a command rather than a glyph; these are the commands the field
// text codec recognizes. Enumerator values are the code byte, and the names
// follow the game's own escape table.
//
// CHARACTER_NAME covers the byte range 0x02..0x0f — the code selects which
// party member's name to splice, reported as the token's operand (0 for the
// first member). WAIT_FRAMES, TAB, and KEY_TIMED each consume one operand byte
// that follows the code; every other command carries no operand.
#pragma once

#include <cstdint>

namespace ostinato {

enum class FieldTextCommand : std::uint8_t {
    END = 0x00,             // end of the record
    NEWLINE = 0x01,         // line break
    CHARACTER_NAME = 0x02,  // splice a party member name (codes 0x02..0x0f)
    WAIT = 0x10,            // timed pause
    WAIT_FRAMES = 0x11,     // timed pause, length from one operand byte
    KEY = 0x12,             // wait for a keypress
    PAGE = 0x13,            // clear to a new page
    TAB = 0x14,             // insert spaces, count from one operand byte
    CHOICE = 0x15,          // multiple-choice marker
    KEY_TIMED = 0x16,       // timed pause then keypress, timeout from one operand byte
    GP = 0x19,              // splice the party's gold amount
    ITEM_NAME = 0x1a,       // splice an item name
    SPELL_NAME = 0x1b,      // splice a spell name
};

}  // namespace ostinato
