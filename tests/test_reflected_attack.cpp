// The spell after the attack for the battlers it bounced off, or a Launcher's or Super Ball's hits:
// how each bounce is aimed, checked and worked out, who is its attacker, what the script shows, and
// the damage taken and shown. Each case is worked out three ways — the port, by hand from the
// original, and the cartridge's own routine through the damage oracle — and every other cell the
// routine writes is compared between the port and the cartridge as well. A seeded sweep then draws
// scenarios at random and checks the port against the cartridge alone.
//
// Authority: docs/contracts/battle_damage.md.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "assets/cartridge.h"
#include "battle/attack.h"
#include "battle/damage.h"
#include "damage_oracle.h"
#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::DamageScenario;
using trace::ReflectedAttackResult;

// The cartridge, read once for the binary; every test starts its own oracle on it.
class ReflectedAttackTest : public ::testing::Test {
public:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* ReflectedAttackTest::image_ = nullptr;

// Run `body` with a started oracle, from a tick of the run loop.
template <typename Body>
void withOracle(Body body) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{ReflectedAttackTest::image()};
        oracle.start();
        body(oracle);
    });
}

// The three-way check: the port's answer, the value worked out by hand, and the cartridge's.
#define EXPECT_THREE_WAYS(port, hand, guest)                                                     \
    do {                                                                                        \
        const auto port_ = (port);                                                              \
        const auto hand_ = (hand);                                                              \
        const auto guest_ = (guest);                                                            \
        EXPECT_EQ(port_, hand_) << "the port disagrees with the hand trace";                    \
        EXPECT_EQ(guest_, hand_) << "the cartridge disagrees with the hand trace";               \
        EXPECT_EQ(port_, guest_) << "the port disagrees with the cartridge";                     \
    } while (0)

constexpr BattlerSlot kHero = BattlerSlot::character(0);
constexpr BattlerSlot kAlly = BattlerSlot::character(1);
constexpr BattlerSlot kFoe = BattlerSlot::monster(0);
constexpr BattlerSlot kOtherFoe = BattlerSlot::monster(1);

std::size_t at(BattlerSlot slot) {
    return static_cast<std::size_t>(slot.index());
}

// `caster`'s Fire with 1,000 damage worked out, taken as it is and never dodged, bounced off
// `reflectors`. On the field: the hero and the ally, the two foes; every battler at 5,000 HP.
DamageScenario bounce(BattlerSlot caster, battle::BattlerSet reflectors) {
    DamageScenario s;
    s.attacker = caster;
    s.attack.command = BattleCommandId::MAGIC;
    s.attack.queuedCommand = BattleCommandId::MAGIC;
    s.attack.attack = AttackId::FIRE;
    s.attack.record.power = 10;
    s.attack.record.hitRate = 0xFF;
    s.attack.damage = 1000;
    s.attack.scratch.damageModification = false;
    s.attack.scratch.targetCount = 1;
    s.attack.params.reflectedOff = reflectors;
    s.state.targetableOrPresent = battle::BattlerSet::allCharacters() | battle::BattlerSet::allMonsters();
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        battle::Battler& battler = s.state.battlers[static_cast<std::size_t>(index)];
        battler.hp = 5000;
        battler.maxHp = 5000;
    }
    for (const BattlerSlot slot : {kHero, kAlly, kFoe, kOtherFoe}) {
        s.state.at(slot).present = true;
    }
    return s;
}

// The bounce worked out by the port and by the cartridge. Every cell either side writes must
// agree.
struct Both {
    ReflectedAttackResult port;
    ReflectedAttackResult guest;
};

Both reflect(trace::DamageOracle& oracle, const DamageScenario& s) {
    Both both{trace::portReflectedAttack(s), oracle.reflectedAttack(s)};
    EXPECT_EQ(trace::differences(both.port, both.guest), "");
    return both;
}

