// The party's side of a battle's start: equipment into battle stats, placing the party, loading
// each member, their commands, the battle inventory, and re-deriving a member mid-battle.
// Every expectation names the line of battle/battle_main.asm it is read from.

#include <gtest/gtest.h>

#include <algorithm>

#include "battle/battle_inventory.h"
#include "battle/party_setup.h"
#include "battle_test_support.h"
#include "data/battle_commands.h"
#include "data/battle_tables.h"
#include "data/character.h"
#include "data/formations.h"
#include "data/item_properties.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using battle::BattleState;
using battle::DerivedStats;
using battle::MaxBoost;

const BattlerSlot kFirst = BattlerSlot::character(0);
const BattlerSlot kSecond = BattlerSlot::character(1);

// The first item of `type` that `matches` accepts, for the rules no named item pins.
template <typename Match>
ItemId firstItem(ItemType type, Match matches) {
    for (const ItemPropertiesEntry& entry : itemPropertiesEn()) {
        if (entry.id != ItemId::EMPTY && entry.record.typeAndUsage.type() == type && matches(entry.record)) {
            return entry.id;
        }
    }
    ADD_FAILURE() << "no item matches";
    return ItemId::EMPTY;
}

// The first item of any type that `matches` accepts.
template <typename Match>
ItemId firstAnyItem(Match matches) {
    for (const ItemPropertiesEntry& entry : itemPropertiesEn()) {
        if (entry.id != ItemId::EMPTY && matches(entry.record)) {
            return entry.id;
        }
    }
    ADD_FAILURE() << "no item matches";
    return ItemId::EMPTY;
}

const ItemProperties& item(ItemId id) { return getItemProperties(id); }

// --- equipment ---------------------------------------------------------------------------------

TEST(DeriveEquipment, StartsFromTheCharactersBattlePropertiesAndOwnStats) {
    // UpdateEquip :2317-2339: battle power into both hands, defense, evade and magic block from the
    // character's battle properties; vigor, speed, stamina and magic power from the record.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::LOCKE);
    record.vigor = 37;
    record.speed = 40;
    record.stamina = 31;
    record.magicPower = 28;
    const CharacterBaseStats& base = getCharacterBaseStats(CharacterPropId::LOCKE);
    const DerivedStats stats = battle::deriveEquipment(record);
    EXPECT_EQ(stats.vigor, 37);
    EXPECT_EQ(stats.speed, 40);
    EXPECT_EQ(stats.stamina, 31);
    EXPECT_EQ(stats.magicPower, 28);
    EXPECT_EQ(stats.defense, base.defense);
    EXPECT_EQ(stats.magicDefense, base.magicDefense);
    EXPECT_EQ(stats.evade, base.evade);
    EXPECT_EQ(stats.magicBlock, base.magicBlock);
}

TEST(DeriveEquipment, TheRunFactorTraitGivesFiveDownToTwo) {
    // :2328-2332: (trait & 3) ^ 3, plus 2.
    for (const CharacterBaseStatsEntry& entry : characterBaseStats()) {
        game::CharacterRecord record = unequippedRecord(entry.id);
        const int trait = static_cast<int>(entry.record.traits.runFactor());
        EXPECT_EQ(battle::deriveEquipment(record).runFactor, (trait ^ 3) + 2) << static_cast<int>(entry.id);
    }
}

TEST(DeriveEquipment, ABareHandIsTheEmptyItemsPowerAndHitRate) {
    // CalcEquipEffect :2612-2641: an empty hand runs the weapon path with the empty item's record,
    // which is typed a weapon; its power 10 and hit rate 200 go to that hand.
    const game::CharacterRecord record = unequippedRecord(CharacterPropId::SABIN);
    const std::uint8_t base = getCharacterBaseStats(CharacterPropId::SABIN).battlePower;
    const DerivedStats stats = battle::deriveEquipment(record);
    EXPECT_EQ(item(ItemId::EMPTY).power, 10);
    EXPECT_EQ(stats.weaponHand.battlePower, base + 10);
    EXPECT_EQ(stats.weaponHand.hitRate, 200);
    // Two bare hands: the weapon hand attacks (UpdateEquip_0c :2433-2439).
    EXPECT_EQ(stats.shieldHand.battlePower, 0);
}

TEST(DeriveEquipment, ArmorAddsDefenseSaturatingAt255) {
    // :2562-2573: defense and magic defense add, capped at 255.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::EDGAR);
    record.armor = ItemId::MITHRIL_VEST;
    const CharacterBaseStats& base = getCharacterBaseStats(CharacterPropId::EDGAR);
    const DerivedStats stats = battle::deriveEquipment(record);
    EXPECT_EQ(stats.defense, std::min(255, base.defense + item(ItemId::MITHRIL_VEST).power));
    EXPECT_EQ(stats.magicDefense, std::min(255, base.magicDefense + item(ItemId::MITHRIL_VEST).hitRateOrDefense));

    // Six pieces of the strongest armor saturate.
    const ItemId heavy = firstItem(ItemType::ARMOR, [](const ItemProperties& p) { return p.power >= 60; });
    record.helmet = record.armor = record.relic1 = record.relic2 = heavy;
    record.shield = heavy;
    EXPECT_EQ(battle::deriveEquipment(record).defense, 255);
}

TEST(DeriveEquipment, StatBoostsAddAndClampToZeroAndTo255) {
    // :2498-2512 adds each signed nibble; :2363-2373 clamps a stat below 0 to 0 and above 255 to 255.
    const ItemId minus = firstAnyItem([](const ItemProperties& p) { return p.vigorSpeed.first() < 0; });
    game::CharacterRecord record = unequippedRecord(CharacterPropId::TERRA);
    record.vigor = 0;
    record.relic1 = minus;
    EXPECT_EQ(battle::deriveEquipment(record).vigor, 0);

    const ItemId plus = firstAnyItem([](const ItemProperties& p) {
        return p.vigorSpeed.first() > 0 && p.typeAndUsage.type() != ItemType::WEAPON;
    });
    record.vigor = 254;
    record.relic1 = plus;
    EXPECT_EQ(battle::deriveEquipment(record).vigor, 255);
    record.vigor = 10;
    EXPECT_EQ(battle::deriveEquipment(record).vigor, 10 + item(plus).vigorSpeed.first());
}

TEST(DeriveEquipment, EvadeBoostsComeFromTheEvadeTable) {
    // :2513-2530: each nibble indexes EquipEvadeTbl.
    const ItemId evasive = firstAnyItem([](const ItemProperties& p) { return p.evadeMagicBlock.evadeIndex() != 0; });
    game::CharacterRecord record = unequippedRecord(CharacterPropId::SHADOW);
    record.relic2 = evasive;
    const int base = getCharacterBaseStats(CharacterPropId::SHADOW).evade;
    EXPECT_EQ(battle::deriveEquipment(record).evade,
              std::clamp(base + equipEvadeBoost(item(evasive).evadeMagicBlock.evadeIndex()), 0, 255));
}

