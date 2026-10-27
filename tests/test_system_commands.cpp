// The actions the battle takes on its own: damage over time, a spell with no caster, status
// timers running out, the party's escape, Seize's drain, a formation change, monsters leaving and
// coming back, Scan's report, and the actions that only show something.
// Behaviors and their sources: docs/contracts/battle_commands.md § The battle's own actions.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "battle/battle_core.h"
#include "battle/battle_inventory.h"
#include "battle/battle_start.h"
#include "battle/command_executor.h"
#include "battle/system_commands.h"
#include "battle_test_support.h"
#include "data/formations.h"
#include "data/monster_properties.h"
#include "ostinato/attack_flags.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using battle::AttackAnimation;
using battle::AttackInFlight;
using battle::Battler;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleScriptBuilder;
using battle::BattleScriptCommand;
using battle::BattleState;
using battle::DamageOverTimeKind;
using battle::MonsterVisibilityMode;

const BattlerSlot kTerra = BattlerSlot::character(0);
const BattlerSlot kLocke = BattlerSlot::character(1);
const BattlerSlot kMonster = BattlerSlot::monster(0);
const BattlerSlot kSecondMonster = BattlerSlot::monster(1);

// Hooks that keep a copy of every attack as its damage is about to be worked out.
struct AttackLog : battle::AttackHooks {
    std::vector<AttackInFlight> made;
    void baseDamage(BattleState&, AttackInFlight& attack, BattlerSlot) override { made.push_back(attack); }
};

// An executor that notes whose links it released.
struct LinkLog : battle::ActionExecutor {
    std::vector<BattlerSlot> released;
    void releaseLinks(BattleState&, BattlerSlot slot) override { released.push_back(slot); }
};

// A draw that RandA(n) turns into `k`.
std::function<bool(std::uint8_t)> drawsTo(int n, int k) {
    return [n, k](std::uint8_t r) { return ((r * n) >> 8) == k; };
}

template <typename T>
std::vector<T> allOf(const std::vector<BattleScriptCommand>& script) {
    std::vector<T> found;
    for (const BattleScriptCommand& command : script) {
        if (const auto* value = std::get_if<T>(&command)) {
            found.push_back(*value);
        }
    }
    return found;
}

BattleState poisonedBattle() {
    BattleState state = makeAimableBattle();
    Battler& terra = state.at(kTerra);
    terra.maxHp = 1000;
    terra.stamina = 30;
    terra.dotActionPending = true;
    return state;
}

AttackInFlight dot(BattleState& state, DamageOverTimeKind kind, std::vector<BattleScriptCommand>* shown = nullptr) {
    AttackLog log;
    AttackInFlight attack;
    BattleScriptBuilder script;
    battle::damageOverTime(state, log, script, attack, kTerra, kind);
    if (shown != nullptr) {
        *shown = script.take();
    }
    return log.made.empty() ? AttackInFlight{} : log.made.front();
}

// --- damage over time ---------------------------------------------------------------------------

