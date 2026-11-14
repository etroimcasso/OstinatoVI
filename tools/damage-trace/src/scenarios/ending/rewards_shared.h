// The party as the cartridge's cells and the name of each cell, the win's flags as their byte, and
// the draws of an item, the party and the experience a level needs: shared with the battle's start
// and end and the cartridge's side of the rewards, in namespace detail.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "ostinato/item_id.h"

#include "cells/damage/frame.h"
#include "cells/ending/party.h"
#include "cells/ending/records.h"
#include "cells/status/effects.h"
#include "scenarios/common/draws.h"
#include "scenarios/ending/records_shared.h"
#include "scenarios/ending/rewards.h"

namespace ostinato::trace::detail {

using namespace cells;

// One cell of the party's image: where it is and what it holds.
struct PartyCell {
    std::uint32_t at = 0;
    std::uint8_t value = 0;
};

// Every cell the party's image fills, in address order: the sixteen records, their party bytes, the
// gil, the spells the first twelve characters know, the Morph supply, the SwdTechs, the Blitzes, the
// lores, the rages and the dances known.
std::vector<PartyCell> partyImage(const PartyCells& cells);

// The name of the party's cell `at`, for a report.
std::string partyCellName(std::uint32_t at);

std::uint8_t winFlagsByte(const WinFlags& flags);

ItemId drawItem(std::mt19937& rng);

// The experience a character at `level` (1-98) needs for its next level.
std::uint32_t experienceForNextLevel(std::uint8_t level);

PartyCells drawPartyCells(std::mt19937& rng);

}  // namespace ostinato::trace::detail
