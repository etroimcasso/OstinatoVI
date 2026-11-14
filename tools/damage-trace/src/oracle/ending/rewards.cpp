// The cartridge's reward steps run over a party, the graphics commands answered as they run, and a
// traced reward call replayed.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "battle/rewards.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

#include "cells/damage/frame.h"
#include "cells/damage/per_target.h"
#include "cells/ending/party.h"
#include "cells/ending/records.h"
#include "cells/status/effects.h"
#include "oracle/ending/records_shared.h"
#include "oracle/routines.h"
#include "scenarios/common/bytes.h"
#include "scenarios/ending/records_shared.h"
#include "scenarios/ending/rewards_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

// The record its 37 bytes hold, with the two boosts out of the top of its maximum HP and MP words.
struct RecordRead {
    game::CharacterRecord record;
    std::uint8_t hpBoostBits = 0;
    std::uint8_t mpBoostBits = 0;
};

RecordRead recordOf(std::span<const std::uint8_t> bytes) {
    const auto word = [&](std::size_t at) { return static_cast<std::uint16_t>(bytes[at] | bytes[at + 1] << 8); };
    RecordRead read;
    game::CharacterRecord& record = read.record;
    record.actor = static_cast<CharacterPropId>(bytes[0]);
    record.graphics = static_cast<CharacterGfxId>(bytes[kRecordGraphics]);
    std::copy(bytes.begin() + kRecordName, bytes.begin() + kRecordName + record.name.size(), record.name.begin());
    record.level = bytes[kRecordLevel];
    record.hp = word(kRecordHp);
    record.maxHp = static_cast<std::uint16_t>(word(kRecordMaxHp) & kMaximumBits);
    read.hpBoostBits = static_cast<std::uint8_t>(word(kRecordMaxHp) >> kBoostShift);
    record.mp = word(kRecordMp);
    record.maxMp = static_cast<std::uint16_t>(word(kRecordMaxMp) & kMaximumBits);
    read.mpBoostBits = static_cast<std::uint8_t>(word(kRecordMaxMp) >> kBoostShift);
    record.experience = static_cast<std::uint32_t>(bytes[kRecordExperience] | bytes[kRecordExperience + 1] << 8 |
                                                   bytes[kRecordExperience + 2] << 16);
    record.status.bytes[0] = bytes[kRecordFirstStatus];
    record.status.bytes[3] = bytes[kRecordFourthStatus];
    for (std::size_t i = 0; i < record.commands.size(); ++i) {
        record.commands[i] = static_cast<BattleCommandId>(bytes[kRecordCommands + i]);
    }
    record.vigor = bytes[kRecordVigor];
    record.speed = bytes[kRecordSpeed];
    record.stamina = bytes[kRecordStamina];
    record.magicPower = bytes[kRecordMagicPower];
    record.esper = esperOfByte(bytes[kRecordEsper]);
    ItemId* const equipment[] = {&record.weapon, &record.shield, &record.helmet,
                                 &record.armor,  &record.relic1, &record.relic2};
    for (std::size_t i = 0; i < std::size(equipment); ++i) {
        *equipment[i] = static_cast<ItemId>(bytes[kRecordEquipment + i]);
    }
    return read;
}

// The win's other direct-page cells: the message "gained a level" is told with, 0 once it has been.
constexpr std::uint32_t kLevelMessage = kWorkRam + 0x00F2;
// The four message variables, three bytes each.
constexpr std::uint32_t kMessageVariables = kWorkRam + 0x2F35;
// The value "gained a level" is armed with, and a monster slot's index with no monster in it.
constexpr std::uint8_t kLevelMessageArmed = battle::kMessageGainedALevel;
}  // namespace

void DamageOracle::writeParty(const PartyCells& cells) {
    for (const PartyCell& cell : partyImage(cells)) {
        writeByte(cell.at, cell.value);
    }
}

