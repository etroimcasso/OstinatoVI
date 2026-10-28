#include "battle/battle_sim.h"

#include <iterator>
#include <utility>
#include <variant>

#include "battle/battle_core.h"

namespace ostinato::battle {

BattleSim::BattleSim(const BattleStart& start, game::Party party, game::BattleSwitches switches,
                     const TextCorpus& text, const MonsterSprites& sprites, ActionExecutor& executor)
    : start_(start),
      text_(text),
      sprites_(sprites),
      executor_(executor),
      state_(startBattle(start, std::move(party), switches, text, sprites, executor)) {}

void BattleSim::tick(const BattleInput& input) {
    if (result_) {
        return;
    }
    state_.chosenActions.insert(state_.chosenActions.end(), input.playerActions.begin(), input.playerActions.end());
    tickBattleTime(state_, input.timeRunning, executor_);
    if (input.presentationIdle) {
        if (const auto outcome = stepBattle(state_, executor_)) {
            finish(*outcome);
        }
    }
}

std::vector<BattleRequest> BattleSim::takeRequests() {
    gatherRequests();
    return std::exchange(requests_, {});
}

void BattleSim::gatherRequests() {
    requests_.insert(requests_.end(), std::make_move_iterator(state_.requests.begin()),
                     std::make_move_iterator(state_.requests.end()));
    state_.requests.clear();
}

void BattleSim::finish(const BattleOutcome& outcome) {
    executor_.playBattleEnd(state_, outcome);
    BattleResult handedBack = writeBack(state_);
    const auto* ended = std::get_if<BattleEnded>(&outcome);
    if (ended != nullptr && ended->reason == BattleEndReason::FinalBattleNextStage && ended->nextFormation) {
        state_.requests.push_back(ScrollFinalBattleBackground{});
        gatherRequests();  // this stage's requests stay ahead of the next stage's
        state_ = startNextFinalBattleStage(state_, handedBack, start_, text_, sprites_, executor_);
        return;
    }
    state_.requests.push_back(CloseBattleScene{});
    result_ = std::move(handedBack);
}

}  // namespace ostinato::battle