#define EXPECT_SHOWN(both, hand) EXPECT_THREE_WAYS((both).port.shownAs, std::string{hand}, (both).guest.shownAs)
#define EXPECT_HP(both, slot, hand) \
    EXPECT_THREE_WAYS((both).port.applied.hp[at(slot)], hand, (both).guest.applied.hp[at(slot)])

// ─── the bounce ─────────────────────────────────────────────────────────────────────────────────

TEST_F(ReflectedAttackTest, Bounce_IsHalvedAndGoesToTheOtherSideFromTheBattlerItBouncedOff) {
    withOracle([](trace::DamageOracle& oracle) {
        // The hero's Fire bounced off the foe: the foe sends it to the party, where only the ally is
        // still standing, so it hits the ally for half of 1,000.
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        const Both both = reflect(oracle, s);
        EXPECT_THREE_WAYS(both.port.damage, 500, both.guest.damage);
        EXPECT_SHOWN(both, "bounced attack 0: 4>1 hit 256 blocks 0 0 0 0");
        EXPECT_HP(both, kAlly, 4500);
        EXPECT_HP(both, kFoe, 5000);
    });
}

TEST_F(ReflectedAttackTest, Bounce_TheBattlerItBouncedOffIsTheAttackerOfRecordTheCasterTheDamager) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        const Both both = reflect(oracle, s);
        constexpr std::uint8_t kWaiting = 0x80;
        // The hit is recorded by the foe and the ally waits to answer the foe; the HP it lost it
        // remembers losing to the hero, who cast the spell.
        EXPECT_THREE_WAYS(both.port.lastHitBy[at(kAlly)], std::optional<BattlerSlot>{kFoe},
                          both.guest.lastHitBy[at(kAlly)]);
        EXPECT_THREE_WAYS(both.port.applied.lastAttackerMark[at(kAlly)],
                          static_cast<std::uint8_t>(kWaiting | kFoe.index()),
                          both.guest.applied.lastAttackerMark[at(kAlly)]);
        EXPECT_THREE_WAYS(both.port.applied.lastDamagedBy[at(kAlly)], std::optional<BattlerSlot>{kHero},
                          both.guest.applied.lastDamagedBy[at(kAlly)]);
    });
}

TEST_F(ReflectedAttackTest, TwoBounces_LastSlotFirstAndTheirDamageAddsUp) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe) | battle::BattlerSet::of(kOtherFoe));
        s.state.at(kHero).present = false;
        const Both both = reflect(oracle, s);
        EXPECT_SHOWN(both, "bounced attack 0: 4>1 5>1 hit 768 blocks 0 0 0 0");
        EXPECT_HP(both, kAlly, 4000);
        EXPECT_THREE_WAYS(trace::differences(both.port.applied, both.guest.applied), std::string{}, std::string{});
    });
}

TEST_F(ReflectedAttackTest, BounceThatMisses_ShowsMissAndHitsNoOne) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.attack.record.hitRate = 0;
        const Both both = reflect(oracle, s);
        EXPECT_SHOWN(both, "bounced attack 0: 4>1 hit 0 blocks 0 0 0 0");
        EXPECT_HP(both, kAlly, 5000);
        ASSERT_EQ(both.port.applied.shown.size(), 1u);
        EXPECT_THREE_WAYS(both.port.applied.shown[0].numbers[0].miss, true, both.guest.applied.shown[0].numbers[0].miss);
    });
}

TEST_F(ReflectedAttackTest, BounceAPartyMemberBlocks_ShowsTheBlock) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.attack.record.hitRate = 0;
        s.state.at(kAlly).magicBlockGraphics.add(BlockGraphic::SHIELD);
        const Both both = reflect(oracle, s);
        EXPECT_SHOWN(both, "bounced attack 0: 4>1 hit 0 blocks 0 3 0 0");
    });
}