TEST(DeriveEquipment, GearWornTheWrongWayRoundForAnImpCountsAsPowerOne) {
    // :2533-2545: an imp item on someone who is not an Imp, or any other item on an Imp, has power 1.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::CELES);
    const CharacterBaseStats& base = getCharacterBaseStats(CharacterPropId::CELES);
    ASSERT_TRUE(item(ItemId::IMPS_ARMOR).equippableBy.impGear());
    ASSERT_FALSE(item(ItemId::MITHRIL_VEST).equippableBy.impGear());

    record.armor = ItemId::IMPS_ARMOR;
    EXPECT_EQ(battle::deriveEquipment(record).defense, base.defense + 1);
    record.status.set(StatusId::IMP);
    EXPECT_EQ(battle::deriveEquipment(record).defense, base.defense + item(ItemId::IMPS_ARMOR).power);
    record.armor = ItemId::MITHRIL_VEST;
    EXPECT_EQ(battle::deriveEquipment(record).defense, base.defense + 1);
    // Magic defense is not held back.
    EXPECT_EQ(battle::deriveEquipment(record).magicDefense,
              base.magicDefense + item(ItemId::MITHRIL_VEST).hitRateOrDefense);
    // An Imp's bare hand is the empty item, which is not imp gear.
    EXPECT_EQ(battle::deriveEquipment(record).weaponHand.battlePower, base.battlePower + 1);
}

TEST(DeriveEquipment, AWeaponAndAShieldAttackWithTheWeaponHand) {
    // UpdateEquip_02 :2431-2439: no two-handed grip, and the shield hand does not attack.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::LOCKE);
    record.weapon = ItemId::DIRK;
    record.shield = ItemId::BUCKLER;
    const std::uint8_t base = getCharacterBaseStats(CharacterPropId::LOCKE).battlePower;
    const DerivedStats stats = battle::deriveEquipment(record);
    EXPECT_EQ(stats.weaponHand.battlePower, base + item(ItemId::DIRK).power);
    EXPECT_EQ(stats.weaponHand.hitRate, item(ItemId::DIRK).hitRateOrDefense);
    EXPECT_EQ(stats.shieldHand.battlePower, 0);
    EXPECT_FALSE(stats.twoWeapons);
}

TEST(DeriveEquipment, AWeaponInTheShieldHandAttacksFromThere) {
    // UpdateEquip_01 :2414-2420: a shield in the weapon hand and a weapon in the other.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::LOCKE);
    record.weapon = ItemId::BUCKLER;
    record.shield = ItemId::DIRK;
    const std::uint8_t base = getCharacterBaseStats(CharacterPropId::LOCKE).battlePower;
    const DerivedStats stats = battle::deriveEquipment(record);
    EXPECT_EQ(stats.weaponHand.battlePower, 0);
    EXPECT_EQ(stats.shieldHand.battlePower, base + item(ItemId::DIRK).power);
}

TEST(DeriveEquipment, ABareHandBesideAWeaponDoesNotAttack) {
    // UpdateEquip_06 :2436-2439 and UpdateEquip_09 :2418-2420: whichever hand is bare is zeroed,
    // and the two-handed grip is kept for the weapon.
    const ItemId twoHanded = firstItem(ItemType::WEAPON, [](const ItemProperties& p) {
        return p.weaponFlags.has(WeaponFlags::TWO_HAND);
    });
    game::CharacterRecord record = unequippedRecord(CharacterPropId::CYAN);
    record.weapon = twoHanded;
    DerivedStats stats = battle::deriveEquipment(record);
    EXPECT_EQ(stats.shieldHand.battlePower, 0);
    EXPECT_TRUE(stats.weaponHand.weaponFlags.has(WeaponFlags::TWO_HAND));

    record.weapon = ItemId::EMPTY;
    record.shield = twoHanded;
    stats = battle::deriveEquipment(record);
    EXPECT_EQ(stats.weaponHand.battlePower, 0);
    EXPECT_TRUE(stats.shieldHand.weaponFlags.has(WeaponFlags::TWO_HAND));

    // Beside a shield the grip is lost (UpdateEquip_02).
    record.weapon = twoHanded;
    record.shield = ItemId::BUCKLER;
    EXPECT_FALSE(battle::deriveEquipment(record).weaponHand.weaponFlags.has(WeaponFlags::TWO_HAND));
}

TEST(DeriveEquipment, AWeaponInEachHandMarksTwoWeaponsAndLosesTheGrip) {
    // UpdateEquip_03 :2441-2450.
    const ItemId twoHanded = firstItem(ItemType::WEAPON, [](const ItemProperties& p) {
        return p.weaponFlags.has(WeaponFlags::TWO_HAND);
    });
    game::CharacterRecord record = unequippedRecord(CharacterPropId::EDGAR);
    record.weapon = twoHanded;
    record.shield = ItemId::DIRK;
    const DerivedStats stats = battle::deriveEquipment(record);
    EXPECT_TRUE(stats.twoWeapons);
    EXPECT_FALSE(stats.weaponHand.weaponFlags.has(WeaponFlags::TWO_HAND));
    EXPECT_GT(stats.weaponHand.battlePower, 0);
    EXPECT_GT(stats.shieldHand.battlePower, 0);
}

TEST(DeriveEquipment, OneItemThatRaisesMagicDamageIsTheSingleEffectAndTwoTheDouble) {
    // :2576-2582.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::RELM);
    record.relic1 = ItemId::EARRINGS;
    DerivedStats stats = battle::deriveEquipment(record);
    EXPECT_TRUE(stats.relicEffects3.has(RelicEffect3::RAISE_MAGIC_DAMAGE));
    EXPECT_FALSE(stats.relicEffects1.has(RelicEffect1::RAISE_MAGIC_DAMAGE));

    record.relic2 = ItemId::HERO_RING;
    stats = battle::deriveEquipment(record);
    EXPECT_TRUE(stats.relicEffects3.has(RelicEffect3::RAISE_MAGIC_DAMAGE));
    EXPECT_TRUE(stats.relicEffects1.has(RelicEffect1::RAISE_MAGIC_DAMAGE));
}

TEST(DeriveEquipment, TheLargestHpAndMpBoostWins) {
    // CheckHPBoost :2456-2472: +50% before +25% before +12.5%.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::TERRA);
    record.relic1 = ItemId::GREEN_BERET;
    EXPECT_EQ(battle::deriveEquipment(record).hpBoost, MaxBoost::Plus12Point5Percent);
    record.helmet = ItemId::RED_CAP;
    EXPECT_EQ(battle::deriveEquipment(record).hpBoost, MaxBoost::Plus25Percent);
    record.relic2 = ItemId::MUSCLE_BELT;
    EXPECT_EQ(battle::deriveEquipment(record).hpBoost, MaxBoost::Plus50Percent);

    record = unequippedRecord(CharacterPropId::TERRA);
    record.helmet = ItemId::BARDS_HAT;
    EXPECT_EQ(battle::deriveEquipment(record).mpBoost, MaxBoost::Plus12Point5Percent);
    record.armor = ItemId::MINERVA;
    EXPECT_EQ(battle::deriveEquipment(record).mpBoost, MaxBoost::Plus25Percent);
    record.relic1 = ItemId::CRYSTAL_ORB;
    EXPECT_EQ(battle::deriveEquipment(record).mpBoost, MaxBoost::Plus50Percent);
}

