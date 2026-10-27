#include "battle/battle_start.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <utility>
#include <variant>

#include "battle/actions.h"
#include "battle/battle_core.h"
#include "battle/battle_inventory.h"
#include "battle/battle_layout.h"
#include "battle/party_setup.h"
#include "data/battle_tables.h"
#include "data/formations.h"
#include "data/monster_items.h"
#include "data/monster_properties.h"
#include "data/monster_special_anim.h"
#include "ostinato/char_ai_id.h"
#include "ostinato/text_class.h"

namespace ostinato::battle {

namespace {

// The colosseum's formations are the last two.
constexpr FormationId kFirstColosseumFormation = FormationId::UNUSED_574;

// Doom Gaze's formation: his HP carries over from one meeting to the next.
constexpr FormationId kDoomGazeFormation = FormationId::DOOM_GAZE;

constexpr int kConditionalBattles = 8;

// Monster names are compared eight bytes at a time, eight bytes into the name table per monster.
constexpr std::size_t kComparedNameBytes = 8;

// The battle-type rates are the fifth row of the four-way rate table.
constexpr std::size_t kBattleTypeRates = 4;

// The preemptive-strike chance, out of 256: 32 in a front attack and 44 in a side attack, doubled
// by a Gale Hairpin.
constexpr int kPreemptiveBase = 32;
constexpr int kPreemptivePerBattleType = 4;

// A monster's stamina: 16, plus one for each 512 of its maximum HP, up to 40.
constexpr int kMonsterStaminaBase = 16;
constexpr int kMonsterStaminaMax = 40;
constexpr int kHpPerStaminaPoint = 512;

// A monster's vigor: 56 to 63.
constexpr int kMonsterVigorBase = 56;
constexpr int kMonsterVigorSpread = 8;

// How far apart the starting gauges are spread: 8 steps per place in the random order, and 16 more
// for every slot left empty.
constexpr int kGaugeStepsPerPlace = 8;
constexpr int kGaugeStepsPerEmptySlot = 16;

// The battle messages the opening shows.
constexpr std::uint8_t kMessagePreemptive = 34;
constexpr std::uint8_t kMessageBackAttack = 35;
constexpr std::uint8_t kMessageSideAttack = 36;
constexpr std::uint8_t kMessagePincerAttack = 37;

constexpr std::uint8_t kStatMax = 255;

// The statuses a won final-battle stage takes off the fighters going on to the next.
const StatusSet kClearedBetweenStages = StatusSet::of(StatusId::DANCE, StatusId::RAGE, StatusId::CHANT);

const StatusSet kFallen = StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE);

// The lasting statuses a party member's record keeps from the fourth status byte.
const StatusSet kRecordedFromFourthByte = StatusSet::of(StatusId::INTERCEPTOR, StatusId::FLOAT);

// The first status byte, all of which the record keeps.
const StatusSet kFirstStatusByte =
    StatusSet::of(StatusId::BLIND, StatusId::ZOMBIE, StatusId::POISON, StatusId::MAGITEK, StatusId::INVISIBLE,
                  StatusId::IMP, StatusId::PETRIFY, StatusId::DEAD);

template <typename Visit>
void forEachSlotLastFirst(Visit visit) {
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        visit(BattlerSlot::fromIndex(index));
    }
}

StatusSet unionOf(StatusSet a, const StatusSet& b) {
    for (std::size_t i = 0; i < a.bytes.size(); ++i) {
        a.bytes[i] = static_cast<std::uint8_t>(a.bytes[i] | b.bytes[i]);
    }
    return a;
}

StatusSet intersectionOf(StatusSet a, const StatusSet& b) {
    for (std::size_t i = 0; i < a.bytes.size(); ++i) {
        a.bytes[i] = static_cast<std::uint8_t>(a.bytes[i] & b.bytes[i]);
    }
    return a;
}

ElementSet unionOf(ElementSet a, ElementSet b) { return ElementSet{static_cast<std::uint8_t>(a.bits | b.bits)}; }

}  // namespace

// --- the formation -------------------------------------------------------------------------------

