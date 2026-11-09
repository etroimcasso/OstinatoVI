#include "battle/spell_learning.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "data/esper_properties.h"

namespace ostinato::battle {

namespace {

// A spell at this percentage or more is learned.
constexpr int kLearnedPercent = 100;

}  // namespace

bool addSpellProgress(game::SpellProgress& progress, AttackId spell, std::uint8_t learnRate,
                      std::uint8_t magicPoints) {
    if (learnRate == 0) {
        return false;
    }
    const auto index = static_cast<std::size_t>(spell);
    assert(index < game::kLearnableSpells && "only the 54 learnable spells have a learned percentage");
    std::uint8_t& learned = progress.progress[index];
    if (learned == game::SpellProgress::kSpellLearned) {
        return false;
    }
    // The amount is the product's low byte: a rate of 30 with 10 magic points adds 44, not 300.
    const auto amount = static_cast<std::uint8_t>(learnRate * magicPoints);
    const int sum = learned + amount;
    learned = sum >= kLearnedPercent ? game::SpellProgress::kSpellJustLearned : static_cast<std::uint8_t>(sum);
    return true;
}

bool learnFromEsper(game::SpellProgress& progress, EsperId esper, std::uint8_t magicPoints) {
    bool progressed = false;
    for (const EsperSpell& slot : getEsperProperties(esper).spells) {
        if (slot.spell == AttackId::NONE) {
            continue;
        }
        progressed |= addSpellProgress(progress, slot.spell, slot.learnRate, magicPoints);
    }
    return progressed;
}

}  // namespace ostinato::battle