TEST(DeriveEquipment, AHyperWristAddsHalfTheVigorAfterTheClamp) {
    // :2376-2383.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::SABIN);
    record.vigor = 128;
    record.relic1 = ItemId::HYPER_WRIST;
    const int boost = item(ItemId::HYPER_WRIST).vigorSpeed.first();
    EXPECT_EQ(battle::deriveEquipment(record).vigor, (128 + boost) + (128 + boost) / 2);
}

TEST(BoostedMaximum, AddsAnEighthAQuarterOrAHalfRoundingDown) {
    // CalcMaxHPMP :2669-2704.
    EXPECT_EQ(battle::boostedMaximum(1000, MaxBoost::None), 1000);
    EXPECT_EQ(battle::boostedMaximum(1007, MaxBoost::Plus12Point5Percent), 1007 + 125);
    EXPECT_EQ(battle::boostedMaximum(1003, MaxBoost::Plus25Percent), 1003 + 250);
    EXPECT_EQ(battle::boostedMaximum(1001, MaxBoost::Plus50Percent), 1001 + 500);
}

// --- into the battler --------------------------------------------------------------------------

BattleState loadedBattle(const game::CharacterRecord& record) {
    BattleState state;
    state.party.characters[0] = record;
    state.party.membership[0] = {.party = 1, .slot = 0};
    battle::placeParty(state, {});
    return state;
}

TEST(ApplyDerivedStats, TheBattlesVigorIsTwiceTheCharactersUpTo255) {
    // UpdateEquipBattle :2752-2757.
    BattleState state = loadedBattle(unequippedRecord(CharacterPropId::TERRA));
    DerivedStats stats;
    stats.vigor = 100;
    battle::applyDerivedStats(state, kFirst, stats);
    EXPECT_EQ(state.at(kFirst).vigor, 200);
    stats.vigor = 128;
    battle::applyDerivedStats(state, kFirst, stats);
    EXPECT_EQ(state.at(kFirst).vigor, 255);
}

TEST(ApplyDerivedStats, EvadeIsStoredAsTheHitChecksFactor) {
    // InvertEvade :2719-2727: 0 gives 255, a rating r below 128 gives 256 - 2r, 128 or more gives 1.
    BattleState state = loadedBattle(unequippedRecord(CharacterPropId::TERRA));
    DerivedStats stats;
    const std::pair<int, int> cases[] = {{0, 255}, {1, 254}, {10, 236}, {127, 2}, {128, 1}, {255, 1}};
    for (const auto& [rating, factor] : cases) {
        stats.evade = rating;
        stats.magicBlock = rating;
        battle::applyDerivedStats(state, kFirst, stats);
        EXPECT_EQ(state.at(kFirst).evadeFactor, factor) << rating;
        EXPECT_EQ(state.at(kFirst).magicBlockFactor, factor) << rating;
    }
}

TEST(ApplyDerivedStats, OnlyAGauntletKeepsTheTwoHandedGrip) {
    // :2802-2806.
    BattleState state = loadedBattle(unequippedRecord(CharacterPropId::TERRA));
    DerivedStats stats;
    stats.weaponHand.weaponFlags.set(WeaponFlags::TWO_HAND);
    battle::applyDerivedStats(state, kFirst, stats);
    EXPECT_FALSE(state.at(kFirst).weaponHand.weaponFlags.has(WeaponFlags::TWO_HAND));
    stats.relicEffects4.set(RelicEffect4::USE_WEAPON_2_HANDED);
    battle::applyDerivedStats(state, kFirst, stats);
    EXPECT_TRUE(state.at(kFirst).weaponHand.weaponFlags.has(WeaponFlags::TWO_HAND));
}

TEST(ApplyDerivedStats, AGenjiGloveLetsTheMenuGiveTwoWeaponsAndKeepsItsEffectOnlyWithOne) {
    // :2749-2751 reads the glove before :2771-2772 clears it for a wearer with two weapons.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::EDGAR);
    record.relic1 = ItemId::GENJI_GLOVE;
    record.weapon = ItemId::DIRK;
    BattleState state = loadedBattle(record);
    battle::applyDerivedStats(state, kFirst, battle::deriveEquipment(record));
    EXPECT_TRUE(state.at(kFirst).mayHoldTwoWeapons);
    EXPECT_TRUE(state.at(kFirst).relicEffects4.has(RelicEffect4::EQUIP_2_WEAPONS));
    record.shield = ItemId::DIRK;
    battle::applyDerivedStats(state, kFirst, battle::deriveEquipment(record));
    EXPECT_TRUE(state.at(kFirst).mayHoldTwoWeapons);
    EXPECT_FALSE(state.at(kFirst).relicEffects4.has(RelicEffect4::EQUIP_2_WEAPONS));
}

TEST(ApplyDerivedStats, AMoogleSuitDrawsTheWearerAsMog) {
    // :2738-2745.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::RELM);
    record.armor = ItemId::MOOGLE_SUIT;
    BattleState state = loadedBattle(record);
    EXPECT_NE(state.at(kFirst).graphics, CharacterGfxId::MOG);
    battle::applyDerivedStats(state, kFirst, battle::deriveEquipment(record));
    EXPECT_EQ(state.at(kFirst).graphics, CharacterGfxId::MOG);
}

TEST(ApplyDerivedStats, ProtectionDecidesTheFirstSixteenStatusesAndDeadBecomesInstantDeathProtection) {
    // :2811-2814 then FixImmuneStatus :6359-6380.
    BattleState state = loadedBattle(unequippedRecord(CharacterPropId::TERRA));
    DerivedStats stats;
    stats.protection = StatusSet::of(StatusId::DEAD, StatusId::SLEEP, StatusId::BLIND);
    battle::applyDerivedStats(state, kFirst, stats);
    const battle::Battler& battler = state.at(kFirst);
    EXPECT_TRUE(battler.instantDeathProtected);
    EXPECT_TRUE(battler.inflictableStatuses.has(StatusId::DEAD));
    EXPECT_FALSE(battler.inflictableStatuses.has(StatusId::SLEEP));
    EXPECT_FALSE(battler.inflictableStatuses.has(StatusId::BLIND));
    EXPECT_TRUE(battler.inflictableStatuses.has(StatusId::POISON));
    EXPECT_TRUE(battler.inflictableStatuses.has(StatusId::STOP));  // the third and fourth bytes are left alone

    stats.protection = StatusSet{};
    stats.elementsNullified = ElementSet::of(Element::POISON);
    battle::applyDerivedStats(state, kFirst, stats);
    EXPECT_FALSE(state.at(kFirst).instantDeathProtected);
    EXPECT_FALSE(state.at(kFirst).inflictableStatuses.has(StatusId::POISON));
    EXPECT_TRUE(state.at(kFirst).inflictableStatuses.has(StatusId::SLEEP));
}

