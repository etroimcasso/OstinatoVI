#include "battle/presentation.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "data/battle_gfx_tables.h"
#include "data/text_codec.h"
#include "data/text_metadata.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_text_command.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"
#include "ostinato/text_class.h"
#include "text/layout.h"

namespace ostinato::battle {

namespace {

// --- the poses (btlgfx _c13071 :6816-6857) -------------------------------------------------------

constexpr std::uint8_t kFallenPose = 1;
constexpr std::uint8_t kChantingPose = 9;
constexpr std::uint8_t kWoundedPose = 0x0a;
constexpr std::uint8_t kConfusedPose = 0x25;
constexpr std::uint8_t kIdlePose = 6;

// The statuses that take a monster off the list of names (battle_main :15202-15204).
const StatusSet kFallen = StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE);

// --- the text ----------------------------------------------------------------------------------

constexpr std::uint8_t raw(AttackId id) { return static_cast<std::uint8_t>(id); }

// The first spell an esper attack's name is read for, the first attack read from the attack names
// (btlgfx DlgTextCmd_0f :14278-14287), and the first Bushido technique.
constexpr std::uint8_t kFirstEsperAttack = 0x36;
constexpr std::uint8_t kFirstNamedAttack = 0x51;
constexpr AttackId kFirstBushidoTechnique = AttackId::DISPATCH;

// The byte a name ends early with, and the byte the empty item draws its blanks with.
constexpr std::uint8_t kNameEnd = text::kNameEnd;
constexpr std::uint8_t kBlankLetter = 0xFF;

// An empty item, a missing command.
constexpr std::uint8_t kNoItem = 0xFF;
constexpr std::uint8_t kNoCommand = 0xFF;

// A pause of WAIT (DlgTextCmd_05 :14063-14065).
constexpr int kWaitFrames = 60;

// A number splices as up to eight digits, leading zeros dropped but the last always shown
// (DlgTextCmd_10 :14234-14251).
constexpr int kNumberDigits = 8;

// The narrow window's text is centered by moving it right by half of what its width falls short
// of 104 pixels, and 2 more (chg_center :22926-22942).
constexpr int kNarrowCenteringWidth = 104;
constexpr int kNarrowCenteringExtra = 2;

// The name kinds' rows in the name kind table, by what the battle shows (_c197a1 :22773-22774).
constexpr int kAttackNameKind = 0;
constexpr int kItemNameKind = 1;
constexpr int kEsperNameKind = 2;
constexpr int kBushidoNameKind = 4;
constexpr int kCommandNameKind = 5;

// An esper's name in one of the esper name lists, at its place among the espers: the esper ids
// count from Ramuh's. Empty past the end of the list.
std::span<const std::uint8_t> esperName(const TextCorpus& text, TextClass names, EsperId esper) {
    const std::size_t size = textClassMetadata(names).recordSize;
    const std::size_t at = (static_cast<std::size_t>(esper) - static_cast<std::size_t>(EsperId::RAMUH)) * size;
    const std::span<const std::uint8_t> list = text.rawBytes(names);
    return at + size <= list.size() ? list.subspan(at, size) : std::span<const std::uint8_t>{};
}

Language languageOf(const BattleState& battle) { return language(battle.version); }

TextLanguage textLanguage(Language lang) { return lang == Language::EN ? TextLanguage::EN : TextLanguage::JP; }

// A letter of battle text as a dialog glyph: English battle text is in the menu font's encoding.
std::uint16_t glyphOf(std::uint8_t letter, Language lang) {
    if (lang == Language::EN) {
        assert(letter >= text::kMenuToDialogOffset + text::kFirstDrawnGlyph && "battle letter below the font");
        return static_cast<std::uint16_t>(letter - text::kMenuToDialogOffset);
    }
    return letter;
}

void addLetters(std::vector<MessageStep>& steps, std::span<const std::uint8_t> letters, Language lang) {
    for (const std::uint8_t letter : letters) {
        if (letter == kNameEnd) {
            return;
        }
        steps.emplace_back(MessageGlyph{glyphOf(letter, lang)});
    }
}

// The name of a spell, an esper or an attack, as a text command splices it (DlgTextCmd_0f).
void addAttackName(std::vector<MessageStep>& steps, std::uint8_t attack, const TextCorpus& text, Language lang) {
    if (attack == kNameEnd) {
        return;
    }
    if (attack < kFirstEsperAttack) {
        addLetters(steps, text.magicName(attack).subspan(1), lang);  // past the spell's symbol
        return;
    }
    if (lang == Language::EN && attack >= kFirstNamedAttack) {
        addLetters(steps, text.attackName(attack - kFirstNamedAttack), lang);
        return;
    }
    addLetters(steps, esperName(text, TextClass::GENJU_NAME, static_cast<EsperId>(attack)), lang);
}

// An item's name (DlgTextCmd_0e): past its symbol, or a row of blanks for no item.
void addItemName(std::vector<MessageStep>& steps, std::uint8_t item, const TextCorpus& text, Language lang) {
    const std::span<const std::uint8_t> name = text.itemName(static_cast<ItemId>(item));
    if (item == kNoItem) {
        steps.insert(steps.end(), name.size(), MessageGlyph{glyphOf(kBlankLetter, lang)});
        return;
    }
    addLetters(steps, name.subspan(1), lang);
}

// A battle command's name (DlgTextCmd_0c).
void addCommandName(std::vector<MessageStep>& steps, std::uint8_t command, const TextCorpus& text, Language lang) {
    if (command == kNoCommand) {
        return;
    }
    addLetters(steps, text.battleCommandName(command), lang);
}

// The name of the character whose record is for `actor` (DlgTextCmd_02 :14073-14099).
void addCharacterName(std::vector<MessageStep>& steps, std::uint8_t actor, const BattleState& battle, Language lang) {
    for (const game::CharacterRecord& record : battle.party.characters) {
        if (static_cast<std::uint8_t>(record.actor) == actor) {
            addLetters(steps, record.name, lang);
            return;
        }
    }
}

// The name of the party member in `slot` (_c15f40 :14181-14198).
void addPartyMemberName(std::vector<MessageStep>& steps, std::uint32_t slot, const BattleState& battle,
                        Language lang) {
    if (slot >= static_cast<std::uint32_t>(kPartySlots)) {
        return;
    }
    const Battler& member = battle.at(BattlerSlot::character(static_cast<int>(slot)));
    if (member.record) {
        addLetters(steps, battle.party.characters[*member.record].name, lang);
    }
}

void addNumber(std::vector<MessageStep>& steps, std::uint32_t number, Language lang) {
    const std::uint16_t zero = lang == Language::EN ? text::kEnglishDigitGlyph : text::kJapaneseDigitGlyph;
    std::array<int, kNumberDigits> digits{};
    for (int i = kNumberDigits - 1; i >= 0; --i) {
        digits[static_cast<std::size_t>(i)] = static_cast<int>(number % 10);
        number /= 10;
    }
    int first = 0;
    while (first < kNumberDigits - 1 && digits[static_cast<std::size_t>(first)] == 0) {
        ++first;
    }
    for (int i = first; i < kNumberDigits; ++i) {
        steps.emplace_back(MessageGlyph{static_cast<std::uint16_t>(zero + digits[static_cast<std::size_t>(i)])});
    }
}

// What a battle text's variables hold: the first two are the message's numbers, which double as
// the item, attack, command or party slot a variable string names. No message reads the last two.
struct TextVariables {
    std::optional<std::uint32_t> first;
    std::optional<std::uint32_t> second;
};

std::vector<MessageStep> textSteps(std::span<const std::uint8_t> record, const TextVariables& variables,
                                   const BattleState& battle, const TextCorpus& text) {
    const Language lang = languageOf(battle);
    std::vector<MessageStep> steps;
    for (const BattleTextToken& token : tokenizeBattle(record, textLanguage(lang))) {
        if (const auto* run = std::get_if<GlyphRun>(&token)) {
            for (const std::uint8_t letter : run->glyphs) {
                steps.emplace_back(MessageGlyph{glyphOf(letter, lang)});
            }
            continue;
        }
        const auto& control = std::get<BattleControl>(token);
        switch (control.command) {
        case BattleTextCommand::END:
        case BattleTextCommand::TEXT_COLOR:
            break;
        case BattleTextCommand::NEWLINE:
            steps.emplace_back(MessageLineBreak{});
            break;
        case BattleTextCommand::CHARACTER_NAME:
            addCharacterName(steps, control.operand, battle, lang);
            break;
        case BattleTextCommand::WAIT:
            steps.emplace_back(MessagePause{kWaitFrames});
            break;
        case BattleTextCommand::WAIT_FRAMES:
            steps.emplace_back(MessagePause{control.operand});
            break;
        case BattleTextCommand::KEY:
            steps.emplace_back(MessageKeyWait{});
            break;
        case BattleTextCommand::COMMAND_NAME:
            addCommandName(steps, control.operand, text, lang);
            break;
        case BattleTextCommand::ITEM_NAME:
            addItemName(steps, control.operand, text, lang);
            break;
        case BattleTextCommand::ATTACK_NAME:
            addAttackName(steps, control.operand, text, lang);
            break;
        case BattleTextCommand::VAR0:
            if (variables.first) {
                addNumber(steps, *variables.first, lang);
            }
            break;
        case BattleTextCommand::VAR1:
            if (variables.second) {
                addNumber(steps, *variables.second, lang);
            }
            break;
        case BattleTextCommand::VAR2:
        case BattleTextCommand::VAR3:
            break;
        case BattleTextCommand::VAR_STRING: {
            // _c15ef0 :14117-14118: the character in variable 1's slot, or the item, attack or
            // command in variable 0.
            enum : std::uint8_t { CharacterOfSlot, Item, Attack, Command };
            const auto first = static_cast<std::uint8_t>(variables.first.value_or(kNameEnd));
            switch (control.operand) {
            case CharacterOfSlot:
                if (variables.second) {
                    addPartyMemberName(steps, *variables.second, battle, lang);
                }
                break;
            case Item:
                addItemName(steps, first, text, lang);
                break;
            case Attack:
                addAttackName(steps, first, text, lang);
                break;
            case Command:
                addCommandName(steps, first, text, lang);
                break;
            default:
                break;
            }
            break;
        }
        case BattleTextCommand::KANJI_PLANE_1:
        case BattleTextCommand::KANJI_PLANE_2:
        case BattleTextCommand::KANJI_PLANE_3:
        case BattleTextCommand::KANJI_PLANE_4: {
            const int plane = static_cast<int>(control.command) - static_cast<int>(BattleTextCommand::KANJI_PLANE_1) + 1;
            steps.emplace_back(MessageGlyph{static_cast<std::uint16_t>(plane << 8 | control.operand)});
            break;
        }
        }
    }
    return steps;
}

// The name the narrow window shows for a name kind's row, read the way the row says.
std::vector<MessageStep> nameKindSteps(int kind, std::uint8_t number, const BattleState& battle,
                                       const TextCorpus& text) {
    const Language lang = languageOf(battle);
    const NameKindText reading = nameKindText(kind);
    std::vector<MessageStep> steps;
    if (reading.isEsperAttackName()) {
        addLetters(steps, esperName(text, TextClass::GENJU_ATTACK_NAME, static_cast<EsperId>(number)), lang);
    } else if (reading.isBushidoName()) {
        addLetters(steps, text.bushidoName(number), lang);
    } else if (const std::optional<BattleTextCommand> command = reading.command()) {
        switch (*command) {
        case BattleTextCommand::ATTACK_NAME:
            addAttackName(steps, number, text, lang);
            break;
        case BattleTextCommand::ITEM_NAME:
            addItemName(steps, number, text, lang);
            break;
        case BattleTextCommand::COMMAND_NAME:
            addCommandName(steps, number, text, lang);
            break;
        default:
            break;
        }
    }
    return steps;
}

// --- playing a message -----------------------------------------------------------------------

MessagePlayback startMessage(MessageWindow window, std::vector<MessageStep> steps, bool typesOut, int holdFrames) {
    MessagePlayback playback;
    playback.window = window;
    playback.steps = std::move(steps);
    playback.typesOut = typesOut;
    playback.holdFrames = holdFrames;
    playback.shown.window = window;
    return playback;
}

// The narrow window's text moves right to sit centered; the shift is worked out from the pen
// once the text is all down.
int narrowCentering(int pen) {
    const int shortfall = kNarrowCenteringWidth - pen;
    return (shortfall >= 0 ? shortfall / 2 : -((-shortfall + 1) / 2)) + kNarrowCenteringExtra;
}

// One frame of a line break: the line shown rises a pixel, and is gone after the last.
void scrollLine(MessagePlayback& message) {
    for (ShownGlyph& glyph : message.shown.glyphs) {
        ++glyph.rise;
    }
    if (--message.scrollLeft == 0) {
        message.shown.glyphs.clear();
        message.pen = 0;
    }
}

// Play one frame of a message. Returns true once it has finished: the frame it closes, which
// shows no window. The frame its text completes shows it, and so does every frame of its hold.
bool playMessageFrame(MessagePlayback& message, const ostinato::text::FontMetrics& metrics, bool confirmPressed) {
    if (message.textDone) {
        if (message.holdLeft <= 0) {
            return true;
        }
        --message.holdLeft;
        return false;
    }
    if (message.awaitingKey) {
        if (!confirmPressed) {
            return false;
        }
        message.awaitingKey = false;
        message.keyWaited = true;
    }
    if (message.pauseLeft > 0) {
        --message.pauseLeft;
        return false;
    }
    if (message.scrollLeft > 0) {
        scrollLine(message);
        return false;
    }
    while (message.next < message.steps.size()) {
        const MessageStep& step = message.steps[message.next++];
        if (const auto* glyph = std::get_if<MessageGlyph>(&step)) {
            message.shown.glyphs.push_back(ShownGlyph{.glyph = glyph->glyph, .x = message.pen});
            message.pen += metrics.width(glyph->glyph);
            if (message.window == MessageWindow::Narrow) {
                message.shown.centering = narrowCentering(message.pen);
            }
            if (message.typesOut) {
                return false;
            }
            continue;
        }
        if (std::holds_alternative<MessageLineBreak>(step)) {
            message.scrollLeft = kLineScrollFrames;
            scrollLine(message);
            return false;
        }
        if (const auto* pause = std::get_if<MessagePause>(&step)) {
            // The frame the pause begins is its first; the text goes on after one more.
            message.pauseLeft = pause->frames;
            if (pause->frames > 0) {
                return false;
            }
            continue;
        }
        message.awaitingKey = true;
        return false;
    }
    message.textDone = true;
    message.holdLeft = message.keyWaited ? 0 : message.holdFrames;
    return false;
}

// --- damage numerals ---------------------------------------------------------------------------

int digitCount(std::uint16_t amount) {
    int digits = 1;
    for (std::uint16_t rest = amount / 10; rest != 0; rest /= 10) {
        ++digits;
    }
    return digits;
}

// Half a numeral's width, from where it stands to where its first digit starts: 4 a digit
// (GfxCmd_0b :24733-24749, _c1a5fa :24894-24911). "Miss" takes 8 shown one at a time and 0 shown
// together (:24717-24718, :24841).
constexpr int kDigitHalfWidth = 4;
constexpr int kSingleMissOffset = 8;
constexpr int kTogetherMissOffset = 0;

ShownNumeral numeral(const DamageNumber& number, bool together) {
    assert(number.amount <= kLargestDamageNumber && "a numeral shows at most four digits");
    return ShownNumeral{
        .target = number.target,
        .amount = number.amount,
        .miss = number.miss,
        .heals = number.heals,
        .offset = number.miss ? (together ? kTogetherMissOffset : kSingleMissOffset)
                              : kDigitHalfWidth * digitCount(number.amount),
    };
}

// Whether a battler can have a numeral over it: a party member on the field, a monster shown
// (_c12bfc :6188-6242, _c1a5fa :24923-24936).
bool numeralShowsOver(BattlerSlot target, const BattlePresentationState& shown, const BattleState& battle) {
    if (target.isMonster()) {
        return shown.monstersShown.has(target);
    }
    return battle.at(target).present && !battle.awayFromTheField.has(target);
}

bool anyNumeralShown(const BattlePresentationState& shown) {
    const auto showing = [](const std::optional<ShownNumeral>& n) { return n.has_value(); };
    return std::any_of(shown.singleNumerals.begin(), shown.singleNumerals.end(), showing) ||
           std::any_of(shown.numeralsTogether.begin(), shown.numeralsTogether.end(), showing);
}

void advanceNumerals(BattlePresentationState& shown, const BattleState& battle) {
    const auto advance = [&](std::optional<ShownNumeral>& n) {
        if (!n) {
            return;
        }
        if (++n->frame >= kDamageNumberLife || !numeralShowsOver(n->target, shown, battle)) {
            n.reset();
        }
    };
    std::for_each(shown.singleNumerals.begin(), shown.singleNumerals.end(), advance);
    std::for_each(shown.numeralsTogether.begin(), shown.numeralsTogether.end(), advance);
}

// A numeral shown one at a time appears on the command's second frame (GfxCmd_0b :24671, :24782).
constexpr int kSingleNumeralFrames = 2;

// The first frame of a numeral shown one at a time: the thread it takes is cleared, and a numeral
// already over the same battler makes it wait (GfxCmd_0b :24665-24681).
void beginSingleNumeral(BattlePresentationState& shown) {
    shown.singleNumerals[shown.nextThread % kSingleNumeralThreads].reset();
    const BattlerSlot target = shown.numeralsToShow.front().target;
    const bool sameTarget = std::any_of(shown.singleNumerals.begin(), shown.singleNumerals.end(),
                                        [&](const std::optional<ShownNumeral>& n) { return n && n->target == target; });
    shown.waitLeft = kSingleNumeralFrames + (sameTarget ? kSameTargetWaitFrames : 0);
}

// --- one script command -----------------------------------------------------------------------

// Start `command`. Returns true when it takes frames and is now playing.
bool startCommand(BattlePresentationState& shown, const BattleScriptCommand& command, const BattleState& battle,
                  const BattlePresentationContext& context) {
    const int hold = messageHoldFrames(std::min<int>(context.messageSpeed, kSlowestMessageSpeed));
    const auto name = [&](int kind, std::uint8_t number) {
        shown.message = startMessage(MessageWindow::Narrow, nameKindSteps(kind, number, battle, context.text), false, hold);
        return true;
    };
    return std::visit(
        [&](const auto& c) -> bool {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, ReadyStance>) {
                if (c.actor.isCharacter()) {
                    shown.waitingPoses[static_cast<std::size_t>(c.actor.position())] = commandWaitingPose(c.command);
                }
                return false;
            } else if constexpr (std::is_same_v<T, StepBack>) {
                shown.waitingPoses = {};
                return false;
            } else if constexpr (std::is_same_v<T, PoseReset>) {
                if (c.battler.isCharacter()) {
                    shown.waitingPoses[static_cast<std::size_t>(c.battler.position())].reset();
                }
                return false;
            } else if constexpr (std::is_same_v<T, FormationChanges>) {
                shown.monstersShown = c.monsters.monstersOnly();
                return false;
            } else if constexpr (std::is_same_v<T, MonstersEnterOrLeave>) {
                if (!c.revived.empty()) {
                    shown.monstersShown = shown.monstersShown | c.revived.monstersOnly();
                } else {
                    shown.monstersShown = shown.monstersShown.without(c.hidden);
                }
                return false;
            } else if constexpr (std::is_same_v<T, AttackNameShown>) {
                return name(kAttackNameKind, raw(c.attack));
            } else if constexpr (std::is_same_v<T, ItemNameShown>) {
                return name(kItemNameKind, static_cast<std::uint8_t>(c.item));
            } else if constexpr (std::is_same_v<T, EsperNameShown>) {
                return name(kEsperNameKind, static_cast<std::uint8_t>(c.esper));
            } else if constexpr (std::is_same_v<T, BushidoNameShown>) {
                return name(kBushidoNameKind, static_cast<std::uint8_t>(raw(c.technique) - raw(kFirstBushidoTechnique)));
            } else if constexpr (std::is_same_v<T, CommandNameShown>) {
                return name(kCommandNameKind, static_cast<std::uint8_t>(c.command));
            } else if constexpr (std::is_same_v<T, MonsterSpecialAttackNameShown>) {
                std::vector<MessageStep> steps;
                addLetters(steps, context.text.monsterSpecialName(c.monster), languageOf(battle));
                shown.message = startMessage(MessageWindow::Narrow, std::move(steps), false, kSpecialAttackNameFrames);
                return true;
            } else if constexpr (std::is_same_v<T, BattleMessage>) {
                shown.message = startMessage(MessageWindow::Wide, battleMessageSteps(c, battle, context.text), false, hold);
                return true;
            } else if constexpr (std::is_same_v<T, MonsterDialogShown>) {
                shown.message = startMessage(MessageWindow::Wide, monsterDialogSteps(c, battle, context.text), true, hold);
                return true;
            } else if constexpr (std::is_same_v<T, DamageNumbersShown>) {
                if (c.wait == DamageNumberWait::UntilGone) {
                    shown.numeralsTogether = {};
                    for (const DamageNumber& number : c.numbers) {
                        if (numeralShowsOver(number.target, shown, battle)) {
                            shown.numeralsTogether[static_cast<std::size_t>(number.target.index())] = numeral(number, true);
                        }
                    }
                    shown.waitingForNumerals = true;
                    return true;
                }
                if (c.numbers.empty()) {
                    return false;
                }
                shown.numeralsToShow.assign(c.numbers.begin(), c.numbers.end());
                beginSingleNumeral(shown);
                return true;
            } else {
                // AttackAnimation, BattleEventPlays, MonsterAnimationPlays: their pictures are not
                // shown here.
                return false;
            }
        },
        command);
}