FormationId chooseFormation(BattleState& state, FormationRef asked) {
    for (int row = kConditionalBattles - 1; row >= 0; --row) {
        if (!state.switches.isSet(game::kConditionalBattleSwitches + row)) {
            continue;
        }
        const ConditionalBattle& conditional = getConditionalBattle(static_cast<std::size_t>(row));
        if (conditional.trigger.raw == asked.raw) {
            asked = conditional.replacement;
        }
    }
    FormationId formation = asked.formationId();
    if (asked.randomizePlus3()) {
        formation = static_cast<FormationId>(static_cast<int>(formation) + (state.rng.next() & 3));
    }
    return formation;
}

namespace {

PartyLineup lineupFor(const BattleState& state, const BattleStart& start, const FormationAux& aux,
                      FinalBattleStage stage) {
    PartyLineup lineup;
    if (state.finalBattleChain) {
        lineup.fighters = stage;
    } else if (state.startingFormation >= kFirstColosseumFormation) {
        lineup.fighters = ColosseumFighter{.fighter = start.colosseum.value_or(ColosseumMatch{}).fighter};
    } else {
        lineup.fighters = CurrentParty{};
    }
    if (aux.characterAiEnabled()) {
        lineup.scriptedSetup = static_cast<CharAiId>(aux.characterAiIndex());
    }
    return lineup;
}

// --- the monsters --------------------------------------------------------------------------------

// The eight bytes of the name table a monster's name is compared by.
std::span<const std::uint8_t> nameKey(const TextCorpus& text, MonsterId monster) {
    const std::span<const std::uint8_t> names = text.rawBytes(TextClass::MONSTER_NAME);
    const std::size_t at = static_cast<std::size_t>(monster) * kComparedNameBytes;
    if (at + kComparedNameBytes > names.size()) {
        return {};
    }
    return names.subspan(at, kComparedNameBytes);
}

// The monster whose line in the list of names the monster in `slot` shares: the monster in the
// highest slot loaded so far — itself included — whose name compares equal.
MonsterId nameGroupFor(const BattleState& state, BattlerSlot slot, const TextCorpus& text) {
    const MonsterId own = *state.at(slot).monster;
    const std::span<const std::uint8_t> key = nameKey(text, own);
    if (key.empty()) {
        return own;
    }
    for (int position = kMonsterSlots - 1; position >= 0; --position) {
        const Battler& other = state.at(BattlerSlot::monster(position));
        if (!other.monster) {
            continue;
        }
        const std::span<const std::uint8_t> otherKey = nameKey(text, *other.monster);
        if (std::equal(key.begin(), key.end(), otherKey.begin(), otherKey.end())) {
            return *other.monster;
        }
    }
    return own;
}

// What a monster's record says about it beyond what a Rage takes on: its name, whether it is
// human or undead, the Imp critical and dying at zero MP.
void loadMonsterNature(Battler& battler, const MonsterProperties& record) {
    battler.diesAtZeroMp = record.traitFlags.has(MonsterTraitFlag::DIES_AT_ZERO_MP);
    battler.nameHidden = record.traitFlags.has(MonsterTraitFlag::DONT_DISPLAY_NAME);
    battler.human = record.traitFlags.has(MonsterTraitFlag::HUMAN);
    battler.impCritical = record.traitFlags.has(MonsterTraitFlag::IMP_CRITICAL);
    battler.undead = record.traitFlags.has(MonsterTraitFlag::UNDEAD);
}

// A monster with first strike, on the field when the battle opens, acts before anyone: its gauge
// is full, its advance wait as good as done, and the battle menus stay shut until a battler has
// finished its turn.
void checkFirstStrike(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    if (!battler.monsterFlags.has(MonsterBattleFlag::FIRST_STRIKE) || !battler.present) {
        return;
    }
    battler.atbStopped = true;
    battler.gauge.setMeter(0);
    battler.advanceWaitCounter = static_cast<std::uint16_t>(0xFF00 | (battler.advanceWaitCounter & 0x00FF));
    joinAdvanceWaitLine(state, slot);
    state.menusDisabled = true;
}

}  // namespace

