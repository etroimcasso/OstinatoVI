#include "data/formations.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// battle_monsters: 576 formation records. Designated initializers at every
// row (through Formation::of) keep each slot self-labeling; each entry carries
// its identity as the FormationId enumerator.
constexpr std::array<FormationEntry, 576> kFormations = {{
#include "data/generated/formation_data.inc"
}};

// battle_prop: 576 aux records, one per formation.
constexpr std::array<FormationAuxEntry, 576> kFormationAux = {{
#include "data/generated/formation_aux_data.inc"
}};

// cond_battle: 16 conditional-battle substitutions (only 0-7 reachable).
constexpr std::array<ConditionalBattle, 16> kConditionalBattles = {{
#include "data/generated/cond_battle_data.inc"
}};

// Every entry's id field must equal its array position, checked at compile
// time — the emitted rows stay aligned with the FormationId space.
static_assert([] {
    for (std::size_t i = 0; i < kFormations.size(); ++i) {
        if (static_cast<std::size_t>(kFormations[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kFormations entry id fields must match array positions");

static_assert([] {
    for (std::size_t i = 0; i < kFormationAux.size(); ++i) {
        if (static_cast<std::size_t>(kFormationAux[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kFormationAux entry id fields must match array positions");

}  // namespace

const Formation& getFormation(FormationId id) {
    const auto raw = static_cast<std::size_t>(id);
    assert(raw < kFormations.size() && "formation id out of range");
    return kFormations[raw].record;
}

std::span<const FormationEntry> formations() { return kFormations; }

const FormationAux& getFormationAux(FormationId id) {
    const auto raw = static_cast<std::size_t>(id);
    assert(raw < kFormationAux.size() && "formation id out of range");
    return kFormationAux[raw].record;
}

std::span<const FormationAuxEntry> formationAux() { return kFormationAux; }

const ConditionalBattle& getConditionalBattle(std::size_t index) {
    assert(index < kConditionalBattles.size() &&
           "conditional-battle index out of range");
    return kConditionalBattles[index];
}

std::span<const ConditionalBattle> conditionalBattles() {
    return kConditionalBattles;
}

}  // namespace ostinato
