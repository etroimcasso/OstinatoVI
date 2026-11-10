// The special effects on each target (docs/contracts/battle_effects.md, "The effects on each
// target"): every effect the original's table names, each worked out by hand from the original and
// also run on the cartridge's own routine (tools/damage-trace/damage_oracle.h); the port must match
// both. Then the routine on the oracle — where the US cartridges hold it, where the frame's and the
// bounce's calls return — and 500 drawn scenarios over which the port leaves every cell as the
// cartridge does.

#include <gtest/gtest.h>

#include <cstddef>
#include <bit>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "assets/cartridge.h"
#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/target_effects.h"
#include "damage_oracle.h"
#include "data/attack_properties.h"
#include "data/formations.h"
#include "data/item_properties.h"
#include "data/metamorph.h"
#include "data/monster_attacks.h"
#include "data/rng_table.h"
#include "ostinato/attack_effects.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/game_version.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/item_type.h"
#include "ostinato/metamorph_info.h"
#include "ostinato/monster_entrance_type.h"
#include "ostinato/monster_flags.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using battle::CommandEffect;
using battle::SpecialEffect;
using trace::DamageOracle;
using trace::DamageScenario;
using trace::TargetEffectResult;
using trace::TargetEffectRun;

class TargetEffectTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    // The port's effect on `target` over `scenario`, checked against the cartridge's own answer.
    static TargetEffectRun checked(const DamageScenario& scenario, BattlerSlot target) {
        TargetEffectRun run = trace::runTargetEffect(scenario, target);
        TargetEffectResult guest;
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            guest = oracle.targetEffect(scenario, target);
        });
        EXPECT_EQ(trace::differences(trace::targetEffectResult(run), guest), "");
        return run;
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* TargetEffectTest::image_ = nullptr;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

constexpr std::uint32_t kDoTargetEffect = 0xC2387E;

TEST_F(TargetEffectTest, AnImageWithoutDoTargetEffectIsRefused) {
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        std::vector<std::uint8_t> other(original.begin(), original.end());
        other[offsetOf(kDoTargetEffect)] ^= 0xFF;
        EXPECT_THROW(DamageOracle{other}, std::runtime_error);
    });
}

// The attack frame's `jsr DoTargetEffect` (battle_main.asm:8357) returns to $C2:345F, the bounce's
// (:8435) to $C2:34FC: the three bytes before each are the call.
TEST_F(TargetEffectTest, TheFramesAndTheBouncesCallsReturnWhereTheTraceExpects) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const std::vector<std::uint8_t> toDoTargetEffect{0x20, 0x7E, 0x38};
        for (const std::uint32_t returnsTo : {0xC2345Fu, 0xC234FCu}) {
            const std::vector<std::uint8_t> call =
                oracle.machine().read(retropp::MemoryRegion{.at = returnsTo - 3, .size = 3});
            EXPECT_EQ(call, toDoTargetEffect) << std::hex << returnsTo;
        }
    });
}

// With no special effect the routine changes nothing: every cell reads back as written.
TEST_F(TargetEffectTest, AScenarioWithNoEffectReadsBackAsWritten) {
    std::mt19937 rng{trace::kSweepSeed};
    trace::TargetScenario drawn = trace::drawTargetEffectScenario(rng);
    drawn.scenario.attack.effect = {};
    const TargetEffectResult unchanged = trace::targetEffectResult(trace::runTargetEffect(drawn.scenario, drawn.target));
    TargetEffectResult guest;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        guest = oracle.targetEffect(drawn.scenario, drawn.target);
    });
    EXPECT_EQ(trace::differences(unchanged, guest), "");
}


constexpr BattlerSlot kHero = BattlerSlot::character(1);
constexpr BattlerSlot kAlly = BattlerSlot::character(2);
constexpr BattlerSlot kMonster = BattlerSlot::monster(2);
constexpr BattlerSlot kOtherMonster = BattlerSlot::monster(4);

// A battle with two party members and two monsters on the field, and an attack by `attacker`
// carrying `effect` at `target`, as the attack frame leaves it for the effect: the target out of the
// targets hit, marked to answer the attacker, its last command hit recorded.
DamageScenario scenarioWith(SpecialEffect effect, BattlerSlot attacker = kHero, BattlerSlot target = kMonster) {
    DamageScenario s;
    s.attacker = attacker;
    s.attack.effect = effect;
    s.attack.attackerLevel = 30;
    s.attack.damage = 500;
    s.attack.record.power = 40;
    s.attack.record.hitRate = 100;
    s.attack.secondary = 0x2A;
    s.attack.targets = BattlerSet::of(target);
    s.attack.params.attacker = attacker;
    s.attack.params.attackerIsMonster = attacker.isMonster();
    s.attack.params.targets = BattlerSet::of(target);
    s.attack.params.noCharacterTargets = target.isMonster();
    s.attack.scratch.targetCount = 1;
    for (const BattlerSlot slot : {kHero, kAlly, kMonster, kOtherMonster}) {
        battle::Battler& battler = s.state.at(slot);
        battler.present = true;
        battler.level = 30;
        battler.maxHp = 2000;
        battler.hp = 1500;
        battler.maxMp = 300;
        battler.mp = 200;
        if (slot.isMonster()) {
            battler.monster = MonsterId::SOLDIER;
        }
    }
    s.state.allyCount = 2;
    battle::Battler& hit = s.state.at(target);
    hit.lastAttacker = attacker;
    hit.retaliationPending = true;
    hit.lastHit.command = battle::Hit<BattleCommandId>{.by = attacker, .with = BattleCommandId::FIGHT};
    return s;
}

// The first random-number position whose next draws satisfy `wanted`.
std::uint8_t positionWhere(const std::function<bool(std::uint8_t)>& wanted) {
    for (int position = 0; position < 256; ++position) {
        if (wanted(rngByte(static_cast<std::uint8_t>(position + 1)))) {
            return static_cast<std::uint8_t>(position);
        }
    }
    ADD_FAILURE() << "no draw in the table satisfies the case";
    return 0;
}

std::uint8_t positionWhere(const std::function<bool(std::uint8_t, std::uint8_t)>& wanted) {
    for (int position = 0; position < 256; ++position) {
        if (wanted(rngByte(static_cast<std::uint8_t>(position + 1)), rngByte(static_cast<std::uint8_t>(position + 2)))) {
            return static_cast<std::uint8_t>(position);
        }
    }
    ADD_FAILURE() << "no pair of draws in the table satisfies the case";
    return 0;
}

// The draw after `position`.
std::uint8_t drawAfter(std::uint8_t position, int nth = 1) {
    return rngByte(static_cast<std::uint8_t>(position + nth));
}

// RandA: a draw scaled by `n` / 256.
int scaled(std::uint8_t draw, int n) {
    return draw * n >> 8;
}

// The `n`th set bit of `bits`, counting from the lowest.
std::uint8_t nthBit(std::uint8_t bits, int n) {
    for (unsigned bit = 0; bit < 8; ++bit) {
        const auto mask = static_cast<std::uint8_t>(1u << bit);
        if ((bits & mask) != 0 && n-- == 0) {
            return mask;
        }
    }
    return 0;
}

// --- the weapons -----------------------------------------------------------------------------------

constexpr std::uint8_t kKillBelow = 0x40;

