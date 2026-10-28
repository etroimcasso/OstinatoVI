// Which of a party member's battle commands can be chosen. Behaviors and their sources:
// docs/contracts/battle_commands.md § The command menu.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

#include "battle/command_executor.h"
#include "battle/command_menu.h"
#include "battle_test_support.h"
#include "data/battle_commands.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using battle::BattleState;
using battle::MenuCommand;

const BattlerSlot kTerra = BattlerSlot::character(0);

// A party member holding four commands, each shown as usable with its own targeting.
BattleState menuBattle(std::array<BattleCommandId, 4> commands) {
    BattleState state = makeBattle();
    battle::Battler& terra = state.at(kTerra);
    for (std::size_t row = 0; row < commands.size(); ++row) {
        const bool present = commands[row] != BattleCommandId::NONE;
        terra.commands[row] = MenuCommand{
            .command = commands[row],
            .usable = present,
            .targeting = present ? battleCommandProperties(commands[row]).targeting : Targeting{},
        };
    }
    return state;
}

std::array<bool, 4> usable(const BattleState& state) {
    const auto& commands = state.at(kTerra).commands;
    return {commands[0].usable, commands[1].usable, commands[2].usable, commands[3].usable};
}

TEST(CommandMenu, EveryCommandIsUsableByDefaultAndTheMenuIsRedrawn) {
    BattleState state = menuBattle({BattleCommandId::FIGHT, BattleCommandId::MAGIC, BattleCommandId::STEAL,
                                    BattleCommandId::ITEM});
    battle::refreshCommandMenu(state, kTerra);
    EXPECT_EQ(usable(state), (std::array<bool, 4>{true, true, true, true}));
    EXPECT_TRUE(state.menusToRefresh.has(kTerra));
}

TEST(CommandMenu, AnEmptyRowCannotBeChosen) {
    BattleState state = menuBattle({BattleCommandId::FIGHT, BattleCommandId::NONE, BattleCommandId::NONE,
                                    BattleCommandId::ITEM});
    battle::refreshCommandMenu(state, kTerra);
    EXPECT_EQ(usable(state), (std::array<bool, 4>{true, false, false, true}));
}

TEST(CommandMenu, AnImpUsesOnlyTheCommandsAnImpMay) {
    BattleState state = menuBattle({BattleCommandId::FIGHT, BattleCommandId::MAGIC, BattleCommandId::STEAL,
                                    BattleCommandId::ITEM});
    state.at(kTerra).status.set(StatusId::IMP);
    battle::refreshCommandMenu(state, kTerra);
    for (std::size_t row = 0; row < 4; ++row) {
        const BattleCommandId command = state.at(kTerra).commands[row].command;
        EXPECT_EQ(state.at(kTerra).commands[row].usable,
                  battleCommandProperties(command).flags.has(BattleCommandFlags::IMP))
            << static_cast<int>(command);
    }
}

TEST(CommandMenu, MagicXMagicAndLoreAreLostToMute) {
    BattleState state = menuBattle({BattleCommandId::MAGIC, BattleCommandId::X_MAGIC, BattleCommandId::LORE,
                                    BattleCommandId::SUMMON});
    state.at(kTerra).status.set(StatusId::SILENCE);
    battle::refreshCommandMenu(state, kTerra);
    EXPECT_EQ(usable(state), (std::array<bool, 4>{false, false, false, true}));
}

TEST(CommandMenu, MorphNeedsSixteenInTheMorphSupply) {
    BattleState state = menuBattle({BattleCommandId::MORPH, BattleCommandId::NONE, BattleCommandId::NONE,
                                    BattleCommandId::NONE});
    state.party.morphSupply = 15;
    battle::refreshCommandMenu(state, kTerra);
    EXPECT_FALSE(state.at(kTerra).commands[0].usable);
    state.party.morphSupply = 16;
    battle::refreshCommandMenu(state, kTerra);
    EXPECT_TRUE(state.at(kTerra).commands[0].usable);
}

TEST(CommandMenu, RunicAndBushidoNeedTheirWeaponInEitherHand) {
    BattleState state = menuBattle({BattleCommandId::RUNIC, BattleCommandId::BUSHIDO, BattleCommandId::NONE,
                                    BattleCommandId::NONE});
    battle::refreshCommandMenu(state, kTerra);
    EXPECT_EQ(usable(state), (std::array<bool, 4>{false, false, false, false}));

    state.at(kTerra).shieldHand.weaponFlags.set(WeaponFlags::RUNIC);
    battle::refreshCommandMenu(state, kTerra);
    EXPECT_EQ(usable(state), (std::array<bool, 4>{true, false, false, false}));

    state.at(kTerra).weaponHand.weaponFlags.set(WeaponFlags::SWDTECH);
    battle::refreshCommandMenu(state, kTerra);
    EXPECT_EQ(usable(state), (std::array<bool, 4>{true, true, false, false}));
}

TEST(CommandMenu, AnOfferingChangesFightAndCapturesTargetingForGood) {
    BattleState state = menuBattle({BattleCommandId::FIGHT, BattleCommandId::CAPTURE, BattleCommandId::STEAL,
                                    BattleCommandId::NONE});
    state.at(kTerra).relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
    battle::refreshCommandMenu(state, kTerra);
    EXPECT_EQ(state.at(kTerra).commands[0].targeting.bits, 0x4E);
    EXPECT_EQ(state.at(kTerra).commands[1].targeting.bits, 0x4E);
    EXPECT_EQ(state.at(kTerra).commands[2].targeting.bits,
              battleCommandProperties(BattleCommandId::STEAL).targeting.bits);

    state.at(kTerra).relicEffects4.clear(RelicEffect4::FIGHT_TO_X_FIGHT);
    battle::refreshCommandMenu(state, kTerra);
    EXPECT_EQ(state.at(kTerra).commands[0].targeting.bits, 0x4E);
}

TEST(CommandMenu, AnImpsFightKeepsItsTargetingOnlyIfAnImpMayFight) {
    BattleState state = menuBattle({BattleCommandId::FIGHT, BattleCommandId::NONE, BattleCommandId::NONE,
                                    BattleCommandId::NONE});
    battle::Battler& terra = state.at(kTerra);
    terra.status.set(StatusId::IMP);
    terra.relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
    battle::refreshCommandMenu(state, kTerra);
    const bool impMayFight = battleCommandProperties(BattleCommandId::FIGHT).flags.has(BattleCommandFlags::IMP);
    EXPECT_EQ(terra.commands[0].targeting.bits == 0x4E, impMayFight);
}

TEST(CommandMenu, TheExecutorRefreshesTheMenu) {
    BattleState state = menuBattle({BattleCommandId::MAGIC, BattleCommandId::NONE, BattleCommandId::NONE,
                                    BattleCommandId::NONE});
    state.at(kTerra).status.set(StatusId::SILENCE);
    battle::AttackHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    executor.refreshCommandList(state, kTerra);
    EXPECT_FALSE(state.at(kTerra).commands[0].usable);
}

}  // namespace
}  // namespace ostinato::test
