// How an attack is aimed: the targeting byte's read side, the command's aiming byte, the four ways
// a command loads its attack, who an attack ends up reaching, and cover. Behaviors and their
// sources: docs/contracts/battle_commands.md § Aiming.

#include <gtest/gtest.h>

#include "battle/attack.h"
#include "battle/targeting.h"
#include "battle_test_support.h"
#include "data/attack_properties.h"
#include "data/battle_commands.h"
#include "data/item_properties.h"
#include "ostinato/command_targeting_init.h"
#include "ostinato/targeting.h"

namespace ostinato::test {
namespace {

using battle::AttackInFlight;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleState;

const BattlerSlot kTerra = BattlerSlot::character(0);
const BattlerSlot kLocke = BattlerSlot::character(1);
const BattlerSlot kGuard = BattlerSlot::monster(0);
const BattlerSlot kSoldier = BattlerSlot::monster(1);

// The byte rebuilt from its read accessors.
std::uint8_t rebuilt(Targeting t) {
    return static_cast<std::uint8_t>((t.manual() ? 0x01 : 0) | (t.oneSideOnly() ? 0x02 : 0) |
                                     (static_cast<std::uint8_t>(t.initialSelection()) << 2) |
                                     (t.autoConfirm() ? 0x10 : 0) | (t.multiTarget() ? 0x20 : 0) |
                                     (t.enemyByDefault() ? 0x40 : 0) | (t.roulette() ? 0x80 : 0));
}

AttackInFlight aimedAt(BattlerSet targets, Targeting targeting) {
    AttackInFlight attack;
    attack.targets = targets;
    attack.targeting = targeting;
    return attack;
}

// --- the targeting byte --------------------------------------------------------------------------

TEST(TargetingByte, SelfOnlyIsTheWholeByte) {
    EXPECT_TRUE(Targeting{0x02}.isSelfOnly());
    EXPECT_FALSE(Targeting{0x03}.isSelfOnly());
    EXPECT_FALSE(Targeting::of(TargetFlags::SELF, TargetFlags::ENEMY).isSelfOnly());
    EXPECT_TRUE(Targeting{0x02}.oneSideOnly());
}

TEST(TargetingByte, MenuDefersToTheAttack) {
    EXPECT_TRUE(Targeting{0xFF}.usesAttackDefault());
    EXPECT_FALSE(Targeting{0xFE}.usesAttackDefault());
}

TEST(TargetingByte, InitialSelectionIsTheTwoMiddleBits) {
    EXPECT_EQ(Targeting::of(TargetFlags::INIT_SINGLE).initialSelection(), InitialSelection::Single);
    EXPECT_EQ(Targeting::of(TargetFlags::INIT_ALL).initialSelection(), InitialSelection::All);
    EXPECT_EQ(Targeting::of(TargetFlags::INIT_GROUP).initialSelection(), InitialSelection::Group);
    EXPECT_EQ(Targeting::of(TargetFlags::INIT_HALF).initialSelection(), InitialSelection::Half);
}

TEST(TargetingByte, EachFlagReadsItsOwnBit) {
    EXPECT_TRUE(Targeting{0x01}.manual());
    EXPECT_TRUE(Targeting{0x10}.autoConfirm());
    EXPECT_TRUE(Targeting{0x20}.multiTarget());
    EXPECT_TRUE(Targeting{0x40}.enemyByDefault());
    EXPECT_TRUE(Targeting{0x80}.roulette());
    const Targeting none{0x00};
    EXPECT_FALSE(none.manual() || none.oneSideOnly() || none.autoConfirm() || none.multiTarget() ||
                 none.enemyByDefault() || none.roulette());
}

TEST(TargetingByte, KeepsEveryTargetWhenMultiOrStartingOnMoreThanOne) {
    EXPECT_FALSE(Targeting::of(TargetFlags::ENEMY).keepsEveryTarget());
    EXPECT_TRUE(Targeting::of(TargetFlags::MULTI_TARGET).keepsEveryTarget());
    EXPECT_TRUE(Targeting::of(TargetFlags::INIT_ALL).keepsEveryTarget());
    EXPECT_TRUE(Targeting::of(TargetFlags::INIT_GROUP).keepsEveryTarget());
}

TEST(TargetingByte, EveryRecordsByteIsItsAccessors) {
    for (const AttackPropertiesEntry& entry : attackPropertiesEn()) {
        EXPECT_EQ(rebuilt(entry.record.targeting), entry.record.targeting.bits) << static_cast<int>(entry.id);
    }
    for (const BattleCommandPropertiesEntry& entry : battleCommandProperties()) {
        if (!entry.record.targeting.usesAttackDefault()) {
            EXPECT_EQ(rebuilt(entry.record.targeting), entry.record.targeting.bits);
        }
    }
    for (const ItemPropertiesEntry& entry : itemPropertiesEn()) {
        EXPECT_EQ(rebuilt(entry.record.targeting), entry.record.targeting.bits) << static_cast<int>(entry.id);
    }
}

// --- the command's aiming byte -------------------------------------------------------------------

TEST(CommandAimingByte, ItemMayReachTheFallenAndKeepsItsTargets) {
    const CommandTargetingInit item = commandTargetingInit(BattleCommandId::ITEM);
    EXPECT_TRUE(item.mayTargetTheFallen());
    EXPECT_TRUE(item.keepsTargetsThatAreGone());
    EXPECT_EQ(item.attackLoad(), CommandAttackLoad::Item);
    const CommandTargetingInit fight = commandTargetingInit(BattleCommandId::FIGHT);
    EXPECT_FALSE(fight.mayTargetTheFallen());
    EXPECT_FALSE(fight.keepsTargetsThatAreGone());
    EXPECT_TRUE(fight.fallBackToEarlierTargets());
    EXPECT_EQ(fight.attackLoad(), CommandAttackLoad::PlainHit);
}

TEST(CommandAimingByte, TheLoadsAreTheFourHandlers) {
    EXPECT_EQ(commandTargetingInit(BattleCommandId::MAGIC).attackLoad(), CommandAttackLoad::Spell);
    EXPECT_EQ(commandTargetingInit(BattleCommandId::THROW).attackLoad(), CommandAttackLoad::ThrownItem);
    EXPECT_EQ(commandTargetingInit(BattleCommandId::TOOLS).attackLoad(), CommandAttackLoad::ThrownItem);
    EXPECT_EQ(commandTargetingInit(BattleCommandId::MORPH).attackLoad(), CommandAttackLoad::PlainHit);
    EXPECT_TRUE(commandTargetingInit(BattleCommandId::CONTROL).noCharacterTargets());
    EXPECT_TRUE(commandTargetingInit(BattleCommandId::JUMP).excludesTheUser());
}

TEST(CommandAimingByte, EveryRowsRulesAreItsFields) {
    for (const CommandTargetingInitEntry& entry : kCommandTargetingInit) {
        const CommandTargetingInit init = entry.init;
        const std::uint8_t direct = static_cast<std::uint8_t>((init.noCharacterTargets() ? 0x80 : 0) |
                                                              (init.randomTarget() ? 0x40 : 0) |
                                                              (init.fallBackToEarlierTargets() ? 0x20 : 0) |
                                                              (init.excludesTheUser() ? 0x01 : 0));
        const std::uint8_t rules = static_cast<std::uint8_t>((init.mayTargetTheFallen() ? 0x10 : 0) |
                                                             (init.keepsTargetsThatAreGone() ? 0x08 : 0));
        EXPECT_EQ(direct, init.directBits());
        EXPECT_EQ(rules, init.initialTarget());
        EXPECT_EQ(static_cast<std::uint8_t>(init.attackLoad()) << 1, init.dispatchIndex());
    }
}

// --- loading the attack --------------------------------------------------------------------------

TEST(InitTargeting, APlainHitLoadsBattleUnderTheCommandsTargeting) {
    AttackInFlight attack;
    EXPECT_FALSE(battle::initTargeting(attack, BattleCommandId::FIGHT, AttackId{}));
    EXPECT_EQ(attack.record.power, getAttackProperties(AttackId::BATTLE).power);
    EXPECT_EQ(attack.record.flags1.bits, getAttackProperties(AttackId::BATTLE).flags1.bits);
    EXPECT_EQ(attack.targeting.bits, battleCommandProperties(BattleCommandId::FIGHT).targeting.bits);
    EXPECT_TRUE(attack.rules.fallBackToEarlierTargets);
    EXPECT_FALSE(attack.scratch.lastSpell);
}

TEST(InitTargeting, ASpellIsRememberedAndAddsItsRules) {
    AttackInFlight attack;
    EXPECT_FALSE(battle::initTargeting(attack, BattleCommandId::MAGIC, AttackId::LIFE));
    EXPECT_EQ(attack.scratch.lastSpell, AttackId::LIFE);
    ASSERT_TRUE(getAttackProperties(AttackId::LIFE).traits.has(AttackTrait::RESURRECTION_TARGET));
    EXPECT_TRUE(attack.rules.mayTargetTheFallen);
    const Targeting commands = battleCommandProperties(BattleCommandId::MAGIC).targeting;
    const Targeting expected = commands.usesAttackDefault() ? getAttackProperties(AttackId::LIFE).targeting : commands;
    EXPECT_EQ(attack.targeting.bits, expected.bits);
}

TEST(InitTargeting, RandomAndNoPartyTraitsBecomeRules) {
    for (const AttackPropertiesEntry& entry : attackPropertiesEn()) {
        AttackInFlight attack;
        battle::initTargeting(attack, BattleCommandId::MAGIC, entry.id);
        EXPECT_EQ(attack.rules.randomTarget, entry.record.traits.has(AttackTrait::RANDOM_TARGET));
        EXPECT_EQ(attack.rules.noCharacterTargets, entry.record.traits.has(AttackTrait::NO_CHARACTER_TARGET));
    }
}

TEST(InitTargeting, AConsumableIsTheAttackItself) {
    AttackInFlight attack;
    attack.record.power = 77;
    EXPECT_TRUE(battle::initTargeting(attack, BattleCommandId::ITEM, static_cast<AttackId>(ItemId::TONIC)));
    EXPECT_EQ(attack.scratch.lastItem, ItemId::TONIC);
    EXPECT_EQ(attack.targeting.bits, getItemProperties(ItemId::TONIC).targeting.bits);
    EXPECT_FALSE(attack.rules.mayTargetTheFallen);
    EXPECT_EQ(attack.record.power, 77);  // no record is loaded
}

TEST(InitTargeting, OnlyAnItemThatRaisesTheDeadReachesTheFallen) {
    AttackInFlight attack;
    EXPECT_TRUE(battle::initTargeting(attack, BattleCommandId::ITEM, static_cast<AttackId>(ItemId::FENIX_DOWN)));
    EXPECT_TRUE(attack.rules.mayTargetTheFallen);
}

TEST(InitTargeting, TheCommandsAimingByteGivesItsTwoTargetRules) {
    AttackInFlight item;
    battle::initTargeting(item, BattleCommandId::ITEM, static_cast<AttackId>(ItemId::TONIC));
    EXPECT_TRUE(item.rules.keepTargetsThatAreGone);
    AttackInFlight morph;
    battle::initTargeting(morph, BattleCommandId::MORPH, AttackId{});
    EXPECT_TRUE(morph.rules.keepTargetsThatAreGone);
    EXPECT_TRUE(morph.rules.mayTargetTheFallen);
    AttackInFlight fight;
    battle::initTargeting(fight, BattleCommandId::FIGHT, AttackId{});
    EXPECT_FALSE(fight.rules.keepTargetsThatAreGone);
    EXPECT_FALSE(fight.rules.mayTargetTheFallen);
}

TEST(InitTargeting, EquipmentUsedAsAnItemCastsItsSpell) {
    const ItemSpellCast cast = getItemProperties(ItemId::FIRE_ROD).spellCast;
    ASSERT_TRUE(cast.castOnItemUse());
    AttackInFlight attack;
    EXPECT_FALSE(battle::initTargeting(attack, BattleCommandId::ITEM, static_cast<AttackId>(ItemId::FIRE_ROD)));
    EXPECT_EQ(attack.scratch.lastItem, ItemId::FIRE_ROD);
    EXPECT_EQ(attack.scratch.lastSpell, cast.spell());
    EXPECT_FALSE(attack.scratch.itemKept);
    EXPECT_EQ(attack.record.power, getAttackProperties(cast.spell()).power);
}

TEST(InitTargeting, AThrownEdgeBecomesItsSpell) {
    AttackInFlight attack;
    EXPECT_FALSE(battle::initTargeting(attack, BattleCommandId::THROW, static_cast<AttackId>(ItemId::FIRE_SKEAN)));
    EXPECT_EQ(attack.scratch.lastSpell, AttackId::FIRE_SKEAN);

    AttackInFlight dirk;
    EXPECT_TRUE(battle::initTargeting(dirk, BattleCommandId::THROW, static_cast<AttackId>(ItemId::DIRK)));
    EXPECT_EQ(dirk.scratch.lastItem, ItemId::DIRK);
    EXPECT_EQ(dirk.targeting.bits, getItemProperties(ItemId::DIRK).targeting.bits);
}

TEST(InitTargeting, ACommandWithNoAimingByteAimsAtTheOtherSide) {
    AttackInFlight attack;
    attack.rules.excludeAttacker = true;
    battle::initTargeting(attack, static_cast<BattleCommandId>(0x22), AttackId{});
    EXPECT_EQ(attack.targeting.bits, Targeting::of(TargetFlags::ENEMY).bits);
    EXPECT_FALSE(attack.rules.excludeAttacker);
    EXPECT_EQ(attack.record.flags1.bits, getAttackProperties(AttackId::BATTLE).flags1.bits);
}

// --- who an attack reaches -----------------------------------------------------------------------

TEST(ChooseTargets, SelfOnlyAimsAtTheAttackerAlone) {
    BattleState state = makeAimableBattle();
    AttackInFlight attack = aimedAt(BattlerSet::of(kGuard), Targeting{0x02});
    battle::chooseTargets(state, attack, kTerra);
    EXPECT_EQ(attack.targets, BattlerSet::of(kTerra));
}

TEST(ChooseTargets, OneOfTheGivenTargetsIsPickedByTheRandomNumbers) {
    BattleState state = makeAimableBattle();
    const BattlerSet given = BattlerSet::of(kGuard) | BattlerSet::of(kSoldier);
    battle::BattleRng copy = state.rng;
    const auto expected = copy.pickFrom(given);
    AttackInFlight attack = aimedAt(given, Targeting::of(TargetFlags::ENEMY));
    battle::chooseTargets(state, attack, kTerra);
    EXPECT_EQ(attack.targets, BattlerSet::of(*expected));
    EXPECT_EQ(state.rng.position(), copy.position());
}

TEST(ChooseTargets, AnAttackThatMayHitThemAllKeepsThemAll) {
    BattleState state = makeAimableBattle();
    const BattlerSet given = BattlerSet::of(kGuard) | BattlerSet::of(kSoldier);
    AttackInFlight attack = aimedAt(given, Targeting::of(TargetFlags::MULTI_TARGET));
    const std::uint8_t before = state.rng.position();
    battle::chooseTargets(state, attack, kTerra);
    EXPECT_EQ(attack.targets, given);
    EXPECT_EQ(state.rng.position(), before);
}

TEST(ChooseTargets, TargetsOutOfReachAreDropped) {
    BattleState state = makeAimableBattle();
    state.at(kLocke).seizedBy = kGuard;
    state.aiUntargetable.add(kSoldier);
    state.awayFromTheField.add(kTerra);
    const BattlerSet everyone = BattlerSet::of(kTerra) | BattlerSet::of(kLocke) | BattlerSet::of(kGuard) |
                                BattlerSet::of(kSoldier);
    AttackInFlight attack = aimedAt(everyone, Targeting::of(TargetFlags::INIT_ALL));
    battle::chooseTargets(state, attack, kGuard);
    EXPECT_EQ(attack.targets, BattlerSet::of(kGuard));

    state.scratch.countersEnabled = false;
    AttackInFlight again = aimedAt(everyone, Targeting::of(TargetFlags::INIT_ALL));
    battle::chooseTargets(state, again, kGuard);
    EXPECT_EQ(again.targets, BattlerSet::of(kTerra) | BattlerSet::of(kGuard));
}

TEST(ChooseTargets, AimingExactlySkipsTheReachCheckOnce) {
    BattleState state = makeAimableBattle();
    state.aiUntargetable.add(kSoldier);
    AttackInFlight attack = aimedAt(BattlerSet::of(kSoldier), Targeting::of(TargetFlags::INIT_ALL));
    attack.scratch.aimExactly = true;
    battle::chooseTargets(state, attack, kTerra);
    EXPECT_EQ(attack.targets, BattlerSet::of(kSoldier));
    EXPECT_FALSE(attack.scratch.aimExactly);
}

TEST(ChooseTargets, TheAttacksOwnExclusions) {
    BattleState state = makeAimableBattle();
    const BattlerSet everyone = BattlerSet::of(kTerra) | BattlerSet::of(kLocke) | BattlerSet::of(kGuard);
    AttackInFlight noParty = aimedAt(everyone, Targeting::of(TargetFlags::INIT_ALL));
    noParty.rules.noCharacterTargets = true;
    battle::chooseTargets(state, noParty, kTerra);
    EXPECT_EQ(noParty.targets, BattlerSet::of(kGuard));

    AttackInFlight notSelf = aimedAt(everyone, Targeting::of(TargetFlags::INIT_ALL));
    notSelf.rules.excludeAttacker = true;
    battle::chooseTargets(state, notSelf, kTerra);
    EXPECT_EQ(notSelf.targets, BattlerSet::of(kLocke) | BattlerSet::of(kGuard));
}

TEST(ChooseTargets, AGoneTargetIsReplacedFromTheDefaultSide) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).present = false;
    battle::BattleRng copy = state.rng;
    const auto expected = copy.pickFrom(BattlerSet::of(kSoldier));
    AttackInFlight attack = aimedAt(BattlerSet::of(kGuard), Targeting::of(TargetFlags::ENEMY));
    battle::chooseTargets(state, attack, kTerra);
    EXPECT_EQ(attack.targets, BattlerSet::of(*expected));
}

