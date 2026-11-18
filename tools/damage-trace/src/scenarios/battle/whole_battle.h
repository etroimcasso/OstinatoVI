// A whole battle: what it starts from and how its graphics program spends time, the cartridge's battle
// program run on it loop step by loop step, and the port's BattleSim run on the same.
//
//     const BattleScenario scenario{.start = start, .model = drawPresentationModel(seed, kFrameCap)};
//     const WholeBattleResult cartridge = oracle.wholeBattle(scenario);   // the cartridge's battle
//     const PortBattle port = portBattle(scenario, text, sprites);        // the port's
//     const BattleCheck check = oracle.checkBattle(scenario);             // both, compared
#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "battle/battle_requests.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"

#include "scenarios/battle/presentation_model.h"
#include "scenarios/battle/start.h"
#include "scenarios/ending/ending.h"

namespace ostinato::trace {

// The most frames a whole battle runs before it is called a failed draw.
inline constexpr std::uint64_t kBattleFrameCap = 20'000;

struct BattleScenario {
    StartScenario start;
    PresentationModel model;
    std::uint64_t frameCap = kBattleFrameCap;
};

// One loop step as the cartridge ran it, read at the step's frame wait (BattleLoop's graphics command $01,
// battle/battle_main.asm:72-73) before the frame runs.
struct CartridgeStep {
    BattleCells cells{};
    std::uint64_t frame = 0;     // the frames run before this one
    std::size_t firstCall = 0;   // this step's frame wait among WholeBattleResult::calls
    // Every cell of $7E:0000-3FFF the battle program wrote from the step before's frame wait to this one.
    std::set<std::uint32_t> writes;
    // Every read the battle program made from the step before's frame wait to this one of a cell nothing
    // had written since that wait.
    std::vector<UnwrittenRead> carriedReads;
    // The stack's two pages ($7E:0100-01FF, used by a battle called from the reset code, then $7E:1500-15FF,
    // used by one the field calls) as they stood at the first of those reads the carried-read gate reports,
    // a read of a cell wholly on kCellsNotCompared: the returns on them name the routine reading.
    std::vector<std::uint8_t> stackAtFirstCarriedRead;
    // The trace's last line before that read, when the oracle was tracing (DamageOracle::trace()).
    std::string tracedBeforeFirstCarriedRead;
    // The battle program's accesses just before that read, oldest first, each "r" or "w" with its cell
    // and byte: the cells next to it name the routine reading.
    std::vector<std::string> accessesBeforeFirstCarriedRead;
};

// The cartridge's whole battle: every graphics call in order, every loop step, and the cells at the end.
struct WholeBattleResult {
    std::vector<GraphicsCall> calls;
    std::vector<CartridgeStep> steps;
    // The battle reached TerminateBattle (battle/battle_main.asm:150-154) after its graphics command $09.
    bool ended = false;
    // The frame cap passed before the battle ended.
    bool timedOut = false;
    std::uint64_t frames = 0;
    // The cells when the battle ended, after what the game keeps was written (UpdateSRAM), and what the
    // game keeps read from them; the Veldt's battles as they stood before the battle.
    BattleCells endCells{};
    WriteBackResult kept;
    std::bitset<512> veldtBattlesBefore;
    // The stack pointer at every arrival at the loop's head and at TerminateBattle: one value when every
    // step left the stack as it found it.
    std::set<std::uint16_t> stackPointers;
    // The battle's start's cells before anything wrote them, and its reads past $7E:3FFF.
    std::vector<UnwrittenRead> unwrittenReads;
    std::vector<UnwrittenRead> readsBeyondTheBattlesCells;
};

// The port's whole battle over the same scenario: its state at each loop step's frame wait, before the
// frame runs, and every request in order.
struct PortBattle {
    std::vector<battle::BattleState> steps;
    std::vector<std::uint64_t> stepFrames;
    // Each step's first request among `requests`: the requests made from its frame wait on.
    std::vector<std::size_t> stepRequests;
    std::vector<battle::BattleRequest> requests;
    std::optional<battle::BattleResult> result;
    std::uint64_t frames = 0;
    bool timedOut = false;
};

[[nodiscard]] PortBattle portBattle(const BattleScenario& scenario, const TextCorpus& text,
                                    const MonsterSprites& sprites);

// Whether a script stops battle time for all its frames, read from the cartridge's script queue
// ($7E:2D6E, four bytes a command, $FF at the end) or from the port's script: it plays a battle event
// other than Gau's ($1B) and Wrexsoul's ($1F).
[[nodiscard]] bool scriptStopsTime(const std::vector<std::uint8_t>& queue);
[[nodiscard]] bool scriptStopsTime(const battle::RunBattleScript& script);

// What a request shows, and what a graphics call shows, in the same words: the menu opened, closed or
// refreshed and whose, a script, the monsters' names, an item obtained, the scene opened, scrolled or
// closed, the fanfare or the celebration. Nothing for a frame wait ($01) and the items obtained merged
// ($0A), which have no request.
[[nodiscard]] std::string requestShown(const battle::BattleRequest& request);
[[nodiscard]] std::optional<std::string> callShown(const GraphicsCall& call);

// A whole battle checked: every line where the cartridge's and the port's part, empty when they agree.
struct BattleCheck {
    std::string differences;
    WholeBattleResult cartridge;
    PortBattle port;
};

}  // namespace ostinato::trace
