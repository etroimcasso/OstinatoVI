#include "battle/links.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "data/attack_properties.h"
#include "data/monster_attacks.h"
#include "ostinato/attack_id.h"
#include "ostinato/monster_id.h"

namespace ostinato::battle {

namespace {

// The MP the comparison wraps at: one more than this reads as none.
constexpr std::uint16_t kMpWordMax = 0xFFFF;

}  // namespace

void controlMenu(BattleState& state, BattlerSlot controller, BattlerSlot monster) {
    const Battler& held = state.at(monster);
    const MonsterControl& attacks = getMonsterControl(held.monster.value_or(MonsterId{}));
    std::array<ControlMenuEntry, 4>& menu = state.at(controller).controlMenu;
    for (std::size_t row = 0; row < menu.size(); ++row) {
        const AttackId attack = attacks.attacks[row];
        if (attack == AttackId::NONE) {
            menu[row] = ControlMenuEntry{};
            continue;
        }
        const AttackProperties& record = getAttackProperties(attack);
        menu[row] = ControlMenuEntry{
            .attack = attack,
            .castable = held.mp != kMpWordMax && record.mpCost <= held.mp,
            .targeting = record.targeting,
        };
    }
}

}  // namespace ostinato::battle