TEST(ApplyDerivedStats, AMeritAwardLetsTheWearerHoldHeavyItems) {
    // :2830-2832: the relic sets the heavy bit of the wearer's equippable-by mask.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::CELES);
    BattleState state = loadedBattle(record);
    EXPECT_FALSE(state.at(kFirst).equipsAs.heavyGear());
    record.relic1 = ItemId::MERIT_AWARD;
    battle::applyDerivedStats(state, kFirst, battle::deriveEquipment(record));
    EXPECT_TRUE(state.at(kFirst).equipsAs.heavyGear());
    EXPECT_TRUE(state.at(kFirst).equipsAs.canEquip(CharacterId::CELES));
    record.relic1 = ItemId::EMPTY;
    battle::applyDerivedStats(state, kFirst, battle::deriveEquipment(record));
    EXPECT_FALSE(state.at(kFirst).equipsAs.heavyGear());
}

TEST(ApplyDerivedStats, RelicEffectsAndUndeadComeFromTheEquipmentAndThePartysSecondEffectsGather) {
    // :2779-2787.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::SETZER);
    record.relic1 = ItemId::RELIC_RING;
    record.relic2 = ItemId::DRAGOONBOOTS;
    BattleState state = loadedBattle(record);
    battle::applyDerivedStats(state, kFirst, battle::deriveEquipment(record));
    EXPECT_TRUE(state.at(kFirst).undead);
    EXPECT_TRUE(state.at(kFirst).human);
    EXPECT_TRUE(state.partyRelicEffects2.has(RelicEffect2::FIGHT_TO_JUMP));
    // A later party member's effects add to the party's; nothing takes them away.
    battle::applyDerivedStats(state, kSecond, battle::deriveEquipment(unequippedRecord(CharacterPropId::MOG)));
    EXPECT_TRUE(state.partyRelicEffects2.has(RelicEffect2::FIGHT_TO_JUMP));
}

TEST(StatusImmunity, FloatEquipmentMorphAndThePairedStatuses) {
    // UpdateImmuneStatus :6386-6433.
    BattleState state = loadedBattle(unequippedRecord(CharacterPropId::TERRA));
    battle::Battler& battler = state.at(kFirst);
    battler.equipmentStatuses = StatusSet::of(StatusId::FLOAT, StatusId::HASTE, StatusId::REGEN);
    state.permanentMorph = true;
    battle::refreshStatusImmunity(state, kFirst);
    EXPECT_FALSE(battler.inflictableStatuses.has(StatusId::FLOAT));
    EXPECT_FALSE(battler.inflictableStatuses.has(StatusId::MORPH));
    // Equipment that keeps Haste and Regen on stops both, and with them Slow and Sap.
    EXPECT_FALSE(battler.inflictableStatuses.has(StatusId::HASTE));
    EXPECT_FALSE(battler.inflictableStatuses.has(StatusId::SLOW));
    EXPECT_FALSE(battler.inflictableStatuses.has(StatusId::REGEN));
    EXPECT_FALSE(battler.inflictableStatuses.has(StatusId::SAP));
    EXPECT_TRUE(battler.inflictableStatuses.has(StatusId::STOP));
    EXPECT_TRUE(battler.inflictableStatuses.has(StatusId::SHELL));
}

TEST(StatusImmunity, EachPairFallsTogetherWhicheverHalfIsBlocked) {
    BattleState state = loadedBattle(unequippedRecord(CharacterPropId::TERRA));
    battle::Battler& battler = state.at(kFirst);
    battler.inflictableStatuses.clear(StatusId::SAP);
    battler.inflictableStatuses.clear(StatusId::SLOW);
    battle::refreshStatusImmunity(state, kFirst);
    EXPECT_FALSE(battler.inflictableStatuses.has(StatusId::REGEN));
    EXPECT_FALSE(battler.inflictableStatuses.has(StatusId::HASTE));
    EXPECT_TRUE(battler.inflictableStatuses.has(StatusId::FLOAT));
}

// --- placing the party -------------------------------------------------------------------------

TEST(PlaceParty, TheCurrentPartyStandsInItsSlots) {
    // InitParty :7757-7774 and :7845-7917.
    BattleState state = battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR});
    state.party.membership[1].slot = 3;
    state.party.membership[1].backRow = true;
    state.party.membership[5] = {.party = 2, .slot = 1};  // another party
    state.party.characters[5] = startingRecord(CharacterPropId::SABIN);
    battle::placeParty(state, {});

    EXPECT_EQ(state.at(kFirst).actor, CharacterPropId::TERRA);
    EXPECT_EQ(state.at(kFirst).record, 0U);
    EXPECT_TRUE(state.at(kFirst).present);
    EXPECT_FALSE(state.at(kFirst).actsAsEnemy);
    EXPECT_EQ(state.at(kFirst).graphics, state.party.characters[0].graphics);
    EXPECT_EQ(state.at(BattlerSlot::character(3)).actor, CharacterPropId::LOCKE);
    EXPECT_TRUE(state.at(BattlerSlot::character(3)).backRow);
    EXPECT_EQ(state.at(kSecond).actor, std::nullopt);
    EXPECT_EQ(state.at(BattlerSlot::character(2)).actor, CharacterPropId::EDGAR);
    EXPECT_TRUE(state.at(BattlerSlot::character(2)).equipsAs.canEquip(CharacterId::EDGAR));
    EXPECT_FALSE(state.at(BattlerSlot::character(2)).equipsAs.canEquip(CharacterId::TERRA));
}

TEST(PlaceParty, TheLowerRecordTakesASlotTwoRecordsClaim) {
    // :7757-7774 walks the records from the last to the first, so the first written last wins.
    BattleState state = battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE});
    state.party.membership[1].slot = 0;
    battle::placeParty(state, {});
    EXPECT_EQ(state.at(kFirst).actor, CharacterPropId::TERRA);
}

TEST(PlaceParty, AGuestsEquippableBitsAreTheLastTwoLettersOfTheirName) {
    // :7891-7908: past the fourteen playable characters the actor mask is the word last loaded,
    // the name's fifth and sixth bytes (docs/Bugs.md).
    BattleState state = battleWithParty({CharacterPropId::BANON});
    state.party.characters[0].name = {0x81, 0x80, 0x8D, 0x8E, 0x8D, 0xFF};
    battle::placeParty(state, {});
    EXPECT_EQ(state.at(kFirst).equipsAs.bits(), 0xFF8D);
}

FormationId formationWhere(bool lastMonsterSlotUsed) {
    for (const FormationEntry& entry : formations()) {
        if (entry.record.slotEmpty(battle::kMonsterSlots - 1) != lastMonsterSlotUsed) {
            return entry.id;
        }
    }
    ADD_FAILURE() << "no such formation";
    return FormationId{};
}

