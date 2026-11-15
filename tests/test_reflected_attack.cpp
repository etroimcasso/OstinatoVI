// The spell after the attack for the battlers it bounced off, or a Launcher's or Super Ball's hits:
// how each bounce is aimed, checked and worked out, who is its attacker, what the script shows, and
// the damage taken and shown. Each case is worked out three ways — the port, by hand from the
// original, and the cartridge's own routine through the damage oracle where the run asks it
// (tests/with_cartridge.h) — and every other cell the routine writes is compared between the port and
// the cartridge as well. A bounce aimed at the other side over every random-number position, and the
// seeded sweep, are in tests/oracle/test_reflected_attack.cpp.
//
// Authority: docs/contracts/battle_damage.md.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <type_traits>
#include <vector>

#include "reflected_attack_cases.h"
#include "battle/attack.h"
#include "battle/damage.h"
#include "oracle/damage_oracle.h"
#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::DamageScenario;
using trace::ReflectedAttackResult;

// The three-way check: the port's answer, the value worked out by hand, and the cartridge's where the
// run asks it (an empty optional where it does not).
#define EXPECT_THREE_WAYS(port, hand, guest)                                                     \
    do {                                                                                        \
        const auto port_ = (port);                                                              \
        const auto hand_ = (hand);                                                              \
        const auto guest_ = (guest);                                                            \
        EXPECT_EQ(port_, hand_) << "the port disagrees with the hand trace";                    \
        if (guest_) {                                                                           \
            EXPECT_EQ(*guest_, hand_) << "the cartridge disagrees with the hand trace";          \
            EXPECT_EQ(port_, *guest_) << "the port disagrees with the cartridge";                \
        }                                                                                       \
    } while (0)

// A value of the cartridge's answer, read by `read`, where the run asked it.
template <typename Read>
auto fromTheCartridge(const Both& both, Read read) -> std::optional<std::decay_t<decltype(read(both.port))>> {
    if (!both.guest) {
        return std::nullopt;
    }
    return read(*both.guest);
}

// The cartridge's `member`, where the run asked it.
#define CARTRIDGE_VALUE(both, member) fromTheCartridge((both), [&](const ReflectedAttackResult& r) { return r.member; })

// What the damage taken and shown differs in between the port and the cartridge, where the run asked
// the cartridge; nothing where it did not.
std::string appliedApart(const Both& both) {
    return both.guest ? trace::differences(both.port.applied, both.guest->applied) : std::string{};
}

#define EXPECT_SHOWN(both, hand) EXPECT_THREE_WAYS((both).port.shownAs, std::string{hand}, CARTRIDGE_VALUE(both, shownAs))
#define EXPECT_HP(both, slot, hand) \
    EXPECT_THREE_WAYS((both).port.applied.hp[at(slot)], hand, CARTRIDGE_VALUE(both, applied.hp[at(slot)]))

// ─── the bounce ─────────────────────────────────────────────────────────────────────────────────

TEST_F(ReflectedAttackTest, Bounce_IsHalvedAndGoesToTheOtherSideFromTheBattlerItBouncedOff) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        // The hero's Fire bounced off the foe: the foe sends it to the party, where only the ally is
        // still standing, so it hits the ally for half of 1,000.
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        const Both both = reflect(cartridge, s);
        EXPECT_THREE_WAYS(both.port.damage, 500, CARTRIDGE_VALUE(both, damage));
        EXPECT_SHOWN(both, "bounced attack 0: 4>1 hit 256 blocks 0 0 0 0");
        EXPECT_HP(both, kAlly, 4500);
        EXPECT_HP(both, kFoe, 5000);
    });
}

TEST_F(ReflectedAttackTest, Bounce_TheBattlerItBouncedOffIsTheAttackerOfRecordTheCasterTheDamager) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        const Both both = reflect(cartridge, s);
        constexpr std::uint8_t kWaiting = 0x80;
        // The hit is recorded by the foe and the ally waits to answer the foe; the HP it lost it
        // remembers losing to the hero, who cast the spell.
        EXPECT_THREE_WAYS(both.port.lastHitBy[at(kAlly)], std::optional<BattlerSlot>{kFoe},
                          CARTRIDGE_VALUE(both, lastHitBy[at(kAlly)]));
        EXPECT_THREE_WAYS(both.port.applied.lastAttackerMark[at(kAlly)],
                          static_cast<std::uint8_t>(kWaiting | kFoe.index()),
                          CARTRIDGE_VALUE(both, applied.lastAttackerMark[at(kAlly)]));
        EXPECT_THREE_WAYS(both.port.applied.lastDamagedBy[at(kAlly)], std::optional<BattlerSlot>{kHero},
                          CARTRIDGE_VALUE(both, applied.lastDamagedBy[at(kAlly)]));
    });
}

TEST_F(ReflectedAttackTest, TwoBounces_LastSlotFirstAndTheirDamageAddsUp) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe) | battle::BattlerSet::of(kOtherFoe));
        s.state.at(kHero).present = false;
        const Both both = reflect(cartridge, s);
        EXPECT_SHOWN(both, "bounced attack 0: 4>1 5>1 hit 768 blocks 0 0 0 0");
        EXPECT_HP(both, kAlly, 4000);
        EXPECT_EQ(appliedApart(both), "");
    });
}

