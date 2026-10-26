#include "battle/targeting.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "battle/commands.h"
#include "data/attack_properties.h"
#include "data/battle_commands.h"
#include "data/battle_tables.h"
#include "data/item_properties.h"
#include "ostinato/attack_flags.h"
#include "ostinato/command_targeting_init.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "ostinato/target_flags.h"

namespace ostinato::battle {

namespace {

// A party member is off the field when dead; a monster also when petrified or a zombie.
const StatusSet kCharacterFallen = StatusSet::of(StatusId::DEAD);
const StatusSet kMonsterFallen = StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE);

// The statuses that keep the target of a blow from being covered or a battler from covering it.
const StatusSet kCannotCover =
    StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::INVISIBLE, StatusId::ZOMBIE, StatusId::SLEEP,
                  StatusId::CONFUSE, StatusId::STOP, StatusId::FROZEN, StatusId::CHANT, StatusId::HIDE);

// A side attack's two halves of the party.
constexpr BattlerSet kLeftHalf{BattleSlotMask(0x03), BattleSlotMask(0x00)};
constexpr BattlerSet kRightHalf{BattleSlotMask(0x0C), BattleSlotMask(0x00)};

ItemId asItem(AttackId attackOrItem) {
    return static_cast<ItemId>(static_cast<std::uint8_t>(attackOrItem));
}

bool hasAimingByte(BattleCommandId command) {
    return static_cast<std::size_t>(command) < kCommandTargetingInit.size();
}

// The attack adds its own rules once its record is loaded.
void addAttackRules(AttackInFlight& attack) {
    if (attack.targeting.usesAttackDefault()) {
        attack.targeting = attack.record.targeting;
    }
    if (attack.record.traits.has(AttackTrait::RESURRECTION_TARGET)) {
        attack.rules.mayTargetTheFallen = true;
    }
    if (attack.record.traits.has(AttackTrait::RANDOM_TARGET)) {
        attack.rules.randomTarget = true;
    }
    if (attack.record.traits.has(AttackTrait::NO_CHARACTER_TARGET)) {
        attack.rules.noCharacterTargets = true;
    }
}

void loadSpell(AttackInFlight& attack, AttackId spell) {
    attack.scratch.lastSpell = spell;
    loadAttackRecord(attack, spell);
    addAttackRules(attack);
}

// An item is aimed by its own targeting, and only an item that raises the dead or turns a
// target to stone or a zombie may be aimed at the fallen.
const ItemProperties& aimItem(AttackInFlight& attack, ItemId item) {
    attack.scratch.lastItem = item;
    const ItemProperties& properties = getItemProperties(item);
    attack.targeting = properties.targeting;
    if (!itemStatuses(properties).hasAny(StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE))) {
        attack.rules.mayTargetTheFallen = false;
    }
    return properties;
}

BattlerSet charactersActingAsEnemies(const BattleState& state) {
    BattlerSet enemies;
    for (int position = 0; position < kPartySlots; ++position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        if (state.at(slot).actsAsEnemy) {
            enemies.add(slot);
        }
    }
    return enemies;
}

// Drop the targets the attack cannot reach: seized party members, monsters their AI hides from
// aiming, battlers not on the field, and those away from it (jumping, carried off, held in Seize)
// unless counterattacks are off for this action. Then the attack's own exclusions.
void maskTargets(BattleState& state, AttackInFlight& attack, BattlerSlot attacker) {
    if (std::exchange(attack.scratch.aimExactly, false)) {
        return;
    }
    BattlerSet reachable = (state.targetableOrPresent & state.countedInBattle).without(state.aiUntargetable);
    for (int position = 0; position < kPartySlots; ++position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        if (state.at(slot).seizedBy) {
            reachable.remove(slot);
        }
    }
    if (state.scratch.countersEnabled) {
        reachable = reachable.without(state.awayFromTheField);
    }
    attack.targets = attack.targets & reachable;
    if (attack.rules.noCharacterTargets) {
        attack.targets = attack.targets.monstersOnly();
    }
    if (attack.rules.excludeAttacker) {
        attack.targets.remove(attacker);
    }
}