TEST(PlaceParty, GauReturnsFromTheVeldtOnlyWhenThereIsRoomForHim) {
    // :7775-7792: not when Gau has joined, the party is full or the sixth monster slot is used;
    // otherwise the Veldt setup brings him in, out of the party and on the far side.
    BattleState state = battleWithParty({CharacterPropId::SABIN, CharacterPropId::CYAN});
    state.formation = formationWhere(false);
    state.gauMayAppear = true;
    const battle::BattlerSet fromSetup = battle::placeParty(state, {});
    EXPECT_TRUE(state.gauMayAppear);
    const BattlerSlot gau = BattlerSlot::character(2);
    EXPECT_TRUE(fromSetup.has(gau));
    EXPECT_EQ(state.at(gau).actor, CharacterPropId::GAU);

    BattleState recruited = battleWithParty({CharacterPropId::SABIN, CharacterPropId::GAU});
    recruited.formation = formationWhere(false);
    recruited.gauMayAppear = true;
    recruited.gauRecruited = true;
    EXPECT_TRUE(battle::placeParty(recruited, {}).empty());
    EXPECT_FALSE(recruited.gauMayAppear);

    BattleState three = battleWithParty({CharacterPropId::SABIN, CharacterPropId::CYAN, CharacterPropId::TERRA});
    three.formation = formationWhere(false);
    three.gauMayAppear = true;
    battle::placeParty(three, {});
    EXPECT_TRUE(three.gauMayAppear);

    BattleState full = battleWithParty(
        {CharacterPropId::SABIN, CharacterPropId::CYAN, CharacterPropId::TERRA, CharacterPropId::LOCKE});
    full.formation = formationWhere(false);
    full.gauMayAppear = true;
    battle::placeParty(full, {});
    EXPECT_FALSE(full.gauMayAppear);

    BattleState crowded = battleWithParty({CharacterPropId::SABIN});
    crowded.formation = formationWhere(true);
    crowded.gauMayAppear = true;
    battle::placeParty(crowded, {});
    EXPECT_FALSE(crowded.gauMayAppear);
}

TEST(PlaceParty, AScriptedSetupTakesItsCharactersSlotOrAnEmptyOne) {
    // :7807-7844, with the Veldt setup: Gau, out of the party and fighting on the far side, driven
    // by script 114 of the second block.
    BattleState state = battleWithParty({CharacterPropId::SABIN, CharacterPropId::CYAN});
    state.party.characters[7] = startingRecord(CharacterPropId::GAU);
    const battle::BattlerSet fromSetup = battle::placeParty(state, {.scriptedSetup = CharAiId::GAU_VELDT});
    const BattlerSlot gau = BattlerSlot::character(2);
    EXPECT_EQ(fromSetup, battle::BattlerSet::of(gau));
    EXPECT_EQ(state.at(gau).actor, CharacterPropId::GAU);
    EXPECT_EQ(state.at(gau).record, 7U);
    EXPECT_FALSE(state.at(gau).present);
    EXPECT_TRUE(state.at(gau).actsAsEnemy);
    EXPECT_EQ(state.at(gau).aiScript, 256 + 114);
    EXPECT_TRUE(state.at(gau).aiTakesTurns);
    EXPECT_EQ(state.at(gau).graphics, CharacterGfxId::GAU);

    // Shadow in the colosseum: an enemy beside the fighter, in the first empty slot.
    BattleState colosseum = battleWithParty({CharacterPropId::LOCKE, CharacterPropId::SHADOW});
    const battle::BattlerSet staged =
        battle::placeParty(colosseum, {.fighters = battle::ColosseumFighter{.fighter = CharacterPropId::LOCKE},
                                       .scriptedSetup = CharAiId::SHADOW_COLOSSEUM});
    EXPECT_EQ(colosseum.at(kFirst).actor, CharacterPropId::LOCKE);
    EXPECT_EQ(colosseum.at(kSecond).actor, CharacterPropId::SHADOW);
    EXPECT_TRUE(colosseum.at(kSecond).actsAsEnemy);
    EXPECT_TRUE(colosseum.at(kSecond).present);
    EXPECT_EQ(staged, battle::BattlerSet::of(kFirst) | battle::BattlerSet::of(kSecond));

    // A setup naming a plain party member takes that member's slot.
    BattleState flashback = battleWithParty({CharacterPropId::SABIN, CharacterPropId::SHADOW});
    battle::placeParty(flashback, {.scriptedSetup = CharAiId::SHADOW_COLOSSEUM});
    EXPECT_EQ(flashback.at(kSecond).actor, CharacterPropId::SHADOW);
    EXPECT_TRUE(flashback.at(kSecond).actsAsEnemy);
    EXPECT_EQ(flashback.at(BattlerSlot::character(2)).actor, std::nullopt);
}

TEST(PlaceParty, AFullLineUpLeavesASetupsNewcomerOut) {
    // :7820-7826: with every slot taken the character is not placed.
    BattleState state = battleWithParty(
        {CharacterPropId::SABIN, CharacterPropId::CYAN, CharacterPropId::TERRA, CharacterPropId::LOCKE});
    state.party.characters[7] = startingRecord(CharacterPropId::GAU);
    EXPECT_TRUE(battle::placeParty(state, {.scriptedSetup = CharAiId::GAU_VELDT}).empty());
    for (int position = 0; position < battle::kPartySlots; ++position) {
        EXPECT_NE(state.at(BattlerSlot::character(position)).actor, CharacterPropId::GAU);
    }
}

TEST(PlaceParty, ASetupThatHidesThePartyFieldsOnlyItsCast) {
    // :7800-7806: the flashback clears every slot, then places Kefka and Terra in the first two.
    BattleState state = battleWithParty({CharacterPropId::LOCKE, CharacterPropId::EDGAR, CharacterPropId::TERRA});
    state.party.characters[14] = startingRecord(CharacterPropId::KEFKA_1);
    const battle::BattlerSet fromSetup = battle::placeParty(state, {.scriptedSetup = CharAiId::TERRA_FLASHBACK});
    EXPECT_EQ(state.at(kFirst).actor, CharacterPropId::KEFKA_1);
    EXPECT_FALSE(state.at(kFirst).present);
    EXPECT_EQ(state.at(kSecond).actor, CharacterPropId::TERRA);
    EXPECT_TRUE(state.at(kSecond).present);
    EXPECT_EQ(state.at(kSecond).aiScript, 256 + 110);
    EXPECT_EQ(state.at(kSecond).graphics, state.party.characters[2].graphics);
    EXPECT_EQ(state.at(BattlerSlot::character(2)).actor, std::nullopt);
    EXPECT_EQ(fromSetup.count(), 2);
}

TEST(PlaceParty, AColosseumFighterStandsAloneInTheFirstSlot) {
    // :7748-7756.
    BattleState state = battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE});
    const battle::BattlerSet fromSetup =
        battle::placeParty(state, {.fighters = battle::ColosseumFighter{.fighter = CharacterPropId::LOCKE}});
    EXPECT_EQ(state.at(kFirst).actor, CharacterPropId::LOCKE);
    EXPECT_EQ(state.at(kFirst).record, 1U);
    EXPECT_EQ(state.at(kSecond).actor, std::nullopt);
    EXPECT_TRUE(state.colosseum);
    EXPECT_EQ(fromSetup, battle::BattlerSet::of(kFirst));
}

TEST(PlaceParty, TheFinalBattleFillsFallenSlotsFromTheReserveInOrder) {
    // :7721-7747.
    BattleState state = battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::CELES,
                                         CharacterPropId::EDGAR, CharacterPropId::SABIN});
    state.finalBattleReserve = {3, 4};
    battle::FinalBattleStage stage;
    stage.standing[0] = CharacterPropId::TERRA;
    stage.standing[2] = CharacterPropId::CELES;
    battle::placeParty(state, {.fighters = stage});
    EXPECT_EQ(state.at(kFirst).actor, CharacterPropId::TERRA);
    EXPECT_EQ(state.at(kSecond).actor, CharacterPropId::EDGAR);
    EXPECT_EQ(state.at(BattlerSlot::character(2)).actor, CharacterPropId::CELES);
    EXPECT_EQ(state.at(BattlerSlot::character(3)).actor, CharacterPropId::SABIN);
    EXPECT_TRUE(state.finalBattleReserve.empty());
}