TEST_F(TargetEffectTest, TheScimitarKillsOnADrawBelow64WhenStaminaDoesNotSave) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SCIMITAR_ZANTETSUKEN));
    s.state.at(kMonster).stamina = 10;
    s.attack.scratch.earlierTargets = BattlerSet::of(kMonster);
    s.state.rng = battle::BattleRng{
        positionWhere([](std::uint8_t first, std::uint8_t second) { return first < kKillBelow && (second & 0x7F) > 10; })};
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.missed);
    const battle::Battler& monster = run.after.state.at(kMonster);
    EXPECT_TRUE(monster.statusesToSet.has(StatusId::DEAD));
    EXPECT_TRUE(monster.statusesToSet.has(StatusId::HIDE));
    EXPECT_TRUE(run.after.attack.params.targetsHit.has(kMonster));
    EXPECT_FALSE(run.after.attack.scratch.earlierTargets.has(kMonster));
    EXPECT_TRUE(run.after.attack.params.castingSwirl);
    EXPECT_TRUE(run.after.attack.scratch.hideWithoutTargets);
    EXPECT_EQ(run.after.attack.record.power, 0);
    EXPECT_EQ(run.after.attack.command, BattleCommandId::MAGIC);
    EXPECT_EQ(run.after.attack.attack, AttackId::BATTLE);
    EXPECT_EQ(run.script.commands().size(), 2u);
    EXPECT_EQ(run.script.params().size(), 1u);
}

TEST_F(TargetEffectTest, TheScimitarDoesNothingOnADrawOf64OrMore) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SCIMITAR_ZANTETSUKEN));
    const std::uint8_t at = positionWhere([](std::uint8_t r) { return r >= kKillBelow; });
    s.state.rng = battle::BattleRng{at};
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
    EXPECT_EQ(run.after.state.rng.position(), static_cast<std::uint8_t>(at + 1));
}

TEST_F(TargetEffectTest, StaminaAtOrAboveTheSecondDrawSavesFromTheScimitar) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SCIMITAR_ZANTETSUKEN));
    s.state.at(kMonster).stamina = 127;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < kKillBelow; })};
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
}

TEST_F(TargetEffectTest, ATargetProtectedFromInstantDeathIsNeverKilledAndNothingIsDrawn) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SCIMITAR_ZANTETSUKEN));
    s.state.at(kMonster).instantDeathProtected = true;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < kKillBelow; })};
    const std::uint8_t before = s.state.rng.position();
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.missed);
    EXPECT_FALSE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
    EXPECT_EQ(run.after.state.rng.position(), before);
}

TEST_F(TargetEffectTest, TheScimitarKillsAPartyMemberWithoutHidingIt) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SCIMITAR_ZANTETSUKEN), kMonster, kHero);
    s.state.rng = battle::BattleRng{
        positionWhere([](std::uint8_t first, std::uint8_t second) { return first < kKillBelow && (second & 0x7F) > 0; })};
    const auto run = checked(s, kHero);
    EXPECT_TRUE(run.after.state.at(kHero).statusesToSet.has(StatusId::DEAD));
    EXPECT_FALSE(run.after.state.at(kHero).statusesToSet.has(StatusId::HIDE));
}

TEST_F(TargetEffectTest, ADragonHornsLaterLandingsNeverKillOnAUsCartridge) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SCIMITAR_ZANTETSUKEN));
    s.attack.command = BattleCommandId::JUMP;
    s.attack.scratch.attacksLeft = 1;
    s.state.rng = battle::BattleRng{
        positionWhere([](std::uint8_t first, std::uint8_t second) { return first < kKillBelow && (second & 0x7F) > 0; })};
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
    EXPECT_EQ(run.after.attack.command, BattleCommandId::JUMP);
}

TEST_F(TargetEffectTest, AJapaneseCartridgeKillsOnADragonHornsLaterLandings) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SCIMITAR_ZANTETSUKEN));
    s.state.version = GameVersion::JP_1_0;
    s.attack.command = BattleCommandId::JUMP;
    s.attack.scratch.attacksLeft = 1;
    s.state.rng = battle::BattleRng{
        positionWhere([](std::uint8_t first, std::uint8_t second) { return first < kKillBelow && (second & 0x7F) > 0; })};
    const auto run = trace::runTargetEffect(s, kMonster);
    EXPECT_TRUE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
}

TEST_F(TargetEffectTest, AnXWeaponKillsTheUndeadWithoutADrawAndDoomBringsAMonsterBack) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::INSTANT_KILL));
    s.state.at(kMonster).undead = true;
    const std::uint8_t before = s.state.rng.position();
    const auto run = checked(s, kMonster);
    const battle::Battler& monster = run.after.state.at(kMonster);
    EXPECT_TRUE(monster.statusesToSet.has(StatusId::DEAD));
    EXPECT_TRUE(monster.statusesToSet.has(StatusId::HIDE));
    EXPECT_EQ(run.after.attack.attack, AttackId::FLASH_TOOL);
    EXPECT_EQ(run.after.state.rng.position(), before);
    ASSERT_EQ(run.after.state.immediateActions.size(), 1u);
    const auto* back = std::get_if<battle::MonsterVisibilityAction>(&run.after.state.immediateActions.front());
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->mode, battle::MonsterVisibilityMode::ReviveAtFullHp);
    EXPECT_EQ(back->monsters, BattlerSet::of(kMonster));
    EXPECT_EQ(back->type, MonsterEntranceType::FADE_IN_CHECKERED);
}

TEST_F(TargetEffectTest, AnXWeaponThatMissesItsDrawLeavesTheLivingAlone) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::INSTANT_KILL));
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r >= kKillBelow; })};
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
    EXPECT_TRUE(run.after.state.immediateActions.empty());
}

TEST_F(TargetEffectTest, AnXWeaponOnAnUndeadPartyMemberHealsItAndItDoesNotDie) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::INSTANT_KILL), kMonster, kHero);
    s.state.at(kHero).undead = true;
    const auto run = checked(s, kHero);
    const battle::Battler& hero = run.after.state.at(kHero);
    EXPECT_FALSE(hero.statusesToSet.has(StatusId::DEAD));
    EXPECT_EQ(hero.hp, 2000);
    EXPECT_EQ(run.after.attack.command, BattleCommandId::MAGIC);
}

TEST_F(TargetEffectTest, TheManEaterStrikesAHumanForTwoMoreSteps) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::MAN_EATER));
    s.state.at(kMonster).human = true;
    s.attack.multiplier = 1;
    EXPECT_EQ(checked(s, kMonster).after.attack.multiplier, 3);
    s.state.at(kMonster).human = false;
    EXPECT_EQ(checked(s, kMonster).after.attack.multiplier, 1);
}

TEST_F(TargetEffectTest, TheSniperStrikesHalfAgainOnAnOddDraw) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SNIPER_HAWKEYE));
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return (r & 1) != 0; })};
    EXPECT_EQ(checked(s, kMonster).after.attack.multiplier, 1);
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return (r & 1) == 0; })};
    EXPECT_EQ(checked(s, kMonster).after.attack.multiplier, 0);
}

TEST_F(TargetEffectTest, TheSnipersFightAtAFloatingTargetIsThrownForFourSteps) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SNIPER_HAWKEYE));
    s.state.at(kMonster).status.set(StatusId::FLOAT);
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return (r & 1) != 0; })};
    const auto run = checked(s, kMonster);
    EXPECT_EQ(run.after.attack.multiplier, 4);
    EXPECT_EQ(run.after.attack.command, BattleCommandId::THROW);
    EXPECT_EQ(run.after.attack.attack, static_cast<AttackId>(0x29));
    const auto* thrown = std::get_if<battle::AttackAnimation>(&run.script.commands().front());
    ASSERT_NE(thrown, nullptr);
    EXPECT_EQ(thrown->item, static_cast<ItemId>(0x29));
}