TEST_F(ReflectedAttackTest, BounceThatMisses_ShowsMissAndHitsNoOne) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.attack.record.hitRate = 0;
        const Both both = reflect(cartridge, s);
        EXPECT_SHOWN(both, "bounced attack 0: 4>1 hit 0 blocks 0 0 0 0");
        EXPECT_HP(both, kAlly, 5000);
        ASSERT_EQ(both.port.applied.shown.size(), 1u);
        EXPECT_THREE_WAYS(both.port.applied.shown[0].numbers[0].miss, true, CARTRIDGE_VALUE(both, applied.shown[0].numbers[0].miss));
    });
}

TEST_F(ReflectedAttackTest, BounceAPartyMemberBlocks_ShowsTheBlock) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.attack.record.hitRate = 0;
        s.state.at(kAlly).magicBlockGraphics.add(BlockGraphic::SHIELD);
        const Both both = reflect(cartridge, s);
        EXPECT_SHOWN(both, "bounced attack 0: 4>1 hit 0 blocks 0 3 0 0");
    });
}

TEST_F(ReflectedAttackTest, NoOneToBounceTo_ShowsNoBounceAndDoesNothing) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.state.at(kAlly).present = false;
        const Both both = reflect(cartridge, s);
        EXPECT_SHOWN(both, "bounced attack 0: hit 0 blocks 0 0 0 0");
        EXPECT_THREE_WAYS(both.port.applied.shown.size(), std::size_t{0}, CARTRIDGE_VALUE(both, applied.shown.size()));
    });
}

// ─── a Launcher's or Super Ball's hits ──────────────────────────────────────────────────────────

TEST_F(ReflectedAttackTest, ScatteredHits_EachOnARandomFoeTheirDamageAddingUp) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        // Three hits from the hero, with only one foe on the field: each is half of 1,000.
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kHero));
        s.state.extraHits = 3;
        s.state.at(kOtherFoe).present = false;
        const Both both = reflect(cartridge, s);
        EXPECT_SHOWN(both, "scattered attack 0 from 0: 4 4 4");
        EXPECT_HP(both, kFoe, 3500);
        EXPECT_THREE_WAYS(both.port.extraHits, std::optional<int>{}, CARTRIDGE_VALUE(both, extraHits));
        EXPECT_THREE_WAYS(both.port.lastHitBy[at(kFoe)], std::optional<BattlerSlot>{kHero},
                          CARTRIDGE_VALUE(both, lastHitBy[at(kFoe)]));
    });
}

TEST_F(ReflectedAttackTest, ScatteredHitsWithNoOneToHit_KeepTheirCount) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        // The count is the battle's: the hits not made stay for whatever attack comes next.
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kHero));
        s.state.extraHits = 8;
        s.state.at(kFoe).present = false;
        s.state.at(kOtherFoe).present = false;
        const Both both = reflect(cartridge, s);
        EXPECT_SHOWN(both, "scattered attack 0 from 0:");
        EXPECT_THREE_WAYS(both.port.extraHits, std::optional<int>{8}, CARTRIDGE_VALUE(both, extraHits));
    });
}

TEST_F(ReflectedAttackTest, ScatteredHits_MissedOnesStillFly) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kHero));
        s.state.extraHits = 2;
        s.attack.record.hitRate = 0;
        s.state.at(kOtherFoe).present = false;
        const Both both = reflect(cartridge, s);
        EXPECT_SHOWN(both, "scattered attack 0 from 0: 4 4");
        EXPECT_HP(both, kFoe, 5000);
    });
}

TEST_F(ReflectedAttackTest, HitsLeftStanding_MakeASpellsBounceScattered) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        // With two hits left standing, Fire bounced off the foe goes without the reflected rule: the
        // foe aims both hits at the party, where only the ally stands, each for half of 1,000, and
        // they are shown as scattered hits from the hero, who cast it.
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.state.extraHits = 2;
        const Both both = reflect(cartridge, s);
        EXPECT_SHOWN(both, "scattered attack 0 from 0: 1 1");
        EXPECT_HP(both, kAlly, 4000);
        EXPECT_THREE_WAYS(both.port.extraHits, std::optional<int>{}, CARTRIDGE_VALUE(both, extraHits));
    });
}

// ─── who a bounce can reach ─────────────────────────────────────────────────────────────────────

TEST_F(ReflectedAttackTest, ABattlerAwayFromTheField_IsNotReachedWhenCounterattacksAreOff) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        // The reach check reads the cell an attack aimed at its own attacker clears, not the
        // counterattack cell: with counterattacks off, the ally away from the field is still out of
        // reach, and the bounce has no one to go to.
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.state.awayFromTheField.add(kAlly);
        s.state.scratch.countersEnabled = false;
        const Both both = reflect(cartridge, s);
        EXPECT_SHOWN(both, "bounced attack 0: hit 0 blocks 0 0 0 0");
        EXPECT_HP(both, kAlly, 5000);
    });
}

TEST_F(ReflectedAttackTest, ABattlerAwayFromTheField_IsReachedAfterASelfAimedAttack) {
    withCartridge(ReflectedAttackTest::image(), [](const Cartridge& cartridge) {
        DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
        s.state.at(kHero).present = false;
        s.state.awayFromTheField.add(kAlly);
        s.attack.scratch.awayFromTheFieldUnreachable = false;
        const Both both = reflect(cartridge, s);
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

}  // namespace
}  // namespace ostinato::test
