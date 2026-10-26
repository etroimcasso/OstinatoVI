#include "battle/party_setup.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "battle/action_executor.h"
#include "data/battle_commands.h"
#include "data/battle_tables.h"
#include "data/char_ai.h"
#include "data/character.h"
#include "data/formations.h"
#include "data/item_properties.h"

namespace ostinato::battle {

namespace {

constexpr int kStatMax = 255;
constexpr int kMaxHp = 9999;
constexpr int kMaxMp = 999;

// The fourteen playable characters, Terra to Umaro, each have a bit of an item's equippable-by
// mask of their own.
constexpr int kCharactersWithEquipBits = 14;

// A character's run-factor trait (0-3) gives the most one escape attempt can add: 5 down to 2.
constexpr int kLeastRunFactor = 2;
constexpr int kLargestRunTrait = 3;

// A scripted setup's characters follow the second block of AI scripts.
constexpr std::uint16_t kCharacterAiScripts = 256;
constexpr std::uint8_t kNoAiScript = 0xFF;

// The record status bits a character a scripted setup brings in keeps.
constexpr StatusSet kStatusesKeptWhenJoining =
    StatusSet::of(StatusId::BLIND, StatusId::POISON, StatusId::MAGITEK, StatusId::IMP);

// The statuses the record carries between battles.
constexpr StatusSet kRecordStatuses =
    StatusSet::of(StatusId::BLIND, StatusId::ZOMBIE, StatusId::POISON, StatusId::MAGITEK, StatusId::INVISIBLE,
                  StatusId::IMP, StatusId::PETRIFY, StatusId::DEAD, StatusId::INTERCEPTOR, StatusId::FLOAT);

// The first eight statuses: the ones the battle hands back to the record when equipment changes.
constexpr StatusSet kFirstStatusByte =
    StatusSet::of(StatusId::BLIND, StatusId::ZOMBIE, StatusId::POISON, StatusId::MAGITEK, StatusId::INVISIBLE,
                  StatusId::IMP, StatusId::PETRIFY, StatusId::DEAD);

// Equipment that keeps one of these on its wearer also stops it being put on.
constexpr StatusSet kEquipmentHeldStatuses =
    StatusSet::of(StatusId::SILENCE, StatusId::BERSERK, StatusId::CONFUSE, StatusId::SAP, StatusId::REGEN,
                  StatusId::SLOW, StatusId::HASTE, StatusId::SHELL, StatusId::PROTECT, StatusId::REFLECT);

std::uint8_t saturatingAdd(std::uint8_t a, std::uint8_t b) {
    return static_cast<std::uint8_t>(std::min(kStatMax, a + b));
}

int clampStat(int value) { return std::clamp(value, 0, kStatMax); }

StatusSet unionOf(StatusSet a, const StatusSet& b) {
    for (std::size_t i = 0; i < a.bytes.size(); ++i) {
        a.bytes[i] = static_cast<std::uint8_t>(a.bytes[i] | b.bytes[i]);
    }
    return a;
}

StatusSet intersectionOf(StatusSet a, const StatusSet& b) {
    for (std::size_t i = 0; i < a.bytes.size(); ++i) {
        a.bytes[i] = static_cast<std::uint8_t>(a.bytes[i] & b.bytes[i]);
    }
    return a;
}

// `to` takes `from`'s statuses among `which`, keeping its own others.
StatusSet replaceStatuses(const StatusSet& to, const StatusSet& from, const StatusSet& which) {
    return unionOf(to.without(which), intersectionOf(from, which));
}

ElementSet unionOf(ElementSet a, ElementSet b) { return ElementSet{static_cast<std::uint8_t>(a.bits | b.bits)}; }

template <typename Flags>
Flags unionOf(Flags a, Flags b) {
    return Flags{static_cast<std::uint8_t>(a.bits | b.bits)};
}

// The status bytes of an item record, as statuses of the whole status space. An item's third
// status byte grants Float with its first bit; there is no other way for an item to give Float.
StatusSet statusesOf(Status1Set first) { return StatusSet{{first.bits, 0, 0, 0}}; }
StatusSet statusesOf(Status2Set second) { return StatusSet{{0, second.bits, 0, 0}}; }
StatusSet statusesOf(Status3Set third) {
    StatusSet result{{0, 0, third.bits, 0}};
    if (result.has(StatusId::DANCE)) {
        result.clear(StatusId::DANCE);
        result.set(StatusId::FLOAT);
    }
    return result;
}

enum class HandContents : std::uint8_t {
    Other,    // a shield, or anything that is not a weapon
    Weapon,
    BareHand,
};

MaxBoost boostFrom(RelicEffect1Set effects, RelicEffect1 plus50, RelicEffect1 plus25, RelicEffect1 plus12Point5) {
    if (effects.has(plus50)) return MaxBoost::Plus50Percent;
    if (effects.has(plus25)) return MaxBoost::Plus25Percent;
    if (effects.has(plus12Point5)) return MaxBoost::Plus12Point5Percent;
    return MaxBoost::None;
}

// What one equipped item adds. `slot` is its place in CharacterRecord::equipment().
void addItem(DerivedStats& stats, std::array<HandContents, 2>& hands, ItemId item, std::size_t slot, bool imp) {
    const ItemProperties& properties = getItemProperties(item);

    stats.protection = unionOf(stats.protection, unionOf(statusesOf(properties.status1Protection),
                                                         statusesOf(properties.status2Protection)));
    stats.equipmentStatuses = unionOf(stats.equipmentStatuses, statusesOf(properties.status3Granted));
    stats.relicEffects1 = unionOf(stats.relicEffects1, properties.relicEffects1);
    stats.relicEffects2 = unionOf(stats.relicEffects2, properties.relicEffects2);
    stats.relicEffects3 = unionOf(stats.relicEffects3, properties.relicEffects3);
    stats.relicEffects4 = unionOf(stats.relicEffects4, properties.relicEffects4);
    stats.relicEffects5 = unionOf(stats.relicEffects5, properties.relicEffects5);

    stats.vigor += properties.vigorSpeed.first();
    stats.speed += properties.vigorSpeed.second();
    stats.stamina += properties.staminaMagicPower.first();
    stats.magicPower += properties.staminaMagicPower.second();
    stats.evade += equipEvadeBoost(properties.evadeMagicBlock.evadeIndex());
    stats.magicBlock += equipEvadeBoost(properties.evadeMagicBlock.mblockIndex());

    // Imp gear works only on an Imp, and other gear only on someone who is not: worn the wrong
    // way round, its power counts as 1.
    const bool fits = properties.equippableBy.impGear() == imp;
    const std::uint8_t power = fits ? properties.power : 1;

    if (properties.typeAndUsage.type() != ItemType::WEAPON) {
        stats.equipmentStatuses = unionOf(stats.equipmentStatuses, statusesOf(properties.status2Set));
        stats.elementsHalved = unionOf(stats.elementsHalved, properties.element);
        stats.elementsWeak = unionOf(stats.elementsWeak, properties.elementsWeak);
        stats.elementsAbsorbed = unionOf(stats.elementsAbsorbed, properties.elementsAbsorbed);
        stats.elementsNullified = unionOf(stats.elementsNullified, properties.elementsNullified);
        stats.defense = saturatingAdd(stats.defense, power);
        stats.magicDefense = saturatingAdd(stats.magicDefense, properties.hitRateOrDefense);
    } else if (slot < hands.size()) {
        // A weapon, or a bare hand, in one of the two hands.
        hands[slot] = item == ItemId::EMPTY ? HandContents::BareHand : HandContents::Weapon;
        Hand& hand = slot == 0 ? stats.weaponHand : stats.shieldHand;
        hand.element = properties.element;
        hand.battlePower = saturatingAdd(hand.battlePower, power);
        hand.hitRate = properties.hitRateOrDefense;
        hand.spellCast = properties.spellCast;
        hand.weaponFlags = properties.weaponFlags;
    }

    // An item that raises magic damage gives the single effect; a second one gives the double.
    if (stats.relicEffects1.has(RelicEffect1::RAISE_MAGIC_DAMAGE)) {
        stats.relicEffects1.clear(RelicEffect1::RAISE_MAGIC_DAMAGE);
        if (stats.relicEffects3.has(RelicEffect3::RAISE_MAGIC_DAMAGE)) {
            stats.relicEffects1.set(RelicEffect1::RAISE_MAGIC_DAMAGE);
        }
        stats.relicEffects3.set(RelicEffect3::RAISE_MAGIC_DAMAGE);
    }

    const ItemSpecialEffect block = properties.specialEffect;
    if (slot == 0) stats.weaponHand.blockEffect = block;
    if (slot == 1) stats.shieldHand.blockEffect = block;
    if (block.blocksPhysical()) stats.physicalBlockGraphics.add(block.blockGraphic());
    if (block.blocksMagic()) stats.magicBlockGraphics.add(block.blockGraphic());
}

void loseTwoHanded(DerivedStats& stats) {
    stats.weaponHand.weaponFlags.clear(WeaponFlags::TWO_HAND);
    stats.shieldHand.weaponFlags.clear(WeaponFlags::TWO_HAND);
}

// Which hand attacks, given what each holds.
void settleHands(DerivedStats& stats, const std::array<HandContents, 2>& hands) {
    const HandContents weaponSlot = hands[0];
    const HandContents shieldSlot = hands[1];
    if (weaponSlot == HandContents::Weapon && shieldSlot == HandContents::Weapon) {
        stats.twoWeapons = true;
        loseTwoHanded(stats);
    } else if (weaponSlot == HandContents::Other && shieldSlot != HandContents::Other) {
        loseTwoHanded(stats);
        stats.weaponHand.battlePower = 0;
    } else if (weaponSlot == HandContents::BareHand && shieldSlot == HandContents::Weapon) {
        stats.weaponHand.battlePower = 0;
    } else if (weaponSlot == HandContents::Weapon && shieldSlot == HandContents::BareHand) {
        stats.shieldHand.battlePower = 0;
    } else if (weaponSlot != HandContents::Other) {
        // A weapon or a bare hand with a shield, or two bare hands.
        loseTwoHanded(stats);
        stats.shieldHand.battlePower = 0;
    }
}

RelicEffect2 relicSwapFlag(BattleCommandId from) {
    switch (from) {
        case BattleCommandId::FIGHT: return RelicEffect2::FIGHT_TO_JUMP;
        case BattleCommandId::MAGIC: return RelicEffect2::MAGIC_TO_X_MAGIC;
        case BattleCommandId::SKETCH: return RelicEffect2::SKETCH_TO_CONTROL;
        case BattleCommandId::SLOT: return RelicEffect2::SLOT_TO_GP_RAIN;
        default: return RelicEffect2::STEAL_TO_CAPTURE;
    }
}

// --- placing the party ---------------------------------------------------------------------------

// Who a party slot holds while the party is being placed.
struct Placement {
    CharacterPropId actor = CharacterPropId::TERRA;
    bool notInParty = false;
    bool enemy = false;
    std::optional<CharacterGfxId> graphics;  // a scripted setup's sprite in place of the record's
    std::optional<std::uint16_t> aiScript;