TEST(DamageOverTime, PoisonIsAnAttackOnItselfOfMaxHpTimesStaminaOver1024PlusTwo) {
    // Cmd_22 :13153-13198.
    BattleState state = poisonedBattle();
    std::vector<BattleScriptCommand> shown;
    const AttackInFlight attack = dot(state, DamageOverTimeKind::Poison, &shown);
    EXPECT_EQ(attack.record.power, (1000 * 30 >> 10) + 2);
    EXPECT_EQ(attack.targets, BattlerSet::of(kTerra));
    EXPECT_EQ(attack.record.traits.bits, AttackTraitSet::of(AttackTrait::INVERT_ON_UNDEAD, AttackTrait::IGNORE_DEFENSE,
                                                            AttackTrait::NO_DAMAGE_SPLIT)
                                             .bits);
    EXPECT_EQ(attack.record.element.bits, ElementSet::of(Element::POISON).bits);
    EXPECT_FALSE(attack.record.flags2.has(AttackFlag2::RESTORE_HP_MP));
    EXPECT_TRUE(attack.record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_FALSE(attack.vanishApplies);
    EXPECT_FALSE(attack.ignoreDefenseStopsMultiplier);
    EXPECT_EQ(attack.attackerLevel, 0);
    EXPECT_FALSE(state.at(kTerra).dotActionPending);
    const auto shownAnimations = allOf<AttackAnimation>(shown);
    ASSERT_EQ(shownAnimations.size(), 1u);
    EXPECT_EQ(std::get<BattleCommandId>(shownAnimations[0].what), battle::kNothingShown);
}

TEST(DamageOverTime, PoisonsMultiplierGrowsByTwoEachBeatToFourteen) {
    BattleState state = poisonedBattle();
    EXPECT_EQ(dot(state, DamageOverTimeKind::Poison).extraMultiplier, 0);
    EXPECT_EQ(state.at(kTerra).poisonMultiplier, 2);
    EXPECT_EQ(dot(state, DamageOverTimeKind::Poison).extraMultiplier, 2);
    EXPECT_EQ(state.at(kTerra).poisonMultiplier, 4);
    state.at(kTerra).poisonMultiplier = 12;
    EXPECT_EQ(dot(state, DamageOverTimeKind::Poison).extraMultiplier, 12);
    EXPECT_EQ(state.at(kTerra).poisonMultiplier, 14);
    EXPECT_EQ(dot(state, DamageOverTimeKind::Poison).extraMultiplier, 14);
    EXPECT_EQ(state.at(kTerra).poisonMultiplier, 14);
}

TEST(DamageOverTime, RegenRestoresAndSapOnlyHurts) {
    // The status byte's bit 1 (Regen) becomes the restore flag; bit 2 (Poison) the multiplier.
    BattleState state = poisonedBattle();
    const AttackInFlight regen = dot(state, DamageOverTimeKind::Regen);
    EXPECT_TRUE(regen.record.flags2.has(AttackFlag2::RESTORE_HP_MP));
    EXPECT_EQ(regen.record.element.bits, 0);
    EXPECT_EQ(regen.extraMultiplier, 0);
    const AttackInFlight sap = dot(state, DamageOverTimeKind::Sap);
    EXPECT_FALSE(sap.record.flags2.has(AttackFlag2::RESTORE_HP_MP));
    EXPECT_EQ(sap.record.element.bits, 0);
    EXPECT_EQ(state.at(kTerra).poisonMultiplier, 0);
}

TEST(DamageOverTime, PowerStopsAt255) {
    BattleState state = poisonedBattle();
    struct Case {
        std::uint16_t maxHp;
        std::uint8_t stamina;
        int power;
    };
    for (const Case c : {Case{9999, 99, 255}, Case{1024, 253, 255}, Case{1024, 252, 254}, Case{100, 1, 2}}) {
        state.at(kTerra).maxHp = c.maxHp;
        state.at(kTerra).stamina = c.stamina;
        EXPECT_EQ(dot(state, DamageOverTimeKind::Sap).record.power, c.power) << c.maxHp << " " << int(c.stamina);
    }
}

TEST(DamageOverTime, NothingWithNoEnemyLeftAndTheBeatStaysPending) {
    BattleState state = poisonedBattle();
    state.enemyCount = 0;
    std::vector<BattleScriptCommand> shown;
    dot(state, DamageOverTimeKind::Poison, &shown);
    EXPECT_TRUE(shown.empty());
    EXPECT_TRUE(state.at(kTerra).dotActionPending);
    EXPECT_EQ(state.at(kTerra).poisonMultiplier, 0);
}

TEST(DamageOverTime, NothingForABattlerHeldByAStatusButTheBeatIsSpent) {
    BattleState state = poisonedBattle();
    state.at(kTerra).heldByStatus = true;
    std::vector<BattleScriptCommand> shown;
    dot(state, DamageOverTimeKind::Poison, &shown);
    EXPECT_TRUE(shown.empty());
    EXPECT_FALSE(state.at(kTerra).dotActionPending);
}

// --- a spell with no caster ---------------------------------------------------------------------

TEST(SpellWithoutCaster, DoomIsCastByTheCondemnedOnItselfFreeUnreflectedAndUndodgeable) {
    // Cmd_26 :13030-13055.
    BattleState state = makeAimableBattle();
    state.at(kTerra).magicPower = 33;
    state.at(kTerra).level = 21;
    AttackLog log;
    AttackInFlight attack;
    BattleScriptBuilder script;
    battle::spellWithoutCaster(state, log, script, attack, AttackId::DOOM, kTerra);
    EXPECT_TRUE(state.at(kTerra).pendingUpdates.stopCondemned);
    ASSERT_EQ(log.made.size(), 1u);
    const AttackInFlight& made = log.made[0];
    EXPECT_EQ(made.attack, AttackId::DOOM);
    EXPECT_EQ(made.command, BattleCommandId::MAGIC);
    EXPECT_EQ(made.targets, BattlerSet::of(kTerra));
    EXPECT_EQ(made.record.flags1.bits, AttackFlags1::of(AttackFlag1::IGNORE_REFLECT).bits);
    EXPECT_TRUE(made.record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_EQ(made.record.mpCost, 0);
    EXPECT_EQ(made.attackerStat, 33);
    EXPECT_EQ(made.attackerLevel, 21);
    const auto shown = allOf<AttackAnimation>(script.take());
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_TRUE(shown[0].params.castingSwirl);
}

TEST(SpellWithoutCaster, DoomWithNoEnemyLeftOnlyStopsTheCount) {
    BattleState state = makeAimableBattle();
    state.enemyCount = 0;
    AttackLog log;
    AttackInFlight attack;
    BattleScriptBuilder script;
    battle::spellWithoutCaster(state, log, script, attack, AttackId::DOOM, kTerra);
    EXPECT_TRUE(state.at(kTerra).pendingUpdates.stopCondemned);
    EXPECT_TRUE(log.made.empty());
    EXPECT_TRUE(script.take().empty());
}

TEST(SpellWithoutCaster, AnyOtherSpellIsCastWhetherOrNotEnemiesRemain) {
    BattleState state = makeAimableBattle();
    state.enemyCount = 0;
    AttackLog log;
    AttackInFlight attack;
    BattleScriptBuilder script;
    battle::spellWithoutCaster(state, log, script, attack, AttackId::LIFE, kTerra);
    EXPECT_FALSE(state.at(kTerra).pendingUpdates.stopCondemned);
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].attack, AttackId::LIFE);
}