TEST_F(TargetEffectTest, TheSniperOnlyThrowsAFight) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::SNIPER_HAWKEYE));
    s.attack.command = BattleCommandId::JUMP;
    s.state.at(kMonster).status.set(StatusId::FLOAT);
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return (r & 1) != 0; })};
    const auto run = checked(s, kMonster);
    EXPECT_EQ(run.after.attack.multiplier, 1);
    EXPECT_EQ(run.after.attack.command, BattleCommandId::JUMP);
}

// --- the attacks -----------------------------------------------------------------------------------

TEST_F(TargetEffectTest, ScanQueuesItsReportOrSaysItCannotProbe) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::SCAN));
    auto run = checked(s, kMonster);
    ASSERT_EQ(run.after.state.immediateActions.size(), 1u);
    EXPECT_EQ(std::get<battle::ScanAction>(run.after.state.immediateActions.front()).target, kMonster);
    s.state.at(kMonster).monsterFlags.set(MonsterBattleFlag::CANT_SCAN);
    run = checked(s, kMonster);
    EXPECT_TRUE(run.after.state.immediateActions.empty());
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageCantProbe);
    EXPECT_FALSE(run.missed);
}

TEST_F(TargetEffectTest, MetamorphMissesAPartyMember) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::METAMORPH), kMonster, kHero);
    const auto run = checked(s, kHero);
    EXPECT_TRUE(run.missed);
    EXPECT_TRUE(run.after.attack.scratch.missed.has(kHero));
}

TEST_F(TargetEffectTest, MetamorphTurnsTheMonsterIntoAnItemFromItsPack) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::METAMORPH));
    const MetamorphInfo info{.packed = 0x05};  // pack 5, the surest odds
    s.state.at(kMonster).metamorph = info;
    const std::uint8_t at = positionWhere([](std::uint8_t r) { return r < 0xFF; });
    s.state.rng = battle::BattleRng{static_cast<std::uint8_t>(at - 2)};
    const std::size_t draw = ((drawAfter(at, -1) & 1) << 1) | (drawAfter(at, 0) & 1);
    const ItemId item = getMetamorphPack(info).items[draw];
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.missed);
    EXPECT_TRUE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
    EXPECT_EQ(run.after.state.at(kHero).itemInHand, item);
    EXPECT_TRUE(run.after.state.holdingItem.has(kHero));
    const auto* better = std::get_if<battle::BattleMessage>(&run.script.commands().front());
    ASSERT_NE(better, nullptr);
    EXPECT_EQ(better->message, battle::kMessageBetterOff);
    EXPECT_EQ(better->firstNumber, static_cast<std::uint8_t>(item));
    EXPECT_EQ(run.script.commands().size(), 2u);
}

TEST_F(TargetEffectTest, AFailedMetamorphStillWritesBetterOffInPlaceOfTheAnimation) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::METAMORPH));
    s.state.at(kMonster).metamorph = MetamorphInfo{.packed = 0xE0};  // the odds that never land
    const auto run = checked(s, kMonster);
    EXPECT_TRUE(run.missed);
    EXPECT_FALSE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
    EXPECT_NE(std::get_if<battle::BattleMessage>(&run.script.commands().front()), nullptr);
}

TEST_F(TargetEffectTest, PalidorCarriesTheTargetOffWithAJumpAtTheFrontOfItsActions) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::PALIDOR), kHero, kAlly);
    s.state.at(kAlly).actions.push_back(
        battle::CommandAction{.command = BattleCommandId::MAGIC,
                              .attack = AttackId::FIRE,
                              .targets = BattlerSet::of(kMonster),
                              .mpCost = 4});
    const auto run = checked(s, kAlly);
    const battle::Battler& ally = run.after.state.at(kAlly);
    EXPECT_TRUE(ally.pendingUpdates.leaveTheField);
    ASSERT_EQ(ally.actions.size(), 2u);
    const auto& jump = std::get<battle::CommandAction>(ally.actions.front());
    EXPECT_EQ(jump.command, BattleCommandId::JUMP);
    EXPECT_TRUE(jump.targets.empty());
    EXPECT_EQ(std::get<battle::CommandAction>(ally.actions.back()).command, BattleCommandId::MAGIC);
}

TEST_F(TargetEffectTest, PalidorGivesATargetWithNothingWaitingItsJumpAlone) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::PALIDOR), kHero, kAlly), kAlly);
    EXPECT_EQ(run.after.state.at(kAlly).actions.size(), 1u);
}

TEST_F(TargetEffectTest, TapirFillsASleepingTargetAndMissesAnAwakeOne) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::TAPIR), kHero, kAlly);
    s.state.at(kAlly).status.set(StatusId::SLEEP);
    auto run = checked(s, kAlly);
    EXPECT_EQ(run.after.state.at(kAlly).hp, 2000);
    EXPECT_EQ(run.after.state.at(kAlly).mp, 300);
    s.state.at(kAlly).status.clear(StatusId::SLEEP);
    run = checked(s, kAlly);
    EXPECT_TRUE(run.missed);
    EXPECT_EQ(run.after.state.at(kAlly).hp, 1500);
}

TEST_F(TargetEffectTest, ExploderOnItsOwnUserChangesNothingTheFrameWillNotPutBack) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::EXPLODER), kHero, kHero);
    const auto run = checked(s, kHero);
    EXPECT_FALSE(run.missed);
    EXPECT_FALSE(run.after.attack.params.targetsHit.has(kHero));
}

TEST_F(TargetEffectTest, DischordHalvesTheLevelRoundingUp) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::DISCHORD));
    s.state.at(kMonster).level = 31;
    EXPECT_EQ(checked(s, kMonster).after.state.at(kMonster).level, 16);
    s.state.at(kMonster).level = 30;
    EXPECT_EQ(checked(s, kMonster).after.state.at(kMonster).level, 15);
}

TEST_F(TargetEffectTest, PepUpGivesTheCastersLifeForTheTargetsMp) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::PEP_UP), kHero, kAlly);
    const auto run = checked(s, kAlly);
    const battle::Battler& caster = run.after.state.at(kHero);
    EXPECT_EQ(caster.hp, 0);
    EXPECT_EQ(caster.mp, 0);
    EXPECT_TRUE(caster.statusesToSet.has(StatusId::DEAD));
    EXPECT_TRUE(caster.statusesToSet.has(StatusId::HIDE));
    EXPECT_TRUE(run.after.state.leavingAfterAction.has(kHero));
    EXPECT_EQ(run.after.state.at(kAlly).mp, 300);
}

TEST_F(TargetEffectTest, RipplerSwapsTheStatusesOnlyOneHasAndWritesOverTheWaitingOnes) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::RIPPLER));
    s.state.at(kHero).status = StatusSet::of(StatusId::HASTE, StatusId::REGEN, StatusId::BLIND);
    s.state.at(kMonster).status = StatusSet::of(StatusId::SLOW, StatusId::REGEN, StatusId::POISON);
    s.state.at(kMonster).statusesToSet = StatusSet::of(StatusId::SLEEP);
    const auto run = checked(s, kMonster);
    const StatusSet heros = StatusSet::of(StatusId::HASTE, StatusId::BLIND);
    const StatusSet monsters = StatusSet::of(StatusId::SLOW, StatusId::POISON);
    EXPECT_EQ(run.after.state.at(kMonster).statusesToSet.bytes, heros.bytes);
    EXPECT_EQ(run.after.state.at(kHero).statusesToClear.bytes, heros.bytes);
    EXPECT_EQ(run.after.state.at(kHero).statusesToSet.bytes, monsters.bytes);
    EXPECT_EQ(run.after.state.at(kMonster).statusesToClear.bytes, monsters.bytes);
}