StatusSet loadMonsterTraits(Battler& battler, MonsterId id) {
    const MonsterProperties& record = getMonsterProperties(id);
    battler.specialAttack = record.specialAttack;
    battler.elementsWeak = unionOf(battler.elementsWeak, record.weakElements);
    for (std::size_t i = 0; i < record.blockedStatuses.bytes.size(); ++i) {
        battler.inflictableStatuses.bytes[i] =
            static_cast<std::uint8_t>(battler.inflictableStatuses.bytes[i] & ~record.blockedStatuses.bytes[i]);
    }

    // The record's third status byte opens with the monster's flying flag, which is Float; the
    // fourth byte's first bit is the True Knight cover effect, and only Reraise and Float of it are
    // statuses a monster starts with.
    const StatusSet& innate = record.innateStatuses;
    const bool flies = (innate.bytes[2] & 0x01) != 0;
    StatusSet starts{{innate.bytes[0], innate.bytes[1], static_cast<std::uint8_t>(innate.bytes[2] & 0xFE), 0}};
    if (flies || innate.has(StatusId::FLOAT)) starts.set(StatusId::FLOAT);
    if (innate.has(StatusId::RERAISE)) starts.set(StatusId::RERAISE);
    if ((innate.bytes[3] & 0x01) != 0) {
        battler.relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
    }

    // The second and third status bytes stay on it, as equipment keeps statuses on a party member;
    // the flying flag there is Float.
    StatusSet held{{0, innate.bytes[1], static_cast<std::uint8_t>(innate.bytes[2] & 0xFE), 0}};
    if (flies) held.set(StatusId::FLOAT);
    battler.equipmentStatuses = unionOf(battler.equipmentStatuses, held);

    battler.elementsAbsorbed = unionOf(battler.elementsAbsorbed, record.absorbElements);
    battler.elementsNullified = unionOf(battler.elementsNullified, record.nullifyElements);
    battler.metamorph = record.metamorph;
    return starts;
}

void loadMonster(BattleState& state, ActionExecutor& executor, BattlerSlot slot, MonsterId id,
                 const TextCorpus& text, MonsterHp hp) {
    Battler& battler = state.at(slot);
    const MonsterProperties& record = getMonsterProperties(id);
    battler.monster = id;
    battler.aiScript = static_cast<std::uint16_t>(id);
    executor.initAi(state, slot);
    const MonsterItems& items = getMonsterItems(id);
    battler.rareSteal = items.rareSteal;
    battler.commonSteal = items.commonSteal;
    battler.nameGroup = nameGroupFor(state, slot, text);

    battler.defense = record.defense;
    battler.magicDefense = record.magicDefense;
    battler.experience = record.experience;
    battler.gold = record.gold;
    if (hp == MonsterHp::FromRecord) {
        battler.mp = battler.maxMp = record.mp;
        battler.hp = battler.maxHp = record.hp;
        if (state.startingFormation == kDoomGazeFormation) {
            state.doomGaze = slot;
            if (const std::uint16_t remembered = state.switches.doomGazeHp(); remembered != 0) {
                battler.hp = remembered;
            }
        }
    }
    battler.stamina = static_cast<std::uint8_t>(
        std::min(kMonsterStaminaMax, kMonsterStaminaBase + battler.maxHp / kHpPerStaminaPoint));
    battler.weaponHand.battlePower = record.attackPower;
    battler.weaponHand.item = record.attackGraphic;
    battler.evadeFactor = hitCheckFactor(record.evade);
    battler.magicBlockFactor = hitCheckFactor(record.magicBlock);
    battler.weaponHand.hitRate = record.hitRate;
    battler.level = record.level;
    battler.speed = record.speed;
    battler.magicPower = static_cast<std::uint8_t>(std::min<int>(kStatMax, record.magicPower + record.magicPower / 2));

    const std::uint8_t special = record.innateStatuses.bytes[3];
    battler.triggers.monsterRunic = battler.triggers.monsterRunic || (special & 0x02) != 0;
    battler.triggers.piranha = battler.triggers.piranha || (special & 0x80) != 0;
    battler.monsterFlags = record.battleFlags;
    battler.innateStatuses = loadMonsterTraits(battler, id);
    loadMonsterNature(battler, record);
    battler.specialAttackAnimation = monsterSpecialAnim(id);
    checkFirstStrike(state, slot);
    battler.vigor = static_cast<std::uint8_t>(kMonsterVigorBase + (state.rng.next() & (kMonsterVigorSpread - 1)));
    applyDeathAndPoisonImmunity(battler);
}