// Play one frame of the command playing now. Returns true once it has finished.
bool playCommandFrame(BattlePresentationState& shown, const BattleState& battle,
                      const BattlePresentationContext& context, bool confirmPressed) {
    if (shown.message) {
        if (playMessageFrame(*shown.message, context.metrics, confirmPressed)) {
            shown.message.reset();
            return true;
        }
        return false;
    }
    if (shown.waitingForNumerals) {
        if (anyNumeralShown(shown)) {
            return false;
        }
        shown.waitingForNumerals = false;
        return true;
    }
    if (!shown.numeralsToShow.empty()) {
        if (--shown.waitLeft > 0) {
            return false;
        }
        const DamageNumber number = shown.numeralsToShow.front();
        shown.numeralsToShow.pop_front();
        if (numeralShowsOver(number.target, shown, battle)) {
            shown.singleNumerals[shown.nextThread % kSingleNumeralThreads] = numeral(number, false);
        }
        ++shown.nextThread;
        if (shown.numeralsToShow.empty()) {
            return true;
        }
        beginSingleNumeral(shown);
        return false;
    }
    return true;
}

bool commandPlaying(const BattlePresentationState& shown) {
    return shown.message || shown.waitingForNumerals || !shown.numeralsToShow.empty();
}

// Move past the command just finished, and past any script it finished.
void finishCommand(BattlePresentationState& shown) {
    ++shown.command;
    while (!shown.scripts.empty() && shown.command >= shown.scripts.front().size()) {
        shown.scripts.pop_front();
        shown.command = 0;
    }
}

