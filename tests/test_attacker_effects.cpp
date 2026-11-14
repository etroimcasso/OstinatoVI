// The special effects on the attacker (docs/contracts/battle_effects.md, "The effects on the
// attacker"): every effect the original's table names, each worked out by hand from the original and
// also run on the cartridge's own routine (tools/damage-trace/damage_oracle.h); the port must match
// both. Then the effects every attack, weapon, item and command in the game carries, and the three
// no one carries.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "assets/cartridge.h"
#include "battle/attack.h"
#include "battle/attacker_effects.h"
#include "battle/battle_script.h"
#include "damage_oracle.h"
#include "data/attack_properties.h"
#include "data/item_properties.h"
#include "data/rng_table.h"
#include "ostinato/attack_effects.h"
#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/element_set.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/item_type.h"
#include "ostinato/monster_flags.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using battle::CommandEffect;
using battle::SpecialEffect;
using trace::AttackerEffectResult;
using trace::DamageOracle;
using trace::DamageScenario;

constexpr BattlerSlot kHero = BattlerSlot::character(1);
constexpr BattlerSlot kMonster = BattlerSlot::monster(2);

class AttackerEffectTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    // The port's effect over `scenario`, checked against the cartridge's own answer.
    static trace::AttackerEffectRun checked(const DamageScenario& scenario) {
        trace::AttackerEffectRun run = trace::runAttackerEffect(scenario);
        AttackerEffectResult guest;
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            guest = oracle.attackerEffect(scenario);
        });
        EXPECT_EQ(trace::differences(trace::attackerEffectResult(run), guest), "");
        return run;
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* AttackerEffectTest::image_ = nullptr;

// A battle with a party member and a monster on the field, and an attack by `attacker` carrying
// `effect`, aimed at and hitting the monster.
DamageScenario scenarioWith(SpecialEffect effect, BattlerSlot attacker = kHero) {
    DamageScenario s;
    s.attacker = attacker;
    s.attack.effect = effect;
    s.attack.attackerLevel = 30;
    s.attack.damage = 500;
    s.attack.record.power = 40;
    s.attack.record.hitRate = 100;
    s.attack.targets = BattlerSet::of(kMonster);
    s.attack.params.attacker = attacker;
    s.attack.params.attackerIsMonster = attacker.isMonster();
    s.attack.params.targets = BattlerSet::of(kMonster);
    s.attack.params.targetsHit = BattlerSet::of(kMonster);
    s.attack.params.noCharacterTargets = true;
    s.attack.scratch.targetCount = 1;
    for (const BattlerSlot slot : {kHero, kMonster}) {
        battle::Battler& battler = s.state.at(slot);
        battler.present = true;
        battler.level = 30;
        battler.maxHp = 2000;
        battler.hp = 1500;
        battler.maxMp = 300;
        battler.mp = 200;
    }
    s.state.allyCount = 1;
    return s;
}

// The first random-number position whose next draw satisfies `wanted`.
std::uint8_t positionWhere(const std::function<bool(std::uint8_t)>& wanted) {
    for (int position = 0; position < 256; ++position) {
        if (wanted(rngByte(static_cast<std::uint8_t>(position + 1)))) {
            return static_cast<std::uint8_t>(position);
        }
    }
    ADD_FAILURE() << "no draw in the table satisfies the case";
    return 0;
}

// The draw after `position`.
std::uint8_t drawAfter(std::uint8_t position, int nth = 1) {
    return rngByte(static_cast<std::uint8_t>(position + nth));
}

// --- the weapons -----------------------------------------------------------------------------------

TEST_F(AttackerEffectTest, AThiefknifeStealsOnAnEvenDrawAndAFightIsShownAsCapture) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::THIEFKNIFE));
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return (r & 1) == 0; })};
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.effect, SpecialEffect::of(CommandEffect::Steal));
    EXPECT_EQ(run.after.attack.command, BattleCommandId::CAPTURE);
}

TEST_F(AttackerEffectTest, AThiefknifeStrikesOnAnOddDraw) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::THIEFKNIFE));
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return (r & 1) != 0; })};
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.effect, SpecialEffect::of(WeaponSpecialEffect::THIEFKNIFE));
    EXPECT_EQ(run.after.attack.command, BattleCommandId::FIGHT);
}

TEST_F(AttackerEffectTest, AThiefknifesStealKeepsAnyCommandButFight) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::THIEFKNIFE));
    s.attack.command = BattleCommandId::JUMP;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return (r & 1) == 0; })};
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.effect, SpecialEffect::of(CommandEffect::Steal));
    EXPECT_EQ(run.after.attack.command, BattleCommandId::JUMP);
}

TEST_F(AttackerEffectTest, TheAtmaWeaponIgnoresDefenseAndIsNeverCritical) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::ATMA_WEAPON));
    s.attack.canBeCritical = true;
    const auto run = checked(s);
    EXPECT_TRUE(run.after.attack.record.traits.has(AttackTrait::IGNORE_DEFENSE));
    EXPECT_FALSE(run.after.attack.canBeCritical);
}

TEST_F(AttackerEffectTest, TheDrainerDrainsAndInvertsOnTheUndead) {
    const auto run = checked(scenarioWith(SpecialEffect::of(WeaponSpecialEffect::DRAINER)));
    EXPECT_TRUE(run.after.attack.record.traits.has(AttackTrait::INVERT_ON_UNDEAD));
    EXPECT_TRUE(run.after.attack.record.flags2.has(AttackFlag2::DRAIN));
    EXPECT_FALSE(run.after.attack.record.flags1.has(AttackFlag1::AFFECT_MP));
}

