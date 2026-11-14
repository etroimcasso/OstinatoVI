// The port's reward steps, the messages a call shows, the differences between two results, and the
// reward sweeps' draws.

#include "scenarios/ending/rewards.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "battle/battle_end.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "battle/rewards.h"
#include "data/level_up.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/game_limits.h"
#include "ostinato/game_version.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

#include "cells/damage/frame.h"
#include "cells/ending/party.h"
#include "cells/ending/records.h"
#include "cells/status/effects.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/ending/records_shared.h"
#include "scenarios/ending/rewards_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {

// Each bit `bits` holds, laid into bytes a bit per place: bit n is bit n % 8 of byte n / 8.
template <std::size_t N>
void putBits(std::vector<PartyCell>& out, std::uint32_t at, const std::bitset<N>& bits) {
    for (std::size_t byte = 0; byte < (N + 7) / 8; ++byte) {
        std::uint8_t value = 0;
        for (std::size_t bit = 0; bit < 8 && byte * 8 + bit < N; ++bit) {
            value = static_cast<std::uint8_t>(value | (bits.test(byte * 8 + bit) ? 1u << bit : 0u));
        }
        out.push_back({.at = at + static_cast<std::uint32_t>(byte), .value = value});
    }
}
}  // namespace

namespace detail {

std::vector<PartyCell> partyImage(const PartyCells& cells) {
    const game::Party& party = cells.party;
    std::vector<PartyCell> out;
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        const std::array<std::uint8_t, kRecordSize> bytes =
            recordBytes(party.characters[record], cells.hpBoostBits[record], cells.mpBoostBits[record]);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            out.push_back({.at = recordAt(record) + static_cast<std::uint32_t>(i), .value = bytes[i]});
        }
    }
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        const game::PartyMembership& m = party.membership[record];
        out.push_back({.at = kRecordPartyBytes + static_cast<std::uint32_t>(record),
                       .value = static_cast<std::uint8_t>((m.party & kPartyNumberMask) |
                                                          (m.slot & kPartySlotMask) << kPartySlotPlace |
                                                          (m.backRow ? kPartyBackRowBit : 0) |
                                                          (m.enabled ? kPartyEnabledBit : 0) |
                                                          (m.visible ? kPartyVisibleBit : 0))});
    }
    for (std::uint32_t i = 0; i < kGilSize; ++i) {
        out.push_back({.at = kGil + i, .value = static_cast<std::uint8_t>(party.gil >> (8 * i) & 0xFF)});
    }
    for (std::size_t actor = 0; actor < game::kSpellLearners; ++actor) {
        for (std::size_t spell = 0; spell < game::kLearnableSpells; ++spell) {
            out.push_back({.at = spellListAt(static_cast<CharacterPropId>(actor)) + static_cast<std::uint32_t>(spell),
                           .value = party.spells[actor].progress[spell]});
        }
    }
    out.push_back({.at = kMorphSupply, .value = party.morphSupply});
    out.push_back({.at = kSwdtechsKnown, .value = party.swdtechsKnown.bits});
    out.push_back({.at = kBlitzesKnown, .value = party.blitzesKnown.bits});
    putBits(out, kLoresKnown, party.loresKnown);
    putBits(out, kRagesKnown, party.ragesKnown);
    out.push_back({.at = kDancesKnown, .value = static_cast<std::uint8_t>(party.dancesKnown.to_ulong())});
    return out;
}

std::string partyCellName(std::uint32_t at) {
    const auto hex = [](std::uint32_t value) {
        char text[8];
        std::snprintf(text, sizeof text, "%04X", value & 0xFFFF);
        return std::string{text};
    };
    if (at >= kCharacterRecords && at < recordAt(game::kCharacterRecords)) {
        const std::uint32_t offset = at - kCharacterRecords;
        return "record " + std::to_string(offset / kRecordSize) + " byte " + std::to_string(offset % kRecordSize);
    }
    if (at >= kSpellsKnown && at < kSpellsKnown + kSpellsKnownSize) {
        const std::uint32_t offset = at - kSpellsKnown;
        return "actor " + std::to_string(offset / game::kLearnableSpells) + " spell " +
               std::to_string(offset % game::kLearnableSpells);
    }
    return "$7E:" + hex(at);
}

