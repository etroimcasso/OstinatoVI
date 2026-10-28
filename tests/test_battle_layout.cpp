// The battle's layout: the party's places by battle type against the cartridge's tables, where a
// monster stands in each battle type, how a laid-out battle starts, how attackers turn to face
// their targets — and, over every formation, that comparing sides decides each turn the way the
// original's comparison of screen positions does.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "battle/action_executor.h"
#include "battle/attack.h"
#include "battle/battle_core.h"
#include "battle/battle_layout.h"
#include "battle/battle_start.h"
#include "battle_test_support.h"
#include "data/battle_tables.h"
#include "data/char_ai.h"
#include "data/formations.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "fixtures/party_layout_expected.h"
#include "ostinato/formation_ref.h"
#include "ostinato/text_class.h"
#include "party_test_support.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::AttackInFlight;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleScriptCommand;
using battle::BattleState;
using battle::BattleType;
using battle::Facing;
using battle::FieldSide;
using battle::placeMonster;

constexpr std::array<BattleType, 4> kBattleTypes = {BattleType::Front, BattleType::Back, BattleType::Pincer,
                                                    BattleType::Side};

const BattlerSlot kTerra = BattlerSlot::character(0);
const BattlerSlot kLocke = BattlerSlot::character(1);
const BattlerSlot kGuard = BattlerSlot::monster(0);
const BattlerSlot kSoldier = BattlerSlot::monster(1);

// --- the party's places ------------------------------------------------------------------------

TEST(PartyLayout, EveryRowMatchesTheCartridgeTables) {
    for (std::size_t row = 0; row < battle::kPartyLayout.size(); ++row) {
        const battle::PartyLayoutRow& layout = battle::kPartyLayout[row];
        EXPECT_EQ(static_cast<std::size_t>(layout.battleType), row);
        for (std::size_t slot = 0; slot < 4; ++slot) {
            EXPECT_EQ(static_cast<int>(layout.slots[slot].facing), kExpectedPartyFacing[row][slot])
                << "row " << row << " slot " << slot;
            // The angle is measured from 256, straight down the middle.
            EXPECT_EQ(layout.slots[slot].slope, kExpectedPartyAngle[row][slot] - 256)
                << "row " << row << " slot " << slot;
        }
    }
}

TEST(PartyLayout, EachBattleTypePutsThePartyOnItsSideOfTheField) {
    const auto sides = [](BattleType type) {
        std::vector<FieldSide> out;
        for (const battle::PartyLayoutSlot& slot : battle::partyLayout(type).slots) {
            out.push_back(battle::sideOfSlope(slot.slope));
        }
        return out;
    };
    const FieldSide left = FieldSide::Left;
    const FieldSide center = FieldSide::Center;
    const FieldSide right = FieldSide::Right;
    EXPECT_EQ(sides(BattleType::Front), (std::vector<FieldSide>{right, right, right, right}));
    EXPECT_EQ(sides(BattleType::Back), (std::vector<FieldSide>{left, left, left, left}));
    EXPECT_EQ(sides(BattleType::Pincer), (std::vector<FieldSide>{center, center, center, center}));
    EXPECT_EQ(sides(BattleType::Side), (std::vector<FieldSide>{right, right, left, left}));
}

// --- where a monster stands --------------------------------------------------------------------

TEST(PlaceMonster, AFrontAttackLeavesTheMonsterWhereItsFormationPutsItFacingRight) {
    const battle::MonsterPlacement placed = placeMonster(BattleType::Front, 40, 4);
    EXPECT_EQ(placed.left, 40);
    EXPECT_EQ(placed.width, 32);
    EXPECT_EQ(placed.center(), 56);
    EXPECT_EQ(placed.side, FieldSide::Left);
    EXPECT_EQ(placed.facing, Facing::Right);
}

TEST(PlaceMonster, ABackAttackMirrorsTheMonsterAboutTheMiddleFacingLeft) {
    // CalcMonsterPos_01 :2715-2726: x = -x - width, flipped.
    const battle::MonsterPlacement placed = placeMonster(BattleType::Back, 16, 4);
    EXPECT_EQ(placed.left, 208);
    EXPECT_EQ(placed.side, FieldSide::Right);
    EXPECT_EQ(placed.facing, Facing::Left);
}

