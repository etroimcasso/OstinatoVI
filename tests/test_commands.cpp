// The player's battle commands: each body's attack, name and animation, and what it changes.
// Behaviors and their sources: docs/contracts/battle_commands.md § The commands.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <variant>
#include <vector>

#include "battle/actions.h"
#include "battle/attack.h"
#include "battle/command_executor.h"
#include "battle/commands.h"
#include "battle_test_support.h"
#include "data/attack_properties.h"
#include "data/battle_tables.h"
#include "data/monster_properties.h"
#include "data/monster_special_anim.h"
#include "game/battle_switches.h"

namespace ostinato::test {
namespace {

using battle::AttackAnimation;
using battle::AttackInFlight;
using battle::Battler;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleScriptBuilder;
using battle::BattleScriptCommand;
using battle::BattleState;
using battle::CommandAction;
using battle::Hand;
using battle::SpecialAnimation;
using battle::SpecialEffect;

using Shown = std::variant<BattleCommandId, SpecialAnimation>;

const BattlerSlot kTerra = BattlerSlot::character(0);
const BattlerSlot kLocke = BattlerSlot::character(1);
const BattlerSlot kGuard = BattlerSlot::monster(0);
const BattlerSlot kSoldier = BattlerSlot::monster(1);

// Hooks that keep a copy of every attack as it is worked out, and let a test act as the special
// effect on a target.
struct AttackLog : battle::AttackHooks {
    std::vector<AttackInFlight> made;
    std::function<void(AttackInFlight&, BattlerSlot)> onTargetEffect;