TEST_F(TargetEffectTest, StoneStrikesATargetOfTheCastersLevelForFourteenMoreSteps) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::STONE));
    EXPECT_EQ(checked(s, kMonster).after.attack.multiplier, 14);
    s.state.at(kMonster).level = 31;
    EXPECT_EQ(checked(s, kMonster).after.attack.multiplier, 0);
}

TEST_F(TargetEffectTest, SomeAttacksTurnCounterattacksOff) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::DISABLE_COUNTERATTACK)), kMonster);
    EXPECT_FALSE(run.after.state.scratch.countersEnabled);
}

TEST_F(TargetEffectTest, SomeAttacksMissFloatingTargets) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::MISSES_FLOATING_TARGETS));
    EXPECT_FALSE(checked(s, kMonster).missed);
    s.state.at(kMonster).status.set(StatusId::FLOAT);
    EXPECT_TRUE(checked(s, kMonster).missed);
}

TEST_F(TargetEffectTest, WallchangeDrawsOneWeaknessNullifiesTheRestAndAbsorbsOneOfThem) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::WALLCHANGE));
    s.state.at(kMonster).elementsHalved.bits = 0x11;
    s.state.rng = battle::BattleRng{40};
    const std::uint8_t weak = nthBit(0xFF, scaled(drawAfter(40), 8));
    const auto others = static_cast<std::uint8_t>(~weak);
    const std::uint8_t absorbed = nthBit(others, scaled(drawAfter(40, 2), 7));
    const auto run = checked(s, kMonster);
    const battle::Battler& monster = run.after.state.at(kMonster);
    EXPECT_EQ(monster.elementsWeak.bits, weak);
    EXPECT_EQ(monster.elementsNullified.bits, others);
    EXPECT_EQ(monster.elementsAbsorbed.bits, absorbed);
    EXPECT_EQ(monster.elementsHalved.bits, 0x11);
}

TEST_F(TargetEffectTest, EscapeTakesAPartyMemberOutOfTheBattle) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::ESCAPE), kHero, kHero), kHero);
    EXPECT_TRUE(run.after.state.leavingAfterAction.has(kHero));
    EXPECT_TRUE(run.after.state.partyLeft.has(kHero));
}

TEST_F(TargetEffectTest, AMonsterThatEscapesHasFallen) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::ESCAPE), kMonster, kMonster), kMonster);
    EXPECT_TRUE(run.after.state.leavingAfterAction.has(kMonster));
    EXPECT_TRUE(run.after.state.monstersFallen.has(kMonster));
    EXPECT_TRUE(run.after.state.partyLeft.empty());
}

TEST_F(TargetEffectTest, SneezeAndASmokeBombTakeTheirTargetOutOfTheBattle) {
    for (const SpecialEffect effect :
         {SpecialEffect::of(AttackSpecialEffect::SNEEZE), SpecialEffect::of(ItemUseEffect::SMOKE_BOMB)}) {
        const auto run = checked(scenarioWith(effect, kMonster, kAlly), kAlly);
        EXPECT_TRUE(run.after.state.partyLeft.has(kAlly));
    }
}

TEST_F(TargetEffectTest, MindBlastGivesOneStatusForEachPickThatNamedTheTarget) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::MIND_BLAST), kMonster, kHero);
    s.attack.record.statuses = StatusSet::of(StatusId::BLIND, StatusId::SILENCE, StatusId::SLOW);
    s.attack.scratch.mindBlastPicks = {kHero, kAlly, kHero, std::nullopt};
    s.state.at(kHero).statusesToSet = StatusSet::of(StatusId::POISON);
    s.state.at(kHero).statusesToClear = StatusSet::of(StatusId::HASTE);
    s.state.rng = battle::BattleRng{70};
    const auto run = checked(s, kHero);
    const battle::Battler& hero = run.after.state.at(kHero);
    EXPECT_EQ(hero.statusesToClear.bytes, StatusSet{}.bytes);
    EXPECT_FALSE(hero.statusesToSet.has(StatusId::POISON));
    const int given = std::popcount(static_cast<unsigned>(hero.statusesToSet.bytes[0] | hero.statusesToSet.bytes[1] << 8 |
                                                           hero.statusesToSet.bytes[2] << 16));
    EXPECT_GE(given, 1);
    EXPECT_LE(given, 2);
    EXPECT_EQ(run.after.state.rng.position(), 74);  // two picks, two draws each
}

TEST_F(TargetEffectTest, EvilTootEmptiesTheWaitingStatusesAndGivesOne) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::EVIL_TOOT));
    s.attack.record.statuses = StatusSet::of(StatusId::IMP);
    s.state.at(kMonster).statusesToSet = StatusSet::of(StatusId::POISON);
    const auto run = checked(s, kMonster);
    EXPECT_EQ(run.after.state.at(kMonster).statusesToSet.bytes, StatusSet::of(StatusId::IMP).bytes);
}

TEST_F(TargetEffectTest, RPolarityTurnsTheRowOver) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::R_POLARITY), kHero, kAlly);
    EXPECT_TRUE(checked(s, kAlly).after.state.at(kAlly).backRow);
    s.state.at(kAlly).backRow = true;
    EXPECT_FALSE(checked(s, kAlly).after.state.at(kAlly).backRow);
}

TEST_F(TargetEffectTest, LoveTokenMakesTheTargetTakeTheCastersHits) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::LOVE_TOKEN), kHero, kAlly), kAlly);
    EXPECT_EQ(run.after.state.at(kHero).bodyguard, kAlly);
    EXPECT_EQ(run.after.state.at(kAlly).guarding, kHero);
}

TEST_F(TargetEffectTest, SeizeTakesHoldOfAPartyMember) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::SEIZE), kMonster, kHero);
    s.state.at(kMonster).aiVariable = 0x05;
    s.state.at(kHero).menuMayOpen = true;
    const auto run = checked(s, kHero);
    EXPECT_FALSE(run.missed);
    EXPECT_EQ(run.after.state.at(kMonster).seizing, kHero);
    EXPECT_EQ(run.after.state.at(kHero).seizedBy, kMonster);
    EXPECT_EQ(run.after.state.at(kMonster).aiVariable, 0x85);
    EXPECT_FALSE(run.after.state.at(kHero).menuMayOpen);
    EXPECT_TRUE(run.after.state.at(kHero).pendingUpdates.removeAllActions);
}

TEST_F(TargetEffectTest, SeizeMissesAMonsterAndAnyoneWhileItHoldsSomeone) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::SEIZE), kMonster, kOtherMonster);
    EXPECT_TRUE(checked(s, kOtherMonster).missed);
    s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::SEIZE), kMonster, kHero);
    s.state.at(kMonster).seizing = kAlly;
    EXPECT_TRUE(checked(s, kHero).missed);
}