TEST(ChooseTargets, AGoneTargetStaysGoneWhenTheRulesSaySo) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).present = false;
    AttackInFlight attack = aimedAt(BattlerSet::of(kGuard), Targeting::of(TargetFlags::ENEMY));
    attack.rules.keepTargetsThatAreGone = true;
    battle::chooseTargets(state, attack, kTerra);
    EXPECT_TRUE(attack.targets.empty());
}

TEST(ChooseTargets, AimingAtASideStartsFromTheAttackersOwn) {
    BattleState state = makeAimableBattle();
    const BattlerSet party = BattlerSet::of(kTerra) | BattlerSet::of(kLocke);
    const BattlerSet monsters = BattlerSet::of(kGuard) | BattlerSet::of(kSoldier);
    const Targeting all = Targeting::of(TargetFlags::INIT_GROUP);
    const Targeting allEnemies = Targeting::of(TargetFlags::INIT_GROUP, TargetFlags::ENEMY);

    AttackInFlight own = aimedAt({}, all);
    battle::chooseTargets(state, own, kTerra);
    EXPECT_EQ(own.targets, party);
    AttackInFlight other = aimedAt({}, allEnemies);
    battle::chooseTargets(state, other, kTerra);
    EXPECT_EQ(other.targets, monsters);
    AttackInFlight monsterOwn = aimedAt({}, all);
    battle::chooseTargets(state, monsterOwn, kGuard);
    EXPECT_EQ(monsterOwn.targets, monsters);
    AttackInFlight monsterOther = aimedAt({}, allEnemies);
    battle::chooseTargets(state, monsterOther, kGuard);
    EXPECT_EQ(monsterOther.targets, party);
}

