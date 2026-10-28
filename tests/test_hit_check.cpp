// The hit check: Vanish, Reflect, death protection, resurrection, the Golem and Interceptor, the
// attacks that cannot be dodged, the stamina save, and evasion — and what a miss shows. Behaviors
// and their sources: docs/contracts/battle_commands.md § The hit check.

#include <gtest/gtest.h>

#include "battle/attack.h"
#include "battle_test_support.h"

namespace ostinato::test {
namespace {

using battle::AttackInFlight;
using battle::BattlerSlot;
using battle::BattleState;
using battle::BlockShown;
using battle::HitResult;

const BattlerSlot kTerra = BattlerSlot::character(0);
const BattlerSlot kLocke = BattlerSlot::character(1);
const BattlerSlot kGuard = BattlerSlot::monster(0);

AttackInFlight spell() {
    AttackInFlight attack;
    attack.command = BattleCommandId::MAGIC;
    attack.record = AttackProperties{};
    attack.record.hitRate = 100;
    return attack;
}

AttackInFlight blow() {
    AttackInFlight attack = spell();
    attack.command = BattleCommandId::FIGHT;
    attack.record.traits.set(AttackTrait::PHYSICAL);
    return attack;
}

AttackInFlight undodgeable(AttackInFlight attack) {
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    return attack;
}

TEST(CheckHit, AVanishedTargetIsHitBySpellsAtOnce) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).status.set(StatusId::INVISIBLE);
    state.at(kGuard).status.set(StatusId::REFLECT);
    AttackInFlight attack = spell();
    const HitResult result = battle::checkHit(state, attack, kTerra, kGuard);
    EXPECT_TRUE(result.hit);
    EXPECT_FALSE(result.reflected);
    EXPECT_FALSE(state.at(kGuard).statusesToClear.has(StatusId::INVISIBLE));
}

TEST(CheckHit, AVanishedTargetDodgesBlows) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).status.set(StatusId::INVISIBLE);
    AttackInFlight attack = undodgeable(blow());
    const HitResult result = battle::checkHit(state, attack, kTerra, kGuard);
    EXPECT_FALSE(result.hit);
    EXPECT_TRUE(attack.scratch.missed.has(kGuard));
}

TEST(CheckHit, AnAttackThatIgnoresVanishGoesOnToTheOtherChecks) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).status.set(StatusId::INVISIBLE);
    AttackInFlight attack = undodgeable(blow());
    attack.vanishApplies = false;
    EXPECT_TRUE(battle::checkHit(state, attack, kTerra, kGuard).hit);
}

TEST(CheckHit, ALevelDivisibleAttackLiftsVanishAndChecksTheLevel) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).status.set(StatusId::INVISIBLE);
    state.at(kGuard).level = 20;
    AttackInFlight attack = spell();
    attack.record.flags2.set(AttackFlag2::LEVEL_DIVISIBLE);
    attack.record.hitRate = 5;
    EXPECT_TRUE(battle::checkHit(state, attack, kTerra, kGuard).hit);
    EXPECT_TRUE(state.at(kGuard).statusesToClear.has(StatusId::INVISIBLE));

    state.at(kGuard).level = 21;
    AttackInFlight again = spell();
    again.record.flags2.set(AttackFlag2::LEVEL_DIVISIBLE);
    again.record.hitRate = 5;
    EXPECT_FALSE(battle::checkHit(state, again, kTerra, kGuard).hit);
    EXPECT_TRUE(again.scratch.missed.has(kGuard));
}

TEST(CheckHit, ReflectBouncesTheAttackWithNoMissShown) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).status.set(StatusId::REFLECT);
    AttackInFlight attack = undodgeable(spell());
    attack.canBeCritical = true;
    const HitResult result = battle::checkHit(state, attack, kTerra, kGuard);
    EXPECT_FALSE(result.hit);
    EXPECT_TRUE(result.reflected);
    EXPECT_TRUE(attack.scratch.missed.empty());
    EXPECT_TRUE(attack.canBeCritical);

    AttackInFlight through = undodgeable(spell());
    through.record.flags1.set(AttackFlag1::IGNORE_REFLECT);
    EXPECT_TRUE(battle::checkHit(state, through, kTerra, kGuard).hit);
}

TEST(CheckHit, InstantDeathMissesATargetProtectedFromIt) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).instantDeathProtected = true;
    AttackInFlight attack = undodgeable(spell());
    attack.record.traits.set(AttackTrait::INSTANT_DEATH);
    EXPECT_FALSE(battle::checkHit(state, attack, kTerra, kGuard).hit);
}

