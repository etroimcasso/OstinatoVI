#include "render/battle/battle_placement.h"

#include <algorithm>
#include <cstdint>

#include "data/battle_gfx_tables.h"

namespace ostinato::render {

MonsterStanding monsterStanding(battle::BattleType type, int formationX, int formationY,
                                MonsterSpriteSize size, std::uint8_t overlap) {
    const battle::MonsterPlacement across = battle::placeMonster(type, formationX, size.width);
    return MonsterStanding{
        .x = across.left,
        .y = formationY,
        .size = size,
        .mirrored = across.facing == battle::Facing::Left,
        .bottomEdge = formationY + size.height * kMonsterTilePx - kMonsterBottomEdgeRaisePx + overlap,
    };
}

MonsterStanding colosseumMonsterStanding(MonsterSpriteSize size, MonsterVerticalAlignment alignment,
                                         std::uint8_t overlap) {
    const int height = size.height * kMonsterTilePx;
    const int width = size.width * kMonsterTilePx;
    const int y = alignment == MonsterVerticalAlignment::CEILING
                      ? 0
                      : kColosseumGroundPx - height + colosseumAlignOffset(alignment);
    return MonsterStanding{
        .x = (kColosseumFieldWidthPx - width) / 2 + kColosseumLeftMarginPx,
        .y = y,
        .size = size,
        .mirrored = false,
        .bottomEdge = y + height - kMonsterBottomEdgeRaisePx + overlap,
    };
}

int partyMemberX(int angle, int y) {
    // The original keeps the angle in nine bits, the distance in eight, and the scaled lean in
    // sixteen, complementing a leftward lean on both sides of the scaling.
    const int lean = (angle & 0x1FF) - kAngleStraightDown;
    const int distance = (y + kPartyApexAboveScreenPx) & 0xFF;
    if (lean >= 0) {
        return kPartyCenterXPx + ((lean * distance) >> 8);
    }
    const int scaled = ((~lean & 0xFFFF) * distance) >> 8;
    return kPartyCenterXPx + static_cast<std::int16_t>(~scaled);
}

PartyStanding partyMemberStanding(battle::BattleType type, std::size_t slot, bool backRow,
                                  bool actsAsEnemy, bool magitekTrain, bool colosseum) {
    const PartySlotPlacement& place = partyPlacement(static_cast<std::size_t>(type))[slot];
    int y = magitekTrain ? magitekTrainPartyY(slot) : place.y;
    if (colosseum && slot == 0) {
        y = kColosseumFighterY;
    }
    const int angle = place.angle + (backRow ? place.backRowAngle : 0);
    return PartyStanding{
        .x = partyMemberX(angle, y),
        .y = y,
        .mirrored = place.mirrored != actsAsEnemy,
        .bottomEdge = y + kCharacterBottomEdgePx,
    };
}

std::vector<battle::BattlerSlot> battlerDrawOrder(
    const std::array<std::optional<int>, battle::kBattlerSlots>& bottomEdges) {
    std::vector<battle::BattlerSlot> order;
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        if (bottomEdges[static_cast<std::size_t>(index)]) {
            order.push_back(battle::BattlerSlot::fromIndex(index));
        }
    }
    // Back to front is ascending by bottom edge; level feet keep slot order with the earlier slot in
    // front, so among equals the later slot comes first.
    std::stable_sort(order.begin(), order.end(), [&](battle::BattlerSlot a, battle::BattlerSlot b) {
        const int edgeA = *bottomEdges[static_cast<std::size_t>(a.index())];
        const int edgeB = *bottomEdges[static_cast<std::size_t>(b.index())];
        if (edgeA != edgeB) {
            return edgeA < edgeB;
        }
        return a.index() > b.index();
    });
    return order;
}

}  // namespace ostinato::render