TEST_F(AttackerEffectTest, TheSoulSabreDrainsMp) {
    const auto run = checked(scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SOUL_SABRE)));
    EXPECT_TRUE(run.after.attack.record.flags1.has(AttackFlag1::AFFECT_MP));
    EXPECT_TRUE(run.after.attack.record.flags2.has(AttackFlag2::DRAIN));
    EXPECT_TRUE(run.after.attack.record.traits.has(AttackTrait::INVERT_ON_UNDEAD));
}

TEST_F(AttackerEffectTest, AnMpCriticalIsPaidForWithTwelvePlusADrawOfSeven) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::MP_CRITICAL));
    s.attack.canBeCritical = true;
    s.state.rng = battle::BattleRng{40};
    const int cost = 12 + (drawAfter(40) & 7);
    const auto run = checked(s);
    EXPECT_TRUE(run.after.attack.automaticCritical);
    EXPECT_EQ(run.after.state.at(kHero).mp, 200 - cost);
}

TEST_F(AttackerEffectTest, AnMpCriticalTheAttackerCannotPayForIsNotTaken) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::MP_CRITICAL));
    s.attack.canBeCritical = true;
    s.state.at(kHero).mp = 11;
    const auto run = checked(s);
    EXPECT_FALSE(run.after.attack.automaticCritical);
    EXPECT_EQ(run.after.state.at(kHero).mp, 11);
}

TEST_F(AttackerEffectTest, AnMpCriticalCostingExactlyTheMpIsTaken) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::MP_CRITICAL));
    s.attack.canBeCritical = true;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return (r & 7) == 3; })};
    s.state.at(kHero).mp = 15;
    const auto run = checked(s);
    EXPECT_TRUE(run.after.attack.automaticCritical);
    EXPECT_EQ(run.after.state.at(kHero).mp, 0);
}

TEST_F(AttackerEffectTest, AnMpCriticalNeedsAnAttackThatMayBeCriticalAndHasTargets) {
    DamageScenario cannot = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::MP_CRITICAL));
    cannot.attack.canBeCritical = false;
    const auto run = checked(cannot);
    EXPECT_FALSE(run.after.attack.automaticCritical);
    EXPECT_EQ(run.after.state.at(kHero).mp, 200);
    EXPECT_EQ(run.after.state.rng.position(), 0);

    DamageScenario noTargets = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::MP_CRITICAL));
    noTargets.attack.canBeCritical = true;
    noTargets.attack.scratch.targetCount = 0;
    const auto alone = checked(noTargets);
    EXPECT_FALSE(alone.after.attack.automaticCritical);
    EXPECT_EQ(alone.after.state.rng.position(), 0);
}

TEST_F(AttackerEffectTest, TheDiceRollTwoDiceFromOneDraw) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::DICE));
    s.attack.record.hitRate = 2;
    s.state.rng = battle::BattleRng{70};
    const std::uint8_t draw = drawAfter(70);
    const int first = (draw & 0x0F) % 6 + 1;
    const int second = (draw % 96) / 16 + 1;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.secondary, (second - 1) << 4 | (first - 1));
    EXPECT_EQ(static_cast<int>(run.after.attack.attack), 0x0F);
    EXPECT_EQ(run.after.attack.damage, first * second * 30 * 2);
    EXPECT_FALSE(run.after.attack.scratch.damageModification);
    EXPECT_TRUE(run.after.attack.record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_EQ(run.after.attack.command, static_cast<BattleCommandId>(battle::SpecialAnimation::DiceRoll));
}

TEST_F(AttackerEffectTest, TheFixedDiceThirdDieReadsTheFrameCount) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::DICE));
    s.attack.record.hitRate = 3;
    s.state.playClockFrames = 200;
    s.state.rng = battle::BattleRng{70};
    const std::uint8_t draw = drawAfter(70);
    const int first = (draw & 0x0F) % 6 + 1;
    const int second = (draw % 96) / 16 + 1;
    const int third = 200 % 6 + 1;
    const auto run = checked(s);
    EXPECT_EQ(static_cast<int>(run.after.attack.attack), third - 1);
    const bool allSame = first == second && second == third;
    EXPECT_EQ(run.after.attack.damage, std::min(first * second * third * 60 * (allSame ? third : 1), 65535));
}

TEST_F(AttackerEffectTest, ThreeMatchingDiceMultiplyByTheirFace) {
    // A draw whose two dice both show 2, and play clock frames whose die shows 2 too.
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::DICE));
    s.attack.record.hitRate = 3;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) {
        return (r & 0x0F) % 6 + 1 == 2 && (r % 96) / 16 + 1 == 2;
    })};
    s.state.playClockFrames = 1;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.secondary, 0x11);
    EXPECT_EQ(run.after.attack.damage, 2 * 2 * 2 * 60 * 2);
}

TEST_F(AttackerEffectTest, TheDiceDamageStopsAtTheDamageWord) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::DICE));
    s.attack.record.hitRate = 3;
    s.attack.attackerLevel = 99;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) {
        return (r & 0x0F) % 6 + 1 == 6 && (r % 96) / 16 + 1 == 6;
    })};
    s.state.playClockFrames = 5;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.damage, 65535);
}

TEST_F(AttackerEffectTest, TheDiceShowAnyCommandButFightAsItIs) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::DICE));
    s.attack.command = BattleCommandId::THROW;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.command, BattleCommandId::THROW);
}

TEST_F(AttackerEffectTest, TheValiantKnifeAddsTheHpTheAttackerHasLost) {
    const auto run = checked(scenarioWith(SpecialEffect::of(WeaponSpecialEffect::VALIANTKNIFE)));
    EXPECT_EQ(run.after.attack.damage, 500 + (2000 - 1500));
    EXPECT_TRUE(run.after.attack.record.traits.has(AttackTrait::IGNORE_DEFENSE));
}

