// The attack frame: Fight's swings, the MP a spell costs, Runic, Air Anchor, a weapon's spell, the
// name shown, a monster's special attack, the critical hit, the hit-in-the-back steps, what a hit
// target records, and the executor that runs it all. Behaviors and their sources:
// docs/contracts/battle_commands.md § The attack frame.

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "battle/attack.h"
#include "battle/command_executor.h"
#include "battle/commands.h"
#include "battle/targeting.h"
#include "battle_test_support.h"
#include "data/attack_properties.h"

namespace ostinato::test {
namespace {

using battle::AttackAnimation;
using battle::AttackInFlight;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleScriptBuilder;
using battle::BattleScriptCommand;
using battle::BattleState;
using battle::CommandAction;

const BattlerSlot kTerra = BattlerSlot::character(0);
const BattlerSlot kLocke = BattlerSlot::character(1);
const BattlerSlot kGuard = BattlerSlot::monster(0);
const BattlerSlot kSoldier = BattlerSlot::monster(1);

// A party member armed in both hands and a battle where every blow lands.
BattleState armedBattle(std::uint8_t shieldHandPower = 40) {
    BattleState state = makeAimableBattle();
    battle::Battler& terra = state.at(kTerra);
    terra.vigor = 60;
    terra.level = 10;
    terra.weaponHand = battle::Hand{.item = ItemId::DIRK, .battlePower = 50, .hitRate = 255};
    terra.shieldHand = battle::Hand{.item = ItemId::FIRE_ROD, .battlePower = shieldHandPower, .hitRate = 255};
    state.at(kGuard).weaponHand = battle::Hand{.battlePower = 30, .hitRate = 255};
    return state;
}

std::vector<AttackAnimation> animations(const std::vector<BattleScriptCommand>& script) {
    std::vector<AttackAnimation> found;
    for (const BattleScriptCommand& command : script) {
        if (const auto* animation = std::get_if<AttackAnimation>(&command)) {
            found.push_back(*animation);
        }
    }
    return found;
}

template <typename T>
int countOf(const std::vector<BattleScriptCommand>& script) {
    int n = 0;
    for (const BattleScriptCommand& command : script) {
        n += std::holds_alternative<T>(command) ? 1 : 0;
    }
    return n;
}

std::vector<BattleScriptCommand> fight(BattleState& state, battle::AttackHooks& hooks, BattlerSlot actor,
                                       BattlerSet targets, BattleCommandId command = BattleCommandId::FIGHT) {
    AttackInFlight attack = battle::startAttack(CommandAction{.command = command, .targets = targets});
    BattleScriptBuilder script;
    battle::fightAttack(state, hooks, script, attack, actor);
    return script.take();
}

// A spell cast by `caster`, set up as the Magic command sets it up.
std::vector<BattleScriptCommand> cast(BattleState& state, battle::AttackHooks& hooks, BattlerSlot caster,
                                      AttackId spell, BattlerSet targets, std::uint8_t mpCost = 0,
                                      void (*adjust)(AttackInFlight&) = nullptr) {
    AttackInFlight attack = battle::startAttack(
        CommandAction{.command = BattleCommandId::MAGIC, .attack = spell, .targets = targets, .mpCost = mpCost});
    battle::initTargeting(attack, BattleCommandId::MAGIC, spell);
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    if (adjust != nullptr) {
        adjust(attack);
    }
    BattleScriptBuilder script;
    battle::runAttack(state, hooks, script, attack, caster);
    return script.take();
}

// --- Fight -----------------------------------------------------------------------------------

TEST(AttackFrame, FightSwingsTheWeaponHandThenTheShieldHand) {
    BattleState state = armedBattle();
    battle::AttackHooks hooks;
    rngWhere(state, {any, atLeast(8), any, atLeast(8)});
    const auto swings = animations(fight(state, hooks, kTerra, BattlerSet::of(kGuard)));
    ASSERT_EQ(swings.size(), 2u);
    EXPECT_EQ(swings[0].what, (std::variant<BattleCommandId, battle::SpecialAnimation>{BattleCommandId::FIGHT}));
    EXPECT_FALSE(swings[0].offHand);
    EXPECT_EQ(swings[0].secondary, static_cast<std::uint8_t>(ItemId::DIRK) + 1);
    EXPECT_TRUE(swings[1].offHand);
    EXPECT_EQ(swings[1].secondary, static_cast<std::uint8_t>(ItemId::FIRE_ROD) + 1);
    EXPECT_EQ(swings[0].params.attacker, kTerra);
    EXPECT_EQ(swings[0].params.targetsHit, BattlerSet::of(kGuard));
}

TEST(AttackFrame, AHandWithNoPowerMakesNoSwing) {
    BattleState state = armedBattle(0);
    battle::AttackHooks hooks;
    EXPECT_EQ(animations(fight(state, hooks, kTerra, BattlerSet::of(kGuard))).size(), 1u);
}

TEST(AttackFrame, AMonsterSwingsOnce) {
    BattleState state = armedBattle();
    battle::AttackHooks hooks;
    const auto swings = animations(fight(state, hooks, kGuard, BattlerSet::of(kTerra)));
    ASSERT_EQ(swings.size(), 1u);
    EXPECT_TRUE(swings[0].params.attackerIsMonster);
}

TEST(AttackFrame, AnOfferingMakesEightBlowsThatAreNeverCritical) {
    BattleState state = armedBattle();
    state.at(kTerra).relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
    battle::AttackHooks hooks;
    const auto swings = animations(fight(state, hooks, kTerra, BattlerSet::of(kGuard)));
    ASSERT_EQ(swings.size(), 8u);
    for (const AttackAnimation& swing : swings) {
        EXPECT_FALSE(swing.params.criticalFlash);
    }
}

TEST(AttackFrame, CaptureIsAFightThatSteals) {
    BattleState state = armedBattle(0);
    RecordingHooks hooks;
    fight(state, hooks, kTerra, BattlerSet::of(kGuard), BattleCommandId::CAPTURE);
    ASSERT_EQ(hooks.effectsAtBaseDamage.size(), 1u);
    EXPECT_EQ(hooks.effectsAtBaseDamage[0], battle::SpecialEffect::of(battle::CommandEffect::Steal));
}

TEST(AttackFrame, TheFirstSwingMayReAimAndTheSecondMayNot) {
    BattleState state = armedBattle();
    RecordingHooks hooks;
    fight(state, hooks, kTerra, BattlerSet::of(kGuard));
    ASSERT_EQ(hooks.rulesAtBaseDamage.size(), 2u);
    EXPECT_FALSE(hooks.rulesAtBaseDamage[0].keepTargetsThatAreGone);
    EXPECT_TRUE(hooks.rulesAtBaseDamage[1].keepTargetsThatAreGone);
}

TEST(AttackFrame, ABlowWithNoTargetLeftIsAimedAgain) {
    BattleState state = armedBattle(0);
    state.at(kGuard).status.set(StatusId::DEAD);
    battle::AttackHooks hooks;
    const auto swings = animations(fight(state, hooks, kTerra, BattlerSet::of(kGuard)));
    ASSERT_EQ(swings.size(), 1u);
    EXPECT_EQ(swings[0].params.targets, BattlerSet::of(kSoldier));
}

// --- the critical hit --------------------------------------------------------------------------

TEST(AttackFrame, OneBlowInThirtyTwoIsCritical) {
    BattleState state = armedBattle(0);
    RecordingHooks hooks;
    const auto exactly = [](int n) { return [n](std::uint8_t r) { return r == n; }; };
    rngWhere(state, {any, exactly(7)});
    const auto critical = animations(fight(state, hooks, kTerra, BattlerSet::of(kGuard)));
    ASSERT_EQ(critical.size(), 1u);
    EXPECT_TRUE(critical[0].params.criticalFlash);
    EXPECT_EQ(hooks.multipliers.back(), 2);

    rngWhere(state, {any, exactly(8)});
    const auto plain = animations(fight(state, hooks, kTerra, BattlerSet::of(kGuard)));
    EXPECT_FALSE(plain[0].params.criticalFlash);
    EXPECT_EQ(hooks.multipliers.back(), 0);
}

TEST(AttackFrame, ABlowAtTheAttackersOwnSideIsNeverCritical) {
    BattleState state = armedBattle(0);
    battle::AttackHooks hooks;
    const std::uint8_t before = state.rng.position();
    fight(state, hooks, kTerra, BattlerSet::of(kLocke));
    EXPECT_EQ(state.rng.position(), static_cast<std::uint8_t>(before + 1));  // the pick, no roll
}

TEST(AttackFrame, AnImpWithTheImpCriticalAlwaysLandsOne) {
    BattleState state = armedBattle();
    state.at(kGuard).impCritical = true;
    state.at(kGuard).status.set(StatusId::IMP);
    battle::AttackHooks hooks;
    const auto swings = animations(fight(state, hooks, kGuard, BattlerSet::of(kTerra)));
    ASSERT_EQ(swings.size(), 1u);
    EXPECT_TRUE(swings[0].params.criticalFlash);
}

TEST(AttackFrame, AnImpMonsterWithoutItHitsWithPowerOne) {
    BattleState state = armedBattle();
    state.at(kGuard).status.set(StatusId::IMP);
    AttackInFlight attack;
    battle::loadWeaponAttack(state, attack, kGuard, false);
    EXPECT_EQ(attack.record.power, 1);
    state.at(kGuard).impCritical = true;
    battle::loadWeaponAttack(state, attack, kGuard, false);
    EXPECT_EQ(attack.record.power, 30);
}

// --- MP ------------------------------------------------------------------------------------------

TEST(AttackFrame, AMutedCastersSpellFizzles) {
    BattleState state = armedBattle();
    state.at(kTerra).mp = 50;
    state.at(kTerra).status.set(StatusId::SILENCE);
    battle::AttackHooks hooks;
    const auto script = cast(state, hooks, kTerra, AttackId::FIRE, BattlerSet::of(kGuard), 4);
    ASSERT_EQ(script.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<battle::PoseReset>(script[0]));
    EXPECT_EQ(std::get<battle::PoseReset>(script[0]).battler, kTerra);
    EXPECT_EQ(state.at(kTerra).mp, 50);
}

TEST(AttackFrame, AnImpFizzlesUnlessItCastsImp) {
    BattleState state = armedBattle();
    state.at(kTerra).mp = 50;
    state.at(kTerra).status.set(StatusId::IMP);
    battle::AttackHooks hooks;
    EXPECT_EQ(countOf<battle::PoseReset>(cast(state, hooks, kTerra, AttackId::FIRE, BattlerSet::of(kGuard), 4)), 1);
    const auto imp = cast(state, hooks, kTerra, AttackId::IMP, BattlerSet::of(kGuard), 4);
    EXPECT_EQ(countOf<battle::PoseReset>(imp), 0);
    EXPECT_EQ(animations(imp).size(), 1u);
}

TEST(AttackFrame, NotEnoughMpShowsTheMessageAndNothingElse) {
    BattleState state = armedBattle();
    state.at(kGuard).mp = 2;
    RecordingHooks hooks;
    const auto script = cast(state, hooks, kGuard, AttackId::FIRE, BattlerSet::of(kTerra), 4);
    const auto shown = animations(script);
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_EQ(shown[0].what, (std::variant<BattleCommandId, battle::SpecialAnimation>{battle::kNothingShown}));
    EXPECT_TRUE(shown[0].params.targets.empty());
    EXPECT_EQ(countOf<battle::BattleMessage>(script), 1);
    EXPECT_TRUE(hooks.damaged.empty());
    EXPECT_EQ(state.at(kGuard).mp, 2);
}

TEST(AttackFrame, PaidMpRefreshesTheCastersSpells) {
    BattleState state = armedBattle();
    state.at(kTerra).mp = 10;
    battle::AttackHooks hooks;
    cast(state, hooks, kTerra, AttackId::FIRE, BattlerSet::of(kGuard), 4);
    EXPECT_EQ(state.at(kTerra).mp, 6);
    EXPECT_TRUE(state.at(kTerra).pendingUpdates.enabledMagic);
}

// --- Runic, Air Anchor, the weapon's spell -------------------------------------------------------

TEST(AttackFrame, RunicDrawsInASpellAndShowsItsOwnAnimationFirst) {
    ASSERT_TRUE(getAttackProperties(AttackId::FIRE).flags1.has(AttackFlag1::ENABLE_RUNIC));
    BattleState state = armedBattle();
    state.at(kLocke).triggers.runic = true;
    state.at(kGuard).mp = 50;
    RecordingHooks hooks;
    const auto script = cast(state, hooks, kGuard, AttackId::FIRE, BattlerSet::of(kTerra));
    const auto shown = animations(script);
    ASSERT_EQ(shown.size(), 2u);
    EXPECT_EQ(shown[0].what,
              (std::variant<BattleCommandId, battle::SpecialAnimation>{battle::SpecialAnimation::RunicAbsorbsSpell}));
    EXPECT_EQ(shown[1].params.targets, BattlerSet::of(kLocke));
    EXPECT_FALSE(state.at(kLocke).triggers.runic);
}

TEST(AttackFrame, ASleepingRunicUserLosesRunicWithoutDrawingTheSpellIn) {
    BattleState state = armedBattle();
    state.at(kLocke).triggers.runic = true;
    state.at(kLocke).status.set(StatusId::SLEEP);
    battle::AttackHooks hooks;
    const auto shown = animations(cast(state, hooks, kGuard, AttackId::FIRE, BattlerSet::of(kTerra)));
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_EQ(shown[0].params.targets, BattlerSet::of(kTerra));
    EXPECT_FALSE(state.at(kLocke).triggers.runic);
}

TEST(AttackFrame, RunicRestoresTheCostSharedBetweenTheAbsorbers) {
    BattleState state = armedBattle();
    state.at(kTerra).triggers.runic = true;
    state.at(kLocke).triggers.runic = true;
    struct Power : battle::AttackHooks {
        int power = -1;
        bool restoresMp = false;
        void baseDamage(BattleState&, AttackInFlight& attack, BattlerSlot) override {
            power = attack.record.power;
            restoresMp = attack.record.flags1.has(AttackFlag1::AFFECT_MP) &&
                         attack.record.flags2.has(AttackFlag2::RESTORE_HP_MP);
        }
    } hooks;
    cast(state, hooks, kGuard, AttackId::FIRE, BattlerSet::of(kTerra), 0,
         [](AttackInFlight& attack) { attack.record.mpCost = 9; });
    EXPECT_EQ(hooks.power, 4);
    EXPECT_TRUE(hooks.restoresMp);
}

TEST(AttackFrame, AnAirAnchoredAttackerIsSetToDie) {
    BattleState state = armedBattle(0);
    state.at(kTerra).airAnchored = true;
    battle::AttackHooks hooks;
    fight(state, hooks, kTerra, BattlerSet::of(kGuard));
    EXPECT_FALSE(state.at(kTerra).airAnchored);
    EXPECT_TRUE(state.at(kTerra).statusesToSet.has(StatusId::DEAD));
}

TEST(AttackFrame, AWeaponCastsItsSpellOneTimeInFour) {
    BattleState state = armedBattle(0);
    state.at(kTerra).weaponHand.spellCast = ItemSpellCast::of(AttackId::FIRE, SpellCastMode::RANDOM_ON_ATTACK);
    battle::AttackHooks hooks;
    rngWhere(state, {any, atLeast(8), below(0x40)});
    const auto shown = animations(fight(state, hooks, kTerra, BattlerSet::of(kGuard)));
    ASSERT_EQ(shown.size(), 2u);
    EXPECT_EQ(shown[1].what, (std::variant<BattleCommandId, battle::SpecialAnimation>{BattleCommandId::MAGIC}));
    EXPECT_EQ(shown[1].attack, AttackId::FIRE);
    EXPECT_EQ(shown[1].params.targets, BattlerSet::of(kGuard));

    rngWhere(state, {any, atLeast(8), atLeast(0x40)});
    EXPECT_EQ(animations(fight(state, hooks, kTerra, BattlerSet::of(kGuard))).size(), 1u);
}

// --- names and monsters' special attacks ---------------------------------------------------------

TEST(AttackFrame, TheNameIsShownOncePerCommand) {
    BattleState state = armedBattle();
    battle::AttackHooks hooks;
    AttackInFlight attack = battle::startAttack(CommandAction{.command = BattleCommandId::FIGHT,
                                                              .targets = BattlerSet::of(kGuard)});
    attack.scratch.nameKind = battle::AttackNameKind::Command;
    BattleScriptBuilder script;
    battle::fightAttack(state, hooks, script, attack, kTerra);
    const auto commands = script.take();
    EXPECT_EQ(countOf<battle::CommandNameShown>(commands), 1);
    EXPECT_TRUE(std::holds_alternative<battle::CommandNameShown>(commands.front()));
}

TEST(AttackFrame, AMonstersSpecialAttackShowsItsNameAndItsBand) {
    BattleState state = armedBattle();
    battle::Battler& guard = state.at(kGuard);
    guard.monster = MonsterId{7};
    guard.specialAttack = MonsterSpecialAttack::damageBoost(2);
    guard.specialAttackAnimation = MonsterAttackAnimation::WING;
    RecordingHooks hooks;
    AttackInFlight attack = battle::startAttack(CommandAction{.command = BattleCommandId::FIGHT,
                                                              .attack = AttackId::SPECIAL,
                                                              .targets = BattlerSet::of(kTerra)});
    BattleScriptBuilder script;
    battle::fightAttack(state, hooks, script, attack, kGuard);
    const auto commands = script.take();
    ASSERT_TRUE(std::holds_alternative<battle::MonsterSpecialAttackNameShown>(commands.front()));
    EXPECT_EQ(std::get<battle::MonsterSpecialAttackNameShown>(commands.front()).monster, MonsterId{7});
    const auto shown = animations(commands);
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_TRUE(shown[0].params.monsterAttack);
    EXPECT_EQ(shown[0].secondary, static_cast<std::uint8_t>(MonsterAttackAnimation::WING));
    // Band step 2, plus the one the band's compare carries in.
    EXPECT_EQ(hooks.multipliers.back(), 3);
}

TEST(AttackFrame, JokerDoomIsAimedExactlyAndCannotBeDodged) {
    BattleState state = armedBattle();
    state.aiUntargetable.add(kGuard);
    battle::AttackHooks hooks;
    const auto script = cast(state, hooks, kTerra, AttackId::FIRE, BattlerSet::of(kGuard), 0,
                             [](AttackInFlight& attack) {
                                 attack.scratch.nameKind = battle::AttackNameKind::JokerDoom;
                                 attack.targeting = Targeting::of(TargetFlags::INIT_ALL);
                             });
    ASSERT_TRUE(std::holds_alternative<battle::AttackNameShown>(script.front()));
    EXPECT_EQ(std::get<battle::AttackNameShown>(script.front()).attack, battle::kJokerDoomName);
    EXPECT_EQ(animations(script)[0].params.targetsHit, BattlerSet::of(kGuard));
}

TEST(AttackFrame, StragoLearnsALoreHeSeesAndDoesNotKnow) {
    ASSERT_TRUE(getAttackProperties(AttackId::AQUA_RAKE).flags1.has(AttackFlag1::LEARNABLE_LORE));
    const auto lore = static_cast<std::size_t>(static_cast<int>(AttackId::AQUA_RAKE) -
                                               static_cast<int>(AttackId::CONDEMNED));
    BattleState state = armedBattle();
    state.at(kLocke).actor = CharacterPropId::STRAGO;
    battle::AttackHooks hooks;
    cast(state, hooks, kGuard, AttackId::AQUA_RAKE, BattlerSet::of(kTerra));
    EXPECT_TRUE(state.loresLearned.test(lore));

    BattleState knows = armedBattle();
    knows.at(kLocke).actor = CharacterPropId::STRAGO;
    knows.party.loresKnown.set(lore);
    cast(knows, hooks, kGuard, AttackId::AQUA_RAKE, BattlerSet::of(kTerra));
    EXPECT_FALSE(knows.loresLearned.test(lore));

    BattleState asleep = armedBattle();
    asleep.at(kLocke).actor = CharacterPropId::STRAGO;
    asleep.at(kLocke).status.set(StatusId::SLEEP);
    cast(asleep, hooks, kGuard, AttackId::AQUA_RAKE, BattlerSet::of(kTerra));
    EXPECT_FALSE(asleep.loresLearned.test(lore));
}

TEST(AttackFrame, AMonstersAttackMayShowAMessageNamedAfterIt) {
    BattleState state = armedBattle();
    battle::AttackHooks hooks;
    const auto script = cast(state, hooks, kGuard, AttackId::FIRE, BattlerSet::of(kTerra), 0,
                             [](AttackInFlight& attack) { attack.record.misc.set(AttackMiscFlag::SHOW_ATTACK_MESSAGE); });
    ASSERT_TRUE(std::holds_alternative<battle::BattleMessage>(script.back()));
    EXPECT_EQ(std::get<battle::BattleMessage>(script.back()).message, static_cast<std::uint8_t>(AttackId::FIRE));
}

// --- damage steps --------------------------------------------------------------------------------

TEST(AttackFrame, DamageIsHalvedAcrossTwoTargetsAndFromTheBackRow) {
    BattleState state = armedBattle(0);
    RecordingHooks hooks;
    hooks.baseDamageValue = 100;
    cast(state, hooks, kTerra, AttackId::FIRE, BattlerSet::of(kGuard) | BattlerSet::of(kSoldier), 0,
         [](AttackInFlight& attack) { attack.targeting = Targeting::of(TargetFlags::INIT_ALL); });
    EXPECT_EQ(hooks.damageAtScale.back(), 50);

    state.at(kTerra).backRow = true;
    rngWhere(state, {any, atLeast(8)});
    fight(state, hooks, kTerra, BattlerSet::of(kGuard));
    EXPECT_EQ(hooks.damageAtScale.back(), 50);
    state.at(kTerra).weaponHand.weaponFlags.set(WeaponFlags::BACK_ROW);
    rngWhere(state, {any, atLeast(8)});
    fight(state, hooks, kTerra, BattlerSet::of(kGuard));
    EXPECT_EQ(hooks.damageAtScale.back(), 100);
}

TEST(AttackFrame, MorphAndBerserkRaiseTheMultiplier) {
    BattleState state = armedBattle(0);
    state.at(kTerra).status.set(StatusId::MORPH);
    state.at(kTerra).status.set(StatusId::BERSERK);
    RecordingHooks hooks;
    rngWhere(state, {any, atLeast(8)});
    fight(state, hooks, kTerra, BattlerSet::of(kGuard));
    EXPECT_EQ(hooks.multipliers.back(), 3);
}

TEST(AttackFrame, EachTargetHitInTheBackAddsAStepForTheRest) {
    BattleState state = armedBattle(0);
    state.battleType = battle::BattleType::Side;
    state.facingRight = BattlerSet::of(kTerra) | BattlerSet::of(kGuard) | BattlerSet::of(kSoldier);
    RecordingHooks hooks;
    AttackInFlight attack = battle::startAttack(CommandAction{
        .command = BattleCommandId::FIGHT, .targets = BattlerSet::of(kGuard) | BattlerSet::of(kSoldier)});
    BattleScriptBuilder script;
    battle::initTargeting(attack, BattleCommandId::FIGHT, AttackId{});
    attack.targeting = Targeting::of(TargetFlags::INIT_ALL);
    attack.record.traits.set(AttackTrait::PHYSICAL);
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    battle::resolveAttack(state, hooks, script, attack, kTerra);
    // Monster 1 is taken first, then monster 0: one step, then two.
    EXPECT_EQ(hooks.damaged, (std::vector<BattlerSlot>{kSoldier, kGuard}));
    EXPECT_EQ(hooks.multipliersAtTargetDamage, (std::vector<int>{1, 2}));
}

TEST(HitInTheBack, OnlyInASideOrPincerAttackAndOnlyByBlows) {
    BattleState state = makeAimableBattle();
    state.facingRight = BattlerSet::of(kTerra) | BattlerSet::of(kGuard);
    AttackInFlight blowAttack;
    blowAttack.record.traits.set(AttackTrait::PHYSICAL);
    EXPECT_TRUE(battle::hitInTheBack(state, blowAttack, kTerra).empty());
    state.battleType = battle::BattleType::Side;
    const BattlerSet facingSame = battle::hitInTheBack(state, blowAttack, kTerra);
    EXPECT_TRUE(facingSame.has(kGuard));
    EXPECT_FALSE(facingSame.has(kSoldier));
    AttackInFlight spellAttack;
    EXPECT_TRUE(battle::hitInTheBack(state, spellAttack, kTerra).empty());
    state.battleType = battle::BattleType::Pincer;
    EXPECT_EQ(battle::hitInTheBack(state, blowAttack, kGuard), BattlerSet::of(kTerra));
}

// --- what a hit target records -------------------------------------------------------------------

TEST(RecordLastHit, ATargetRecordsTheBlowAndIsMarkedToAnswerIt) {
    BattleState state = makeAimableBattle();
    AttackInFlight attack;
    attack.queuedCommand = BattleCommandId::MAGIC;
    attack.scratch.lastSpell = AttackId::FIRE;
    attack.record.element = ElementSet{0x01};
    battle::recordLastHit(state, attack, kTerra, kGuard);
    const battle::Battler& guard = state.at(kGuard);
    EXPECT_EQ(guard.lastAttacker, kTerra);
    EXPECT_TRUE(guard.retaliationPending);
    ASSERT_TRUE(guard.lastHit.command && guard.lastHit.spell && guard.lastHit.element);
    EXPECT_EQ(guard.lastHit.command->with, BattleCommandId::MAGIC);
    EXPECT_EQ(guard.lastHit.spell->with, AttackId::FIRE);
    EXPECT_EQ(guard.lastHit.spell->by, kTerra);
    EXPECT_EQ(guard.lastHit.element->with.bits, 0x01);
    EXPECT_FALSE(guard.lastHit.item);
}

TEST(RecordLastHit, ATargetAlreadyWaitingToAnswerTakesTheNewAttackerOnHeads) {
    BattleState state = makeAimableBattle();
    AttackInFlight attack;
    battle::recordLastHit(state, attack, kTerra, kGuard);
    rngWhere(state, {tails});
    battle::recordLastHit(state, attack, kLocke, kGuard);
    EXPECT_EQ(state.at(kGuard).lastAttacker, kTerra);
    rngWhere(state, {heads});
    battle::recordLastHit(state, attack, kLocke, kGuard);
    EXPECT_EQ(state.at(kGuard).lastAttacker, kLocke);
}

TEST(AttackFrame, ZingersPreyIsNotHurt) {
    BattleState state = armedBattle(0);
    state.heldByZinger = kGuard;
    RecordingHooks hooks;
    fight(state, hooks, kTerra, BattlerSet::of(kGuard));
    EXPECT_TRUE(hooks.damaged.empty());
}

TEST(AttackFrame, ATargetTheEffectMissesTakesNoDamage) {
    BattleState state = armedBattle(0);
    RecordingHooks hooks;
    hooks.missesEffect = [](BattlerSlot) { return true; };
    const auto shown = animations(fight(state, hooks, kTerra, BattlerSet::of(kGuard)));
    EXPECT_EQ(hooks.effects, (std::vector<BattlerSlot>{kGuard}));
    EXPECT_TRUE(hooks.damaged.empty());
    EXPECT_TRUE(shown[0].params.targetsHit.empty());
}

TEST(AttackFrame, TheCommandsEarlierTargetsGatherEveryAttacksTargets) {
    AttackInFlight attack;
    attack.targets = BattlerSet::of(kGuard);
    battle::rememberTargets(attack);
    attack.targets = BattlerSet::of(kSoldier);
    battle::rememberTargets(attack);
    EXPECT_EQ(attack.scratch.earlierTargets, BattlerSet::of(kGuard) | BattlerSet::of(kSoldier));
    EXPECT_EQ(attack.scratch.currentTargets, BattlerSet::of(kSoldier));
    attack.scratch.damageModification = false;
    attack.targets = BattlerSet::of(kTerra);
    battle::rememberTargets(attack);
    EXPECT_EQ(attack.targets, BattlerSet::of(kSoldier));
}

TEST(AttackFrame, AnAttackShownPastTheCommandsIsASpecialAnimation) {
    BattleState state = armedBattle();
    battle::AttackHooks hooks;
    AttackInFlight attack = battle::startAttack(CommandAction{.targets = BattlerSet::of(kGuard)});
    battle::initTargeting(attack, BattleCommandId::FIGHT, AttackId{});
    attack.command = static_cast<BattleCommandId>(battle::SpecialAnimation::UmaroTackle);
    BattleScriptBuilder script;
    battle::resolveAttack(state, hooks, script, attack, kTerra);
    const auto shown = animations(script.take());
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_EQ(shown[0].what,
              (std::variant<BattleCommandId, battle::SpecialAnimation>{battle::SpecialAnimation::UmaroTackle}));
}

// --- the executor --------------------------------------------------------------------------------

TEST(CommandExecutor, FightRunsAndReportsItsBlow) {
    BattleState state = armedBattle();
    battle::AttackHooks hooks;
    battle::CommandExecutor executor(hooks);
    const auto script = executor.execute(
        state, kTerra, battle::Action{CommandAction{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(kGuard)}});
    EXPECT_EQ(animations(script).size(), 2u);
    EXPECT_TRUE(state.scratch.physical);
    EXPECT_EQ(state.scratch.finalTargets, BattlerSet::of(kGuard));
}

TEST(CommandExecutor, ACommandWithNoBodyShowsNothing) {
    BattleState state = armedBattle();
    battle::AttackHooks hooks;
    battle::CommandExecutor executor(hooks);
    EXPECT_TRUE(executor.execute(state, kTerra, battle::Action{CommandAction{.command = BattleCommandId::DEF}}).empty());
    EXPECT_TRUE(executor.execute(state, std::nullopt, battle::Action{battle::NoAction{}}).empty());
}

TEST(CommandExecutor, ACommandChosenWithoutTargetsIsAimed) {
    BattleState state = armedBattle();
    battle::AttackHooks hooks;
    battle::CommandExecutor executor(hooks);
    CommandAction command{.command = BattleCommandId::FIGHT};
    executor.aim(state, kTerra, command);
    EXPECT_EQ(command.targets.count(), 1);
    EXPECT_TRUE(command.targets.charactersOnly().empty());

    CommandAction given{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(kSoldier)};
    executor.aim(state, kTerra, given);
    EXPECT_EQ(given.targets, BattlerSet::of(kSoldier));
}

}  // namespace
}  // namespace ostinato::test