TEST(CheckHit, ResurrectionReachesOnlyTheDeadOrTheUndead) {
    BattleState state = makeAimableBattle();
    const auto reaches = [&](bool dead, bool undead) {
        state.at(kGuard).status = dead ? StatusSet::of(StatusId::DEAD) : StatusSet{};
        state.at(kGuard).undead = undead;
        AttackInFlight attack = undodgeable(spell());
        attack.record.traits.set(AttackTrait::RESURRECTION_TARGET);
        return battle::checkHit(state, attack, kTerra, kGuard).hit;
    };
    EXPECT_TRUE(reaches(true, false));
    EXPECT_TRUE(reaches(false, true));
    EXPECT_FALSE(reaches(false, false));
    EXPECT_FALSE(reaches(true, true));
}

TEST(CheckHit, InterceptorStepsInOnAHeadsAndTheBlowLandsWithoutStatuses) {
    BattleState state = makeAimableBattle();
    state.at(kTerra).status.set(StatusId::INTERCEPTOR);
    AttackInFlight attack = blow();
    attack.scratch.targetCount = 1;
    attack.record.statuses.set(StatusId::POISON);
    rngWhere(state, {heads, any});
    const HitResult result = battle::checkHit(state, attack, kGuard, kTerra);
    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.block, BlockShown::Interceptor);
    EXPECT_EQ(attack.scratch.interceptorBlocked, kTerra);
    EXPECT_FALSE(attack.record.statuses.has(StatusId::POISON));
}

TEST(CheckHit, OnTailsInterceptorStaysOut) {
    BattleState state = makeAimableBattle();
    state.at(kTerra).status.set(StatusId::INTERCEPTOR);
    AttackInFlight attack = undodgeable(blow());
    attack.scratch.targetCount = 1;
    rngWhere(state, {tails});
    const HitResult result = battle::checkHit(state, attack, kGuard, kTerra);
    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.block, BlockShown::None);
}

TEST(CheckHit, TheGolemStepsInWhileItHasHp) {
    BattleState state = makeAimableBattle();
    state.golemHp = 300;
    AttackInFlight attack = blow();
    attack.scratch.targetCount = 1;
    const HitResult result = battle::checkHit(state, attack, kGuard, kTerra);
    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.block, BlockShown::Golem);
    EXPECT_EQ(attack.scratch.golemBlocked, kTerra);
}

TEST(CheckHit, NoOneStepsInForASpreadBlowAnEffectOrAMonster) {
    BattleState state = makeAimableBattle();
    state.golemHp = 300;
    AttackInFlight two = undodgeable(blow());
    two.scratch.targetCount = 2;
    EXPECT_EQ(battle::checkHit(state, two, kGuard, kTerra).block, BlockShown::None);

    AttackInFlight effect = undodgeable(blow());
    effect.scratch.targetCount = 1;
    effect.effect = battle::SpecialEffect::of(battle::CommandEffect::Steal);
    EXPECT_EQ(battle::checkHit(state, effect, kGuard, kTerra).block, BlockShown::None);

    AttackInFlight atMonster = undodgeable(blow());
    atMonster.scratch.targetCount = 1;
    EXPECT_EQ(battle::checkHit(state, atMonster, kTerra, kGuard).block, BlockShown::None);
}

TEST(CheckHit, TheStaminaSave) {
    BattleState state = makeAimableBattle();
    battle::Battler& guard = state.at(kGuard);
    guard.magicBlockFactor = 200;
    guard.stamina = 50;
    AttackInFlight attack = spell();
    attack.record.flags2.set(AttackFlag2::STAMINA_DEFENSE);
    attack.record.hitRate = 128;   // chance (200 x 128) / 256 = 100: the first roll always passes
    rngWhere(state, {any, [](std::uint8_t r) { return (r & 0x7F) <= 50; }});
    EXPECT_FALSE(battle::checkHit(state, attack, kTerra, kGuard).hit);
    rngWhere(state, {any, [](std::uint8_t r) { return (r & 0x7F) > 50; }});
    EXPECT_TRUE(battle::checkHit(state, attack, kTerra, kGuard).hit);

    attack.record.hitRate = 0;
    const std::uint8_t before = state.rng.position();
    EXPECT_FALSE(battle::checkHit(state, attack, kTerra, kGuard).hit);
    EXPECT_EQ(state.rng.position(), static_cast<std::uint8_t>(before + 1));
}

TEST(CheckHit, ATargetThatCannotMoveOrIsHitInTheBackIsHit) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).magicBlockFactor = 0;
    const std::uint8_t before = state.rng.position();
    for (StatusId status : {StatusId::PETRIFY, StatusId::SLEEP, StatusId::STOP, StatusId::FROZEN}) {
        state.at(kGuard).status = StatusSet::of(status);
        AttackInFlight attack = blow();
        EXPECT_TRUE(battle::checkHit(state, attack, kTerra, kGuard).hit);
    }
    state.at(kGuard).status = {};
    AttackInFlight back = blow();
    back.scratch.hitInTheBack.add(kGuard);
    EXPECT_TRUE(battle::checkHit(state, back, kTerra, kGuard).hit);
    AttackInFlight sure = blow();
    sure.record.hitRate = 255;
    EXPECT_TRUE(battle::checkHit(state, sure, kTerra, kGuard).hit);
    EXPECT_EQ(state.rng.position(), before);
}