std::uint8_t winFlagsByte(const WinFlags& flags) {
    return static_cast<std::uint8_t>((flags.curseDispelled ? kCurseDispelledBit : 0) |
                                     (flags.magicPointsEarned ? kMagicPointsEarnedBit : 0) |
                                     (flags.catHoodCounted ? kCatHoodCountedBit : 0) |
                                     (flags.techniqueMessageShown ? kTechniqueToldBit : 0) |
                                     (flags.blitzMessageShown ? kBlitzToldBit : 0));
}
}  // namespace detail

namespace {

// The message variables a message's text reads: none, a byte of one, or the whole of one.
enum class VariableRead : std::uint8_t { None, LowByte, Whole };
struct MessageVariables {
    std::uint8_t message;
    VariableRead first;
    VariableRead second;
};
// The win's messages, each with what its text reads: an item, an attack or a party slot is the low
// byte of its variable; a number the whole of it.
constexpr std::array<MessageVariables, 14> kMessageVariableReads{{
    {battle::kMessageBanonFell, VariableRead::None, VariableRead::None},
    {battle::kMessageGotItem, VariableRead::LowByte, VariableRead::None},
    {battle::kMessageGotItems, VariableRead::LowByte, VariableRead::Whole},
    {battle::kMessageGotGp, VariableRead::None, VariableRead::Whole},
    {battle::kMessageGotExperience, VariableRead::Whole, VariableRead::None},
    {battle::kMessageAnnihilated, VariableRead::None, VariableRead::None},
    {battle::kMessageCurseDispelled, VariableRead::None, VariableRead::None},
    {battle::kMessageLearnedLore, VariableRead::LowByte, VariableRead::None},
    {battle::kMessageGainedALevel, VariableRead::None, VariableRead::LowByte},
    {battle::kMessageLearnedSpell, VariableRead::LowByte, VariableRead::LowByte},
    {battle::kMessageDevisedABlitz, VariableRead::None, VariableRead::None},
    {battle::kMessageGotMagicPoints, VariableRead::Whole, VariableRead::None},
    {battle::kMessageMasteredADance, VariableRead::None, VariableRead::None},
    {battle::kMessageMasteredATechnique, VariableRead::None, VariableRead::None},
}};

std::optional<std::uint32_t> variableRead(const GraphicsCall& call, std::size_t variable, VariableRead read) {
    const std::size_t at = variable * kMessageVariableSize;
    switch (read) {
        case VariableRead::None:
            return std::nullopt;
        case VariableRead::LowByte:
            return call.messageVariables[at];
        case VariableRead::Whole:
            return static_cast<std::uint32_t>(call.messageVariables[at] | call.messageVariables[at + 1] << 8 |
                                              call.messageVariables[at + 2] << 16);
    }
    return std::nullopt;
}

RewardResult startingResult(const RewardScenario& scenario) {
    return RewardResult{.cells = scenario.cells,
                        .switches = scenario.switches,
                        .flags = scenario.flags,
                        .levelMessageToTell = scenario.levelMessageToTell};
}

// A battle holding `result`'s party, for the win's own steps to work on.
battle::BattleState battleHolding(const RewardResult& result) {
    battle::BattleState state;
    state.party = result.cells.party;
    state.version = GameVersion::US_1_1;
    return state;
}

// The win's two once-only messages as `result` keeps them.
battle::AbilityMessagesShown abilityMessagesOf(const RewardResult& result) {
    return {.swdtech = result.flags.techniqueMessageShown, .blitz = result.flags.blitzMessageShown};
}

// `state`'s party, the once-only messages and every message the steps told, put back into `result`.
void takeBack(RewardResult& result, const battle::BattleState& state, const battle::AbilityMessagesShown& shown) {
    result.cells.party = state.party;
    result.flags.techniqueMessageShown = shown.swdtech;
    result.flags.blitzMessageShown = shown.blitz;
    for (const battle::BattleRequest& request : state.requests) {
        const auto* script = std::get_if<battle::RunBattleScript>(&request);
        if (script == nullptr) {
            continue;
        }
        for (const battle::BattleScriptCommand& command : script->commands) {
            if (const auto* message = std::get_if<battle::BattleMessage>(&command)) {
                result.messages.push_back(
                    {.message = message->message, .first = message->firstNumber, .second = message->secondNumber});
            }
        }
    }
}
}  // namespace