PartyCells DamageOracle::readParty() {
    PartyCells cells;
    game::Party& party = cells.party;
    const std::vector<std::uint8_t> records = machine_.read(
        retropp::MemoryRegion{.at = kCharacterRecords, .size = kRecordSize * static_cast<std::uint32_t>(game::kCharacterRecords)});
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        const RecordRead read = recordOf(std::span<const std::uint8_t>(records).subspan(record * kRecordSize, kRecordSize));
        party.characters[record] = read.record;
        cells.hpBoostBits[record] = read.hpBoostBits;
        cells.mpBoostBits[record] = read.mpBoostBits;
        const std::uint8_t value = readByte(kRecordPartyBytes + static_cast<std::uint32_t>(record));
        party.membership[record] =
            game::PartyMembership{.party = static_cast<std::uint8_t>(value & kPartyNumberMask),
                                  .slot = static_cast<std::uint8_t>(value >> kPartySlotPlace & kPartySlotMask),
                                  .backRow = (value & kPartyBackRowBit) != 0,
                                  .enabled = (value & kPartyEnabledBit) != 0,
                                  .visible = (value & kPartyVisibleBit) != 0};
    }
    party.gil = 0;
    for (std::uint32_t i = 0; i < kGilSize; ++i) {
        party.gil |= static_cast<std::uint32_t>(readByte(kGil + i)) << (8 * i);
    }
    const std::vector<std::uint8_t> spells =
        machine_.read(retropp::MemoryRegion{.at = kSpellsKnown, .size = kSpellsKnownSize});
    for (std::size_t actor = 0; actor < game::kSpellLearners; ++actor) {
        std::copy_n(spells.begin() + static_cast<std::ptrdiff_t>(actor * game::kLearnableSpells), game::kLearnableSpells,
                    party.spells[actor].progress.begin());
    }
    party.morphSupply = readByte(kMorphSupply);
    party.swdtechsKnown = AbilityLearnedSet{readByte(kSwdtechsKnown)};
    party.blitzesKnown = AbilityLearnedSet{readByte(kBlitzesKnown)};
    const auto readBits = [&](auto& bits, std::uint32_t at) {
        for (std::size_t n = 0; n < bits.size(); ++n) {
            bits.set(n, (readByte(at + static_cast<std::uint32_t>(n / 8)) >> (n % 8) & 1) != 0);
        }
    };
    readBits(party.loresKnown, kLoresKnown);
    readBits(party.ragesKnown, kRagesKnown);
    party.dancesKnown = std::bitset<8>(readByte(kDancesKnown));
    return cells;
}

void DamageOracle::writeRewardScenario(const RewardScenario& scenario) {
    if (scenario.recordIndex >= game::kCharacterRecords) {
        throw std::logic_error("a reward scenario names one of the sixteen records");
    }
    writeParty(scenario.cells);
    writeBytes(retropp::MemoryRegion{.at = kSwitchVariables, .size = game::kBattleVariables},
               scenario.switches.variables);
    writeByte(kWinFlags, winFlagsByte(scenario.flags));
    writeByte(kPartyHasMagicite, scenario.flags.partyHasMagicite ? kMagiciteBit : 0);
    writeByte(kLevelMessage, scenario.levelMessageToTell ? kLevelMessageArmed : 0);
    writeByte(kMagicPoints, scenario.magicPoints);
    // The message variables as the win leaves them for these steps: the share in the first, the
    // member's party slot in the second, the rest 0.
    std::array<std::uint8_t, 4 * kMessageVariableSize> variables{};
    for (std::size_t i = 0; i < kMessageVariableSize; ++i) {
        variables[i] = static_cast<std::uint8_t>(scenario.share >> (8 * i) & 0xFF);
    }
    variables[kMessageVariableSize] = scenario.partySlot;
    writeBytes(retropp::MemoryRegion{.at = kMessageVariables, .size = static_cast<std::uint32_t>(variables.size())},
               variables);
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        const std::optional<MonsterId>& monster = scenario.monsters[static_cast<std::size_t>(position)];
        writeWord(kMonsterIndex + slotIndex(battle::BattlerSlot::monster(position)),
                  monster ? static_cast<std::uint16_t>(*monster) : kEmptyMonsterSlot);
    }
}

RewardResult DamageOracle::readRewardResult() {
    RewardResult result;
    result.cells = readParty();
    const std::vector<std::uint8_t> switches =
        machine_.read(retropp::MemoryRegion{.at = kSwitchVariables, .size = game::kBattleVariables});
    std::copy(switches.begin(), switches.end(), result.switches.variables.begin());
    const std::uint8_t flags = readByte(kWinFlags);
    result.flags = WinFlags{.curseDispelled = (flags & kCurseDispelledBit) != 0,
                            .magicPointsEarned = (flags & kMagicPointsEarnedBit) != 0,
                            .catHoodCounted = (flags & kCatHoodCountedBit) != 0,
                            .techniqueMessageShown = (flags & kTechniqueToldBit) != 0,
                            .blitzMessageShown = (flags & kBlitzToldBit) != 0,
                            .partyHasMagicite = (readByte(kPartyHasMagicite) & kMagiciteBit) != 0};
    result.levelMessageToTell = readByte(kLevelMessage) != 0;
    for (const GraphicsCall& call : graphicsCalls_) {
        if (const std::optional<MessageShown> shown = messageShown(call)) {
            result.messages.push_back(*shown);
        } else {
            result.otherGraphicsCalls.push_back(call);
        }
    }
    return result;
}