namespace {

void loadMonsters(BattleState& state, ActionExecutor& executor, const TextCorpus& text,
                  MonsterHp hp = MonsterHp::FromRecord) {
    const Formation& formation = getFormation(state.formation);
    for (int position = kMonsterSlots - 1; position >= 0; --position) {
        state.at(BattlerSlot::monster(position)).present = formation.isPresent(static_cast<std::size_t>(position));
    }
    for (int position = kMonsterSlots - 1; position >= 0; --position) {
        const auto at = static_cast<std::size_t>(position);
        std::optional<MonsterId> monster;
        if (state.colosseum) {
            // A colosseum match puts its opponent in every slot whose monster number is below 256.
            if ((formation.monsterIdHigh & (1u << at)) == 0 && state.colosseumOpponent) {
                monster = state.colosseumOpponent;
            }
        } else if (!formation.slotEmpty(at)) {
            monster = formation.monsterId(at);
        }
        if (monster) {
            loadMonster(state, executor, BattlerSlot::monster(position), *monster, text, hp);
        }
    }
}

// --- the battle type -----------------------------------------------------------------------------

// The battle types are bits $10 front, $20 back, $40 pincer, $80 side of the formation's mask.
constexpr std::uint8_t kFront = 0x10;
constexpr std::uint8_t kBack = 0x20;
constexpr std::uint8_t kPincer = 0x40;
constexpr std::uint8_t kSide = 0x80;
constexpr int kAlliesForSideAttack = 3;

std::uint8_t onlyIfAnyLeft(std::uint8_t possible, std::uint8_t keep) {
    return (possible & keep) != 0 ? static_cast<std::uint8_t>(possible & keep) : possible;
}

}  // namespace

// One of the possible battle types, weighted by the battle-type rates: side, pincer, back and
// front in that order of the table's row.
BattleType chooseBattleType(BattleState& state, std::uint8_t possible) {
    if (state.partyRelicEffects2.has(RelicEffect2::PREVENT_BACK_PINCER)) {
        possible = onlyIfAnyLeft(possible, kFront | kBack | kSide);
        possible = onlyIfAnyLeft(possible, kFront | kPincer | kSide);
    }
    if (state.allyCount < kAlliesForSideAttack) {
        possible = onlyIfAnyLeft(possible, kFront | kBack | kPincer);
    }
    constexpr std::array<std::uint8_t, 4> kByWeight{kSide, kPincer, kBack, kFront};
    constexpr std::array<BattleType, 4> kTypeByWeight{BattleType::Side, BattleType::Pincer, BattleType::Back,
                                                      BattleType::Front};
    std::array<bool, 4> offered{};
    for (std::size_t i = 0; i < kByWeight.size(); ++i) {
        offered[i] = (possible & kByWeight[i]) != 0;
    }
    const std::optional<std::size_t> picked =
        pickWeighted(state.rng, kRandomBitRates[kBattleTypeRates].weights, offered);
    return picked ? kTypeByWeight[*picked] : BattleType::Front;
}

void applyBattleType(BattleState& state, const FormationAux& aux) {
    if (state.battleType != BattleType::Front) {
        state.gauMayAppear = false;
    }
    switch (state.battleType) {
        case BattleType::Front:
        case BattleType::Side: {
            if (state.menusDisabled || aux.preemptiveDisabled()) {
                break;
            }
            int chance = kPreemptiveBase | (static_cast<int>(state.battleType) * kPreemptivePerBattleType);
            if (state.partyRelicEffects2.has(RelicEffect2::RAISE_PREEMPTIVE_RATE)) {
                chance *= 2;
            }
            if (state.rng.next() < chance) {
                state.preemptive = true;
            }
            break;
        }
        case BattleType::Pincer:
            for (int position = 0; position < kPartySlots; ++position) {
                state.at(BattlerSlot::character(position)).backRow = false;
            }
            state.backAttack = true;
            break;
        case BattleType::Back:
            for (int position = 0; position < kPartySlots; ++position) {
                Battler& battler = state.at(BattlerSlot::character(position));
                battler.backRow = !battler.backRow;
            }
            state.backAttack = true;
            break;
    }
}