TEST_F(AttackerEffectTest, TheTempestCastsWindSlashBelowHalf) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::TEMPEST));
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < 0x80; })};
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.scratch.followUpSpell, AttackId::WIND_SLASH);
    EXPECT_EQ(run.after.attack.record.power, 0);
    EXPECT_EQ(run.after.attack.scratch.attacksLeft, 1);
}

TEST_F(AttackerEffectTest, TheTempestStrikesFromHalfUp) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::TEMPEST));
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r == 0x80 || r > 0xF0; })};
    const auto run = checked(s);
    EXPECT_FALSE(run.after.attack.scratch.followUpSpell);
    EXPECT_EQ(run.after.attack.record.power, 40);
}

TEST_F(AttackerEffectTest, TheHealRodRestoresAndIgnoresDefense) {
    const auto run = checked(scenarioWith(SpecialEffect::of(WeaponSpecialEffect::HEAL_ROD)));
    EXPECT_TRUE(run.after.attack.record.flags2.has(AttackFlag2::RESTORE_HP_MP));
    EXPECT_TRUE(run.after.attack.record.traits.has(AttackTrait::IGNORE_DEFENSE));
}

// Ogre Nix in the weapon hand of party member 1, swinging with the weapon hand (an odd count of
// attacks still to come).
DamageScenario ogreNixSwing(bool shieldHand) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::OGRE_NIX));
    s.attack.scratch.attacksLeft = shieldHand ? 0 : 1;
    battle::BattleItem nix{.item = ItemId::OGRE_NIX, .quantity = 1};
    s.state.inventory.weaponHands[1] = nix;
    s.state.inventory.shieldHands[1] = nix;
    s.state.at(kHero).weaponHand.battlePower = 117;
    s.state.at(kHero).shieldHand.battlePower = 117;
    // HP 1500: its last digit is 0, so one draw in one breaks it.
    return s;
}

TEST_F(AttackerEffectTest, OgreNixBreaksOneTimeInTheLastDigitOfTheHpPlusOne) {
    const auto run = checked(ogreNixSwing(false));
    const battle::BattleState& after = run.after.state;
    EXPECT_EQ(after.inventory.weaponHands[1].item, ItemId::EMPTY);
    EXPECT_EQ(after.inventory.weaponHands[1].quantity, 0);
    EXPECT_EQ(after.inventory.shieldHands[1].item, ItemId::OGRE_NIX);
    EXPECT_EQ(after.at(kHero).weaponHand.battlePower, 0);
    EXPECT_EQ(after.at(kHero).shieldHand.battlePower, 117);
    EXPECT_TRUE(after.at(kHero).equipmentChanged);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageOgreNixBroken);
}

TEST_F(AttackerEffectTest, OgreNixBreaksInTheShieldHandOnAnEvenCount) {
    const auto run = checked(ogreNixSwing(true));
    EXPECT_EQ(run.after.state.inventory.shieldHands[1].item, ItemId::EMPTY);
    EXPECT_EQ(run.after.state.inventory.weaponHands[1].item, ItemId::OGRE_NIX);
    EXPECT_EQ(run.after.state.at(kHero).shieldHand.battlePower, 0);
}

TEST_F(AttackerEffectTest, OgreNixDrawsAgainstTheHpsLastDigit) {
    DamageScenario s = ogreNixSwing(false);
    s.state.at(kHero).hp = 1509;
    // RandA(10): 0 only on a draw below 26.
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r >= 26; })};
    const auto kept = checked(s);
    EXPECT_EQ(kept.after.state.inventory.weaponHands[1].item, ItemId::OGRE_NIX);
    EXPECT_FALSE(kept.after.state.at(kHero).equipmentChanged);

    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < 26; })};
    const auto broken = checked(s);
    EXPECT_EQ(broken.after.state.inventory.weaponHands[1].item, ItemId::EMPTY);
}

TEST_F(AttackerEffectTest, OgreNixLosesItsPowerButNotAnotherWeaponWhenItBreaks) {
    DamageScenario s = ogreNixSwing(false);
    s.state.inventory.weaponHands[1].item = ItemId::DIRK;
    const auto run = checked(s);
    EXPECT_EQ(run.after.state.inventory.weaponHands[1].item, ItemId::DIRK);
    EXPECT_EQ(run.after.state.at(kHero).weaponHand.battlePower, 0);
    EXPECT_FALSE(run.after.attack.scratch.message);
}

TEST_F(AttackerEffectTest, OgreNixNeverBreaksInACounterattackOrWithTheMenuOpen) {
    DamageScenario counter = ogreNixSwing(false);
    counter.state.counterattackRunning = true;
    EXPECT_EQ(checked(counter).after.state.inventory.weaponHands[1].item, ItemId::OGRE_NIX);

    DamageScenario menu = ogreNixSwing(false);
    menu.state.at(kHero).inBattleMenu = true;
    EXPECT_EQ(checked(menu).after.state.inventory.weaponHands[1].item, ItemId::OGRE_NIX);
}

TEST_F(AttackerEffectTest, OgreNixAlsoPaysForACriticalWithMp) {
    DamageScenario s = ogreNixSwing(false);
    s.attack.canBeCritical = true;
    const auto run = checked(s);
    EXPECT_TRUE(run.after.attack.automaticCritical);
    EXPECT_LT(run.after.state.at(kHero).mp, 200);
}

// --- the attacks -----------------------------------------------------------------------------------

TEST_F(AttackerEffectTest, TheUnusedMpCriticalCostsTwentyEight) {
    // The effect at $0F, which nothing carries, as the table would run it.
    DamageScenario s = scenarioWith(SpecialEffect::of(static_cast<WeaponSpecialEffect>(0x0F)));
    s.attack.canBeCritical = true;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return (r & 7) == 0; })};
    s.state.at(kHero).mp = 28;
    const auto run = checked(s);
    EXPECT_TRUE(run.after.attack.automaticCritical);
    EXPECT_EQ(run.after.state.at(kHero).mp, 0);
}

