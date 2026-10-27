#include "battle/battle_layout.h"

#include <variant>

#include "data/formations.h"

namespace ostinato::battle {

namespace {

void face(BattleState& state, BattlerSlot slot, Facing facing) {
    if (facing == Facing::Right) {
        state.facingRight.add(slot);
    } else {
        state.facingRight.remove(slot);
    }
}

constexpr Facing opposite(Facing facing) {
    return facing == Facing::Right ? Facing::Left : Facing::Right;
}

// The animations that show nothing an attacker could turn for.
bool turnsNoOne(BattleCommandId command) {
    return command == BattleCommandId::MIMIC || command == BattleCommandId::ROW ||
           command == BattleCommandId::DEF;
}

}  // namespace

void layOutMonsters(BattleState& state, const MonsterSprites& sprites) {
    const Formation& formation = getFormation(state.formation);
    state.pincerSides = {};
    for (int position = 0; position < kMonsterSlots; ++position) {
        const BattlerSlot slot = BattlerSlot::monster(position);
        Battler& battler = state.at(slot);
        if (!battler.monster) {
            continue;
        }
        const auto at = static_cast<std::size_t>(position);
        const std::optional<MonsterSpriteSize> box = monsterSpriteBox(formation.vramMap(), at);
        const int width = box ? sprites.size(*battler.monster, *box).width : 0;
        const MonsterPlacement placed = placeMonster(state.battleType, formation.positionX(at), width);
        battler.side = placed.side;
        face(state, slot, placed.facing);
        const auto bit = static_cast<std::uint8_t>(1u << position);
        if (placed.side == FieldSide::Left) {
            state.pincerSides[0].bits |= bit;
        } else if (placed.side == FieldSide::Right) {
            state.pincerSides[1].bits |= bit;
        }
    }
}

void layOutParty(BattleState& state) {
    const PartyLayoutRow& row = partyLayout(state.battleType);
    for (int position = 0; position < kPartySlots; ++position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        Battler& battler = state.at(slot);
        const PartyLayoutSlot& place = row.slots[static_cast<std::size_t>(position)];
        battler.side = sideOfSlope(place.slope);
        face(state, slot, battler.actsAsEnemy ? opposite(place.facing) : place.facing);
    }
}

std::optional<BattlerSlot> facedTarget(BattlerSet targets) {
    for (int position = 0; position < kMonsterSlots; ++position) {
        if (targets.has(BattlerSlot::monster(position))) {
            return BattlerSlot::monster(position);
        }
    }
    for (int position = 0; position < kPartySlots; ++position) {
        if (targets.has(BattlerSlot::character(position))) {
            return BattlerSlot::character(position);
        }
    }
    return std::nullopt;
}

void turnToFace(BattleState& state, BattlerSlot attacker, BattlerSet targets) {
    const std::optional<BattlerSlot> target = facedTarget(targets);
    if (!target || attacker.isCharacter() == target->isCharacter()) {
        return;
    }
    const bool targetToTheRight = state.at(*target).side > state.at(attacker).side;
    face(state, attacker, targetToTheRight ? Facing::Right : Facing::Left);
}

void turnToFaceTargets(BattleState& state, std::span<const BattleScriptCommand> script) {
    bool partyRanAway = false;
    const auto turn = [&](BattlerSlot attacker, BattlerSet targets) {
        if (partyRanAway && attacker.isCharacter()) {
            return;
        }
        turnToFace(state, attacker, targets);
    };

    for (const BattleScriptCommand& command : script) {
        if (const auto* stance = std::get_if<ReadyStance>(&command)) {
            state.animationTargets = stance->targets;
            turn(stance->actor, stance->targets);
            continue;
        }
        const auto* animation = std::get_if<AttackAnimation>(&command);
        if (animation == nullptr) {
            continue;
        }
        const AttackAnimationParams& params = animation->params;
        if (const auto* special = std::get_if<SpecialAnimation>(&animation->what)) {
            if (*special == SpecialAnimation::RouletteCursor) {
                continue;
            }
            if (*special == SpecialAnimation::PartyRunsAway) {
                partyRanAway = true;
            }
        } else {
            const BattleCommandId shown = std::get<BattleCommandId>(animation->what);
            if (turnsNoOne(shown)) {
                continue;
            }
            if (shown == BattleCommandId::JUMP) {
                turn(params.attacker, state.animationTargets);
                continue;
            }
        }
        state.animationTargets = params.targets;
        turn(params.attacker, params.targets);
    }
}

}  // namespace ostinato::battle
