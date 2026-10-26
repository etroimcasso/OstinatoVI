// A battle run from a frame tick: both clocks on one tick, the loop waiting for the presentation,
// the player's commands coming in, and whole scripted battles to a win, to a loss and through a
// stage of the final battle.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "battle/actions.h"
#include "battle/battle_sim.h"
#include "data/battle_tables.h"
#include "data/formations.h"
#include "ostinato/text_class.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using battle::BattleInput;
using battle::BattleRequest;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleSim;
using battle::BattleState;
using battle::CommandAction;

const TextCorpus& noNames() {
    static const TextCorpus corpus = [] {
        std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
        buffers[static_cast<std::size_t>(TextClass::MONSTER_NAME)].assign(3840, 0xFF);
        return TextCorpus{std::move(buffers)};
    }();
    return corpus;
}

game::Party threeMembers() {
    return battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR}).party;
}

// A battle where each command hits: a party member's takes `partyDamage` from every monster it
// targets, a monster's AI turn is a Fight at the first party member standing that takes
// `monsterDamage`. A battler brought to 0 HP is Dead.
struct HitsExecutor : battle::ActionExecutor {
    int partyDamage = 0;
    int monsterDamage = 0;
    int stageEnds = 0;
    std::vector<battle::BattleOutcome> ends;

    static void hit(battle::Battler& battler, int damage) {
        battler.hp = static_cast<std::uint16_t>(std::max(0, battler.hp - damage));
        if (battler.hp == 0) {
            battler.status.set(StatusId::DEAD);
        }
    }

    std::vector<battle::BattleScriptCommand> execute(BattleState& state, std::optional<BattlerSlot> actor,
                                                     const battle::Action& action) override {
        const auto* command = std::get_if<CommandAction>(&action);
        if (command == nullptr || !actor) {
            return {};
        }
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            const BattlerSlot target = BattlerSlot::fromIndex(index);
            if (command->targets.has(target) && state.at(target).present) {
                hit(state.at(target), actor->isCharacter() ? partyDamage : monsterDamage);
            }
        }
        return {};
    }

    void runAi(BattleState& state, BattlerSlot actor) override {
        for (int position = 0; position < battle::kPartySlots; ++position) {
            const BattlerSlot target = BattlerSlot::character(position);
            if (state.alive.has(target)) {
                battle::addAction(state, actor,
                                  CommandAction{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(target)});
                return;
            }
        }
    }

    void playBattleEnd(BattleState&, const battle::BattleOutcome& outcome) override { ends.push_back(outcome); }
};

std::string nameOf(const BattleRequest& request) {
    return std::visit(
        [](const auto& r) -> std::string {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, battle::OpenBattleMenu>) return "OpenBattleMenu";
            else if constexpr (std::is_same_v<T, battle::CloseBattleMenu>) return "CloseBattleMenu";
            else if constexpr (std::is_same_v<T, battle::RefreshBattleMenu>) return "RefreshBattleMenu";
            else if constexpr (std::is_same_v<T, battle::RunBattleScript>) return "RunBattleScript";
            else if constexpr (std::is_same_v<T, battle::RefreshMonsterNames>) return "RefreshMonsterNames";
            else if constexpr (std::is_same_v<T, battle::OpenBattleScene>) return "OpenBattleScene";
            else if constexpr (std::is_same_v<T, battle::ScrollFinalBattleBackground>) return "ScrollFinalBattleBackground";
            else return "CloseBattleScene";
        },
        request);
}

