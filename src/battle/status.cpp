#include "battle/status.h"

#include <array>
#include <cstddef>

#include "ostinato/element_set.h"

namespace ostinato::battle {

namespace {

constexpr int kStatusCount = 32;
constexpr int kStatusesPerWord = 16;

// The two halves the statuses take effect in, statuses 1-2 then statuses 3-4: each half's set
// handlers run before the next half's, and each half's clears are masked as its clear handlers start.
constexpr std::array<StatusSet, 2> kStatusWords{{
    StatusSet{{0xFF, 0xFF, 0x00, 0x00}},
    StatusSet{{0x00, 0x00, 0xFF, 0xFF}},
}};

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

// One battler's statuses while they take effect: what it has, and what is to go on and come off.
struct StatusWork {
    StatusSet current;
    StatusSet toSet;
    StatusSet toClear;
};

// What a status does to the battler and the battle as it goes on, or as it comes off.
using StatusHandler = void (*)(BattleState&, Battler&, BattlerSlot, StatusWork&);

// --- what the handlers share ----------------------------------------------------------------------

// The statuses of `ruledOut` the battler has or is to be given come off.
void takeOff(StatusWork& work, const StatusSet& ruledOut) {
    work.toClear = unionOf(work.toClear, intersectionOf(unionOf(work.current, work.toSet), ruledOut));
}

// A monster is back among those standing on the field and no longer among the fallen. A party
// member is not counted in either.
void backAmongTheStanding(BattleState& state, BattlerSlot slot) {
    if (slot.isMonster()) {
        state.monstersStanding.add(slot);
        state.monstersFallen.remove(slot);
    }
}

void diedThisAction(BattleState& state, BattlerSlot slot) {
    state.diedAwaitingCounter.add(slot);
}

// A party member's next command is aimed again. Only party members are marked.
void statusesChanged(BattleState& state, BattlerSlot slot) {
    if (slot.isCharacter()) {
        state.statusChanged.add(slot);
    }
}

// Its menu does not open, and every action it had waiting is dropped.
void menuClosed(Battler& battler) {
    battler.menuMayOpen = false;
    battler.pendingUpdates.removeAllActions = true;
}

// --- the handlers ---------------------------------------------------------------------------------

void noEffect(BattleState&, Battler&, BattlerSlot, StatusWork&) {}

void zombieGoesOn(BattleState& state, Battler&, BattlerSlot slot, StatusWork& work) {
    takeOff(work, StatusSet::of(StatusId::DEAD));
    diedThisAction(state, slot);
    statusesChanged(state, slot);
}

void zombieComesOff(BattleState& state, Battler&, BattlerSlot slot, StatusWork&) {
    backAmongTheStanding(state, slot);
    statusesChanged(state, slot);
}

void confuseGoesOnOrComesOff(BattleState& state, Battler&, BattlerSlot slot, StatusWork&) {
    statusesChanged(state, slot);
}

// A party member's equipment is worked out again.
void rageComesOff(BattleState&, Battler& battler, BattlerSlot slot, StatusWork&) {
    if (slot.isCharacter()) {
        battler.equipmentChanged = true;
    }
}

// Its castable spells and its commands change under Imp, and a party member's equipment is worked
// out again as Rage's coming off does.
void impGoesOnOrComesOff(BattleState& state, Battler& battler, BattlerSlot slot, StatusWork& work) {
    battler.pendingUpdates.enabledMagic = true;
    battler.pendingUpdates.commandList = true;
    rageComesOff(state, battler, slot, work);
}

void petrifyGoesOn(BattleState& state, Battler& battler, BattlerSlot slot, StatusWork& work) {
    diedThisAction(state, slot);
    takeOff(work, kStatusesPetrifyTakesOff);
    battler.triggers.phantasm = false;
    menuClosed(battler);
}

// Dead takes Petrify and Condemned off as well, and a Dead to set wins over a Dead to clear; then it
// does everything Petrify's going on does.
void deadGoesOn(BattleState& state, Battler& battler, BattlerSlot slot, StatusWork& work) {
    takeOff(work, kStatusesDeadTakesOff);
    work.toClear.clear(StatusId::DEAD);
    petrifyGoesOn(state, battler, slot, work);
}

void petrifyOrDeadComesOff(BattleState& state, Battler& battler, BattlerSlot slot, StatusWork&) {
    backAmongTheStanding(state, slot);
    battler.emptyGaugeAfterAction = true;
    battler.pendingUpdates.removeAllActions = true;
}

void condemnedGoesOn(BattleState&, Battler& battler, BattlerSlot, StatusWork&) {
    battler.pendingUpdates.startCondemned = true;
}

void condemnedComesOff(BattleState&, Battler& battler, BattlerSlot, StatusWork&) {
    battler.pendingUpdates.stopCondemned = true;
}

void silenceGoesOnOrComesOff(BattleState&, Battler& battler, BattlerSlot, StatusWork&) {
    battler.pendingUpdates.commandList = true;
}

void sapGoesOn(BattleState&, Battler&, BattlerSlot, StatusWork& work) {
    work.toClear.set(StatusId::REGEN);
}

void sleepGoesOn(BattleState&, Battler& battler, BattlerSlot, StatusWork&) {
    battler.sleepTimer = kSleepDuration;
    menuClosed(battler);
}

void sleepComesOff(BattleState&, Battler& battler, BattlerSlot, StatusWork&) {
    battler.emptyGaugeAfterAction = true;
}

void regenGoesOn(BattleState&, Battler&, BattlerSlot, StatusWork& work) {
    work.toClear.set(StatusId::SAP);
}

void speedChanges(BattleState&, Battler& battler, BattlerSlot, StatusWork&) {
    battler.pendingUpdates.speed = true;
}

void slowGoesOn(BattleState& state, Battler& battler, BattlerSlot slot, StatusWork& work) {
    work.toClear.set(StatusId::HASTE);
    speedChanges(state, battler, slot, work);
}

void hasteGoesOn(BattleState& state, Battler& battler, BattlerSlot slot, StatusWork& work) {
    work.toClear.set(StatusId::SLOW);
    speedChanges(state, battler, slot, work);
}

void stopGoesOn(BattleState&, Battler& battler, BattlerSlot, StatusWork&) {
    battler.stopTimer = kStopDuration;
}

void reflectGoesOn(BattleState&, Battler& battler, BattlerSlot, StatusWork&) {
    battler.reflectTimer = kReflectDuration;
}

void frozenGoesOn(BattleState&, Battler& battler, BattlerSlot, StatusWork&) {
    battler.freezeTimer = kFreezeDuration;
}

void morphGoesOnOrComesOff(BattleState&, Battler& battler, BattlerSlot, StatusWork&) {
    battler.pendingUpdates.morph = true;
}

// --- the two tables, one handler per status -------------------------------------------------------

constexpr std::size_t entry(StatusId id) {
    return static_cast<std::size_t>(id);
}

constexpr std::array<StatusHandler, kStatusCount> kGoingOn = [] {
    std::array<StatusHandler, kStatusCount> table{};
    table.fill(noEffect);
    table[entry(StatusId::ZOMBIE)] = zombieGoesOn;
    table[entry(StatusId::INVISIBLE)] = noEffect;  // shown as vanished: the presentation reads the status
    table[entry(StatusId::IMP)] = impGoesOnOrComesOff;
    table[entry(StatusId::PETRIFY)] = petrifyGoesOn;
    table[entry(StatusId::DEAD)] = deadGoesOn;
    table[entry(StatusId::CONDEMNED)] = condemnedGoesOn;
    table[entry(StatusId::SILENCE)] = silenceGoesOnOrComesOff;
    table[entry(StatusId::CONFUSE)] = confuseGoesOnOrComesOff;
    table[entry(StatusId::SAP)] = sapGoesOn;
    table[entry(StatusId::SLEEP)] = sleepGoesOn;
    table[entry(StatusId::REGEN)] = regenGoesOn;
    table[entry(StatusId::SLOW)] = slowGoesOn;
    table[entry(StatusId::HASTE)] = hasteGoesOn;
    table[entry(StatusId::STOP)] = stopGoesOn;
    table[entry(StatusId::REFLECT)] = reflectGoesOn;
    table[entry(StatusId::FROZEN)] = frozenGoesOn;
    table[entry(StatusId::MORPH)] = morphGoesOnOrComesOff;
    return table;
}();

constexpr std::array<StatusHandler, kStatusCount> kComingOff = [] {
    std::array<StatusHandler, kStatusCount> table{};
    table.fill(noEffect);
    table[entry(StatusId::ZOMBIE)] = zombieComesOff;
    table[entry(StatusId::INVISIBLE)] = noEffect;  // shown as vanished: the presentation reads the status
    table[entry(StatusId::IMP)] = impGoesOnOrComesOff;
    table[entry(StatusId::PETRIFY)] = petrifyOrDeadComesOff;
    table[entry(StatusId::DEAD)] = petrifyOrDeadComesOff;
    table[entry(StatusId::CONDEMNED)] = condemnedComesOff;
    table[entry(StatusId::SILENCE)] = silenceGoesOnOrComesOff;
    table[entry(StatusId::CONFUSE)] = confuseGoesOnOrComesOff;
    table[entry(StatusId::SLEEP)] = sleepComesOff;
    table[entry(StatusId::SLOW)] = speedChanges;
    table[entry(StatusId::HASTE)] = speedChanges;
    table[entry(StatusId::RAGE)] = rageComesOff;
    table[entry(StatusId::MORPH)] = morphGoesOnOrComesOff;
    return table;
}();

// Each status of `which` in `word` runs its handler, highest first.
void runHandlers(const std::array<StatusHandler, kStatusCount>& table, std::size_t word, const StatusSet& which,
                 BattleState& state, Battler& battler, BattlerSlot slot, StatusWork& work) {
    const int first = static_cast<int>(word) * kStatusesPerWord;
    for (int status = first + kStatusesPerWord - 1; status >= first; --status) {
        const auto id = static_cast<StatusId>(status);
        if (which.has(id)) {
            table[entry(id)](state, battler, slot, work);
        }
    }
}

// What is to go on and come off the battler, before any handler runs.
StatusWork startingWork(const Battler& battler) {
    StatusWork work{
        .current = battler.status,
        .toSet = intersectionOf(battler.statusesToSet, battler.inflictableStatuses),
        .toClear = battler.statusesToClear,
    };
    if (work.current.has(StatusId::PETRIFY)) {
        work.toSet.set(StatusId::PETRIFY);
    }
    const bool hpLow = battler.maxHp / 8 >= battler.hp;
    if (work.current.has(StatusId::NEAR_FATAL)) {
        if (!hpLow) {
            work.toClear.set(StatusId::NEAR_FATAL);
        }
    } else if (hpLow) {
        work.toSet.set(StatusId::NEAR_FATAL);
    }
    if (work.toSet.has(StatusId::DEAD) && battler.triggers.overcast) {
        work.toSet.set(StatusId::ZOMBIE);
        work.toSet.clear(StatusId::DEAD);
        work.toClear.set(StatusId::CONDEMNED);
    }
    return work;
}

void takeEffect(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    StatusWork work = startingWork(battler);
    if (battler.retaliationPending) {
        battler.statusForCounter = calcStatus(work.current, work.toSet, work.toClear);
    }
    for (std::size_t word = 0; word < kStatusWords.size(); ++word) {
        const StatusSet goingOn = intersectionOf(work.toSet, kStatusWords[word]);
        runHandlers(kGoingOn, word, goingOn, state, battler, slot, work);
    }
    for (std::size_t word = 0; word < kStatusWords.size(); ++word) {
        const StatusSet comingOff =
            intersectionOf(intersectionOf(work.toClear, kStatusWords[word]), battler.inflictableStatuses);
        work.toClear = unionOf(work.toClear.without(kStatusWords[word]), comingOff);
        runHandlers(kComingOff, word, comingOff, state, battler, slot, work);
    }
    StatusSet result = calcStatus(work.current, work.toSet, work.toClear);
    if (result.has(StatusId::ZOMBIE)) {
        result = result.without(kStatusesAZombieCannotHave);
    }
    battler.status = result;
    battler.statusesToSet = {};
    battler.statusesToClear = {};
}

}  // namespace

StatusChange statusModification(const StatusSet& current, const StatusSet& attackStatuses, AttackFlags2 flags2,
                                bool physical, bool fire) {
    const bool removes = flags2.has(AttackFlag2::REMOVE_STATUS);
    const bool toggles = flags2.has(AttackFlag2::TOGGLE_STATUS);
    StatusChange change;
    if (!removes || toggles) {
        change.toSet = attackStatuses.without(current);
    }
    if (removes || toggles) {
        change.toClear = intersectionOf(attackStatuses, current);
    }
    if (!physical && current.has(StatusId::INVISIBLE) && !attackStatuses.has(StatusId::INVISIBLE)) {
        change.toClear.set(StatusId::INVISIBLE);
    }
    if (fire && current.has(StatusId::FROZEN) && !attackStatuses.has(StatusId::FROZEN)) {
        change.toClear.set(StatusId::FROZEN);
    }
    return change;
}

StatusSet calcStatus(const StatusSet& current, const StatusSet& toSet, const StatusSet& toClear) {
    return unionOf(current, toSet).without(toClear);
}

void statusEffect(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target) {
    Battler& battler = state.at(target);
    const AttackProperties& record = attack.record;
    StatusChange change = statusModification(battler.status, record.statuses, record.flags2,
                                             record.traits.has(AttackTrait::PHYSICAL),
                                             record.element.has(Element::FIRE));
    if (!attack.vanishApplies) {
        change.toClear.clear(StatusId::INVISIBLE);
    }
    if (battler.undead && record.traits.has(AttackTrait::INVERT_ON_UNDEAD) &&
        record.flags2.has(AttackFlag2::REMOVE_STATUS) &&
        record.statuses.hasAny(StatusSet::of(StatusId::DEAD, StatusId::ZOMBIE))) {
        change.toSet.set(StatusId::DEAD);
    }
    battler.statusesToSet = unionOf(battler.statusesToSet, change.toSet);
    battler.statusesToClear = unionOf(battler.statusesToClear, change.toClear);
    if (record.misc.has(AttackMiscFlag::MISS_IF_STATUS_IMMUNE) &&
        !unionOf(change.toSet, change.toClear).hasAny(battler.inflictableStatuses)) {
        attack.scratch.missedByEffect.add(target);
        attack.scratch.missed.add(target);
    }
}

void applyStatusChanges(BattleState& state) {
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        if (state.at(slot).present) {
            takeEffect(state, slot);
        }
    }
}

}  // namespace ostinato::battle