// --- status timers --------------------------------------------------------------------------------

AttackInFlight timersRunOut(BattleState& state, battle::StatusTimersExpired expired) {
    AttackLog log;
    AttackInFlight attack;
    BattleScriptBuilder script;
    battle::statusTimersExpire(state, log, script, attack, expired);
    return log.made.at(0);
}

TEST(StatusTimers, EachTimerThatRanOutTakesItsStatusOff) {
    // Cmd_29 :13335-13367.
    BattleState state = makeAimableBattle();
    const AttackInFlight attack = timersRunOut(
        state, {.battler = kTerra, .stop = true, .reflect = true, .freeze = true, .sleep = false});
    EXPECT_TRUE(attack.record.statuses.has(StatusId::STOP));
    EXPECT_TRUE(attack.record.statuses.has(StatusId::REFLECT));
    EXPECT_TRUE(attack.record.statuses.has(StatusId::FROZEN));
    EXPECT_FALSE(attack.record.statuses.has(StatusId::SLEEP));
    EXPECT_TRUE(attack.record.flags2.has(AttackFlag2::REMOVE_STATUS));
    EXPECT_EQ(attack.command, battle::kNothingShown);
    EXPECT_EQ(attack.targets, BattlerSet::of(kTerra));
}

TEST(StatusTimers, ASleeperWakesWithTapir) {
    BattleState state = makeAimableBattle();
    state.at(kTerra).status.set(StatusId::SLEEP);
    const AttackInFlight attack = timersRunOut(state, {.battler = kTerra, .sleep = true});
    EXPECT_TRUE(attack.record.statuses.has(StatusId::SLEEP));
    EXPECT_EQ(attack.command, BattleCommandId::MAGIC);
    EXPECT_EQ(attack.attack, AttackId::TAPIR);
}

TEST(StatusTimers, SleepThatRanOutOnAnAwakeBattlerTakesNothingOff) {
    BattleState state = makeAimableBattle();
    const AttackInFlight attack = timersRunOut(state, {.battler = kTerra, .sleep = true});
    EXPECT_FALSE(attack.record.statuses.has(StatusId::SLEEP));
    EXPECT_EQ(attack.command, battle::kNothingShown);
}

// --- the party's escape ---------------------------------------------------------------------------

BattleState escapingBattle() {
    BattleState state = makeAimableBattle();
    state.partyRunning = true;
    for (BattlerSlot slot : {kTerra, kLocke}) {
        state.at(slot).gauge.setMeter(0);
        state.partyJustEscaped.add(slot);
    }
    return state;
}

std::vector<BattleScriptCommand> escape(BattleState& state, LinkLog& links) {
    BattleScriptBuilder script;
    battle::partyEscapes(state, links, script);
    return script.take();
}

