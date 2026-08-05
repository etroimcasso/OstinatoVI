// Full-corpus test of the item-properties table. The byte-equivalence test
// asserts EVERY one of the 256 packed records is byte-identical to the ROM's
// 30-byte record (no subset) and that every entry's identity field matches its
// position; the semantic tests exercise the lookup and the builder round-trips
// the emitted rows depend on. The JP language variant is pending a J-ROM rip —
// visible skip below, registered on every platform.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/item_properties.h"

#include "ostinato/character_id.h"
#include "ostinato/element.h"
#include "ostinato/element_set.h"
#include "ostinato/equip_permissions.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/item_type.h"
#include "ostinato/item_usage.h"
#include "ostinato/stat_boost_pair.h"
#include "ostinato/status_id.h"
#include "ostinato/target_flags.h"
#include "ostinato/weapon_flags.h"

#include "fixtures/item_prop_expected.h"

namespace {

// Full corpus: identity fields on both sides match the position, and one
// memcmp per packed record catches field-order, padding, decomposition, and
// builder drift in a single byte-for-byte comparison against the ROM.
TEST(ItemProperties, AllRecordsAreByteIdenticalToRom) {
    const auto table = ostinato::itemPropertiesEn();
    ASSERT_EQ(table.size(), ostinato::test::kExpectedItemEntries.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& expected = ostinato::test::kExpectedItemEntries[i];
        EXPECT_EQ(expected.id, i) << "fixture entry " << i;
        EXPECT_EQ(static_cast<std::size_t>(table[i].id), i)
            << "table entry " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, &expected.record, 30), 0)
            << "item index " << i;
    }
}

// The lookup indexes by ItemId. Spot-check a weapon's semantic surface
// against values hand-traced from the ROM record ($42 EXCALIBUR: type $11,
// equip mask $8053, holy element, +2/+2 and +1/+1 stat boosts, weapon flags
// $C2, power 217, hit rate 150, evade nibbles $02, price 2).
TEST(ItemProperties, LookupWeaponSemanticSurface) {
    using ostinato::CharacterId;
    using ostinato::Element;
    using ostinato::ItemId;
    using ostinato::ItemType;
    using ostinato::ItemUsage;
    using ostinato::WeaponFlags;

    const auto& excalibur = ostinato::getItemProperties(ItemId::EXCALIBUR);
    EXPECT_EQ(excalibur.typeAndUsage.type(), ItemType::WEAPON);
    EXPECT_TRUE(excalibur.typeAndUsage.usage().has(ItemUsage::THROW));
    EXPECT_FALSE(excalibur.typeAndUsage.usage().has(ItemUsage::MENU));
    EXPECT_TRUE(excalibur.equippableBy.canEquip(CharacterId::TERRA));
    EXPECT_TRUE(excalibur.equippableBy.canEquip(CharacterId::LOCKE));
    EXPECT_TRUE(excalibur.equippableBy.canEquip(CharacterId::EDGAR));
    EXPECT_TRUE(excalibur.equippableBy.canEquip(CharacterId::CELES));
    EXPECT_FALSE(excalibur.equippableBy.canEquip(CharacterId::CYAN));
    EXPECT_FALSE(excalibur.equippableBy.canEquip(CharacterId::UMARO));
    EXPECT_TRUE(excalibur.equippableBy.heavyGear());
    EXPECT_FALSE(excalibur.equippableBy.impGear());
    EXPECT_TRUE(excalibur.element.has(Element::HOLY));
    EXPECT_FALSE(excalibur.element.has(Element::FIRE));
    EXPECT_EQ(excalibur.vigorSpeed.first(), 2);
    EXPECT_EQ(excalibur.vigorSpeed.second(), 2);
    EXPECT_EQ(excalibur.staminaMagicPower.first(), 1);
    EXPECT_EQ(excalibur.staminaMagicPower.second(), 1);
    EXPECT_TRUE(excalibur.weaponFlags.has(WeaponFlags::SWDTECH));
    EXPECT_TRUE(excalibur.weaponFlags.has(WeaponFlags::TWO_HAND));
    EXPECT_TRUE(excalibur.weaponFlags.has(WeaponFlags::RUNIC));
    EXPECT_FALSE(excalibur.weaponFlags.has(WeaponFlags::BACK_ROW));
    EXPECT_EQ(excalibur.power, 217u);
    EXPECT_EQ(excalibur.hitRateOrDefense, 150u);
    EXPECT_EQ(excalibur.evadeMagicBlock.evadeIndex(), 2u);
    EXPECT_EQ(excalibur.evadeMagicBlock.mblockIndex(), 0u);
    EXPECT_EQ(excalibur.specialEffect.weaponEffect(),
              ostinato::WeaponSpecialEffect::NONE);
    EXPECT_EQ(excalibur.specialEffect.blockGraphic(),
              ostinato::BlockGraphic::SWORD);
    EXPECT_TRUE(excalibur.specialEffect.blocksPhysical());
    EXPECT_FALSE(excalibur.specialEffect.blocksMagic());
    EXPECT_EQ(excalibur.price, 2u);

    const auto& drainer = ostinato::getItemProperties(ItemId::DRAINER);
    EXPECT_EQ(drainer.specialEffect.weaponEffect(),
              ostinato::WeaponSpecialEffect::DRAINER);

    // The rods and elemental shields carry the spell-cast byte's two mode
    // bits (random cast on attack / cast when used as an item).
    const auto& fireRod = ostinato::getItemProperties(ItemId::FIRE_ROD);
    EXPECT_TRUE(fireRod.spellCast.randomOnAttack());
    EXPECT_TRUE(fireRod.spellCast.castOnItemUse());
    EXPECT_EQ(fireRod.spellCast.spell(), ostinato::AttackId::FIRE_2);
    const auto& flameShield = ostinato::getItemProperties(ItemId::FLAME_SHLD);
    EXPECT_FALSE(flameShield.spellCast.randomOnAttack());
    EXPECT_TRUE(flameShield.spellCast.castOnItemUse());
    EXPECT_EQ(flameShield.spellCast.spell(), ostinato::AttackId::FIRE_3);
}