// --- loading a party member ---------------------------------------------------------------------

TEST(LoadPartyMember, HpAndMpComeFromTheRecordWithBoostedCappedMaximums) {
    // LoadCharProp :6605-6621.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::CELES);
    record.hp = 1200;
    record.maxHp = 9000;
    record.mp = 300;
    record.maxMp = 900;
    record.level = 40;
    record.relic1 = ItemId::MUSCLE_BELT;
    record.relic2 = ItemId::CRYSTAL_ORB;
    BattleState state = loadedBattle(record);
    battle::loadPartyMember(state, kFirst, battle::deriveEquipment(record), false);
    EXPECT_EQ(state.at(kFirst).hp, 1200);
    EXPECT_EQ(state.at(kFirst).mp, 300);
    EXPECT_EQ(state.at(kFirst).maxHp, 9999);
    EXPECT_EQ(state.at(kFirst).maxMp, 999);
    EXPECT_EQ(state.at(kFirst).level, 40);

    record.relic1 = ItemId::GREEN_BERET;
    record.relic2 = ItemId::EMPTY;
    record.maxHp = 800;
    state.party.characters[0] = record;
    battle::loadPartyMember(state, kFirst, battle::deriveEquipment(record), false);
    EXPECT_EQ(state.at(kFirst).maxHp, 900);
    EXPECT_EQ(state.at(kFirst).maxMp, 900);
}

TEST(LoadPartyMember, ACharacterASetupBringsInIsHealedAndKeepsOnlyFourStatuses) {
    // :6622-6631: full HP and MP; the record keeps Blind, Poison, Magitek and Imp of its first eight.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::TERRA);
    record.hp = 1;
    record.mp = 0;
    record.status = StatusSet::of(StatusId::BLIND, StatusId::ZOMBIE, StatusId::POISON, StatusId::MAGITEK,
                                  StatusId::INVISIBLE, StatusId::IMP, StatusId::PETRIFY, StatusId::FLOAT);
    BattleState state = loadedBattle(record);
    battle::loadPartyMember(state, kFirst, battle::deriveEquipment(record), true);
    EXPECT_EQ(state.at(kFirst).hp, state.at(kFirst).maxHp);
    EXPECT_EQ(state.at(kFirst).mp, state.at(kFirst).maxMp);
    const StatusSet kept = state.party.characters[0].status;
    EXPECT_EQ(kept.bytes, StatusSet::of(StatusId::BLIND, StatusId::POISON, StatusId::MAGITEK, StatusId::IMP,
                                        StatusId::FLOAT)
                              .bytes);
}

TEST(LoadPartyMember, TheRecordAndTheEquipmentGiveTheStatusesTheBattleStartsWith) {
    // :6632-6659: the record's first eight and its Interceptor and Float, the equipment's statuses,
    // and an item's Float moved to where the battle keeps Float.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::SHADOW);
    record.status = StatusSet::of(StatusId::POISON, StatusId::INTERCEPTOR);
    const ItemId floats = firstAnyItem([](const ItemProperties& p) { return p.status3Granted.has(StatusId::DANCE); });
    record.relic1 = floats;
    BattleState state = loadedBattle(record);
    const DerivedStats stats = battle::deriveEquipment(record);
    battle::applyDerivedStats(state, kFirst, stats);
    battle::loadPartyMember(state, kFirst, stats, false);
    const StatusSet& innate = state.at(kFirst).innateStatuses;
    EXPECT_TRUE(innate.has(StatusId::POISON));
    EXPECT_TRUE(innate.has(StatusId::INTERCEPTOR));
    EXPECT_TRUE(innate.has(StatusId::FLOAT));
    EXPECT_FALSE(innate.has(StatusId::DANCE));
}

TEST(LoadPartyMember, MagitekArmorLeavesMimicWithMagitekFireBeam) {
    // :6642-6647.
    game::CharacterRecord record = unequippedRecord(CharacterPropId::TERRA);
    record.status.set(StatusId::MAGITEK);
    BattleState state = loadedBattle(record);
    battle::loadPartyMember(state, kFirst, battle::deriveEquipment(record), false);
    EXPECT_EQ(state.mimic.last.command, BattleCommandId::MAGITEK);
    EXPECT_EQ(state.mimic.last.attack, AttackId::FIRE_BEAM);
}

TEST(LoadPartyMember, EquipmentThatCondemnsKeepsTheCountGoing) {
    // :6632-6639: an equipment Condemned cancels the pending stop of the count.
    BattleState state = loadedBattle(unequippedRecord(CharacterPropId::TERRA));
    state.at(kFirst).pendingUpdates.stopCondemned = true;
    state.at(kFirst).equipmentStatuses.set(StatusId::CONDEMNED);
    battle::loadPartyMember(state, kFirst, DerivedStats{}, false);
    EXPECT_FALSE(state.at(kFirst).pendingUpdates.stopCondemned);
}

// --- commands -----------------------------------------------------------------------------------

BattleState commandBattle(CharacterPropId actor, std::array<BattleCommandId, 4> commands) {
    game::CharacterRecord record = unequippedRecord(actor);
    record.commands = commands;
    BattleState state = loadedBattle(record);
    state.at(kFirst).mp = 50;
    state.at(kFirst).maxMp = 50;
    return state;
}

std::array<BattleCommandId, 4> menu(const BattleState& state) {
    std::array<BattleCommandId, 4> result{};
    for (std::size_t row = 0; row < result.size(); ++row) result[row] = state.at(kFirst).commands[row].command;
    return result;
}

TEST(BuildCommandList, EachRowTakesItsCommandsTargetingAndAnEmptyRowStaysEmpty) {
    // InitCmdList :13785-13796.
    BattleState state = commandBattle(CharacterPropId::LOCKE, {BattleCommandId::FIGHT, BattleCommandId::STEAL,
                                                               BattleCommandId::NONE, BattleCommandId::ITEM});
    battle::buildCommandList(state, kFirst);
    const battle::MenuCommand& steal = state.at(kFirst).commands[1];
    EXPECT_EQ(steal.command, BattleCommandId::STEAL);
    EXPECT_TRUE(steal.usable);
    EXPECT_EQ(steal.targeting.bits, battleCommandProperties(BattleCommandId::STEAL).targeting.bits);
    EXPECT_EQ(state.at(kFirst).commands[2].command, BattleCommandId::NONE);
    EXPECT_FALSE(state.at(kFirst).commands[2].usable);
    EXPECT_EQ(state.at(kFirst).commands[2].targeting.bits, 0);
}

