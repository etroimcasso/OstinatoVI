// The draws of a script's target, variable and switch: shared with the AI's commands and the
// battle's own actions, in namespace detail.
#pragma once

#include <random>

#include "data/ai_scripts.h"

#include "cells/ai/ai.h"
#include "scenarios/common/draws.h"

namespace ostinato::trace::detail {

using namespace cells;

AiTarget anyAiTarget(std::mt19937& rng);

AiVariable anyAiVariable(std::mt19937& rng);

AiSwitch anyAiSwitch(std::mt19937& rng);

}  // namespace ostinato::trace::detail