// Play the scripts for one frame: zero-frame commands run straight through, and the frame ends on
// the first command that takes frames.
void playScripts(BattlePresentationState& shown, const BattleState& battle, const BattlePresentationContext& context,
                 bool confirmPressed) {
    if (commandPlaying(shown)) {
        if (playCommandFrame(shown, battle, context, confirmPressed)) {
            finishCommand(shown);
        }
        return;
    }
    while (!shown.scripts.empty()) {
        if (shown.command >= shown.scripts.front().size()) {
            finishCommand(shown);
            continue;
        }
        const BattleScriptCommand& command = shown.scripts.front()[shown.command];
        if (startCommand(shown, command, battle, context)) {
            if (playCommandFrame(shown, battle, context, confirmPressed)) {
                finishCommand(shown);
            }
            return;
        }
        finishCommand(shown);
    }
}

// --- a monster's death (MonsterDeathAnim :22222-22317) ------------------------------------------

void playDeaths(BattlePresentationState& shown, const BattleState& battle) {
    if (shown.deathFade) {
        if (++shown.deathFade->frame > kDeathFadeFrames) {
            shown.monstersShown = shown.monstersShown.without(shown.deathFade->dying);
            shown.deathFade.reset();
        }
        return;
    }
    const BattlerSet dying = shown.monstersShown.without(battle.monstersStanding).monstersOnly();
    if (!dying.empty()) {
        shown.deathFade = DeathFade{.dying = dying};
        return;
    }
    shown.monstersShown = shown.monstersShown | battle.monstersStanding.monstersOnly();
}

}  // namespace