std::optional<MessageShown> messageShown(const GraphicsCall& call) {
    if (call.command != kPlayBattleScript || call.script[0] != kShowMessageScriptCommand) {
        return std::nullopt;
    }
    const std::uint8_t message = call.script[1];
    for (const MessageVariables& reads : kMessageVariableReads) {
        if (reads.message == message) {
            return MessageShown{.message = message,
                                .first = variableRead(call, 0, reads.first),
                                .second = variableRead(call, 1, reads.second)};
        }
    }
    throw std::logic_error("no table entry says what message " + std::to_string(message) + "'s text reads");
}

RewardResult portAddExperience(const RewardScenario& scenario) {
    RewardResult result = startingResult(scenario);
    battle::addExperience(result.cells.party.characters[scenario.recordIndex], scenario.share);
    return result;
}

RewardResult portLearnFromEquipment(const RewardScenario& scenario) {
    RewardResult result = startingResult(scenario);
    game::Party& party = result.cells.party;
    game::CharacterRecord& record = party.characters[scenario.recordIndex];
    const battle::EquipmentLearning learning = battle::learnFromEquipment(
        record, party.spells[static_cast<std::size_t>(record.actor)], result.switches, scenario.magicPoints);
    result.flags.curseDispelled |= learning.curseDispelled;
    result.flags.magicPointsEarned |= learning.progressed && result.flags.partyHasMagicite;
    return result;
}

RewardResult portLevelUps(const RewardScenario& scenario) {
    RewardResult result = startingResult(scenario);
    battle::BattleState state = battleHolding(result);
    battle::AbilityMessagesShown shown = abilityMessagesOf(result);
    result.levelMessageToTell = battle::reachEarnedLevels(state, state.party.characters[scenario.recordIndex],
                                                          scenario.partySlot, scenario.levelMessageToTell, shown);
    takeBack(result, state, shown);
    return result;
}

RewardResult portLearnAbilities(const RewardScenario& scenario) {
    RewardResult result = startingResult(scenario);
    battle::BattleState state = battleHolding(result);
    battle::AbilityMessagesShown shown = abilityMessagesOf(result);
    battle::learnLevelAbilities(state, scenario.actor, scenario.level, shown);
    takeBack(result, state, shown);
    return result;
}

RewardResult portLearnRages(const RewardScenario& scenario) {
    RewardResult result = startingResult(scenario);
    battle::BattleState state;
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        state.at(battle::BattlerSlot::monster(position)).monster = scenario.monsters[static_cast<std::size_t>(position)];
    }
    battle::learnRages(result.cells.party, state);
    return result;
}

