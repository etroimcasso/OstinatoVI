#include "scenarios/battle/script_shown.h"

#include <cstdio>
#include <type_traits>
#include <variant>

#include "data/text_codec.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/battle_text_command.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_animation.h"
#include "ostinato/text_token.h"

#include "cells/damage/frame.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/attack_shared.h"

namespace ostinato::trace {

using namespace detail;

namespace {

// A name command's kinds as the queue holds them (ShowAttackName battle_main.asm:8685-8701): Joker
// Doom's and a Blitz's are written as kind 0 (:8760-8761, :8770-8771), and a monster's special attack
// is a command of its own ($11, :8742-8748).
enum class NameKind : std::uint8_t { Attack = 0, Item = 1, Esper = 2, Bushido = 4, Command = 5 };

// An esper's name is shown by its index among the espers' attacks (ShowAttackName_02 :8724-8728).
constexpr std::uint8_t kFirstEsperAttack = static_cast<std::uint8_t>(EsperId::RAMUH);

// What a message's text shows of one of its two numbers.
enum class NumberShown : std::uint8_t { Nothing, LowByte, Whole };

struct NumbersShown {
    NumberShown first = NumberShown::Nothing;
    NumberShown second = NumberShown::Nothing;
};

// How much of each number `message`'s text shows, as the port's text reads its codes
// (battleMessageSteps(), battle/presentation.cpp): a number code shows the whole of its number, and a
// name code a name from the low byte — a party member's from the second, an item's, an attack's or a
// command's from the first. The cartridge the tool hosts is the English one.
NumbersShown numbersShown(std::uint8_t message, const TextCorpus& text) {
    enum : std::uint8_t { CharacterOfSlot = 0, Item = 1, Attack = 2, Command = 3 };
    NumbersShown shown;
    for (const BattleTextToken& token : tokenizeBattle(text.attackMessage(static_cast<AttackId>(message)))) {
        const auto* control = std::get_if<BattleControl>(&token);
        if (control == nullptr) {
            continue;
        }
        if (control->command == BattleTextCommand::VAR0) {
            shown.first = NumberShown::Whole;
        } else if (control->command == BattleTextCommand::VAR1) {
            shown.second = NumberShown::Whole;
        } else if (control->command == BattleTextCommand::VAR_STRING) {
            if (control->operand == CharacterOfSlot && shown.second == NumberShown::Nothing) {
                shown.second = NumberShown::LowByte;
            } else if (control->operand >= Item && control->operand <= Command &&
                       shown.first == NumberShown::Nothing) {
                shown.first = NumberShown::LowByte;
            }
        }
    }
    return shown;
}

std::string numberText(const char* which, NumberShown shown, const std::optional<std::uint32_t>& number) {
    if (shown == NumberShown::Nothing) {
        return {};
    }
    if (!number) {
        return std::string{" "} + which + " none";
    }
    constexpr std::uint32_t kLowByte = 0xFF;
    return std::string{" "} + which + " " + std::to_string(shown == NumberShown::LowByte ? *number & kLowByte : *number);
}

std::string messageText(const battle::BattleMessage& message, const TextCorpus& text) {
    const NumbersShown shown = numbersShown(message.message, text);
    return "message " + std::to_string(message.message) + numberText("first", shown.first, message.firstNumber) +
           numberText("second", shown.second, message.secondNumber);
}

std::string nameText(NameKind kind, std::uint8_t number) {
    return "name " + std::to_string(static_cast<int>(kind)) + " " + std::to_string(number);
}

std::string monstersText(battle::BattlerSet set) {
    return std::to_string(maskOf(set.monstersOnly()) >> 8);
}

// The attack byte an animation shows. Two commands show an index where the port keeps what it names:
// Tools a tool's index among the tools' animations — the item less Noiseblaster's, or 8 for the Chain
// Saw's kill (Cmd_09 battle_main.asm:3933-3937, ToolsEffect_03 :7130-7131), which the port keeps as the
// animation's item and chainSawKill — and Blitz a blitz's among the blitzes', the attack less Pummel's
// (Cmd_0a :3383-3387). Every other command shows its attack.
std::uint8_t attackByte(const battle::AttackAnimation& animation) {
    constexpr std::uint8_t kChainSawKillAnimation = 8;
    const auto* command = std::get_if<BattleCommandId>(&animation.what);
    if (command != nullptr && *command == BattleCommandId::TOOLS && animation.item) {
        if (animation.chainSawKill) {
            return kChainSawKillAnimation;
        }
        return static_cast<std::uint8_t>(static_cast<std::uint8_t>(*animation.item) -
                                         static_cast<std::uint8_t>(ItemId::NOISEBLASTER));
    }
    if (command != nullptr && *command == BattleCommandId::BLITZ) {
        return static_cast<std::uint8_t>(static_cast<std::uint8_t>(animation.attack) -
                                         static_cast<std::uint8_t>(AttackId::PUMMEL));
    }
    return static_cast<std::uint8_t>(animation.attack);
}

std::string animationText(const battle::AttackAnimation& animation) {
    const std::uint8_t what = commandByte(animation.what);
    std::string text = "animation " + std::to_string(what);
    if (animationReadsItsAttack(what)) {
        text += " " + std::to_string(attackByte(animation));
        if (fourthByteIsTheAttacks(animation)) {
            text += " " + std::to_string(fourthByte(animation));
        }
    }
    if (animationReadsItsBlock(what)) {
        text += " | " + frameParamsText(animation.params);
    }
    return text;
}

void addLines(std::vector<std::string>& lines, const battle::BattleScriptCommand& command, const TextCorpus& text) {
    std::visit(
        [&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, battle::ReadyStance>) {
                // A Jump's ready stance shows no targets. The cartridge's aims at the action pool's top
                // slot's: UpdateCharGfxBuf (battle_main.asm:5773, :15259-15275) leaves Y at $FE, and :5779
                // reads $3520,y. The port keeps no counterpart for that slot (docs/Bugs.md, "A Jump's ready
                // stance aims at the action pool's top slot's targets"). Every other stance shows its targets.
                std::string line = "ready stance " + slotText(c.actor) + " command " + std::to_string(commandByte(c.command));
                if (c.command != BattleCommandId::JUMP) {
                    line += " targets " + setText(c.targets);
                }
                lines.push_back(std::move(line));
            } else if constexpr (std::is_same_v<T, battle::StepBack>) {
                lines.emplace_back("step back");
            } else if constexpr (std::is_same_v<T, battle::FormationChanges>) {
                constexpr std::uint8_t kScrollBackground = 0x80;
                const auto shown = static_cast<std::uint8_t>((c.scrollBackground ? kScrollBackground : 0) |
                                                             static_cast<std::uint8_t>(c.exit));
                lines.push_back("formation changes " + std::to_string(shown) + " monsters " + monstersText(c.monsters));
            } else if constexpr (std::is_same_v<T, battle::AttackAnimation>) {
                lines.push_back(animationText(c));
            } else if constexpr (std::is_same_v<T, battle::AttackNameShown>) {
                lines.push_back(nameText(NameKind::Attack, static_cast<std::uint8_t>(c.attack)));
            } else if constexpr (std::is_same_v<T, battle::BushidoNameShown>) {
                lines.push_back(nameText(NameKind::Bushido, static_cast<std::uint8_t>(c.technique)));
            } else if constexpr (std::is_same_v<T, battle::ItemNameShown>) {
                lines.push_back(nameText(NameKind::Item, static_cast<std::uint8_t>(c.item)));
            } else if constexpr (std::is_same_v<T, battle::EsperNameShown>) {
                lines.push_back(nameText(NameKind::Esper,
                                         static_cast<std::uint8_t>(static_cast<std::uint8_t>(c.esper) - kFirstEsperAttack)));
            } else if constexpr (std::is_same_v<T, battle::CommandNameShown>) {
                lines.push_back(nameText(NameKind::Command, commandByte(c.command)));
            } else if constexpr (std::is_same_v<T, battle::MonsterSpecialAttackNameShown>) {
                lines.push_back("monster name " +
                                std::to_string(c.monster ? static_cast<std::uint16_t>(*c.monster) : cells::kNoMonsterNamed));
            } else if constexpr (std::is_same_v<T, battle::BattleMessage>) {
                lines.push_back(messageText(c, text));
            } else if constexpr (std::is_same_v<T, battle::PoseReset>) {
                lines.push_back("pose reset " + slotText(c.battler));
            } else if constexpr (std::is_same_v<T, battle::MonsterDialogShown>) {
                lines.push_back("monster dialog " + std::to_string(c.dialog));
            } else if constexpr (std::is_same_v<T, battle::BattleEventPlays>) {
                lines.push_back("battle event " + std::to_string(c.event));
            } else if constexpr (std::is_same_v<T, battle::MonstersEnterOrLeave>) {
                // GfxCmd_13 btlgfx_main.asm:22177-22206 reads the monsters hidden only when none is revived.
                std::string line = "monsters enter or leave " + std::to_string(static_cast<int>(c.type)) + " revived " +
                                   monstersText(c.revived);
                if (c.revived.monstersOnly().empty()) {
                    line += " hidden " + monstersText(c.hidden);
                }
                lines.push_back(std::move(line));
            } else if constexpr (std::is_same_v<T, battle::MonsterAnimationPlays>) {
                // DoMonsterAnim btlgfx_main.asm:45502-45516: the sound effect reads a pan and a sound where
                // every other animation reads the monsters it plays on.
                if (c.animation == MonsterAnimation::SoundEffect) {
                    lines.push_back("monster animation " + std::to_string(static_cast<int>(c.animation)) + " pan " +
                                    std::to_string(c.pan) + " sound " + std::to_string(c.soundEffect));
                } else {
                    lines.push_back("monster animation " + std::to_string(static_cast<int>(c.animation)) +
                                    " monsters " + monstersText(c.monsters));
                }
            } else if constexpr (std::is_same_v<T, battle::DamageNumbersShown>) {
                if (c.wait == battle::DamageNumberWait::UntilGone) {
                    std::string line = "numerals together";
                    for (const battle::DamageNumber& number : c.numbers) {
                        line += " " + numeralText(number);
                    }
                    lines.push_back(std::move(line));
                } else {
                    for (const battle::DamageNumber& number : c.numbers) {
                        lines.push_back("numeral " + numeralText(number));
                    }
                }
            } else if constexpr (std::is_same_v<T, battle::ReflectedAttackShown> ||
                                 std::is_same_v<T, battle::ScatteredHitsShown>) {
                lines.push_back(shownText(c));
            } else if constexpr (std::is_same_v<T, battle::Wait>) {
                lines.push_back("wait " + std::to_string(c.frames));
            } else {
                static_assert(!sizeof(T), "every kind of script command has a line");
            }
        },
        command);
}

std::string bytesText(const std::array<std::uint8_t, 4>& bytes) {
    char line[32];
    std::snprintf(line, sizeof line, "command $%02X $%02X $%02X $%02X", bytes[0], bytes[1], bytes[2], bytes[3]);
    return line;
}

}  // namespace

