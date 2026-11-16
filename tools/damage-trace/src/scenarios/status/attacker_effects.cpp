// The port's attacker effect read back as named cells, the differences between two results, and the
// attacker-effect sweep's draw.

#include "scenarios/status/attacker_effects.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "battle/attack.h"
#include "battle/attacker_effects.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/damage.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/attack_properties.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/game_limits.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"

#include "cells/damage/per_target.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/damage.h"
#include "scenarios/status/attacker_effects_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace detail {

void putRecord(EffectCells& out, const AttackProperties& record) {
    out.put("power", record.power);
    out.put("hit rate", record.hitRate);
    out.put("traits", record.traits.bits);
    out.put("flags 1", record.flags1.bits);
    out.put("flags 2", record.flags2.bits);
}
}  // namespace detail

AttackerEffectRun runAttackerEffect(DamageScenario scenario) {
    AttackerEffectRun run{.after = std::move(scenario)};
    battle::attackerEffect(run.after.state, run.after.attack, run.after.attacker, run.script);
    return run;
}

AttackerEffectResult attackerEffectResult(const AttackerEffectRun& run) {
    const battle::BattleState& state = run.after.state;
    const battle::AttackInFlight& attack = run.after.attack;
    EffectCells out;
    putRecord(out, attack.record);
    out.put("effect", attack.effect.index());
    out.put("damage", static_cast<std::uint16_t>(attack.damage));
    out.put("command", static_cast<std::uint8_t>(attack.command));
    out.put("attack", static_cast<std::uint8_t>(attack.attack));
    out.put("second value", attack.secondary);
    out.put("can be critical", attack.canBeCritical);
    out.put("automatic critical", attack.automaticCritical);
    out.put("swirl suppressed", attack.castingSwirlSuppressed);
    out.put("multiplier", attack.multiplier);
    out.put("params", paramsText(attack.params));
    out.put("targets", std::to_string(maskOf(attack.targets)));
    out.put("random target", attack.rules.randomTarget);
    out.put("fall back to earlier targets", attack.rules.fallBackToEarlierTargets);
    out.put("other side only", attack.rules.opposingSideOnly);
    out.put("spell next", attack.scratch.followUpSpell ? std::to_string(static_cast<int>(*attack.scratch.followUpSpell))
                                                       : std::string{"none"});
    out.put("message", attack.scratch.message ? std::to_string(*attack.scratch.message) : std::string{"none"});
    out.put("damage modification", attack.scratch.damageModification);
    out.put("attacks left", static_cast<std::uint8_t>(attack.scratch.attacksLeft));
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        const auto& pick = attack.scratch.mindBlastPicks[position];
        out.put("Mind Blast pick " + std::to_string(position),
                std::to_string(pick ? maskOf(battle::BattlerSet::of(*pick)) : 0));
    }
    out.put("shown Miss", std::to_string(maskOf(attack.scratch.missed)));
    out.put("hits to come", state.extraHits ? std::to_string(*state.extraHits) : std::string{"none"});
    out.put("the Golem's HP", state.golemHp);
    out.put("Forcefield", state.elementsNullifiedByForcefield.bits);
    out.put("end of battle", state.endOfBattleEvent ? static_cast<int>(*state.endOfBattleEvent) : 0);
    out.put("Quick's turns", state.quickTurnsLeft ? std::to_string(*state.quickTurnsLeft) : std::string{"none"});
    out.put("Quick's target", slotText(state.quickTarget));
    out.put("leaving", std::to_string(maskOf(state.leavingAfterAction)));
    out.put("turn ends", state.endTurnAfterAction);
    out.put("gil", static_cast<int>(state.party.gil));
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::string of = " of battler " + std::to_string(i);
        out.put("HP" + of, battler.hp);
        out.put("MP" + of, battler.mp);
        out.put("statuses to set" + of, statusesText(battler.statusesToSet));
        const std::uint8_t mark = markByte(battler);
        out.put("last attacker" + of, mark);
        out.put("gold" + of, battler.gold);
        if (slot.isCharacter()) {
            const auto position = static_cast<std::size_t>(slot.position());
            const battle::BattleItem& weapon = state.inventory.weaponHands[position];
            const battle::BattleItem& shield = state.inventory.shieldHands[position];
            out.put("hands' power" + of,
                    std::to_string(battler.weaponHand.battlePower) + "/" + std::to_string(battler.shieldHand.battlePower));
            out.put("hands" + of, std::to_string(static_cast<int>(weapon.item)) + "x" + std::to_string(weapon.quantity) +
                                      "/" + std::to_string(static_cast<int>(shield.item)) + "x" +
                                      std::to_string(shield.quantity));
            out.put("equipment to work out" + of, battler.equipmentChanged);
        }
    }
    battle::BattleScriptBuilder script = run.script;
    const std::vector<battle::BattleScriptCommand> commands = script.take();
    int animations = 0;
    std::vector<std::string> blocks;
    for (const battle::BattleScriptCommand& command : commands) {
        if (const auto* animation = std::get_if<battle::AttackAnimation>(&command)) {
            ++animations;
            blocks.push_back(paramsText(animation->params));
        }
    }
    out.put("script animations", animations);
    out.put("script params blocks", static_cast<int>(blocks.size()));
    for (std::size_t block = 0; block < blocks.size(); ++block) {
        out.put("script params " + std::to_string(block), blocks[block]);
    }
    out.put("random-number position", state.rng.position());
    return AttackerEffectResult{.cells = std::move(out.cells)};
}