TEST(PartyEscape, TheCharactersWhoGotAwayRunOffTogether) {
    // Cmd_2a :5637-5696.
    BattleState state = escapingBattle();
    LinkLog links;
    const auto shown = escape(state, links);
    const BattlerSet both = BattlerSet::of(kTerra) | BattlerSet::of(kLocke);
    EXPECT_EQ(state.partyLeft, both);
    EXPECT_EQ(state.leavingAfterAction, both);
    EXPECT_TRUE(state.partyJustEscaped.empty());
    for (BattlerSlot slot : {kTerra, kLocke}) {
        EXPECT_TRUE(state.at(slot).emptyGaugeAfterAction);
        EXPECT_TRUE(state.at(slot).pendingUpdates.removeAllActions);
    }
    EXPECT_EQ(links.released, (std::vector<BattlerSlot>{kLocke, kTerra}));
    const auto animations = allOf<AttackAnimation>(shown);
    ASSERT_EQ(animations.size(), 1u);
    EXPECT_EQ(std::get<battle::SpecialAnimation>(animations[0].what), battle::SpecialAnimation::PartyRunsAway);
    EXPECT_EQ(animations[0].params.targets, both);
    EXPECT_EQ(animations[0].params.attacker, kTerra);
}

TEST(PartyEscape, OnlyACharacterWithAFullGaugeOnTheFieldAndFreeToActLeaves) {
    struct Case {
        const char* name;
        void (*make)(Battler&);
    };
    const Case cases[] = {
        {"gauge not full", [](Battler& b) { b.gauge.setMeter(200); }},
        {"held by a status", [](Battler& b) { b.heldByStatus = true; }},
        {"held by control", [](Battler& b) { b.heldByControl = true; }},
        {"not on the field", [](Battler& b) { b.present = false; }},
        {"a zombie", [](Battler& b) { b.status.set(StatusId::ZOMBIE); }},
        {"hidden", [](Battler& b) { b.status.set(StatusId::HIDE); }},
    };
    for (const Case& c : cases) {
        BattleState state = escapingBattle();
        c.make(state.at(kLocke));
        LinkLog links;
        escape(state, links);
        EXPECT_EQ(state.partyLeft, BattlerSet::of(kTerra)) << c.name;
        EXPECT_FALSE(state.at(kLocke).emptyGaugeAfterAction) << c.name;
    }
}

TEST(PartyEscape, NoOneLeavingShowsNothingButTheEscapesAreSpent) {
    BattleState state = escapingBattle();
    state.at(kTerra).gauge.setMeter(9);
    state.at(kLocke).gauge.setMeter(9);
    LinkLog links;
    EXPECT_TRUE(escape(state, links).empty());
    EXPECT_TRUE(state.partyJustEscaped.empty());
    EXPECT_TRUE(state.partyLeft.empty());
}

TEST(PartyEscape, APartyThatCannotRunIsToldSo) {
    BattleState state = escapingBattle();
    state.cannotRun = true;
    LinkLog links;
    const auto shown = escape(state, links);
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_EQ(std::get<battle::BattleMessage>(shown[0]).message, battle::kMessageCantRunAway);
    EXPECT_TRUE(state.partyLeft.empty());
    EXPECT_FALSE(state.partyJustEscaped.empty());
}

TEST(PartyEscape, NothingWhileThePartyIsNotRunningOrNoOneGotAway) {
    BattleState state = escapingBattle();
    state.partyRunning = false;
    state.cannotRun = true;
    LinkLog links;
    EXPECT_TRUE(escape(state, links).empty());
    state = escapingBattle();
    state.partyJustEscaped = {};
    EXPECT_TRUE(escape(state, links).empty());
}

// --- Seize's drain --------------------------------------------------------------------------------

TEST(SeizeDrain, TheSeizerDrainsItsCaptiveAtLevelSixteenPowerSixteenUncapped) {
    // Cmd_2d :13385-13406.
    BattleState state = makeAimableBattle();
    state.at(kMonster).dotActionPending = true;
    AttackLog log;
    AttackInFlight attack;
    BattleScriptBuilder script;
    battle::seizeDrain(state, log, script, attack, kMonster, kTerra);
    EXPECT_FALSE(state.at(kMonster).dotActionPending);
    ASSERT_EQ(log.made.size(), 1u);
    const AttackInFlight& made = log.made[0];
    EXPECT_EQ(made.attackerLevel, 16);
    EXPECT_EQ(made.attackerStat, 0);
    EXPECT_EQ(made.record.power, 16);
    EXPECT_EQ(made.record.traits.bits, AttackTraitSet::of(AttackTrait::INVERT_ON_UNDEAD, AttackTrait::IGNORE_DEFENSE).bits);
    EXPECT_EQ(made.record.flags1.bits, AttackFlags1::of(AttackFlag1::IGNORE_REFLECT).bits);
    EXPECT_TRUE(made.record.flags2.has(AttackFlag2::DRAIN));
    EXPECT_FALSE(made.drainCapped);
    EXPECT_EQ(made.targets, BattlerSet::of(kTerra));
    EXPECT_EQ(made.command, battle::kNothingShown);
}

