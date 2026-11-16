// A battle's start: what the field sets before it calls the battle program, the battle's cells and
// what the cartridge's start leaves, the comparison of two scenarios and the port's start.
#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "battle/battle_inventory.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/items_obtained.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "game/battle_switches.h"

#include "scenarios/battle/battle_cells.h"
#include "scenarios/ending/rewards.h"

namespace ostinato::trace {

// Everything a battle's start reads that the field sets before it calls the battle program: what
// battle::startBattle() takes, with the party as the cartridge keeps it in its cells.
struct StartScenario {
    battle::BattleStart start;
    PartyCells cells;
    game::BattleSwitches switches;
};

// The battle's cells, $7E:0000-3FFF, one byte per cell.
using BattleCells = std::array<std::uint8_t, 0x4000>;

// The battle program's start as the cartridge made it: its cells at the loop's first frame wait, the
// graphics calls up to there, and whether the call reached that wait and then returned.
struct StartResult {
    BattleCells cells{};
    std::vector<GraphicsCall> calls;
    bool reachedTheLoop = false;
    bool returned = false;
    // Every cell the start read before anything wrote it, up to the loop's first frame wait (as
    // DamageOracle::unwrittenReads() lists them).
    std::vector<UnwrittenRead> unwrittenReads;
    // Every cell of $7E:0000-3FFF the battle program wrote up to the loop's first frame wait.
    std::set<std::uint32_t> battleWrites;
    // Every read of $7E:0000-3FFF the battle program made after the loop's first frame wait of a cell
    // nothing had written since, with the byte it found: what the comparison point carried on.
    std::vector<UnwrittenRead> readsAfterThePoint;
    // Every read the battle program made of $7E:4000-$7F:FFFF that is not on kCellsNotCompared, with
    // the byte it found.
    std::vector<UnwrittenRead> readsBeyondTheBattlesCells;
};

// A start checked against the cartridge (DamageOracle::checkStart()): every line where the two starts
// part, empty when they agree, with the cartridge's start and the port's.
struct StartCheck {
    std::string differences;
    StartResult cartridge;
    battle::BattleState port;
};

// Every field where two start scenarios differ, one per line naming it and both values; empty when
// they agree.
[[nodiscard]] std::string differences(const StartScenario& a, const StartScenario& b);

// The port's start over `scenario` (battle::startBattle()), with the game's text and the monsters'
// sprites the start reads.
[[nodiscard]] battle::BattleState portStart(const StartScenario& scenario, const TextCorpus& text,
                                            const MonsterSprites& sprites);

}  // namespace ostinato::trace