// --- the gauges ----------------------------------------------------------------------------------

// Where each battler's gauge starts. The ten slots are given the places 0-9 in a random order; a
// slot's place spreads it from the others. A gauge already set (a first strike) keeps its value,
// and a final-battle stage after the first leaves every gauge to fill from nearly full.
void fillStartingGauges(BattleState& state) {
    int emptySlots = 0;
    for (const Battler& battler : state.battlers) {
        if (!battler.present) {
            ++emptySlots;
        }
    }
    BattlerSet placesLeft = BattlerSet::allCharacters() | BattlerSet::allMonsters();
    forEachSlotLastFirst([&](BattlerSlot slot) {
        const BattlerSlot drawn = *state.rng.pickFrom(placesLeft);
        placesLeft.remove(drawn);
        const int spread = drawn.index() * kGaugeStepsPerPlace;

        Battler& battler = state.at(slot);
        if (battler.gauge.meter() != 0xFF || state.finalBattleScroll) {
            return;
        }
        const bool enemySide = slot.isMonster() || battler.actsAsEnemy;
        int start = 0;
        auto random = [&] {
            int value = battler.speed + state.rng.below(battler.speed);
            value = std::min<int>(kStatMax, value);
            if (value < kStatMax) value = std::min<int>(kStatMax, value + spread);
            if (value < kStatMax) value = std::min<int>(kStatMax, value + emptySlots * kGaugeStepsPerEmptySlot);
            return value;
        };
        if (enemySide) {
            start = state.preemptive || state.battleType == BattleType::Side ? 1 : random();
        } else {
            if (state.preemptive || state.battleType == BattleType::Side) {
                return;  // left nearly full: the party acts first
            }
            start = state.battleType == BattleType::Front ? random() : spread;
        }
        battler.gauge.setMeter(static_cast<std::uint8_t>(start == kStatMax ? kStatMax : start + 1));
    });
}

void changeFormation(BattleState& state, ActionExecutor& executor, FormationId formation, MonsterHp hp,
                     const TextCorpus& text, const MonsterSprites& sprites) {
    state.formation = chooseFormation(state, FormationRef::of(formation));
    // Every monster slot starts over: off the field, nothing pending but the start's
    // recalculations, no monster in it yet.
    for (int position = kMonsterSlots - 1; position >= 0; --position) {
        Battler& battler = state.at(BattlerSlot::monster(position));
        battler.present = false;
        battler.takesMenuInput = false;
        battler.inBattleMenu = false;
        battler.atbStopped = false;
        battler.heldByStatus = false;
        battler.actionQueued = false;
        battler.heldByControl = false;
        battler.menuMayOpen = false;
        battler.triggers = {};
        battler.monster.reset();
        battler.pendingUpdates = PendingUpdates{.enabledMagic = true,
                                                .startCondemned = true,
                                                .stopCondemned = true,
                                                .commandList = true,
                                                .speed = true};
        battler.nearFatalSpellAvailable = true;
        battler.airAnchored = false;
        battler.mayActAfterJump = true;
    }
    loadMonsters(state, executor, text, hp);
    executor.applyStatusChanges(state);
    checkTheFallen(state, executor);
    settleAfterAction(state, executor);
    refreshAliveSets(state);
    forEachSlotLastFirst([&](BattlerSlot slot) { refreshStatusImmunity(state, slot); });
    state.battleType = chooseBattleType(state, getFormationAux(state.formation).possibleBattleTypes());
    // Showing the change marks the new formation's monsters still fighting as the ones standing.
    state.monstersStanding = state.alive.monstersOnly();
    layOutMonsters(state, sprites);
}