TEST_F(AttackerEffectTest, GolemTakesTheCastersHp) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::GOLEM)));
    EXPECT_EQ(run.after.state.golemHp, 1500);
}

// Sonic Dive passes over a battler in each of its five statuses, and over no one else.
class SonicDiveTest : public AttackerEffectTest, public ::testing::WithParamInterface<StatusId> {};

TEST_P(SonicDiveTest, PassesOverTheStatus) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::PALIDOR));
    const BattlerSlot other = BattlerSlot::monster(4);
    s.state.at(other).present = true;
    s.attack.params.targets = BattlerSet::of(kMonster) | BattlerSet::of(other);
    s.attack.params.targetsHit = s.attack.params.targets;
    s.state.at(other).status.set(GetParam());
    const auto run = checked(s);
    EXPECT_TRUE(run.after.state.endTurnAfterAction);
    EXPECT_EQ(run.after.attack.params.targets, BattlerSet::of(kMonster));
    EXPECT_EQ(run.after.attack.params.targetsHit, BattlerSet::of(kMonster));
}

INSTANTIATE_TEST_SUITE_P(EachStatus, SonicDiveTest,
                         ::testing::Values(StatusId::PETRIFY, StatusId::SLEEP, StatusId::STOP, StatusId::FROZEN,
                                           StatusId::HIDE));

TEST_F(AttackerEffectTest, SonicDiveDoesNotPassOverTheDead) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::PALIDOR));
    s.state.at(kMonster).status.set(StatusId::DEAD);
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.params.targetsHit, BattlerSet::of(kMonster));
}

TEST_F(AttackerEffectTest, MantraSharesTheCastersHpAmongTheOthers) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::MANTRA));
    const BattlerSet party = BattlerSet::of(kHero) | BattlerSet::of(BattlerSlot::character(0)) |
                             BattlerSet::of(BattlerSlot::character(2));
    s.attack.params.targets = party;
    s.attack.params.targetsHit = party;
    s.attack.scratch.targetCount = 3;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.damage, 1500 / 2);
    EXPECT_FALSE(run.after.attack.params.targetsHit.has(kHero));
    EXPECT_TRUE(run.after.attack.record.traits.has(AttackTrait::IGNORE_DEFENSE));
    EXPECT_TRUE(run.after.attack.record.traits.has(AttackTrait::NO_DAMAGE_SPLIT));
    EXPECT_FALSE(run.after.attack.scratch.damageModification);
}

TEST_F(AttackerEffectTest, AMantraWithNoOneElseDoes65535) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::MANTRA));
    s.attack.params.targets = BattlerSet::of(kHero);
    s.attack.params.targetsHit = BattlerSet::of(kHero);
    s.attack.scratch.targetCount = 1;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.damage, 65535);
    EXPECT_TRUE(run.after.attack.params.targetsHit.empty());
}

TEST_F(AttackerEffectTest, SpiralerGivesUpThePartyMember) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::SPIRALER));
    s.attack.params.targets = BattlerSet::of(kHero) | BattlerSet::of(BattlerSlot::character(0));
    s.attack.params.targetsHit = s.attack.params.targets;
    const auto run = checked(s);
    const battle::Battler& caster = run.after.state.at(kHero);
    EXPECT_EQ(caster.hp, 0);
    EXPECT_EQ(caster.mp, 0);
    EXPECT_TRUE(caster.statusesToSet.has(StatusId::HIDE));
    EXPECT_TRUE(caster.statusesToSet.has(StatusId::DEAD));
    EXPECT_EQ(caster.lastAttacker, kHero);
    EXPECT_FALSE(caster.retaliationPending);
    EXPECT_TRUE(run.after.state.leavingAfterAction.has(kHero));
    EXPECT_EQ(run.after.attack.params.targetsHit, BattlerSet::of(BattlerSlot::character(0)));
}

TEST_F(AttackerEffectTest, AMonstersSpiralerLeavesItAmongItsTargets) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::SPIRALER), kMonster);
    s.attack.params.targets = BattlerSet::of(kMonster) | BattlerSet::of(BattlerSlot::monster(0));
    s.attack.params.targetsHit = s.attack.params.targets;
    const auto run = checked(s);
    EXPECT_TRUE(run.after.attack.params.targetsHit.has(kMonster));
    EXPECT_FALSE(run.after.state.leavingAfterAction.has(kMonster));
    EXPECT_EQ(run.after.state.at(kMonster).hp, 0);
    EXPECT_TRUE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
}

TEST_F(AttackerEffectTest, SpiralerLeavesAMarkAlreadyWaitingAlone) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::SPIRALER));
    s.state.at(kHero).lastAttacker = kMonster;
    s.state.at(kHero).retaliationPending = true;
    const auto run = checked(s);
    EXPECT_EQ(run.after.state.at(kHero).lastAttacker, kMonster);
    EXPECT_TRUE(run.after.state.at(kHero).retaliationPending);
}

TEST_F(AttackerEffectTest, WarpEndsTheBattleAsTimeRunningOut) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::WARP)));
    EXPECT_EQ(run.after.state.endOfBattleEvent, battle::EndOfBattleEvent::RanOutOfTime);
    EXPECT_EQ(run.after.attack.params.targetsHit, BattlerSet::of(kMonster));
}

TEST_F(AttackerEffectTest, WarpWhereThePartyCannotRunHitsNoOne) {
    DamageScenario s = scenarioWith(SpecialEffect::of(ItemUseEffect::WARP_STONE));
    s.state.cannotRun = true;
    const auto run = checked(s);
    EXPECT_FALSE(run.after.state.endOfBattleEvent);
    EXPECT_TRUE(run.after.attack.params.targetsHit.empty());
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageCantEscape);
}

