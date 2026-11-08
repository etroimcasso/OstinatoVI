// The control menu (docs/contracts/battle_effects.md, "The control menu"): a controller's four rows
// from its monster's control attacks, each castable when the monster has the MP it costs; the rows
// filled again when the controlled monster acts; and the 65,535-MP wrap, checked against the
// cartridge's own routine through Control taking hold.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "assets/cartridge.h"
#include "battle/battle_hooks.h"
#include "battle/links.h"
#include "damage_oracle.h"
#include "data/attack_properties.h"
#include "data/monster_attacks.h"
#include "data/monster_properties.h"
#include "ostinato/attack_id.h"
#include "ostinato/monster_id.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using battle::CommandEffect;
using battle::SpecialEffect;
using trace::DamageOracle;
using trace::DamageScenario;
using trace::TargetEffectResult;

constexpr BattlerSlot kController = BattlerSlot::character(0);
constexpr BattlerSlot kMonster = BattlerSlot::monster(1);

class ControlMenuTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    // Control taking hold of the monster with `mp` MP, worked out by the port and checked against the
    // cartridge; returns the controller's menu.
    static std::array<battle::ControlMenuEntry, 4> controlledWith(std::uint16_t mp) {
        DamageScenario s;
        s.attacker = kController;
        s.attack.effect = SpecialEffect::of(CommandEffect::Control);
        s.attack.targets = BattlerSet::of(kMonster);
        s.attack.params.attacker = kController;
        s.attack.params.targets = BattlerSet::of(kMonster);
        for (const BattlerSlot slot : {kController, kMonster}) {
            battle::Battler& battler = s.state.at(slot);
            battler.present = true;
            battler.level = 20;
            battler.maxHp = 1000;
            battler.hp = 1000;
        }
        battle::Battler& monster = s.state.at(kMonster);
        monster.monster = MonsterId::SOLDIER;
        monster.maxMp = 0xFFFF;
        monster.mp = mp;
        monster.lastAttacker = kController;
        monster.retaliationPending = true;
        monster.lastHit.command = battle::Hit<BattleCommandId>{.by = kController, .with = BattleCommandId::CONTROL};
        const trace::TargetEffectRun run = trace::runTargetEffect(s, kMonster);
        TargetEffectResult guest;
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            guest = oracle.targetEffect(s, kMonster);
        });
        EXPECT_EQ(trace::differences(trace::targetEffectResult(run), guest), "");
        EXPECT_FALSE(run.missed);
        return run.after.state.at(kController).controlMenu;
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* ControlMenuTest::image_ = nullptr;

// The Soldier's control attacks: Battle, Scan, and two empty slots.
const MonsterControl& soldier() {
    return getMonsterControl(MonsterId::SOLDIER);
}

TEST(ControlMenu, TheRowsAreTheMonstersControlAttacksWithTheirOwnTargeting) {
    battle::BattleState state;
    state.at(kMonster).monster = MonsterId::SOLDIER;
    state.at(kMonster).mp = 500;
    battle::controlMenu(state, kController, kMonster);
    const auto& menu = state.at(kController).controlMenu;
    ASSERT_EQ(soldier().attacks[0], AttackId::BATTLE);
    ASSERT_EQ(soldier().attacks[1], AttackId::SCAN);
    for (std::size_t row = 0; row < 2; ++row) {
        EXPECT_EQ(menu[row].attack, soldier().attacks[row]);
        EXPECT_TRUE(menu[row].castable);
        EXPECT_EQ(menu[row].targeting.bits, getAttackProperties(soldier().attacks[row]).targeting.bits);
    }
}

TEST(ControlMenu, AnEmptySlotIsARowThatCannotBeChosen) {
    battle::BattleState state;
    state.at(kMonster).monster = MonsterId::SOLDIER;
    state.at(kMonster).mp = 500;
    battle::controlMenu(state, kController, kMonster);
    const battle::ControlMenuEntry& empty = state.at(kController).controlMenu[2];
    EXPECT_EQ(empty.attack, AttackId::NONE);
    EXPECT_FALSE(empty.castable);
    EXPECT_EQ(empty.targeting.bits, 0);
}

TEST(ControlMenu, TheBattlesHooksFillTheMenuAgainWhenTheMonsterActs) {
    battle::BattleState state;
    state.at(kMonster).monster = MonsterId::SOLDIER;
    state.at(kMonster).mp = 0;
    battle::BattleHooks hooks;
    hooks.controlMenu(state, kController, kMonster);
    EXPECT_EQ(state.at(kController).controlMenu[1].attack, AttackId::SCAN);
    EXPECT_EQ(state.at(kController).controlMenu[1].castable, getAttackProperties(AttackId::SCAN).mpCost == 0);
}

TEST_F(ControlMenuTest, AnAttackIsCastableWithExactlyItsMpAndNotWithOneLess) {
    const std::uint8_t cost = getAttackProperties(AttackId::SCAN).mpCost;
    ASSERT_GT(cost, 0);
    EXPECT_TRUE(controlledWith(cost)[1].castable);
    EXPECT_FALSE(controlledWith(static_cast<std::uint16_t>(cost - 1))[1].castable);
}

TEST_F(ControlMenuTest, AMonsterWith255MpOrMoreCanCastEveryAttack) {
    const auto menu = controlledWith(300);
    EXPECT_TRUE(menu[0].castable);
    EXPECT_TRUE(menu[1].castable);
}

TEST_F(ControlMenuTest, AMonsterWith65535MpCanCastNothing) {
    const auto menu = controlledWith(0xFFFF);
    EXPECT_FALSE(menu[0].castable);
    EXPECT_FALSE(menu[1].castable);
}

// The wrap at 65,535 MP would need a monster with that much, and no monster's MP rises in battle past
// the MP it starts with: none of the game's 384 starts there, so the wrap is never reached.
TEST(ControlMenuCorpus, NoMonsterStartsWith65535Mp) {
    int wrapping = 0;
    for (const MonsterPropertiesEntry& entry : monsterProperties()) {
        wrapping += entry.record.mp == 0xFFFF ? 1 : 0;
    }
    EXPECT_EQ(monsterProperties().size(), 384u);
    EXPECT_EQ(wrapping, 0);
}

}  // namespace
}  // namespace ostinato::test