namespace {

// --- the opening ---------------------------------------------------------------------------------

std::optional<std::uint8_t> openingMessage(const BattleState& state, const FormationAux& aux) {
    if (aux.preemptiveDisabled()) {
        return std::nullopt;
    }
    switch (state.battleType) {
        case BattleType::Back:
            return kMessageBackAttack;
        case BattleType::Pincer:
            return kMessagePincerAttack;
        case BattleType::Side:
            return state.preemptive ? kMessagePreemptive : kMessageSideAttack;
        case BattleType::Front:
            break;
    }
    return state.preemptive ? std::optional<std::uint8_t>(kMessagePreemptive) : std::nullopt;
}

// What a final-battle stage carries into the next: the statuses and timers that last the whole
// final battle, Mimic's memory, who is in the air, and who has summoned.
struct StageCarry {
    std::array<StatusSet, kBattlerSlots> status{};
    std::array<std::uint8_t, kBattlerSlots> reflectTimer{};
    std::array<std::uint8_t, kBattlerSlots> freezeTimer{};
    MimicMemory mimic;
    BattlerSet awayFromTheField;
    BattlerSet espersSummoned;
    std::optional<BattlerSlot> morphedBattler;
    FormationId startingFormation{};
    std::deque<std::size_t> finalBattleReserve;
    std::uint8_t scroll = 0;
    FinalBattleStage standing;
    // The party keeps the way it faces and the last animation's targets; only the monsters are
    // placed anew.
    BattlerSet partyFacingRight;
    BattlerSet animationTargets;
};

BattleState assembleBattle(const BattleStart& start, game::Party party, game::BattleSwitches switches,
                           const TextCorpus& text, const MonsterSprites& sprites, ActionExecutor& executor,
                           const StageCarry* carry) {
    BattleState state;
    state.version = start.version;
    state.config = start.config;
    state.party = std::move(party);
    state.switches = switches;
    state.background = start.background;
    state.keepFieldMusic = start.keepFieldMusic;
    state.onTheVeldt = start.onTheVeldt;
    state.gauRecruited = start.gauRecruited;
    state.shadowAvailable = start.shadowAvailable;
    state.monstersStanding = BattlerSet::allMonsters();
    if (start.colosseum) {
        state.colosseumWager = start.colosseum->wager;
        state.colosseumPrize = start.colosseum->prize;
        state.colosseumOpponent = start.colosseum->opponent;
    }

    if (carry != nullptr) {
        state.finalBattleChain = true;
        state.finalBattleScroll = carry->scroll;
        for (std::size_t i = 0; i < state.battlers.size(); ++i) {
            state.battlers[i].status = carry->status[i];
            state.battlers[i].reflectTimer = carry->reflectTimer[i];
            state.battlers[i].freezeTimer = carry->freezeTimer[i];
        }
        state.mimic = carry->mimic;
        state.awayFromTheField = carry->awayFromTheField;
        state.espersSummoned = carry->espersSummoned;
        state.morphedBattler = carry->morphedBattler;
        state.finalBattleReserve = carry->finalBattleReserve;
    } else {
        state.finalBattleChain = start.formation.raw == FormationRef::of(kFinalBattleFormations[0]).raw;
        state.finalBattleReserve.assign(start.finalBattleOrder.begin(), start.finalBattleOrder.end());
        state.mimic.last.command = BattleCommandId::MIMIC;
    }

    state.rng = BattleRng(static_cast<std::uint8_t>(start.playTimeFrames * 4));
    state.formation = chooseFormation(state, start.formation);
    state.startingFormation = carry != nullptr ? carry->startingFormation : state.formation;
    const FormationAux& aux = getFormationAux(state.formation);

    state.gauMayAppear = start.gauMayAppear;
    const BattlerSet fromSetup =
        placeParty(state, lineupFor(state, start, aux, carry != nullptr ? carry->standing : FinalBattleStage{}));

    state.switches.clear(game::kClearedAtBattleStartSwitch);
    state.switches.clear(game::kPartyDefeatedSwitch);
    state.switches.clear(game::kRanOutOfTimeSwitch);
    state.switches.clear(game::kEatenByZoneEaterSwitch);
    state.magicOnly = state.switches.isSet(game::kMagicOnlySwitch);
    state.permanentMorph = state.switches.isSet(game::kPermanentMorphSwitch);
    state.morphAvailable = state.switches.isSet(game::kMorphAvailableSwitch);
    state.gauObtained = state.switches.isSet(game::kGauObtainedSwitch);
    state.shadowStays = state.switches.isSet(game::kShadowStaysSwitch);

    forEachSlotLastFirst([&](BattlerSlot slot) {
        Battler& battler = state.at(slot);
        battler.damageOverTimePhase = state.rng.next();
        battler.pendingUpdates = PendingUpdates{.enabledMagic = true,
                                                .startCondemned = true,
                                                .stopCondemned = true,
                                                .commandList = true,
                                                .speed = true,
                                                .morph = state.morphedBattler == slot};
    });
    loadParty(state, fromSetup, state.party.playTimeAtLastSave);
    loadBattleInventory(state);
    loadKnownSkills(state);
    loadMonsters(state, executor, text);

    executor.applyStatusChanges(state);
    checkTheFallen(state, executor);
    state.lastAttackLevel = 20;
    settleAfterAction(state, executor);
    refreshAliveSets(state);
    state.battleType = chooseBattleType(state, aux.possibleBattleTypes());
    forEachSlotLastFirst([&](BattlerSlot slot) { refreshStatusImmunity(state, slot); });
    applyBattleType(state, aux);
    fillStartingGauges(state);
    if (const auto message = openingMessage(state, aux)) {
        addImmediateAction(state, BattleMessageAction{.message = *message});
    }
    // LEGACY VERBATIM (docs/Bugs.md): the run check comes before the battle is laid out, so it
    // reads every monster as standing on both sides of a pincer.
    state.pincerSides = {BattlerSet::allMonsters().monsters, BattlerSet::allMonsters().monsters};
    refreshRunDifficulty(state);

    // A fighter still in the air from the last stage comes down as the stage opens.
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        if (!state.awayFromTheField.has(slot)) {
            continue;
        }
        Battler& battler = state.at(slot);
        battler.atbStopped = true;
        battler.actionQueued = true;
        battler.gauge.setMeter(0);
        state.actionQueue.push(slot);
        addAction(state, slot, CommandAction{.command = BattleCommandId::JUMP});
    }