TEST_F(AttackerEffectTest, ExploderIsShownOnItsUserAndThenItsTargets) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::EXPLODER), kMonster);
    s.attack.multiplier = 3;
    s.attack.castingSwirlSuppressed = false;
    s.attack.params.targets = BattlerSet::of(kHero);
    s.attack.params.targetsHit = BattlerSet::of(kHero);
    const auto run = checked(s);
    const battle::AttackInFlight& attack = run.after.attack;
    EXPECT_EQ(attack.multiplier, 0);
    EXPECT_EQ(attack.damage, 1500);
    EXPECT_FALSE(attack.scratch.damageModification);
    EXPECT_EQ(attack.params.targetsHit, BattlerSet::of(kHero) | BattlerSet::of(kMonster));
    EXPECT_EQ(attack.params.targets, BattlerSet::of(kHero));
    EXPECT_TRUE(attack.params.castingSwirl);
    battle::BattleScriptBuilder script = run.script;
    const std::vector<battle::BattleScriptCommand> commands = script.take();
    ASSERT_EQ(commands.size(), 1u);
    const auto& shown = std::get<battle::AttackAnimation>(commands.front());
    EXPECT_EQ(shown.params.targets, BattlerSet::of(kMonster));
    EXPECT_FALSE(shown.params.castingSwirl);
}

TEST_F(AttackerEffectTest, BlowFishDoes1000) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::BLOW_FISH)));
    EXPECT_EQ(run.after.attack.damage, 1000);
    EXPECT_TRUE(run.after.attack.record.traits.has(AttackTrait::NO_DAMAGE_SPLIT));
    EXPECT_FALSE(run.after.attack.scratch.damageModification);
}

TEST_F(AttackerEffectTest, PearlWindHealsByTheCastersHp) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::PEARL_WIND)));
    EXPECT_EQ(run.after.attack.damage, 1500);
    EXPECT_TRUE(run.after.attack.record.traits.has(AttackTrait::IGNORE_DEFENSE));
}

TEST_F(AttackerEffectTest, ReflectLoreHitsOnlyTheBattlersWithReflect) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::REFLECT_LORE), kMonster);
    const BattlerSet party = BattlerSet::of(kHero) | BattlerSet::of(BattlerSlot::character(0));
    s.attack.params.targetsHit = party;
    s.state.at(BattlerSlot::character(0)).status.set(StatusId::REFLECT);
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.params.targetsHit, BattlerSet::of(BattlerSlot::character(0)));
}

TEST_F(AttackerEffectTest, LPearlsHitRateIsTheLastDigitOfTheGil) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::PEARL_LORE));
    s.state.party.gil = 9'876'547;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.record.hitRate, 7);
}

TEST_F(AttackerEffectTest, StepMineIsTheStepsOverThePower) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::STEP_MINE));
    s.state.party.steps = 123'456;
    s.attack.record.power = 32;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.damage, 123'456 / 32);
    EXPECT_FALSE(run.after.attack.scratch.damageModification);
}

TEST_F(AttackerEffectTest, StepMineStopsAt65535) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::STEP_MINE));
    s.state.party.steps = 9'999'999;
    s.attack.record.power = 32;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.damage, 65535);

    // Exactly 65,535 steps' worth fits; one power-step more does not.
    s.state.party.steps = 65535u * 32u;
    EXPECT_EQ(checked(s).after.attack.damage, 65535);
    s.state.party.steps = 65536u * 32u;
    EXPECT_EQ(checked(s).after.attack.damage, 65535);
    s.state.party.steps = 65534u * 32u + 31u;
    EXPECT_EQ(checked(s).after.attack.damage, 65534);
}

TEST_F(AttackerEffectTest, EscapeByAPartyMemberIsShownAsThePartyRunning) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::ESCAPE)));
    EXPECT_EQ(run.after.attack.command, static_cast<BattleCommandId>(battle::SpecialAnimation::PartyRunsAway));
    EXPECT_TRUE(run.after.attack.params.castingSwirl);
}

TEST_F(AttackerEffectTest, EscapeByAMonsterIsShownAsItIs) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::ESCAPE), kMonster));
    EXPECT_EQ(run.after.attack.command, BattleCommandId::FIGHT);
    EXPECT_FALSE(run.after.attack.params.castingSwirl);
}

TEST_F(AttackerEffectTest, MindBlastPicksOneBattlerHitForEachPartyPosition) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::MIND_BLAST), kMonster);
    const BattlerSet hit = BattlerSet::of(kHero) | BattlerSet::of(BattlerSlot::character(3)) | BattlerSet::of(kMonster);
    s.attack.params.targetsHit = hit;
    s.state.rng = battle::BattleRng{90};
    const auto run = checked(s);
    // Position 3 draws first: each a third of the way along the hit battlers, party members first.
    const std::array<BattlerSlot, 3> order{kHero, BattlerSlot::character(3), kMonster};
    for (int position = 3; position >= 0; --position) {
        const int nth = 4 - position;
        const std::uint8_t pick = static_cast<std::uint8_t>((drawAfter(90, nth) * 3) >> 8);
        EXPECT_EQ(run.after.attack.scratch.mindBlastPicks[static_cast<std::size_t>(position)], order[pick]);
    }
}

TEST_F(AttackerEffectTest, MindBlastWithNoOneHitPicksNoOne) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::MIND_BLAST), kMonster);
    s.attack.params.targetsHit = {};
    const auto run = checked(s);
    for (const auto& pick : run.after.attack.scratch.mindBlastPicks) {
        EXPECT_FALSE(pick);
    }
    EXPECT_EQ(run.after.state.rng.position(), 0);
}