TEST_F(TargetEffectTest, DiscardLetsGoOfTheOneHeld) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::DISCARD), kMonster, kHero);
    s.state.at(kMonster).seizing = kHero;
    s.state.at(kMonster).aiVariable = 0x85;
    s.state.at(kHero).seizedBy = kMonster;
    const auto run = checked(s, kHero);
    EXPECT_FALSE(run.after.state.at(kMonster).seizing);
    EXPECT_FALSE(run.after.state.at(kHero).seizedBy);
    EXPECT_EQ(run.after.state.at(kMonster).aiVariable, 0x05);
}

TEST_F(TargetEffectTest, TargettingAimsTheMonstersNextAttack) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::TARGETTING), kMonster, kAlly), kAlly);
    EXPECT_EQ(run.after.state.at(kMonster).targetingTarget, kAlly);
}

TEST_F(TargetEffectTest, SuplexMissesAMonsterThatCannotBeThrown) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::SUPLEX));
    EXPECT_FALSE(checked(s, kMonster).missed);
    s.state.at(kMonster).monsterFlags.set(MonsterBattleFlag::CANT_SUPLEX);
    EXPECT_TRUE(checked(s, kMonster).missed);
}

TEST_F(TargetEffectTest, BababreathBlowsAPartyMemberAwayFromTheParty) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::BABABREATH), kMonster, kAlly), kAlly);
    EXPECT_TRUE(run.after.state.takenFromTheParty.has(kAlly));
    EXPECT_TRUE(run.after.state.partyLeft.has(kAlly));
    EXPECT_TRUE(run.after.state.leavingAfterAction.has(kAlly));
}

TEST_F(TargetEffectTest, CharmTakesOneBattlerAtATime) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::CHARM), kMonster, kHero);
    auto run = checked(s, kHero);
    EXPECT_EQ(run.after.state.at(kMonster).charming, kHero);
    EXPECT_EQ(run.after.state.at(kHero).charmedBy, kMonster);
    s.state.at(kMonster).charming = kAlly;
    run = checked(s, kHero);
    EXPECT_TRUE(run.missed);
    EXPECT_FALSE(run.after.state.at(kHero).charmedBy);
}

TEST_F(TargetEffectTest, DoomLeavesTheLivingAlone) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::DOOM)), kMonster);
    EXPECT_TRUE(run.after.state.immediateActions.empty());
    EXPECT_FALSE(run.missed);
}

TEST_F(TargetEffectTest, DoomOnAnUndeadPartyMemberCancelsItsDeathAndFillsItsHp) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::DOOM), kMonster, kHero);
    s.state.at(kHero).undead = true;
    s.state.at(kHero).statusesToSet = StatusSet::of(StatusId::DEAD, StatusId::POISON);
    const auto run = checked(s, kHero);
    EXPECT_EQ(run.after.state.at(kHero).statusesToSet.bytes, StatusSet::of(StatusId::POISON).bytes);
    EXPECT_EQ(run.after.state.at(kHero).hp, 2000);
}

TEST_F(TargetEffectTest, DoomOnAnUndeadMonsterHidesItAndBringsItBackWhole) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::DOOM));
    s.state.at(kMonster).undead = true;
    const auto run = checked(s, kMonster);
    EXPECT_TRUE(run.after.state.at(kMonster).statusesToSet.has(StatusId::HIDE));
    EXPECT_EQ(run.after.attack.targets, BattlerSet::of(kMonster));
    ASSERT_EQ(run.after.state.immediateActions.size(), 1u);
}

TEST_F(TargetEffectTest, OvercastAndPhantasmMarkTheTarget) {
    EXPECT_TRUE(checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::OVERCAST)), kMonster)
                    .after.state.at(kMonster)
                    .triggers.overcast);
    EXPECT_TRUE(checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::PHANTASM)), kMonster)
                    .after.state.at(kMonster)
                    .triggers.phantasm);
}

TEST_F(TargetEffectTest, EngulfSwallowsAPartyMember) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::ENGULF), kMonster, kAlly), kAlly);
    EXPECT_TRUE(run.after.state.engulfed.has(kAlly));
    EXPECT_TRUE(run.after.state.partyLeft.has(kAlly));
}

TEST_F(TargetEffectTest, ZingerTakesTheMonsterOffTheFieldWithItsTarget) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::ZINGER), kMonster, kAlly), kAlly);
    EXPECT_EQ(run.after.state.heldByZinger, kMonster);
    EXPECT_EQ(run.after.state.swallowedByZinger, kAlly);
    EXPECT_TRUE(run.after.state.leavingAfterAction.has(kMonster));
    EXPECT_FALSE(run.after.state.leavingAfterAction.has(kAlly));
}

TEST_F(TargetEffectTest, StunnersStatusesFailOnADrawAtOrAboveItsHitRate) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::STUNNER));
    s.attack.record.statuses = StatusSet::of(StatusId::STOP);
    s.attack.record.hitRate = 0x80;
    s.state.at(kMonster).statusesToSet = StatusSet::of(StatusId::STOP, StatusId::POISON);
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r >= 0x80; })};
    EXPECT_EQ(checked(s, kMonster).after.state.at(kMonster).statusesToSet.bytes, StatusSet::of(StatusId::POISON).bytes);
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < 0x80; })};
    EXPECT_TRUE(checked(s, kMonster).after.state.at(kMonster).statusesToSet.has(StatusId::STOP));
}

TEST_F(TargetEffectTest, FallenOneLeavesOneHp) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::FALLEN_ONE)), kMonster);
    EXPECT_EQ(run.after.state.at(kMonster).hp, 1);
}

TEST_F(TargetEffectTest, ReflectLoresRowHereDoesNothing) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::REFLECT_LORE)), kMonster);
    EXPECT_FALSE(run.missed);
}

// --- the items -------------------------------------------------------------------------------------

TEST_F(TargetEffectTest, ASuperBallsHitDoesItsDrawTimes256) {
    DamageScenario s = scenarioWith(SpecialEffect::of(ItemUseEffect::SUPER_BALL));
    s.state.rng = battle::BattleRng{90};
    EXPECT_EQ(checked(s, kMonster).after.attack.damage, ((drawAfter(90) & 7) + 1) * 256);
}

TEST_F(TargetEffectTest, AnElixirFillsTheMpAndTheSpellsAreWorkedOutAgain) {
    const auto run = checked(scenarioWith(SpecialEffect::of(ItemUseEffect::ELIXIR), kHero, kAlly), kAlly);
    EXPECT_EQ(run.after.state.at(kAlly).mp, 300);
    EXPECT_TRUE(run.after.state.at(kAlly).pendingUpdates.enabledMagic);
}

// --- the commands ----------------------------------------------------------------------------------

TEST_F(TargetEffectTest, PossessTakesThePossessorAndThePossessedAndMarksThePossessorToAnswer) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Possess));
    const auto run = checked(s, kMonster);
    const battle::BattleState& state = run.after.state;
    EXPECT_TRUE(state.leavingAfterAction.has(kHero));
    EXPECT_TRUE(state.takenFromTheParty.has(kHero));
    for (const BattlerSlot slot : {kHero, kMonster}) {
        EXPECT_TRUE(state.at(slot).statusesToSet.has(StatusId::DEAD));
        EXPECT_TRUE(state.at(slot).statusesToSet.has(StatusId::HIDE));
    }
    EXPECT_EQ(state.at(kHero).lastAttacker, kMonster);
    EXPECT_TRUE(state.at(kHero).retaliationPending);
}

