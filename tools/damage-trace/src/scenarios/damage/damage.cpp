// The port's damage over a scenario, the differences between two results, and the damage sweeps'
// draws.

#include "scenarios/damage/damage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <variant>
#include <vector>

#include "battle/attack.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/damage.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/attack_properties.h"
#include "game/character_record.h"
#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

#include "cells/damage/per_target.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/damage_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {

constexpr std::uint8_t kEquipmentBlocks = 0x0F;
}  // namespace

namespace detail {

ApplyDamageResult portApplied(const battle::BattleState& state, const battle::AttackInFlight& attack,
                              const battle::BattleScriptBuilder& script) {
    ApplyDamageResult result;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        result.hp[i] = battler.hp;
        result.mp[i] = battler.mp;
        result.statusesToSet[i] = battler.statusesToSet;
        const std::uint8_t mark = markByte(battler);
        result.lastAttackerMark[i] = mark;
        result.lastDamagedBy[i] = battler.lastDamagedBy;
        result.enabledMagic[i] = battler.pendingUpdates.enabledMagic;
    }
    result.blackBeltEligible = state.scratch.blackBeltEligible;
    result.missed = attack.scratch.missed;
    result.golemHp = state.golemHp;
    result.weaponSpellcast = attack.scratch.weaponSpellcast.raw;
    result.ledger = attack.scratch.ledger;
    for (const battle::BattleScriptCommand& command : script.commands()) {
        if (const auto* numerals = std::get_if<battle::DamageNumbersShown>(&command)) {
            result.shown.push_back(*numerals);
        }
    }
    return result;
}
}  // namespace detail

ApplyDamageResult portApplyDamage(DamageScenario scenario) {
    battle::BattleScriptBuilder script;
    battle::applyDamage(scenario.state, scenario.attack, scenario.attacker, script);
    return portApplied(scenario.state, scenario.attack, script);
}

ReflectedAttackResult portReflectedAttack(DamageScenario scenario) {
    battle::BattleScriptBuilder script;
    // The block before the bounce's: the frame's own, whose attacker is the caster, as
    // reflectedAttack() writes it.
    script.addParams(battle::AttackAnimationParams{.attacker = scenario.attacker});
    battle::BattleHooks hooks;
    battle::resolveReflectedAttack(scenario.state, hooks, script, scenario.attack, scenario.attacker);
    ReflectedAttackResult result;
    result.applied = portApplied(scenario.state, scenario.attack, script);
    for (const battle::BattleScriptCommand& command : script.commands()) {
        if (const auto* bounces = std::get_if<battle::ReflectedAttackShown>(&command)) {
            result.shownAs = shownText(*bounces);
        } else if (const auto* hits = std::get_if<battle::ScatteredHitsShown>(&command)) {
            result.shownAs = shownText(*hits);
        }
    }
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = scenario.state.battlers[i];
        result.statusesToClear[i] = battler.statusesToClear;
        if (battler.lastHit.command) {
            result.lastHitBy[i] = battler.lastHit.command->by;
        }
    }
    result.damage = scenario.attack.damage;
    result.canBeCritical = scenario.attack.canBeCritical;
    result.extraHits = scenario.state.extraHits;
    result.golemBlocked = scenario.attack.scratch.golemBlocked;
    result.interceptorBlocked = scenario.attack.scratch.interceptorBlocked;
    result.rngPosition = scenario.state.rng.position();
    return result;
}

std::string differences(const ReflectedAttackResult& port, const ReflectedAttackResult& guest) {
    std::string out = differences(port.applied, guest.applied);
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    const auto slotName = [](const std::optional<battle::BattlerSlot>& slot) {
        return slot ? std::to_string(slot->index()) : std::string{"none"};
    };
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("statuses to clear" + slot, statuses(port.statusesToClear[i]), statuses(guest.statusesToClear[i]));
        note("last hit by" + slot, slotName(port.lastHitBy[i]), slotName(guest.lastHitBy[i]));
    }
    note("shown", port.shownAs, guest.shownAs);
    note("damage", std::to_string(port.damage), std::to_string(guest.damage));
    note("can be critical", std::to_string(port.canBeCritical), std::to_string(guest.canBeCritical));
    note("hits to come", port.extraHits ? std::to_string(*port.extraHits) : "none",
         guest.extraHits ? std::to_string(*guest.extraHits) : "none");
    note("Golem's block", slotName(port.golemBlocked), slotName(guest.golemBlocked));
    note("Interceptor's block", slotName(port.interceptorBlocked), slotName(guest.interceptorBlocked));
    note("random-number position", std::to_string(port.rngPosition), std::to_string(guest.rngPosition));
    return out;
}