TEST(PlaceMonster, InAPincerAMonsterReachingAcrossPixel104JoinsTheRightGroup) {
    // CalcMonsterPos_02 :2742-2760: right edge below $68 stays left; otherwise mirrored about 160.
    const battle::MonsterPlacement left = placeMonster(BattleType::Pincer, 64, 4);   // reaches 96
    EXPECT_EQ(left.side, FieldSide::Left);
    EXPECT_EQ(left.left, 64);
    EXPECT_EQ(left.facing, Facing::Right);
    const battle::MonsterPlacement right = placeMonster(BattleType::Pincer, 72, 4);  // reaches 104
    EXPECT_EQ(right.side, FieldSide::Right);
    EXPECT_EQ(right.left, 216);
    EXPECT_EQ(right.facing, Facing::Left);
}

TEST(PlaceMonster, InASideAttackAMonsterWhoseMiddleIsLeftOfPixel128FacesLeft) {
    // CalcMonsterPos_03 :2798-2813: moved 48 right; a center below $80 is flipped.
    const battle::MonsterPlacement leftHalf = placeMonster(BattleType::Side, 56, 5);   // center 124
    EXPECT_EQ(leftHalf.left, 104);
    EXPECT_EQ(leftHalf.center(), 124);
    EXPECT_EQ(leftHalf.side, FieldSide::Center);
    EXPECT_EQ(leftHalf.facing, Facing::Left);
    const battle::MonsterPlacement rightHalf = placeMonster(BattleType::Side, 56, 6);  // center 128
    EXPECT_EQ(rightHalf.center(), 128);
    EXPECT_EQ(rightHalf.facing, Facing::Right);
}

// --- turning to face a target ------------------------------------------------------------------

// A pincer: Terra and Locke in the middle, the guard on the left facing right and the soldier on
// the right facing left. Terra faces right, Locke left.
BattleState pincer() {
    BattleState state = makeAimableBattle();
    state.battleType = BattleType::Pincer;
    state.at(kTerra).side = FieldSide::Center;
    state.at(kLocke).side = FieldSide::Center;
    state.at(kGuard).side = FieldSide::Left;
    state.at(kSoldier).side = FieldSide::Right;
    state.facingRight = BattlerSet::of(kTerra) | BattlerSet::of(kGuard);
    return state;
}

battle::AttackAnimation animation(BattleCommandId command, BattlerSlot attacker, BattlerSet targets) {
    battle::AttackAnimation shown{.what = command};
    shown.params.attacker = attacker;
    shown.params.attackerIsMonster = attacker.isMonster();
    shown.params.targets = targets;
    return shown;
}

TEST(TurnToFace, APartyMemberTurnsToFaceTheMonsterItAttacks) {
    BattleState state = pincer();
    battle::turnToFace(state, kLocke, BattlerSet::of(kSoldier));
    EXPECT_TRUE(state.facingRight.has(kLocke));
    battle::turnToFace(state, kLocke, BattlerSet::of(kGuard));
    EXPECT_FALSE(state.facingRight.has(kLocke));
}

TEST(TurnToFace, AMonsterTurnsToFaceThePartyMemberItAttacks) {
    BattleState state = pincer();
    battle::turnToFace(state, kSoldier, BattlerSet::of(kTerra));
    EXPECT_FALSE(state.facingRight.has(kSoldier));
    state.at(kSoldier).side = FieldSide::Left;
    battle::turnToFace(state, kSoldier, BattlerSet::of(kTerra));
    EXPECT_TRUE(state.facingRight.has(kSoldier));
}

TEST(TurnToFace, NoOneTurnsToFaceTheirOwnSide) {
    // _c19aa2 :23277-23278 and :23330-23331: a party member turns only for a monster target, a
    // monster only for a party member. Each attacker here faces away from where its target is.
    BattleState state = pincer();
    battle::turnToFace(state, kTerra, BattlerSet::of(kLocke));
    EXPECT_TRUE(state.facingRight.has(kTerra));
    state.facingRight.remove(kGuard);
    battle::turnToFace(state, kGuard, BattlerSet::of(kSoldier));
    EXPECT_FALSE(state.facingRight.has(kGuard));
    battle::turnToFace(state, kTerra, BattlerSet{});
    EXPECT_TRUE(state.facingRight.has(kTerra));
}