TEST_F(TargetEffectTest, StealingFromATargetWithNothingSaysSoAndIsRememberedAsAFight) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal));
    s.state.at(kMonster).lastHit.command->with = BattleCommandId::STEAL;
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.missed);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageDoesntHaveAnything);
    EXPECT_EQ(run.after.state.at(kMonster).lastHit.command->with, BattleCommandId::FIGHT);
}

TEST_F(TargetEffectTest, AThiefFarAboveTheTargetStealsTheCommonItemOnADrawOf32OrMore) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal));
    s.state.at(kHero).level = 99;
    s.state.at(kMonster).level = 20;
    s.state.at(kMonster).rareSteal = ItemId::ELIXIR;
    s.state.at(kMonster).commonSteal = ItemId::POTION;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r >= 0x20; })};
    const auto run = checked(s, kMonster);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageStole);
    EXPECT_EQ(run.after.attack.scratch.messageItem, ItemId::POTION);
    EXPECT_EQ(run.after.state.at(kHero).itemInHand, ItemId::POTION);
    EXPECT_TRUE(run.after.state.holdingItem.has(kHero));
    EXPECT_EQ(run.after.state.at(kMonster).rareSteal, ItemId::EMPTY);
    EXPECT_EQ(run.after.state.at(kMonster).commonSteal, ItemId::EMPTY);
}

TEST_F(TargetEffectTest, TheRareItemIsStolenOnADrawBelow32) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal));
    s.state.at(kHero).level = 99;
    s.state.at(kMonster).level = 20;
    s.state.at(kMonster).rareSteal = ItemId::ELIXIR;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < 0x20; })};
    EXPECT_EQ(checked(s, kMonster).after.state.at(kHero).itemInHand, ItemId::ELIXIR);
}

TEST_F(TargetEffectTest, DrawingAnEmptySlotIsAFailedSteal) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal));
    s.state.at(kHero).level = 99;
    s.state.at(kMonster).level = 20;
    s.state.at(kMonster).rareSteal = ItemId::ELIXIR;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r >= 0x20; })};
    const auto run = checked(s, kMonster);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageCouldntSteal);
    EXPECT_EQ(run.after.state.at(kMonster).rareSteal, ItemId::ELIXIR);
    EXPECT_FALSE(run.after.state.at(kHero).itemInHand);
}

TEST_F(TargetEffectTest, AThiefMoreThan50LevelsBelowNeverStealsAndDrawsNothing) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal));
    s.state.at(kHero).level = 10;
    s.state.at(kMonster).level = 61;
    s.state.at(kMonster).commonSteal = ItemId::POTION;
    const std::uint8_t before = s.state.rng.position();
    const auto run = checked(s, kMonster);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageCouldntSteal);
    EXPECT_EQ(run.after.state.rng.position(), before);
}

TEST_F(TargetEffectTest, TheStealChanceIs50PlusTheLevelGapOutOf100AndASneakRingDoublesIt) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal));
    s.state.at(kHero).level = 20;
    s.state.at(kMonster).level = 40;  // a chance of 30
    s.state.at(kMonster).commonSteal = ItemId::POTION;
    // A percentage roll of 30 to 59 fails without the ring and succeeds with it.
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t roll, std::uint8_t slot) {
        return scaled(roll, 100) >= 30 && scaled(roll, 100) < 60 && slot >= 0x20;
    })};
    EXPECT_EQ(checked(s, kMonster).after.attack.scratch.message, battle::kMessageCouldntSteal);
    s.state.at(kHero).relicEffects3.set(RelicEffect3::RAISE_STEAL_RATE);
    EXPECT_EQ(checked(s, kMonster).after.attack.scratch.message, battle::kMessageStole);
}

TEST_F(TargetEffectTest, AMonsterStealsItsLevelTimes256Plus20Gil) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal), kMonster, kHero);
    s.state.at(kMonster).level = 12;
    s.state.at(kMonster).gold = 100;
    s.state.party.gil = 50000;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < 0xC0; })};
    const auto run = checked(s, kHero);
    const std::uint32_t taken = 12 * 256 + 20;
    EXPECT_EQ(run.after.state.party.gil, 50000 - taken);
    EXPECT_EQ(run.after.state.at(kMonster).gold, 100 + taken);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageGpStolen);
    EXPECT_EQ(run.after.attack.scratch.messageAmount, taken);
}

TEST_F(TargetEffectTest, AMonstersStealFailsOnADrawOf192OrMore) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal), kMonster, kHero);
    s.state.party.gil = 50000;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r >= 0xC0; })};
    const auto run = checked(s, kHero);
    EXPECT_EQ(run.after.state.party.gil, 50000u);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageCouldntSteal);
    EXPECT_EQ(run.after.state.at(kHero).lastHit.command->with, BattleCommandId::FIGHT);
}

TEST_F(TargetEffectTest, AMonsterFindsAPartyWithNoGilHasNothing) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal), kMonster, kHero);
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < 0xC0; })};
    EXPECT_EQ(checked(s, kHero).after.attack.scratch.message, battle::kMessageDoesntHaveAnything);
}

TEST_F(TargetEffectTest, AMonstersGoldStopsAt65535) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal), kMonster, kHero);
    s.state.at(kMonster).gold = 65000;
    s.state.party.gil = 50000;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < 0xC0; })};
    EXPECT_EQ(checked(s, kHero).after.state.at(kMonster).gold, 65535);
}

TEST_F(TargetEffectTest, ControlTakesHoldOfAMonsterAtTheCastersLevel) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Control));
    const auto run = checked(s, kMonster);
    const battle::Battler& hero = run.after.state.at(kHero);
    const battle::Battler& monster = run.after.state.at(kMonster);
    EXPECT_FALSE(run.missed);
    EXPECT_EQ(hero.controlling, kMonster);
    EXPECT_EQ(monster.controlledBy, kHero);
    EXPECT_TRUE(run.after.state.flippedByControl.has(kMonster));
    EXPECT_TRUE(hero.triggers.usesControlMenu);
    EXPECT_TRUE(hero.status.has(StatusId::CHANT));
    EXPECT_FALSE(hero.statusesToSet.has(StatusId::CHANT));
    EXPECT_TRUE(monster.emptyGaugeAfterAction);
    EXPECT_EQ(hero.controlMenu[0].attack, getMonsterControl(MonsterId::SOLDIER).attacks[0]);
}

TEST_F(TargetEffectTest, ControlRefusesAPartyMemberAMonsterThatCannotBeControlledAndOneInTheWrongState) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Control), kHero, kAlly);
    auto run = checked(s, kAlly);
    EXPECT_TRUE(run.missed);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageCantControl);
    s = scenarioWith(SpecialEffect::of(CommandEffect::Control));
    s.state.at(kMonster).monsterFlags.set(MonsterBattleFlag::CANT_CONTROL);
    EXPECT_EQ(checked(s, kMonster).after.attack.scratch.message, battle::kMessageCantControl);
    for (const StatusId status :
         {StatusId::DEAD, StatusId::INVISIBLE, StatusId::BERSERK, StatusId::HIDE, StatusId::MORPH}) {
        s = scenarioWith(SpecialEffect::of(CommandEffect::Control));
        s.state.at(kMonster).status.set(status);
        EXPECT_TRUE(checked(s, kMonster).missed);
    }
}