// Aim at a whole side: by default the attacker's own, turned to the other by a Charm, by fighting
// against the party, by an enemy-first targeting byte and by Confuse; a Zombie and an initial
// selection of "all" aim at both. A spell bounced off Reflect goes to the side opposite the
// battler it bounced off.
void aimAtSide(BattleState& state, AttackInFlight& attack, BattlerSlot attacker) {
    const Battler& battler = state.at(attacker);
    bool monsterSide = attacker.isMonster();
    bool bothSides = false;
    if (attack.rules.reflected) {
        monsterSide = !monsterSide;
    } else {
        if (battler.charmedBy) {
            monsterSide = !monsterSide;
        }
        if (attacker.isCharacter() && battler.actsAsEnemy) {
            monsterSide = !monsterSide;
        }
        if (attack.targeting.initialSelection() == InitialSelection::All) {
            bothSides = true;
        }
        if (attack.targeting.enemyByDefault()) {
            monsterSide = !monsterSide;
        }
        if (battler.status.has(StatusId::ZOMBIE)) {
            bothSides = true;
        }
        if (battler.status.has(StatusId::CONFUSE)) {
            monsterSide = !monsterSide;
        }
    }
    const BattlerSet enemyCharacters = charactersActingAsEnemies(state);
    attack.targets = {};
    if (bothSides || monsterSide) {
        attack.targets = BattlerSet::allMonsters() | enemyCharacters;
    }
    if (bothSides || !monsterSide) {
        attack.targets = attack.targets | BattlerSet::allCharacters().without(enemyCharacters);
    }
}

BattlerSet withoutMonsters(BattlerSet targets, BattleSlotMask monsters) {
    return targets.without(BattlerSet{BattleSlotMask(), monsters});
}

bool reachesSide(BattlerSet targets, BattleSlotMask monsters) {
    return (targets.monsters.bits & monsters.bits) != 0;
}

// A pincer or side attack keeps an attack on one side of it, and an attack aimed at both the
// party and the monsters ends up on one of them.
void keepToOneSide(BattleState& state, AttackInFlight& attack, BattlerSlot attacker,
                   std::optional<BattlerSlot> reflectedFrom) {
    if (attack.rules.reflected) {
        if (!attack.rules.opposingSideOnly || state.at(attacker).status.has(StatusId::CONFUSE)) {
            return;
        }
        if (state.rng.coin()) {
            return;
        }
        if (reflectedFrom && attack.targets.has(*reflectedFrom)) {
            attack.targets = BattlerSet::of(*reflectedFrom);
        }
        return;
    }
    const InitialSelection selection = attack.targeting.initialSelection();
    const bool spreads = selection == InitialSelection::All || selection == InitialSelection::Half;
    if (!spreads && state.battleType == BattleType::Pincer) {
        const BattleSlotMask first = state.pincerSides[0];
        const BattleSlotMask second = state.pincerSides[1];
        if (attacker.isMonster()) {
            const bool onFirst = first.has(static_cast<std::uint8_t>(attacker.position()));
            attack.targets = withoutMonsters(attack.targets, onFirst ? second : first);
        } else if (reachesSide(attack.targets, first) && reachesSide(attack.targets, second)) {
            attack.targets = withoutMonsters(attack.targets, state.rng.coin() ? first : second);
        }
    }
    if (!spreads && state.battleType == BattleType::Side) {
        if (attacker.isCharacter()) {
            attack.targets = attack.targets.without(kLeftHalf.has(attacker) ? kRightHalf : kLeftHalf);
        } else if (!(attack.targets & kLeftHalf).empty() && !(attack.targets & kRightHalf).empty()) {
            attack.targets = attack.targets.without(state.rng.coin() ? kLeftHalf : kRightHalf);
        }
    }
    if (selection != InitialSelection::All && !attack.targets.charactersOnly().empty() &&
        !attack.targets.monstersOnly().empty()) {
        attack.targets = state.rng.coin() ? attack.targets.charactersOnly() : attack.targets.monstersOnly();
    }
}

void pickOne(BattleState& state, AttackInFlight& attack) {
    const std::optional<BattlerSlot> picked = state.rng.pickFrom(attack.targets);
    attack.targets = picked ? BattlerSet::of(*picked) : BattlerSet{};
}

// The highest-numbered battler in a set: monster 5 first, party member 0 last.
std::optional<BattlerSlot> lastIn(BattlerSet set) {
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        if (set.has(slot)) {
            return slot;
        }
    }
    return std::nullopt;
}