std::string differences(const AttackerEffectResult& port, const AttackerEffectResult& guest) {
    std::string out;
    for (const auto& [cell, theirs] : guest.cells) {
        const auto ours = port.cells.find(cell);
        const std::string value = ours == port.cells.end() ? std::string{"(not read)"} : ours->second;
        if (value != theirs) {
            out += cell + ": port " + value + ", cartridge " + theirs + "\n";
        }
    }
    return out;
}

DamageScenario drawAttackerEffectScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    constexpr std::uint8_t kEffectSpace = battle::SpecialEffect::of(battle::CommandEffect::AirAnchor).index() + 1;
    attack.effect = effectAt(static_cast<std::uint8_t>(rng() % kEffectSpace));
    // A weapon's effect comes from a party member's hand.
    constexpr std::uint8_t kLastWeaponEffectIndex = 14;
    if (attack.effect.index() != 0 && attack.effect.index() <= kLastWeaponEffectIndex && s.attacker.isMonster()) {
        s.attacker = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    attack.record.hitRate = byte(rng);
    // The attacker's level as the attack loads it: a party member's 1 to 99, as every Dice and Fixed
    // Dice wielder's is.
    attack.attackerLevel = static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1));
    attack.damage = upTo(rng, battle::kDamageWordMax);
    attack.canBeCritical = oneIn(rng, 2);
    attack.automaticCritical = oneIn(rng, 4);
    attack.castingSwirlSuppressed = oneIn(rng, 2);
    attack.command = oneIn(rng, 2) ? BattleCommandId::FIGHT : static_cast<BattleCommandId>(byte(rng) % 0x20);
    attack.targets = anyBattlers(rng);
    attack.rules.randomTarget = oneIn(rng, 4);
    attack.rules.opposingSideOnly = oneIn(rng, 2);
    attack.params.attacker = s.attacker;
    attack.params.attackerIsMonster = s.attacker.isMonster();
    attack.params.targets = anyBattlers(rng);
    attack.params.targetsHit = oneIn(rng, 2) ? attack.params.targets : anyBattlers(rng);
    attack.params.noCharacterTargets = attack.params.targets.charactersOnly().empty();
    attack.params.castingSwirl = oneIn(rng, 2);
    attack.scratch.targetCount = 1 + upTo(rng, battle::kBattlerSlots - 1);
    attack.scratch.attacksLeft = upTo(rng, 3);
    attack.scratch.dragonHorn = oneIn(rng, 8);
    attack.scratch.missed = oneIn(rng, 4) ? anyBattlers(rng) : battle::BattlerSet{};
    if (oneIn(rng, 4)) {
        attack.scratch.message = static_cast<std::uint8_t>(byte(rng) % kNoOne);
    }
    if (oneIn(rng, 4)) {
        attack.scratch.followUpSpell = static_cast<AttackId>(byte(rng) % 0xFF);
    }

    state.party.gil = oneIn(rng, 4) ? static_cast<std::uint32_t>(upTo(rng, 5000))
                                    : static_cast<std::uint32_t>(upTo(rng, static_cast<int>(kMaxGil)));
    state.party.steps = static_cast<std::uint32_t>(upTo(rng, static_cast<int>(kMaxSteps)));
    state.playClockFrames = byte(rng);
    state.counterattackRunning = oneIn(rng, 4);
    // A monster that cannot be run from keeps the party from running as well (:15205-15210).
    state.escapeBlockedByMonster = oneIn(rng, 3);
    state.cannotRun = state.escapeBlockedByMonster || oneIn(rng, 2);
    state.allyCount = upTo(rng, battle::kPartySlots);
    state.golemHp = oneIn(rng, 2) ? static_cast<std::uint16_t>(upTo(rng, battle::kDamageWordMax)) : 0;
    state.endTurnAfterAction = oneIn(rng, 4);
    state.awayFromTheField = oneIn(rng, 4) ? anyBattlers(rng) : battle::BattlerSet{};
    if (oneIn(rng, 2)) {
        state.quickTurnsLeft = static_cast<std::uint8_t>(upTo(rng, 2));
        state.quickTarget = anySlot(rng);
    }
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battler.present = !oneIn(rng, 4);
        battler.inBattleMenu = oneIn(rng, 4);
        battler.menuMayOpen = oneIn(rng, 2);
        battler.monsterFlags.bits = sparse(rng);
        battler.gold = oneIn(rng, 4) ? 0 : static_cast<std::uint16_t>(upTo(rng, battle::kDamageWordMax));
        if (oneIn(rng, 2)) {
            battler.status.set(StatusId::REFLECT);
        }
        if (oneIn(rng, 4)) {
            battler.seizing = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
        }
        if (slot.isCharacter()) {
            battler.weaponHand.battlePower = byte(rng);
            battler.shieldHand.battlePower = byte(rng);
            battler.equipmentChanged = oneIn(rng, 4);
            const auto position = static_cast<std::size_t>(slot.position());
            const auto hand = [&] {
                return battle::BattleItem{.item = oneIn(rng, 2) ? ItemId::OGRE_NIX : static_cast<ItemId>(byte(rng)),
                                          .quantity = 1};
            };
            state.inventory.weaponHands[position] = hand();
            state.inventory.shieldHands[position] = hand();
        }
    }
    return s;
}

}  // namespace ostinato::trace