// --- monsters leaving and coming back -----------------------------------------------------------

BattleState visibilityBattle() {
    BattleState state = makeAimableBattle();
    state.at(kMonster).monster = MonsterId::GUARD;
    state.at(kSecondMonster).monster = MonsterId::SOLDIER;
    state.at(kMonster).maxHp = 500;
    state.at(kMonster).hp = 20;
    return state;
}

battle::MonstersEnterOrLeave setVisibility(BattleState& state, MonsterVisibilityMode mode, BattlerSet monsters) {
    BattleScriptBuilder script;
    battle::setMonsterVisibility(
        state, script,
        battle::MonsterVisibilityAction{.mode = mode, .monsters = monsters, .type = MonsterEntranceType::FLASH});
    const auto shown = script.take();
    EXPECT_EQ(shown.size(), 1u);
    return std::get<battle::MonstersEnterOrLeave>(shown.at(0));
}

TEST(MonsterVisibility, ReviveAtFullHpBringsTheMonsterBackWhole) {
    // Cmd_24 :13061-13083 with MonsterDeath_00 :13118-13137.
    BattleState state = visibilityBattle();
    state.monstersStanding = {};
    state.monstersFallen = BattlerSet::of(kMonster);
    state.countedInBattle.remove(kMonster);
    state.battleMayNotEnd = true;
    const auto shown = setVisibility(state, MonsterVisibilityMode::ReviveAtFullHp, BattlerSet::of(kMonster));
    EXPECT_EQ(state.at(kMonster).hp, 500);
    EXPECT_FALSE(state.monstersFallen.has(kMonster));
    EXPECT_TRUE(state.monstersStanding.has(kMonster));
    EXPECT_TRUE(state.joiningAfterAction.has(kMonster));
    EXPECT_TRUE(state.countedInBattle.has(kMonster));
    EXPECT_FALSE(state.battleMayNotEnd);
    EXPECT_EQ(shown.type, MonsterEntranceType::FLASH);
    EXPECT_EQ(shown.revived, BattlerSet::of(kMonster));
    EXPECT_TRUE(shown.hidden.empty());
}

TEST(MonsterVisibility, ReviveAtCurrentHpKeepsTheHp) {
    BattleState state = visibilityBattle();
    const auto shown = setVisibility(state, MonsterVisibilityMode::ReviveAtCurrentHp, BattlerSet::of(kMonster));
    EXPECT_EQ(state.at(kMonster).hp, 20);
    EXPECT_EQ(shown.revived, BattlerSet::of(kMonster));
}

TEST(MonsterVisibility, TheFourWaysToHide) {
    struct Case {
        MonsterVisibilityMode mode;
        bool untargetable;
        bool keepsBattleGoing;
        bool dead;
    };
    for (const Case c : {Case{MonsterVisibilityMode::Hide, true, false, false},
                         Case{MonsterVisibilityMode::HideButTargetable, false, false, false},
                         Case{MonsterVisibilityMode::HideAndKeepBattleGoing, false, true, false},
                         Case{MonsterVisibilityMode::HideAndKill, false, false, true}}) {
        BattleState state = visibilityBattle();
        state.monstersStanding = BattlerSet::of(kMonster) | BattlerSet::of(kSecondMonster);
        const auto shown = setVisibility(state, c.mode, BattlerSet::of(kMonster));
        const int mode = static_cast<int>(c.mode);
        EXPECT_TRUE(state.at(kMonster).status.has(StatusId::HIDE)) << mode;
        EXPECT_FALSE(state.monstersStanding.has(kMonster)) << mode;
        EXPECT_FALSE(state.countedInBattle.has(kMonster)) << mode;
        EXPECT_EQ(state.leavingAfterAction.has(kMonster), c.untargetable) << mode;
        EXPECT_EQ(state.battleMayNotEnd, c.keepsBattleGoing) << mode;
        EXPECT_EQ(state.at(kMonster).status.has(StatusId::DEAD), c.dead) << mode;
        EXPECT_EQ(shown.hidden, BattlerSet::of(kMonster)) << mode;
        EXPECT_TRUE(shown.revived.empty()) << mode;
        EXPECT_TRUE(state.monstersStanding.has(kSecondMonster)) << mode;
    }
}

