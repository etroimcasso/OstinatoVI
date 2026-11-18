// The whole battles the battle sweep draws, and the table of what each one took and how it ended.
//
//     std::mt19937 rng{seed};
//     const BattleScenario scenario = drawBattleScenario(rng, text, sprites);
//     const BattleCheck check = oracle.checkBattle(scenario);
//     for (const std::string_view row : battleReached(scenario, check)) ++counts[row];
#pragma once

#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "data/monster_sprites.h"
#include "data/text_corpus.h"

#include "scenarios/battle/whole_battle.h"

namespace ostinato::trace {

// One whole battle with no player, drawn to end. The start is drawStartScenario()'s; then every party
// member seated is one of — Berserk or Muddled (laid at the first loop step, FirstStepLaid), Umaro, or a
// member whose menu opens and is never answered — or the start is a colosseum match, where the member
// fights on their own. A Berserk member holds Rage among their commands now and then, and a Muddled one
// Rage or Dance, so the battle begins a rage or a dance for them itself. The party's HP is cut now and
// then, and every monster's HP is cut, so a battle is a few hundred frames. The frame model is
// drawPresentationModel()'s, with a story countdown now and then, and the run buttons held from a drawn
// frame one battle in eight.
[[nodiscard]] BattleScenario drawBattleScenario(std::mt19937& rng, const TextCorpus& text,
                                                const MonsterSprites& sprites);

// One line naming what a drawn battle is: the start (describe()), each member's part, and whether the
// run buttons are held.
[[nodiscard]] std::string describe(const BattleScenario& scenario);

// The rows of the battle sweep's reach table, in the order it prints them: each part a member took, then
// each way a battle ended — won, lost, fled, a final stage scrolled on, Gau turning up, Shadow walking
// off, a story countdown running out, and an ending a battle event or the story chose.
[[nodiscard]] std::span<const std::string_view> battleReachRows();

// The rows `check`'s battle reached, from the scenario and the port's battle (the two sides having been
// found the same).
[[nodiscard]] std::vector<std::string_view> battleReached(const BattleScenario& scenario, const BattleCheck& check);

}  // namespace ostinato::trace