    void baseDamage(BattleState&, AttackInFlight& attack, BattlerSlot) override { made.push_back(attack); }
    bool targetEffect(BattleState&, AttackInFlight& attack, BattlerSlot, BattlerSlot target) override {
        if (onTargetEffect) {
            onTargetEffect(attack, target);
        }
        return false;
    }
};

// Terra and Locke against two monsters; everyone on the field, Terra armed in both hands.
BattleState commandBattle() {
    BattleState state = makeAimableBattle();
    Battler& terra = state.at(kTerra);
    terra.actor = CharacterPropId::TERRA;
    terra.level = 10;
    terra.vigor = 60;
    terra.magicPower = 40;
    terra.mp = 200;
    terra.weaponHand = Hand{.item = ItemId::DIRK, .battlePower = 50, .hitRate = 255};
    terra.shieldHand = Hand{.item = ItemId::FIRE_ROD, .battlePower = 40, .hitRate = 255};
    Battler& locke = state.at(kLocke);
    locke.actor = CharacterPropId::LOCKE;
    locke.weaponHand = Hand{.item = ItemId::DIRK, .battlePower = 40, .hitRate = 255};
    locke.shieldHand = Hand{.battlePower = 20, .hitRate = 255};
    state.at(kGuard).weaponHand = Hand{.battlePower = 30, .hitRate = 255};
    state.at(kGuard).monster = MonsterId{};
    return state;
}

std::vector<BattleScriptCommand> run(BattleState& state, battle::AttackHooks& hooks, BattlerSlot actor,
                                     CommandAction command) {
    AttackInFlight attack = battle::startAttack(command);
    BattleScriptBuilder script;
    battle::runCommand(state, hooks, script, attack, actor);
    return script.take();
}

AttackId asAttack(ItemId item) {
    return static_cast<AttackId>(static_cast<std::uint8_t>(item));
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
std::vector<T> allOf(const std::vector<BattleScriptCommand>& script) {
    std::vector<T> found;
    for (const BattleScriptCommand& command : script) {
        if (const auto* value = std::get_if<T>(&command)) {
            found.push_back(*value);
        }
    }
    return found;
}

std::vector<std::uint8_t> messages(const std::vector<BattleScriptCommand>& script) {
    std::vector<std::uint8_t> found;
    for (const battle::BattleMessage& message : allOf<battle::BattleMessage>(script)) {
        found.push_back(message.message);
    }
    return found;
}

bool lowNibbleClear(std::uint8_t r) { return (r & 0x0F) == 0; }

// The draw a row of Umaro's weights turns into: below(255).
std::function<bool(std::uint8_t)> umaroRoll(int lowest, int highest) {
    return [lowest, highest](std::uint8_t r) {
        const int roll = (r * 255) >> 8;
        return roll >= lowest && roll <= highest;
    };
}

// --- Fight: desperation attacks --------------------------------------------------------------

BattleState desperateBattle() {
    BattleState state = commandBattle();
    state.at(kTerra).status.set(StatusId::NEAR_FATAL);
    state.turnCounter = battle::kTimeUpdatesBeforeDesperation;
    return state;
}

const CommandAction kFightTheGuard{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(kGuard)};

TEST(Commands, ADesperationAttackReplacesTheFight) {
    BattleState state = desperateBattle();
    battle::AttackHooks hooks;
    rngWhere(state, {lowNibbleClear});
    const auto script = run(state, hooks, kTerra, kFightTheGuard);
    const auto shown = animations(script);
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::MAGIC});
    EXPECT_EQ(shown[0].attack, AttackId::RIOT_BLADE);
    EXPECT_TRUE(shown[0].params.castingSwirl);
    ASSERT_EQ(allOf<battle::AttackNameShown>(script).size(), 1u);
    EXPECT_EQ(allOf<battle::AttackNameShown>(script)[0].attack, AttackId::RIOT_BLADE);
    EXPECT_TRUE(state.desperationUsed.has(kTerra));
}

TEST(Commands, ADesperationAttackNeedsEveryCondition) {
    const std::vector<std::function<void(BattleState&, CommandAction&)>> spoilers = {
        [](BattleState& s, CommandAction&) { s.turnCounter = battle::kTimeUpdatesBeforeDesperation - 1; },
        [](BattleState& s, CommandAction&) { s.at(kTerra).status.clear(StatusId::NEAR_FATAL); },
        [](BattleState& s, CommandAction&) { s.at(kTerra).status.set(StatusId::IMAGE); },
        [](BattleState& s, CommandAction&) { s.at(kTerra).status.set(StatusId::CONFUSE); },
        [](BattleState& s, CommandAction&) { s.at(kTerra).status.set(StatusId::ZOMBIE); },
        [](BattleState& s, CommandAction&) { s.at(kTerra).status.set(StatusId::INVISIBLE); },
        [](BattleState&, CommandAction& c) { c.targets = BattlerSet::of(kLocke); },
        [](BattleState& s, CommandAction&) { s.desperationUsed.add(kTerra); },
    };
    for (std::size_t i = 0; i < spoilers.size(); ++i) {
        BattleState state = desperateBattle();
        CommandAction fight = kFightTheGuard;
        spoilers[i](state, fight);
        rngWhere(state, {lowNibbleClear});
        battle::AttackHooks hooks;
        const auto shown = animations(run(state, hooks, kTerra, fight));
        ASSERT_FALSE(shown.empty()) << i;
        EXPECT_EQ(shown[0].what, Shown{BattleCommandId::FIGHT}) << i;
    }
}

TEST(Commands, ADesperationAttackComesOnADrawWithItsLowFourBitsClear) {
    BattleState state = desperateBattle();
    battle::AttackHooks hooks;
    rngWhere(state, {[](std::uint8_t r) { return (r & 0x0F) == 0x08; }});
    EXPECT_EQ(animations(run(state, hooks, kTerra, kFightTheGuard))[0].what, Shown{BattleCommandId::FIGHT});
    EXPECT_FALSE(state.desperationUsed.has(kTerra));

    BattleState other = desperateBattle();
    rngWhere(other, {[](std::uint8_t r) { return r != 0 && (r & 0x0F) == 0; }});
    EXPECT_EQ(animations(run(other, hooks, kTerra, kFightTheGuard))[0].what, Shown{BattleCommandId::MAGIC});
}

TEST(Commands, GauAndUmaroSpendTheirChanceButHaveNoDesperationAttack) {
    for (const CharacterPropId actor : {CharacterPropId::GAU, CharacterPropId::UMARO}) {
        BattleState state = desperateBattle();
        state.at(kTerra).actor = actor;
        rngWhere(state, {lowNibbleClear});
        battle::AttackHooks hooks;
        const auto shown = animations(run(state, hooks, kTerra, kFightTheGuard));
        ASSERT_FALSE(shown.empty());
        EXPECT_NE(shown[0].what, Shown{BattleCommandId::MAGIC});
        EXPECT_TRUE(state.desperationUsed.has(kTerra));
    }
}

TEST(Commands, GogoDesperationAttackIsXMeteo) {
    BattleState state = desperateBattle();
    state.at(kTerra).actor = CharacterPropId::GOGO;
    rngWhere(state, {lowNibbleClear});
    battle::AttackHooks hooks;
    EXPECT_EQ(animations(run(state, hooks, kTerra, kFightTheGuard))[0].attack, AttackId::X_METEO);
}

// The original works the attack out from the actor: $f0 plus Terra..Mog, one before for Gogo; Gau
// and Umaro are turned away before (battle_main.asm:3420-3428). The shipped table holds the same.
TEST(Commands, EachDesperationAttackIsTheActorsOwn) {
    for (int actor = 0; actor <= static_cast<int>(CharacterId::MOG); ++actor) {
        EXPECT_EQ(desperationAttack(static_cast<CharacterId>(actor)), static_cast<AttackId>(0xF0 + actor));
    }
    EXPECT_EQ(desperationAttack(CharacterId::GOGO), static_cast<AttackId>(0xF0 + 11));
    EXPECT_EQ(desperationAttack(CharacterId::GAU), AttackId::NONE);
    EXPECT_EQ(desperationAttack(CharacterId::UMARO), AttackId::NONE);
}

// --- Fight: Umaro ----------------------------------------------------------------------------

BattleState umaroBattle() {
    BattleState state = commandBattle();
    Battler& umaro = state.at(kTerra);
    umaro.actor = CharacterPropId::UMARO;
    umaro.level = 30;
    umaro.vigor = 90;
    umaro.weaponHand = Hand{.battlePower = 100, .hitRate = 255};
    umaro.shieldHand = Hand{.battlePower = 60, .hitRate = 255};
    return state;
}

std::set<Shown> umaroOutcomes(BattleState base) {
    std::set<Shown> seen;
    for (int seed = 0; seed < 256; ++seed) {
        BattleState state = base;
        state.rng = battle::BattleRng(static_cast<std::uint8_t>(seed));
        battle::AttackHooks hooks;
        const auto shown = animations(run(state, hooks, kTerra, kFightTheGuard));
        if (!shown.empty()) {
            seen.insert(shown[0].what);
        }
    }
    return seen;
}

TEST(Commands, UmaroWithoutRelicsFightsOrTackles) {
    EXPECT_EQ(umaroOutcomes(umaroBattle()),
              (std::set<Shown>{Shown{BattleCommandId::FIGHT}, Shown{SpecialAnimation::UmaroTackle}}));
}

TEST(Commands, TheBlizzardOrbAddsTheStormAndTheRageRingTheThrow) {
    BattleState state = umaroBattle();
    state.at(kTerra).relics = {ItemId::RAGE_RING, ItemId::BLIZZARD_ORB};
    EXPECT_EQ(umaroOutcomes(state),
              (std::set<Shown>{Shown{BattleCommandId::FIGHT}, Shown{SpecialAnimation::UmaroTackle},
                               Shown{BattleCommandId::MAGIC}, Shown{SpecialAnimation::UmaroThrow}}));
}

TEST(Commands, TheRageRingNeedsAnotherPartyMemberAlive) {
    BattleState state = umaroBattle();
    state.at(kTerra).relics = {ItemId::RAGE_RING, ItemId::EMPTY};
    state.alive.remove(kLocke);
    EXPECT_FALSE(umaroOutcomes(state).contains(Shown{SpecialAnimation::UmaroThrow}));
}

TEST(Commands, UmarosRelicsPickTheRowOfWeights) {
    // A roll of 100-150 is a Fight on the plain row (Fight below 159) and a tackle on the Rage
    // Ring's (Fight below 95, tackle below 159).
    BattleState plain = umaroBattle();
    rngWhere(plain, {umaroRoll(100, 150)});
    battle::AttackHooks hooks;
    EXPECT_EQ(animations(run(plain, hooks, kTerra, kFightTheGuard))[0].what, Shown{BattleCommandId::FIGHT});

    BattleState ringed = umaroBattle();
    ringed.at(kTerra).relics = {ItemId::RAGE_RING, ItemId::EMPTY};
    rngWhere(ringed, {umaroRoll(100, 150)});
    EXPECT_EQ(animations(run(ringed, hooks, kTerra, kFightTheGuard))[0].what, Shown{SpecialAnimation::UmaroTackle});
}

TEST(Commands, UmarosTackleHitsWithBothHandsUndodgeably) {
    BattleState state = umaroBattle();
    rngWhere(state, {umaroRoll(159, 254)});
    AttackLog log;
    run(state, log, kTerra, kFightTheGuard);
    ASSERT_EQ(log.made.size(), 1u);
    const AttackInFlight& tackle = log.made[0];
    EXPECT_EQ(tackle.command, static_cast<BattleCommandId>(SpecialAnimation::UmaroTackle));
    EXPECT_EQ(tackle.record.power, 160);
    EXPECT_EQ(tackle.attackerLevel, 30);
    EXPECT_EQ(tackle.attackerStat, 90);
    EXPECT_TRUE(tackle.record.traits.has(AttackTrait::IGNORE_DEFENSE));
    EXPECT_TRUE(tackle.record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_TRUE(tackle.canBeCritical);
}

// A Rage Ring and a roll past the tackle's reach: Umaro throws.
BattleState throwingUmaro() {
    BattleState state = umaroBattle();
    state.at(kTerra).relics = {ItemId::RAGE_RING, ItemId::EMPTY};
    rngWhere(state, {umaroRoll(159, 254), any});
    return state;
}

TEST(Commands, UmaroThrowsTheSleepingAllyAndWakesIt) {
    BattleState state = throwingUmaro();
    const BattlerSlot edgar = BattlerSlot::character(2);
    state.at(edgar).present = true;
    state.alive.add(edgar);
    state.at(kLocke).status.set(StatusId::SLEEP);
    AttackLog log;
    const auto shown = animations(run(state, log, kTerra, kFightTheGuard));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(shown[0].what, Shown{SpecialAnimation::UmaroThrow});
    EXPECT_EQ(log.made[0].record.power, 40 + 20 + 160);
    EXPECT_TRUE(log.made[0].rules.excludeAttacker);
    EXPECT_FALSE(log.made[0].automaticCritical);
    EXPECT_TRUE(state.at(kLocke).statusesToClear.has(StatusId::SLEEP));
}

TEST(Commands, ThrowingMogIsACriticalHitAndAddsOne) {
    BattleState state = throwingUmaro();
    state.at(kLocke).actor = CharacterPropId::MOG;
    AttackLog log;
    run(state, log, kTerra, kFightTheGuard);
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_TRUE(log.made[0].automaticCritical);
    EXPECT_EQ(log.made[0].record.power, 40 + 20 + 1 + 160);
}

TEST(Commands, AnAllyAfterMogAlsoAddsOne) {
    BattleState state = throwingUmaro();
    state.at(kLocke).actor = CharacterPropId::GAU;
    AttackLog log;
    run(state, log, kTerra, kFightTheGuard);
    EXPECT_FALSE(log.made[0].automaticCritical);
    EXPECT_EQ(log.made[0].record.power, 40 + 20 + 1 + 160);
}

TEST(Commands, TheThrownPowerStopsAt255) {
    BattleState state = throwingUmaro();
    state.at(kLocke).weaponHand.battlePower = 200;
    state.at(kLocke).shieldHand.battlePower = 100;
    AttackLog log;
    run(state, log, kTerra, kFightTheGuard);
    EXPECT_EQ(log.made[0].record.power, 255);
}

TEST(Commands, AHiddenAllyIsNotThrownAndWithNoOneLeftUmaroTackles) {
    BattleState state = throwingUmaro();
    state.at(kLocke).status.set(StatusId::HIDE);
    battle::AttackHooks hooks;
    EXPECT_EQ(animations(run(state, hooks, kTerra, kFightTheGuard))[0].what, Shown{SpecialAnimation::UmaroTackle});
}

// --- Item, Throw and Tools -------------------------------------------------------------------

CommandAction itemCommand(BattleCommandId command, ItemId item, BattlerSet targets) {
    return CommandAction{.command = command, .attack = asAttack(item), .targets = targets};
}

TEST(Commands, AConsumableIsUsedUpAndShownAsTheItem) {
    BattleState state = commandBattle();
    state.at(kTerra).itemInHand = ItemId::POTION;
    AttackLog log;
    const auto script = run(state, log, kTerra, itemCommand(BattleCommandId::ITEM, ItemId::POTION, BattlerSet::of(kLocke)));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_FALSE(log.made[0].scratch.damageModification);
    EXPECT_FALSE(log.made[0].vanishApplies);
    EXPECT_FALSE(log.made[0].emptyTargetsReaim);
    EXPECT_EQ(state.at(kTerra).itemInHand, std::nullopt);
    EXPECT_TRUE(state.holdingItem.has(kTerra));
    ASSERT_EQ(allOf<battle::ItemNameShown>(script).size(), 1u);
    EXPECT_EQ(allOf<battle::ItemNameShown>(script)[0].item, ItemId::POTION);
    const auto shown = animations(script);
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::ITEM});
    EXPECT_EQ(shown[0].item, ItemId::POTION);
}