    // A plain party member, as a scripted setup matches them.
    bool isPlain(CharacterPropId other) const { return !notInParty && !enemy && actor == other; }
};

using Placements = std::array<std::optional<Placement>, kPartySlots>;

Placements currentParty(BattleState& state) {
    Placements placed{};
    int partySize = 0;
    for (std::size_t record = game::kCharacterRecords; record-- > 0;) {
        const game::PartyMembership& member = state.party.membership[record];
        if (member.party != state.party.currentParty) {
            continue;
        }
        ++partySize;
        placed[member.slot] = Placement{.actor = state.party.characters[record].actor};
    }
    // Gau can only come back from the Veldt to a party with room for him, when he has not already
    // joined, and when the battle leaves the last monster slot empty.
    const bool lastMonsterSlotUsed = !getFormation(state.formation).slotEmpty(kMonsterSlots - 1);
    if (state.gauRecruited || lastMonsterSlotUsed || partySize >= kPartySlots) {
        state.gauMayAppear = false;
    }
    return placed;
}

Placements finalBattleParty(BattleState& state, const FinalBattleStage& stage) {
    Placements placed{};
    for (int slot = 0; slot < kPartySlots; ++slot) {
        if (stage.standing[slot]) {
            placed[slot] = Placement{.actor = *stage.standing[slot]};
        } else if (!state.finalBattleReserve.empty()) {
            const std::size_t record = state.finalBattleReserve.front();
            state.finalBattleReserve.pop_front();
            placed[slot] = Placement{.actor = state.party.characters[record].actor};
        }
    }
    return placed;
}

void stageScriptedSetup(Placements& placed, BattlerSet& fromSetup, CharAiId id) {
    const CharAiSetup& setup = charAiSetup(id);
    if (setup.flags.hidesPartyCharacters()) {
        placed = Placements{};
    }
    for (const CharAiSlot& cast : setup.slots) {
        if (cast.isEmpty()) {
            continue;
        }
        const CharacterPropId actor = cast.character.characterProperties();
        auto at = std::find_if(placed.begin(), placed.end(),
                               [&](const std::optional<Placement>& p) { return p && p->isPlain(actor); });
        if (at == placed.end()) {
            at = std::find_if(placed.begin(), placed.end(), [](const std::optional<Placement>& p) { return !p; });
        }
        if (at == placed.end()) {
            continue;  // every slot is taken: this character sits the battle out
        }
        const int slot = static_cast<int>(at - placed.begin());
        fromSetup.add(BattlerSlot::character(slot));
        *at = Placement{
            .actor = actor,
            .notInParty = cast.character.isNotInParty(),
            .enemy = cast.character.isEnemy(),
            .graphics = cast.graphics.usesCharacterDefault() ? std::nullopt
                                                              : std::optional<CharacterGfxId>(cast.graphics.id()),
            .aiScript = cast.aiScript == kNoAiScript
                            ? std::nullopt
                            : std::optional<std::uint16_t>(kCharacterAiScripts + cast.aiScript),
        };
    }
}

// The equippable-by bits a character's items must share. A character beyond the fourteen with a
// bit of their own (Banon, Leo, the ghosts and the other guests) gets the last two letters of
// their name instead.
EquipPermissions equipBitsFor(const game::CharacterRecord& record) {
    const int actor = static_cast<int>(record.actor);
    if (actor < kCharactersWithEquipBits) {
        const auto bits = static_cast<std::uint16_t>(1u << actor);
        return EquipPermissions{static_cast<std::uint8_t>(bits & 0xFF), static_cast<std::uint8_t>(bits >> 8)};
    }
    // LEGACY VERBATIM (docs/Bugs.md): the original takes the word it last read, the name's fifth
    // and sixth letters.
    return EquipPermissions{record.name[4], record.name[5]};
}

void seat(BattleState& state, BattlerSlot slot, const Placement& placement) {
    Battler& battler = state.at(slot);
    battler.present = !placement.notInParty;
    battler.actsAsEnemy = placement.enemy;
    battler.actor = placement.actor;
    if (placement.aiScript) {
        battler.aiScript = placement.aiScript;
        battler.aiTakesTurns = true;
    }
    for (std::size_t index = game::kCharacterRecords; index-- > 0;) {
        const game::CharacterRecord& record = state.party.characters[index];
        if (record.actor != placement.actor) {
            continue;
        }
        battler.backRow = state.party.membership[index].backRow;
        battler.record = index;
        battler.graphics = placement.graphics.value_or(record.graphics);
        battler.equipsAs = equipBitsFor(record);
    }
}

}  // namespace

std::uint8_t hitCheckFactor(int rating) {
    if (rating == 0) return kStatMax;
    if (rating >= 128) return 1;
    return static_cast<std::uint8_t>(256 - 2 * rating);
}

// --- equipment -----------------------------------------------------------------------------------

DerivedStats deriveEquipment(const game::CharacterRecord& record) {
    const CharacterBaseStats& base = getCharacterBaseStats(record.actor);
    DerivedStats stats;
    stats.weaponHand.item = record.weapon;
    stats.shieldHand.item = record.shield;
    stats.armor = record.armor;
    stats.relics = {record.relic1, record.relic2};
    stats.weaponHand.battlePower = base.battlePower;
    stats.shieldHand.battlePower = base.battlePower;
    stats.defense = base.defense;
    stats.magicDefense = base.magicDefense;
    stats.evade = base.evade;
    stats.magicBlock = base.magicBlock;
    stats.runFactor = static_cast<std::uint8_t>(
        kLeastRunFactor + kLargestRunTrait - static_cast<int>(base.traits.runFactor()));
    stats.vigor = record.vigor;
    stats.speed = record.speed;
    stats.stamina = record.stamina;
    stats.magicPower = record.magicPower;

    // The relics are worn first and the weapon last; the order decides which of two items that
    // raise magic damage gives the single effect.
    const bool imp = record.status.has(StatusId::IMP);
    const auto equipment = record.equipment();
    std::array<HandContents, 2> hands{HandContents::Other, HandContents::Other};
    for (std::size_t slot = equipment.size(); slot-- > 0;) {
        addItem(stats, hands, equipment[slot], slot, imp);
    }

    stats.hpBoost = boostFrom(stats.relicEffects1, RelicEffect1::HP_PLUS_50, RelicEffect1::HP_PLUS_25,
                              RelicEffect1::HP_PLUS_12_5);
    stats.mpBoost = boostFrom(stats.relicEffects1, RelicEffect1::MP_PLUS_50, RelicEffect1::MP_PLUS_25,
                              RelicEffect1::MP_PLUS_12_5);

    stats.vigor = clampStat(stats.vigor);
    stats.speed = clampStat(stats.speed);
    stats.stamina = clampStat(stats.stamina);
    stats.magicPower = clampStat(stats.magicPower);
    stats.evade = clampStat(stats.evade);
    stats.magicBlock = clampStat(stats.magicBlock);

    settleHands(stats, hands);

    if (stats.relicEffects3.has(RelicEffect3::RAISE_VIGOR_50_PERCENT)) {
        stats.vigor += stats.vigor / 2;
    }
    return stats;
}

int boostedMaximum(int maximum, MaxBoost boost) {
    switch (boost) {
        case MaxBoost::None: return maximum;
        case MaxBoost::Plus12Point5Percent: return maximum + maximum / 8;
        case MaxBoost::Plus25Percent: return maximum + maximum / 4;
        case MaxBoost::Plus50Percent: return maximum + maximum / 2;
    }
    return maximum;
}

void applyDerivedStats(BattleState& state, BattlerSlot slot, const DerivedStats& stats) {
    Battler& battler = state.at(slot);
    if (stats.armor == ItemId::MOOGLE_SUIT) {
        battler.graphics = CharacterGfxId::MOG;
    }
    battler.mayHoldTwoWeapons = stats.relicEffects4.has(RelicEffect4::EQUIP_2_WEAPONS);

    // The battle's vigor is twice the character's. A Hyper Wrist can take the character's past
    // 255 only from a vigor of 171, which the 128 the game lets a stat reach plus 42 from six
    // items cannot make.
    battler.vigor = static_cast<std::uint8_t>(std::min(kStatMax, 2 * stats.vigor));
    battler.speed = static_cast<std::uint8_t>(stats.speed);
    battler.stamina = static_cast<std::uint8_t>(stats.stamina);
    battler.magicPower = static_cast<std::uint8_t>(stats.magicPower);
    battler.evadeFactor = hitCheckFactor(stats.evade);
    battler.magicBlockFactor = hitCheckFactor(stats.magicBlock);
    battler.equipmentStatuses = stats.equipmentStatuses;
    battler.runFactor = stats.runFactor;
    battler.human = true;
    battler.undead = stats.relicEffects5.has(RelicEffect5::MAKE_UNDEAD);

    battler.relicEffects1 = stats.relicEffects1;
    battler.relicEffects2 = stats.relicEffects2;
    battler.relicEffects3 = stats.relicEffects3;
    battler.relicEffects4 = stats.relicEffects4;
    battler.relicEffects5 = stats.relicEffects5;
    if (stats.twoWeapons) {
        battler.relicEffects4.clear(RelicEffect4::EQUIP_2_WEAPONS);
    }
    state.partyRelicEffects2 = unionOf(state.partyRelicEffects2, stats.relicEffects2);

    battler.weaponHand = stats.weaponHand;
    battler.shieldHand = stats.shieldHand;
    if (!stats.relicEffects4.has(RelicEffect4::USE_WEAPON_2_HANDED)) {
        battler.weaponHand.weaponFlags.clear(WeaponFlags::TWO_HAND);
        battler.shieldHand.weaponFlags.clear(WeaponFlags::TWO_HAND);
    }
    battler.relics = stats.relics;
    battler.defense = stats.defense;
    battler.magicDefense = stats.magicDefense;

    // The first sixteen statuses can be put on the battler unless its equipment protects it.
    const StatusSet firstTwoBytes =
        unionOf(kFirstStatusByte, StatusSet::of(StatusId::CONDEMNED, StatusId::NEAR_FATAL, StatusId::IMAGE,
                                                StatusId::SILENCE, StatusId::BERSERK, StatusId::CONFUSE,
                                                StatusId::SAP, StatusId::SLEEP));
    battler.inflictableStatuses =
        replaceStatuses(battler.inflictableStatuses, firstTwoBytes.without(stats.protection), firstTwoBytes);

    battler.elementsAbsorbed = stats.elementsAbsorbed;
    battler.elementsNullified = stats.elementsNullified;
    battler.elementsWeak = stats.elementsWeak;
    battler.elementsHalved = stats.elementsHalved;
    battler.physicalBlockGraphics = stats.physicalBlockGraphics;
    battler.magicBlockGraphics = stats.magicBlockGraphics;

    const bool heavy = stats.relicEffects4.has(RelicEffect4::EQUIP_HEAVY_ITEMS);
    const auto heavyBit = static_cast<std::uint8_t>(static_cast<std::uint16_t>(EquipSpecial::HEAVY) >> 8);
    battler.equipsAs.hi = static_cast<std::uint8_t>(heavy ? battler.equipsAs.hi | heavyBit : battler.equipsAs.hi & ~heavyBit);

    applyDeathAndPoisonImmunity(battler);
}

void applyDeathAndPoisonImmunity(Battler& battler) {
    battler.instantDeathProtected = !battler.inflictableStatuses.has(StatusId::DEAD);
    battler.inflictableStatuses.set(StatusId::DEAD);
    if (battler.elementsNullified.has(Element::POISON)) {
        battler.inflictableStatuses.clear(StatusId::POISON);
    }
}

void refreshStatusImmunity(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    StatusSet& inflictable = battler.inflictableStatuses;
    if (battler.equipmentStatuses.has(StatusId::FLOAT)) inflictable.clear(StatusId::FLOAT);
    if (state.permanentMorph) inflictable.clear(StatusId::MORPH);
    inflictable = inflictable.without(intersectionOf(battler.equipmentStatuses, kEquipmentHeldStatuses));
    if (!inflictable.has(StatusId::REGEN) || !inflictable.has(StatusId::SAP)) {
        inflictable.clear(StatusId::REGEN);
        inflictable.clear(StatusId::SAP);
    }
    if (!inflictable.has(StatusId::SLOW) || !inflictable.has(StatusId::HASTE)) {
        inflictable.clear(StatusId::SLOW);
        inflictable.clear(StatusId::HASTE);
    }
}

// --- the party -----------------------------------------------------------------------------------

BattlerSet placeParty(BattleState& state, const PartyLineup& lineup) {
    Placements placed{};
    BattlerSet fromSetup;
    std::optional<CharAiId> setup = lineup.scriptedSetup;
    if (std::holds_alternative<CurrentParty>(lineup.fighters)) {
        placed = currentParty(state);
        if (state.gauMayAppear) {
            setup = CharAiId::GAU_VELDT;
        }
    } else if (const auto* colosseum = std::get_if<ColosseumFighter>(&lineup.fighters)) {
        // The colosseum fighter, like a scripted setup's cast, enters at full strength.
        placed[0] = Placement{.actor = colosseum->fighter};
        fromSetup.add(BattlerSlot::character(0));
        state.colosseum = true;
    } else {
        placed = finalBattleParty(state, std::get<FinalBattleStage>(lineup.fighters));
    }
    if (setup) {
        stageScriptedSetup(placed, fromSetup, *setup);
    }

    for (int position = kPartySlots - 1; position >= 0; --position) {
        if (placed[position]) {
            seat(state, BattlerSlot::character(position), *placed[position]);
        }
    }
    return fromSetup;
}

void loadPartyMember(BattleState& state, BattlerSlot slot, const DerivedStats& stats, bool joinedFromSetup) {
    Battler& battler = state.at(slot);
    game::CharacterRecord& record = state.party.characters[*battler.record];
    battler.hp = record.hp;
    battler.mp = record.mp;
    battler.maxHp = static_cast<std::uint16_t>(std::min(kMaxHp, boostedMaximum(record.maxHp, stats.hpBoost)));
    battler.maxMp = static_cast<std::uint16_t>(std::min(kMaxMp, boostedMaximum(record.maxMp, stats.mpBoost)));
    if (joinedFromSetup) {
        battler.hp = battler.maxHp;
        battler.mp = battler.maxMp;
        record.status = replaceStatuses(record.status, intersectionOf(record.status, kStatusesKeptWhenJoining),
                                        kFirstStatusByte);
    }
    if (battler.equipmentStatuses.has(StatusId::CONDEMNED)) {
        battler.pendingUpdates.stopCondemned = false;
    }
    battler.innateStatuses = unionOf(intersectionOf(record.status, kRecordStatuses), battler.equipmentStatuses);
    if (record.status.has(StatusId::MAGITEK)) {
        state.mimic.last.command = BattleCommandId::MAGITEK;
        state.mimic.last.attack = AttackId::FIRE_BEAM;
    }
    battler.level = record.level;
}

void buildCommandList(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    game::CharacterRecord& record = state.party.characters[*battler.record];
    std::array<BattleCommandId, 4> commands = record.commands;

    // In Magitek armor, and in places where only magic works, Fight becomes Magitek and every
    // command but Magic, Item and Mimic goes. Where only magic works, Magitek then becomes Magic and
    // Magic goes.
    if (record.status.has(StatusId::MAGITEK) || state.magicOnly) {
        for (BattleCommandId& command : commands) {
            if (command == BattleCommandId::ITEM || command == BattleCommandId::MIMIC) {
                continue;
            }
            if (battler.actor == CharacterPropId::GAU && command == BattleCommandId::RAGE) {
                command = BattleCommandId::FIGHT;
            }
            if (command == BattleCommandId::FIGHT) {
                command = BattleCommandId::MAGITEK;
            } else if (command != BattleCommandId::MAGIC) {
                command = BattleCommandId::NONE;
            }
            if (state.magicOnly) {
                if (command == BattleCommandId::MAGIC) {
                    command = BattleCommandId::NONE;
                } else if (command == BattleCommandId::MAGITEK) {
                    command = BattleCommandId::MAGIC;
                }
            }
        }
    }

    bool hasMp = false;
    for (std::size_t row = 0; row < commands.size(); ++row) {
        BattleCommandId command = commands[row];
        for (auto swap = kRelicCommandSwaps.rbegin(); swap != kRelicCommandSwaps.rend(); ++swap) {
            if (command == swap->from && battler.relicEffects2.has(relicSwapFlag(swap->from))) {
                command = swap->to;
            }
        }
        switch (command) {
            case BattleCommandId::MORPH:
                if (!state.morphAvailable) {
                    command = BattleCommandId::NONE;
                } else if (state.permanentMorph) {
                    battler.innateStatuses.set(StatusId::MORPH);
                    state.party.morphSupply = kStatMax;
                    command = BattleCommandId::NONE;
                }
                break;
            case BattleCommandId::LEAP:
                if (!state.onTheVeldt) command = BattleCommandId::NONE;
                break;
            case BattleCommandId::DANCE:
                if (state.party.dancesKnown.none()) command = BattleCommandId::NONE;
                break;
            case BattleCommandId::MAGIC:
            case BattleCommandId::X_MAGIC:
                if (battler.spellCount != 0 || battler.spells.esper) {
                    hasMp = true;
                } else {
                    command = BattleCommandId::NONE;
                }
                break;
            case BattleCommandId::LORE:
                hasMp = true;
                break;
            default:
                break;
        }
        const bool present = command != BattleCommandId::NONE;
        battler.commands[row] = MenuCommand{
            .command = command,
            .usable = present,
            .targeting = present ? battleCommandProperties(command).targeting : Targeting{},
        };
    }

    // A party member with nothing that uses MP has none.
    if (!hasMp) {
        battler.mp = 0;
        battler.maxMp = 0;
    }
}

void loadParty(BattleState& state, BattlerSet joinedFromSetup, game::PlayTime savedPlayTime) {
    buildSpellLists(state, savedPlayTime);
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        Battler& battler = state.at(slot);
        if (!battler.actor) {
            continue;
        }
        state.partyPresentAtStart.add(slot);
        assert(battler.record && "a placed party member has a character record");
        if (!battler.record) {
            continue;
        }
        const DerivedStats stats = deriveEquipment(state.party.characters[*battler.record]);
        applyDerivedStats(state, slot, stats);
        loadPartyMember(state, slot, stats, joinedFromSetup.has(slot));
        loadSpellList(state, slot);
        buildCommandList(state, slot);
    }
}

void applyEquipmentChanges(BattleState& state, ActionExecutor& executor) {
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        Battler& battler = state.at(slot);
        if (!std::exchange(battler.equipmentChanged, false) || !battler.record) {
            continue;
        }
        game::CharacterRecord& record = state.party.characters[*battler.record];
        record.weapon = state.inventory.weaponHands[position].item;
        record.shield = state.inventory.shieldHands[position].item;
        record.status = replaceStatuses(record.status, battler.status, kFirstStatusByte);
        applyDerivedStats(state, slot, deriveEquipment(record));
        executor.refreshCommandList(state, slot);
        refreshStatusImmunity(state, slot);
    }
}

}  // namespace ostinato::battle