RewardResult DamageOracle::callReward(const std::function<void()>& routine) {
    graphicsCalls_.clear();
    unansweredGraphics_.reset();
    machine_.escapes()[kGraphicsKey].armed(true);
    call(routine);
    machine_.escapes()[kGraphicsKey].armed(false);
    if (unansweredGraphics_) {
        throw std::runtime_error("the routine called graphics command " + std::to_string(*unansweredGraphics_) +
                                 ", which the oracle has no answer for");
    }
    return readRewardResult();
}

const std::vector<GraphicsCall>& DamageOracle::graphicsCommand(std::uint8_t command) {
    graphicsCalls_.clear();
    unansweredGraphics_.reset();
    machine_.escapes()[kGraphicsKey].armed(true);
    call([&] { execBtlGfx_(command, kWideIndexStatus, kBattleDataBank, kBattleDirectPage); });
    machine_.escapes()[kGraphicsKey].armed(false);
    return graphicsCalls_;
}
namespace {

// The win's steps among kTracedRoutines, closed by the oracle when it calls one itself.
const TracedRoutine& kTracedAddExp = kTracedRoutines[15];
const TracedRoutine& kTracedLearnItemMagic = kTracedRoutines[16];
const TracedRoutine& kTracedLearnAbilities = kTracedRoutines[17];
const TracedRoutine& kTracedLearnRage = kTracedRoutines[18];
static_assert(kTracedRoutines[15].entry == kAddExp && kTracedRoutines[16].entry == kLearnItemMagic &&
              kTracedRoutines[17].entry == kLearnAbilities && kTracedRoutines[18].entry == kLearnRage);
}  // namespace