bool fourthByteIsTheAttacks(const battle::AttackAnimation& animation) {
    if (animation.params.monsterAttack) {
        return true;
    }
    const auto* command = std::get_if<BattleCommandId>(&animation.what);
    if (command == nullptr) {
        return false;
    }
    switch (*command) {
    case BattleCommandId::FIGHT:
    case BattleCommandId::CAPTURE:
    case BattleCommandId::JUMP:
    case BattleCommandId::DANCE:
        return true;
    case BattleCommandId::SKETCH:
        return !animation.params.targetsHit.empty();
    default:
        return false;
    }
}

std::uint8_t fourthByte(const battle::AttackAnimation& animation) {
    constexpr std::uint8_t kNoBackgroundChange = 0xFF;
    const auto* command = std::get_if<BattleCommandId>(&animation.what);
    if (command != nullptr && *command == BattleCommandId::DANCE) {
        return animation.newBackground ? static_cast<std::uint8_t>(*animation.newBackground) : kNoBackgroundChange;
    }
    return animation.secondary;
}

std::vector<std::string> scriptShown(std::span<const battle::BattleScriptCommand> commands, const TextCorpus& text) {
    std::vector<std::string> lines;
    for (const battle::BattleScriptCommand& command : commands) {
        addLines(lines, command, text);
    }
    return lines;
}

std::vector<std::string> scriptShown(std::span<const ScriptCommandRead> commands, const TextCorpus& text) {
    std::vector<std::string> lines;
    for (const ScriptCommandRead& read : commands) {
        if (read.command) {
            addLines(lines, *read.command, text);
        } else {
            lines.push_back(bytesText(read.bytes));
        }
    }
    return lines;
}

}  // namespace ostinato::trace