TEST(BuildCommandList, MagitekArmorKeepsMagicItemAndMimicAndTurnsFightIntoMagitek) {
    // :13710-13739.
    BattleState state = commandBattle(CharacterPropId::TERRA, {BattleCommandId::FIGHT, BattleCommandId::MAGIC,
                                                               BattleCommandId::MORPH, BattleCommandId::ITEM});
    state.party.characters[0].status.set(StatusId::MAGITEK);
    state.at(kFirst).spellCount = 1;
    battle::buildCommandList(state, kFirst);
    EXPECT_EQ(menu(state), (std::array{BattleCommandId::MAGITEK, BattleCommandId::MAGIC, BattleCommandId::NONE,
                                       BattleCommandId::ITEM}));

    // Gau's Rage is his Fight.
    BattleState gau = commandBattle(CharacterPropId::GAU, {BattleCommandId::RAGE, BattleCommandId::LEAP,
                                                           BattleCommandId::MIMIC, BattleCommandId::ITEM});
    gau.party.characters[0].status.set(StatusId::MAGITEK);
    battle::buildCommandList(gau, kFirst);
    EXPECT_EQ(menu(gau), (std::array{BattleCommandId::MAGITEK, BattleCommandId::NONE, BattleCommandId::MIMIC,
                                     BattleCommandId::ITEM}));
}

TEST(BuildCommandList, WhereOnlyMagicWorksFightBecomesMagicAndMagicGoes) {
    // :13740-13751.
    BattleState state = commandBattle(CharacterPropId::CELES, {BattleCommandId::FIGHT, BattleCommandId::RUNIC,
                                                               BattleCommandId::MAGIC, BattleCommandId::ITEM});
    state.magicOnly = true;
    state.at(kFirst).spellCount = 1;
    battle::buildCommandList(state, kFirst);
    EXPECT_EQ(menu(state), (std::array{BattleCommandId::MAGIC, BattleCommandId::NONE, BattleCommandId::NONE,
                                       BattleCommandId::ITEM}));
}

TEST(BuildCommandList, RelicsSwapTheirCommands) {
    // :13758-13774 with RelicCmdTbl1/2.
    const std::pair<ItemId, std::pair<BattleCommandId, BattleCommandId>> swaps[] = {
        {ItemId::DRAGOONBOOTS, {BattleCommandId::FIGHT, BattleCommandId::JUMP}},
        {ItemId::GEM_BOX, {BattleCommandId::MAGIC, BattleCommandId::X_MAGIC}},
        {ItemId::FAKEMUSTACHE, {BattleCommandId::SKETCH, BattleCommandId::CONTROL}},
        {ItemId::COIN_TOSS, {BattleCommandId::SLOT, BattleCommandId::GP_RAIN}},
        {ItemId::THIEF_GLOVE, {BattleCommandId::STEAL, BattleCommandId::CAPTURE}},
    };
    for (const auto& [relic, swap] : swaps) {
        BattleState state = commandBattle(CharacterPropId::GOGO, {swap.first, BattleCommandId::NONE,
                                                                  BattleCommandId::NONE, BattleCommandId::NONE});
        state.at(kFirst).spellCount = 1;
        state.at(kFirst).relicEffects2 = item(relic).relicEffects2;
        battle::buildCommandList(state, kFirst);
        EXPECT_EQ(state.at(kFirst).commands[0].command, swap.second) << static_cast<int>(relic);
    }
}

TEST(BuildCommandList, MorphNeedsTheStoryToAllowItAndAPermanentMorphTakesTheCommandAway) {
    // InitCmd_00 :13813-13826.
    BattleState state = commandBattle(CharacterPropId::TERRA, {BattleCommandId::FIGHT, BattleCommandId::MORPH,
                                                               BattleCommandId::NONE, BattleCommandId::ITEM});
    battle::buildCommandList(state, kFirst);
    EXPECT_EQ(state.at(kFirst).commands[1].command, BattleCommandId::NONE);

    state.morphAvailable = true;
    battle::buildCommandList(state, kFirst);
    EXPECT_EQ(state.at(kFirst).commands[1].command, BattleCommandId::MORPH);

    state.permanentMorph = true;
    battle::buildCommandList(state, kFirst);
    EXPECT_EQ(state.at(kFirst).commands[1].command, BattleCommandId::NONE);
    EXPECT_TRUE(state.at(kFirst).innateStatuses.has(StatusId::MORPH));
    EXPECT_EQ(state.party.morphSupply, 255);
}

TEST(BuildCommandList, LeapNeedsTheVeldtAndDanceADance) {
    // InitCmd_01 :13856-13859, InitCmd_02 :13848-13850.
    BattleState state = commandBattle(CharacterPropId::GOGO, {BattleCommandId::LEAP, BattleCommandId::DANCE,
                                                              BattleCommandId::NONE, BattleCommandId::NONE});
    battle::buildCommandList(state, kFirst);
    EXPECT_EQ(state.at(kFirst).commands[0].command, BattleCommandId::NONE);
    EXPECT_EQ(state.at(kFirst).commands[1].command, BattleCommandId::NONE);
    state.onTheVeldt = true;
    state.party.dancesKnown.set(0);
    battle::buildCommandList(state, kFirst);
    EXPECT_EQ(state.at(kFirst).commands[0].command, BattleCommandId::LEAP);
    EXPECT_EQ(state.at(kFirst).commands[1].command, BattleCommandId::DANCE);
}

TEST(BuildCommandList, MagicNeedsASpellOrAnEsperAndOnlyMagicOrLoreKeepsMp) {
    // InitCmd_03/04 :13832-13842, InitCmd_05 :13865-13868, and :13797-13803.
    BattleState state = commandBattle(CharacterPropId::CELES, {BattleCommandId::FIGHT, BattleCommandId::MAGIC,
                                                               BattleCommandId::NONE, BattleCommandId::ITEM});
    battle::buildCommandList(state, kFirst);
    EXPECT_EQ(state.at(kFirst).commands[1].command, BattleCommandId::NONE);
    EXPECT_EQ(state.at(kFirst).mp, 0);
    EXPECT_EQ(state.at(kFirst).maxMp, 0);

    state = commandBattle(CharacterPropId::CELES, {BattleCommandId::FIGHT, BattleCommandId::MAGIC,
                                                   BattleCommandId::NONE, BattleCommandId::ITEM});
    state.at(kFirst).spells.esper = EsperId::SHIVA;
    battle::buildCommandList(state, kFirst);
    EXPECT_EQ(state.at(kFirst).commands[1].command, BattleCommandId::MAGIC);
    EXPECT_EQ(state.at(kFirst).mp, 50);

    state = commandBattle(CharacterPropId::STRAGO, {BattleCommandId::FIGHT, BattleCommandId::LORE,
                                                    BattleCommandId::NONE, BattleCommandId::ITEM});
    battle::buildCommandList(state, kFirst);
    EXPECT_EQ(state.at(kFirst).commands[1].command, BattleCommandId::LORE);
    EXPECT_EQ(state.at(kFirst).maxMp, 50);
}

// --- the whole party ----------------------------------------------------------------------------