TEST(Commands, AnItemThatRaisesNoOneReachesOnlyTheLiving) {
    BattleState state = commandBattle();
    state.at(kLocke).status.set(StatusId::DEAD);
    state.alive.remove(kLocke);
    AttackLog log;
    run(state, log, kTerra, itemCommand(BattleCommandId::ITEM, ItemId::POTION, BattlerSet::of(kLocke)));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_TRUE(log.made[0].targets.empty());
}

TEST(Commands, AFenixDownMayBeAimedAtTheFallen) {
    BattleState state = commandBattle();
    state.at(kLocke).status.set(StatusId::DEAD);
    state.alive.remove(kLocke);
    AttackLog log;
    run(state, log, kTerra, itemCommand(BattleCommandId::ITEM, ItemId::FENIX_DOWN, BattlerSet::of(kLocke)));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].targets, BattlerSet::of(kLocke));
    EXPECT_TRUE(log.made[0].emptyTargetsReaim);
}

TEST(Commands, ARodCastsItsSpellAsMagicForFree) {
    BattleState state = commandBattle();
    state.at(kTerra).itemInHand = ItemId::FIRE_ROD;
    AttackLog log;
    const auto script = run(state, log, kTerra, itemCommand(BattleCommandId::ITEM, ItemId::FIRE_ROD, BattlerSet::of(kGuard)));
    ASSERT_EQ(log.made.size(), 1u);
    const AttackInFlight& spell = log.made[0];
    EXPECT_EQ(spell.attack, AttackId::FIRE_2);
    EXPECT_EQ(spell.command, BattleCommandId::MAGIC);
    EXPECT_EQ(spell.record.mpCost, 0);
    EXPECT_TRUE(spell.record.flags1.has(AttackFlag1::IGNORE_REFLECT));
    EXPECT_TRUE(spell.record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_FALSE(spell.rules.mayTargetTheFallen);
    EXPECT_EQ(spell.attackerStat, 40);
    EXPECT_EQ(spell.extraMultiplier, 0);
    EXPECT_EQ(allOf<battle::ItemNameShown>(script)[0].item, ItemId::FIRE_ROD);
    const auto shown = animations(script);
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::MAGIC});
    EXPECT_EQ(shown[0].attack, AttackId::FIRE_2);
    EXPECT_FALSE(shown[0].item.has_value());
    EXPECT_EQ(state.at(kTerra).itemInHand, std::nullopt);  // the rod breaks
}

