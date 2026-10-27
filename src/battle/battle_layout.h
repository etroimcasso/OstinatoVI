// The battle's layout: which part of the field each battler stands in, which way it faces, and
// how attackers turn to face their targets.
//
// A battle is laid out when it starts. Each monster stands where its formation puts it, moved and
// mirrored by the battle type: on the left in a front attack, on the right in a back attack, split
// into a left and a right group in a pincer attack, and in the middle of the party in a side
// attack. The party stands opposite, between the two groups of a pincer, or split to both edges in
// a side attack. Facings start toward the other side.
//
//     layOutMonsters(state, content.monsterSprites);   // every monster's side and facing
//     layOutParty(state);                              // every party member's side and facing
//     ...
//     turnToFaceTargets(state, script);                // as each action's script is handed out
//
// Facings matter to the battle itself in a side or pincer attack, where an attacker hits a target
// in the back when both face the same way (battle/attack.h, hitInTheBack). The monsters on each
// side of a pincer are BattleState::pincerSides.
//
// The rules, and the line in the original each comes from, are in docs/contracts/battle_commands.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/monster_sprites.h"

namespace ostinato::battle {

// Which way a battler faces.
enum class Facing : std::uint8_t {
    Left = 0,
    Right = 1,
};

// --- the party's places ------------------------------------------------------------------------

// One party member's place in a battle type: the way it faces when the battle starts, and how far
// its place leans from the middle of the field — above zero right of the middle, below zero left
// of it, zero on it.
struct PartyLayoutSlot {
    Facing facing;
    std::int16_t slope;
};

// The party's places in one battle type, party slot by party slot.
struct PartyLayoutRow {
    BattleType battleType;
    std::array<PartyLayoutSlot, kPartySlots> slots;
};

inline constexpr std::array<PartyLayoutRow, 4> kPartyLayout = {{
#include "data/generated/party_layout_data.inc"
}};

static_assert([] {
    for (std::size_t i = 0; i < kPartyLayout.size(); ++i) {
        if (static_cast<std::size_t>(kPartyLayout[i].battleType) != i) {
            return false;
        }
    }
    return true;
}(), "kPartyLayout rows must be in battle-type order");

// The party's places in battle type `type`.
constexpr const PartyLayoutRow& partyLayout(BattleType type) {
    return kPartyLayout[static_cast<std::size_t>(type)];
}

// The part of the field a party member whose place leans by `slope` stands in.
constexpr FieldSide sideOfSlope(int slope) {
    return slope > 0 ? FieldSide::Right : slope < 0 ? FieldSide::Left : FieldSide::Center;
}

// --- the monsters' places ----------------------------------------------------------------------

// Where a monster stands: the left edge of its sprite and its width, in pixels across the
// battlefield, the part of the field that puts it in, and the way it faces.
struct MonsterPlacement {
    int left = 0;
    int width = 0;
    FieldSide side = FieldSide::Left;
    Facing facing = Facing::Right;

    constexpr int center() const { return left + width / 2; }
};

// A monster's sprite is laid out on a grid of 8-pixel tiles.
inline constexpr int kMonsterTilePx = 8;

// In a pincer attack a monster whose sprite reaches this far across, or further, stands with the
// right group; the right group is mirrored about kPincerRightMirrorPx and faces left.
inline constexpr int kPincerRightGroupEdgePx = 104;
inline constexpr int kPincerRightMirrorPx = 160;

// In a back attack every monster is mirrored about the middle of the field and faces left.
inline constexpr int kBackAttackMirrorPx = 128;

// In a side attack every monster moves this far right, and one whose middle is left of
// kFieldMiddlePx faces left.
inline constexpr int kSideAttackShiftPx = 48;
inline constexpr int kFieldMiddlePx = 128;

// Where a monster `widthTiles` tiles wide stands in battle type `type`, when its formation puts the
// left edge of its sprite `formationX` pixels across (Formation::positionX()).
constexpr MonsterPlacement placeMonster(BattleType type, int formationX, int widthTiles) {
    const int width = widthTiles * kMonsterTilePx;
    const int right = formationX + width;
    switch (type) {
    case BattleType::Front:
        return {.left = formationX, .width = width, .side = FieldSide::Left, .facing = Facing::Right};
    case BattleType::Back:
        return {.left = 2 * kBackAttackMirrorPx - right, .width = width, .side = FieldSide::Right,
                .facing = Facing::Left};
    case BattleType::Pincer:
        if (right < kPincerRightGroupEdgePx) {
            return {.left = formationX, .width = width, .side = FieldSide::Left, .facing = Facing::Right};
        }
        return {.left = 2 * kPincerRightMirrorPx - right, .width = width, .side = FieldSide::Right,
                .facing = Facing::Left};
    case BattleType::Side: {
        const int left = formationX + kSideAttackShiftPx;
        const bool rightHalf = left + width / 2 >= kFieldMiddlePx;
        return {.left = left, .width = width, .side = FieldSide::Center,
                .facing = rightHalf ? Facing::Right : Facing::Left};
    }
    }
    return {};
}

// --- laying the battle out ---------------------------------------------------------------------

// Place every monster in the formation: its side and facing from where the formation puts it and
// how wide its sprite stands (`sprites`), and the monsters on each side of a pincer. Runs when the
// battle starts and when its formation changes; the party is left as it is.
void layOutMonsters(BattleState& state, const MonsterSprites& sprites);

// Place every party member: its side, and the way it faces as the battle starts. A party member
// fighting against the party faces the other way.
void layOutParty(BattleState& state);

// --- turning to face a target ------------------------------------------------------------------

// The target an attacker aimed at `targets` turns to face: the first monster among them, or the
// first party member when there is no monster.
std::optional<BattlerSlot> facedTarget(BattlerSet targets);

// Turn `attacker` to face the target of an animation aimed at `targets`. A party member turns only
// to face a monster and a monster only to face a party member; with no target, or the wrong kind,
// nothing changes.
void turnToFace(BattleState& state, BattlerSlot attacker, BattlerSet targets);

// Turn every attacker `script` shows to face its target, in the order the script plays, and
// remember the targets of the last animation shown. Called as each script is handed to the
// presentation.
//
// Every animation turns its attacker except Mimic's, Row's and Def's, which show nothing of the
// kind, and the roulette cursor. A party running away turns no party member for the rest of the
// script. A Jump's landing turns the jumper toward the targets of the last animation shown before
// it, not its own.
void turnToFaceTargets(BattleState& state, std::span<const BattleScriptCommand> script);

}  // namespace ostinato::battle
