// The statuses a battle starts with and the near-fatal relics, on the battle's own executor
// (docs/contracts/battle_status.md, "Where the statuses are worked out" and "The near-fatal relics"):
// the statuses a monster's and a party member's records start them with going on as the battle
// starts and as a formation changes, Rage's statuses going on with its attack, and the near-fatal
// relics casting from the battle's settling after an action — and at its start.

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>
#include <variant>

#include "battle/battle_core.h"
#include "battle/battle_hooks.h"
#include "battle/battle_start.h"
#include "battle/command_executor.h"
#include "battle/commands.h"
#include "battle_test_support.h"
#include "data/formations.h"
#include "data/item_properties.h"
#include "data/monster_properties.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleState;
using battle::CommandAction;

constexpr BattlerSlot kFirst = BattlerSlot::character(0);

// The first monster whose record says `starts` of it.
template <typename Match>
MonsterId firstMonster(Match starts) {
    for (const MonsterPropertiesEntry& entry : monsterProperties()) {
        if (starts(entry.record.innateStatuses)) {
            return entry.id;
        }
    }
    ADD_FAILURE() << "no monster matches";
    return MonsterId{};
}

// A monster that flies: the first bit of its record's third status byte.
MonsterId aFlier() {
    return firstMonster([](const StatusSet& innate) { return (innate.bytes[2] & 0x01) != 0; });
}

MonsterId aMonsterWithProtect() {
    return firstMonster([](const StatusSet& innate) { return innate.has(StatusId::PROTECT); });
}

// The first formation `monster` stands in, and the slot it stands in there.
struct Placed {
    FormationId formation{};
    BattlerSlot slot = BattlerSlot::monster(0);
};

Placed formationWith(MonsterId monster) {
    for (const FormationEntry& entry : formations()) {
        if (getFormationAux(entry.id).characterAiEnabled()) {
            continue;
        }
        for (std::size_t slot = 0; slot < 6; ++slot) {
            if (!entry.record.slotEmpty(slot) && entry.record.monsterId(slot) == monster) {
                return {.formation = entry.id, .slot = BattlerSlot::monster(static_cast<int>(slot))};
            }
        }
    }
    ADD_FAILURE() << "no formation holds the monster";
    return {};
}

battle::BattleStart startFor(FormationId formation) {
    battle::BattleStart start;
    start.formation = FormationRef::of(formation);
    return start;
}

game::Party threeMembers() {
    return battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR}).party;
}

// The battle's own executor and hooks, for a test.
struct OwnExecutor {
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, blankMonsterNames(), noMonsterSprites()};
};

BattleState startOnTheBattlesExecutor(OwnExecutor& own, FormationId formation, game::Party party) {
    return battle::startBattle(startFor(formation), std::move(party), {}, blankMonsterNames(), noMonsterSprites(),
                               own.executor);
}

ItemId firstRelicWith(RelicEffect5 effect) {
    for (const ItemPropertiesEntry& entry : itemPropertiesEn()) {
        if (entry.id != ItemId::EMPTY && entry.record.relicEffects5.has(effect)) {
            return entry.id;
        }
    }
    ADD_FAILURE() << "no relic has the effect";
    return ItemId::EMPTY;
}

bool queuesSpellAt(const BattleState& state, AttackId spell, BattlerSlot target) {
    for (const battle::Action& action : state.immediateActions) {
        const auto* cast = std::get_if<battle::SpellWithoutCaster>(&action);
        if (cast != nullptr && cast->spell == spell && cast->target == target) {
            return true;
        }
    }
    return false;
}

// --- the start ------------------------------------------------------------------------------------

// :6127-6128: the monsters are loaded, then the statuses waiting to be set go on.
TEST(StatusStart, AFlyingMonsterFloatsFromTheStart) {
    const Placed placed = formationWith(aFlier());
    OwnExecutor own;
    const BattleState state = startOnTheBattlesExecutor(own, placed.formation, threeMembers());
    const battle::Battler& flier = state.at(placed.slot);
    ASSERT_TRUE(flier.present);
    EXPECT_TRUE(flier.status.has(StatusId::FLOAT));
    EXPECT_EQ(flier.statusesToSet.bytes, StatusSet{}.bytes);
}

TEST(StatusStart, AMonsterStartsWithTheStatusesItsRecordGivesIt) {
    const Placed placed = formationWith(aMonsterWithProtect());
    OwnExecutor own;
    const BattleState state = startOnTheBattlesExecutor(own, placed.formation, threeMembers());
    EXPECT_TRUE(state.at(placed.slot).status.has(StatusId::PROTECT));
}

// The loader keeps Reraise of a record's fourth status byte (LoadRageProp :7527), but no monster's
// record has it: no monster starts a battle with Reraise.
TEST(StatusStart, NoMonstersRecordStartsItWithReraise) {
    for (const MonsterPropertiesEntry& entry : monsterProperties()) {
        EXPECT_FALSE(entry.record.innateStatuses.has(StatusId::RERAISE)) << static_cast<int>(entry.id);
    }
}