TEST(ChooseTargets, ConfuseAndCharmEachTurnTheSide) {
    BattleState state = makeAimableBattle();
    const Targeting allEnemies = Targeting::of(TargetFlags::INIT_GROUP, TargetFlags::ENEMY);
    const BattlerSet party = BattlerSet::of(kTerra) | BattlerSet::of(kLocke);
    const BattlerSet monsters = BattlerSet::of(kGuard) | BattlerSet::of(kSoldier);

    state.at(kTerra).status.set(StatusId::CONFUSE);
    AttackInFlight confused = aimedAt({}, allEnemies);
    battle::chooseTargets(state, confused, kTerra);
    EXPECT_EQ(confused.targets, party);

    state.at(kTerra).charmedBy = kGuard;
    AttackInFlight both = aimedAt({}, allEnemies);
    battle::chooseTargets(state, both, kTerra);
    EXPECT_EQ(both.targets, monsters);
}

TEST(ChooseTargets, AZombieAndAnInitialAllReachBothSides) {
    BattleState state = makeAimableBattle();
    const BattlerSet everyone = BattlerSet::of(kTerra) | BattlerSet::of(kLocke) | BattlerSet::of(kGuard) |
                                BattlerSet::of(kSoldier);
    AttackInFlight all = aimedAt({}, Targeting::of(TargetFlags::INIT_ALL, TargetFlags::ENEMY));
    battle::chooseTargets(state, all, kTerra);
    EXPECT_EQ(all.targets, everyone);

    state.at(kTerra).status.set(StatusId::ZOMBIE);
    AttackInFlight zombie = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP, TargetFlags::ENEMY));
    rngWhere(state, {heads});
    battle::chooseTargets(state, zombie, kTerra);
    // Both sides are aimed at, then the attack keeps one of them on a coin: heads, the party.
    EXPECT_EQ(zombie.targets, BattlerSet::of(kTerra) | BattlerSet::of(kLocke));
}