std::string differences(const ApplyDamageResult& port, const ApplyDamageResult& guest) {
    std::string out;
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    const auto slotName = [](const std::optional<battle::BattlerSlot>& slot) {
        return slot ? std::to_string(slot->index()) : std::string{"none"};
    };
    const auto entry = [](const std::optional<std::uint16_t>& value) {
        return value ? std::to_string(*value) : std::string{"none"};
    };
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    const auto numerals = [](const std::vector<battle::DamageNumbersShown>& shown) {
        std::string text;
        for (const battle::DamageNumbersShown& one : shown) {
            text += one.wait == battle::DamageNumberWait::UntilGone ? "[together" : "[one at a time";
            for (const battle::DamageNumber& number : one.numbers) {
                text += " " + std::to_string(number.target.index()) + ":" +
                        (number.miss ? std::string{"miss"} : std::to_string(number.amount)) + (number.heals ? "+" : "");
            }
            text += "]";
        }
        return text;
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("HP" + slot, std::to_string(port.hp[i]), std::to_string(guest.hp[i]));
        note("MP" + slot, std::to_string(port.mp[i]), std::to_string(guest.mp[i]));
        note("statuses to set" + slot, statuses(port.statusesToSet[i]), statuses(guest.statusesToSet[i]));
        note("last attacker" + slot, std::to_string(port.lastAttackerMark[i]), std::to_string(guest.lastAttackerMark[i]));
        note("last damaged by" + slot, slotName(port.lastDamagedBy[i]), slotName(guest.lastDamagedBy[i]));
        note("castable spells to work out" + slot, std::to_string(port.enabledMagic[i]),
             std::to_string(guest.enabledMagic[i]));
        note("damage taken" + slot, entry(port.ledger.taken[i]), entry(guest.ledger.taken[i]));
        note("damage healed" + slot, entry(port.ledger.healed[i]), entry(guest.ledger.healed[i]));
    }
    note("Black Belt targets", std::to_string(maskOf(port.blackBeltEligible)),
         std::to_string(maskOf(guest.blackBeltEligible)));
    note("shown Miss", std::to_string(maskOf(port.missed)), std::to_string(maskOf(guest.missed)));
    note("the Golem's HP", std::to_string(port.golemHp), std::to_string(guest.golemHp));
    note("the weapon's spellcast", std::to_string(port.weaponSpellcast), std::to_string(guest.weaponSpellcast));
    note("numerals", numerals(port.shown), numerals(guest.shown));
    return out;
}

TargetDamageResult portTargetDamage(DamageScenario scenario, battle::BattlerSlot target) {
    battle::BattleScriptBuilder script;
    script.addAnimation(battle::AttackAnimation{.secondary = kUnwritten});
    battle::targetDamage(scenario.state, scenario.attack, scenario.attacker, target, script);

    TargetDamageResult result;
    result.ledger = scenario.attack.scratch.ledger;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = scenario.state.battlers[i];
        result.statusesToSet[i] = battler.statusesToSet;
        result.statusesToClear[i] = battler.statusesToClear;
        result.controlled[i] = battler.controlledBy && !battler.controlEnding;
        const std::uint8_t mark = markByte(battler);
        result.lastAttackerMark[i] = mark;
    }
    result.stepsLeft = scenario.attack.multiplier;
    result.secondary = scenario.attack.secondary;
    const auto* animation = std::get_if<battle::AttackAnimation>(&script.commands().front());
    if (animation == nullptr || animation->secondary != kUnwritten) {
        result.animationRewrittenWith = animation != nullptr ? animation->secondary : 0;
    }
    result.rngPosition = scenario.state.rng.position();
    return result;
}