// The loaders only say what is to go on; an executor that puts nothing on leaves it waiting.
TEST(StatusStart, WithAnExecutorThatAppliesNothingTheStatusesStayWaiting) {
    const Placed placed = formationWith(aFlier());
    battle::NullExecutor executor;
    const BattleState state = battle::startBattle(startFor(placed.formation), threeMembers(), {},
                                                  blankMonsterNames(), noMonsterSprites(), executor);
    const battle::Battler& flier = state.at(placed.slot);
    EXPECT_FALSE(flier.status.has(StatusId::FLOAT));
    EXPECT_TRUE(flier.statusesToSet.has(StatusId::FLOAT));
}

// LoadCharProp :6640-6641: the record's statuses are the party member's to set.
TEST(StatusStart, APartyMemberStartsWithItsRecordsStatuses) {
    game::Party party = threeMembers();
    party.characters[0].status = StatusSet::of(StatusId::POISON);
    OwnExecutor own;
    const BattleState state = startOnTheBattlesExecutor(own, formationWith(aFlier()).formation, party);
    EXPECT_TRUE(state.at(kFirst).status.has(StatusId::POISON));
    EXPECT_EQ(state.at(kFirst).statusesToSet.bytes, StatusSet{}.bytes);
}

// :6632-6634: the statuses its equipment keeps on it go on with the record's.
TEST(StatusStart, APartyMemberStartsWithItsEquipmentsStatuses) {
    game::Party party = threeMembers();
    ItemId floats = ItemId::EMPTY;
    for (const ItemPropertiesEntry& entry : itemPropertiesEn()) {
        if (entry.id != ItemId::EMPTY && entry.record.status3Granted.has(StatusId::DANCE)) {
            floats = entry.id;
            break;
        }
    }
    ASSERT_NE(floats, ItemId::EMPTY);
    party.characters[0].relic1 = floats;
    OwnExecutor own;
    const BattleState state = startOnTheBattlesExecutor(own, formationWith(aFlier()).formation, party);
    EXPECT_TRUE(state.at(kFirst).status.has(StatusId::FLOAT));
}

// :13230-13232: the new formation's monsters are loaded and their statuses go on; the party's
// statuses are left as they are.
TEST(StatusStart, AFormationChangePutsTheNewMonstersStatusesOnAndLeavesThePartys) {
    OwnExecutor own;
    BattleState state = startOnTheBattlesExecutor(own, FormationId::LOBO, threeMembers());
    state.at(kFirst).status.set(StatusId::HASTE);
    const StatusSet partyBefore = state.at(kFirst).status;
    const Placed placed = formationWith(aFlier());
    battle::changeFormation(state, own.executor, placed.formation, battle::MonsterHp::FromRecord,
                            blankMonsterNames(), noMonsterSprites());
    EXPECT_TRUE(state.at(placed.slot).status.has(StatusId::FLOAT));
    EXPECT_EQ(state.at(kFirst).status.bytes, partyBefore.bytes);
}

// SetRage :2dc1 writes the monster's statuses to set; the attack's statuses then take effect.
TEST(StatusStart, RagesStatusesGoOnWithItsAttack) {
    BattleState state = makeAimableBattle();
    battle::Battler& gau = state.at(kFirst);
    gau.actor = CharacterPropId::GAU;
    gau.rage = aFlier();
    gau.weaponHand = battle::Hand{.battlePower = 10, .hitRate = 255};
    battle::BattleHooks hooks;
    battle::AttackInFlight attack = battle::startAttack(CommandAction{
        .command = BattleCommandId::RAGE, .attack = AttackId::BATTLE, .targets = BattlerSet::of(BattlerSlot::monster(0))});
    battle::BattleScriptBuilder script;
    battle::runCommand(state, hooks, script, attack, kFirst);
    EXPECT_TRUE(gau.status.has(StatusId::FLOAT));
    EXPECT_EQ(gau.statusesToSet.bytes, StatusSet{}.bytes);
}

// --- the near-fatal relics on the battle's executor -----------------------------------------------

// AfterAction2 :1374: the settling after an action ends each battler's turn with its near-fatal
// relics.
TEST(StatusStart, TheSettlingAfterAnActionCastsANearFatalRelicsSpell) {
    BattleState state = makeBattle();
    battle::Battler& member = state.at(kFirst);
    member.status = StatusSet::of(StatusId::NEAR_FATAL);
    member.relicEffects5 = RelicEffect5Set::of(RelicEffect5::SHELL_WHEN_HP_LOW);
    OwnExecutor own;
    battle::settleAfterAction(state, own.executor);
    EXPECT_TRUE(queuesSpellAt(state, AttackId::SHELL, kFirst));
    EXPECT_FALSE(member.nearFatalSpellAvailable);
}

// :6128-6132: the start's statuses go on, near fatal with them, before the settling — so a party
// member who starts the battle at low HP has its relic's spell waiting from the first frame.
TEST(StatusStart, APartyMemberStartingNearFatalCastsItsRelicsSpellAsTheBattleOpens) {
    game::Party party = threeMembers();
    party.characters[0].hp = 1;
    party.characters[0].relic1 = firstRelicWith(RelicEffect5::SHELL_WHEN_HP_LOW);
    OwnExecutor own;
    const BattleState state = startOnTheBattlesExecutor(own, formationWith(aFlier()).formation, party);
    EXPECT_TRUE(state.at(kFirst).status.has(StatusId::NEAR_FATAL));
    EXPECT_TRUE(queuesSpellAt(state, AttackId::SHELL, kFirst));
}

}  // namespace
}  // namespace ostinato::test