TEST(ChooseTargets, ASpellBouncedOffReflectGoesToTheOtherSide) {
    BattleState state = makeAimableBattle();
    AttackInFlight attack = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP, TargetFlags::ENEMY));
    attack.rules.reflected = true;
    battle::chooseTargets(state, attack, kGuard);
    // The enemy-first flag is not read: a bounce from a monster goes to the party.
    EXPECT_EQ(attack.targets, BattlerSet::of(kTerra) | BattlerSet::of(kLocke));
}

TEST(ChooseTargets, HalfTheTimeABouncedSpellHitsItsCaster) {
    BattleState state = makeAimableBattle();
    AttackInFlight attack = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP));
    attack.rules.reflected = true;
    attack.rules.opposingSideOnly = true;
    rngWhere(state, {tails});
    battle::chooseTargets(state, attack, kTerra, kGuard);
    EXPECT_EQ(attack.targets, BattlerSet::of(kGuard));

    AttackInFlight lucky = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP));
    lucky.rules.reflected = true;
    lucky.rules.opposingSideOnly = true;
    rngWhere(state, {heads});
    battle::chooseTargets(state, lucky, kTerra, kGuard);
    EXPECT_EQ(lucky.targets, BattlerSet::of(kGuard) | BattlerSet::of(kSoldier));
}