TEST_F(AttackerEffectTest, NCrossMissesARandomHandfulOnEachSide) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::N_CROSS), kMonster);
    s.attack.params.targetsHit = BattlerSet::allCharacters() | BattlerSet::allMonsters();
    s.state.rng = battle::BattleRng{17};
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.params.targetsHit.characters.bits, 0x0F & ~drawAfter(17, 1));
    EXPECT_EQ(run.after.attack.params.targetsHit.monsters.bits, 0x3F & ~drawAfter(17, 2));
}

TEST_F(AttackerEffectTest, FlareStarDividesByTheTargetsTheLevelOfOne) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::FLARE_STAR));
    const BattlerSlot other = BattlerSlot::monster(0);
    s.state.at(other).level = 45;
    s.attack.params.targets = BattlerSet::of(other) | BattlerSet::of(kMonster);
    s.state.rng = battle::BattleRng{5};
    const BattlerSlot drawn = ((drawAfter(5) * 2) >> 8) == 0 ? other : kMonster;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.damage, 40 * run.after.state.at(drawn).level / 2);
}

TEST_F(AttackerEffectTest, FlareStarWithNoTargetsIs65535) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::FLARE_STAR));
    s.attack.params.targets = {};
    EXPECT_EQ(checked(s).after.attack.damage, 65535);
}

TEST_F(AttackerEffectTest, ALauncherMakesEightHitsFromItsAttacker) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::LAUNCHER), kMonster));
    EXPECT_EQ(run.after.state.extraHits, 8);
    EXPECT_EQ(run.after.attack.params.reflectedOff, BattlerSet::of(kMonster));
}

TEST_F(AttackerEffectTest, ASuperBallMakesOneToFourHits) {
    DamageScenario s = scenarioWith(SpecialEffect::of(ItemUseEffect::SUPER_BALL));
    s.state.rng = battle::BattleRng{33};
    const auto run = checked(s);
    EXPECT_EQ(run.after.state.extraHits, (drawAfter(33) & 3) + 1);
    EXPECT_EQ(run.after.attack.params.reflectedOff, BattlerSet::of(kHero));
    EXPECT_EQ(run.after.attack.attack, AttackId::BIO_BLAST_TOOL);
}

TEST_F(AttackerEffectTest, SuplexPicksAMonsterThatCanBeThrown) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::SUPLEX));
    const BattlerSlot heavy = BattlerSlot::monster(0);
    s.state.at(heavy).present = true;
    s.state.at(heavy).monsterFlags.set(MonsterBattleFlag::CANT_SUPLEX);
    s.attack.params.targets = BattlerSet::of(heavy) | BattlerSet::of(kMonster);
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.targets, BattlerSet::of(kMonster));
    EXPECT_EQ(run.after.attack.params.targets, BattlerSet::of(kMonster));
    EXPECT_EQ(run.after.attack.params.targetsHit, BattlerSet::of(kMonster));
    EXPECT_FALSE(run.after.attack.params.castingSwirl);
}

TEST_F(AttackerEffectTest, SuplexWithNoneThrowablePicksAmongThemAll) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::SUPLEX));
    s.state.at(kMonster).monsterFlags.set(MonsterBattleFlag::CANT_SUPLEX);
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.params.targets, BattlerSet::of(kMonster));
}

TEST_F(AttackerEffectTest, ForcefieldNullifiesAnElementAndSaysWhich) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::FORCEFIELD), kMonster);
    s.state.elementsNullifiedByForcefield.bits = 0xFE;  // only the first element is left
    const auto run = checked(s);
    EXPECT_EQ(run.after.state.elementsNullifiedByForcefield.bits, 0xFF);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageElementIneffectual);
}

TEST_F(AttackerEffectTest, ForcefieldDrawsAmongTheElementsLeft) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::FORCEFIELD), kMonster);
    s.state.elementsNullifiedByForcefield.bits = 0x0F;
    s.state.rng = battle::BattleRng{60};
    const int nth = (drawAfter(60) * 4) >> 8;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageElementIneffectual + 4 + nth);
    EXPECT_EQ(run.after.state.elementsNullifiedByForcefield.bits, 0x0F | (0x10 << nth));
}

TEST_F(AttackerEffectTest, AForcefieldWithEveryElementNullifiedHitsNoOne) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::FORCEFIELD), kMonster);
    s.state.elementsNullifiedByForcefield.bits = 0xFF;
    const auto run = checked(s);
    EXPECT_TRUE(run.after.attack.params.targetsHit.empty());
    EXPECT_FALSE(run.after.attack.scratch.message);
}

TEST_F(AttackerEffectTest, QuadraMakesFourAttacksAtRandomTargets) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::QUADRA_SLAM_SLICE)));
    EXPECT_EQ(run.after.attack.scratch.attacksLeft, 3);
    EXPECT_TRUE(run.after.attack.rules.randomTarget);
    EXPECT_TRUE(run.after.attack.effect.isNone());
}

TEST_F(AttackerEffectTest, BababreathBlowsAwayTheLowestFallenPartyMember) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::BABABREATH), kMonster);
    for (const int position : {0, 2, 3}) {
        s.state.at(BattlerSlot::character(position)).present = true;
    }
    s.state.at(BattlerSlot::character(3)).status.set(StatusId::ZOMBIE);
    s.state.at(BattlerSlot::character(2)).status.set(StatusId::PETRIFY);
    s.state.at(BattlerSlot::character(0)).status.set(StatusId::DEAD);
    s.state.awayFromTheField.add(BattlerSlot::character(0));
    s.attack.targets = BattlerSet::of(kHero);
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.targets, BattlerSet::of(BattlerSlot::character(2)));
    EXPECT_EQ(run.after.attack.params.targetsHit, BattlerSet::of(BattlerSlot::character(2)));
}