TEST(TurnToFace, TheFirstMonsterAimedAtIsFacedAheadOfAnyPartyMember) {
    // GetTargetID :28023-28035: a monster target first, the lowest-numbered one.
    BattleState state = pincer();
    EXPECT_EQ(battle::facedTarget(BattlerSet::of(kTerra) | BattlerSet::of(kSoldier)), kSoldier);
    EXPECT_EQ(battle::facedTarget(BattlerSet::of(kSoldier) | BattlerSet::of(kGuard)), kGuard);
    EXPECT_EQ(battle::facedTarget(BattlerSet::of(kLocke) | BattlerSet::of(kTerra)), kTerra);
    // A monster aiming at a monster and a party member faces the monster, so does not turn.
    battle::turnToFace(state, kSoldier, BattlerSet::of(kTerra) | BattlerSet::of(kGuard));
    EXPECT_FALSE(state.facingRight.has(kSoldier));
}

TEST(TurnToFaceTargets, EachAnimationTurnsItsAttackerAndIsRemembered) {
    BattleState state = pincer();
    const std::vector<BattleScriptCommand> script = {
        animation(BattleCommandId::FIGHT, kLocke, BattlerSet::of(kSoldier)),
        animation(BattleCommandId::MAGIC, kGuard, BattlerSet::of(kLocke)),
    };
    battle::turnToFaceTargets(state, script);
    EXPECT_TRUE(state.facingRight.has(kLocke));
    EXPECT_TRUE(state.facingRight.has(kGuard));
    EXPECT_EQ(state.animationTargets, BattlerSet::of(kLocke));
}

TEST(TurnToFaceTargets, MimicRowDefAndTheRouletteCursorTurnNoOne) {
    BattleState state = pincer();
    state.animationTargets = BattlerSet::of(kGuard);
    battle::AttackAnimation roulette = animation(BattleCommandId::FIGHT, kLocke, BattlerSet::of(kSoldier));
    roulette.what = battle::SpecialAnimation::RouletteCursor;
    const std::vector<BattleScriptCommand> script = {
        animation(BattleCommandId::MIMIC, kLocke, BattlerSet::of(kSoldier)),
        animation(BattleCommandId::ROW, kLocke, BattlerSet::of(kSoldier)),
        animation(BattleCommandId::DEF, kLocke, BattlerSet::of(kSoldier)),
        roulette,
    };
    battle::turnToFaceTargets(state, script);
    EXPECT_FALSE(state.facingRight.has(kLocke));
    EXPECT_EQ(state.animationTargets, BattlerSet::of(kGuard));
}

TEST(TurnToFaceTargets, AfterThePartyRunsNoPartyMemberTurnsForTheRestOfTheScript) {
    // CmdAnim_22 :27149-27152 sets w7e62a4; _c19aa2 :23272-23273 skips a party member while it is
    // set; BtlGfx_04 :22393 clears it for the next script.
    BattleState state = pincer();
    battle::AttackAnimation run = animation(BattleCommandId::FIGHT, kTerra, BattlerSet::of(kGuard));
    run.what = battle::SpecialAnimation::PartyRunsAway;
    const std::vector<BattleScriptCommand> script = {
        run,
        animation(BattleCommandId::FIGHT, kLocke, BattlerSet::of(kSoldier)),
        animation(BattleCommandId::FIGHT, kSoldier, BattlerSet::of(kLocke)),
    };
    battle::turnToFaceTargets(state, script);
    EXPECT_TRUE(state.facingRight.has(kTerra));   // ran without turning toward the guard
    EXPECT_FALSE(state.facingRight.has(kLocke));
    EXPECT_FALSE(state.facingRight.has(kSoldier));
    const std::vector<BattleScriptCommand> next = {
        animation(BattleCommandId::FIGHT, kLocke, BattlerSet::of(kSoldier))};
    battle::turnToFaceTargets(state, next);
    EXPECT_TRUE(state.facingRight.has(kLocke));
}