TEST(ChooseTargets, TheMonsterSideIncludesThePartyMembersFightingTheParty) {
    BattleState state = makeAimableBattle();
    state.at(kLocke).actsAsEnemy = true;
    // Terra aims at the monsters' side, which holds Locke as well; aimed at a party member and
    // monsters at once, the attack keeps one of them on a coin.
    AttackInFlight heads_ = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP, TargetFlags::ENEMY));
    rngWhere(state, {heads});
    battle::chooseTargets(state, heads_, kTerra);
    EXPECT_EQ(heads_.targets, BattlerSet::of(kLocke));
    AttackInFlight tails_ = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP, TargetFlags::ENEMY));
    rngWhere(state, {tails});
    battle::chooseTargets(state, tails_, kTerra);
    EXPECT_EQ(tails_.targets, BattlerSet::of(kGuard) | BattlerSet::of(kSoldier));

    AttackInFlight own = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP));
    battle::chooseTargets(state, own, kTerra);
    EXPECT_EQ(own.targets, BattlerSet::of(kTerra));
}

TEST(ChooseTargets, InAPincerAMonsterReachesOnlyItsOwnSide) {
    BattleState state = makeAimableBattle();
    state.battleType = battle::BattleType::Pincer;
    state.pincerSides = {BattleSlotMask(0x01), BattleSlotMask(0x02)};
    AttackInFlight attack = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP));
    battle::chooseTargets(state, attack, kGuard);
    EXPECT_EQ(attack.targets, BattlerSet::of(kGuard));
}