    layOutMonsters(state, sprites);
    layOutParty(state);
    if (carry != nullptr) {
        state.facingRight = state.facingRight.monstersOnly() | carry->partyFacingRight;
        state.animationTargets = carry->animationTargets;
    }

    if (state.finalBattleScroll) {
        state.requests.push_back(RunBattleScript{
            .commands = {FormationChanges::from(*state.finalBattleScroll, state.alive.monstersOnly())}});
        state.waitingForScript = ScriptWait{.resume = ScriptResume::NextStep};
    } else {
        state.requests.push_back(OpenBattleScene{});
        state.rng.advance();
    }
    return state;
}

}  // namespace

BattleState startBattle(const BattleStart& start, game::Party party, game::BattleSwitches switches,
                        const TextCorpus& text, const MonsterSprites& sprites, ActionExecutor& executor) {
    return assembleBattle(start, std::move(party), switches, text, sprites, executor, nullptr);
}

BattleResult writeBack(const BattleState& state) {
    BattleResult result{.outcome = state.outcome.value_or(BattleOutcome{BattleWon{}}),
                        .party = state.party,
                        .switches = state.switches};
    game::Party& party = result.party;

    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        const Battler& battler = state.at(slot);
        if (!battler.actor || !battler.record) {
            continue;
        }
        const bool ghost = *battler.actor == CharacterPropId::GHOST_1 || *battler.actor == CharacterPropId::GHOST_2;
        if ((ghost && battler.status.hasAny(kFallen)) || state.possessed.has(slot)) {
            party.membership[*battler.record].party = 0;
        }
        game::CharacterRecord& record = party.characters[*battler.record];
        record.status = unionOf(intersectionOf(battler.status, kFirstStatusByte),
                                intersectionOf(battler.status, kRecordedFromFourthByte));
        record.hp = battler.hp;
        if (battler.maxMp != 0) {
            record.mp = battler.mp;
        }
    }

    for (std::size_t slot = 0; slot < party.inventory.size(); ++slot) {
        const BattleItem& item = state.inventory.items[slot];
        party.inventory[slot] =
            game::InventorySlot{.item = item.item,
                                .quantity = item.item == ItemId::EMPTY ? std::uint8_t{0} : item.quantity};
    }

    // The wager is given up whatever the result, unless the match ran out of time.
    const auto* ended = std::get_if<BattleEnded>(&result.outcome);
    const bool ranOutOfTime = ended != nullptr && ended->reason == BattleEndReason::RanOutOfTime;
    if (state.colosseum && state.colosseumWager && *state.colosseumWager != ItemId::EMPTY && !ranOutOfTime) {
        for (game::InventorySlot& held : party.inventory) {
            if (held.item != *state.colosseumWager) {
                continue;
            }
            if (--held.quantity == 0 || held.quantity >= 0x80) {
                held = game::InventorySlot{};
            }
        }
    }

    if (state.doomGaze) {
        result.switches.setDoomGazeHp(state.at(*state.doomGaze).hp);
    }

    if (std::holds_alternative<BattleLost>(result.outcome)) {
        result.switches.set(game::kPartyDefeatedSwitch);
    }
    if (ended != nullptr) {
        switch (ended->reason) {
            case BattleEndReason::RanOutOfTime:
                result.switches.set(game::kRanOutOfTimeSwitch);
                break;
            case BattleEndReason::TimerExpired:
                result.switches.set(game::kTimerExpiredSwitch);
                break;
            case BattleEndReason::EatenByZoneEater:
                result.switches.set(game::kEatenByZoneEaterSwitch);
                break;
            default:
                break;
        }
    }

    if (state.shadowLeftParty) {
        result.shadowLeftParty = true;
        for (std::size_t index = 0; index < party.characters.size(); ++index) {
            if (party.characters[index].actor == CharacterPropId::SHADOW) {
                party.membership[index].party = 0;
                party.characters[index].esper.reset();
            }
        }
    }

    // A formation with a monster below 256 joins the ones Gau can find on the Veldt, unless the
    // formation is past the first 512 or is kept off the Veldt.
    const FormationAux& aux = getFormationAux(state.formation);
    if (!aux.veldtDisabled()) {
        for (int position = kMonsterSlots - 1; position >= 0; --position) {
            const auto& monster = state.at(BattlerSlot::monster(position)).monster;
            if (!monster || static_cast<int>(*monster) >= 256) {
                continue;
            }
            if (static_cast<int>(state.startingFormation) < 512) {
                result.veldtFormation = state.startingFormation;
            }
            break;
        }
    }
    return result;
}

