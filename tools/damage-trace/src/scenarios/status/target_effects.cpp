// The port's target effect read back as named cells, the differences between two results, and the
// target-effect sweep's draw.

#include "scenarios/status/target_effects.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <variant>

#include "battle/attack.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "battle/target_effects.h"
#include "data/ai_scripts.h"
#include "data/formations.h"
#include "data/monster_attacks.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/damage.h"
#include "scenarios/status/attacker_effects.h"
#include "scenarios/status/attacker_effects_shared.h"
#include "scenarios/status/target_effects_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace detail {

std::string actionText(std::uint8_t command, std::uint8_t attack, std::uint16_t targets, std::uint8_t mp) {
    return "command " + std::to_string(command) + " attack " + std::to_string(attack) + " targets " +
           std::to_string(targets) + " mp " + std::to_string(mp);
}
}  // namespace detail

namespace {

std::string actionsText(const std::deque<battle::Action>& actions) {
    std::string out;
    for (const battle::Action& action : actions) {
        if (const auto* command = std::get_if<battle::CommandAction>(&action)) {
            out += actionText(static_cast<std::uint8_t>(command->command), static_cast<std::uint8_t>(command->attack),
                              maskOf(command->targets), command->mpCost) +
                   "; ";
        } else {
            out += "other; ";
        }
    }
    return out.empty() ? std::string{"none"} : out;
}
}  // namespace

namespace detail {

std::string immediateText(std::uint8_t command, std::uint8_t attack, std::uint16_t targets) {
    if (command == kScanCommand) {
        return "scan " + std::to_string(attack / 2);
    }
    if (command == kVisibilityCommand) {
        return "visibility mode " + std::to_string(targets & 0xFF) + " monsters " + std::to_string(targets >> 8) +
               " type " + std::to_string(attack);
    }
    return "command " + std::to_string(command) + " attack " + std::to_string(attack) + " targets " +
           std::to_string(targets);
}
}  // namespace detail

namespace {

std::string immediateActionsText(const std::deque<battle::Action>& actions) {
    std::string out;
    for (const battle::Action& action : actions) {
        if (const auto* scan = std::get_if<battle::ScanAction>(&action)) {
            out += immediateText(kScanCommand, static_cast<std::uint8_t>(slotIndex(scan->target)), 0) + "; ";
        } else if (const auto* shown = std::get_if<battle::MonsterVisibilityAction>(&action)) {
            out += immediateText(kVisibilityCommand, static_cast<std::uint8_t>(shown->type),
                                 static_cast<std::uint16_t>(static_cast<std::uint8_t>(shown->mode) |
                                                            shown->monsters.monsters.bits << 8)) +
                   "; ";
        } else if (const auto* command = std::get_if<battle::CommandAction>(&action)) {
            out += immediateText(static_cast<std::uint8_t>(command->command), static_cast<std::uint8_t>(command->attack),
                                 maskOf(command->targets)) +
                   "; ";
        } else {
            out += "other; ";
        }
    }
    return out.empty() ? std::string{"none"} : out;
}
}  // namespace

namespace detail {

std::string controlMenuText(const std::array<battle::ControlMenuEntry, 4>& menu) {
    std::string out;
    for (const battle::ControlMenuEntry& row : menu) {
        out += std::to_string(static_cast<std::uint8_t>(row.attack)) + (row.castable ? " castable " : " not castable ") +
               std::to_string(row.targeting.bits) + "; ";
    }
    return out;
}
}  // namespace detail

namespace {

// The port's script as the cartridge's queue reads: the attack's own animation the frame put there,
// each animation by its command, attack and second value, each message by its index.
std::string scriptCommandText(const battle::BattleScriptCommand& command, bool first) {
    if (const auto* animation = std::get_if<battle::AttackAnimation>(&command)) {
        if (first && animation->secondary == kUnwritten && animation->attack == static_cast<AttackId>(kUnwritten)) {
            return "the frame's animation";
        }
        return "animation " + std::to_string(commandByte(animation->what)) + " " +
               std::to_string(static_cast<std::uint8_t>(animation->attack)) + " " + std::to_string(animation->secondary);
    }
    if (const auto* message = std::get_if<battle::BattleMessage>(&command)) {
        return "message " + std::to_string(message->message);
    }
    return "other";
}
}  // namespace