TEST(ChooseTargets, InAPincerAPartyMemberKeepsOneSideByACoin) {
    BattleState state = makeAimableBattle();
    state.battleType = battle::BattleType::Pincer;
    state.pincerSides = {BattleSlotMask(0x01), BattleSlotMask(0x02)};
    AttackInFlight heads_ = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP, TargetFlags::ENEMY));
    rngWhere(state, {heads});
    battle::chooseTargets(state, heads_, kTerra);
    EXPECT_EQ(heads_.targets, BattlerSet::of(kSoldier));

    AttackInFlight tails_ = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP, TargetFlags::ENEMY));
    rngWhere(state, {tails});
    battle::chooseTargets(state, tails_, kTerra);
    EXPECT_EQ(tails_.targets, BattlerSet::of(kGuard));
}

TEST(ChooseTargets, InASideAttackAPartyMemberReachesOnlyItsHalf) {
    BattleState state = makeAimableBattle();
    state.battleType = battle::BattleType::Side;
    state.at(BattlerSlot::character(2)).present = true;
    state.targetableOrPresent.add(BattlerSlot::character(2));
    AttackInFlight attack = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP));
    battle::chooseTargets(state, attack, kTerra);
    EXPECT_EQ(attack.targets, BattlerSet::of(kTerra) | BattlerSet::of(kLocke));

    AttackInFlight right = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP));
    battle::chooseTargets(state, right, BattlerSlot::character(2));
    EXPECT_EQ(right.targets, BattlerSet::of(BattlerSlot::character(2)));
}

