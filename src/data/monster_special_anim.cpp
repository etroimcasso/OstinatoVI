#include "data/monster_special_anim.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The 384-entry table. The generated rows carry the ROM data; each entry
// carries its identity as the MonsterId enumerator and its value as the
// MonsterAttackAnimation enumerator.
constexpr std::array<MonsterSpecialAnimEntry, 384> kMonsterSpecialAnims = {{
#include "data/generated/monster_special_anim_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal
// its array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kMonsterSpecialAnims.size(); ++i) {
        if (static_cast<std::size_t>(kMonsterSpecialAnims[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kMonsterSpecialAnims entry id fields must match array positions");

}  // namespace

MonsterAttackAnimation monsterSpecialAnim(MonsterId id) {
    const auto raw = static_cast<std::size_t>(id);
    assert(raw < kMonsterSpecialAnims.size() && "monster id out of range");
    return kMonsterSpecialAnims[raw].specialAnim;
}

std::span<const MonsterSpecialAnimEntry> monsterSpecialAnims() {
    return kMonsterSpecialAnims;
}

}  // namespace ostinato