TargetEffectRun runTargetEffect(DamageScenario scenario, battle::BattlerSlot target) {
    TargetEffectRun run{.after = std::move(scenario), .target = target};
    run.script.addAnimation(battle::AttackAnimation{.attack = static_cast<AttackId>(kUnwritten), .secondary = kUnwritten});
    run.missed = battle::targetEffect(run.after.state, run.after.attack, run.after.attacker, target, run.script);
    return run;
}

TargetEffectResult targetEffectResult(const TargetEffectRun& run) {
    const battle::BattleState& state = run.after.state;
    const battle::AttackInFlight& attack = run.after.attack;
    EffectCells out;
    out.put("missed by the effect", run.missed);
    putRecord(out, attack.record);
    out.put("damage", static_cast<std::uint16_t>(attack.damage));
    out.put("multiplier", static_cast<std::uint8_t>(attack.multiplier));
    out.put("command", static_cast<std::uint8_t>(attack.command));
    out.put("attack", static_cast<std::uint8_t>(attack.attack));
    out.put("second value", attack.secondary);
    out.put("targets", std::to_string(maskOf(attack.targets)));
    out.put("params", paramsText(attack.params));
    out.put("earlier targets", std::to_string(maskOf(attack.scratch.earlierTargets)));
    out.put("shown Miss", std::to_string(maskOf(attack.scratch.missed)));
    out.put("message", attack.scratch.message ? std::to_string(*attack.scratch.message) : std::string{"none"});
    out.put("message item", attack.scratch.messageItem ? static_cast<std::uint8_t>(*attack.scratch.messageItem) : kNoOne);
    out.put("message amount", static_cast<int>(attack.scratch.messageAmount.value_or(0) & 0xFFFF));
    const std::optional<AttackId>& next = attack.scratch.followUpSpell;
    out.put("spell next", next ? std::to_string(static_cast<std::uint8_t>(*next)) : std::string{"none"});
    out.put("sketched", attack.scratch.sketch ? std::to_string(attack.scratch.sketch->monster.index()) : std::string{"none"});
    out.put("counterattacks", state.scratch.countersEnabled);
    out.put("hidden without targets", attack.scratch.hideWithoutTargets);
    out.put("leaving", std::to_string(maskOf(state.leavingAfterAction)));
    out.put("party gone", state.partyLeft.characters.bits);
    out.put("monsters fallen", state.monstersFallen.monsters.bits);
    out.put("engulfed", state.engulfed.characters.bits);
    out.put("taken from the party", state.takenFromTheParty.characters.bits);
    out.put("holding an item", state.holdingItem.characters.bits);
    out.put("flipped", state.flippedByControl.monsters.bits);
    out.put("Zinger's monster", slotText(state.heldByZinger));
    out.put("Zinger swallowed", slotText(state.swallowedByZinger));
    out.put("seized", seizedSet(state).characters.bits);
    out.put("end of battle", state.endOfBattleEvent ? static_cast<int>(*state.endOfBattleEvent) : 0);
    out.put("gil", static_cast<int>(state.party.gil));
    out.put("interrupting actions", immediateActionsText(state.immediateActions));
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::string of = " of battler " + std::to_string(i);
        out.put("HP" + of, battler.hp);
        out.put("MP" + of, battler.mp);
        out.put("level" + of, battler.level);
        out.put("statuses" + of, statusesText(battler.status));
        out.put("statuses to set" + of, statusesText(battler.statusesToSet));
        out.put("statuses to clear" + of, statusesText(battler.statusesToClear));
        out.put("to work out" + of, pendingByte(battler.pendingUpdates));
        out.put("Air Anchor" + of, battler.airAnchored);
        out.put("back row" + of, battler.backRow);
        out.put("gauge empties" + of, battler.emptyGaugeAfterAction);
        out.put("menu may open" + of, battler.menuMayOpen);
        out.put("triggers" + of, triggersByte(battler.triggers));
        out.put("elements" + of, std::to_string(battler.elementsAbsorbed.bits) + "/" +
                                     std::to_string(battler.elementsNullified.bits) + "/" +
                                     std::to_string(battler.elementsWeak.bits) + "/" +
                                     std::to_string(battler.elementsHalved.bits));
        out.put("controlling" + of, linkByte(battler.controlling, battler.controlEnding));
        out.put("controlled by" + of, linkByte(battler.controlledBy, battler.controlEnding));
        out.put("seizing" + of, slotByte(battler.seizing));
        out.put("seized by" + of, slotByte(battler.seizedBy));
        out.put("charming" + of, slotByte(battler.charming));
        out.put("charmed by" + of, slotByte(battler.charmedBy));
        out.put("bodyguard" + of, slotByte(battler.bodyguard));
        out.put("guarding" + of, slotByte(battler.guarding));
        out.put("Targetting" + of, slotByte(battler.targetingTarget));
        out.put("item in hand" + of, battler.itemInHand ? static_cast<std::uint8_t>(*battler.itemInHand) : kNoOne);
        out.put("AI variable" + of, battler.aiVariable);
        out.put("items to steal" + of, std::to_string(static_cast<std::uint8_t>(battler.rareSteal)) + "/" +
                                           std::to_string(static_cast<std::uint8_t>(battler.commonSteal)));
        out.put("gold" + of, battler.gold);
        out.put("last hit with" + of,
                battler.lastHit.command ? static_cast<std::uint8_t>(battler.lastHit.command->with) : kNoOne);
        const std::uint8_t mark = markByte(battler);
        out.put("last attacker" + of, mark);
        out.put("actions" + of, actionsText(battler.actions));
        if (slot.isCharacter()) {
            out.put("control menu" + of, controlMenuText(battler.controlMenu));
        }
    }
    std::string commands;
    for (std::size_t command = 0; command < run.script.commands().size(); ++command) {
        commands += scriptCommandText(run.script.commands()[command], command == 0) + "; ";
    }
    out.put("script", commands);
    out.put("script params blocks", static_cast<int>(run.script.params().size()));
    for (std::size_t block = 0; block < run.script.params().size(); ++block) {
        out.put("script params " + std::to_string(block), paramsText(run.script.params()[block]));
    }
    out.put("random-number position", state.rng.position());
    return TargetEffectResult{.cells = std::move(out.cells)};
}

