// A whole battle fought to a win on the battle's own executor: the party's Fights carried out by
// CommandExecutor through BattleSim, with hooks that take 10 HP from every target hit.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "battle/battle_sim.h"
#include "battle/command_executor.h"
#include "battle_test_support.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using battle::BattleInput;
using battle::BattleRequest;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleSim;
using battle::BattleState;

// Every target hit loses 10 HP and falls at 0.
struct TenDamage : battle::AttackHooks {
    int hits = 0;
    void targetDamage(BattleState& state, battle::AttackInFlight&, BattlerSlot, BattlerSlot target) override {
        battle::Battler& battler = state.at(target);
        battler.hp = static_cast<std::uint16_t>(std::max(0, battler.hp - 10));
        if (battler.hp == 0) {
            battler.status.set(StatusId::DEAD);
        }
        ++hits;
    }
};

std::string nameOf(const battle::BattleScriptCommand& command) {
    return std::visit(
        [](const auto& c) -> std::string {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, battle::ReadyStance>) return "ReadyStance";
            else if constexpr (std::is_same_v<T, battle::StepBack>) return "StepBack";
            else if constexpr (std::is_same_v<T, battle::AttackAnimation>) return "AttackAnimation";
            else return "other";
        },
        command);
}

TEST(CommandBattle, ThreeFightersBeatLoboWithFights) {
    TenDamage hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    battle::BattleStart start;
    start.formation = FormationRef::of(FormationId::LOBO);
    BattleSim battle(start,
                     battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR}).party,
                     {}, blankMonsterNames(), noMonsterSprites(), executor);
    const BattlerSlot lobo = BattlerSlot::monster(0);
    ASSERT_TRUE(battle.state().at(lobo).present);
    const std::uint16_t loboHp = battle.state().at(lobo).hp;

    std::vector<BattleRequest> log;
    std::vector<battle::PlayerAction> answers;
    for (int frame = 0; frame < 20000 && !battle.result(); ++frame) {
        battle.tick(BattleInput{.playerActions = std::exchange(answers, {})});
        for (BattleRequest& request : battle.takeRequests()) {
            if (const auto* menu = std::get_if<battle::OpenBattleMenu>(&request)) {
                answers.push_back({.character = menu->character,
                                   .command = BattleCommandId::FIGHT,
                                   .targets = BattlerSet::of(lobo)});
            }
            log.push_back(std::move(request));
        }
    }
    ASSERT_TRUE(battle.result().has_value());
    EXPECT_TRUE(std::holds_alternative<battle::BattleWon>(battle.result()->outcome));
    EXPECT_TRUE(battle.state().at(lobo).status.has(StatusId::DEAD));
    // Each hit takes 10: the blows that reached Lobo brought it down from full and no further.
    EXPECT_EQ(hooks.hits, (loboHp + 9) / 10);

    // The log: the scene opens, the fighters step forward, swing and step back, and the scene
    // closes last. Every swing is a Fight by a party member at Lobo.
    ASSERT_FALSE(log.empty());
    EXPECT_TRUE(std::holds_alternative<battle::OpenBattleScene>(log.front()));
    EXPECT_TRUE(std::holds_alternative<battle::CloseBattleScene>(log.back()));
    int stances = 0;
    int stepsBack = 0;
    int swings = 0;
    std::string sequence;
    for (const BattleRequest& request : log) {
        const auto* script = std::get_if<battle::RunBattleScript>(&request);
        if (script == nullptr) {
            continue;
        }
        for (const battle::BattleScriptCommand& command : script->commands) {
            sequence += nameOf(command) + " ";
            if (std::holds_alternative<battle::ReadyStance>(command)) ++stances;
            if (std::holds_alternative<battle::StepBack>(command)) ++stepsBack;
            if (const auto* swing = std::get_if<battle::AttackAnimation>(&command)) {
                ++swings;
                EXPECT_EQ(std::get<BattleCommandId>(swing->what), BattleCommandId::FIGHT);
                EXPECT_TRUE(swing->params.attacker.isCharacter());
                EXPECT_EQ(swing->params.targets, BattlerSet::of(lobo));
            }
        }
    }
    RecordProperty("scripts", sequence);
    EXPECT_GT(stances, 0);
    EXPECT_GE(stepsBack, stances);  // every turn ends with a step back, the monsters' empty ones too
    EXPECT_GE(swings, hooks.hits);
    EXPECT_EQ(std::count_if(log.begin(), log.end(),
                            [](const BattleRequest& r) { return std::holds_alternative<battle::AddItemObtained>(r); }),
              0);
}

}  // namespace
}  // namespace ostinato::test