// --- the text ----------------------------------------------------------------------------------

std::vector<MessageStep> battleMessageSteps(const BattleMessage& message, const BattleState& battle,
                                            const TextCorpus& text) {
    return textSteps(text.attackMessage(static_cast<AttackId>(message.message)),
                     {.first = message.firstNumber, .second = message.secondNumber}, battle, text);
}

std::vector<MessageStep> monsterDialogSteps(const MonsterDialogShown& dialog, const BattleState& battle,
                                            const TextCorpus& text) {
    return textSteps(text.monsterDialog(dialog.dialog), {}, battle, text);
}

// --- what the presentation works out ----------------------------------------------------------

MonsterNameList monsterNameList(const BattleState& battle) {
    MonsterNameList list{};
    for (int position = 0; position < kMonsterSlots; ++position) {
        const BattlerSlot slot = BattlerSlot::monster(position);
        const Battler& monster = battle.at(slot);
        if (!monster.present || battle.monstersFallen.has(slot) || !battle.countedInBattle.has(slot) ||
            monster.status.hasAny(kFallen) || monster.nameHidden || !monster.nameGroup) {
            continue;
        }
        for (std::optional<MonsterId>& line : list) {
            if (!line) {
                line = monster.nameGroup;
                break;
            }
            if (*line == *monster.nameGroup) {
                break;
            }
        }
    }
    return list;
}