std::string differences(const TargetEffectResult& port, const TargetEffectResult& guest) {
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
namespace {

// The effects TargetEffectTbl names (battle_main.asm:10014-10112).
const std::array<battle::SpecialEffect, 46> kTargetEffectsNamed{
    battle::SpecialEffect::of(WeaponSpecialEffect::INSTANT_KILL),
    battle::SpecialEffect::of(WeaponSpecialEffect::MAN_EATER),
    battle::SpecialEffect::of(WeaponSpecialEffect::SNIPER_HAWKEYE),
    battle::SpecialEffect::of(WeaponSpecialEffect::SCIMITAR_ZANTETSUKEN),
    battle::SpecialEffect::of(AttackSpecialEffect::SCAN),
    battle::SpecialEffect::of(AttackSpecialEffect::METAMORPH),
    battle::SpecialEffect::of(AttackSpecialEffect::PALIDOR),
    battle::SpecialEffect::of(AttackSpecialEffect::TAPIR),
    battle::SpecialEffect::of(AttackSpecialEffect::EXPLODER),
    battle::SpecialEffect::of(AttackSpecialEffect::REFLECT_LORE),
    battle::SpecialEffect::of(AttackSpecialEffect::DISCHORD),
    battle::SpecialEffect::of(AttackSpecialEffect::PEP_UP),
    battle::SpecialEffect::of(AttackSpecialEffect::RIPPLER),
    battle::SpecialEffect::of(AttackSpecialEffect::STONE),
    battle::SpecialEffect::of(AttackSpecialEffect::DISABLE_COUNTERATTACK),
    battle::SpecialEffect::of(AttackSpecialEffect::MISSES_FLOATING_TARGETS),
    battle::SpecialEffect::of(AttackSpecialEffect::WALLCHANGE),
    battle::SpecialEffect::of(AttackSpecialEffect::ESCAPE),
    battle::SpecialEffect::of(AttackSpecialEffect::MIND_BLAST),
    battle::SpecialEffect::of(AttackSpecialEffect::R_POLARITY),
    battle::SpecialEffect::of(AttackSpecialEffect::LOVE_TOKEN),
    battle::SpecialEffect::of(AttackSpecialEffect::SEIZE),
    battle::SpecialEffect::of(AttackSpecialEffect::TARGETTING),
    battle::SpecialEffect::of(AttackSpecialEffect::SUPLEX),
    battle::SpecialEffect::of(AttackSpecialEffect::BABABREATH),
    battle::SpecialEffect::of(AttackSpecialEffect::CHARM),
    battle::SpecialEffect::of(AttackSpecialEffect::DOOM),
    battle::SpecialEffect::of(AttackSpecialEffect::OVERCAST),
    battle::SpecialEffect::of(AttackSpecialEffect::SNEEZE),
    battle::SpecialEffect::of(AttackSpecialEffect::ENGULF),
    battle::SpecialEffect::of(AttackSpecialEffect::ZINGER),
    battle::SpecialEffect::of(AttackSpecialEffect::EVIL_TOOT),
    battle::SpecialEffect::of(AttackSpecialEffect::PHANTASM),
    battle::SpecialEffect::of(AttackSpecialEffect::STUNNER),
    battle::SpecialEffect::of(AttackSpecialEffect::FALLEN_ONE),
    battle::SpecialEffect::of(AttackSpecialEffect::DISCARD),
    battle::SpecialEffect::of(ItemUseEffect::SUPER_BALL),
    battle::SpecialEffect::of(ItemUseEffect::SMOKE_BOMB),
    battle::SpecialEffect::of(ItemUseEffect::ELIXIR),
    battle::SpecialEffect::of(battle::CommandEffect::Possess),
    battle::SpecialEffect::of(battle::CommandEffect::Steal),
    battle::SpecialEffect::of(battle::CommandEffect::Control),
    battle::SpecialEffect::of(battle::CommandEffect::Leap),
    battle::SpecialEffect::of(battle::CommandEffect::Sketch),
    battle::SpecialEffect::of(battle::CommandEffect::Debilitator),
    battle::SpecialEffect::of(battle::CommandEffect::AirAnchor),
};

std::optional<battle::BattlerSlot> sometimes(std::mt19937& rng, unsigned n) {
    return oneIn(rng, n) ? std::optional<battle::BattlerSlot>{anySlot(rng)} : std::nullopt;
}
}  // namespace

TargetScenario drawTargetEffectScenario(std::mt19937& rng) {
    TargetScenario drawn;
    drawn.scenario = drawAttackerEffectScenario(rng);
    DamageScenario& s = drawn.scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    if (!oneIn(rng, 4)) {
        attack.effect = kTargetEffectsNamed[rng() % kTargetEffectsNamed.size()];
    }
    // A weapon's effect comes from a party member's hand, and Control fills a party member's menu.
    constexpr std::uint8_t kLastWeaponEffectIndex = 14;
    const bool weapon = attack.effect.index() != 0 && attack.effect.index() <= kLastWeaponEffectIndex;
    if ((weapon || attack.effect == battle::SpecialEffect::of(battle::CommandEffect::Control)) && s.attacker.isMonster()) {
        s.attacker = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    attack.params.attacker = s.attacker;
    attack.params.attackerIsMonster = s.attacker.isMonster();
    drawn.target = oneIn(rng, 8) ? s.attacker : anySlot(rng);
    if (oneIn(rng, 8)) {
        attack.command = BattleCommandId::JUMP;
    }
    attack.scratch.hideWithoutTargets = oneIn(rng, 4);
    attack.scratch.earlierTargets = anyBattlers(rng);
    for (std::optional<battle::BattlerSlot>& pick : attack.scratch.mindBlastPicks) {
        pick = sometimes(rng, 2);
    }

    state.formation = static_cast<FormationId>(rng() % formationAux().size());
    state.heldByZinger = sometimes(rng, 8);
    state.swallowedByZinger = sometimes(rng, 8);
    state.flippedByControl = oneIn(rng, 4) ? anyBattlers(rng).monstersOnly() : battle::BattlerSet{};
    state.engulfed = oneIn(rng, 4) ? anyBattlers(rng).charactersOnly() : battle::BattlerSet{};
    state.takenFromTheParty = oneIn(rng, 4) ? anyBattlers(rng).charactersOnly() : battle::BattlerSet{};
    state.partyLeft = oneIn(rng, 4) ? anyBattlers(rng).charactersOnly() : battle::BattlerSet{};
    state.holdingItem = oneIn(rng, 4) ? anyBattlers(rng).charactersOnly() : battle::BattlerSet{};
    state.monstersFallen = oneIn(rng, 4) ? anyBattlers(rng).monstersOnly() : battle::BattlerSet{};
    state.scratch.countersEnabled = !oneIn(rng, 4);
    const auto monsterCount = static_cast<unsigned>(monsterControls().size());
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battler.stamina = byte(rng);
        battler.instantDeathProtected = oneIn(rng, 4);
        battler.metamorph.packed = byte(rng);
        battler.rareSteal = oneIn(rng, 3) ? ItemId::EMPTY : static_cast<ItemId>(byte(rng) % 0xFF);
        battler.commonSteal = oneIn(rng, 3) ? ItemId::EMPTY : static_cast<ItemId>(byte(rng) % 0xFF);
        if (slot.isMonster()) {
            battler.monster = static_cast<MonsterId>(rng() % monsterCount);
        }
        battler.controlling = sometimes(rng, 8);
        battler.controlEnding = oneIn(rng, 4);
        battler.charming = sometimes(rng, 8);
        battler.charmedBy = sometimes(rng, 8);
        battler.seizedBy = sometimes(rng, 8);
        battler.bodyguard = sometimes(rng, 8);
        battler.guarding = sometimes(rng, 8);
        battler.targetingTarget = sometimes(rng, 8);
        battler.aiVariable = byte(rng);
        if (oneIn(rng, 4)) {
            battler.itemInHand = static_cast<ItemId>(byte(rng) % 0xFF);
        }
        battler.relicEffects3.bits = sparse(rng);
        battler.triggers.usesControlMenu = oneIn(rng, 4);
        battler.triggers.overcast = oneIn(rng, 4);
        battler.triggers.phantasm = oneIn(rng, 4);
        battler.airAnchored = oneIn(rng, 4);
        battler.emptyGaugeAfterAction = oneIn(rng, 4);
        if (oneIn(rng, 4)) {
            battler.actions.push_back(battle::CommandAction{.command = static_cast<BattleCommandId>(byte(rng) % 0x20),
                                                            .attack = static_cast<AttackId>(byte(rng)),
                                                            .targets = anyBattlers(rng),
                                                            .mpCost = byte(rng)});
        }
        if (oneIn(rng, 2)) {
            battler.lastHit.command =
                battle::Hit<BattleCommandId>{.by = anySlot(rng), .with = static_cast<BattleCommandId>(byte(rng) % 0x20)};
        }
    }
    // The target as the attack frame leaves it for the effect: out of the targets hit, marked to
    // answer the attacker, and its last command hit recorded.
    battle::Battler& target = state.at(drawn.target);
    target.lastAttacker = s.attacker;
    target.retaliationPending = true;
    target.lastHit.command = battle::Hit<BattleCommandId>{.by = s.attacker, .with = attack.queuedCommand};
    attack.params.targetsHit.remove(drawn.target);
    return drawn;
}

}  // namespace ostinato::trace
