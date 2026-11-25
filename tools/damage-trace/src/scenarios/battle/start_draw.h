// The battle starts the start sweep draws, the records a scripted setup's cast is given, and the
// table of what each start reached.
#pragma once

#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/rom_reader.h"
#include "battle/battle_state.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"

#include "scenarios/battle/start.h"

namespace ostinato::trace {

// Gives each character a scripted setup seats a record of their own, from the last records outside
// the party played, when no record holds them: the setup the formation asks for (a conditional
// battle's replacement and a randomized formation's other three included), and the Veldt's when Gau
// may appear. In play the story has created a setup's cast before its battle; a start whose seated
// character has no record loads them from past the battle's own memory and is not compared.
void giveTheCastRecords(StartScenario& scenario);

// One battle start drawn whole from `rng`: sixteen drawn records with commands the game holds, any
// items in their hands and relics, sparse statuses, any inventory, settings and battle switches, and
// one of twelve kinds of start, each set up to reach what it is named for — a colosseum match; the
// final battle's first stage; a formation with a scripted setup; one Gau may come back after, with
// room for him; a conditional battle its switch turns on; a permanent morph with a member who can
// Morph; a side attack with three or four standing; a pincer; a back attack; a Back Guard where the
// formation allows only a pincer or only a back attack; fewer than three where it allows only a side
// attack; any formation. A Back Guard and a Gale Hairpin are worn now and then. The play clock's frames
// are drawn from 1 to 60, now and then any byte; for the attacks above, and half the time otherwise,
// they are the first frames whose start the port makes that battle type (or a drawn one, or a
// preemptive strike) when the formation allows it. The cast of any setup is given records
// (giveTheCastRecords()). The byte above the random numbers' position is 0 three starts in four and 1
// to 255 otherwise, with the Chain Saw's entries it picks on `cartridge` (setByteAboveRngPosition()),
// drawn from a generator of its own so the rest of the draw is what it would be without it.
[[nodiscard]] StartScenario drawStartScenario(std::mt19937& rng, const TextCorpus& text, const MonsterSprites& sprites,
                                              const assets::HiRomImage& cartridge);

// One line naming a start scenario's formation, the party played and its members, the play clock's
// frames and what the field allows, for a report.
[[nodiscard]] std::string describe(const StartScenario& scenario);

// The rows of the start sweep's reach table, in the order it prints them: each battle type, a
// preemptive strike, the colosseum, the final battle, a scripted setup, Gau's return, an empty party
// slot, a morphed member, a randomized formation, a conditional battle, a party of fewer than three
// losing a side attack, and each branch a Back Guard and a Gale Hairpin take.
[[nodiscard]] std::span<const std::string_view> startReachRows();

// The rows `scenario`'s start reached, from the port's start and the cartridge's cells at the loop's
// first frame wait (the two the comparison found the same).
[[nodiscard]] std::vector<std::string_view> startReached(const StartScenario& scenario, const battle::BattleState& port,
                                                         const BattleCells& cartridge);

}  // namespace ostinato::trace