TEST_F(ReflectedAttackTest, NoOneToBounceTo_ShowsNoBounceAndDoesNothing) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.state.at(kAlly).present = false;
        const Both both = reflect(oracle, s);
        EXPECT_SHOWN(both, "bounced attack 0: hit 0 blocks 0 0 0 0");
        EXPECT_THREE_WAYS(both.port.applied.shown.size(), std::size_t{0}, both.guest.applied.shown.size());
    });
}

TEST_F(ReflectedAttackTest, BounceAimedAtTheOtherSideOnly_GoesBackToTheCasterOnHalfTheDraws) {
    withOracle([](trace::DamageOracle& oracle) {
        // Bounced off the foe with the hero still standing, a spell aimed at the other side only goes
        // to the hero on tails and to either party member on heads. Over every random-number
        // position the port and the cartridge send it to the same battler, and both outcomes happen.
        int toTheCaster = 0;
        int toTheAlly = 0;
        for (int position = 0; position < 256; ++position) {
            DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
            s.attack.rules.opposingSideOnly = true;
            s.state.rng = battle::BattleRng{static_cast<std::uint8_t>(position)};
            const Both both = reflect(oracle, s);
            toTheCaster += both.guest.applied.hp[at(kHero)] < 5000 ? 1 : 0;
            toTheAlly += both.guest.applied.hp[at(kAlly)] < 5000 ? 1 : 0;
        }
        EXPECT_GT(toTheCaster, toTheAlly);
        EXPECT_GT(toTheAlly, 0);
        EXPECT_EQ(toTheCaster + toTheAlly, 256);
    });
}

// ─── a Launcher's or Super Ball's hits ──────────────────────────────────────────────────────────

TEST_F(ReflectedAttackTest, ScatteredHits_EachOnARandomFoeTheirDamageAddingUp) {
    withOracle([](trace::DamageOracle& oracle) {
        // Three hits from the hero, with only one foe on the field: each is half of 1,000.
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kHero));
        s.state.extraHits = 3;
        s.state.at(kOtherFoe).present = false;
        const Both both = reflect(oracle, s);
        EXPECT_SHOWN(both, "scattered attack 0 from 0: 4 4 4");
        EXPECT_HP(both, kFoe, 3500);
        EXPECT_THREE_WAYS(both.port.extraHits, std::optional<int>{}, both.guest.extraHits);
        EXPECT_THREE_WAYS(both.port.lastHitBy[at(kFoe)], std::optional<BattlerSlot>{kHero},
                          both.guest.lastHitBy[at(kFoe)]);
    });
}

TEST_F(ReflectedAttackTest, ScatteredHitsWithNoOneToHit_KeepTheirCount) {
    withOracle([](trace::DamageOracle& oracle) {
        // The count is the battle's: the hits not made stay for whatever attack comes next.
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kHero));
        s.state.extraHits = 8;
        s.state.at(kFoe).present = false;
        s.state.at(kOtherFoe).present = false;
        const Both both = reflect(oracle, s);
        EXPECT_SHOWN(both, "scattered attack 0 from 0:");
        EXPECT_THREE_WAYS(both.port.extraHits, std::optional<int>{8}, both.guest.extraHits);
    });
}

TEST_F(ReflectedAttackTest, ScatteredHits_MissedOnesStillFly) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kHero));
        s.state.extraHits = 2;
        s.attack.record.hitRate = 0;
        s.state.at(kOtherFoe).present = false;
        const Both both = reflect(oracle, s);
        EXPECT_SHOWN(both, "scattered attack 0 from 0: 4 4");
        EXPECT_HP(both, kFoe, 5000);
    });
}

TEST_F(ReflectedAttackTest, HitsLeftStanding_MakeASpellsBounceScattered) {
    withOracle([](trace::DamageOracle& oracle) {
        // With two hits left standing, Fire bounced off the foe goes without the reflected rule: the
        // foe aims both hits at the party, where only the ally stands, each for half of 1,000, and
        // they are shown as scattered hits from the hero, who cast it.
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.state.extraHits = 2;
        const Both both = reflect(oracle, s);
        EXPECT_SHOWN(both, "scattered attack 0 from 0: 1 1");
        EXPECT_HP(both, kAlly, 4000);
        EXPECT_THREE_WAYS(both.port.extraHits, std::optional<int>{}, both.guest.extraHits);
    });
}