TEST(MonsterVisibility, ASlotWithNoMonsterIsLeftAlone) {
    BattleState state = visibilityBattle();
    const BattlerSlot empty = BattlerSlot::monster(4);
    const auto shown =
        setVisibility(state, MonsterVisibilityMode::HideAndKill, BattlerSet::of(empty) | BattlerSet::of(kMonster));
    EXPECT_FALSE(state.at(empty).status.has(StatusId::DEAD));
    EXPECT_EQ(shown.hidden, BattlerSet::of(kMonster));
}

// --- Scan -----------------------------------------------------------------------------------------

std::vector<battle::BattleMessage> scanMessages(BattleState& state) {
    std::vector<battle::BattleMessage> shown;
    for (const battle::BattleRequest& request : state.requests) {
        const auto& script = std::get<battle::RunBattleScript>(request);
        EXPECT_EQ(script.commands.size(), 1u);
        shown.push_back(std::get<battle::BattleMessage>(script.commands.at(0)));
    }
    return shown;
}

TEST(Scan, LevelHpMpAndEachWeaknessEachInItsOwnScript) {
    // Cmd_27 :13274-13329.
    BattleState state = makeAimableBattle();
    Battler& monster = state.at(kMonster);
    monster.level = 18;
    monster.hp = 250;
    monster.maxHp = 300;
    monster.mp = 40;
    monster.maxMp = 60;
    monster.elementsWeak = ElementSet::of(Element::FIRE, Element::POISON, Element::WATER, Element::ICE);
    monster.elementsHalved = ElementSet::of(Element::ICE);
    battle::scan(state, kMonster);
    const auto shown = scanMessages(state);
    ASSERT_EQ(shown.size(), 6u);
    EXPECT_EQ(shown[0].message, battle::kMessageLevel);
    EXPECT_EQ(shown[0].firstNumber, 18u);
    EXPECT_EQ(shown[1].message, battle::kMessageHp);
    EXPECT_EQ(shown[1].firstNumber, 250u);
    EXPECT_EQ(shown[1].secondNumber, 300u);
    EXPECT_EQ(shown[2].message, battle::kMessageMp);
    EXPECT_EQ(shown[2].firstNumber, 40u);
    EXPECT_EQ(shown[2].secondNumber, 60u);
    EXPECT_EQ(shown[3].message, battle::kMessageWeakAgainst);      // Fire
    EXPECT_EQ(shown[4].message, battle::kMessageWeakAgainst + 3);  // Poison
    EXPECT_EQ(shown[5].message, battle::kMessageWeakAgainst + 7);  // Water
}

TEST(Scan, NoMpMessageForAMonsterWithNoMpAndAbsorbedOrNullifiedWeaknessesAreLeftOut) {
    BattleState state = makeAimableBattle();
    Battler& monster = state.at(kMonster);
    monster.maxMp = 0;
    monster.elementsWeak = ElementSet::of(Element::WIND, Element::HOLY, Element::EARTH);
    monster.elementsAbsorbed = ElementSet::of(Element::WIND);
    monster.elementsNullified = ElementSet::of(Element::HOLY);
    battle::scan(state, kMonster);
    const auto shown = scanMessages(state);
    ASSERT_EQ(shown.size(), 3u);
    EXPECT_EQ(shown[1].message, battle::kMessageHp);
    EXPECT_EQ(shown[2].message, battle::kMessageWeakAgainst + 6);  // Earth
}

// --- a formation change ---------------------------------------------------------------------------

BattleState loboBattle(battle::ActionExecutor& executor) {
    battle::BattleStart start;
    start.formation = FormationRef::of(FormationId::LOBO);
    BattleState state = battle::startBattle(start, battleWithParty({CharacterPropId::TERRA}).party, {},
                                            blankMonsterNames(), noMonsterSprites(), executor);
    state.requests.clear();
    return state;
}