TEST(TurnToFaceTargets, AJumpLandsFacingTheLastAnimationsTargetsNotItsOwn) {
    // CmdAnim_16 :27536-27540 turns without setting the animation's targets first.
    BattleState state = pincer();
    const std::vector<BattleScriptCommand> before = {
        animation(BattleCommandId::FIGHT, kGuard, BattlerSet::of(kSoldier) | BattlerSet::of(kTerra))};
    battle::turnToFaceTargets(state, before);
    const std::vector<BattleScriptCommand> landing = {
        animation(BattleCommandId::JUMP, kLocke, BattlerSet::of(kGuard))};
    battle::turnToFaceTargets(state, landing);
    EXPECT_TRUE(state.facingRight.has(kLocke));  // toward the soldier, on the right
    EXPECT_EQ(state.animationTargets, BattlerSet::of(kSoldier) | BattlerSet::of(kTerra));
}

TEST(TurnToFaceTargets, TheReadyStanceTurnsTheActorToItsTargets) {
    // GfxCmd_0c :21841-21842 runs init_all_anim, which sets the targets and turns.
    BattleState state = pincer();
    const std::vector<BattleScriptCommand> script = {
        battle::ReadyStance{.actor = kLocke, .command = BattleCommandId::FIGHT,
                            .targets = BattlerSet::of(kSoldier)}};
    battle::turnToFaceTargets(state, script);
    EXPECT_TRUE(state.facingRight.has(kLocke));
    EXPECT_EQ(state.animationTargets, BattlerSet::of(kSoldier));
}

TEST(TurnToFaceTargets, ATurnDecidesWhetherTheNextBlowLandsInTheBack) {
    // UpdateFacingDirection :3217-3260: in a pincer a monster's blow lands in the back of a party
    // member facing the same way. Locke faces left, toward the guard; once he turns to attack the
    // soldier he faces right, the way the guard faces.
    BattleState state = pincer();
    AttackInFlight blow;
    blow.record.traits.set(AttackTrait::PHYSICAL);
    EXPECT_FALSE(battle::hitInTheBack(state, blow, kGuard).has(kLocke));
    const std::vector<BattleScriptCommand> script = {
        animation(BattleCommandId::FIGHT, kLocke, BattlerSet::of(kSoldier))};
    battle::turnToFaceTargets(state, script);
    EXPECT_TRUE(battle::hitInTheBack(state, blow, kGuard).has(kLocke));
}

// --- laying out a battle -----------------------------------------------------------------------

// Monster names are not read here: every name record is blank.
const TextCorpus& noNames() {
    static const TextCorpus corpus = [] {
        std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
        buffers[static_cast<std::size_t>(TextClass::MONSTER_NAME)].assign(3840, 0xFF);
        return TextCorpus{std::move(buffers)};
    }();
    return corpus;
}

TEST(LayOutParty, APartyMemberFightingAgainstThePartyFacesTheOtherWay) {
    // :8457-8467: CharDirTbl, then flipped for a character acting as an enemy.
    BattleState state = makeBattle();
    state.battleType = BattleType::Side;
    state.at(kLocke).actsAsEnemy = true;
    battle::layOutParty(state);
    EXPECT_FALSE(state.facingRight.has(kTerra));
    EXPECT_TRUE(state.facingRight.has(kLocke));
    EXPECT_TRUE(state.facingRight.has(BattlerSlot::character(2)));
    EXPECT_EQ(state.at(BattlerSlot::character(3)).side, FieldSide::Left);
}

class BattleLayoutCartridgeTest : public ::testing::Test {
protected:
    inline static IngestedCartridge cartridge_;

    static void SetUpTestSuite() { cartridge_ = ingestVanilla(); }

    static const MonsterSprites& sprites() { return cartridge_.content->monsterSprites; }

    // The width a formation's slot gives its monster, in tiles.
    static int widthOf(const Formation& formation, std::size_t slot, MonsterId monster) {
        const auto box = monsterSpriteBox(formation.vramMap(), slot);
        return box ? sprites().size(monster, *box).width : 0;
    }

    // A battle in `formation` that the draw makes a `type` attack, for a party of three.
    static std::optional<BattleState> startAs(FormationId formation, BattleType type) {
        battle::NullExecutor executor;
        const game::Party party =
            battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR}).party;
        for (std::uint8_t frames = 0; frames < 60; ++frames) {
            battle::BattleStart start;
            start.formation = FormationRef::of(formation);
            start.playTimeFrames = frames;
            BattleState state = battle::startBattle(start, party, {}, noNames(), sprites(), executor);
            if (state.battleType == type) {
                return state;
            }
        }
        return std::nullopt;
    }
};