// ─── who a bounce can reach ─────────────────────────────────────────────────────────────────────

TEST_F(ReflectedAttackTest, ABattlerAwayFromTheField_IsNotReachedWhenCounterattacksAreOff) {
    withOracle([](trace::DamageOracle& oracle) {
        // The reach check reads the cell an attack aimed at its own attacker clears, not the
        // counterattack cell: with counterattacks off, the ally away from the field is still out of
        // reach, and the bounce has no one to go to.
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.state.awayFromTheField.add(kAlly);
        s.state.scratch.countersEnabled = false;
        const Both both = reflect(oracle, s);
        EXPECT_SHOWN(both, "bounced attack 0: hit 0 blocks 0 0 0 0");
        EXPECT_HP(both, kAlly, 5000);
    });
}

TEST_F(ReflectedAttackTest, ABattlerAwayFromTheField_IsReachedAfterASelfAimedAttack) {
    withOracle([](trace::DamageOracle& oracle) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.state.awayFromTheField.add(kAlly);
        s.attack.scratch.awayFromTheFieldUnreachable = false;
        const Both both = reflect(oracle, s);
        EXPECT_SHOWN(both, "bounced attack 0: 4>1 hit 256 blocks 0 0 0 0");
        EXPECT_HP(both, kAlly, 4500);
    });
}

// ─── in the attack frame ────────────────────────────────────────────────────────────────────────

// Records which targets the special effect was worked out for; it misses the ones `misses` names.
struct EffectLog : battle::AttackHooks {
    std::vector<BattlerSlot> effects;
    battle::BattlerSet misses;
    std::vector<BattlerSlot> damaged;
    bool targetEffect(battle::BattleState&, battle::AttackInFlight&, BattlerSlot, BattlerSlot target,
                      battle::BattleScriptBuilder&) override {
        effects.push_back(target);
        return misses.has(target);
    }
    void targetDamage(battle::BattleState&, battle::AttackInFlight&, BattlerSlot, BattlerSlot target,
                      battle::BattleScriptBuilder&) override {
        damaged.push_back(target);
    }
};

TEST_F(ReflectedAttackTest, ABounceTheSpecialEffectMisses_DoesNoDamageAndDoesNotCountAsAHit) {
    DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
    s.state.at(kHero).present = false;
    EffectLog hooks;
    hooks.misses.add(kAlly);
    battle::BattleScriptBuilder script;
    battle::resolveReflectedAttack(s.state, hooks, script, s.attack, s.attacker);
    EXPECT_EQ(hooks.effects, std::vector<BattlerSlot>{kAlly});
    EXPECT_TRUE(hooks.damaged.empty());
    const auto* shown = std::get_if<battle::ReflectedAttackShown>(&script.commands().front());
    ASSERT_NE(shown, nullptr);
    EXPECT_TRUE(shown->bouncesThatHit.empty());
}

// ─── the sweep ───────────────────────────────────────────────────────────────────────────────────

constexpr int kSweepDraws = 500;

TEST_F(ReflectedAttackTest, ReflectedAttackSweep_EveryCellMatchesTheCartridge) {
    withOracle([](trace::DamageOracle& oracle) {
        std::mt19937 rng{trace::kSweepSeed};
        for (int index = 0; index < kSweepDraws; ++index) {
            const DamageScenario drawn = trace::drawReflectScenario(rng);
            const ReflectedAttackResult port = trace::portReflectedAttack(drawn);
            const ReflectedAttackResult guest = oracle.reflectedAttack(drawn);
            ASSERT_EQ(trace::differences(port, guest), "") << "draw " << index;
        }
    });
}

}  // namespace
}  // namespace ostinato::test