TEST_F(TargetEffectTest, ControlRefusesAMonsterAlreadyHeldButNotOneWhoseLinkIsEnding) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Control));
    s.state.at(kMonster).controlledBy = kAlly;
    s.state.at(kAlly).controlling = kMonster;
    EXPECT_EQ(checked(s, kMonster).after.attack.scratch.message, battle::kMessageCantControl);
    s.state.at(kMonster).controlEnding = true;
    s.state.at(kAlly).controlEnding = true;
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.missed);
    EXPECT_EQ(run.after.state.at(kMonster).controlledBy, kHero);
    EXPECT_FALSE(run.after.state.at(kMonster).controlEnding);
}

TEST_F(TargetEffectTest, AControlThatFailsItsLevelCheckMissesWithNoMessage) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Control));
    s.state.at(kMonster).level = 60;  // a quotient of 128
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r >= 128; })};
    const auto run = checked(s, kMonster);
    EXPECT_TRUE(run.missed);
    EXPECT_FALSE(run.after.attack.scratch.message);
}

TEST_F(TargetEffectTest, ACoronetTakesTheTargetsLevelDownToTwoThirds) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Control));
    s.state.at(kMonster).level = 60;  // 60 * 170 / 256 = 39: a quotient of 196
    s.state.at(kHero).relicEffects3.set(RelicEffect3::RAISE_CONTROL_RATE);
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r >= 128 && r < 196; })};
    EXPECT_FALSE(checked(s, kMonster).missed);
}

TEST_F(TargetEffectTest, LeapEndsTheBattleWithGauHidden) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Leap));
    const auto run = checked(s, kMonster);
    EXPECT_TRUE(run.after.state.at(kHero).statusesToSet.has(StatusId::HIDE));
    EXPECT_EQ(run.after.state.endOfBattleEvent, battle::EndOfBattleEvent::GauLeapt);
}

TEST_F(TargetEffectTest, LeapCannotDiveAlone) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Leap));
    s.state.allyCount = 1;
    const auto run = checked(s, kMonster);
    EXPECT_TRUE(run.missed);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageCantDive);
    EXPECT_FALSE(run.after.state.endOfBattleEvent);
}

// A formation can forbid Leap, but none of the game's 576 does: the refusal is never reached.
TEST(TargetEffectCorpus, NoFormationForbidsLeap) {
    int forbidding = 0;
    for (const FormationAuxEntry& entry : formationAux()) {
        forbidding += entry.record.leapDisabled() ? 1 : 0;
    }
    EXPECT_EQ(formationAux().size(), 576u);
    EXPECT_EQ(forbidding, 0);
}

TEST_F(TargetEffectTest, SketchCatchesTheMonstersSecondAttackOnADrawOf64OrMore) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Sketch));
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r >= 0x40; })};
    const auto run = checked(s, kMonster);
    ASSERT_TRUE(run.after.attack.scratch.sketch);
    EXPECT_EQ(run.after.attack.scratch.sketch->monster, kMonster);
    EXPECT_EQ(run.after.attack.scratch.followUpSpell, getMonsterSketch(MonsterId::SOLDIER).attacks[1]);
    EXPECT_EQ(run.after.attack.secondary, kMonster.position());
    EXPECT_NE(std::get_if<battle::AttackAnimation>(&run.script.commands().front()), nullptr);
}

TEST_F(TargetEffectTest, SketchCatchesTheFirstAttackOnADrawBelow64) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Sketch));
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < 0x40; })};
    EXPECT_EQ(checked(s, kMonster).after.attack.scratch.followUpSpell, getMonsterSketch(MonsterId::SOLDIER).attacks[0]);
}

TEST_F(TargetEffectTest, SketchMissesAPartyMemberSilentlyAndSaysSoForAMonsterItCannotSketch) {
    auto run = checked(scenarioWith(SpecialEffect::of(CommandEffect::Sketch), kHero, kAlly), kAlly);
    EXPECT_TRUE(run.missed);
    EXPECT_FALSE(run.after.attack.scratch.message);
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Sketch));
    s.state.at(kMonster).monsterFlags.set(MonsterBattleFlag::CANT_SKETCH);
    run = checked(s, kMonster);
    EXPECT_TRUE(run.missed);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageCantSketch);
}

TEST_F(TargetEffectTest, DebilitatorMakesTheTargetWeakToOneElementItIsNotYet) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Debilitator));
    battle::Battler& monster = s.state.at(kMonster);
    monster.elementsWeak.bits = 0x01;
    monster.elementsHalved.bits = 0xFF;
    monster.elementsAbsorbed.bits = 0xFF;
    monster.elementsNullified.bits = 0xFF;
    s.state.elementsNullifiedByForcefield.bits = 0x02;
    s.state.rng = battle::BattleRng{120};
    const std::uint8_t element = nthBit(0xFC, scaled(drawAfter(120), 6));
    const auto run = checked(s, kMonster);
    const battle::Battler& after = run.after.state.at(kMonster);
    EXPECT_EQ(after.elementsWeak.bits, 0x01 | element);
    EXPECT_EQ(after.elementsHalved.bits, static_cast<std::uint8_t>(~element));
    EXPECT_EQ(after.elementsAbsorbed.bits, static_cast<std::uint8_t>(~element));
    EXPECT_EQ(after.elementsNullified.bits, static_cast<std::uint8_t>(~element));
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageNowWeakAgainst + std::countr_zero(element));
}

TEST_F(TargetEffectTest, DebilitatorMissesATargetWeakToEverything) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Debilitator));
    s.state.at(kMonster).elementsWeak.bits = 0xFF;
    EXPECT_TRUE(checked(s, kMonster).missed);
}

TEST_F(TargetEffectTest, AirAnchorMarksATargetInstantDeathCanReach) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::AirAnchor));
    auto run = checked(s, kMonster);
    EXPECT_TRUE(run.after.state.at(kMonster).airAnchored);
    EXPECT_EQ(run.after.attack.scratch.message, battle::kMessageMoveAndYoureDust);
    EXPECT_FALSE(run.after.state.scratch.countersEnabled);
    s.state.at(kMonster).instantDeathProtected = true;
    run = checked(s, kMonster);
    EXPECT_TRUE(run.missed);
    EXPECT_FALSE(run.after.attack.scratch.message);
    EXPECT_TRUE(run.after.state.scratch.countersEnabled);
}

// --- the effects by a monster, and on a monster ------------------------------------------------------

TEST_F(TargetEffectTest, AMonstersPepUpLeavesTheFieldToo) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::PEP_UP), kMonster, kOtherMonster),
                             kOtherMonster);
    EXPECT_TRUE(run.after.state.leavingAfterAction.has(kMonster));
    EXPECT_EQ(run.after.state.at(kOtherMonster).mp, 300);
}

TEST_F(TargetEffectTest, AMonsterPossessingIsNotTakenFromTheParty) {
    const auto run = checked(scenarioWith(SpecialEffect::of(CommandEffect::Possess), kMonster, kHero), kHero);
    EXPECT_TRUE(run.after.state.leavingAfterAction.empty());
    EXPECT_TRUE(run.after.state.takenFromTheParty.empty());
    EXPECT_TRUE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
}

TEST_F(TargetEffectTest, APossessorAlreadyWaitingToAnswerSomeoneElseChangesItsMarkOnAnOddDraw) {
    for (const bool odd : {true, false}) {
        DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Possess));
        s.state.at(kHero).lastAttacker = kOtherMonster;
        s.state.at(kHero).retaliationPending = true;
        s.state.rng = battle::BattleRng{positionWhere([odd](std::uint8_t r) { return ((r & 1) != 0) == odd; })};
        const auto run = checked(s, kMonster);
        EXPECT_EQ(run.after.state.at(kHero).lastAttacker, odd ? kMonster : kOtherMonster);
    }
}