TEST_F(BattleLayoutCartridgeTest, APincerStartsWithEachGroupFacingTheParty) {
    // Tyranosaur ×2 is only ever a pincer: one on each side.
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const auto state = startAs(FormationId::TYRANOSAUR_X2, BattleType::Pincer);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->at(BattlerSlot::monster(0)).side, FieldSide::Left);
    EXPECT_EQ(state->at(BattlerSlot::monster(1)).side, FieldSide::Right);
    EXPECT_TRUE(state->facingRight.has(BattlerSlot::monster(0)));
    EXPECT_FALSE(state->facingRight.has(BattlerSlot::monster(1)));
    EXPECT_EQ(state->pincerSides[0].bits, 0x01);
    EXPECT_EQ(state->pincerSides[1].bits, 0x02);
    // CharDirTbl's pincer row: right, left, right, left.
    EXPECT_TRUE(state->facingRight.has(BattlerSlot::character(0)));
    EXPECT_FALSE(state->facingRight.has(BattlerSlot::character(1)));
    EXPECT_TRUE(state->facingRight.has(BattlerSlot::character(2)));
    EXPECT_TRUE(state->cannotRun);
}

TEST_F(BattleLayoutCartridgeTest, APincerWithEveryMonsterOnOneSideStillCannotBeRunFromAtFirst) {
    // InitBattle checks running (:6162, :15184-15193) before the battle graphics place the
    // monsters (:6197), so both sides read as every monster. The Latimeria stands alone on the
    // right; once the check runs again it can be run from.
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    auto state = startAs(FormationId::LATIMERIA, BattleType::Pincer);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->pincerSides[0].bits, 0x00);
    EXPECT_EQ(state->pincerSides[1].bits, 0x01);
    EXPECT_TRUE(state->cannotRun);
    battle::refreshRunDifficulty(*state);
    EXPECT_FALSE(state->cannotRun);
}

TEST_F(BattleLayoutCartridgeTest, TheNextFinalStageKeepsThePartysFacing) {
    // GfxCmd_12 :22118-22149 places the new formation's monsters; the party is left as it was.
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    battle::NullExecutor executor;
    battle::BattleStart start;
    start.formation = FormationRef::of(kFinalBattleFormations[0]);
    start.finalBattleOrder = {0, 1, 2, 3};
    const game::Party party = battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE,
                                               CharacterPropId::EDGAR, CharacterPropId::SABIN})
                                  .party;
    BattleState finished = battle::startBattle(start, party, {}, noNames(), sprites(), executor);
    finished.facingRight.add(kLocke);
    finished.animationTargets = BattlerSet::of(kGuard);
    finished.outcome = battle::BattleEnded{.reason = battle::BattleEndReason::FinalBattleNextStage,
                                           .nextFormation = kFinalBattleFormations[1]};
    const BattleState next =
        battle::startNextFinalBattleStage(finished, battle::writeBack(finished), start, noNames(), sprites(), executor);
    EXPECT_TRUE(next.facingRight.has(kLocke));
    EXPECT_FALSE(next.facingRight.has(kTerra));
    EXPECT_EQ(next.animationTargets, BattlerSet::of(kGuard));
}

// --- the proof: sides decide every turn the original's screen positions do ---------------------

// Where the original stands a party member (CalcCharXPos :8171-8197): on a line leaning `angle`
// from a point 104 pixels above the screen, at depth `y`, about the middle at x = 120. The depth
// is a byte, as there. A party member's middle is 8 pixels in (_c10659 :1091).
constexpr int kApexAbovePx = 104;
constexpr int kPartyMiddleX = 120;
constexpr int kPartyHalfWidthPx = 8;

int partyMemberCenter(int angle, int y) {
    const int depth = (y + kApexAbovePx) & 0xFF;
    const int lean = (angle & 0x1FF) - 256;
    const int left = lean >= 0 ? kPartyMiddleX + ((lean * depth) >> 8)
                               : kPartyMiddleX - (((-lean - 1) * depth) >> 8) - 1;
    return left + kPartyHalfWidthPx;
}

// A step forward to attack moves 24 pixels (GfxCmd_0d :23133-23203 steps back 8 frames of 3).
constexpr int kStepForwardPx = 24;

