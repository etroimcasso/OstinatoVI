// What the tests of the spell after the attack share: the cartridge they read, the battle a bounce is
// made in, and the bounce worked out by the port and the cartridge. Included by
// tests/test_reflected_attack.cpp and tests/oracle/test_reflected_attack.cpp.
//
// Test code. ReflectedAttackTest reads the cartridge FF6_VANILLA_ROM names once for the test binary.
#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "assets/cartridge.h"
#include "oracle/damage_oracle.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "vanilla_rom.h"
#include "with_cartridge.h"

namespace ostinato::test {

class ReflectedAttackTest : public ::testing::Test {
public:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    inline static std::vector<std::uint8_t>* image_ = nullptr;
};

inline constexpr battle::BattlerSlot kHero = battle::BattlerSlot::character(0);
inline constexpr battle::BattlerSlot kAlly = battle::BattlerSlot::character(1);
inline constexpr battle::BattlerSlot kFoe = battle::BattlerSlot::monster(0);
inline constexpr battle::BattlerSlot kOtherFoe = battle::BattlerSlot::monster(1);

inline std::size_t at(battle::BattlerSlot slot) {
    return static_cast<std::size_t>(slot.index());
}

// `caster`'s Fire with 1,000 damage worked out, taken as it is and never dodged, bounced off
// `reflectors`. On the field: the hero and the ally, the two foes; every battler at 5,000 HP.
inline trace::DamageScenario bounce(battle::BattlerSlot caster, battle::BattlerSet reflectors) {
    trace::DamageScenario s;
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
    for (const battle::BattlerSlot slot : {kHero, kAlly, kFoe, kOtherFoe}) {
        s.state.at(slot).present = true;
    }
    return s;
}

// The bounce worked out by the port, and by the cartridge where the run asks it. Every cell either
// side writes must agree.
struct Both {
    trace::ReflectedAttackResult port;
    std::optional<trace::ReflectedAttackResult> guest;
};

// The bounce worked out by the port over `s`, and by the cartridge where the run asks it.
inline Both reflect(const Cartridge& cartridge, const trace::DamageScenario& s) {
    Both both{trace::portReflectedAttack(s), std::nullopt};
    if (cartridge) {
        both.guest = cartridge.oracle().reflectedAttack(s);
        EXPECT_EQ(trace::differences(both.port, *both.guest), "");
    }
    return both;
}

}  // namespace ostinato::test
