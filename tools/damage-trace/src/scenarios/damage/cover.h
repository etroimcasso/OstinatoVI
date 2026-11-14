// Setting up the attack's animation (InitGfxParams) and a True Knight's or a Love Token's cover
// (CoverEffect): what each leaves, the port's answer, the comparison and the cover sweep's draw.
#pragma once

#include <map>
#include <random>
#include <string>

#include "scenarios/damage/damage.h"

namespace ostinato::trace {

// What setting up the attack's animation leaves (InitGfxParams), as the same cells on both sides,
// each named and given as text: the params block, the three aiming rules it can add, and whether the
// casting swirl is still suppressed.
struct ParamsInitResult {
    std::map<std::string, std::string> cells;
};

// The port's setting up of the scenario's animation (battle::initParams), read back as a
// ParamsInitResult.
[[nodiscard]] ParamsInitResult portInitParams(DamageScenario scenario);

[[nodiscard]] std::string differences(const ParamsInitResult& port, const ParamsInitResult& guest);

// What a True Knight's or a Love Token's cover leaves (CoverEffect), as the same cells on both sides,
// each named and given as text: the targets, the ally covered and the battlers stepping in.
struct CoverResult {
    std::map<std::string, std::string> cells;
};

// The port's cover over the scenario (battle::coverTarget), read back as a CoverResult.
[[nodiscard]] CoverResult portCoverTarget(DamageScenario scenario);

[[nodiscard]] std::string differences(const CoverResult& port, const CoverResult& guest);

// The scenarios the cover sweep draws, from the same seed: every cell the target-effect sweep draws,
// then for every battler its statuses cleared one time in two, Near Fatal and the True Knight's relic
// each one time in two, and one time in six a Control link on it (ending one time in two) or else one
// time in six a Seize hold on it; and targets for the blow three times in four. Nothing has been
// bounced off anyone, no one has stepped in yet and no ally is covered.
[[nodiscard]] DamageScenario drawCoverScenario(std::mt19937& rng);

}  // namespace ostinato::trace