// The colosseum's formation centers its one monster (InitMonsterPos :2858-2891) and stands the
// fighter at depth 104 (:8370-8376).
constexpr int colosseumMonsterLeft(int widthPx) {
    return ((128 - widthPx) >> 1) + 24;
}
constexpr int kColosseumFighterY = 104;

struct Standing {
    int center;
    FieldSide side;
};

// Every monster's place in `formation` in battle type `type`.
std::vector<std::pair<std::size_t, Standing>> monstersOf(const Formation& formation, FormationId id, BattleType type,
                                                         const std::function<int(std::size_t, MonsterId)>& width,
                                                         MonsterId colosseumOpponent) {
    std::vector<std::pair<std::size_t, Standing>> out;
    for (std::size_t slot = 0; slot < 6; ++slot) {
        if (formation.slotEmpty(slot)) {
            continue;
        }
        const bool colosseum = id == FormationId::PUGS;
        const MonsterId monster = colosseum ? colosseumOpponent : formation.monsterId(slot);
        const int widthTiles = width(slot, monster);
        battle::MonsterPlacement placed = placeMonster(type, formation.positionX(slot), widthTiles);
        if (colosseum) {
            placed.left = colosseumMonsterLeft(placed.width);
        }
        out.push_back({slot, Standing{.center = placed.center(), .side = placed.side}});
    }
    return out;
}

bool possible(const FormationAux& aux, BattleType type) {
    switch (type) {
    case BattleType::Front: return aux.frontPossible();
    case BattleType::Back: return aux.backPossible();
    case BattleType::Pincer: return aux.pincerPossible();
    case BattleType::Side: return aux.sidePossible();
    }
    return false;
}

// The original's answer: is the target to the right of the attacker? Equal middles turn the
// attacker whichever way it faces, which no side can say.
enum class Answer { Right, Left, Toggles };

Answer byPosition(int attacker, int target) {
    return target > attacker ? Answer::Right : target < attacker ? Answer::Left : Answer::Toggles;
}

Answer bySide(FieldSide attacker, FieldSide target) {
    return target > attacker ? Answer::Right : target < attacker ? Answer::Left : Answer::Toggles;
}

struct Disagreement {
    FormationId formation;
    BattleType type;
    int partySlot;  // -1 for a scripted battle's cast
    bool backRow;
};

// Every party place a battle type gives: each slot, front row and back, on the battle type's
// depths and the magitek train car's.
template <typename Visit>
void forEachPartyPlace(BattleType type, Visit visit) {
    const auto row = static_cast<std::size_t>(type);
    for (std::size_t slot = 0; slot < 4; ++slot) {
        for (bool backRow : {false, true}) {
            for (std::size_t depths : {row, std::size_t{4}}) {
                const int angle = kExpectedPartyAngle[row][slot] + (backRow ? kExpectedPartyBackRowAngle[row][slot] : 0);
                visit(static_cast<int>(slot), backRow, partyMemberCenter(angle, kExpectedPartyY[depths][slot]));
            }
        }
    }
}

