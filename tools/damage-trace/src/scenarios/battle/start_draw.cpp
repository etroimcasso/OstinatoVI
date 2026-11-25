// The start sweep's draws, the records given to a setup's cast, and the reach table's rows.

#include "scenarios/battle/start_draw.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/rom_reader.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "data/battle_tables.h"
#include "data/char_ai.h"
#include "data/formations.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/char_ai_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/formation_ref.h"
#include "ostinato/item_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"

#include "cells/battle/state.h"
#include "cells/damage/per_target.h"
#include "scenarios/common/draws.h"
#include "scenarios/ending/rewards_shared.h"

namespace ostinato::trace {

using namespace detail;
using namespace cells;

namespace {

// The colosseum's two formations are the last two, and the formations below them are the field's.
constexpr int kFirstColosseumFormation = 574;
// A randomized formation becomes itself or one of the three after it.
constexpr int kRandomizedSpread = 3;
// The sixth monster slot: Gau comes back from the Veldt only when the formation leaves it empty.
constexpr std::size_t kGauReturnSlot = 5;
constexpr int kConditionalBattles = 8;
// The commands a record holds: Fight to Magitek, or none.
constexpr std::uint8_t kRecordCommands = 0x1E;
// The battle backgrounds, Tentacles the last.
constexpr int kBackgrounds = static_cast<int>(BattleBackgroundId::TENTACLES) + 1;
// The settings the config menu offers: six battle speeds and six spell orders.
constexpr int kSpeedSettings = 6;
constexpr int kSpellOrders = 6;
// The play clock's frames, 1 to 60.
constexpr int kPlayClockFramesMost = 60;
// The final battle's line: up to twelve records, at least four.
constexpr std::size_t kFinalLineMost = 12;
constexpr std::size_t kFinalLineLeast = 4;
constexpr int kMaxItemQuantity = 99;
constexpr int kSaveHoursMost = 99;
constexpr int kSaveMinutesMost = 59;
constexpr std::uint32_t kStepsMost = 9999999;
// The byte above the random numbers' position: not zero one start in this many, drawn from its own place.
constexpr unsigned kAboveRngPositionSetOneIn = 4;
constexpr std::uint32_t kAboveRngPositionPlace = 1;
constexpr int kByteMost = 255;

// The formation's possible battle types, as ChooseBattleType tests them (battle_main.asm:7556-7578).
constexpr std::uint8_t kFrontBit = 0x10;
constexpr std::uint8_t kBackBit = 0x20;
constexpr std::uint8_t kPincerBit = 0x40;
constexpr std::uint8_t kSideBit = 0x80;
constexpr std::uint8_t kAllButPincer = kFrontBit | kBackBit | kSideBit;
constexpr std::uint8_t kAllButBack = kFrontBit | kPincerBit | kSideBit;
constexpr std::uint8_t kAllButSide = kFrontBit | kBackBit | kPincerBit;
constexpr int kAlliesForSideAttack = 3;

// The formation byte and the scripted setup the start lays (LoadBattleProp :7973-7977, InitParty
// :7786-7792): the third byte's top bit turns a setup on, and the fourth names it.
constexpr std::uint32_t kSetupFlags = kWorkRam + 0x2F49;
constexpr std::uint32_t kSetupIndex = kWorkRam + 0x2F4A;
constexpr std::uint8_t kSetupOn = 0x80;
constexpr std::uint8_t kBattleTypeFront = 0;
constexpr std::uint8_t kBattleTypeBack = 1;
constexpr std::uint8_t kBattleTypePincer = 2;
constexpr std::uint8_t kBattleTypeSide = 3;

constexpr std::array<std::string_view, 22> kReachRows{{
    "front attack",
    "back attack",
    "pincer attack",
    "side attack",
    "preemptive strike",
    "colosseum",
    "final battle",
    "scripted setup",
    "Gau's return",
    "empty party slot",
    "morphed member",
    "randomized formation",
    "conditional battle",
    "fewer than three: side attack ruled out",
    "fewer than three: only a side attack",
    "Back Guard: pincer ruled out",
    "Back Guard: only a pincer",
    "Back Guard: back attack ruled out",
    "Back Guard: only a back attack",
    "Gale Hairpin: front attack",
    "Gale Hairpin: side attack",
    "menus shut: no preemptive strike",
}};

std::uint8_t at(const BattleCells& cells, std::uint32_t cell) { return cells[cell - kWorkRam]; }

// The formations a start asking for `asked` can fight: a conditional battle whose switch is on
// replaces it, last row first, and a randomized formation is itself or one of the three after it
// (LoadBattleProp :7943-7965).
std::vector<FormationId> formationsFought(FormationRef asked, const game::BattleSwitches& switches) {
    for (int row = kConditionalBattles - 1; row >= 0; --row) {
        if (!switches.isSet(game::kConditionalBattleSwitches + row)) {
            continue;
        }
        const ConditionalBattle& conditional = getConditionalBattle(static_cast<std::size_t>(row));
        if (conditional.trigger.raw == asked.raw) {
            asked = conditional.replacement;
        }
    }
    std::vector<FormationId> fought{asked.formationId()};
    if (asked.randomizePlus3()) {
        for (int more = 1; more <= kRandomizedSpread; ++more) {
            fought.push_back(static_cast<FormationId>(static_cast<int>(asked.formationId()) + more));
        }
    }
    return fought;
}

bool holdsActor(const game::Party& party, CharacterPropId actor) {
    return std::any_of(party.characters.begin(), party.characters.end(),
                       [&](const game::CharacterRecord& record) { return record.actor == actor; });
}

// The field's formations (every one below the colosseum's) for which `keep` holds.
std::vector<int> formationsWhere(bool (*keep)(FormationId)) {
    std::vector<int> ids;
    for (int id = 0; id < kFirstColosseumFormation; ++id) {
        if (keep(static_cast<FormationId>(id))) {
            ids.push_back(id);
        }
    }
    return ids;
}

const std::vector<int>& setupFormations() {
    static const std::vector<int> ids =
        formationsWhere([](FormationId id) { return getFormationAux(id).characterAiEnabled(); });
    return ids;
}

const std::vector<int>& formationsOfOnlyAPincerOrABackAttack() {
    static const std::vector<int> ids = formationsWhere([](FormationId id) {
        const std::uint8_t types = getFormationAux(id).possibleBattleTypes();
        return types == kPincerBit || types == kBackBit;
    });
    return ids;
}

const std::vector<int>& formationsOfOnlyASideAttack() {
    static const std::vector<int> ids =
        formationsWhere([](FormationId id) { return getFormationAux(id).possibleBattleTypes() == kSideBit; });
    return ids;
}

const std::vector<int>& formationsWithASideAttack() {
    static const std::vector<int> ids = formationsWhere([](FormationId id) { return getFormationAux(id).sidePossible(); });
    return ids;
}

const std::vector<int>& formationsWithAPincer() {
    static const std::vector<int> ids =
        formationsWhere([](FormationId id) { return getFormationAux(id).pincerPossible(); });
    return ids;
}

const std::vector<int>& formationsWithABackAttack() {
    static const std::vector<int> ids = formationsWhere([](FormationId id) { return getFormationAux(id).backPossible(); });
    return ids;
}

const std::vector<int>& formationsGauMayFollow() {
    static const std::vector<int> ids =
        formationsWhere([](FormationId id) { return getFormation(id).slotEmpty(kGauReturnSlot); });
    return ids;
}

int pick(std::mt19937& rng, const std::vector<int>& from) { return from[rng() % from.size()]; }

// The battle type and strike a drawn start aims its play clock at.
enum class Aim : std::uint8_t { Front, Back, Pincer, Side, Preemptive };
constexpr unsigned kAims = 5;

// The kinds of start the sweep draws, one time in twelve each, every one set up to reach what it is
// named for.
enum class StartKind : std::uint8_t {
    Colosseum,
    FinalBattle,
    ScriptedSetup,
    GausReturn,
    ConditionalBattle,
    PermanentMorph,
    SideAttack,
    PincerAttack,
    BackAttack,
    BackGuardWithNothingElse,
    FewerThanThreeWithOnlyASideAttack,
    AnyFormation,
};
constexpr unsigned kStartKinds = 12;

bool aimed(const battle::BattleState& state, Aim aim) {
    switch (aim) {
        case Aim::Front:
            return state.battleType == battle::BattleType::Front && !state.preemptive;
        case Aim::Back:
            return state.battleType == battle::BattleType::Back;
        case Aim::Pincer:
            return state.battleType == battle::BattleType::Pincer;
        case Aim::Side:
            return state.battleType == battle::BattleType::Side;
        case Aim::Preemptive:
            return state.preemptive;
    }
    return false;
}

// The records in the party played, last first.
std::vector<std::size_t> playing(const game::Party& party) {
    std::vector<std::size_t> records;
    for (std::size_t r = game::kCharacterRecords; r-- > 0;) {
        if (party.membership[r].party == party.currentParty) {
            records.push_back(r);
        }
    }
    return records;
}

// The party played made `count` members in slots 0 on, each standing (not dead, petrified or a
// zombie, and with HP); every other record of the party leaves it.
void seatStanding(std::mt19937& rng, game::Party& party, int count) {
    for (game::PartyMembership& member : party.membership) {
        if (member.party == party.currentParty) {
            member.party = 0;
        }
    }
    std::vector<std::size_t> records(game::kCharacterRecords);
    for (std::size_t r = 0; r < records.size(); ++r) {
        records[r] = r;
    }
    std::shuffle(records.begin(), records.end(), rng);
    for (int slot = 0; slot < count; ++slot) {
        const std::size_t r = records[static_cast<std::size_t>(slot)];
        party.membership[r] = game::PartyMembership{.party = party.currentParty,
                                                    .slot = static_cast<std::uint8_t>(slot),
                                                    .backRow = oneIn(rng, 2),
                                                    .enabled = true,
                                                    .visible = true};
        game::CharacterRecord& record = party.characters[r];
        for (const StatusId fallen : {StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE}) {
            record.status.clear(fallen);
        }
        record.maxHp = std::max<std::uint16_t>(record.maxHp, 1);
        record.hp = std::max<std::uint16_t>(record.hp, 1);
    }
}

PartyCells drawParty(std::mt19937& rng) {
    PartyCells cells = drawPartyCells(rng);
    game::Party& party = cells.party;
    party.currentParty = static_cast<std::uint8_t>(1 + rng() % 3);
    for (game::CharacterRecord& record : party.characters) {
        for (BattleCommandId& command : record.commands) {
            command = oneIn(rng, 5) ? BattleCommandId::NONE : static_cast<BattleCommandId>(rng() % kRecordCommands);
        }
    }
    for (game::InventorySlot& slot : party.inventory) {
        slot = oneIn(rng, 2) ? game::InventorySlot{.item = ItemId::EMPTY, .quantity = 0}
                             : game::InventorySlot{.item = drawItem(rng),
                                                   .quantity = static_cast<std::uint8_t>(1 + upTo(rng, kMaxItemQuantity - 1))};
    }
    party.steps = static_cast<std::uint32_t>(rng() % (kStepsMost + 1));
    party.playTimeAtLastSave = game::PlayTime{.hours = static_cast<std::uint8_t>(upTo(rng, kSaveHoursMost)),
                                              .minutes = static_cast<std::uint8_t>(upTo(rng, kSaveMinutesMost))};
    return cells;
}

}  // namespace

void giveTheCastRecords(StartScenario& scenario) {
    game::Party& party = scenario.cells.party;
    std::set<CharAiId> setups;
    for (const FormationId fought : formationsFought(scenario.start.formation, scenario.switches)) {
        const FormationAux& aux = getFormationAux(fought);
        if (aux.characterAiEnabled()) {
            setups.insert(static_cast<CharAiId>(aux.characterAiIndex()));
        }
    }
    if (scenario.start.gauMayAppear) {
        setups.insert(CharAiId::GAU_VELDT);
    }
    // The records a start reads besides the party played's: the final battle's line and the
    // colosseum's fighter.
    std::set<std::size_t> kept{scenario.start.finalBattleOrder.begin(), scenario.start.finalBattleOrder.end()};
    for (std::size_t r = 0; r < game::kCharacterRecords; ++r) {
        if (party.membership[r].party == party.currentParty ||
            (scenario.start.colosseum && party.characters[r].actor == scenario.start.colosseum->fighter)) {
            kept.insert(r);
        }
    }
    for (const CharAiId setup : setups) {
        for (const CharAiSlot& cast : charAiSetup(setup).slots) {
            if (cast.isEmpty()) {
                continue;
            }
            const CharacterPropId actor = cast.character.characterProperties();
            if (holdsActor(party, actor)) {
                continue;
            }
            for (std::size_t record = game::kCharacterRecords; record-- > 0;) {
                if (!kept.contains(record)) {
                    party.characters[record].actor = actor;
                    kept.insert(record);
                    break;
                }
            }
        }
    }
}

StartScenario drawStartScenario(std::mt19937& rng, const TextCorpus& text, const MonsterSprites& sprites,
                                const assets::HiRomImage& cartridge) {
    StartScenario s;
    // The byte above the random numbers' position, from a generator of its own so every other draw is
    // what it would be without it: 0 three times in four, 1 to 255 otherwise.
    std::mt19937 above = generatorOfItsOwn(rng, kAboveRngPositionPlace);
    setByteAboveRngPosition(
        s, cartridge, oneIn(above, kAboveRngPositionSetOneIn) ? static_cast<std::uint8_t>(1 + upTo(above, kByteMost - 1)) : 0);
    for (std::uint8_t& variable : s.switches.variables) {
        variable = byte(rng);
    }
    s.cells = drawParty(rng);
    game::Party& party = s.cells.party;
    battle::BattleStart& start = s.start;

    start.background = static_cast<BattleBackgroundId>(rng() % kBackgrounds);
    start.onTheVeldt = oneIn(rng, 4);
    start.keepFieldMusic = oneIn(rng, 4);
    start.gauRecruited = oneIn(rng, 2);
    start.shadowAvailable = oneIn(rng, 2);
    start.config = battle::BattleConfig{.waitMode = oneIn(rng, 2),
                                        .speedSetting = static_cast<std::uint8_t>(rng() % kSpeedSettings),
                                        .gaugeShown = oneIn(rng, 2),
                                        .magicOrder = static_cast<std::uint8_t>(rng() % kSpellOrders)};
    start.playTimeFrames =
        oneIn(rng, 8) ? byte(rng) : static_cast<std::uint8_t>(1 + upTo(rng, kPlayClockFramesMost - 1));

    // What the start is drawn to reach: the play clock is aimed at a battle type or a preemptive
    // strike half the time, or always where the kind of start asks for one.
    std::optional<Aim> aim;
    int backGuardOneIn = 4;
    int galeHairpinOneIn = 4;
    const auto anyFormation = [&](int formation) {
        const bool randomized = formation + kRandomizedSpread < kFirstColosseumFormation && oneIn(rng, 3);
        start.formation = FormationRef::of(static_cast<FormationId>(formation), randomized);
    };
    switch (static_cast<StartKind>(rng() % kStartKinds)) {
        case StartKind::Colosseum: {
            const auto fighter = static_cast<std::size_t>(rng() % game::kCharacterRecords);
            start.formation = FormationRef::of(static_cast<FormationId>(kFirstColosseumFormation + rng() % 2));
            start.colosseum = battle::ColosseumMatch{.wager = drawItem(rng),
                                                     .prize = drawItem(rng),
                                                     .opponent = static_cast<MonsterId>(byte(rng)),
                                                     .fighter = party.characters[fighter].actor};
            break;
        }
        case StartKind::FinalBattle: {
            start.formation = FormationRef::of(kFinalBattleFormations[0]);
            std::vector<std::size_t> records(game::kCharacterRecords);
            for (std::size_t r = 0; r < records.size(); ++r) {
                records[r] = r;
            }
            std::shuffle(records.begin(), records.end(), rng);
            records.resize(kFinalLineLeast + rng() % (kFinalLineMost - kFinalLineLeast + 1));
            start.finalBattleOrder = records;
            break;
        }
        case StartKind::ScriptedSetup:
            anyFormation(pick(rng, setupFormations()));
            break;
        case StartKind::GausReturn:
            start.formation = FormationRef::of(static_cast<FormationId>(pick(rng, formationsGauMayFollow())));
            start.gauMayAppear = true;
            start.gauRecruited = false;
            seatStanding(rng, party, 1 + static_cast<int>(rng() % 3));
            break;
        case StartKind::ConditionalBattle: {
            const int row = static_cast<int>(rng() % kConditionalBattles);
            s.switches.set(game::kConditionalBattleSwitches + row);
            start.formation = getConditionalBattle(static_cast<std::size_t>(row)).trigger;
            break;
        }
        case StartKind::PermanentMorph: {
            anyFormation(static_cast<int>(rng() % kFirstColosseumFormation));
            seatStanding(rng, party, 1 + static_cast<int>(rng() % battle::kPartySlots));
            s.switches.set(game::kPermanentMorphSwitch);
            s.switches.set(game::kMorphAvailableSwitch);
            s.switches.clear(game::kMagicOnlySwitch);
            const std::vector<std::size_t> members = playing(party);
            game::CharacterRecord& morpher = party.characters[members[rng() % members.size()]];
            morpher.commands[rng() % morpher.commands.size()] = BattleCommandId::MORPH;
            morpher.status.clear(StatusId::MAGITEK);
            break;
        }
        case StartKind::SideAttack:
            anyFormation(pick(rng, formationsWithASideAttack()));
            seatStanding(rng, party, 3 + static_cast<int>(rng() % 2));
            aim = Aim::Side;
            galeHairpinOneIn = 2;
            break;
        case StartKind::PincerAttack:
            anyFormation(pick(rng, formationsWithAPincer()));
            aim = Aim::Pincer;
            backGuardOneIn = 0;
            break;
        case StartKind::BackAttack:
            anyFormation(pick(rng, formationsWithABackAttack()));
            aim = Aim::Back;
            backGuardOneIn = 0;
            break;
        case StartKind::BackGuardWithNothingElse:
            start.formation = FormationRef::of(static_cast<FormationId>(pick(rng, formationsOfOnlyAPincerOrABackAttack())));
            seatStanding(rng, party, 1 + static_cast<int>(rng() % battle::kPartySlots));
            backGuardOneIn = 1;
            break;
        case StartKind::FewerThanThreeWithOnlyASideAttack:
            start.formation = FormationRef::of(static_cast<FormationId>(pick(rng, formationsOfOnlyASideAttack())));
            seatStanding(rng, party, 1 + static_cast<int>(rng() % 2));
            break;
        case StartKind::AnyFormation:
            anyFormation(static_cast<int>(rng() % kFirstColosseumFormation));
            start.gauMayAppear = oneIn(rng, 8);
            break;
    }

    // A Back Guard or a Gale Hairpin on someone playing.
    const std::vector<std::size_t> members = playing(party);
    if (!members.empty()) {
        if (backGuardOneIn != 0 && oneIn(rng, static_cast<unsigned>(backGuardOneIn))) {
            party.characters[members[rng() % members.size()]].relic1 = ItemId::BACK_GUARD;
        }
        if (oneIn(rng, static_cast<unsigned>(galeHairpinOneIn))) {
            party.characters[members[rng() % members.size()]].relic2 = ItemId::GALE_HAIRPIN;
        }
    }
    giveTheCastRecords(s);

    // The play clock aimed: the first frames whose start the port makes the battle type or strike.
    if (!aim && oneIn(rng, 2)) {
        aim = static_cast<Aim>(rng() % kAims);
    }
    if (aim) {
        for (int frames = 1; frames <= kPlayClockFramesMost; ++frames) {
            StartScenario tried = s;
            tried.start.playTimeFrames = static_cast<std::uint8_t>(frames);
            if (aimed(portStart(tried, text, sprites), *aim)) {
                start.playTimeFrames = static_cast<std::uint8_t>(frames);
                break;
            }
        }
    }
    return s;
}

std::string describe(const StartScenario& scenario) {
    const battle::BattleStart& start = scenario.start;
    const game::Party& party = scenario.cells.party;
    std::string line = "formation " + std::to_string(static_cast<int>(start.formation.formationId()));
    if (start.formation.randomizePlus3()) {
        line += " (one of four)";
    }
    line += ", party " + std::to_string(party.currentParty) + " [";
    bool first = true;
    for (std::size_t r = 0; r < game::kCharacterRecords; ++r) {
        if (party.membership[r].party == party.currentParty) {
            line += (first ? "" : " ") + std::to_string(r) + ":" + std::to_string(static_cast<int>(party.characters[r].actor)) +
                    "@" + std::to_string(party.membership[r].slot);
            first = false;
        }
    }
    line += "], play clock " + std::to_string(start.playTimeFrames);
    if (start.colosseum) {
        line += ", colosseum fighter " + std::to_string(static_cast<int>(start.colosseum->fighter)) + " against " +
                std::to_string(static_cast<int>(start.colosseum->opponent));
    }
    if (!start.finalBattleOrder.empty()) {
        line += ", final battle line of " + std::to_string(start.finalBattleOrder.size());
    }
    if (start.gauMayAppear) {
        line += ", Gau may appear";
    }
    return line;
}

std::span<const std::string_view> startReachRows() { return kReachRows; }

std::vector<std::string_view> startReached(const StartScenario& scenario, const battle::BattleState& port,
                                           const BattleCells& cartridge) {
    std::vector<std::string_view> reached;
    const auto reach = [&](std::size_t row) { reached.push_back(kReachRows[row]); };
    const std::uint8_t type = at(cartridge, kBattleType);
    if (type == kBattleTypeFront) reach(0);
    if (type == kBattleTypeBack) reach(1);
    if (type == kBattleTypePincer) reach(2);
    if (type == kBattleTypeSide) reach(3);
    if ((at(cartridge, kBattleFlags0) & kPreemptiveBit) != 0) reach(4);
    if (scenario.start.colosseum) reach(5);
    if (port.finalBattleChain) reach(6);
    const bool setupOn = (at(cartridge, kSetupFlags) & kSetupOn) != 0;
    const bool gauReturns = setupOn && at(cartridge, kSetupIndex) == static_cast<std::uint8_t>(CharAiId::GAU_VELDT);
    if (setupOn && !gauReturns) reach(7);
    if (gauReturns) reach(8);
    if (!port.colosseum) {
        for (int position = 0; position < battle::kPartySlots; ++position) {
            if (!port.at(battle::BattlerSlot::character(position)).actor) {
                reach(9);
                break;
            }
        }
    }
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (port.at(battle::BattlerSlot::character(position)).status.has(StatusId::MORPH)) {
            reach(10);
            break;
        }
    }
    if (scenario.start.formation.randomizePlus3()) reach(11);
    if (port.startingFormation != scenario.start.formation.formationId() && !scenario.start.formation.randomizePlus3()) {
        reach(12);
    }