TEST(Commands, AnItemWhoseSpellIsNotCastOnUseIsKept) {
    BattleState state = commandBattle();
    state.at(kTerra).itemInHand = ItemId::FLAME_SABRE;
    AttackLog log;
    run(state, log, kTerra, itemCommand(BattleCommandId::ITEM, ItemId::FLAME_SABRE, BattlerSet::of(kGuard)));
    EXPECT_EQ(log.made[0].attack, AttackId::FIRE);
    EXPECT_EQ(state.at(kTerra).itemInHand, ItemId::FLAME_SABRE);
    EXPECT_TRUE(state.holdingItem.has(kTerra));
}

TEST(Commands, AThrownWeaponIsDoubledThroughDefense) {
    BattleState state = commandBattle();
    state.at(kTerra).itemInHand = ItemId::DIRK;
    AttackLog log;
    const auto script = run(state, log, kTerra, itemCommand(BattleCommandId::THROW, ItemId::DIRK, BattlerSet::of(kGuard)));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].extraMultiplier, 2);
    EXPECT_FALSE(log.made[0].ignoreDefenseStopsMultiplier);
    EXPECT_TRUE(log.made[0].record.traits.has(AttackTrait::PHYSICAL));
    EXPECT_TRUE(log.made[0].scratch.damageModification);
    EXPECT_TRUE(log.made[0].vanishApplies);
    EXPECT_EQ(state.at(kTerra).itemInHand, std::nullopt);
    const auto shown = animations(script);
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::THROW});
    EXPECT_EQ(shown[0].item, ItemId::DIRK);
}

TEST(Commands, AThrownEdgeCastsItsSpellUndoubled) {
    BattleState state = commandBattle();
    AttackLog log;
    const auto script =
        run(state, log, kTerra, itemCommand(BattleCommandId::THROW, ItemId::FIRE_SKEAN, BattlerSet::of(kGuard)));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].attack, AttackId::FIRE_SKEAN);
    EXPECT_EQ(log.made[0].extraMultiplier, 0);
    EXPECT_EQ(log.made[0].record.power, getAttackProperties(AttackId::FIRE_SKEAN).power);
    const auto shown = animations(script);
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::THROW});
    EXPECT_EQ(shown[0].item, ItemId::FIRE_SKEAN);
}

TEST(Commands, ToolsWorkTheTool) {
    BattleState state = commandBattle();
    AttackLog log;
    const auto script = run(state, log, kTerra, itemCommand(BattleCommandId::TOOLS, ItemId::DRILL, BattlerSet::of(kGuard)));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_FALSE(log.made[0].record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_EQ(log.made[0].attackerStat, 60);
    EXPECT_EQ(log.made[0].extraMultiplier, 0);
    const auto shown = animations(script);
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::TOOLS});
    EXPECT_EQ(shown[0].item, ItemId::DRILL);
    EXPECT_FALSE(shown[0].chainSawKill);
}

TEST(Commands, TheBioBlasterCastsItsSpellAsATool) {
    BattleState state = commandBattle();
    AttackLog log;
    const auto script =
        run(state, log, kTerra, itemCommand(BattleCommandId::TOOLS, ItemId::BIO_BLASTER, BattlerSet::of(kGuard)));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].attack, AttackId::BIO_BLAST_TOOL);
    EXPECT_EQ(log.made[0].command, BattleCommandId::TOOLS);
    EXPECT_EQ(animations(script)[0].item, ItemId::BIO_BLASTER);
}

TEST(Commands, TheChainSawsKillShowsItsOwnAnimation) {
    BattleState state = commandBattle();
    rngWhere(state, {[](std::uint8_t r) { return (r & 0x03) == 0; }});
    battle::AttackHooks hooks;
    const auto shown =
        animations(run(state, hooks, kTerra, itemCommand(BattleCommandId::TOOLS, ItemId::CHAIN_SAW, BattlerSet::of(kGuard))));
    ASSERT_FALSE(shown.empty());
    EXPECT_TRUE(shown[0].chainSawKill);
}

// --- Magic and the commands that cast ---------------------------------------------------------

CommandAction cast(BattleCommandId command, AttackId attack) {
    return CommandAction{.command = command, .attack = attack, .targets = BattlerSet::of(kGuard)};
}

TEST(Commands, TerrasSpellStartsTheSceneOnce) {
    BattleState state = commandBattle();
    state.switches.set(game::kTerraMagicSceneSwitch);
    battle::AttackHooks hooks;
    run(state, hooks, kTerra, cast(BattleCommandId::MAGIC, AttackId::FIRE));
    ASSERT_EQ(state.immediateActions.size(), 1u);
    const auto* event = std::get_if<battle::BattleEventAction>(&state.immediateActions.front());
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->event, battle::kBattleEventTerraMagicScene);
    EXPECT_FALSE(state.switches.isSet(game::kTerraMagicSceneSwitch));
    run(state, hooks, kTerra, cast(BattleCommandId::X_MAGIC, AttackId::FIRE));
    EXPECT_EQ(state.immediateActions.size(), 1u);
}

TEST(Commands, OnlyTerraStartsTheScene) {
    BattleState state = commandBattle();
    state.switches.set(game::kTerraMagicSceneSwitch);
    battle::AttackHooks hooks;
    run(state, hooks, kLocke, cast(BattleCommandId::MAGIC, AttackId::FIRE));
    EXPECT_TRUE(state.immediateActions.empty());
    EXPECT_TRUE(state.switches.isSet(game::kTerraMagicSceneSwitch));
}

TEST(Commands, MagicIsNamedAndCastWithTheCastersMagicPower) {
    BattleState state = commandBattle();
    AttackLog log;
    const auto script = run(state, log, kTerra, cast(BattleCommandId::MAGIC, AttackId::FIRE));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].attackerStat, 40);
    EXPECT_EQ(log.made[0].attackerLevel, 10);
    EXPECT_EQ(log.made[0].record.mpCost, getAttackProperties(AttackId::FIRE).mpCost);
    EXPECT_EQ(allOf<battle::AttackNameShown>(script)[0].attack, AttackId::FIRE);
    EXPECT_FALSE(animations(script)[0].params.castingSwirl);
}

TEST(Commands, SlotsSpellsCostNothingAndSwirl) {
    BattleState state = commandBattle();
    AttackLog log;
    const auto script = run(state, log, kTerra, cast(BattleCommandId::SLOT, AttackId::SEVEN_FLUSH));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].record.mpCost, 0);
    EXPECT_EQ(allOf<battle::AttackNameShown>(script)[0].attack, AttackId::SEVEN_FLUSH);
    EXPECT_EQ(animations(script)[0].what, Shown{BattleCommandId::SLOT});
    EXPECT_TRUE(animations(script)[0].params.castingSwirl);
}