std::string differences(const TargetDamageResult& port, const TargetDamageResult& guest) {
    std::string out;
    const auto entry = [](const std::optional<std::uint16_t>& value) {
        return value ? std::to_string(*value) : std::string{"none"};
    };
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("damage taken" + slot, entry(port.ledger.taken[i]), entry(guest.ledger.taken[i]));
        note("damage healed" + slot, entry(port.ledger.healed[i]), entry(guest.ledger.healed[i]));
        note("statuses to set" + slot, statuses(port.statusesToSet[i]), statuses(guest.statusesToSet[i]));
        note("statuses to clear" + slot, statuses(port.statusesToClear[i]), statuses(guest.statusesToClear[i]));
        note("controlled" + slot, std::to_string(port.controlled[i]), std::to_string(guest.controlled[i]));
        note("last attacker" + slot, std::to_string(port.lastAttackerMark[i]),
             std::to_string(guest.lastAttackerMark[i]));
    }
    note("multiplier steps", std::to_string(port.stepsLeft), std::to_string(guest.stepsLeft));
    note("animation second value", std::to_string(port.secondary), std::to_string(guest.secondary));
    note("animation rewritten with", port.animationRewrittenWith ? std::to_string(*port.animationRewrittenWith) : "none",
         guest.animationRewrittenWith ? std::to_string(*guest.animationRewrittenWith) : "none");
    note("random-number position", std::to_string(port.rngPosition), std::to_string(guest.rngPosition));
    return out;
}

DamageScenario drawScenario(std::mt19937& rng) {
    DamageScenario s;
    s.attacker = battle::BattlerSlot::fromIndex(static_cast<int>(rng() % battle::kBattlerSlots));
    s.attack.record.traits.bits = byte(rng);
    s.attack.record.flags1.bits = byte(rng);
    s.attack.record.flags2.bits = byte(rng);
    s.attack.record.power = byte(rng);
    s.attack.attackerStat = byte(rng);
    // A party member's level is 1 to 99; a monster's is whatever its record says.
    s.attack.attackerLevel = s.attacker.isCharacter() ? static_cast<std::uint8_t>(1 + rng() % 99) : byte(rng);
    s.attack.twoHanded = (rng() & 1) != 0;
    s.attack.ignoreDefenseStopsMultiplier = (rng() & 1) != 0;
    s.attack.multiplier = static_cast<int>(rng() % 8);
    s.attack.damage = static_cast<int>(rng() & 0xFFFF);
    battle::Battler& attacker = s.state.at(s.attacker);
    attacker.relicEffects1.bits = byte(rng);
    attacker.relicEffects2.bits = byte(rng);
    attacker.relicEffects3.bits = byte(rng);
    attacker.relicEffects4.bits = byte(rng);
    attacker.relicEffects5.bits = byte(rng);
    return s;
}
namespace {

constexpr int kLedgerMax = 9999;
// No fractional attack or item in the game has a power above this.
constexpr int kMostFractionalPower = 16;
}  // namespace