TEST(FormationChange, TheNewFormationsMonstersTakeTheSlotsAndTheChangeIsShown) {
    // Cmd_20 :13206-13244.
    battle::NullExecutor executor;
    BattleState state = loboBattle(executor);
    const Formation& next = getFormation(FormationId::LOBO_X2_MARSHAL);
    BattleScriptBuilder script;
    battle::formationChange(state, executor, script,
                            battle::FormationChange{.formation = FormationId::LOBO_X2_MARSHAL,
                                                    .restoreMonsters = true,
                                                    .scrollBackground = false,
                                                    .exit = MonsterEntranceType::BOSS_DEATH},
                            blankMonsterNames(), noMonsterSprites());
    EXPECT_EQ(state.formation, FormationId::LOBO_X2_MARSHAL);
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        const auto at = static_cast<std::size_t>(position);
        const Battler& battler = state.at(BattlerSlot::monster(position));
        EXPECT_EQ(battler.present, next.isPresent(at)) << position;
        if (next.slotEmpty(at)) {
            continue;
        }
        EXPECT_EQ(battler.monster, next.monsterId(at)) << position;
        EXPECT_EQ(battler.hp, getMonsterProperties(next.monsterId(at)).hp) << position;
    }
    const auto shown = script.take();
    ASSERT_EQ(shown.size(), 1u);
    const auto& changes = std::get<battle::FormationChanges>(shown[0]);
    EXPECT_FALSE(changes.scrollBackground);
    EXPECT_EQ(changes.exit, MonsterEntranceType::BOSS_DEATH);
    EXPECT_EQ(changes.monsters, state.alive.monstersOnly());
    EXPECT_EQ(state.monstersStanding, state.alive.monstersOnly());
}

TEST(FormationChange, WithoutRestoringEachNewMonsterKeepsItsSlotsHpAndMp) {
    battle::NullExecutor executor;
    BattleState state = loboBattle(executor);
    state.at(kMonster).hp = 7;
    state.at(kMonster).maxHp = 9;
    state.at(kMonster).mp = 3;
    state.at(kMonster).maxMp = 4;
    BattleScriptBuilder script;
    battle::formationChange(state, executor, script,
                            battle::FormationChange{.formation = FormationId::LOBO_X2_MARSHAL},
                            blankMonsterNames(), noMonsterSprites());
    const Battler& kept = state.at(kMonster);
    EXPECT_EQ(kept.hp, 7);
    EXPECT_EQ(kept.maxHp, 9);
    EXPECT_EQ(kept.mp, 3);
    EXPECT_EQ(kept.maxMp, 4);
    EXPECT_EQ(kept.stamina, 16);  // 16 + max HP / 512, from the kept max HP
}

// --- the executor ---------------------------------------------------------------------------------

TEST(SystemActions, TheActionsThatOnlyShowSomethingEachWriteTheirCommand) {
    BattleState state = makeAimableBattle();
    battle::AttackHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    auto run = [&](battle::Action action) { return executor.execute(state, kMonster, action); };

    auto shown = run(battle::MonsterDialogAction{.dialog = 77});
    EXPECT_EQ(std::get<battle::MonsterDialogShown>(shown.at(0)).dialog, 77);
    shown = run(battle::BattleEventAction{.event = 12});
    EXPECT_EQ(std::get<battle::BattleEventPlays>(shown.at(0)).event, 12);
    shown = run(battle::BattleMessageAction{.message = 35});
    EXPECT_EQ(std::get<battle::BattleMessage>(shown.at(0)).message, 35);
    shown = run(battle::ResetBattlePose{.character = kLocke});
    EXPECT_EQ(std::get<battle::PoseReset>(shown.at(0)).battler, kLocke);
    shown = run(battle::MonsterAnimationAction{.animation = battle::MonsterAnimation::SoundEffect,
                                               .soundEffect = 140,
                                               .pan = 128});
    const auto& animation = std::get<battle::MonsterAnimationPlays>(shown.at(0));
    EXPECT_EQ(animation.animation, battle::MonsterAnimation::SoundEffect);
    EXPECT_EQ(animation.soundEffect, 140);
    EXPECT_EQ(animation.pan, 128);
}

TEST(SystemActions, ScansReportPlaysAheadOfItsEmptyClosingScript) {
    BattleState state = makeAimableBattle();
    battle::AttackHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    state.at(kMonster).maxMp = 10;
    const auto closing = executor.execute(state, kTerra, battle::ScanAction{.target = kMonster});
    EXPECT_TRUE(closing.empty());
    EXPECT_EQ(state.requests.size(), 3u);  // level, HP, MP
}

TEST(SystemActions, AnActionWithNoBattlerStillRuns) {
    BattleState state = makeAimableBattle();
    AttackLog log;
    battle::CommandExecutor executor(log, blankMonsterNames(), noMonsterSprites());
    executor.execute(state, std::nullopt, battle::SpellWithoutCaster{.spell = AttackId::DOOM, .target = kTerra});
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(state.scratch.finalTargets, BattlerSet::of(kTerra));
}