std::string differences(const RewardResult& port, const RewardResult& guest) {
    std::string out;
    const std::vector<PartyCell> ours = partyImage(port.cells);
    const std::vector<PartyCell> theirs = partyImage(guest.cells);
    for (std::size_t i = 0; i < ours.size(); ++i) {
        addLine(out, partyCellName(ours[i].at).c_str(), ours[i].value, theirs[i].value);
    }
    for (std::size_t i = 0; i < port.switches.variables.size(); ++i) {
        const std::string field = "battle variable " + std::to_string(i);
        addLine(out, field.c_str(), port.switches.variables[i], guest.switches.variables[i]);
    }
    addLine(out, "curse dispelled", port.flags.curseDispelled, guest.flags.curseDispelled);
    addLine(out, "magic points earned", port.flags.magicPointsEarned, guest.flags.magicPointsEarned);
    addLine(out, "Cat Hood counted", port.flags.catHoodCounted, guest.flags.catHoodCounted);
    addLine(out, "technique told", port.flags.techniqueMessageShown, guest.flags.techniqueMessageShown);
    addLine(out, "blitz told", port.flags.blitzMessageShown, guest.flags.blitzMessageShown);
    addLine(out, "party has magicite", port.flags.partyHasMagicite, guest.flags.partyHasMagicite);
    addLine(out, "gained a level still to tell", port.levelMessageToTell, guest.levelMessageToTell);
    const std::size_t messages = std::max(port.messages.size(), guest.messages.size());
    for (std::size_t i = 0; i < messages; ++i) {
        const std::string ours = i < port.messages.size() ? messageText(port.messages[i]) : "none";
        const std::string theirs = i < guest.messages.size() ? messageText(guest.messages[i]) : "none";
        if (ours != theirs) {
            out += "message shown " + std::to_string(i) + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    }
    for (const GraphicsCall& call : guest.otherGraphicsCalls) {
        out += "the cartridge called graphics command " + std::to_string(call.command) + ", which the port does not\n";
    }
    return out;
}
namespace {

constexpr std::uint32_t kLargestShare = 0xFFFFFF;

// A spell's learned percentage: below 100, just learned or learned.
std::uint8_t drawProgress(std::mt19937& rng) {
    switch (rng() % 6) {
        case 0:
            return game::SpellProgress::kSpellJustLearned;
        case 1:
            return game::SpellProgress::kSpellLearned;
        case 2:
            return static_cast<std::uint8_t>(90 + upTo(rng, 9));
        default:
            return static_cast<std::uint8_t>(upTo(rng, 99));
    }
}
}  // namespace

namespace detail {

ItemId drawItem(std::mt19937& rng) {
    return static_cast<ItemId>(rng() % kItemsBelowEmpty);
}

std::uint32_t experienceForNextLevel(std::uint8_t level) {
    std::uint32_t steps = 0;
    for (int at = kFirstProgressionLevel; at <= level + 1; ++at) {
        steps += levelUpExp(static_cast<std::uint8_t>(at));
    }
    return steps * 8;
}

PartyCells drawPartyCells(std::mt19937& rng) {
    PartyCells cells;
    game::Party& party = cells.party;
    for (std::size_t r = 0; r < game::kCharacterRecords; ++r) {
        game::CharacterRecord& record = party.characters[r];
        record.actor = static_cast<CharacterPropId>(r);
        record.graphics = static_cast<CharacterGfxId>(rng() % kActors);
        for (std::uint8_t& letter : record.name) {
            letter = byte(rng);
        }
        record.level = static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1));
        record.maxHp = static_cast<std::uint16_t>(upTo(rng, kPartyMaxHp));
        record.hp = static_cast<std::uint16_t>(upTo(rng, record.maxHp));
        record.maxMp = static_cast<std::uint16_t>(upTo(rng, kPartyMaxMp));
        record.mp = static_cast<std::uint16_t>(upTo(rng, record.maxMp));
        record.experience = static_cast<std::uint32_t>(rng() % (kMaxExperience + 1));
        record.status.bytes[0] = sparse(rng);
        record.status.bytes[3] = sparse(rng);
        for (BattleCommandId& command : record.commands) {
            command = static_cast<BattleCommandId>(byte(rng));
        }
        record.vigor = static_cast<std::uint8_t>(upTo(rng, 128));
        record.speed = static_cast<std::uint8_t>(upTo(rng, 128));
        record.stamina = static_cast<std::uint8_t>(upTo(rng, 128));
        record.magicPower = static_cast<std::uint8_t>(upTo(rng, 128));
        if (!oneIn(rng, 4)) {
            record.esper = static_cast<EsperId>(static_cast<std::uint8_t>(EsperId::RAMUH) + rng() % kEspers);
        }
        ItemId* const equipment[] = {&record.weapon, &record.shield, &record.helmet,
                                     &record.armor,  &record.relic1, &record.relic2};
        for (ItemId* slot : equipment) {
            *slot = oneIn(rng, 3) ? ItemId::EMPTY : drawItem(rng);
        }
        cells.hpBoostBits[r] = static_cast<std::uint8_t>(upTo(rng, 3));
        cells.mpBoostBits[r] = static_cast<std::uint8_t>(upTo(rng, 3));
        party.membership[r] = game::PartyMembership{.party = static_cast<std::uint8_t>(upTo(rng, 3)),
                                                    .slot = static_cast<std::uint8_t>(upTo(rng, 3)),
                                                    .backRow = oneIn(rng, 2),
                                                    .enabled = oneIn(rng, 2),
                                                    .visible = oneIn(rng, 2)};
    }
    for (game::SpellProgress& spells : party.spells) {
        for (std::uint8_t& learned : spells.progress) {
            learned = drawProgress(rng);
        }
    }
    party.swdtechsKnown = AbilityLearnedSet{byte(rng)};
    party.blitzesKnown = AbilityLearnedSet{byte(rng)};
    for (std::size_t lore = 0; lore < game::kLores; ++lore) {
        party.loresKnown.set(lore, oneIn(rng, 2));
    }
    party.dancesKnown = std::bitset<8>(byte(rng));
    for (std::size_t rage = 0; rage < party.ragesKnown.size(); ++rage) {
        party.ragesKnown.set(rage, oneIn(rng, 2));
    }
    party.gil = static_cast<std::uint32_t>(rng() % (kMaxGil + 1));
    party.morphSupply = byte(rng);
    return cells;
}
}  // namespace detail

