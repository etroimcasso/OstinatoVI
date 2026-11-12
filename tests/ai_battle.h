// The battle the AI's target and condition tests read, and the oracle run beside the port.
//
// Test code. One fixed battle with a battler of every kind a selector tells apart:
//
//   party slot 1    Terra, on the field                                alive
//   party slot 2    Locke, on the field, Dead
//   party slot 3    Edgar, on the field, Reflect, held in Seize by monster slot 2   alive
//   party slot 4    empty
//   monster slot 1  Lobo, on the field — the monster whose script runs      alive
//   monster slot 2  Lobo, on the field, Reflect                        alive
//   monster slot 3  Lobo, on the field, Dead
//   monster slot 4  Lobo, off the field, Hide
//   monster slot 5  empty
//   monster slot 6  Lobo, on the field and away from it (jumping)      alive
//
// The script's monster was last aimed at by Terra, and its Targetting mark is on Edgar. The battlers an
// attack can reach are Terra and monster slots 1 and 2.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "assets/cartridge.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "damage_oracle.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"
#include "vanilla_rom.h"

namespace ostinato::test {

inline constexpr battle::BattlerSlot kTerra = battle::BattlerSlot::character(0);
inline constexpr battle::BattlerSlot kLocke = battle::BattlerSlot::character(1);
inline constexpr battle::BattlerSlot kEdgar = battle::BattlerSlot::character(2);
inline constexpr battle::BattlerSlot kEmptyPartySlot = battle::BattlerSlot::character(3);
inline constexpr battle::BattlerSlot kSelf = battle::BattlerSlot::monster(0);
inline constexpr battle::BattlerSlot kReflecting = battle::BattlerSlot::monster(1);
inline constexpr battle::BattlerSlot kFallen = battle::BattlerSlot::monster(2);
inline constexpr battle::BattlerSlot kHidden = battle::BattlerSlot::monster(3);
inline constexpr battle::BattlerSlot kEmptyMonsterSlot = battle::BattlerSlot::monster(4);
inline constexpr battle::BattlerSlot kJumping = battle::BattlerSlot::monster(5);

template <typename... Slots>
constexpr battle::BattlerSet setOf(Slots... slots) {
    battle::BattlerSet set;
    (set.add(slots), ...);
    return set;
}

inline trace::AiScenario aiBattle() {
    trace::AiScenario s;
    battle::BattleState& state = s.state;
    s.actor = kSelf;
    const auto partyMember = [&](battle::BattlerSlot slot, CharacterPropId who) {
        battle::Battler& battler = state.at(slot);
        battler.present = true;
        battler.actor = who;
        battler.level = 20;
        battler.hp = 500;
        battler.mp = 40;
    };
    partyMember(kTerra, CharacterPropId::TERRA);
    partyMember(kLocke, CharacterPropId::LOCKE);
    partyMember(kEdgar, CharacterPropId::EDGAR);
    state.at(kLocke).status.set(StatusId::DEAD);
    state.at(kEdgar).status.set(StatusId::REFLECT);
    state.at(kEdgar).seizedBy = kReflecting;
    for (const battle::BattlerSlot slot : {kSelf, kReflecting, kFallen, kHidden, kJumping}) {
        battle::Battler& battler = state.at(slot);
        battler.present = slot != kHidden;
        battler.monster = MonsterId::LOBO;
        battler.level = 10;
        battler.hp = 1000;
        battler.mp = 10;
    }
    state.at(kReflecting).status.set(StatusId::REFLECT);
    state.at(kFallen).status.set(StatusId::DEAD);
    state.at(kHidden).status.set(StatusId::HIDE);
    state.awayFromTheField = setOf(kJumping);
    state.targetableOrPresent = setOf(kTerra, kLocke, kEdgar, kSelf, kReflecting, kFallen, kJumping);
    state.alive = setOf(kTerra, kEdgar, kSelf, kReflecting, kJumping);
    state.allyCount = 2;
    state.enemyCount = 3;
    state.at(kSelf).lastAttacker = kTerra;
    state.at(kSelf).targetingTarget = kEdgar;
    state.rng = battle::BattleRng(40);
    return s;
}

// The cartridge, hosted once for every test that runs the oracle.
inline std::span<const std::uint8_t> aiCartridge() {
    static const std::vector<std::uint8_t> image = vanillaRom();
    return assets::stripCopierHeader(image);
}

// `body(oracle)` with the oracle started on the cartridge.
template <typename Body>
void withAiOracle(Body body) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{aiCartridge()};
        oracle.start();
        body(oracle);
    });
}

}  // namespace ostinato::test