TEST(CheckHit, RetortTakesTheBlowToAnswerIt) {
    BattleState state = makeAimableBattle();
    state.at(kTerra).magicBlockFactor = 0;
    state.at(kTerra).triggers.retort = true;
    AttackInFlight attack = blow();
    EXPECT_TRUE(battle::checkHit(state, attack, kGuard, kTerra).hit);
}

TEST(CheckHit, AnImageTakesEveryBlowAndIsUsedUpOneTimeInFour) {
    BattleState state = makeAimableBattle();
    state.at(kTerra).status.set(StatusId::IMAGE);
    AttackInFlight kept = blow();
    rngWhere(state, {atLeast(0x40)});
    EXPECT_FALSE(battle::checkHit(state, kept, kGuard, kTerra).hit);
    EXPECT_FALSE(state.at(kTerra).statusesToClear.has(StatusId::IMAGE));
    EXPECT_TRUE(kept.scratch.missed.has(kTerra));

    AttackInFlight spent = blow();
    rngWhere(state, {below(0x40)});
    EXPECT_FALSE(battle::checkHit(state, spent, kGuard, kTerra).hit);
    EXPECT_TRUE(state.at(kTerra).statusesToClear.has(StatusId::IMAGE));
}

TEST(CheckHit, BlowsAreEvadedByMagicBlockNotEvade) {
    BattleState state = makeAimableBattle();
    battle::Battler& guard = state.at(kGuard);
    guard.evadeFactor = 255;
    guard.magicBlockFactor = 0;
    AttackInFlight attack = blow();
    attack.record.hitRate = 200;
    EXPECT_FALSE(battle::checkHit(state, attack, kTerra, kGuard).hit);

    guard.evadeFactor = 0;
    guard.magicBlockFactor = 255;
    AttackInFlight again = blow();
    again.record.hitRate = 200;
    EXPECT_TRUE(battle::checkHit(state, again, kTerra, kGuard).hit);
}

TEST(CheckHit, TheAttackersBlindnessAndTheTargetsStatusesDoNotChangeTheChance) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).magicBlockFactor = 128;
    const auto rollOf49 = [](std::uint8_t r) { return ((r * 100) >> 8) == 49; };  // chance 50: a hit
    for (bool blind : {false, true}) {
        state.at(kTerra).status = blind ? StatusSet::of(StatusId::BLIND) : StatusSet{};
        state.at(kGuard).status = StatusSet::of(StatusId::HASTE);
        AttackInFlight attack = blow();
        attack.record.hitRate = 100;
        rngWhere(state, {rollOf49});
        EXPECT_TRUE(battle::checkHit(state, attack, kTerra, kGuard).hit) << blind;
    }
}

TEST(CheckHit, AMissedPartyMemberShowsOneOfItsBlocks) {
    BattleState state = makeAimableBattle();
    battle::Battler& terra = state.at(kTerra);
    terra.magicBlockFactor = 0;
    terra.physicalBlockGraphics.add(BlockGraphic::SHIELD);
    AttackInFlight attack = blow();
    attack.canBeCritical = true;
    attack.scratch.weaponSpellcast = ItemSpellCast{0x40};
    const HitResult result = battle::checkHit(state, attack, kGuard, kTerra);
    EXPECT_FALSE(result.hit);
    EXPECT_EQ(result.block, BlockShown::Shield);
    EXPECT_TRUE(attack.scratch.missed.has(kTerra));
    EXPECT_FALSE(attack.canBeCritical);
    EXPECT_FALSE(attack.scratch.weaponSpellcast.randomOnAttack());
}

TEST(CheckHit, QuietMissesAreNotShown) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).magicBlockFactor = 0;
    AttackInFlight attack = blow();
    attack.scratch.quietMisses = true;
    EXPECT_FALSE(battle::checkHit(state, attack, kTerra, kGuard).hit);
    EXPECT_TRUE(attack.scratch.missed.empty());
}

TEST(CheckHit, AZombieOrAMonsterShowsNoBlock) {
    BattleState state = makeAimableBattle();
    state.at(kLocke).magicBlockFactor = 0;
    state.at(kLocke).physicalBlockGraphics.add(BlockGraphic::SWORD);
    state.at(kLocke).status.set(StatusId::ZOMBIE);
    AttackInFlight attack = blow();
    EXPECT_EQ(battle::checkHit(state, attack, kGuard, kLocke).block, BlockShown::None);

    state.at(kGuard).magicBlockFactor = 0;
    state.at(kGuard).physicalBlockGraphics.add(BlockGraphic::SWORD);
    AttackInFlight atMonster = blow();
    EXPECT_EQ(battle::checkHit(state, atMonster, kTerra, kGuard).block, BlockShown::None);
}

}  // namespace
}  // namespace ostinato::test
