#include "data/text_codec.h"

#include <cassert>
#include <stdexcept>
#include <string>

namespace ostinato {

namespace {

std::string byteHex(std::uint8_t byte) {
    static const char* digits = "0123456789abcdef";
    return std::string("0x") + digits[byte >> 4] + digits[byte & 0x0f];
}

// One control code decoded from a record: the command, its operand (0 when the
// command carries none), and how many bytes the code + operand occupy.
struct DecodedControl {
    FieldTextCommand fieldCommand;
    BattleTextCommand battleCommand;
    std::uint8_t operand;
    std::size_t width;  // bytes consumed (1 for a bare code, 2 with an operand)
};

// Read the operand byte that follows a control code, or throw if the record
// ends first.
std::uint8_t operandAt(std::span<const std::uint8_t> record, std::size_t codePos,
                       const char* what) {
    if (codePos + 1 >= record.size()) {
        throw std::runtime_error(std::string("text codec: ") + what +
                                 " control code is missing its operand byte");
    }
    return record[codePos + 1];
}

}  // namespace

std::span<const std::uint8_t> MteTable::expand(std::size_t index) const {
    assert(loaded() && "MTE table not loaded");
    assert(index < offsets_.size() && "MTE code past the table");
    const std::size_t start = offsets_[index];
    const std::size_t end =
        (index + 1 < offsets_.size()) ? offsets_[index + 1] : bytes_.size();
    return bytes_.subspan(start, end - start);
}

namespace {

// Field-dialogue control grammar. `pos` points at the code byte.
DecodedControl decodeFieldControl(std::span<const std::uint8_t> record,
                                   std::size_t pos) {
    const std::uint8_t byte = record[pos];
    DecodedControl out{};
    out.width = 1;
    switch (byte) {
        case 0x00: out.fieldCommand = FieldTextCommand::END; break;
        case 0x01: out.fieldCommand = FieldTextCommand::NEWLINE; break;
        case 0x10: out.fieldCommand = FieldTextCommand::WAIT; break;
        case 0x12: out.fieldCommand = FieldTextCommand::KEY; break;
        case 0x13: out.fieldCommand = FieldTextCommand::PAGE; break;
        case 0x15: out.fieldCommand = FieldTextCommand::CHOICE; break;
        case 0x19: out.fieldCommand = FieldTextCommand::GP; break;
        case 0x1a: out.fieldCommand = FieldTextCommand::ITEM_NAME; break;
        case 0x1b: out.fieldCommand = FieldTextCommand::SPELL_NAME; break;
        case 0x11:
            out.fieldCommand = FieldTextCommand::WAIT_FRAMES;
            out.operand = operandAt(record, pos, "wait-frames");
            out.width = 2;
            break;
        case 0x14:
            out.fieldCommand = FieldTextCommand::TAB;
            out.operand = operandAt(record, pos, "tab");
            out.width = 2;
            break;
        case 0x16:
            out.fieldCommand = FieldTextCommand::KEY_TIMED;
            out.operand = operandAt(record, pos, "timed-key");
            out.width = 2;
            break;
        default:
            // 0x02..0x0f: character-name splice, the low bytes selecting the
            // member; anything else in 0x00..0x1f is outside the grammar.
            if (byte >= 0x02 && byte <= 0x0f) {
                out.fieldCommand = FieldTextCommand::CHARACTER_NAME;
                out.operand = static_cast<std::uint8_t>(byte - 0x02);
                break;
            }
            throw std::runtime_error("field text: unknown control code " +
                                     byteHex(byte));
    }
    return out;
}

// Battle-text control grammar. `pos` points at the code byte. Japanese adds the
// kanji-plane codes 0x1c..0x1f.
DecodedControl decodeBattleControl(std::span<const std::uint8_t> record,
                                   std::size_t pos, TextLanguage lang) {
    const std::uint8_t byte = record[pos];
    DecodedControl out{};
    out.width = 1;
    switch (byte) {
        case 0x00: out.battleCommand = BattleTextCommand::END; break;
        case 0x01: out.battleCommand = BattleTextCommand::NEWLINE; break;
        case 0x04: out.battleCommand = BattleTextCommand::TEXT_COLOR; break;
        case 0x05: out.battleCommand = BattleTextCommand::WAIT; break;
        case 0x07: out.battleCommand = BattleTextCommand::KEY; break;
        case 0x10: out.battleCommand = BattleTextCommand::VAR0; break;
        case 0x11: out.battleCommand = BattleTextCommand::VAR1; break;
        case 0x13: out.battleCommand = BattleTextCommand::VAR2; break;
        case 0x14: out.battleCommand = BattleTextCommand::VAR3; break;
        case 0x02:
            out.battleCommand = BattleTextCommand::CHARACTER_NAME;
            out.operand = operandAt(record, pos, "character-name");
            out.width = 2;
            break;
        case 0x06:
            out.battleCommand = BattleTextCommand::WAIT_FRAMES;
            out.operand = operandAt(record, pos, "wait-frames");
            out.width = 2;
            break;
        case 0x0c:
            out.battleCommand = BattleTextCommand::COMMAND_NAME;
            out.operand = operandAt(record, pos, "command-name");
            out.width = 2;
            break;
        case 0x0e:
            out.battleCommand = BattleTextCommand::ITEM_NAME;
            out.operand = operandAt(record, pos, "item-name");
            out.width = 2;
            break;
        case 0x0f:
            out.battleCommand = BattleTextCommand::ATTACK_NAME;
            out.operand = operandAt(record, pos, "attack-name");
            out.width = 2;
            break;
        case 0x12:
            out.battleCommand = BattleTextCommand::VAR_STRING;
            out.operand = operandAt(record, pos, "variable-string");
            out.width = 2;
            break;
        case 0x1c:
        case 0x1d:
        case 0x1e:
        case 0x1f:
            if (lang != TextLanguage::JP) {
                throw std::runtime_error("battle text: kanji control code " +
                                         byteHex(byte) + " outside Japanese");
            }
            out.battleCommand = static_cast<BattleTextCommand>(byte);
            out.operand = operandAt(record, pos, "kanji");
            out.width = 2;
            break;
        default:
            throw std::runtime_error("battle text: unknown control code " +
                                     byteHex(byte));
    }
    return out;
}

}  // namespace

std::vector<FieldTextToken> tokenizeDialogue(std::span<const std::uint8_t> record,
                                             const DteTable& dte,
                                             TextLanguage lang,
                                             const MteTable& mte) {
    std::vector<FieldTextToken> tokens;
    GlyphRun run;
    auto flush = [&] {
        if (!run.glyphs.empty()) {
            tokens.push_back(GlyphRun{std::move(run.glyphs)});
            run.glyphs.clear();
        }
    };

    std::size_t i = 0;
    while (i < record.size()) {
        const std::uint8_t byte = record[i];
        if (byte < 0x20) {
            const DecodedControl control = decodeFieldControl(record, i);
            flush();
            tokens.push_back(FieldControl{control.fieldCommand, control.operand});
            if (control.fieldCommand == FieldTextCommand::END) {
                return tokens;
            }
            i += control.width;
            continue;
        }
        if (lang == TextLanguage::EN) {
            if (byte < 0x80) {
                run.glyphs.push_back(byte);  // direct glyph (0x7f is a space)
            } else {
                const auto [a, b] = dte.expand(byte);  // DTE pair
                run.glyphs.push_back(a);
                run.glyphs.push_back(b);
            }
            ++i;
            continue;
        }
        // Japanese glyph ranges: direct, multi-tile, name-splice, wide space.
        if (byte <= 0xd7) {
            run.glyphs.push_back(byte);
            ++i;
        } else if (byte <= 0xef) {
            const std::span<const std::uint8_t> tiles =
                mte.expand(static_cast<std::size_t>(byte - 0xd8));
            run.glyphs.insert(run.glyphs.end(), tiles.begin(), tiles.end());
            ++i;
        } else if (byte <= 0xfe) {
            flush();
            tokens.push_back(FieldControl{FieldTextCommand::CHARACTER_NAME,
                                          static_cast<std::uint8_t>(byte - 0xf0)});
            ++i;
        } else {  // 0xff: wide space
            run.glyphs.push_back(byte);
            ++i;
        }
    }
    flush();
    return tokens;
}

std::vector<BattleTextToken> tokenizeBattle(std::span<const std::uint8_t> record,
                                            TextLanguage lang) {
    std::vector<BattleTextToken> tokens;
    GlyphRun run;
    auto flush = [&] {
        if (!run.glyphs.empty()) {
            tokens.push_back(GlyphRun{std::move(run.glyphs)});
            run.glyphs.clear();
        }
    };

    std::size_t i = 0;
    while (i < record.size()) {
        const std::uint8_t byte = record[i];
        if (byte < 0x20) {
            const DecodedControl control = decodeBattleControl(record, i, lang);
            flush();
            tokens.push_back(
                BattleControl{control.battleCommand, control.operand});
            if (control.battleCommand == BattleTextCommand::END) {
                return tokens;
            }
            i += control.width;
            continue;
        }
        run.glyphs.push_back(byte);  // direct glyph
        ++i;
    }
    flush();
    return tokens;
}

std::vector<std::uint8_t> decodeMenuDescription(
    std::span<const std::uint8_t> record) {
    std::vector<std::uint8_t> glyphs;
    for (const std::uint8_t byte : record) {
        if (byte == 0x00) {
            break;  // terminator
        }
        glyphs.push_back(byte);
    }
    return glyphs;
}

}  // namespace ostinato