// Drive the battle as a presentation that plays every script at once and a player who answers
// every menu with a Fight at the first monster standing. Returns every request, in order.
std::vector<BattleRequest> playOut(BattleSim& battle, int frames) {
    std::vector<BattleRequest> log;
    std::vector<battle::PlayerAction> answers;
    for (int frame = 0; frame < frames && !battle.result(); ++frame) {
        battle.tick(BattleInput{.playerActions = std::exchange(answers, {})});
        for (BattleRequest& request : battle.takeRequests()) {
            if (const auto* menu = std::get_if<battle::OpenBattleMenu>(&request)) {
                for (int position = 0; position < battle::kMonsterSlots; ++position) {
                    const BattlerSlot monster = BattlerSlot::monster(position);
                    if (battle.state().alive.has(monster)) {
                        answers.push_back({.character = menu->character,
                                           .command = BattleCommandId::FIGHT,
                                           .targets = BattlerSet::of(monster)});
                        break;
                    }
                }
            }
            log.push_back(std::move(request));
        }
    }
    return log;
}

battle::BattleStart startFor(FormationId formation) {
    battle::BattleStart start;
    start.formation = FormationRef::of(formation);
    return start;
}

TEST(BattleSim, TimeRunsThroughAnAnimationWhileTheLoopWaits) {
    // ExecBtlGfx $04 blocks the loop while UpdateBattleTime keeps running from the graphics
    // program's frame wait (:392-439).
    battle::NullExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), executor);
    battle.takeRequests();
    const std::uint16_t turnsBefore = battle.state().turnCounter;
    const std::uint8_t rngBefore = battle.state().rng.position();
    for (int frame = 0; frame < 20; ++frame) {
        battle.tick(BattleInput{.timeRunning = true, .presentationIdle = false});
    }
    EXPECT_EQ(battle.state().turnCounter, turnsBefore + 10);  // one time update every two frames
    // The loop took no step: its once-a-step advance of the random numbers never ran, and only the
    // time updates' own draws (none in ten updates of an opening battle) could move it.
    EXPECT_EQ(battle.state().rng.position(), rngBefore);
}

TEST(BattleSim, StoppedTimeHoldsTheGaugesButTheLoopStillSteps) {
    battle::NullExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), executor);
    const std::uint16_t turnsBefore = battle.state().turnCounter;
    const std::uint8_t rngBefore = battle.state().rng.position();
    battle.tick(BattleInput{.timeRunning = false, .presentationIdle = true});
    battle.tick(BattleInput{.timeRunning = false, .presentationIdle = true});
    EXPECT_EQ(battle.state().turnCounter, turnsBefore);
    EXPECT_NE(battle.state().rng.position(), rngBefore);
}

TEST(BattleSim, AScriptedBattleToAWin) {
    HitsExecutor executor;
    executor.partyDamage = 9999;
    executor.monsterDamage = 1;
    game::Party party = threeMembers();
    BattleSim battle(startFor(FormationId::LOBO), party, {}, noNames(), executor);
    const auto log = playOut(battle, 6000);
    ASSERT_TRUE(battle.result().has_value());
    EXPECT_TRUE(std::holds_alternative<battle::BattleWon>(battle.result()->outcome));
    ASSERT_EQ(executor.ends.size(), 1u);

    // The request log: the scene opens, menus open as gauges fill, every turn plays its script,
    // and the scene closes last.
    std::vector<std::string> names;
    std::string sequence;
    for (const auto& request : log) {
        names.push_back(nameOf(request));
        sequence += names.back() + " ";
    }
    RecordProperty("requests", sequence);
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names.front(), "OpenBattleScene");
    EXPECT_EQ(names.back(), "CloseBattleScene");
    const auto firstMenu = std::find(names.begin(), names.end(), "OpenBattleMenu");
    ASSERT_NE(firstMenu, names.end());
    EXPECT_NE(std::find(firstMenu, names.end(), "RunBattleScript"), names.end());

    // The party is handed back with the HP the monsters' hits left.
    const BattleState& state = battle.state();
    for (int position = 0; position < 3; ++position) {
        const battle::Battler& member = state.at(BattlerSlot::character(position));
        EXPECT_EQ(battle.result()->party.characters[*member.record].hp, member.hp);
    }
}

