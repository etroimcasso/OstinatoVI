// The cells a special effect is compared by, and the attack's record put into them: shared with the
// target effect, the cover, the whole attack and the AI, in namespace detail.
#pragma once

#include <map>
#include <string>

#include "data/attack_properties.h"

namespace ostinato::trace::detail {

// The cells both sides are read into, by name.
struct EffectCells {
    std::map<std::string, std::string> cells;
    void put(const std::string& name, const std::string& value) { cells[name] = value; }
    void put(const std::string& name, int value) { cells[name] = std::to_string(value); }
};

void putRecord(EffectCells& out, const AttackProperties& record);

}  // namespace ostinato::trace::detail