TEST(ChooseTargets, AnInitialAllIsNotSplitByTheBattleLayout) {
    BattleState state = makeAimableBattle();
    state.battleType = battle::BattleType::Pincer;
    state.pincerSides = {BattleSlotMask(0x01), BattleSlotMask(0x02)};
    AttackInFlight attack = aimedAt({}, Targeting::of(TargetFlags::INIT_ALL, TargetFlags::ENEMY));
    battle::chooseTargets(state, attack, kGuard);
    EXPECT_EQ(attack.targets.count(), 4);
}

TEST(ChooseTargets, AimedAtBothSidesTheAttackKeepsOneByACoin) {
    BattleState state = makeAimableBattle();
    AttackInFlight heads_ = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP));
    state.at(kTerra).status.set(StatusId::ZOMBIE);
    rngWhere(state, {heads});
    battle::chooseTargets(state, heads_, kTerra);
    EXPECT_EQ(heads_.targets, BattlerSet::of(kTerra) | BattlerSet::of(kLocke));

    AttackInFlight tails_ = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP));
    rngWhere(state, {tails});
    battle::chooseTargets(state, tails_, kTerra);
    EXPECT_EQ(tails_.targets, BattlerSet::of(kGuard) | BattlerSet::of(kSoldier));
}

TEST(ChooseTargets, WithNoOneLeftTheCommandsEarlierTargetsAreUsed) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).present = false;
    state.at(kSoldier).present = false;
    AttackInFlight attack = aimedAt({}, Targeting::of(TargetFlags::INIT_GROUP, TargetFlags::ENEMY));
    attack.rules.fallBackToEarlierTargets = true;
    attack.scratch.earlierTargets = BattlerSet::of(kGuard);
    battle::chooseTargets(state, attack, kTerra);
    EXPECT_EQ(attack.targets, BattlerSet::of(kGuard));
}

TEST(ChooseTargets, AMultiTargetAttackReAimedKeepsTheWholeSideOnHeads) {
    BattleState state = makeAimableBattle();
    const Targeting multi = Targeting::of(TargetFlags::MULTI_TARGET, TargetFlags::ENEMY);
    AttackInFlight kept = aimedAt({}, multi);
    rngWhere(state, {heads});
    battle::chooseTargets(state, kept, kTerra);
    EXPECT_EQ(kept.targets, BattlerSet::of(kGuard) | BattlerSet::of(kSoldier));

    AttackInFlight one = aimedAt({}, multi);
    rngWhere(state, {tails, any});
    battle::chooseTargets(state, one, kTerra);
    EXPECT_EQ(one.targets.count(), 1);
}

TEST(RemoveAbsentTargets, APartyMemberLeavesWhenDeadAMonsterAlsoWhenStoneOrZombie) {
    BattleState state = makeAimableBattle();
    const BattlerSet everyone = BattlerSet::of(kTerra) | BattlerSet::of(kLocke) | BattlerSet::of(kGuard) |
                                BattlerSet::of(kSoldier);
    state.at(kTerra).status.set(StatusId::PETRIFY);
    state.at(kLocke).status.set(StatusId::ZOMBIE);
    state.at(kGuard).status.set(StatusId::PETRIFY);
    state.at(kSoldier).status.set(StatusId::ZOMBIE);
    EXPECT_EQ(battle::removeAbsentTargets(state, everyone), BattlerSet::of(kTerra) | BattlerSet::of(kLocke));

    state.at(kTerra).status.set(StatusId::DEAD);
    state.at(kLocke).status.set(StatusId::HIDE);
    EXPECT_TRUE(battle::removeAbsentTargets(state, everyone).empty());
}

TEST(RemoveAbsentTargets, ABattlerNotOnTheFieldLeaves) {
    BattleState state = makeAimableBattle();
    state.at(kGuard).present = false;
    EXPECT_EQ(battle::removeAbsentTargets(state, BattlerSet::of(kGuard) | BattlerSet::of(kSoldier)),
              BattlerSet::of(kSoldier));
}

// --- cover ---------------------------------------------------------------------------------------

BattleState coverBattle() {
    BattleState state = makeAimableBattle();
    for (int position = 2; position < 4; ++position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        state.at(slot).present = true;
        state.at(slot).hp = 100;
        state.targetableOrPresent.add(slot);
    }
    state.at(kTerra).status.set(StatusId::NEAR_FATAL);
    return state;
}

AttackInFlight blowAt(BattlerSlot target) {
    AttackInFlight attack;
    attack.targets = BattlerSet::of(target);
    attack.canBeCritical = true;
    return attack;
}