TargetScenario drawTargetScenario(std::mt19937& rng) {
    TargetScenario drawn;
    DamageScenario& s = drawn.scenario;
    s.attacker = anySlot(rng);
    drawn.target = anySlot(rng);

    battle::AttackInFlight& attack = s.attack;
    attack.record.traits.bits = byte(rng);
    attack.record.flags1.bits = byte(rng);
    attack.record.flags2.bits = byte(rng);
    attack.record.element.bits = oneIn(rng, 2) ? 0 : byte(rng);
    attack.record.power = oneIn(rng, 16) ? 0 : byte(rng);
    if (attack.record.flags2.has(AttackFlag2::FRACTIONAL_DAMAGE)) {
        attack.record.power = static_cast<std::uint8_t>(upTo(rng, kMostFractionalPower));
    }
    for (std::uint8_t& status : attack.record.statuses.bytes) {
        status = sparse(rng);
    }
    attack.damage = oneIn(rng, 2) ? upTo(rng, 0x3FFF) : upTo(rng, battle::kDamageWordMax);
    attack.multiplier = upTo(rng, 7);
    attack.command = oneIn(rng, 8) ? BattleCommandId::ITEM : BattleCommandId::FIGHT;
    attack.attack = static_cast<AttackId>(byte(rng));
    attack.secondary = byte(rng);
    if (s.attacker.isCharacter() && oneIn(rng, 4)) {
        attack.effect = battle::SpecialEffect::of(WeaponSpecialEffect::ATMA_WEAPON);
    }
    attack.scratch.damageModification = !oneIn(rng, 4);
    attack.drainCapped = oneIn(rng, 2);
    attack.ignoreDefenseStopsMultiplier = oneIn(rng, 2);
    if (oneIn(rng, 8)) {
        attack.scratch.golemBlocked = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    if (oneIn(rng, 8)) {
        attack.scratch.interceptorBlocked = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }

    battle::BattleState& state = s.state;
    state.elementsNullifiedByForcefield.bits = oneIn(rng, 4) ? byte(rng) : 0;
    state.rng = battle::BattleRng{byte(rng)};
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const bool party = battle::BattlerSlot::fromIndex(static_cast<int>(i)).isCharacter();
        battler.level = party ? static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1)) : byte(rng);
        battler.maxHp = static_cast<std::uint16_t>(1 + upTo(rng, (party ? kPartyMaxHp : battle::kDamageWordMax) - 1));
        battler.hp = static_cast<std::uint16_t>(upTo(rng, battler.maxHp));
        battler.maxMp = static_cast<std::uint16_t>(upTo(rng, party ? kPartyMaxMp : battle::kDamageWordMax));
        battler.mp = static_cast<std::uint16_t>(upTo(rng, battler.maxMp));
        battler.defense = oneIn(rng, 8) ? 255 : byte(rng);
        battler.magicDefense = oneIn(rng, 8) ? 255 : byte(rng);
        battler.elementsAbsorbed.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.elementsNullified.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.elementsWeak.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.elementsHalved.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.undead = oneIn(rng, 4);
        battler.impCritical = oneIn(rng, 2);
        battler.human = oneIn(rng, 2);
        battler.nameHidden = oneIn(rng, 2);
        battler.diesAtZeroMp = oneIn(rng, 2);
        battler.defending = oneIn(rng, 2);
        battler.backRow = oneIn(rng, 2);
        for (std::uint8_t& status : battler.status.bytes) {
            status = sparse(rng);
        }
        for (std::uint8_t& status : battler.statusesToSet.bytes) {
            status = static_cast<std::uint8_t>(sparse(rng) & byte(rng));
        }
        for (std::uint8_t& status : battler.statusesToClear.bytes) {
            status = static_cast<std::uint8_t>(sparse(rng) & byte(rng));
        }
        if (oneIn(rng, 4)) {
            battler.controlledBy = anySlot(rng);
        }
        if (oneIn(rng, 2)) {
            battler.lastAttacker = anySlot(rng);
        }
        battler.retaliationPending = oneIn(rng, 2);
        if (oneIn(rng, 4)) {
            attack.scratch.ledger.taken[i] = static_cast<std::uint16_t>(upTo(rng, kLedgerMax));
        }
        if (oneIn(rng, 8)) {
            attack.scratch.ledger.healed[i] = static_cast<std::uint16_t>(upTo(rng, kLedgerMax));
        }
    }
    return drawn;
}

DamageScenario drawApplyScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    attack.record.flags1.bits = oneIn(rng, 3) ? static_cast<std::uint8_t>(AttackFlag1::AFFECT_MP) : 0;
    attack.scratch.ledger = {};
    attack.scratch.weaponSpellcast.raw = oneIn(rng, 2) ? byte(rng) : 0;
    state.golemHp = oneIn(rng, 2) ? static_cast<std::uint16_t>(upTo(rng, battle::kDamageWordMax)) : 0;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battle::Battler& battler = state.battlers[i];
        if (oneIn(rng, 4)) {
            battler.status.set(StatusId::ZOMBIE);
        }
        // Small pools often enough that a loss brings them to 0.
        if (oneIn(rng, 3)) {
            battler.hp = static_cast<std::uint16_t>(std::min<int>(battler.hp, upTo(rng, 64)));
            battler.mp = static_cast<std::uint16_t>(std::min<int>(battler.mp, upTo(rng, 64)));
        }
        if (oneIn(rng, 2)) {
            attack.scratch.ledger.taken[i] = static_cast<std::uint16_t>(oneIn(rng, 2) ? upTo(rng, 128) : upTo(rng, kLedgerMax));
        }
        if (oneIn(rng, 4)) {
            attack.scratch.ledger.healed[i] = static_cast<std::uint16_t>(oneIn(rng, 2) ? upTo(rng, 128) : upTo(rng, kLedgerMax));
        }
        if (oneIn(rng, 4)) {
            attack.scratch.missed.add(slot);
        }
        if (slot.isMonster() && oneIn(rng, 8)) {
            state.invincible.add(slot);
        }
        if (slot.isCharacter() && oneIn(rng, 4)) {
            state.scratch.blackBeltEligible.add(slot);
        }
        if (oneIn(rng, 4)) {
            battler.lastDamagedBy = anySlot(rng);
        }
        battler.pendingUpdates.enabledMagic = oneIn(rng, 4);
    }
    return s;
}