RewardResult DamageOracle::addExperience(const RewardScenario& scenario) {
    writeRewardScenario(scenario);
    const auto record = static_cast<std::uint16_t>(recordAt(scenario.recordIndex) - kCharacterRecords);
    return callReward([&] {
        addExp_(record, kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
        trace_->returnToFrame(kTracedAddExp);
    });
}

RewardResult DamageOracle::learnFromEquipment(const RewardScenario& scenario) {
    const CharacterPropId actor = scenario.cells.party.characters[scenario.recordIndex].actor;
    if (static_cast<std::size_t>(actor) >= game::kSpellLearners) {
        throw std::logic_error("the spells equipment teaches are learned by the first twelve characters only");
    }
    writeRewardScenario(scenario);
    writeWord(kSpellListPlace, static_cast<std::uint16_t>(spellListAt(actor) & 0xFFFF));
    const auto record = static_cast<std::uint16_t>(recordAt(scenario.recordIndex) - kCharacterRecords);
    return callReward([&] {
        learnItemMagic_(record, kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
        trace_->returnToFrame(kTracedLearnItemMagic);
    });
}

RewardResult DamageOracle::levelUps(const RewardScenario& scenario) {
    const game::CharacterRecord& member = scenario.cells.party.characters[scenario.recordIndex];
    if (member.level == 0) {
        throw std::logic_error("a character's level is 1 or more");
    }
    writeRewardScenario(scenario);
    // The place the spells of the record's actor would be, as the win works it out for every actor.
    writeWord(kSpellListPlace, static_cast<std::uint16_t>(spellListAt(member.actor) & 0xFFFF));
    const auto record = static_cast<std::uint16_t>(recordAt(scenario.recordIndex) - kCharacterRecords);
    return callReward( [&] { checkLevelUp_(record, kWideIndexStatus, kBattleDataBank, kBattleDirectPage); });
}

RewardResult DamageOracle::learnAbilities(const RewardScenario& scenario) {
    writeRewardScenario(scenario);
    writeWord(kSpellListPlace, static_cast<std::uint16_t>(spellListAt(scenario.actor) & 0xFFFF));
    const auto actorAndLevel =
        static_cast<std::uint16_t>(static_cast<std::uint8_t>(scenario.actor) | scenario.level << 8);
    return callReward([&] {
        trace_->readForAbilities(static_cast<std::uint8_t>(scenario.actor), scenario.level);
        learnAbilities_(actorAndLevel, kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
        trace_->returnToFrame(kTracedLearnAbilities);
    });
}

RewardResult DamageOracle::learnRages(const RewardScenario& scenario) {
    writeRewardScenario(scenario);
    return callReward([&] {
        learnRage_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage);
        trace_->returnToFrame(kTracedLearnRage);
    });
}

RewardScenario DamageOracle::readRewardScenario() {
    RewardScenario s;
    s.cells = readParty();
    const std::vector<std::uint8_t> switches =
        machine_.read(retropp::MemoryRegion{.at = kSwitchVariables, .size = game::kBattleVariables});
    std::copy(switches.begin(), switches.end(), s.switches.variables.begin());
    const std::uint8_t flags = readByte(kWinFlags);
    s.flags = WinFlags{.curseDispelled = (flags & kCurseDispelledBit) != 0,
                       .magicPointsEarned = (flags & kMagicPointsEarnedBit) != 0,
                       .catHoodCounted = (flags & kCatHoodCountedBit) != 0,
                       .techniqueMessageShown = (flags & kTechniqueToldBit) != 0,
                       .blitzMessageShown = (flags & kBlitzToldBit) != 0,
                       .partyHasMagicite = (readByte(kPartyHasMagicite) & kMagiciteBit) != 0};
    s.levelMessageToTell = readByte(kLevelMessage) != 0;
    s.magicPoints = readByte(kMagicPoints);
    s.share = 0;
    for (std::uint32_t i = 0; i < kMessageVariableSize; ++i) {
        s.share |= static_cast<std::uint32_t>(readByte(kMessageVariables + i)) << (8 * i);
    }
    s.partySlot = readByte(kMessageVariables + kMessageVariableSize);
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        const std::uint16_t monster = readWord(kMonsterIndex + slotIndex(battle::BattlerSlot::monster(position)));
        s.monsters[static_cast<std::size_t>(position)] =
            monster == kEmptyMonsterSlot ? std::nullopt : std::optional<MonsterId>(static_cast<MonsterId>(monster));
    }
    return s;
}
namespace {

// The one character record whose cells `call` read or wrote, or nothing when it touched none.
std::optional<std::size_t> recordTouched(const TracedCall& call) {
    std::optional<std::size_t> index;
    for (const auto* cells : {&call.inputs, &call.writes}) {
        for (const auto& [cell, value] : *cells) {
            if (cell < kCharacterRecords || cell >= recordAt(game::kCharacterRecords)) {
                continue;
            }
            const std::size_t in = (cell - kCharacterRecords) / kRecordSize;
            if (index && *index != in) {
                throw std::runtime_error(call.routine + " touched more than one character record");
            }
            index = in;
        }
    }
    return index;
}
}  // namespace

std::string DamageOracle::replayRewardCall(const TracedCall& call, const TracedRoutine& routine) {
    // The step as a plain scenario, then every cell the routine read as it read it.
    RewardScenario plain;
    const bool onARecord = routine.entry == kAddExp || routine.entry == kLearnItemMagic;
    if (onARecord) {
        const std::optional<std::size_t> record = recordTouched(call);
        if (!record) {
            throw std::runtime_error(call.routine + " touched no character record");
        }
        plain.recordIndex = *record;
    }
    writeRewardScenario(plain);
    for (const auto& [cell, value] : call.inputs) {
        writeByte(cell, value);
    }
    RewardScenario s = readRewardScenario();
    s.recordIndex = plain.recordIndex;

    RewardResult port;
    if (routine.entry == kAddExp) {
        port = portAddExperience(s);
    } else if (routine.entry == kLearnItemMagic) {
        // The spells learned are the list the win's place for them names, whoever the record says it
        // is; a call that never read the place taught nothing, and the list it names does not matter.
        game::CharacterRecord& record = s.cells.party.characters[s.recordIndex];
        const CharacterPropId actor = record.actor;
        if (call.inputs.contains(kSpellListPlace)) {
            const std::uint32_t place = kWorkRam | readWord(kSpellListPlace);
            if (place < kSpellsKnown || (place - kSpellsKnown) % game::kLearnableSpells != 0 ||
                (place - kSpellsKnown) / game::kLearnableSpells >= game::kSpellLearners) {
                throw std::runtime_error("LearnItemMagic's spell list place names no character's spells");
            }
            record.actor = static_cast<CharacterPropId>((place - kSpellsKnown) / game::kLearnableSpells);
        } else if (static_cast<std::size_t>(record.actor) >= game::kSpellLearners) {
            record.actor = CharacterPropId::TERRA;
        }
        port = portLearnFromEquipment(s);
        port.cells.party.characters[s.recordIndex].actor = actor;
    } else if (routine.entry == kLearnAbilities) {
        if (!call.abilitiesActor || !call.abilitiesLevel) {
            throw std::runtime_error("LearnAbilities was entered with no actor and level read before it");
        }
        s.actor = static_cast<CharacterPropId>(*call.abilitiesActor);
        s.level = *call.abilitiesLevel;
        port = portLearnAbilities(s);
    } else {
        port = portLearnRages(s);
    }

    // The cartridge's side: the cells as the call left them, and the messages it showed.
    for (const auto& [cell, value] : call.writes) {
        writeByte(cell, value);
    }
    graphicsCalls_.clear();
    RewardResult guest = readRewardResult();
    for (const LoggedMessage& message : call.messages) {
        guest.messages.push_back(messageShownBy(message));
    }
    return differences(port, guest);
}

}  // namespace ostinato::trace