TEST(CoverTarget, ATrueKnightStepsInFrontOfANearFatalAlly) {
    BattleState state = coverBattle();
    state.at(kLocke).relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
    AttackInFlight attack = blowAt(kTerra);
    battle::coverTarget(state, attack, kGuard);
    EXPECT_EQ(attack.targets, BattlerSet::of(kLocke));
    EXPECT_EQ(attack.params.coveredAlly, kTerra);
    EXPECT_EQ(attack.params.steppingIn, BattlerSet::of(kLocke));
}

TEST(CoverTarget, OnlyABlowThatCanBeCriticalIsCovered) {
    BattleState state = coverBattle();
    state.at(kLocke).relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
    AttackInFlight attack = blowAt(kTerra);
    attack.canBeCritical = false;
    battle::coverTarget(state, attack, kGuard);
    EXPECT_EQ(attack.targets, BattlerSet::of(kTerra));
    EXPECT_FALSE(attack.params.coveredAlly);
}

TEST(CoverTarget, TheKnightWithTheMostHpCoversAndTiesGoToTheLowestSlot) {
    BattleState state = coverBattle();
    for (int position = 1; position < 4; ++position) {
        state.at(BattlerSlot::character(position)).relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
    }
    state.at(BattlerSlot::character(2)).hp = 300;
    state.at(BattlerSlot::character(3)).hp = 300;
    AttackInFlight attack = blowAt(kTerra);
    battle::coverTarget(state, attack, kGuard);
    EXPECT_EQ(attack.targets, BattlerSet::of(BattlerSlot::character(2)));
    EXPECT_EQ(attack.params.steppingIn.count(), 3);
}

TEST(CoverTarget, NoCoverForAHealthyOrVanishedAlly) {
    BattleState state = coverBattle();
    state.at(kLocke).relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
    state.at(kTerra).status.clear(StatusId::NEAR_FATAL);
    AttackInFlight healthy = blowAt(kTerra);
    battle::coverTarget(state, healthy, kGuard);
    EXPECT_EQ(healthy.targets, BattlerSet::of(kTerra));

    state.at(kTerra).status.set(StatusId::NEAR_FATAL);
    state.at(kTerra).status.set(StatusId::INVISIBLE);
    AttackInFlight vanished = blowAt(kTerra);
    battle::coverTarget(state, vanished, kGuard);
    EXPECT_EQ(vanished.targets, BattlerSet::of(kTerra));
}

TEST(CoverTarget, ABodyguardMustBeFreeToMove) {
    BattleState state = coverBattle();
    state.at(kLocke).relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
    for (StatusId status : {StatusId::SLEEP, StatusId::INVISIBLE, StatusId::CHANT, StatusId::STOP}) {
        state.at(kLocke).status = StatusSet::of(status);
        AttackInFlight attack = blowAt(kTerra);
        battle::coverTarget(state, attack, kGuard);
        EXPECT_EQ(attack.targets, BattlerSet::of(kTerra)) << static_cast<int>(status);
    }
    state.at(kLocke).status = {};
    state.at(kGuard).controlledBy = kLocke;
    AttackInFlight controlling = blowAt(kTerra);
    battle::coverTarget(state, controlling, kSoldier);
    EXPECT_EQ(controlling.targets, BattlerSet::of(kTerra));
}

TEST(CoverTarget, ALoveTokenBodyguardCoversFirstWhateverTheAllysHp) {
    BattleState state = coverBattle();
    state.at(kTerra).status = {};
    state.at(kTerra).bodyguard = BattlerSlot::character(3);
    state.at(kLocke).relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
    AttackInFlight attack = blowAt(kTerra);
    battle::coverTarget(state, attack, kGuard);
    EXPECT_EQ(attack.targets, BattlerSet::of(BattlerSlot::character(3)));
    EXPECT_EQ(attack.params.coveredAlly, kTerra);
}

TEST(CoverTarget, TheCoveredTargetIsTheHighestNumberedOne) {
    BattleState state = coverBattle();
    state.at(kLocke).status.set(StatusId::NEAR_FATAL);
    state.at(BattlerSlot::character(3)).relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
    AttackInFlight attack = blowAt(kTerra);
    attack.targets.add(kLocke);
    battle::coverTarget(state, attack, kGuard);
    EXPECT_EQ(attack.params.coveredAlly, kLocke);
}

}  // namespace
}  // namespace ostinato::test