DamageScenario drawReflectScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    AttackProperties& record = attack.record;
    // A spell the status and special-effect passes change nothing for.
    record.traits.clear(AttackTrait::PHYSICAL);
    record.flags2.clear(AttackFlag2::REMOVE_STATUS);
    record.flags2.clear(AttackFlag2::TOGGLE_STATUS);
    record.misc.clear(AttackMiscFlag::MISS_IF_STATUS_IMMUNE);
    record.statuses = {};
    record.hitRate = byte(rng);
    attack.effect = {};
    // A bounce is a spell's, so the Golem and Interceptor, which step in only for Fight and
    // Capture, never block it.
    attack.command = BattleCommandId::MAGIC;
    attack.queuedCommand = attack.command;
    attack.scratch.ledger = {};
    attack.canBeCritical = oneIn(rng, 2);
    attack.vanishApplies = !oneIn(rng, 4);
    attack.scratch.quietMisses = oneIn(rng, 4);
    attack.scratch.aimExactly = oneIn(rng, 4);
    attack.scratch.targetCount = 1 + upTo(rng, 4);
    attack.scratch.weaponSpellcast.raw = oneIn(rng, 2) ? byte(rng) : 0;
    if (oneIn(rng, 2)) {
        attack.scratch.lastSpell = static_cast<AttackId>(byte(rng));
    }
    if (oneIn(rng, 4)) {
        attack.scratch.lastItem = static_cast<ItemId>(byte(rng));
    }
    const auto anySet = [&] {
        battle::BattlerSet set;
        set.characters = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x0F));
        set.monsters = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F));
        return set;
    };
    attack.targets = anySet();
    attack.scratch.earlierTargets = anySet();
    attack.scratch.hitInTheBack = oneIn(rng, 4) ? anySet() : battle::BattlerSet{};
    battle::AimingRules& rules = attack.rules;
    rules.noCharacterTargets = oneIn(rng, 8);
    rules.fallBackToEarlierTargets = oneIn(rng, 4);
    rules.mayTargetTheFallen = oneIn(rng, 4);
    rules.keepTargetsThatAreGone = oneIn(rng, 2);
    rules.opposingSideOnly = oneIn(rng, 2);
    rules.excludeAttacker = oneIn(rng, 8);
    // The hits to come and the battlers the spell bounced off are drawn apart: a Launcher's own
    // bounce is off its attacker, and a count left standing meets whatever bounce comes next.
    if (oneIn(rng, 3)) {
        state.extraHits = 1 + upTo(rng, 7);
    }
    if (oneIn(rng, 3)) {
        attack.params.reflectedOff = battle::BattlerSet::of(s.attacker);
    } else {
        attack.params.reflectedOff = anySet();
        if (attack.params.reflectedOff.empty()) {
            attack.params.reflectedOff.add(anySlot(rng));
        }
    }

    state.battleType = static_cast<battle::BattleType>(upTo(rng, 3));
    state.pincerSides[0] = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F));
    state.pincerSides[1] = BattleSlotMask(static_cast<std::uint8_t>(~state.pincerSides[0].bits & 0x3F));
    state.targetableOrPresent = oneIn(rng, 4) ? anySet() : battle::BattlerSet::allCharacters() | battle::BattlerSet::allMonsters();
    state.aiUntargetable = oneIn(rng, 8) ? anySet().monstersOnly() : battle::BattlerSet{};
    state.awayFromTheField = oneIn(rng, 8) ? anySet() : battle::BattlerSet{};
    state.scratch.countersEnabled = !oneIn(rng, 4);
    attack.scratch.awayFromTheFieldUnreachable = !oneIn(rng, 4);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battle::Battler& battler = state.battlers[i];
        battler.present = !oneIn(rng, 6);
        battler.status.clear(StatusId::INVISIBLE);
        battler.status.clear(StatusId::FROZEN);
        if (oneIn(rng, 3)) {
            battler.status.set(StatusId::REFLECT);
        }
        battler.stamina = byte(rng);
        battler.evadeFactor = byte(rng);
        battler.magicBlockFactor = byte(rng);
        // The four blocks equipment can show: a dagger's, a sword's, a shield's and the Zephyr Cape's.
        battler.physicalBlockGraphics.bits = oneIn(rng, 2) ? static_cast<std::uint8_t>(sparse(rng) & kEquipmentBlocks) : 0;
        battler.magicBlockGraphics.bits = oneIn(rng, 2) ? static_cast<std::uint8_t>(sparse(rng) & kEquipmentBlocks) : 0;
        battler.instantDeathProtected = oneIn(rng, 4);
        battler.triggers.retort = oneIn(rng, 4);
        if (oneIn(rng, 8)) {
            battler.charmedBy = anySlot(rng);
        }
        if (slot.isCharacter()) {
            battler.actsAsEnemy = oneIn(rng, 8);
            if (oneIn(rng, 16)) {
                battler.seizedBy = battle::BattlerSlot::monster(upTo(rng, battle::kMonsterSlots - 1));
            }
        }
        if (oneIn(rng, 2)) {
            battler.lastHit.command = battle::Hit<BattleCommandId>{.by = anySlot(rng), .with = BattleCommandId::FIGHT};
        }
    }
    return s;
}

