// Where each battler stands on the screen, and the order they are drawn in.
//
// A monster stands where its formation puts it, moved and mirrored by the battle type the way the
// battle's layout already decides (battle/battle_layout.h); a colosseum monster stands centered. A
// party member stands at the y its slot and the battle type give it, on a line that fans out from a
// point above the screen. Battlers are drawn from the one whose feet are highest on the screen to the
// one whose feet are lowest, so a battler standing lower is in front.
//
//     const MonsterStanding monster = monsterStanding(type, formation.positionX(slot),
//                                                     formation.positionY(slot), size, overlap);
//     const PartyStanding member = partyMemberStanding(type, slot, backRow, actsAsEnemy, train, false);
//     for (BattlerSlot slot : battlerDrawOrder(bottomEdges)) { ... }   // back to front
//
// Nothing here draws: the results are pixels and flags for the components to take.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "battle/battle_layout.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/monster_sprites.h"
#include "ostinato/monster_vertical_alignment.h"

namespace ostinato::render {

// --- monsters ----------------------------------------------------------------------------------

// A monster's sprite is a grid of 8-pixel tiles.
inline constexpr int kMonsterTilePx = battle::kMonsterTilePx;

// A monster's feet, for the draw order: its bottom edge less one tile, plus its own overlap shift.
inline constexpr int kMonsterBottomEdgeRaisePx = 8;

// Where a monster stands: the top-left corner of its sprite, its size in tiles, whether its art is
// mirrored (unmirrored art faces right), and where its feet are for the draw order.
struct MonsterStanding {
    int x = 0;
    int y = 0;
    MonsterSpriteSize size;
    bool mirrored = false;
    int bottomEdge = 0;

    constexpr bool operator==(const MonsterStanding&) const = default;
};

// A monster its formation puts at (formationX, formationY), standing `size` tiles, in battle type
// `type`. `overlap` is the monster's own draw-order shift (data/attack_animations.h, monsterOverlap).
MonsterStanding monsterStanding(battle::BattleType type, int formationX, int formationY,
                                MonsterSpriteSize size, std::uint8_t overlap);

// The colosseum's one monster stands centered: its feet on the ground line, raised or lowered by
// its alignment, and its sprite centered across the field with a margin on the left. A ceiling
// monster hangs from the top of the screen.
inline constexpr int kColosseumGroundPx = 128;
inline constexpr int kColosseumFieldWidthPx = 128;
inline constexpr int kColosseumLeftMarginPx = 24;

MonsterStanding colosseumMonsterStanding(MonsterSpriteSize size, MonsterVerticalAlignment alignment,
                                         std::uint8_t overlap);

// --- the party ---------------------------------------------------------------------------------

// The party stands along lines from a point kPartyApexAboveScreenPx above the top of the screen at
// x = kPartyCenterXPx. A slot's angle (data/battle_gfx_tables.h, PartySlotPlacement) says which
// line: kAngleStraightDown points straight down, and each 128 either side is 45 degrees.
inline constexpr int kPartyApexAboveScreenPx = 104;
inline constexpr int kPartyCenterXPx = 120;
inline constexpr int kAngleStraightDown = 256;

// A party member's frame is drawn with its top kCharacterFrameRaisePx above its y, and its feet are
// kCharacterBottomEdgePx below its y.
inline constexpr int kCharacterFrameRaisePx = 8;
inline constexpr int kCharacterBottomEdgePx = 16;

// The colosseum's fighter stands at this y.
inline constexpr int kColosseumFighterY = 104;

// The x a party member standing at `y` on the line at `angle` has. Exact to the original's
// arithmetic: the angle's lean from straight down, scaled by the distance from the apex and taken
// to whole pixels, is added to the center; a lean to the left is complemented before and after
// the scaling rather than negated.
int partyMemberX(int angle, int y);

// Where a party member stands: the point its frame is placed by (the frame's top is
// kCharacterFrameRaisePx above it), whether its art is mirrored (unmirrored art faces left), and
// where its feet are for the draw order.
struct PartyStanding {
    int x = 0;
    int y = 0;
    bool mirrored = false;
    int bottomEdge = 0;

    constexpr bool operator==(const PartyStanding&) const = default;
};

// Party slot `slot` in battle type `type`. A back-row member leans its line by its slot's back-row
// angle; a member fighting against the party faces the other way; on the Magitek train's background
// every slot stands at its own y; in the colosseum slot 0 stands at kColosseumFighterY.
PartyStanding partyMemberStanding(battle::BattleType type, std::size_t slot, bool backRow,
                                  bool actsAsEnemy, bool magitekTrain, bool colosseum);

// --- the draw order ----------------------------------------------------------------------------

// The battlers to draw, back to front: the one whose feet are highest first. Two with their feet
// level keep slot order, the earlier slot in front. A slot with no bottom edge is not drawn.
std::vector<battle::BattlerSlot> battlerDrawOrder(
    const std::array<std::optional<int>, battle::kBattlerSlots>& bottomEdges);

}  // namespace ostinato::render
