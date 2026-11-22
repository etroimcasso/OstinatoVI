// A battle driven from a frame tick, from its start to the party being handed back.
//
// BattleSim owns one battle and runs both of its clocks from the host's frame. Each frame the host
// passes what its presentation knows — whether battle time runs this frame (an attack animation
// lets it run; a battle event or the opening animation stops it), whether the presentation is idle
// (no battle script still playing), and any commands the player finished choosing — and takes the
// requests the battle made:
//
//     BattleSim battle(start, party, switches, cartridge.text, cartridge.monsterSprites, executor);
//     while (!battle.result()) {
//         battle.tick({.timeRunning = presentation.timeRuns(),
//                      .presentationIdle = presentation.idle(),
//                      .playerActions = menu.finishedActions()});
//         for (const BattleRequest& request : battle.takeRequests()) presentation.handle(request);
//     }
//     party = battle.result()->party;
//
// Battle time advances on every tick; the battle loop takes its step only on a tick the
// presentation is idle. Once the battle has an outcome the loop takes no more steps: on that tick and
// on every idle tick after it, the executor plays the battle's end on to the next script it asks for
// (ActionExecutor::continueBattleEnd()), so the end's scripts take their frames with battle time
// running through them. When the end has nothing left to play, the party is handed back and the scene
// closes — except for a won stage of the final battle, which scrolls on to the next stage, its play
// clock and frame count counting on from where the end left them, and keeps going. result() stays
// empty until then.
#pragma once

#include <optional>
#include <vector>

#include "battle/action_executor.h"
#include "battle/battle_requests.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/player_intake.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"

namespace ostinato::battle {

// What the presentation tells the battle each frame.
struct BattleInput {
    bool timeRunning = true;
    bool presentationIdle = true;
    std::vector<PlayerAction> playerActions;
};

class BattleSim {
public:
    // `text`, `sprites` and `executor` must outlive the BattleSim.
    BattleSim(const BattleStart& start, game::Party party, game::BattleSwitches switches, const TextCorpus& text,
              const MonsterSprites& sprites, ActionExecutor& executor);

    // Run one frame. Does nothing once the battle is over.
    void tick(const BattleInput& input);

    // The requests made since they were last taken, in order.
    std::vector<BattleRequest> takeRequests();

    BattleState& state() { return state_; }
    const BattleState& state() const { return state_; }

    // What the game keeps from the battle, once it is over: empty until the battle's end has played its
    // last script.
    const std::optional<BattleResult>& result() const { return result_; }

private:
    void finish(const BattleOutcome& outcome);
    void gatherRequests();

    BattleStart start_;
    const TextCorpus& text_;
    const MonsterSprites& sprites_;
    ActionExecutor& executor_;
    BattleState state_;
    std::vector<BattleRequest> requests_;
    std::optional<BattleResult> result_;
};

}  // namespace ostinato::battle