TEST(Commands, SlotsOutcomesAreNamedJokerDoomAnEsperOrMuguMugu) {
    BattleState state = commandBattle();
    battle::AttackHooks hooks;
    EXPECT_EQ(allOf<battle::AttackNameShown>(run(state, hooks, kTerra, cast(BattleCommandId::SLOT, AttackId::L5_DOOM)))[0]
                  .attack,
              battle::kJokerDoomName);
    EXPECT_EQ(allOf<battle::EsperNameShown>(run(state, hooks, kTerra, cast(BattleCommandId::SLOT, AttackId::BAHAMUT)))
                  .size(),
              1u);
    EXPECT_EQ(messages(run(state, hooks, kTerra, cast(BattleCommandId::SLOT, AttackId::LAGOMORPH))),
              (std::vector<std::uint8_t>{battle::kMessageMuguMugu}));
}

TEST(Commands, HealthAndShockCastTheirAttackUnderTheCommandsName) {
    BattleState state = commandBattle();
    AttackLog log;
    const auto health = run(state, log, kTerra, cast(BattleCommandId::HEALTH, AttackId{}));
    EXPECT_EQ(log.made[0].attack, AttackId::CURE_2);
    EXPECT_EQ(allOf<battle::CommandNameShown>(health)[0].command, BattleCommandId::HEALTH);
    const auto shock = run(state, log, kTerra, cast(BattleCommandId::SHOCK, AttackId{}));
    EXPECT_EQ(log.made[1].attack, AttackId::SHOCK);
    EXPECT_EQ(allOf<battle::CommandNameShown>(shock)[0].command, BattleCommandId::SHOCK);
}

TEST(Commands, SummonNamesTheEsperAndLoreTheAttack) {
    BattleState state = commandBattle();
    battle::AttackHooks hooks;
    EXPECT_EQ(allOf<battle::EsperNameShown>(run(state, hooks, kTerra, cast(BattleCommandId::SUMMON, AttackId::BAHAMUT)))
                  .size(),
              1u);
    EXPECT_EQ(allOf<battle::AttackNameShown>(run(state, hooks, kTerra, cast(BattleCommandId::LORE, AttackId::AERO)))[0]
                  .attack,
              AttackId::AERO);
}

// --- Dance -----------------------------------------------------------------------------------

CommandAction danceStep() {
    return CommandAction{.command = BattleCommandId::DANCE, .attack = AttackId::WIND_SLASH, .targets = BattlerSet::of(kGuard)};
}

TEST(Commands, ADanceThatMatchesTheBattlefieldIsDanced) {
    BattleState state = commandBattle();
    state.background = BattleBackgroundId::FIELD_WOB;
    state.at(kTerra).dance = DanceId::WIND_SONG;
    battle::AttackHooks hooks;
    const auto script = run(state, hooks, kTerra, danceStep());
    EXPECT_TRUE(state.at(kTerra).status.has(StatusId::DANCE));
    const auto shown = animations(script);
    ASSERT_FALSE(shown.empty());
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::DANCE});
    EXPECT_EQ(shown[0].newBackground, std::nullopt);
    ASSERT_EQ(allOf<battle::AttackNameShown>(script).size(), 1u);
    EXPECT_EQ(allOf<battle::AttackNameShown>(script)[0].attack, AttackId::WIND_SLASH);
}

TEST(Commands, AnotherDanceChangesTheBattlefieldOnHeads) {
    BattleState state = commandBattle();
    state.background = BattleBackgroundId::CAVES;
    state.at(kTerra).dance = DanceId::WIND_SONG;
    rngWhere(state, {heads});
    battle::AttackHooks hooks;
    const auto shown = animations(run(state, hooks, kTerra, danceStep()));
    EXPECT_EQ(state.background, BattleBackgroundId::FIELD_WOB);
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::DANCE});
    EXPECT_EQ(shown[0].newBackground, BattleBackgroundId::FIELD_WOB);
}

TEST(Commands, OnTailsTheDancerStumbles) {
    BattleState state = commandBattle();
    state.background = BattleBackgroundId::CAVES;
    state.at(kTerra).dance = DanceId::WIND_SONG;
    rngWhere(state, {tails});
    battle::AttackHooks hooks;
    const auto script = run(state, hooks, kTerra, danceStep());
    EXPECT_FALSE(state.at(kTerra).status.has(StatusId::DANCE));
    EXPECT_EQ(state.background, BattleBackgroundId::CAVES);
    EXPECT_EQ(messages(script), (std::vector<std::uint8_t>{battle::kMessageStumbled}));
    const auto shown = animations(script);
    EXPECT_EQ(shown[0].what, Shown{SpecialAnimation::DanceStumble});
    EXPECT_EQ(shown[0].params.targets, BattlerSet::of(kTerra));
    EXPECT_TRUE(allOf<battle::AttackNameShown>(script).empty());
}

TEST(Commands, ADancerWithoutADanceDancesTheLastOneChosen) {
    BattleState state = commandBattle();
    state.background = BattleBackgroundId::FOREST_WOB;
    state.lastDance = DanceId::FOREST_SUITE;
    battle::AttackHooks hooks;
    run(state, hooks, kTerra, danceStep());
    EXPECT_EQ(state.at(kTerra).dance, DanceId::FOREST_SUITE);
    EXPECT_TRUE(state.at(kTerra).status.has(StatusId::DANCE));
}

// --- the commands that act on the user -------------------------------------------------------

CommandAction plain(BattleCommandId command) {
    return CommandAction{.command = command, .targets = BattlerSet::of(kGuard)};
}

TEST(Commands, MorphPutsMorphOnTheMorpher) {
    EXPECT_EQ(getAttackProperties(AttackId::BATTLE).statuses.bytes, StatusSet{}.bytes);
    BattleState state = commandBattle();
    AttackLog log;
    const auto shown = animations(run(state, log, kTerra, plain(BattleCommandId::MORPH)));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].record.statuses.bytes, StatusSet::of(StatusId::MORPH).bytes);
    EXPECT_FALSE(log.made[0].record.flags2.has(AttackFlag2::REMOVE_STATUS));
    EXPECT_TRUE(log.made[0].record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::MORPH});
    EXPECT_EQ(shown[0].params.targets, BattlerSet::of(kTerra));
}