// Spot-check the non-equipment roles: a consumable's power byte is its HP
// restored, its weapon-flags byte carries the named consumable-role bits,
// and its special-effect byte is the item-use surface. A relic's effect byte
// carries its named relic bit; the imp-gear bit rides bit 14 of the equip
// mask (TORTOISESHLD); the two dead +19 bits port verbatim via their named
// constants.
TEST(ItemProperties, LookupRoleOverloadedSurfaces) {
    using ostinato::ItemId;
    using ostinato::ItemType;
    using ostinato::ItemUsage;
    using ostinato::ItemUseEffect;
    using ostinato::ItemUseFlag;
    using ostinato::itemUseView;
    using ostinato::RelicEffect3;

    const auto& potion = ostinato::getItemProperties(ItemId::POTION);
    EXPECT_EQ(potion.typeAndUsage.type(), ItemType::CONSUMABLE);
    EXPECT_TRUE(potion.typeAndUsage.usage().has(ItemUsage::BATTLE));
    EXPECT_TRUE(potion.typeAndUsage.usage().has(ItemUsage::MENU));
    EXPECT_EQ(potion.power, 250u);  // HP restored (consumable role of +20)
    EXPECT_TRUE(itemUseView(potion.weaponFlags).has(ItemUseFlag::RESTORES_HP));
    EXPECT_TRUE(
        itemUseView(potion.weaponFlags).has(ItemUseFlag::INVERT_ON_UNDEAD));
    EXPECT_FALSE(
        itemUseView(potion.weaponFlags).has(ItemUseFlag::FRACTIONAL_DAMAGE));
    EXPECT_TRUE(potion.specialEffect.itemUseDisabled());
    EXPECT_EQ(potion.price, 300u);

    const auto& magicite = ostinato::getItemProperties(ItemId::MAGICITE);
    EXPECT_EQ(magicite.specialEffect.itemUseEffect(), ItemUseEffect::MAGICITE);
    EXPECT_FALSE(magicite.specialEffect.itemUseDisabled());
    EXPECT_EQ(magicite.weaponFlags.bits, ostinato::kDeadItemFlagBit6);

    const auto& sneakRing = ostinato::getItemProperties(ItemId::SNEAK_RING);
    EXPECT_EQ(sneakRing.typeAndUsage.type(), ItemType::RELIC);
    EXPECT_TRUE(sneakRing.relicEffects3.has(RelicEffect3::RAISE_STEAL_RATE));
    EXPECT_FALSE(sneakRing.relicEffects3.has(RelicEffect3::MP_COST_1));

    const auto& safetyBit = ostinato::getItemProperties(ItemId::SAFETY_BIT);
    EXPECT_EQ(safetyBit.weaponFlags.bits, ostinato::kDeadItemFlagBit0);

    const auto& tortoiseShield =
        ostinato::getItemProperties(ItemId::TORTOISESHLD);
    EXPECT_TRUE(tortoiseShield.equippableBy.impGear());
}