std::string describe(const TargetScenario& drawn) {
    const DamageScenario& s = drawn.scenario;
    const battle::AttackInFlight& a = s.attack;
    const battle::Battler& target = s.state.at(drawn.target);
    return "attacker " + std::to_string(s.attacker.index()) + " target " + std::to_string(drawn.target.index()) +
           " traits " + std::to_string(a.record.traits.bits) + " flags1 " + std::to_string(a.record.flags1.bits) +
           " flags2 " + std::to_string(a.record.flags2.bits) + " element " + std::to_string(a.record.element.bits) +
           " power " + std::to_string(a.record.power) + " damage " + std::to_string(a.damage) + " steps " +
           std::to_string(a.multiplier) + " command " + std::to_string(static_cast<int>(a.command)) + " effect " +
           std::to_string(a.effect.index()) + " modification " + std::to_string(a.scratch.damageModification) +
           " capped " + std::to_string(a.drainCapped) + " rng " + std::to_string(s.state.rng.position()) +
           " target hp " + std::to_string(target.hp) + "/" + std::to_string(target.maxHp) + " mp " +
           std::to_string(target.mp) + "/" + std::to_string(target.maxMp) + " defenses " +
           std::to_string(target.defense) + "/" + std::to_string(target.magicDefense) + " undead " +
           std::to_string(target.undead);
}

std::string describe(const DamageScenario& s) {
    const battle::Battler& attacker = s.state.at(s.attacker);
    return "attacker " + std::to_string(s.attacker.index()) + " traits " + std::to_string(s.attack.record.traits.bits) +
           " flags1 " + std::to_string(s.attack.record.flags1.bits) + " flags2 " +
           std::to_string(s.attack.record.flags2.bits) + " power " + std::to_string(s.attack.record.power) + " stat " +
           std::to_string(s.attack.attackerStat) + " level " + std::to_string(s.attack.attackerLevel) +
           " two-handed " + std::to_string(s.attack.twoHanded) + " stops " +
           std::to_string(s.attack.ignoreDefenseStopsMultiplier) + " steps " + std::to_string(s.attack.multiplier) +
           " damage " + std::to_string(s.attack.damage) + " relics " + std::to_string(attacker.relicEffects1.bits) +
           "/" + std::to_string(attacker.relicEffects2.bits) + "/" + std::to_string(attacker.relicEffects3.bits) + "/" +
           std::to_string(attacker.relicEffects4.bits) + "/" + std::to_string(attacker.relicEffects5.bits);
}

}  // namespace ostinato::trace