TEST_F(AttackerEffectTest, BababreathWithNoOneFallenGoesOnWithTwoAllies) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::BABABREATH), kMonster);
    s.attack.params.targetsHit = BattlerSet::of(kHero);
    s.state.allyCount = 2;
    EXPECT_EQ(checked(s).after.attack.params.targetsHit, BattlerSet::of(kHero));
    s.state.allyCount = 1;
    EXPECT_TRUE(checked(s).after.attack.params.targetsHit.empty());
}

TEST_F(AttackerEffectTest, EmpowererOnAffectsMpAndIsNotShown) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::EMPOWERER), kMonster));
    EXPECT_TRUE(run.after.attack.record.flags1.has(AttackFlag1::AFFECT_MP));
    EXPECT_EQ(run.after.attack.command, battle::kNothingShown);
    EXPECT_EQ(run.after.attack.scratch.attacksLeft, 0);
}

TEST_F(AttackerEffectTest, EmpowererOffIsMadeAgainAtAQuarterOfThePower) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::EMPOWERER), kMonster);
    s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
    s.attack.record.power = 43;
    const auto run = checked(s);
    EXPECT_FALSE(run.after.attack.record.flags1.has(AttackFlag1::AFFECT_MP));
    EXPECT_EQ(run.after.attack.record.power, 10);
    EXPECT_EQ(run.after.attack.scratch.attacksLeft, 1);
    EXPECT_EQ(run.after.attack.command, BattleCommandId::FIGHT);
}

TEST_F(AttackerEffectTest, RevengeIsTheHpTheAttackerHasLost) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::REVENGE), kMonster));
    EXPECT_EQ(run.after.attack.damage, 500);
    EXPECT_FALSE(run.after.attack.scratch.damageModification);
}

TEST_F(AttackerEffectTest, TheUncarriedEffectsHalveAndQuarterTheDamage) {
    DamageScenario s = scenarioWith(SpecialEffect::of(static_cast<AttackSpecialEffect>(0x41)));
    s.attack.damage = 999;
    EXPECT_EQ(checked(s).after.attack.damage, 499);
    s.attack.effect = SpecialEffect::of(static_cast<AttackSpecialEffect>(0x42));
    EXPECT_EQ(checked(s).after.attack.damage, 249);
}

TEST_F(AttackerEffectTest, QuickGivesTheCasterTwoTurns) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::QUICK)));
    EXPECT_EQ(run.after.state.quickTarget, kHero);
    EXPECT_EQ(run.after.state.quickTurnsLeft, 2);
    EXPECT_TRUE(run.after.attack.scratch.missed.empty());
}

TEST_F(AttackerEffectTest, QuickWhileAnotherIsQuickShowsTheCasterAMiss) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::QUICK));
    s.state.quickTarget = BattlerSlot::character(0);
    s.state.quickTurnsLeft = 0;  // just ran out: still not castable
    const auto run = checked(s);
    EXPECT_EQ(run.after.state.quickTarget, BattlerSlot::character(0));
    EXPECT_TRUE(run.after.attack.scratch.missed.has(kHero));
}

TEST_F(AttackerEffectTest, DiscardHitsOnlyTheBattlerHeld) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::DISCARD), kMonster);
    s.state.at(kMonster).seizing = kHero;
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.targets, BattlerSet::of(kHero));
    EXPECT_EQ(run.after.attack.params.targetsHit, BattlerSet::of(kHero));
    EXPECT_TRUE(run.after.attack.record.flags2.has(AttackFlag2::UNDODGEABLE));
}

TEST_F(AttackerEffectTest, DiscardWithNoOneHeldHitsNoOne) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::DISCARD), kMonster));
    EXPECT_TRUE(run.after.attack.params.targetsHit.empty());
    EXPECT_TRUE(run.after.attack.record.flags2.has(AttackFlag2::UNDODGEABLE));
}

// --- the items -------------------------------------------------------------------------------------

TEST_F(AttackerEffectTest, MagiciteSummonsAnEspersButNotOdinOrRaiden) {
    DamageScenario s = scenarioWith(SpecialEffect::of(ItemUseEffect::MAGICITE));
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return ((r * 25) >> 8) == 11; })};
    const auto run = checked(s);
    EXPECT_EQ(run.after.attack.scratch.followUpSpell,
              static_cast<AttackId>(static_cast<int>(AttackId::RAMUH) + 13));
    EXPECT_EQ(run.after.attack.scratch.attacksLeft, 1);
}

TEST_F(AttackerEffectTest, ASmokeBombWhereThePartyCannotRunDoesNothing) {
    DamageScenario s = scenarioWith(SpecialEffect::of(ItemUseEffect::SMOKE_BOMB));
    s.state.cannotRun = true;
    const auto run = checked(s);
    EXPECT_TRUE(run.after.attack.params.targetsHit.empty());
    EXPECT_TRUE(run.after.attack.effect.isNone());
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageCantRunAway);

    s.state.cannotRun = false;
    EXPECT_EQ(checked(s).after.attack.effect, SpecialEffect::of(ItemUseEffect::SMOKE_BOMB));
}

TEST_F(AttackerEffectTest, AnElixirRestoresHpFirst) {
    DamageScenario s = scenarioWith(SpecialEffect::of(ItemUseEffect::ELIXIR));
    s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
    EXPECT_FALSE(checked(s).after.attack.record.flags1.has(AttackFlag1::AFFECT_MP));
}

// --- the commands ----------------------------------------------------------------------------------

TEST_F(AttackerEffectTest, PossessMissesFrom150Up) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Possess));
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r == 150 || (r > 150 && r < 160); })};
    EXPECT_TRUE(checked(s).after.attack.params.targetsHit.empty());
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r == 149 || (r > 140 && r < 150); })};
    EXPECT_EQ(checked(s).after.attack.params.targetsHit, BattlerSet::of(kMonster));
}

