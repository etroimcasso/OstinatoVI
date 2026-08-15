// Control codes that appear in battle text records (battle dialogue, attack
// messages, monster dialogue). A record byte below 0x20 is a command rather
// than a glyph; these are the commands the battle text codec recognizes.
// Enumerator values are the code byte, and the names follow the game's own
// escape table.
//
// CHARACTER_NAME, WAIT_FRAMES, COMMAND_NAME, ITEM_NAME, ATTACK_NAME, and
// VAR_STRING each consume one operand byte that follows the code (the id to
// splice, a pause length, or a variable-string selector); every other command
// carries no operand. KANJI_PLANE_1..4 appear only in Japanese text; each takes
// one operand byte selecting a character within its kanji plane.
#pragma once

#include <cstdint>

namespace ostinato {

enum class BattleTextCommand : std::uint8_t {
    END = 0x00,             // end of the record
    NEWLINE = 0x01,         // line break
    CHARACTER_NAME = 0x02,  // splice a party member name, id from one operand byte
    TEXT_COLOR = 0x04,      // toggle the text colour
    WAIT = 0x05,            // timed pause
    WAIT_FRAMES = 0x06,     // timed pause, length from one operand byte
    KEY = 0x07,             // wait for a keypress
    COMMAND_NAME = 0x0c,    // splice a battle command name, id from one operand byte
    ITEM_NAME = 0x0e,       // splice an item name, id from one operand byte
    ATTACK_NAME = 0x0f,     // splice an attack/spell/esper name, id from one operand byte
    VAR0 = 0x10,            // splice battle variable 0
    VAR1 = 0x11,            // splice battle variable 1
    VAR_STRING = 0x12,      // splice a variable string, selector from one operand byte
    VAR2 = 0x13,            // splice battle variable 2
    VAR3 = 0x14,            // splice battle variable 3
    KANJI_PLANE_1 = 0x1c,   // Japanese kanji, index from one operand byte
    KANJI_PLANE_2 = 0x1d,
    KANJI_PLANE_3 = 0x1e,
    KANJI_PLANE_4 = 0x1f,
};

}  // namespace ostinato
