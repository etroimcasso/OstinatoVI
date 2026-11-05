// The spells a near-fatal battler's relics cast (docs/contracts/battle_status.md, "The near-fatal
// relics"): what lets them be cast, the one chance a battle spent before the relics are read, and the
// three spells in the order they are queued.

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "battle/battle_state.h"
#include "battle/status.h"
#include "ostinato/attack_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using battle::BattleState;
using battle::SpellWithoutCaster;

constexpr BattlerSlot kMember = BattlerSlot::character(2);

// A battle with one party member near fatal, wearing relics with `relics`.
BattleState nearFatalWith(RelicEffect5Set relics) {
    BattleState state;
    battle::Battler& battler = state.at(kMember);
    battler.present = true;
    battler.status = StatusSet::of(StatusId::NEAR_FATAL);
    battler.relicEffects5 = relics;
    return state;
}

// The spells queued to run before anything else, as SpellWithoutCaster actions.
std::vector<SpellWithoutCaster> queuedSpells(const BattleState& state) {
    std::vector<SpellWithoutCaster> spells;
    for (const battle::Action& action : state.immediateActions) {
        const auto* spell = std::get_if<SpellWithoutCaster>(&action);
        EXPECT_NE(spell, nullptr);
        if (spell != nullptr) {
            spells.push_back(*spell);
        }
    }
    return spells;
}

// :1630-1637.
TEST(NearFatalRelicSpells, ShellWhenHpLowCastsShellOnTheWearer) {
    BattleState state = nearFatalWith(RelicEffect5Set::of(RelicEffect5::SHELL_WHEN_HP_LOW));
    battle::nearFatalRelicSpells(state, kMember);
    const auto spells = queuedSpells(state);
    ASSERT_EQ(spells.size(), 1u);
    EXPECT_EQ(spells[0].spell, AttackId::SHELL);
    EXPECT_EQ(spells[0].target, kMember);
}

// :1639-1645.
TEST(NearFatalRelicSpells, SafeWhenHpLowCastsSafeOnTheWearer) {
    BattleState state = nearFatalWith(RelicEffect5Set::of(RelicEffect5::SAFE_WHEN_HP_LOW));
    battle::nearFatalRelicSpells(state, kMember);
    const auto spells = queuedSpells(state);
    ASSERT_EQ(spells.size(), 1u);
    EXPECT_EQ(spells[0].spell, AttackId::SAFE);
    EXPECT_EQ(spells[0].target, kMember);
}

// :1647-1653.
TEST(NearFatalRelicSpells, WallWhenHpLowCastsReflectOnTheWearer) {
    BattleState state = nearFatalWith(RelicEffect5Set::of(RelicEffect5::WALL_WHEN_HP_LOW));
    battle::nearFatalRelicSpells(state, kMember);
    const auto spells = queuedSpells(state);
    ASSERT_EQ(spells.size(), 1u);
    EXPECT_EQ(spells[0].spell, AttackId::RFLECT);
    EXPECT_EQ(spells[0].target, kMember);
}

// The three bits are read low first: Shell, then Safe, then Reflect.
TEST(NearFatalRelicSpells, AllThreeAreQueuedShellSafeReflect) {
    BattleState state = nearFatalWith(RelicEffect5Set::of(
        RelicEffect5::SHELL_WHEN_HP_LOW, RelicEffect5::SAFE_WHEN_HP_LOW, RelicEffect5::WALL_WHEN_HP_LOW));
    battle::nearFatalRelicSpells(state, kMember);
    const auto spells = queuedSpells(state);
    ASSERT_EQ(spells.size(), 3u);
    EXPECT_EQ(spells[0].spell, AttackId::SHELL);
    EXPECT_EQ(spells[1].spell, AttackId::SAFE);
    EXPECT_EQ(spells[2].spell, AttackId::RFLECT);
    for (const SpellWithoutCaster& spell : spells) {
        EXPECT_EQ(spell.target, kMember);
    }
}

// :1626-1629: the chance is spent with the first cast; a second near-fatal check casts nothing.
TEST(NearFatalRelicSpells, TheSpellsAreCastOnceABattle) {
    BattleState state = nearFatalWith(RelicEffect5Set::of(RelicEffect5::SHELL_WHEN_HP_LOW));
    battle::nearFatalRelicSpells(state, kMember);
    EXPECT_FALSE(state.at(kMember).nearFatalSpellAvailable);
    state.immediateActions.clear();
    battle::nearFatalRelicSpells(state, kMember);
    EXPECT_TRUE(state.immediateActions.empty());
}

// :1628-1629 come before the relics are read: falling near fatal with none of the three effects
// spends the chance all the same.
TEST(NearFatalRelicSpells, TheChanceIsSpentEvenWithNoNearFatalRelic) {
    BattleState state = nearFatalWith(RelicEffect5Set{});
    battle::nearFatalRelicSpells(state, kMember);
    EXPECT_TRUE(state.immediateActions.empty());
    EXPECT_FALSE(state.at(kMember).nearFatalSpellAvailable);
}

// :1620-1622.
TEST(NearFatalRelicSpells, NothingForABattlerHeldByAStatus) {
    BattleState state = nearFatalWith(RelicEffect5Set::of(RelicEffect5::SHELL_WHEN_HP_LOW));
    state.at(kMember).heldByStatus = true;
    battle::nearFatalRelicSpells(state, kMember);
    EXPECT_TRUE(state.immediateActions.empty());
    EXPECT_TRUE(state.at(kMember).nearFatalSpellAvailable);
}

// :1623-1625.
TEST(NearFatalRelicSpells, NothingForABattlerThatIsNotNearFatal) {
    BattleState state = nearFatalWith(RelicEffect5Set::of(RelicEffect5::SHELL_WHEN_HP_LOW));
    state.at(kMember).status = StatusSet{};
    battle::nearFatalRelicSpells(state, kMember);
    EXPECT_TRUE(state.immediateActions.empty());
    EXPECT_TRUE(state.at(kMember).nearFatalSpellAvailable);
}

// :1626-1627.
TEST(NearFatalRelicSpells, NothingOnceTheChanceIsSpent) {
    BattleState state = nearFatalWith(RelicEffect5Set::of(RelicEffect5::SHELL_WHEN_HP_LOW));
    state.at(kMember).nearFatalSpellAvailable = false;
    battle::nearFatalRelicSpells(state, kMember);
    EXPECT_TRUE(state.immediateActions.empty());
}

// The spells join whatever is already waiting to run before anything else, after it.
TEST(NearFatalRelicSpells, TheSpellsGoAfterActionsAlreadyWaiting) {
    BattleState state = nearFatalWith(RelicEffect5Set::of(RelicEffect5::SAFE_WHEN_HP_LOW));
    state.immediateActions.push_back(SpellWithoutCaster{.spell = AttackId::LIFE, .target = BattlerSlot::character(0)});
    battle::nearFatalRelicSpells(state, kMember);
    const auto spells = queuedSpells(state);
    ASSERT_EQ(spells.size(), 2u);
    EXPECT_EQ(spells[0].spell, AttackId::LIFE);
    EXPECT_EQ(spells[1].spell, AttackId::SAFE);
}

}  // namespace
}  // namespace ostinato::test