    // ChooseBattleType's branches (:7556-7578), over the types the formation fought allows.
    std::uint8_t possible = getFormationAux(port.formation).possibleBattleTypes();
    if (port.partyRelicEffects2.has(RelicEffect2::PREVENT_BACK_PINCER)) {
        if ((possible & kAllButPincer) != 0) {
            possible = static_cast<std::uint8_t>(possible & kAllButPincer);
            reach(15);
        } else {
            reach(16);
        }
        if ((possible & kAllButBack) != 0) {
            possible = static_cast<std::uint8_t>(possible & kAllButBack);
            reach(17);
        } else {
            reach(18);
        }
    }
    if (port.allyCount < kAlliesForSideAttack) {
        reach((possible & kAllButSide) != 0 ? 13 : 14);
    }
    // InitBattleType_00/_03 (:7621-7641): the Gale Hairpin doubles the chance in a front or side
    // attack the menus and the formation leave open to a preemptive strike; with the menus shut there
    // is no draw.
    const FormationAux& aux = getFormationAux(port.formation);
    if ((type == kBattleTypeFront || type == kBattleTypeSide) && !aux.preemptiveDisabled()) {
        if (port.menusDisabled) {
            reach(21);
        } else if (port.partyRelicEffects2.has(RelicEffect2::RAISE_PREEMPTIVE_RATE)) {
            reach(type == kBattleTypeFront ? 19 : 20);
        }
    }
    return reached;
}

}  // namespace ostinato::trace