TEST(BattleSim, AScriptedBattleToALoss) {
    HitsExecutor executor;
    executor.partyDamage = 0;
    executor.monsterDamage = 9999;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), executor);
    const auto log = playOut(battle, 20000);
    ASSERT_TRUE(battle.result().has_value());
    const auto* lost = std::get_if<battle::BattleLost>(&battle.result()->outcome);
    ASSERT_NE(lost, nullptr);
    EXPECT_EQ(lost->message, battle::kMessageAnnihilated);
    EXPECT_TRUE(battle.result()->switches.isSet(game::kPartyDefeatedSwitch));
    EXPECT_EQ(nameOf(log.back()), "CloseBattleScene");
    for (int position = 0; position < 3; ++position) {
        const auto& member = battle.state().at(BattlerSlot::character(position));
        EXPECT_TRUE(battle.result()->party.characters[*member.record].status.has(StatusId::DEAD));
    }
}

TEST(BattleSim, AWonFinalBattleStageScrollsOnToTheNext) {
    // CheckFinalBattle :12245-12262 into BattleEnd_06 :12204-12239: the battle does not end; the
    // background scrolls, and the next stage arrives with its own script.
    HitsExecutor executor;
    executor.partyDamage = 65535;
    executor.monsterDamage = 0;
    battle::BattleStart start = startFor(kFinalBattleFormations[0]);
    start.finalBattleOrder = {0, 1, 2};
    BattleSim battle(start, threeMembers(), {}, noNames(), executor);
    std::vector<std::string> names;
    std::vector<battle::PlayerAction> answers;
    for (int frame = 0; frame < 20000 && battle.state().formation == kFinalBattleFormations[0]; ++frame) {
        battle.tick(BattleInput{.playerActions = std::exchange(answers, {})});
        for (BattleRequest& request : battle.takeRequests()) {
            if (const auto* menu = std::get_if<battle::OpenBattleMenu>(&request)) {
                answers.push_back({.character = menu->character, .command = BattleCommandId::FIGHT,
                                   .targets = battle.state().alive.monstersOnly()});
            }
            names.push_back(nameOf(request));
        }
    }
    EXPECT_FALSE(battle.result().has_value());
    EXPECT_EQ(battle.state().formation, kFinalBattleFormations[1]);
    ASSERT_GE(names.size(), 2u);
    std::string tail;
    for (std::size_t i = names.size() > 6 ? names.size() - 6 : 0; i < names.size(); ++i) tail += names[i] + " ";
    EXPECT_EQ(names[names.size() - 2], "ScrollFinalBattleBackground") << tail;
    EXPECT_EQ(names.back(), "RunBattleScript");
    ASSERT_EQ(executor.ends.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<battle::BattleEnded>(executor.ends[0]));
}

TEST(BattleSim, ThePlayersChoicesComeInWithTheTick) {
    battle::NullExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), executor);
    battle.tick(BattleInput{.playerActions = {{.character = BattlerSlot::character(1),
                                               .command = BattleCommandId::FIGHT}}});
    EXPECT_TRUE(battle.state().chosenActions.empty());
    ASSERT_EQ(battle.state().at(BattlerSlot::character(1)).actions.size(), 1u);
    EXPECT_EQ(std::get<CommandAction>(battle.state().at(BattlerSlot::character(1)).actions[0]).command,
              BattleCommandId::FIGHT);
}

TEST(BattleSim, NothingRunsOnceTheBattleIsOver) {
    HitsExecutor executor;
    executor.partyDamage = 9999;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), executor);
    playOut(battle, 6000);
    ASSERT_TRUE(battle.result().has_value());
    const std::uint16_t turns = battle.state().turnCounter;
    battle.tick(BattleInput{});
    EXPECT_EQ(battle.state().turnCounter, turns);
    EXPECT_TRUE(battle.takeRequests().empty());
}

}  // namespace
}  // namespace ostinato::test