TEST(Commands, RevertTakesMorphOff) {
    BattleState state = commandBattle();
    state.at(kTerra).status.set(StatusId::MORPH);
    AttackLog log;
    run(state, log, kTerra, plain(BattleCommandId::REVERT));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].record.statuses.bytes, StatusSet::of(StatusId::MORPH).bytes);
    EXPECT_TRUE(log.made[0].record.flags2.has(AttackFlag2::REMOVE_STATUS));
}

TEST(Commands, RevertWithoutMorphOnlyStandsTheUserUp) {
    BattleState state = commandBattle();
    AttackLog log;
    const auto script = run(state, log, kTerra, plain(BattleCommandId::REVERT));
    ASSERT_EQ(script.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<battle::PoseReset>(script[0]));
    EXPECT_EQ(std::get<battle::PoseReset>(script[0]).battler, kTerra);
    EXPECT_TRUE(log.made.empty());
}

TEST(Commands, RowRunicAndDefActOnTheUser) {
    BattleState state = commandBattle();
    battle::AttackHooks hooks;
    auto shown = animations(run(state, hooks, kTerra, plain(BattleCommandId::ROW)));
    EXPECT_TRUE(state.at(kTerra).backRow);
    EXPECT_EQ(shown[0].params.targets, BattlerSet::of(kTerra));
    run(state, hooks, kTerra, plain(BattleCommandId::ROW));
    EXPECT_FALSE(state.at(kTerra).backRow);

    run(state, hooks, kTerra, plain(BattleCommandId::RUNIC));
    EXPECT_TRUE(state.at(kTerra).triggers.runic);
    shown = animations(run(state, hooks, kTerra, plain(BattleCommandId::DEF)));
    EXPECT_TRUE(state.at(kTerra).defending);
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::DEF});
}

// --- the commands that act on a target -------------------------------------------------------

TEST(Commands, StealIsAnUndodgeableTouchThatSteals) {
    BattleState state = commandBattle();
    AttackLog log;
    run(state, log, kTerra, plain(BattleCommandId::STEAL));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].effect, SpecialEffect::of(battle::CommandEffect::Steal));
    EXPECT_TRUE(log.made[0].record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_EQ(log.made[0].attackerLevel, 0);
    EXPECT_EQ(log.made[0].attackerStat, 0);
}

TEST(Commands, LeapAndControlAreNotBlows) {
    BattleState state = commandBattle();
    AttackLog log;
    run(state, log, kTerra, plain(BattleCommandId::LEAP));
    EXPECT_EQ(log.made[0].effect, SpecialEffect::of(battle::CommandEffect::Leap));
    EXPECT_FALSE(log.made[0].record.traits.has(AttackTrait::PHYSICAL));
    EXPECT_EQ(log.made[0].targeting.bits, Targeting::of(TargetFlags::ENEMY).bits);
    run(state, log, kTerra, plain(BattleCommandId::CONTROL));
    EXPECT_EQ(log.made[1].effect, SpecialEffect::of(battle::CommandEffect::Control));
    EXPECT_FALSE(log.made[1].record.traits.has(AttackTrait::PHYSICAL));
    EXPECT_TRUE(log.made[1].record.flags2.has(AttackFlag2::UNDODGEABLE));
}

TEST(Commands, PossessCannotBeDodged) {
    BattleState state = commandBattle();
    AttackLog log;
    run(state, log, kTerra, plain(BattleCommandId::POSSESS));
    EXPECT_EQ(log.made[0].effect, SpecialEffect::of(battle::CommandEffect::Possess));
    EXPECT_TRUE(log.made[0].record.flags2.has(AttackFlag2::UNDODGEABLE));
}

TEST(Commands, GpRainThrowsCoinsThatIgnoreDefenseUnsplit) {
    BattleState state = commandBattle();
    AttackLog log;
    const auto byTerra = run(state, log, kTerra, plain(BattleCommandId::GP_RAIN));
    const AttackInFlight& coins = log.made[0];
    EXPECT_EQ(coins.effect, SpecialEffect::of(battle::CommandEffect::GpRain));
    EXPECT_EQ(coins.record.power, getAttackProperties(AttackId::BATTLE).power + 1);
    EXPECT_TRUE(coins.record.traits.has(AttackTrait::IGNORE_DEFENSE));
    EXPECT_TRUE(coins.record.traits.has(AttackTrait::NO_DAMAGE_SPLIT));
    EXPECT_FALSE(coins.scratch.damageModification);
    EXPECT_TRUE(allOf<battle::CommandNameShown>(byTerra).empty());

    const auto byGuard = run(state, log, kGuard, CommandAction{.command = BattleCommandId::GP_RAIN, .targets = BattlerSet::of(kTerra)});
    ASSERT_EQ(allOf<battle::CommandNameShown>(byGuard).size(), 1u);
    EXPECT_EQ(allOf<battle::CommandNameShown>(byGuard)[0].command, BattleCommandId::GP_RAIN);
}

TEST(Commands, AControllerHasItsMonsterMakeThePickedAttack) {
    BattleState state = commandBattle();
    state.at(kTerra).status.set(StatusId::CHANT);
    state.at(kGuard).controlledBy = kTerra;
    battle::AttackHooks hooks;
    const auto script = run(state, hooks, kTerra,
                            CommandAction{.command = BattleCommandId::CONTROL, .attack = AttackId::BATTLE,
                                          .targets = BattlerSet::of(kSoldier)});
    ASSERT_GE(script.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<battle::PoseReset>(script[0]));
    EXPECT_EQ(std::get<battle::PoseReset>(script[0]).battler, kTerra);
    const auto shown = animations(script);
    ASSERT_FALSE(shown.empty());
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::FIGHT});
    EXPECT_EQ(shown[0].params.attacker, kGuard);
}

TEST(Commands, ASketchThatCatchesNothingIsOnlyTheSketch) {
    BattleState state = commandBattle();
    AttackLog log;
    const auto shown = animations(run(state, log, kTerra, plain(BattleCommandId::SKETCH)));
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::SKETCH});
    EXPECT_EQ(log.made[0].effect, SpecialEffect::of(battle::CommandEffect::Sketch));
}