// Builder round-trips: every of(...) builder re-packs to the raw ROM byte(s)
// the parser decomposed. One case per builder family, values chosen so each
// bit path is exercised.
TEST(ItemProperties, BuilderRoundTrips) {
    using ostinato::CharacterId;
    using ostinato::EquipPermissions;
    using ostinato::EquipSpecial;
    using ostinato::EvadeBlockPair;
    using ostinato::FieldEffect;
    using ostinato::FieldEffectSet;
    using ostinato::ItemSpellCast;
    using ostinato::ItemType;
    using ostinato::ItemTypeUsage;
    using ostinato::ItemUsage;
    using ostinato::StatBoostPair;
    using ostinato::Status1Set;
    using ostinato::Status3Set;
    using ostinato::StatusId;

    EXPECT_EQ(ItemTypeUsage::of(ItemType::CONSUMABLE, ItemUsage::BATTLE,
                                ItemUsage::MENU).packed,
              0x66u);
    EXPECT_EQ(ItemTypeUsage::of(ItemType::WEAPON).packed, 0x01u);
    EXPECT_EQ(ItemTypeUsage::of(ItemType::WEAPON, ItemUsage::THROW).type(),
              ItemType::WEAPON);

    const auto auraLance = EquipPermissions::of(
        CharacterId::EDGAR, CharacterId::MOG, EquipSpecial::HEAVY);
    EXPECT_EQ(auraLance.bits(), 0x8410u);
    EXPECT_EQ(auraLance.lo, 0x10u);
    EXPECT_EQ(auraLance.hi, 0x84u);
    EXPECT_EQ(EquipPermissions{}.bits(), 0x0000u);

    EXPECT_EQ(StatBoostPair::of(2, 2).packed, 0x22u);
    EXPECT_EQ(StatBoostPair::of(-7, 3).packed, 0x3Fu);
    EXPECT_EQ(StatBoostPair{0x3F}.first(), -7);
    EXPECT_EQ(StatBoostPair{0x3F}.second(), 3);

    EXPECT_EQ(EvadeBlockPair::of(2, 5).packed, 0x52u);
    EXPECT_EQ(EvadeBlockPair{0x52}.evadeIndex(), 2u);
    EXPECT_EQ(EvadeBlockPair{0x52}.mblockIndex(), 5u);

    EXPECT_EQ(FieldEffectSet::of(FieldEffect::SPRINT_SHOES,
                                 FieldEffect::CHARM_BANGLE).bits,
              0x21u);

    // The role-shaped +18/+19/+27 builders re-pack to the exact ROM bytes:
    // Fire Rod's $C5 spell-cast byte, Potion's $0A item-use flags, and the
    // special-effect byte's three shapes (Excalibur $05 equipment, Drainer
    // $50 weapon, Magicite $01 item-use, plus the $FF disabled sentinel).
    using ostinato::AttackId;
    using ostinato::BlockAbility;
    using ostinato::BlockGraphic;
    using ostinato::ItemSpecialEffect;
    using ostinato::ItemUseEffect;
    using ostinato::ItemUseFlag;
    using ostinato::itemUseFlags;
    using ostinato::SpellCastMode;
    using ostinato::WeaponSpecialEffect;

    EXPECT_EQ(ItemSpellCast::of(AttackId::FIRE_2,
                                SpellCastMode::RANDOM_ON_ATTACK,
                                SpellCastMode::CAST_ON_ITEM_USE).raw,
              0xC5u);
    EXPECT_EQ(ItemSpellCast::of(AttackId::FIRE_3,
                                SpellCastMode::CAST_ON_ITEM_USE).raw,
              0x89u);

    EXPECT_EQ(itemUseFlags(ItemUseFlag::INVERT_ON_UNDEAD,
                           ItemUseFlag::RESTORES_HP).bits,
              0x0Au);

    EXPECT_EQ(ItemSpecialEffect::equipment(WeaponSpecialEffect::NONE,
                                           BlockGraphic::SWORD,
                                           BlockAbility::PHYSICAL).packed,
              0x05u);
    EXPECT_EQ(ItemSpecialEffect::weapon(WeaponSpecialEffect::DRAINER).packed,
              0x50u);
    EXPECT_EQ(ItemSpecialEffect::itemUse(ItemUseEffect::MAGICITE).packed,
              0x01u);
    EXPECT_EQ(ItemSpecialEffect::disabled().packed, 0xFFu);
    EXPECT_EQ(ItemSpecialEffect{}.packed, 0x00u);

    // StatusId slices pack id -> bit id%8 within their byte: DEAD=$07 ->
    // byte 0 bit 7, DANCE=$10 -> byte 2 bit 0.
    EXPECT_EQ(Status1Set::of(StatusId::BLIND, StatusId::DEAD).bits, 0x81u);
    EXPECT_EQ(Status3Set::of(StatusId::DANCE).bits, 0x01u);
    EXPECT_TRUE(Status1Set::of(StatusId::DEAD).has(StatusId::DEAD));
    EXPECT_FALSE(Status1Set::of(StatusId::DEAD).has(StatusId::DANCE));

    EXPECT_EQ(ItemSpellCast{}.raw, 0x00u);
}

// The JP table (item_prop_jp.dat) is language-variant upstream and pending a
// J-ROM rip — the skip stays visible on every platform until it lands.
TEST(ItemProperties, JpVariantTable) {
    GTEST_SKIP() << "item_prop JP variant pending J-ROM rip";
}

}  // namespace