namespace {

WinFlags drawWinFlags(std::mt19937& rng) {
    return WinFlags{.curseDispelled = oneIn(rng, 2),
                    .magicPointsEarned = oneIn(rng, 2),
                    .catHoodCounted = oneIn(rng, 2),
                    .techniqueMessageShown = oneIn(rng, 2),
                    .blitzMessageShown = oneIn(rng, 2),
                    .partyHasMagicite = oneIn(rng, 2)};
}

RewardScenario drawRewardScenario(std::mt19937& rng) {
    RewardScenario s;
    s.cells = drawPartyCells(rng);
    for (std::uint8_t& variable : s.switches.variables) {
        variable = byte(rng);
    }
    s.recordIndex = rng() % game::kCharacterRecords;
    s.partySlot = static_cast<std::uint8_t>(upTo(rng, battle::kPartySlots - 1));
    s.magicPoints = oneIn(rng, 2) ? static_cast<std::uint8_t>(upTo(rng, 10)) : byte(rng);
    s.flags = drawWinFlags(rng);
    s.levelMessageToTell = oneIn(rng, 2);
    return s;
}
}  // namespace

RewardScenario drawExperienceScenario(std::mt19937& rng) {
    RewardScenario s = drawRewardScenario(rng);
    game::CharacterRecord& record = s.cells.party.characters[s.recordIndex];
    switch (rng() % 4) {
        case 0:
            record.experience = kMaxExperience - static_cast<std::uint32_t>(upTo(rng, 100'000));
            break;
        case 1:
            // Past the cap, and up to the top of the three bytes.
            record.experience = kMaxExperience + static_cast<std::uint32_t>(rng() % (kLargestShare - kMaxExperience + 1));
            break;
        default:
            break;
    }
    s.share = oneIn(rng, 3) ? static_cast<std::uint32_t>(rng() & kLargestShare)
                            : static_cast<std::uint32_t>(upTo(rng, 30'000));
    return s;
}

RewardScenario drawEquipmentScenario(std::mt19937& rng) {
    RewardScenario s = drawRewardScenario(rng);
    game::CharacterRecord& record = s.cells.party.characters[s.recordIndex];
    record.actor = static_cast<CharacterPropId>(rng() % game::kSpellLearners);
    ItemId* const equipment[] = {&record.weapon, &record.shield, &record.helmet,
                                 &record.armor,  &record.relic1, &record.relic2};
    for (ItemId* slot : equipment) {
        switch (rng() % 4) {
            case 0:
                *slot = ItemId::EMPTY;
                break;
            case 1:
                *slot = ItemId::CURSED_SHLD;
                break;
            default:
                *slot = drawItem(rng);
                break;
        }
    }
    s.switches.setCursedShieldBattles(oneIn(rng, 2) ? static_cast<std::uint8_t>(254 + upTo(rng, 1)) : byte(rng));
    return s;
}

RewardScenario drawLevelScenario(std::mt19937& rng) {
    RewardScenario s = drawRewardScenario(rng);
    game::CharacterRecord& record = s.cells.party.characters[s.recordIndex];
    record.actor = static_cast<CharacterPropId>(rng() % kActors);
    record.level = static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1));
    if (record.level < kPartyMaxLevel) {
        if (oneIn(rng, 4)) {
            // Just short of the next level.
            const std::uint32_t need = experienceForNextLevel(record.level);
            record.experience = need - 1 - std::min<std::uint32_t>(need - 1, static_cast<std::uint32_t>(upTo(rng, 100)));
        } else {
            // At the next level or up to five past it.
            const int reached = std::min<int>(record.level + upTo(rng, 5), kPartyMaxLevel - 1);
            record.experience = experienceForNextLevel(static_cast<std::uint8_t>(reached)) +
                                static_cast<std::uint32_t>(upTo(rng, 1000));
        }
    }
    return s;
}

RewardScenario drawRageScenario(std::mt19937& rng) {
    RewardScenario s = drawRewardScenario(rng);
    for (std::optional<MonsterId>& monster : s.monsters) {
        switch (rng() % 4) {
            case 0:
                monster.reset();
                break;
            case 1:
                monster = static_cast<MonsterId>(256 + rng() % (kMonsterIds - 256));
                break;
            default:
                monster = static_cast<MonsterId>(rng() % 256);
                break;
        }
    }
    return s;
}

RewardScenario abilityCase(CharacterPropId actor, std::uint8_t level, int mix, std::mt19937& rng) {
    RewardScenario s = drawRewardScenario(rng);
    s.actor = actor;
    s.level = level;
    if (static_cast<std::size_t>(actor) < game::kSpellLearners) {
        for (std::uint8_t& learned : s.cells.party.spells[static_cast<std::size_t>(actor)].progress) {
            learned = mix == 0   ? static_cast<std::uint8_t>(upTo(rng, 99))
                      : mix == 1 ? game::SpellProgress::kSpellJustLearned
                                 : game::SpellProgress::kSpellLearned;
        }
    }
    return s;
}

std::string describe(const RewardScenario& s) {
    const game::CharacterRecord& r = s.cells.party.characters[s.recordIndex];
    std::string text = "record " + std::to_string(s.recordIndex) + " actor " + std::to_string(static_cast<int>(r.actor)) +
                       " level " + std::to_string(r.level) + " experience " + std::to_string(r.experience) +
                       " share " + std::to_string(s.share) + " magic points " + std::to_string(s.magicPoints) +
                       " flags " + std::to_string(winFlagsByte(s.flags)) + " magicite " +
                       std::to_string(s.flags.partyHasMagicite) + " level message " +
                       std::to_string(s.levelMessageToTell) + " cursed-shield battles " +
                       std::to_string(s.switches.cursedShieldBattles()) + " equipment";
    for (const ItemId item : r.equipment()) {
        text += " " + std::to_string(static_cast<int>(item));
    }
    text += " ability check " + std::to_string(static_cast<int>(s.actor)) + "@" + std::to_string(s.level) + " monsters";
    for (const std::optional<MonsterId>& monster : s.monsters) {
        text += " " + (monster ? std::to_string(static_cast<int>(*monster)) : std::string{"-"});
    }
    return text;
}

}  // namespace ostinato::trace