TEST(Commands, ACaughtSketchIsMadeByTheSketcherAsTheMonsterWould) {
    BattleState state = commandBattle();
    state.at(kTerra).level = 20;
    Battler& guard = state.at(kGuard);
    guard.level = 5;
    guard.magicPower = 99;
    guard.specialAttack = MonsterSpecialAttack::drainHp();
    guard.specialAttackAnimation = static_cast<MonsterAttackAnimation>(7);
    AttackLog log;
    log.onTargetEffect = [](AttackInFlight& attack, BattlerSlot target) {
        if (attack.command == BattleCommandId::SKETCH) {
            attack.scratch.sketch = battle::SketchDrawn{.monster = target, .attack = AttackId::FIRE};
        }
    };
    const auto script = run(state, log, kTerra, plain(BattleCommandId::SKETCH));
    ASSERT_EQ(log.made.size(), 2u);
    const AttackInFlight& sketched = log.made[1];
    EXPECT_EQ(sketched.attack, AttackId::FIRE);
    EXPECT_EQ(sketched.command, BattleCommandId::MAGIC);
    EXPECT_EQ(sketched.attackerStat, 99);
    EXPECT_EQ(sketched.attackerLevel, 20);
    EXPECT_TRUE(sketched.scratch.randomizeTargets);
    EXPECT_TRUE(sketched.rules.randomTarget);
    EXPECT_EQ(state.at(kTerra).specialAttack.packed, guard.specialAttack.packed);
    EXPECT_EQ(state.at(kTerra).specialAttackAnimation, guard.specialAttackAnimation);
    EXPECT_EQ(allOf<battle::AttackNameShown>(script).back().attack, AttackId::FIRE);
    const auto shown = animations(script);
    ASSERT_EQ(shown.size(), 2u);
    EXPECT_EQ(shown[1].params.attacker, kTerra);
}

// The first monster whose record says it flies.
MonsterId aFlier() {
    for (const MonsterPropertiesEntry& entry : monsterProperties()) {
        if ((entry.record.innateStatuses.bytes[2] & 0x01) != 0) {
            return entry.id;
        }
    }
    ADD_FAILURE() << "no monster flies";
    return MonsterId{};
}

TEST(Commands, RageTakesOnTheMonsterAndMakesItsAttack) {
    BattleState state = commandBattle();
    Battler& gau = state.at(kTerra);
    gau.actor = CharacterPropId::GAU;
    const MonsterId flier = aFlier();
    gau.rage = flier;
    battle::AttackHooks hooks;
    const auto shown = animations(run(state, hooks, kTerra,
                                      CommandAction{.command = BattleCommandId::RAGE, .attack = AttackId::BATTLE,
                                                    .targets = BattlerSet::of(kGuard)}));
    EXPECT_EQ(state.lastRager, kTerra);
    EXPECT_TRUE(gau.status.has(StatusId::RAGE));
    EXPECT_EQ(gau.specialAttackAnimation, monsterSpecialAnim(flier));
    EXPECT_EQ(gau.specialAttack.packed, getMonsterProperties(flier).specialAttack.packed);
    EXPECT_EQ(gau.weaponHand.item, getMonsterProperties(flier).attackGraphic);
    EXPECT_EQ(gau.shieldHand.item, getMonsterProperties(flier).attackGraphic);
    EXPECT_TRUE(gau.statusesToSet.has(StatusId::FLOAT));
    ASSERT_FALSE(shown.empty());
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::FIGHT});
    EXPECT_EQ(shown[0].secondary, static_cast<std::uint8_t>(getMonsterProperties(flier).attackGraphic) + 1);
}

TEST(Commands, ARageWithNoMonsterChosenTakesTheLastRagers) {
    BattleState state = commandBattle();
    const MonsterId flier = aFlier();
    state.at(kLocke).rage = flier;
    state.lastRager = kLocke;
    battle::AttackHooks hooks;
    run(state, hooks, kTerra,
        CommandAction{.command = BattleCommandId::RAGE, .attack = AttackId::BATTLE, .targets = BattlerSet::of(kGuard)});
    EXPECT_EQ(state.at(kTerra).rage, flier);
    EXPECT_EQ(state.lastRager, kTerra);
}

TEST(Commands, BushidoReadiesRetortThenAnswersWithIt) {
    BattleState state = commandBattle();
    AttackLog log;
    const CommandAction retort{.command = BattleCommandId::BUSHIDO, .attack = AttackId::RETORT,
                               .targets = BattlerSet::of(kGuard)};
    const auto readied = run(state, log, kTerra, retort);
    EXPECT_TRUE(state.at(kTerra).triggers.retort);
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].record.power, 0);
    EXPECT_FALSE(log.made[0].record.traits.has(AttackTrait::PHYSICAL));
    EXPECT_TRUE(log.made[0].record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_TRUE(allOf<battle::BushidoNameShown>(readied).empty());
    EXPECT_FALSE(animations(readied)[0].showsAttack);

    const auto answered = run(state, log, kTerra, retort);
    EXPECT_FALSE(state.at(kTerra).triggers.retort);
    EXPECT_EQ(log.made[1].record.power, getAttackProperties(AttackId::RETORT).power);
    ASSERT_EQ(allOf<battle::BushidoNameShown>(answered).size(), 1u);
    EXPECT_EQ(allOf<battle::BushidoNameShown>(answered)[0].technique, AttackId::RETORT);
    EXPECT_TRUE(animations(answered)[0].showsAttack);
    EXPECT_TRUE(animations(answered)[0].params.castingSwirl);
}

TEST(Commands, AnyOtherTechniqueIsNamed) {
    BattleState state = commandBattle();
    battle::AttackHooks hooks;
    const auto script = run(state, hooks, kTerra,
                            CommandAction{.command = BattleCommandId::BUSHIDO, .attack = AttackId::DISPATCH,
                                          .targets = BattlerSet::of(kGuard)});
    EXPECT_EQ(allOf<battle::BushidoNameShown>(script)[0].technique, AttackId::DISPATCH);
    EXPECT_FALSE(animations(script)[0].params.castingSwirl);
}

TEST(Commands, ABlitzIsNamedAndAFailedOneHitsNoOne) {
    BattleState state = commandBattle();
    AttackLog log;
    const auto pummel = run(state, log, kTerra,
                            CommandAction{.command = BattleCommandId::BLITZ, .attack = AttackId::PUMMEL,
                                          .targets = BattlerSet::of(kGuard)});
    EXPECT_EQ(allOf<battle::AttackNameShown>(pummel)[0].attack, AttackId::PUMMEL);
    EXPECT_FALSE(log.made[0].hitsNoOne);

    const auto failed = run(state, log, kTerra,
                            CommandAction{.command = BattleCommandId::BLITZ, .attack = AttackId::BATTLE,
                                          .targets = BattlerSet::of(kGuard)});
    EXPECT_TRUE(allOf<battle::AttackNameShown>(failed).empty());
    EXPECT_EQ(messages(failed), (std::vector<std::uint8_t>{battle::kMessageBlitzFailed}));
    EXPECT_EQ(log.made[1].attack, AttackId::PUMMEL);
    EXPECT_TRUE(log.made[1].hitsNoOne);
    EXPECT_TRUE(animations(failed)[0].params.targets.empty());
}