TEST(LoadParty, EveryPlacedMemberIsLoadedAndCountedPresent) {
    // InitChars :6201-6222.
    BattleState state = battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE});
    state.party.membership[1].slot = 2;
    const battle::BattlerSet fromSetup = battle::placeParty(state, {});
    battle::loadParty(state, fromSetup, {});
    EXPECT_TRUE(state.partyPresentAtStart.has(kFirst));
    EXPECT_TRUE(state.partyPresentAtStart.has(BattlerSlot::character(2)));
    EXPECT_FALSE(state.partyPresentAtStart.has(kSecond));
    const battle::Battler& locke = state.at(BattlerSlot::character(2));
    EXPECT_EQ(locke.hp, state.party.characters[1].hp);
    EXPECT_EQ(locke.commands[0].command, state.party.characters[1].commands[0]);
    EXPECT_GT(locke.vigor, 0);
}

// --- the battle inventory -----------------------------------------------------------------------

TEST(BattleInventory, EachItemCarriesItsMenusFlagsTargetingAndQuantity) {
    // LoadItemProp :13981-14020.
    BattleState state = battleWithParty({CharacterPropId::LOCKE});
    battle::placeParty(state, {});
    const battle::BattleItem potion = battle::battleItem(state, ItemId::POTION, 7);
    EXPECT_TRUE(potion.usableInBattle);
    EXPECT_EQ(potion.quantity, 7);
    EXPECT_EQ(potion.targeting.bits, item(ItemId::POTION).targeting.bits);
    EXPECT_FALSE(potion.isWeapon);

    const battle::BattleItem star = battle::battleItem(state, ItemId::NINJA_STAR, 1);
    EXPECT_TRUE(star.throwable);

    const battle::BattleItem drill = battle::battleItem(state, ItemId::DRILL, 1);
    EXPECT_TRUE(drill.isTool);
    EXPECT_EQ(drill.equippableBy.bits, 0);

    const battle::BattleItem dirk = battle::battleItem(state, ItemId::DIRK, 1);
    EXPECT_TRUE(dirk.isWeapon);
    EXPECT_TRUE(dirk.equippableBy.has(0));  // Locke can hold a Dirk
    const battle::BattleItem buckler = battle::battleItem(state, ItemId::BUCKLER, 1);
    EXPECT_TRUE(buckler.isShield);

    // :14016-14020: armor, helmets and relics skip the equippable-by check.
    EXPECT_EQ(battle::battleItem(state, ItemId::MITHRIL_VEST, 1).equippableBy.bits, 0);
    EXPECT_FALSE(battle::battleItem(state, ItemId::EMPTY, 3).usableInBattle);
}

TEST(BattleInventory, ThePartysItemsAndEachMembersHandsAndTheToolsOwned) {
    // InitInventory :13911-13956.
    BattleState state = battleWithParty({CharacterPropId::EDGAR});
    state.party.characters[0].weapon = ItemId::DIRK;
    state.party.characters[0].shield = ItemId::BUCKLER;
    state.party.inventory[0] = {.item = ItemId::POTION, .quantity = 5};
    state.party.inventory[9] = {.item = ItemId::DRILL, .quantity = 1};
    battle::placeParty(state, {});
    battle::loadBattleInventory(state);
    EXPECT_EQ(state.inventory.weaponHands[0].item, ItemId::DIRK);
    EXPECT_EQ(state.inventory.weaponHands[0].quantity, 1);
    EXPECT_EQ(state.inventory.shieldHands[0].item, ItemId::BUCKLER);
    EXPECT_EQ(state.inventory.items[0].item, ItemId::POTION);
    EXPECT_EQ(state.inventory.items[0].quantity, 5);
    EXPECT_EQ(state.inventory.items[9].item, ItemId::DRILL);
    EXPECT_TRUE(state.inventory.tools.has(ItemId::DRILL));
    EXPECT_FALSE(state.inventory.tools.has(ItemId::NOISEBLASTER));
    EXPECT_EQ(state.inventory.weaponHands[1].item, ItemId::EMPTY);
}

TEST(KnownSkills, CountsDancesAndRagesUpTo254) {
    // InitSkills :14473-14513.
    BattleState state;
    state.party.swdtechsKnown = AbilityLearnedSet{0x07};
    state.party.blitzesKnown = AbilityLearnedSet{0x1F};
    state.party.dancesKnown.set(1);
    state.party.dancesKnown.set(7);
    state.party.ragesKnown.set(0);
    state.party.ragesKnown.set(64);
    state.party.ragesKnown.set(254);
    state.party.ragesKnown.set(255);
    battle::loadKnownSkills(state);
    EXPECT_EQ(state.swdtechsKnown, 3);
    EXPECT_EQ(state.blitzesKnown, 5);
    EXPECT_EQ(state.dancesKnown[0], std::nullopt);
    EXPECT_EQ(state.dancesKnown[1], DanceId::FOREST_SUITE);
    EXPECT_EQ(state.dancesKnown[7], DanceId::SNOWMAN_JAZZ);
    EXPECT_EQ(state.ragesKnown,
              (std::vector{static_cast<MonsterId>(0), static_cast<MonsterId>(64), static_cast<MonsterId>(254)}));
}

// --- the loop's equipment step --------------------------------------------------------------------

TEST(EquipmentChanges, TheItemMenusSwapIsWrittenToTheRecordAndTheMemberReDerived) {
    // UpdateCharProp :5595-5631.
    BattleState state = battleWithParty({CharacterPropId::LOCKE});
    state.party.characters[0].weapon = ItemId::EMPTY;
    state.party.characters[0].shield = ItemId::EMPTY;
    battle::placeParty(state, {});
    battle::loadParty(state, {}, {});
    battle::loadBattleInventory(state);
    const std::uint8_t bare = state.at(kFirst).weaponHand.battlePower;

    state.inventory.weaponHands[0] = battle::battleItem(state, ItemId::DIRK, 1);
    state.at(kFirst).status.set(StatusId::POISON);
    state.at(kFirst).equipmentChanged = true;
    RecordingExecutor executor;
    battle::applyEquipmentChanges(state, executor);

    EXPECT_EQ(state.party.characters[0].weapon, ItemId::DIRK);
    EXPECT_TRUE(state.party.characters[0].status.has(StatusId::POISON));
    EXPECT_FALSE(state.at(kFirst).equipmentChanged);
    EXPECT_EQ(state.at(kFirst).weaponHand.battlePower,
              getCharacterBaseStats(CharacterPropId::LOCKE).battlePower + item(ItemId::DIRK).power);
    EXPECT_NE(state.at(kFirst).weaponHand.battlePower, bare);
}

TEST(EquipmentChanges, RunAtTheStartOfEveryLoopStep) {
    // BattleLoop :67-80: the change is taken in before the menus refresh and the line is served.
    BattleState state = battleWithParty({CharacterPropId::LOCKE});
    state.party.characters[0].weapon = ItemId::EMPTY;
    battle::placeParty(state, {});
    battle::loadParty(state, {}, {});
    battle::loadBattleInventory(state);
    state.at(kFirst).present = true;
    state.inventory.weaponHands[0] = battle::battleItem(state, ItemId::DIRK, 1);
    state.at(kFirst).equipmentChanged = true;
    RecordingExecutor executor;
    battle::stepBattle(state, executor);
    EXPECT_EQ(state.party.characters[0].weapon, ItemId::DIRK);
}

}  // namespace
}  // namespace ostinato::test