// --- the level a condemned count reads ----------------------------------------------------------

TEST(LastAttackLevel, EachAttackLeavesItsLevelForTheNextCondemnedCount) {
    // StartCondemn :1512-1525 reads $11af, which only an attack's setup writes.
    BattleState state = makeAimableBattle();
    state.at(kTerra).level = 37;
    state.at(kTerra).weaponHand = battle::Hand{.battlePower = 10, .hitRate = 255};
    battle::AttackHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    executor.execute(state, kTerra,
                     battle::CommandAction{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(kMonster)});
    EXPECT_EQ(state.lastAttackLevel, 37);
    executor.execute(state, kMonster, battle::SeizeDrain{.seized = kTerra});
    EXPECT_EQ(state.lastAttackLevel, 16);
    executor.execute(state, kTerra,
                     battle::CommandAction{.command = BattleCommandId::STEAL, .targets = BattlerSet::of(kMonster)});
    EXPECT_EQ(state.lastAttackLevel, 0);  // the plain hit clears it (_c2298d :6907)
}

TEST(LastAttackLevel, AnActionWithoutAnAttackLevelLeavesTheLastOne) {
    BattleState state = makeAimableBattle();
    state.lastAttackLevel = 44;
    battle::AttackHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    executor.execute(state, kMonster, battle::MonsterDialogAction{.dialog = 3});
    EXPECT_EQ(state.lastAttackLevel, 44);
    state.enemyCount = 0;
    executor.execute(state, kTerra, battle::DamageOverTime{.kind = DamageOverTimeKind::Poison});
    EXPECT_EQ(state.lastAttackLevel, 44);
    executor.execute(state, kTerra,
                     battle::CommandAction{.command = BattleCommandId::POSSESS, .targets = BattlerSet::of(kMonster)});
    EXPECT_EQ(state.lastAttackLevel, 44);  // Possess writes no level
}

TEST(LastAttackLevel, ACountStartedAfterAnActionIsWorkedOutFromItsAttacksLevel) {
    // AfterAction2 :1324-1377 runs StartCondemn for a battler the action left a count to start.
    for (const auto& [level, count] : {std::pair{1, 79}, std::pair{99, 20}}) {
        BattleState state = makeAimableBattle();
        state.at(kTerra).level = static_cast<std::uint8_t>(level);
        state.at(kTerra).weaponHand = battle::Hand{.battlePower = 10, .hitRate = 255};
        battle::AttackHooks hooks;
        battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
        executor.execute(state, kTerra,
                         battle::CommandAction{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(kMonster)});
        state.at(kMonster).pendingUpdates.startCondemned = true;
        rngWhere(state, {drawsTo(level, 0)});
        battle::settleAfterAction(state, executor);
        EXPECT_EQ(state.at(kMonster).condemnedNumber, count) << level;
    }
}

// --- items obtained -------------------------------------------------------------------------------

TEST(ItemsObtained, AnItemStillInHandGoesIntoTheInventoryOnceTheActionIsOver) {
    // _c262c7 :16168-16187.
    BattleState state = makeAimableBattle();
    state.holdingItem = BattlerSet::of(kTerra) | BattlerSet::of(kLocke);
    state.at(kTerra).itemInHand = ItemId::FIRE_ROD;
    state.at(kLocke).itemInHand.reset();
    battle::putAwayItemsObtained(state);
    ASSERT_EQ(state.requests.size(), 1u);
    const auto& obtained = std::get<battle::AddItemObtained>(state.requests[0]);
    EXPECT_EQ(obtained.character, kTerra);
    EXPECT_EQ(obtained.item, ItemId::FIRE_ROD);
    EXPECT_FALSE(state.at(kTerra).itemInHand.has_value());
    EXPECT_TRUE(state.holdingItem.empty());
}

TEST(ItemsObtained, OnlyThePartyMembersMarkedHoldingPutTheirsAway) {
    BattleState state = makeAimableBattle();
    state.at(kTerra).itemInHand = ItemId::FIRE_ROD;
    battle::putAwayItemsObtained(state);
    EXPECT_TRUE(state.requests.empty());
    EXPECT_EQ(state.at(kTerra).itemInHand, ItemId::FIRE_ROD);
}

}  // namespace
}  // namespace ostinato::test
