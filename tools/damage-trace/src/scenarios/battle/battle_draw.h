// The whole battles the battle sweep draws, aimed at the rows of its reach table that no odds reach, and
// the table of what each one took and how it ended.
//
//     std::mt19937 rng{seed};
//     const AimedBattle drawn = drawBattle(rng, text, sprites);
//     const BattleCheck check = oracle.checkBattle(drawn.scenario);
//     for (const std::string_view row : battleReached(drawn.scenario, check)) ++counts[row];
//
//     const AimedBattle terra = drawAimedBattleScenario(rng, text, sprites, BattleAim::TerrasMagicEvent);
//     if (!terra.reached) { /* no try of kAimTries reached it: terra.scenario is the last drawn */ }
#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "data/monster_sprites.h"
#include "data/text_corpus.h"

#include "scenarios/battle/whole_battle.h"

namespace ostinato::trace {

// What a drawn battle is aimed at: a row of the reach table that hangs on the cartridge's own roll or on a
// chain no odds make likely, or Terra's Magic starting her battle event.
enum class BattleAim : std::uint8_t {
    None,
    Dancing,           // a Muddled member who holds Dance, whose chooser picks it
    GauTurnsUp,        // a won battle on the Veldt that Gau comes back from
    ShadowWalksOff,    // a won battle that Shadow leaves the party after
    TerrasMagicEvent,  // a Muddled Terra casting Magic while battle switch 65 calls for her event
};

// The most times an aimed battle is drawn again before the last one drawn is taken as it is.
inline constexpr int kAimTries = 128;

// A battle drawn for an aim, and whether the port's own battle over it reached what it was aimed at.
struct AimedBattle {
    BattleScenario scenario;
    BattleAim aim = BattleAim::None;
    bool reached = false;
};

// One whole battle with no player, drawn to end. The start is drawStartScenario()'s; then every party
// member seated is one of — Berserk or Muddled (laid at the first loop step, FirstStepLaid), Umaro, or a
// member whose menu opens and is never answered — or the start is a colosseum match, where the member
// fights on their own. A Berserk member holds Rage among their commands now and then, and a Muddled one
// Rage or Dance, so the battle begins a rage or a dance for them itself. The party's HP is cut now and
// then, and every monster's HP is cut, so a battle is a few hundred frames. The frame model is
// drawPresentationModel()'s, with a story countdown now and then, and the run buttons held from a drawn
// frame one battle in eight. One battle in ten is aimed at each of a dancing member, Gau turning up and
// Shadow walking off (drawAimedBattleScenario()).
[[nodiscard]] AimedBattle drawBattle(std::mt19937& rng, const TextCorpus& text, const MonsterSprites& sprites);

// drawBattle()'s battle alone.
[[nodiscard]] BattleScenario drawBattleScenario(std::mt19937& rng, const TextCorpus& text,
                                                const MonsterSprites& sprites);

// One battle drawn as drawBattle() draws one and given what `aim` needs, then drawn again whole — every
// draw from `rng`, in order — until the port's own battle over it reaches the aim, at most kAimTries times.
// The first that reaches it is the battle; when none does, the last drawn is, with `reached` false. What
// each aim is given:
//   - a dancing member: the first record of the party played Muddled, holding Dance and no Rage;
//   - Gau turning up: every member Berserk and the monsters' HP cut further, in a start where Gau may
//     appear (a start of another kind is a try spent);
//   - Shadow walking off: the first record of the party played is Shadow, free to walk off at a win
//     (BattleStart::shadowAvailable set, the switch that keeps him clear), every member Berserk and the
//     monsters' HP cut further;
//   - Terra's Magic and her event: one record of the party played is Terra, Muddled, holding Magic and a
//     spell she can pay for, with battle switch 65 set.
// BattleAim::None draws once.
[[nodiscard]] AimedBattle drawAimedBattleScenario(std::mt19937& rng, const TextCorpus& text,
                                                  const MonsterSprites& sprites, BattleAim aim);

// The reach table's row an aim is for; Terra's event, which is no row of it, is named as its own.
[[nodiscard]] std::string_view aimName(BattleAim aim);

// One line naming what a drawn battle is: the start (describe()), each member's part, and whether the
// run buttons are held.
[[nodiscard]] std::string describe(const BattleScenario& scenario);

// The rows of the battle sweep's reach table, in the order it prints them: each part a member took, then
// each way a battle ended — won, lost, fled, a final stage scrolled on, Gau turning up, Shadow walking
// off, a story countdown running out, and an ending a battle event or the story chose.
[[nodiscard]] std::span<const std::string_view> battleReachRows();

// The rows `check`'s battle reached, from the scenario and the port's battle (the two sides having been
// found the same, or the port's side alone).
[[nodiscard]] std::vector<std::string_view> battleReached(const BattleScenario& scenario, const BattleCheck& check);

}  // namespace ostinato::trace
