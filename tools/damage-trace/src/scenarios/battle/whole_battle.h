// A whole battle: what it starts from and how its graphics program spends time, the cartridge's battle
// program run on it loop step by loop step, and the port's BattleSim run on the same.
//
//     const BattleScenario scenario{.start = start, .model = drawPresentationModel(seed, kFrameCap)};
//     const WholeBattleResult cartridge = oracle.wholeBattle(scenario);   // the cartridge's battle
//     const PortBattle port = portBattle(scenario, text, sprites);        // the port's
//     const BattleCheck check = oracle.checkBattle(scenario);             // both, compared
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "battle/battle_requests.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "ostinato/status_set.h"

#include "scenarios/battle/presentation_model.h"
#include "scenarios/battle/start.h"
#include "scenarios/ending/ending.h"

namespace ostinato::trace {

// The most frames a whole battle runs before it is called a failed draw.
inline constexpr std::uint64_t kBattleFrameCap = 20'000;

// What a scenario lays onto both sides once, as the battle's first loop step is reached and before that
// step is compared: statuses added to battlers, and battlers' HP. It brings into a battle what no record or
// equipment can — Berserk, Muddled — and cuts the monsters' HP so a battle is short. A status is laid with
// what its set handler leaves beside its bit: Muddled draws the battler facing the other way and marks a
// party member's statuses changed (SetStatus_0d battle_main.asm:11366-11383); Berserk's handler leaves
// nothing (SetStatusNoEffect). A morph under way is laid as UpdateMorph leaves a member it has just
// morphed (:1679-1756): Morph's status, the member as the one morphed ($3EE2), Morph and Revert traded in
// their battle commands ($202E), and the meter and its speed ($3F30, $3F32), here of the scenario's
// choosing.
struct MorphLaid {
    battle::BattlerSlot member;
    std::uint16_t counter = 0;
    std::uint16_t speed = 1;
};

struct FirstStepLaid {
    std::array<StatusSet, battle::kBattlerSlots> statusesAdded{};
    std::array<std::optional<std::uint16_t>, battle::kBattlerSlots> hp{};
    std::optional<MorphLaid> morph;
};

// `laid` laid onto the port's state.
void layFirstStep(battle::BattleState& state, const FirstStepLaid& laid);

// Bytes written into the cartridge's cells at a loop step's frame wait, once that step's cells are read: a
// cell carried into the next step set to a value of the check's choosing. Running a battle twice with a cell
// set two ways, and both runs agreeing with the port to the end, proves nothing the battle reads depends on
// what the cell held.
struct StepCellWrite {
    std::size_t step = 0;
    std::uint32_t at = 0;
    std::vector<std::uint8_t> bytes;
};

struct BattleScenario {
    StartScenario start;
    PresentationModel model;
    FirstStepLaid firstStep{};
    std::uint64_t frameCap = kBattleFrameCap;
    // Written into the cartridge's side only (DamageOracle::wholeBattle()); the port's side never sees them.
    std::vector<StepCellWrite> cartridgeWrites{};
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
    // had written since that wait, but CreateAction's copy of the targets.
    std::vector<UnwrittenRead> carriedReads;
    // CreateAction's copies of $B8-$B9 carried from the step before into an action's slot (:12960-12961):
    // the reads, and the slots whose targets hold a carried byte at this step's frame wait.
    std::vector<UnwrittenRead> carriedTargetCopies;
    std::set<std::uint8_t> slotsHoldingCarriedTargets;
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
    // The last three places of the items obtained once the battle has closed ($7E:606E-$7E:607C), the
    // tail the next battle is handed.
    battle::ItemsObtainedTail itemsObtainedTail = battle::kEmptyItemsObtainedTail;
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
    // The battle's state as it closed, once it has.
    std::optional<battle::BattleState> closed;
    std::uint64_t frames = 0;
    bool timedOut = false;
    // The battlers facing right as each frame began, one mask a frame ($2F50-$2F51's form): the facings
    // the graphics program hands the battle program on every frame (DamageOracle::wholeBattle()).
    std::vector<std::uint16_t> facings;
    // Whether a battler was in Morph as each frame began, one a frame.
    std::vector<bool> morphed;
    // The frame the wrap-up's wait begins on, once it has been asked for.
    std::optional<std::uint64_t> wrapUpWaitFrom;
};

[[nodiscard]] PortBattle portBattle(const BattleScenario& scenario, const TextCorpus& text,
                                    const MonsterSprites& sprites);

// The script command that scrolls the final battle's background between its stages (InitBattle
// :6185-6193, GfxCmd_12 btlgfx_main.asm:22118).
inline constexpr std::uint8_t kFinalBattleScrollCommand = 0x12;

// Whether a script stops battle time for all its frames, read from the cartridge's script queue
// ($7E:2D6E, four bytes a command, $FF at the end) or from the port's script: it plays a battle event
// other than Gau's ($1B) and Wrexsoul's ($1F), or, on the cartridge's side, holds the final battle's
// scroll ($12). portBattle() knows the port's scroll script as the one asked for after
// ScrollFinalBattleBackground.
[[nodiscard]] bool scriptStopsTime(const std::vector<std::uint8_t>& queue);
[[nodiscard]] bool scriptStopsTime(const battle::RunBattleScript& script);

// What a request shows, and what a graphics call shows, in the same words: the menu opened, closed or
// refreshed and whose, a script, the monsters' names, an item obtained, the scene opened, scrolled or
// closed, the fanfare or the celebration. Nothing for a frame wait ($01), the port's wrap-up wait (a
// script holding one Wait) and the items obtained merged ($0A), which have no request.
[[nodiscard]] std::optional<std::string> requestShown(const battle::BattleRequest& request);
[[nodiscard]] std::optional<std::string> callShown(const GraphicsCall& call);

// What a run of requests shows, in order. In a `won` battle's end the celebration — every menu closing,
// first slot first, then the fanfare and battle event 1 where they were asked for — shows as the
// cartridge's one call ($08) shows: "celebration".
[[nodiscard]] std::vector<std::string> requestsShown(std::span<const battle::BattleRequest> requests, bool won);

// A whole battle checked: every line where the cartridge's and the port's part, empty when they agree.
struct BattleCheck {
    std::string differences;
    WholeBattleResult cartridge;
    PortBattle port;
};

}  // namespace ostinato::trace