TEST_F(TargetEffectTest, AMonstersMetamorphLeavesNoPartyMemberHoldingTheItem) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::METAMORPH), kOtherMonster, kMonster);
    s.state.at(kMonster).metamorph = MetamorphInfo{.packed = 0x02};
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t, std::uint8_t) { return true; })};
    const auto run = checked(s, kMonster);
    EXPECT_TRUE(run.after.state.holdingItem.empty());
}

TEST_F(TargetEffectTest, EngulfAndBababreathOnAMonsterOnlyTakeItOffTheField) {
    for (const SpecialEffect effect :
         {SpecialEffect::of(AttackSpecialEffect::ENGULF), SpecialEffect::of(AttackSpecialEffect::BABABREATH)}) {
        const auto run = checked(scenarioWith(effect, kHero, kMonster), kMonster);
        EXPECT_TRUE(run.after.state.engulfed.empty());
        EXPECT_TRUE(run.after.state.takenFromTheParty.empty());
        EXPECT_TRUE(run.after.state.monstersFallen.has(kMonster));
    }
}

TEST_F(TargetEffectTest, APartyMembersZingerLeavesNoOne) {
    const auto run = checked(scenarioWith(SpecialEffect::of(AttackSpecialEffect::ZINGER), kHero, kAlly), kAlly);
    EXPECT_EQ(run.after.state.heldByZinger, kHero);
    EXPECT_TRUE(run.after.state.leavingAfterAction.empty());
}

TEST_F(TargetEffectTest, AMonstersExploderOnItselfLeavesItsTargetsAlone) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::EXPLODER), kMonster, kMonster);
    s.attack.params.targetsHit = BattlerSet::of(kHero);
    const auto run = checked(s, kMonster);
    EXPECT_EQ(run.after.attack.params.targetsHit, BattlerSet::of(kHero));
}

TEST_F(TargetEffectTest, AMonsterStealingMoreThanThePartyHasTakesAllOfIt) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal), kMonster, kHero);
    s.state.at(kMonster).level = 40;
    s.state.party.gil = 1234;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r < 0xC0; })};
    const auto run = checked(s, kHero);
    EXPECT_EQ(run.after.state.party.gil, 0u);
    EXPECT_EQ(run.after.attack.scratch.messageAmount, 1234u);
}

TEST_F(TargetEffectTest, AThiefOfTheTargetsLevelStealsOnARollBelow50) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Steal));
    s.state.at(kMonster).commonSteal = ItemId::POTION;
    s.state.rng = battle::BattleRng{
        positionWhere([](std::uint8_t roll, std::uint8_t slot) { return scaled(roll, 100) == 49 && slot >= 0x20; })};
    EXPECT_EQ(checked(s, kMonster).after.attack.scratch.message, battle::kMessageStole);
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t roll) { return scaled(roll, 100) == 50; })};
    EXPECT_EQ(checked(s, kMonster).after.attack.scratch.message, battle::kMessageCouldntSteal);
}

TEST_F(TargetEffectTest, ABeretTakesTheTargetsLevelDownForSketch) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Sketch));
    s.state.at(kMonster).level = 60;
    s.state.rng = battle::BattleRng{positionWhere([](std::uint8_t r) { return r >= 128 && r < 196; })};
    EXPECT_TRUE(checked(s, kMonster).missed);
    s.state.at(kHero).relicEffects3.set(RelicEffect3::RAISE_SKETCH_RATE);
    EXPECT_FALSE(checked(s, kMonster).missed);
}

TEST_F(TargetEffectTest, ALevelCheckAgainstALevelOfZeroAlwaysSucceeds) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Sketch));
    s.state.at(kMonster).level = 1;
    s.state.at(kHero).relicEffects3.set(RelicEffect3::RAISE_SKETCH_RATE);  // 1 * 170 / 256 = 0
    s.state.at(kHero).level = 1;
    const std::uint8_t before = s.state.rng.position();
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.missed);
    EXPECT_EQ(run.after.state.rng.position(), static_cast<std::uint8_t>(before + 1));  // the slot's draw only
}

TEST_F(TargetEffectTest, AnAttackWithNoStatusesGivesMindBlastsTargetNone) {
    DamageScenario s = scenarioWith(SpecialEffect::of(AttackSpecialEffect::MIND_BLAST), kMonster, kHero);
    s.attack.scratch.mindBlastPicks = {kHero, std::nullopt, std::nullopt, std::nullopt};
    const auto run = checked(s, kHero);
    EXPECT_EQ(run.after.state.at(kHero).statusesToSet.bytes, StatusSet{}.bytes);
}

TEST_F(TargetEffectTest, DebilitatorPassesOverAnElementForcefieldNullified) {
    DamageScenario s = scenarioWith(SpecialEffect::of(CommandEffect::Debilitator));
    s.state.at(kMonster).elementsWeak.bits = 0x7F;
    s.state.elementsNullifiedByForcefield.bits = 0x80;
    EXPECT_TRUE(checked(s, kMonster).missed);
}

TEST_F(TargetEffectTest, AnXWeaponOnAnUndeadMonsterProtectedFromInstantDeathStillFadesItOut) {
    DamageScenario s = scenarioWith(SpecialEffect::of(WeaponSpecialEffect::INSTANT_KILL));
    s.state.at(kMonster).undead = true;
    s.state.at(kMonster).instantDeathProtected = true;
    const auto run = checked(s, kMonster);
    EXPECT_FALSE(run.after.state.at(kMonster).statusesToSet.has(StatusId::DEAD));
    EXPECT_EQ(run.after.state.immediateActions.size(), 1u);
}

// --- who carries each effect ------------------------------------------------------------------------

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

TEST(TargetEffectCarriers, EveryEffectOnATargetHasACarrier) {
    const std::set<std::uint8_t> carried = carriedEffects();
    const std::vector<std::uint8_t> rows{
        0x03, 0x04, 0x08, 0x0D, 0x10, 0x12, 0x13, 0x17, 0x19, 0x1C, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x25,
        0x26, 0x27, 0x28, 0x2B, 0x2D, 0x2E, 0x2F, 0x30, 0x33, 0x34, 0x35, 0x37, 0x38, 0x39, 0x3A, 0x3B,
        0x3E, 0x3F, 0x40, 0x44, 0x4A, 0x4B, 0x4C, 0x50, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    };
    for (const std::uint8_t row : rows) {
        EXPECT_TRUE(carried.contains(row)) << "effect " << int{row};
    }
}

constexpr int kSweepDraws = 500;

TEST_F(TargetEffectTest, EveryDrawnScenarioLeavesTheCellsAsTheCartridgeDoes) {
    std::mt19937 rng{trace::kSweepSeed};
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const trace::TargetScenario drawn = trace::drawTargetEffectScenario(rng);
            const TargetEffectResult port =
                trace::targetEffectResult(trace::runTargetEffect(drawn.scenario, drawn.target));
            const TargetEffectResult guest = oracle.targetEffect(drawn.scenario, drawn.target);
            EXPECT_EQ(trace::differences(port, guest), "")
                << "draw " << draw << ", effect " << int{drawn.scenario.attack.effect.index()} << ", attacker "
                << drawn.scenario.attacker.index() << ", target " << drawn.target.index();
        }
    });
}

}  // namespace
}  // namespace ostinato::test