std::uint8_t partyMemberPose(const Battler& member, std::optional<std::uint8_t> waitingPose) {
    if (member.status.has(StatusId::DEAD)) {
        return kFallenPose;
    }
    if (member.status.has(StatusId::SLEEP)) {
        return kWoundedPose;
    }
    if (member.status.has(StatusId::CONFUSE)) {
        return kConfusedPose;
    }
    if (member.status.has(StatusId::CHANT)) {
        return kChantingPose;
    }
    if (member.status.has(StatusId::POISON) || member.status.has(StatusId::NEAR_FATAL)) {
        return kWoundedPose;
    }
    return waitingPose.value_or(kIdlePose);
}

// --- running it -------------------------------------------------------------------------------

void tickBattlePresentation(BattlePresentationState& shown, const BattleState& battle,
                            std::span<const BattleRequest> requests, const BattlePresentationContext& context,
                            bool confirmPressed) {
    for (const BattleRequest& request : requests) {
        if (std::holds_alternative<OpenBattleScene>(request)) {
            shown.sceneOpen = true;
            shown.monstersShown = battle.monstersStanding.monstersOnly();
            shown.monsterNames = monsterNameList(battle);
        } else if (std::holds_alternative<RefreshMonsterNames>(request)) {
            shown.monsterNames = monsterNameList(battle);
        } else if (const auto* script = std::get_if<RunBattleScript>(&request)) {
            shown.scripts.push_back(script->commands);
        } else if (std::holds_alternative<CloseBattleScene>(request)) {
            shown.sceneClosed = true;
        }
    }

    for (std::uint8_t& counter : shown.poseCounters) {
        ++counter;
    }
    advanceNumerals(shown, battle);

    if (commandPlaying(shown) || !shown.scripts.empty()) {
        playScripts(shown, battle, context, confirmPressed);
    } else {
        playDeaths(shown, battle);
    }

    shown.idle = !commandPlaying(shown) && shown.scripts.empty() && !shown.deathFade;
    shown.timeRunning = true;
}

BattleInput battleInput(const BattlePresentationState& shown) {
    return BattleInput{.timeRunning = shown.timeRunning, .presentationIdle = shown.idle};
}

bool awaitingKey(const BattlePresentationState& shown) {
    return shown.message && shown.message->awaitingKey;
}

}  // namespace ostinato::battle