BattleState startNextFinalBattleStage(const BattleState& finished, const BattleResult& handedBack,
                                      const BattleStart& start, const TextCorpus& text,
                                      const MonsterSprites& sprites, ActionExecutor& executor) {
    const auto* ended = std::get_if<BattleEnded>(&*finished.outcome);
    const FormationId next = *ended->nextFormation;

    StageCarry carry;
    carry.mimic = finished.mimic;
    carry.awayFromTheField = finished.awayFromTheField;
    carry.espersSummoned = finished.espersSummoned;
    carry.morphedBattler = finished.morphedBattler;
    carry.startingFormation = finished.startingFormation;
    carry.finalBattleReserve = finished.finalBattleReserve;
    carry.partyFacingRight = finished.facingRight.charactersOnly();
    carry.animationTargets = finished.animationTargets;
    for (std::size_t stage = 0; stage + 1 < kFinalBattleFormations.size(); ++stage) {
        if (kFinalBattleFormations[stage + 1] == next) {
            carry.scroll = kFinalBattleScroll[2 * stage].scroll;
        }
    }

    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        const Battler& battler = finished.at(slot);
        const auto i = static_cast<std::size_t>(index);
        carry.reflectTimer[i] = battler.reflectTimer;
        carry.freezeTimer[i] = battler.freezeTimer;
        if (slot.isCharacter() && battler.present && !battler.status.hasAny(kFallen) && !battler.airAnchored) {
            carry.status[i] = battler.status.without(kClearedBetweenStages);
            carry.standing.standing[slot.position()] = battler.actor;
            continue;
        }
        if (slot.isCharacter()) {
            carry.awayFromTheField.remove(slot);
            carry.espersSummoned.remove(slot);
        }
        carry.status[i] = StatusSet{};
    }

    BattleStart nextStage = start;
    nextStage.formation = FormationRef::of(next);
    return assembleBattle(nextStage, handedBack.party, handedBack.switches, text, sprites, executor, &carry);
}

}  // namespace ostinato::battle