TEST_F(BattleLayoutCartridgeTest, SidesDecideEveryTurnFromWhereTheBattlersStand) {
    // For every formation and every battle type it allows: each party member, front row and
    // back, against each monster, both ways round — the side rule gives the original's answer,
    // and no two middles are ever equal. The colosseum's fighter against every monster; the
    // scripted battles' cast at their own places.
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const auto width = [](const Formation& formation) {
        return [&formation](std::size_t slot, MonsterId monster) { return widthOf(formation, slot, monster); };
    };
    int compared = 0;
    std::vector<Disagreement> wrong;
    const auto compare = [&](const FormationEntry& entry, BattleType type, int slot, bool backRow, int partyCenter,
                             FieldSide partySide, const Standing& monster) {
        ++compared;
        const bool partyAttacks = byPosition(partyCenter, monster.center) == bySide(partySide, monster.side);
        const bool monsterAttacks = byPosition(monster.center, partyCenter) == bySide(monster.side, partySide);
        if (!partyAttacks || !monsterAttacks || monster.center == partyCenter) {
            wrong.push_back({entry.id, type, slot, backRow});
        }
    };

    for (const FormationEntry& entry : formations()) {
        const FormationAux& aux = getFormationAux(entry.id);
        for (BattleType type : kBattleTypes) {
            if (!possible(aux, type)) {
                continue;
            }
            const auto& row = battle::partyLayout(type);
            const bool colosseum = entry.id == FormationId::PUGS;
            for (int opponent = 0; opponent < (colosseum ? 384 : 1); ++opponent) {
                const auto monsters =
                    monstersOf(entry.record, entry.id, type, width(entry.record), static_cast<MonsterId>(opponent));
                forEachPartyPlace(type, [&](int slot, bool backRow, int center) {
                    if (colosseum && slot == 0) {
                        const auto r = static_cast<std::size_t>(type);
                        center = partyMemberCenter(kExpectedPartyAngle[r][0], kColosseumFighterY);
                    }
                    const FieldSide side = battle::sideOfSlope(row.slots[static_cast<std::size_t>(slot)].slope);
                    for (const auto& [monsterSlot, monster] : monsters) {
                        compare(entry, type, slot, backRow, center, side, monster);
                    }
                });
            }
            if (!aux.characterAiEnabled()) {
                continue;
            }
            // A scripted battle's cast stands where the setup puts it (_c2be6e :41368-41424):
            // angle 2x + 128, depth 2y. Gau's place on the Veldt is not among them: he appears
            // only once every monster is down (:11933-11942), so never faces one.
            const CharAiSetup& setup = charAiSetup(static_cast<CharAiId>(aux.characterAiIndex()));
            for (const CharAiSlot& cast : setup.slots) {
                if (cast.isEmpty() || cast.x == 0xFF) {
                    continue;
                }
                const int center = partyMemberCenter(2 * cast.x + 128, 2 * cast.y);
                const FieldSide side = battle::sideOfSlope(row.slots[0].slope);
                for (const auto& [monsterSlot, monster] :
                     monstersOf(entry.record, entry.id, type, width(entry.record), MonsterId{})) {
                    compare(entry, type, -1, false, center, side, monster);
                }
            }
        }
    }
    EXPECT_GT(compared, 50000);
    EXPECT_TRUE(wrong.empty()) << wrong.size() << " places disagree, first in formation "
                               << (wrong.empty() ? -1 : static_cast<int>(wrong.front().formation));
}

TEST_F(BattleLayoutCartridgeTest, AStepForwardCarriesPartyMemberZeroPastAMonsterInTwoSideAttacks) {
    // A party member that has stepped forward toward one monster and is then shown attacking
    // another: in two side-attack formations the step takes party member 0, front row, past a
    // monster whose middle is at 184, and the original turns it around. The side rule does not
    // follow the step; these twelve are every place it differs.
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    std::set<FormationId> where;
    int count = 0;
    for (const FormationEntry& entry : formations()) {
        const FormationAux& aux = getFormationAux(entry.id);
        for (BattleType type : kBattleTypes) {
            if (!possible(aux, type)) {
                continue;
            }
            const auto& row = battle::partyLayout(type);
            const auto monsters = monstersOf(entry.record, entry.id, type,
                                             [&](std::size_t slot, MonsterId monster) {
                                                 return widthOf(entry.record, slot, monster);
                                             },
                                             MonsterId{});
            forEachPartyPlace(type, [&](int slot, bool backRow, int center) {
                const FieldSide side = battle::sideOfSlope(row.slots[static_cast<std::size_t>(slot)].slope);
                for (const auto& [firstSlot, first] : monsters) {
                    const int stepped = center + (first.center > center ? kStepForwardPx : -kStepForwardPx);
                    for (const auto& [nextSlot, next] : monsters) {
                        if (byPosition(stepped, next.center) == bySide(side, next.side)) {
                            continue;
                        }
                        ++count;
                        where.insert(entry.id);
                        EXPECT_EQ(type, BattleType::Side);
                        EXPECT_EQ(slot, 0);
                        EXPECT_FALSE(backRow);
                        EXPECT_EQ(next.center, 184);
                    }
                }
            });
        }
    }
    EXPECT_EQ(count, 12);
    EXPECT_EQ(where, (std::set<FormationId>{FormationId::CEPHALER_X3, FormationId::OROG_POWERDEMON_X2}));
}

}  // namespace
}  // namespace ostinato::test