bool controlsAnother(const BattleState& state, BattlerSlot slot) {
    for (const Battler& other : state.battlers) {
        if (other.controlledBy == slot) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool initTargeting(AttackInFlight& attack, BattleCommandId command, AttackId attackOrItem) {
    attack.rules = {};
    attack.targeting = Targeting::of(TargetFlags::ENEMY);
    CommandAttackLoad load = CommandAttackLoad::PlainHit;
    if (hasAimingByte(command)) {
        const CommandTargetingInit init = commandTargetingInit(command);
        attack.rules.noCharacterTargets = init.noCharacterTargets();
        attack.rules.randomTarget = init.randomTarget();
        attack.rules.fallBackToEarlierTargets = init.fallBackToEarlierTargets();
        attack.rules.excludeAttacker = init.excludesTheUser();
        attack.rules.mayTargetTheFallen = init.mayTargetTheFallen();
        attack.rules.keepTargetsThatAreGone = init.keepsTargetsThatAreGone();
        attack.targeting = battleCommandProperties(command).targeting;
        load = init.attackLoad();
    }
    switch (load) {
    case CommandAttackLoad::PlainHit:
        loadAttackRecord(attack, AttackId::BATTLE);
        addAttackRules(attack);
        return false;
    case CommandAttackLoad::Spell:
        loadSpell(attack, attackOrItem);
        return false;
    case CommandAttackLoad::Item: {
        const ItemId item = asItem(attackOrItem);
        const ItemProperties& properties = aimItem(attack, item);
        if (item >= kFirstItemUsedAsItself) {
            return true;
        }
        if (!properties.spellCast.castOnItemUse()) {
            attack.scratch.itemKept = true;
        }
        loadSpell(attack, properties.spellCast.spell());
        return false;
    }
    case CommandAttackLoad::ThrownItem: {
        const ItemId item = asItem(attackOrItem);
        for (const ThrowToolsConversion& conversion : kThrowToolsConversions) {
            if (conversion.item == item) {
                loadSpell(attack, static_cast<AttackId>(static_cast<std::uint8_t>(item) - conversion.attackOffset));
                return false;
            }
        }
        aimItem(attack, item);
        return true;
    }
    }
    return false;
}

BattlerSet removeAbsentTargets(const BattleState& state, BattlerSet targets) {
    for (int index = 0; index < kBattlerSlots; ++index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        if (!targets.has(slot)) {
            continue;
        }
        const Battler& battler = state.at(slot);
        const StatusSet& fallen = slot.isCharacter() ? kCharacterFallen : kMonsterFallen;
        if (!battler.present || battler.status.hasAny(fallen) || battler.status.has(StatusId::HIDE)) {
            targets.remove(slot);
        }
    }
    return targets;
}

void chooseTargets(BattleState& state, AttackInFlight& attack, BattlerSlot attacker,
                   std::optional<BattlerSlot> reflectedFrom) {
    if (attack.targeting.isSelfOnly()) {
        attack.targets = BattlerSet::of(attacker);
        return;
    }
    maskTargets(state, attack, attacker);
    bool reaim = attack.rules.randomTarget;
    if (!reaim) {
        if (!attack.rules.mayTargetTheFallen) {
            attack.targets = removeAbsentTargets(state, attack.targets);
        }
        if (!attack.targets.empty()) {
            if (!attack.targeting.keepsEveryTarget()) {
                pickOne(state, attack);
            }
            return;
        }
        reaim = !attack.rules.keepTargetsThatAreGone;
    }
    if (reaim) {
        aimAtSide(state, attack, attacker);
        maskTargets(state, attack, attacker);
        if (!attack.rules.mayTargetTheFallen) {
            attack.targets = removeAbsentTargets(state, attack.targets);
        }
    }
    keepToOneSide(state, attack, attacker, reflectedFrom);
    if (attack.rules.fallBackToEarlierTargets && attack.targets.empty()) {
        attack.targets = attack.scratch.earlierTargets;
    }
    if (attack.targeting.initialSelection() != InitialSelection::Single) {
        return;
    }
    if (attack.targeting.multiTarget() && state.rng.coin()) {
        return;
    }
    pickOne(state, attack);
}

void coverTarget(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker) {
    if (!attack.canBeCritical || attack.targets.empty()) {
        return;
    }
    const BattlerSlot covered = *lastIn(attack.targets);
    std::optional<BattlerSlot> bodyguard;
    int bodyguardHp = 0;
    bool coveredAlready = false;

    const auto consider = [&](BattlerSlot candidate) {
        const Battler& battler = state.at(candidate);
        if (!battler.present || controlsAnother(state, candidate) || battler.seizing ||
            battler.status.hasAny(kCannotCover)) {
            return;
        }
        attack.params.steppingIn.add(candidate);
        if (battler.hp >= bodyguardHp) {
            bodyguardHp = battler.hp;
            bodyguard = candidate;
        }
    };
    const auto stepIn = [&] {
        if (!bodyguard || coveredAlready) {
            return;
        }
        coveredAlready = true;
        attack.params.coveredAlly = covered;
        attack.targets = BattlerSet::of(*bodyguard);
    };

    const Battler& target = state.at(covered);
    if (target.bodyguard) {
        consider(*target.bodyguard);
        stepIn();
    }
    if (!target.status.has(StatusId::NEAR_FATAL) || target.status.has(StatusId::INVISIBLE) || target.seizing) {
        return;
    }
    BattlerSet allies = covered.isCharacter() ? BattlerSet::allCharacters() : BattlerSet::allMonsters();
    allies.remove(covered);
    allies.remove(attacker);
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        if (allies.has(slot) && state.at(slot).relicEffects4.has(RelicEffect4::PROTECT_WEAK_ALLIES)) {
            consider(slot);
        }
    }
    if (bodyguardHp != 0) {
        stepIn();
    }
}

}  // namespace ostinato::battle