TEST(Commands, TheBlitzesAreTheEightFromPummel) {
    for (int i = 0; i < 8; ++i) {
        BattleState state = commandBattle();
        AttackLog log;
        const auto attack = static_cast<AttackId>(static_cast<int>(AttackId::PUMMEL) + i);
        run(state, log, kTerra, CommandAction{.command = BattleCommandId::BLITZ, .attack = attack,
                                              .targets = BattlerSet::of(kGuard)});
        EXPECT_FALSE(log.made[0].hitsNoOne) << i;
    }
    BattleState state = commandBattle();
    AttackLog log;
    run(state, log, kTerra, CommandAction{.command = BattleCommandId::BLITZ, .attack = AttackId::WIND_SLASH,
                                          .targets = BattlerSet::of(kGuard)});
    EXPECT_TRUE(log.made[0].hitsNoOne);
}

// --- Jump ------------------------------------------------------------------------------------

const CommandAction kJump{.command = BattleCommandId::JUMP, .targets = BattlerSet::of(kGuard)};

TEST(Commands, AJumpComesDownWithTheHandThatHasPower) {
    BattleState state = commandBattle();
    state.at(kTerra).shieldHand.battlePower = 0;
    AttackLog log;
    run(state, log, kTerra, kJump);
    EXPECT_FALSE(log.made[0].offHand);

    state.at(kTerra).shieldHand.battlePower = 40;
    state.at(kTerra).weaponHand.battlePower = 0;
    run(state, log, kTerra, kJump);
    EXPECT_TRUE(log.made[1].offHand);
}

TEST(Commands, WithPowerInBothHandsACoinPicksTheHand) {
    for (const bool heads : {true, false}) {
        BattleState state = commandBattle();
        rngWhere(state, {heads ? test::heads : test::tails});
        AttackLog log;
        run(state, log, kTerra, kJump);
        EXPECT_EQ(log.made[0].offHand, heads);
    }
}

TEST(Commands, AJumpIsUndodgeableAndHalfAgainAsStrong) {
    BattleState state = commandBattle();
    state.at(kTerra).status.set(StatusId::HIDE);
    state.at(kTerra).relicEffects3.set(RelicEffect3::HIT_100_IGNORE_MBLOCK);
    AttackLog log;
    const auto shown = animations(run(state, log, kTerra, kJump));
    ASSERT_EQ(log.made.size(), 1u);
    EXPECT_EQ(log.made[0].record.flags2.bits, AttackFlags2::of(AttackFlag2::UNDODGEABLE).bits);
    EXPECT_TRUE(log.made[0].rowIgnored);
    EXPECT_EQ(log.made[0].extraMultiplier, 1);
    EXPECT_FALSE(state.at(kTerra).status.has(StatusId::HIDE));
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::JUMP});
}

TEST(Commands, ASpearInEitherHandDoublesTheJump) {
    BattleState state = commandBattle();
    state.at(kTerra).shieldHand.item = ItemId::STOUT_SPEAR;
    AttackLog log;
    run(state, log, kTerra, kJump);
    EXPECT_EQ(log.made[0].extraMultiplier, 2);
    EXPECT_TRUE(battle::isSpear(ItemId::MITHRIL_PIKE));
    EXPECT_TRUE(battle::isSpear(ItemId::IMP_HALBERD));
    EXPECT_FALSE(battle::isSpear(static_cast<ItemId>(static_cast<int>(ItemId::MITHRIL_PIKE) - 1)));
    EXPECT_FALSE(battle::isSpear(static_cast<ItemId>(static_cast<int>(ItemId::IMP_HALBERD) + 1)));
}

TEST(Commands, ADragonHornComesDownTwoToFourTimes) {
    struct Case {
        std::function<bool(std::uint8_t)> draw;
        std::size_t landings;
    };
    // The draws either side of each threshold.
    const auto exactly = [](std::uint8_t value) { return [value](std::uint8_t r) { return r == value; }; };
    const std::vector<Case> cases = {
        {exactly(0x40), 2},
        {exactly(0x3F), 3},
        {exactly(0x10), 3},
        {exactly(0x0F), 4},
    };
    for (const Case& c : cases) {
        BattleState state = commandBattle();
        state.at(kTerra).shieldHand.battlePower = 0;
        state.at(kTerra).relicEffects2.set(RelicEffect2::CONTINUOUS_JUMP);
        rngWhere(state, {c.draw});
        AttackLog log;
        const auto shown = animations(run(state, log, kTerra, kJump));
        EXPECT_EQ(log.made.size(), c.landings);
        ASSERT_FALSE(shown.empty());
        EXPECT_TRUE(shown.back().params.dragonHorn);
    }
}

// --- the Roulette command, and no command at all ---------------------------------------------

TEST(Commands, TheRouletteCommandCastsAtNoOneThenShowsTheCursor) {
    BattleState state = commandBattle();
    battle::AttackHooks hooks;
    const auto shown = animations(run(state, hooks, kTerra,
                                      CommandAction{.command = battle::kRouletteCommand, .attack = AttackId::ROULETTE,
                                                    .targets = BattlerSet::of(kGuard)}));
    ASSERT_EQ(shown.size(), 2u);
    EXPECT_EQ(shown[0].what, Shown{BattleCommandId::LORE});
    EXPECT_EQ(shown[0].attack, AttackId::ROULETTE);
    EXPECT_TRUE(shown[0].params.targets.empty());
    EXPECT_EQ(shown[1].what, Shown{SpecialAnimation::RouletteCursor});
}

TEST(Commands, MimicShowsNothing) {
    BattleState state = commandBattle();
    battle::AttackHooks hooks;
    EXPECT_TRUE(run(state, hooks, kTerra, plain(BattleCommandId::MIMIC)).empty());
}

TEST(Commands, AnAttacksCommandIsTheCommandOfItsRange) {
    EXPECT_EQ(battle::commandForAttack(AttackId::FIRE), BattleCommandId::MAGIC);
    EXPECT_EQ(battle::commandForAttack(AttackId::BAHAMUT), BattleCommandId::SUMMON);
    EXPECT_EQ(battle::commandForAttack(AttackId::RETORT), BattleCommandId::BUSHIDO);
    EXPECT_EQ(battle::commandForAttack(AttackId::PUMMEL), BattleCommandId::BLITZ);
    EXPECT_EQ(battle::commandForAttack(AttackId::AERO), BattleCommandId::LORE);
    EXPECT_EQ(battle::commandForAttack(AttackId::BATTLE), BattleCommandId::FIGHT);
    EXPECT_EQ(battle::commandForAttack(AttackId::SPECIAL), BattleCommandId::FIGHT);
    EXPECT_EQ(battle::commandForAttack(AttackId::RIOT_BLADE), BattleCommandId::MAGIC);
}

}  // namespace
}  // namespace ostinato::test