TEST_F(AttackerEffectTest, GpRainByAPartyMemberThrowsThirtyGilALevel) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::GpRain));
    s.state.party.gil = 5000;
    s.attack.scratch.targetCount = 2;
    const auto run = checked(s);
    EXPECT_EQ(run.after.state.party.gil, 5000u - 900u);
    EXPECT_EQ(run.after.attack.damage, 900 * 2 / 2);
}

TEST_F(AttackerEffectTest, GpRainBeyondThePartysGilThrowsAllOfIt) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::GpRain));
    s.state.party.gil = 250;
    const auto run = checked(s);
    EXPECT_EQ(run.after.state.party.gil, 0u);
    EXPECT_EQ(run.after.attack.damage, 250 * 2);
}

TEST_F(AttackerEffectTest, GpRainWithNoGilSaysNoMoney) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::GpRain));
    s.state.party.gil = 0;
    const auto run = checked(s);
    EXPECT_TRUE(run.after.attack.params.targetsHit.empty());
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageNoMoney);
    EXPECT_EQ(run.after.state.party.gil, 0u);
}

TEST_F(AttackerEffectTest, GpRainByAMonsterSpendsItsGold) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::GpRain), kMonster);
    s.state.at(kMonster).gold = 2000;
    s.state.party.gil = 777;
    const auto run = checked(s);
    EXPECT_EQ(run.after.state.at(kMonster).gold, 2000 - 900);
    EXPECT_EQ(run.after.state.party.gil, 777u);
    EXPECT_EQ(run.after.attack.damage, 1800);
}

TEST_F(AttackerEffectTest, GpRainByAMonsterWithLessGoldThrowsWhatItHas) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::GpRain), kMonster);
    s.state.at(kMonster).gold = 100;
    const auto run = checked(s);
    EXPECT_EQ(run.after.state.at(kMonster).gold, 0);
    EXPECT_EQ(run.after.attack.damage, 200);

    s.state.at(kMonster).gold = 0;
    const auto broke = checked(s);
    EXPECT_TRUE(broke.after.attack.params.targetsHit.empty());
    EXPECT_EQ(broke.after.attack.scratch.message, battle::kMessageNoMoney);
}

TEST(TakeGil, TakesWhatItAsksOrAllThereIs) {
    game::Party party;
    party.gil = 1000;
    EXPECT_EQ(battle::takeGil(party, 400), 400u);
    EXPECT_EQ(party.gil, 600u);
    EXPECT_EQ(battle::takeGil(party, 600), 600u);
    EXPECT_EQ(party.gil, 0u);
    party.gil = 70'000;
    EXPECT_EQ(battle::takeGil(party, 65'535), 65'535u);
    EXPECT_EQ(party.gil, 4'465u);
    EXPECT_EQ(battle::takeGil(party, 5'000), 4'465u);
    EXPECT_EQ(party.gil, 0u);
}

// --- every effect, and who carries it --------------------------------------------------------------

TEST_F(AttackerEffectTest, AnEffectThatActsOnlyOnTargetsLeavesTheAttackAlone) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::DOOM));
    const auto run = checked(s);
    EXPECT_EQ(trace::differences(trace::attackerEffectResult(run),
                                 trace::attackerEffectResult(trace::AttackerEffectRun{.after = s})),
              "");
}

// Every attack's own effect, every weapon's, every consumable's and every command's.
std::set<std::uint8_t> carriedEffects() {
    std::set<std::uint8_t> carried;
    for (int id = 0; id < 256; ++id) {
        carried.insert(SpecialEffect::of(getAttackProperties(static_cast<AttackId>(id)).specialEffect).index());
    }
    for (int id = 0; id < 256; ++id) {
        if (static_cast<ItemId>(id) == ItemId::EMPTY) {
            continue;
        }
        const ItemProperties& item = getItemProperties(static_cast<ItemId>(id));
        const ItemType type = item.typeAndUsage.type();
        if (type == ItemType::WEAPON) {
            carried.insert(SpecialEffect::of(item.specialEffect.weaponEffect()).index());
        }
        if (type == ItemType::CONSUMABLE && !item.specialEffect.itemUseDisabled() && item.specialEffect.packed != 0) {
            carried.insert(SpecialEffect::of(item.specialEffect.itemUseEffect()).index());
        }
    }
    for (int effect = static_cast<int>(CommandEffect::Possess); effect <= static_cast<int>(CommandEffect::AirAnchor);
         ++effect) {
        carried.insert(SpecialEffect::of(static_cast<CommandEffect>(effect)).index());
    }
    return carried;
}

TEST(AttackerEffectCarriers, EveryEffectOnTheAttackerButThreeHasACarrier) {
    const std::set<std::uint8_t> carried = carriedEffects();
    const std::vector<std::uint8_t> carriedRows{
        0x01, 0x02, 0x05, 0x06, 0x07, 0x09, 0x0A, 0x0B, 0x0C, 0x0E, 0x11, 0x13, 0x15, 0x16,
        0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x27, 0x28, 0x29, 0x2A, 0x2C, 0x30, 0x31,
        0x32, 0x33, 0x36, 0x3D, 0x43, 0x44, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x50, 0x51,
    };
    for (const std::uint8_t row : carriedRows) {
        EXPECT_TRUE(carried.contains(row)) << "effect " << int{row};
    }
    for (const std::uint8_t row : {std::uint8_t{0x0F}, std::uint8_t{0x41}, std::uint8_t{0x42}}) {
        EXPECT_FALSE(carried.contains(row)) << "effect " << int{row};
    }
}

}  // namespace
}  // namespace ostinato::test
