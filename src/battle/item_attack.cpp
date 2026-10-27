#include "battle/item_attack.h"

#include <cstdint>

#include "battle/commands.h"
#include "data/attack_properties.h"
#include "data/item_properties.h"
#include "ostinato/attack_effects.h"
#include "ostinato/attack_flags.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_type.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

namespace ostinato::battle {

namespace {

// Chain Saw kills outright one time in four: a draw whose low two bits are clear.
constexpr std::uint8_t kChainSawKillBits = 0x03;

// What a tool adds to the attack it makes. Bio Blaster and Flash cast their spells and never come
// here.
void addToolEffect(BattleState& state, AttackInFlight& attack, ItemId tool) {
    AttackProperties& record = attack.record;
    switch (tool) {
    case ItemId::NOISEBLASTER:
        record.statuses.set(StatusId::CONFUSE);
        break;
    case ItemId::CHAIN_SAW:
        if ((state.rng.next() & kChainSawKillBits) == 0) {
            attack.chainSawKill = true;
            record.power = 0;
            record.statuses.set(StatusId::DEAD);
            record.flags2 = AttackFlags2::of(AttackFlag2::STAMINA_DEFENSE);
            record.traits.set(AttackTrait::INSTANT_DEATH);
        }
        record.traits.set(AttackTrait::IGNORE_DEFENSE);
        break;
    case ItemId::DRILL:
        record.traits.set(AttackTrait::IGNORE_DEFENSE);
        break;
    case ItemId::DEBILITATOR:
        attack.effect = SpecialEffect::of(CommandEffect::Debilitator);
        break;
    case ItemId::AIR_ANCHOR:
        attack.effect = SpecialEffect::of(CommandEffect::AirAnchor);
        break;
    case ItemId::AUTOCROSSBOW:
        record.traits.set(AttackTrait::NO_DAMAGE_SPLIT);
        break;
    default:
        break;
    }
}

// What a consumable's record makes of the attack: its effect, its statuses and how it heals.
void addConsumableEffects(AttackInFlight& attack, const ItemProperties& properties) {
    AttackProperties& record = attack.record;
    record.traits.clear(AttackTrait::PHYSICAL);
    if (!properties.specialEffect.itemUseDisabled()) {
        attack.effect = SpecialEffect::of(properties.specialEffect.itemUseEffect());
    }
    record.statuses = itemStatuses(properties);

    const ItemUseFlagSet use = itemUseView(properties.weaponFlags);
    if (use.has(ItemUseFlag::FRACTIONAL_DAMAGE)) {
        record.flags2.set(AttackFlag2::FRACTIONAL_DAMAGE);
    }
    if (use.has(ItemUseFlag::REMOVES_STATUS)) {
        record.flags2.set(AttackFlag2::REMOVE_STATUS);
    }
    // Restoring MP restores, and works on MP.
    if (use.has(ItemUseFlag::RESTORES_MP)) {
        record.flags1.set(AttackFlag1::AFFECT_MP);
        record.flags2.set(AttackFlag2::RESTORE_HP_MP);
    }
    if (use.has(ItemUseFlag::RESTORES_HP)) {
        record.flags2.set(AttackFlag2::RESTORE_HP_MP);
    }
    if (use.has(ItemUseFlag::INVERT_ON_UNDEAD)) {
        record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
    }
    // An item that works on the dead raises them, and harms the undead.
    if (record.statuses.has(StatusId::DEAD)) {
        record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
        record.traits.set(AttackTrait::RESURRECTION_TARGET);
    }
}

}  // namespace

void itemAttack(BattleState& state, AttackInFlight& attack, BattlerSlot user, ItemId item, ItemUse use) {
    AttackProperties& record = attack.record;
    record = AttackProperties{};
    record.specialEffect = AttackSpecialEffect::NONE;
    record.traits = AttackTraitSet::of(AttackTrait::PHYSICAL, AttackTrait::IGNORE_DEFENSE);
    record.flags1 = AttackFlags1::of(AttackFlag1::RETARGET_IF_INVALID, AttackFlag1::IGNORE_REFLECT);
    record.flags2 = AttackFlags2::of(AttackFlag2::UNDODGEABLE);
    attack.effect = {};
    attack.attackerStat = 0;
    attack.attackerLevel = 0;
    if (use == ItemUse::ThrownOrWorked) {
        attack.attackerStat = state.at(user).vigor;
        attack.attackerLevel = attackerLevel(state, attack, user);
    }

    const ItemProperties& properties = getItemProperties(item);
    record.power = properties.power;
    record.element = properties.element;
    if (use == ItemUse::Consumed) {
        addConsumableEffects(attack, properties);
    }
    if (item == ItemId::INVIZ_EDGE) {
        record.statuses.set(StatusId::INVISIBLE);
    } else if (item == ItemId::SHADOW_EDGE) {
        record.statuses.set(StatusId::IMAGE);
    }

    if (properties.typeAndUsage.type() != ItemType::TOOL) {
        return;
    }
    record.traits.clear(AttackTrait::IGNORE_DEFENSE);
    record.flags2.clear(AttackFlag2::UNDODGEABLE);
    record.hitRate = properties.hitRateOrDefense;
    addToolEffect(state, attack, item);
}

}  // namespace ostinato::battle
