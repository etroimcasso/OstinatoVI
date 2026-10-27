// The attack an item makes when it is used as itself: a consumable, a thrown weapon, a tool.
// Behaviors and their sources: docs/contracts/battle_commands.md § Items.

#include <gtest/gtest.h>

#include <cstdint>

#include "battle/attack.h"
#include "battle/commands.h"
#include "battle/item_attack.h"
#include "battle_test_support.h"
#include "data/item_properties.h"
#include "ostinato/attack_flags.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_type.h"

namespace ostinato::test {
namespace {

using battle::AttackInFlight;
using battle::BattlerSlot;
using battle::BattleState;
using battle::ItemUse;
using battle::SpecialEffect;

const BattlerSlot kEdgar = BattlerSlot::character(0);

BattleState toolBattle() {
    BattleState state = makeAimableBattle();
    state.at(kEdgar).vigor = 70;
    state.at(kEdgar).level = 21;
    return state;
}

AttackInFlight itemAttackOf(BattleState& state, ItemId item, ItemUse use) {
    AttackInFlight attack;
    battle::itemAttack(state, attack, kEdgar, item, use);
    return attack;
}

// --- consumables -----------------------------------------------------------------------------

TEST(ItemAttack, AConsumableIsNotPhysicalAndCannotBeDodgedOrReflected) {
    BattleState state = toolBattle();
    const AttackInFlight potion = itemAttackOf(state, ItemId::POTION, ItemUse::Consumed);
    EXPECT_FALSE(potion.record.traits.has(AttackTrait::PHYSICAL));
    EXPECT_TRUE(potion.record.traits.has(AttackTrait::IGNORE_DEFENSE));
    EXPECT_TRUE(potion.record.flags1.has(AttackFlag1::IGNORE_REFLECT));
    EXPECT_TRUE(potion.record.flags1.has(AttackFlag1::RETARGET_IF_INVALID));
    EXPECT_TRUE(potion.record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_EQ(potion.record.power, 250);
    EXPECT_EQ(potion.attackerStat, 0);
    EXPECT_EQ(potion.attackerLevel, 0);
    EXPECT_TRUE(potion.effect.isNone());
}

TEST(ItemAttack, APotionRestoresHpAndHarmsTheUndead) {
    BattleState state = toolBattle();
    const AttackInFlight potion = itemAttackOf(state, ItemId::POTION, ItemUse::Consumed);
    EXPECT_TRUE(potion.record.flags2.has(AttackFlag2::RESTORE_HP_MP));
    EXPECT_FALSE(potion.record.flags1.has(AttackFlag1::AFFECT_MP));
    EXPECT_TRUE(potion.record.traits.has(AttackTrait::INVERT_ON_UNDEAD));
    EXPECT_FALSE(potion.record.flags2.has(AttackFlag2::FRACTIONAL_DAMAGE));
}

TEST(ItemAttack, RestoringMpAlsoRestores) {
    BattleState state = toolBattle();
    const AttackInFlight ether = itemAttackOf(state, ItemId::ETHER, ItemUse::Consumed);
    EXPECT_TRUE(ether.record.flags1.has(AttackFlag1::AFFECT_MP));
    EXPECT_TRUE(ether.record.flags2.has(AttackFlag2::RESTORE_HP_MP));
}

TEST(ItemAttack, AnItemThatWorksOnTheDeadRaisesThemAndHarmsTheUndead) {
    BattleState state = toolBattle();
    const AttackInFlight fenix = itemAttackOf(state, ItemId::FENIX_DOWN, ItemUse::Consumed);
    EXPECT_TRUE(fenix.record.statuses.has(StatusId::DEAD));
    EXPECT_TRUE(fenix.record.traits.has(AttackTrait::RESURRECTION_TARGET));
    EXPECT_TRUE(fenix.record.traits.has(AttackTrait::INVERT_ON_UNDEAD));
    EXPECT_TRUE(fenix.record.flags2.has(AttackFlag2::REMOVE_STATUS));
    EXPECT_TRUE(fenix.record.flags2.has(AttackFlag2::FRACTIONAL_DAMAGE));
}

TEST(ItemAttack, AConsumablesUseEffectIsOffsetIntoTheEffectSpace) {
    BattleState state = toolBattle();
    const AttackInFlight elixir = itemAttackOf(state, ItemId::ELIXIR, ItemUse::Consumed);
    EXPECT_EQ(elixir.effect.index(), 0x48 + static_cast<int>(ItemUseEffect::ELIXIR));
}

// Every consumable, read bit by bit from its record's +19 byte as the original's shift chain reads
// it (battle_main.asm:7035-7059): bit 7 fractional, bit 5 removes status, bit 4 restores MP (and so
// restores), bit 3 restores HP, bit 1 inverts on the undead; and a Dead status raises.
TEST(ItemAttack, EveryConsumableTakesItsHealingFromItsRecord) {
    BattleState state = toolBattle();
    int consumables = 0;
    for (const ItemPropertiesEntry& entry : itemPropertiesEn()) {
        if (entry.record.typeAndUsage.type() != ItemType::CONSUMABLE) {
            continue;
        }
        ++consumables;
        const std::uint8_t byte = entry.record.weaponFlags.bits;
        const AttackInFlight attack = itemAttackOf(state, entry.id, ItemUse::Consumed);
        const AttackProperties& record = attack.record;
        SCOPED_TRACE(static_cast<int>(entry.id));
        EXPECT_EQ(record.flags2.has(AttackFlag2::FRACTIONAL_DAMAGE), (byte & 0x80) != 0);
        EXPECT_EQ(record.flags2.has(AttackFlag2::REMOVE_STATUS), (byte & 0x20) != 0);
        EXPECT_EQ(record.flags1.has(AttackFlag1::AFFECT_MP), (byte & 0x10) != 0);
        EXPECT_EQ(record.flags2.has(AttackFlag2::RESTORE_HP_MP), (byte & 0x18) != 0);
        const bool raises = record.statuses.has(StatusId::DEAD);
        EXPECT_EQ(record.traits.has(AttackTrait::INVERT_ON_UNDEAD), (byte & 0x02) != 0 || raises);
        EXPECT_EQ(record.traits.has(AttackTrait::RESURRECTION_TARGET), raises);
        EXPECT_EQ(record.statuses.bytes, battle::itemStatuses(entry.record).bytes);
        EXPECT_EQ(record.power, entry.record.power);
        EXPECT_EQ(attack.effect.isNone(), entry.record.specialEffect.itemUseDisabled());
    }
    EXPECT_GT(consumables, 20);
}

// --- thrown weapons --------------------------------------------------------------------------

TEST(ItemAttack, AThrownWeaponIsAPhysicalBlowAtItsBattlePower) {
    BattleState state = toolBattle();
    const AttackInFlight dirk = itemAttackOf(state, ItemId::DIRK, ItemUse::ThrownOrWorked);
    EXPECT_TRUE(dirk.record.traits.has(AttackTrait::PHYSICAL));
    EXPECT_TRUE(dirk.record.traits.has(AttackTrait::IGNORE_DEFENSE));
    EXPECT_TRUE(dirk.record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_EQ(dirk.record.power, 26);
    EXPECT_EQ(dirk.attackerStat, 70);
    EXPECT_EQ(dirk.attackerLevel, 21);
    EXPECT_EQ(dirk.record.statuses.bytes, StatusSet{}.bytes);
    EXPECT_TRUE(dirk.effect.isNone());
}

TEST(ItemAttack, TheInvizAndShadowEdgesAddVanishAndImage) {
    BattleState state = toolBattle();
    EXPECT_EQ(itemAttackOf(state, ItemId::INVIZ_EDGE, ItemUse::ThrownOrWorked).record.statuses.bytes,
              StatusSet::of(StatusId::INVISIBLE).bytes);
    EXPECT_EQ(itemAttackOf(state, ItemId::SHADOW_EDGE, ItemUse::ThrownOrWorked).record.statuses.bytes,
              StatusSet::of(StatusId::IMAGE).bytes);
}

// --- tools -----------------------------------------------------------------------------------

TEST(ItemAttack, AToolCanBeDodgedAndHitsAtItsOwnRate) {
    BattleState state = toolBattle();
    const AttackInFlight noise = itemAttackOf(state, ItemId::NOISEBLASTER, ItemUse::ThrownOrWorked);
    EXPECT_FALSE(noise.record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_FALSE(noise.record.traits.has(AttackTrait::IGNORE_DEFENSE));
    EXPECT_TRUE(noise.record.traits.has(AttackTrait::PHYSICAL));
    EXPECT_EQ(noise.record.hitRate, 255);
    EXPECT_EQ(noise.record.statuses.bytes, StatusSet::of(StatusId::CONFUSE).bytes);
}

TEST(ItemAttack, EachToolAddsItsOwnEffect) {
    BattleState state = toolBattle();
    EXPECT_TRUE(itemAttackOf(state, ItemId::DRILL, ItemUse::ThrownOrWorked)
                    .record.traits.has(AttackTrait::IGNORE_DEFENSE));
    EXPECT_EQ(itemAttackOf(state, ItemId::DEBILITATOR, ItemUse::ThrownOrWorked).effect,
              SpecialEffect::of(battle::CommandEffect::Debilitator));
    EXPECT_EQ(itemAttackOf(state, ItemId::AIR_ANCHOR, ItemUse::ThrownOrWorked).effect,
              SpecialEffect::of(battle::CommandEffect::AirAnchor));
    EXPECT_TRUE(itemAttackOf(state, ItemId::AUTOCROSSBOW, ItemUse::ThrownOrWorked)
                    .record.traits.has(AttackTrait::NO_DAMAGE_SPLIT));
    const AttackInFlight drill = itemAttackOf(state, ItemId::DRILL, ItemUse::ThrownOrWorked);
    EXPECT_FALSE(drill.record.traits.has(AttackTrait::NO_DAMAGE_SPLIT));
    EXPECT_TRUE(drill.effect.isNone());
}

TEST(ItemAttack, TheChainSawKillsOneTimeInFour) {
    BattleState state = toolBattle();
    rngWhere(state, {[](std::uint8_t r) { return (r & 0x03) == 0; }});
    const AttackInFlight kill = itemAttackOf(state, ItemId::CHAIN_SAW, ItemUse::ThrownOrWorked);
    EXPECT_TRUE(kill.chainSawKill);
    EXPECT_EQ(kill.record.power, 0);
    EXPECT_EQ(kill.record.statuses.bytes, StatusSet::of(StatusId::DEAD).bytes);
    EXPECT_EQ(kill.record.flags2.bits, AttackFlags2::of(AttackFlag2::STAMINA_DEFENSE).bits);
    EXPECT_TRUE(kill.record.traits.has(AttackTrait::INSTANT_DEATH));
    EXPECT_TRUE(kill.record.traits.has(AttackTrait::IGNORE_DEFENSE));

    rngWhere(state, {[](std::uint8_t r) { return (r & 0x03) == 2; }});
    const AttackInFlight cut = itemAttackOf(state, ItemId::CHAIN_SAW, ItemUse::ThrownOrWorked);
    EXPECT_FALSE(cut.chainSawKill);
    EXPECT_NE(cut.record.power, 0);
    EXPECT_FALSE(cut.record.traits.has(AttackTrait::INSTANT_DEATH));
    EXPECT_TRUE(cut.record.traits.has(AttackTrait::IGNORE_DEFENSE));
}

TEST(ItemAttack, EveryToolIsOneOfTheEight) {
    int tools = 0;
    for (const ItemPropertiesEntry& entry : itemPropertiesEn()) {
        if (entry.record.typeAndUsage.type() != ItemType::TOOL) {
            continue;
        }
        ++tools;
        EXPECT_GE(entry.id, ItemId::NOISEBLASTER);
        EXPECT_LE(entry.id, ItemId::AUTOCROSSBOW);
    }
    EXPECT_EQ(tools, 8);
}

}  // namespace
}  // namespace ostinato::test
