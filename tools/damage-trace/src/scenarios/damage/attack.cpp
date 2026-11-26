// The port's whole attack read back as the cells the cartridge holds, the differences between two
// results, and the whole-attack sweeps' draws.

#include "scenarios/damage/attack.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "battle/attack.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/attack_properties.h"
#include "data/battle_commands.h"
#include "game/character_record.h"
#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"

#include "cells/damage/frame.h"
#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/attack_shared.h"
#include "scenarios/damage/damage.h"
#include "scenarios/damage/damage_shared.h"
#include "scenarios/status/attacker_effects_shared.h"
#include "scenarios/status/status.h"
#include "scenarios/status/status_shared.h"
#include "scenarios/status/target_effects.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {

// The first attack-flags byte ($B2) as the frame reads it, beside a drain capped (bit 7) and the
// cannot-be-critical bit (bit 1): clear while Runic's animation is still to show, set for the first
// pass, clear when the weapon's spell goes after the hit's targets, set while no name has been shown.
constexpr std::uint8_t kRunicShownBit = 0x40;
constexpr std::uint8_t kFirstPassBit = 0x20;
constexpr std::uint8_t kOtherTargetsBit = 0x10;
constexpr std::uint8_t kNameNotShownBit = 0x01;
// The second ($B3), beside Vanish applying (bit 7), not two-handed (bit 6), the ignore-defense bit
// (bit 4) and the automatic critical (bit 1, clear): the attacker's row ignored, an attack with no
// targets aimed again, and an attack that hits someone (clear: a Blitz whose buttons were wrong).
constexpr std::uint8_t kRowIgnoredBit = 0x20;
constexpr std::uint8_t kEmptyTargetsReaimBit = 0x04;
constexpr std::uint8_t kHitsSomeoneBit = 0x01;
// An esper's name is numbered from the first esper's attack.
constexpr std::uint8_t kFirstEsperAttack = static_cast<std::uint8_t>(EsperId::RAMUH);
}  // namespace

namespace detail {

std::string frameParamsText(battle::AttackAnimationParams params) {
    params.reflectedOff = params.reflectedOff | params.steppingIn;
    const int covered = params.coveredAlly ? params.coveredAlly->index() : 0;
    std::string text = paramsText(params) + " critical " + std::to_string(params.criticalFlash) + " monster attack " +
                       std::to_string(params.monsterAttack) + " covered " + std::to_string(covered) + " blocks";
    for (const battle::BlockShown block : params.blocks) {
        text += " " + std::to_string(static_cast<int>(block));
    }
    return text;
}

namespace {

// The animations whose handler is a bare return (CmdAnim_12 btlgfx_main.asm:27985-27991), and the
// roulette cursor's, which starts the cursor and reads nothing of the command (CmdAnim_21 :27158-27160,
// _c1b3b0 :26627-26639).
constexpr std::array<std::uint8_t, 5> kAnimationsReadingNothing{
    0x12, 0x15, 0x1E, 0x1F, static_cast<std::uint8_t>(battle::SpecialAnimation::RouletteCursor)};

bool readsNothing(std::uint8_t what) {
    return (what & kTopBit) != 0 ||
           std::find(kAnimationsReadingNothing.begin(), kAnimationsReadingNothing.end(), what) !=
               kAnimationsReadingNothing.end();
}

}  // namespace

// Runic's reads its block alone (CmdAnim_25 :27658-27661), and so does the party running away
// (CmdAnim_22 :27149-27152, _c1b8a4 :27298-27330).
bool animationReadsItsAttack(std::uint8_t what) {
    return !readsNothing(what) && what != static_cast<std::uint8_t>(battle::SpecialAnimation::RunicAbsorbsSpell) &&
           what != static_cast<std::uint8_t>(battle::SpecialAnimation::PartyRunsAway);
}

bool animationReadsItsBlock(std::uint8_t what) {
    return !readsNothing(what);
}

std::string numeralText(const battle::DamageNumber& number) {
    return std::to_string(number.target.index()) + ":" +
           (number.miss ? std::string{"miss"} : std::to_string(number.amount)) + (number.heals ? "+" : "");
}
}  // namespace detail

namespace {

// The port's script as the cartridge's queue holds it, one line per queue command; each animation, a
// bounce and a Launcher's hits with the params block it shows with. `params` are the blocks not yet
// handed to an animation, in order (BattleScriptBuilder::params()); a finished script carries each
// animation's on the animation.
std::string portScriptText(const std::vector<battle::BattleScriptCommand>& commands,
                           const std::deque<battle::AttackAnimationParams>* params) {
    std::string text;
    std::size_t nextParams = 0;
    for (const battle::BattleScriptCommand& command : commands) {
        std::visit(
            [&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, battle::AttackAnimation>) {
                    text += "animation " + std::to_string(commandByte(c.what));
                    if (animationReadsItsAttack(commandByte(c.what))) {
                        text += " " + std::to_string(static_cast<std::uint8_t>(c.attack)) + " " +
                                std::to_string(c.secondary);
                    }
                    if (params == nullptr) {
                        text += " | " + frameParamsText(c.params);
                    } else if (nextParams < params->size()) {
                        text += " | " + frameParamsText((*params)[nextParams++]);
                    } else {
                        text += " | no params";
                    }
                } else if constexpr (std::is_same_v<T, battle::AttackNameShown>) {
                    text += "name 0 " + std::to_string(static_cast<std::uint8_t>(c.attack));
                } else if constexpr (std::is_same_v<T, battle::ItemNameShown>) {
                    text += "name 1 " + std::to_string(static_cast<std::uint8_t>(c.item));
                } else if constexpr (std::is_same_v<T, battle::EsperNameShown>) {
                    text += "name 2 " +
                            std::to_string(static_cast<std::uint8_t>(static_cast<std::uint8_t>(c.esper) - kFirstEsperAttack));
                } else if constexpr (std::is_same_v<T, battle::BushidoNameShown>) {
                    text += "name 4 " + std::to_string(static_cast<std::uint8_t>(c.technique));
                } else if constexpr (std::is_same_v<T, battle::CommandNameShown>) {
                    text += "name 5 " + std::to_string(static_cast<std::uint8_t>(c.command));
                } else if constexpr (std::is_same_v<T, battle::MonsterSpecialAttackNameShown>) {
                    text += "monster name " +
                            std::to_string(c.monster ? static_cast<std::uint16_t>(*c.monster) : cells::kNoMonsterNamed);
                } else if constexpr (std::is_same_v<T, battle::BattleMessage>) {
                    text += "message " + std::to_string(c.message);
                } else if constexpr (std::is_same_v<T, battle::PoseReset>) {
                    text += "pose reset " + std::to_string(c.battler.index());
                } else if constexpr (std::is_same_v<T, battle::DamageNumbersShown>) {
                    if (c.wait == battle::DamageNumberWait::UntilGone) {
                        text += "numerals together";
                        for (const battle::DamageNumber& number : c.numbers) {
                            text += " " + numeralText(number);
                        }
                    } else {
                        for (std::size_t i = 0; i < c.numbers.size(); ++i) {
                            text += (i == 0 ? "numeral " : "; numeral ") + numeralText(c.numbers[i]);
                        }
                    }
                } else if constexpr (std::is_same_v<T, battle::ReflectedAttackShown> ||
                                     std::is_same_v<T, battle::ScatteredHitsShown>) {
                    text += shownText(c);
                } else {
                    text += "a command the attack frame does not write";
                }
            },
            command);
        text += "; ";
    }
    if (params != nullptr) {
        for (; nextParams < params->size(); ++nextParams) {
            text += "params with no animation | " + frameParamsText((*params)[nextParams]) + "; ";
        }
    }
    return text;
}
}  // namespace

namespace detail {

bool attackOwnCell(const std::string& cell) {
    static const std::set<std::string> kOwn{
        "missed by the effect", "power",       "hit rate",       "traits",         "flags 1",
        "flags 2",              "damage",      "multiplier",     "command",        "attack",
        "second value",         "targets",     "params",         "earlier targets", "shown Miss",
        "message",              "message item", "message amount", "spell next",     "sketched",
        "hidden without targets", "the weapon's spellcast", "attacks left", "damage modification",
        "re-aimed at random",   "quiet misses",
    };
    return kOwn.contains(cell) || cell.starts_with("next attack: ") || cell.starts_with("damage taken of ") ||
           cell.starts_with("damage healed of ");
}

void putApplied(EffectCells& out, const ApplyDamageResult& applied) {
    const auto entry = [](const std::optional<std::uint16_t>& value) {
        return value ? std::to_string(*value) : std::string{"none"};
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string of = " of battler " + std::to_string(i);
        out.put("HP" + of, applied.hp[i]);
        out.put("MP" + of, applied.mp[i]);
        out.put("statuses to set" + of, statusesText(applied.statusesToSet[i]));
        out.put("last attacker" + of, applied.lastAttackerMark[i]);
        out.put("last damaged by" + of, slotText(applied.lastDamagedBy[i]));
        out.put("castable spells to work out" + of, applied.enabledMagic[i]);
        out.put("damage taken" + of, entry(applied.ledger.taken[i]));
        out.put("damage healed" + of, entry(applied.ledger.healed[i]));
    }
    out.put("Black Belt targets", std::to_string(maskOf(applied.blackBeltEligible)));
    out.put("shown Miss", std::to_string(maskOf(applied.missed)));
    out.put("the Golem's HP", applied.golemHp);
    out.put("the weapon's spellcast", applied.weaponSpellcast);
}

void putStatusChanges(EffectCells& out, const StatusChangesResult& changes) {
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string of = " of battler " + std::to_string(i);
        out.put("statuses" + of, statusesText(changes.status[i]));
        out.put("statuses to set" + of, statusesText(changes.statusesToSet[i]));
        out.put("statuses to clear" + of, statusesText(changes.statusesToClear[i]));
        out.put("statuses for a counterattack" + of, statusesText(changes.statusForCounter[i]));
        out.put("to work out again" + of, changes.pendingUpdates[i]);
        out.put("menu may open" + of, changes.menuMayOpen[i]);
        out.put("gauge empties after the action" + of, changes.emptyGaugeAfterAction[i]);
        out.put("Phantasm" + of, changes.phantasm[i]);
        out.put("sleep timer" + of, changes.sleepTimer[i]);
        out.put("stop timer" + of, changes.stopTimer[i]);
        out.put("reflect timer" + of, changes.reflectTimer[i]);
        out.put("freeze timer" + of, changes.freezeTimer[i]);
    }
    for (std::size_t i = 0; i < battle::kPartySlots; ++i) {
        out.put("equipment to work out of party slot " + std::to_string(i), changes.equipmentChangeFlags[i]);
    }
    out.put("the windows' wallpaper", changes.menuWindowDesign);
    out.put("died this action", std::to_string(maskOf(changes.diedAwaitingCounter)));
    out.put("statuses changed", std::to_string(maskOf(changes.statusChanged)));
    out.put("monsters standing", std::to_string(maskOf(changes.monstersStanding)));
}

void putEffectCells(EffectCells& out, const std::map<std::string, std::string>& cells) {
    for (const auto& [cell, value] : cells) {
        if (cell != "script" && cell != "params" && !cell.starts_with("script params")) {
            out.cells[cell] = value;
        }
    }
}

std::uint8_t firstFlagsByte(const battle::AttackInFlight& attack) {
    std::uint8_t first = kAttackFlagsAtStart;
    const auto clearIf = [&](bool condition, std::uint8_t bit) {
        if (condition) {
            first = static_cast<std::uint8_t>(first & ~bit);
        }
    };
    clearIf(!attack.drainCapped, kDrainCappedBit);
    clearIf(attack.runicAbsorbing, kRunicShownBit);
    clearIf(!attack.firstPass, kFirstPassBit);
    clearIf(attack.followUpSameTargets, kOtherTargetsBit);
    clearIf(attack.canBeCritical, kCannotBeCriticalBit);
    clearIf(attack.nameShown, kNameNotShownBit);
    return first;
}

std::uint8_t secondFlagsByte(const battle::AttackInFlight& attack) {
    std::uint8_t second = kAttackFlagsAtStart;
    const auto clearIf = [&](bool condition, std::uint8_t bit) {
        if (condition) {
            second = static_cast<std::uint8_t>(second & ~bit);
        }
    };
    clearIf(!attack.vanishApplies, kVanishAppliesBit);
    clearIf(attack.twoHanded, kNotTwoHanded);
    clearIf(!attack.rowIgnored, kRowIgnoredBit);
    clearIf(!attack.ignoreDefenseStopsMultiplier, kIgnoreDefenseStopsMultiplier);
    clearIf(!attack.emptyTargetsReaim, kEmptyTargetsReaimBit);
    clearIf(attack.automaticCritical, kAutomaticCriticalClearBit);
    clearIf(attack.hitsNoOne, kHitsSomeoneBit);
    return second;
}

void putNextAttack(EffectCells& out, std::uint8_t first, std::uint8_t second) {
    out.put("next attack: drain capped", (first & kDrainCappedBit) != 0);
    out.put("next attack: Runic's animation to show", (first & kRunicShownBit) == 0);
    out.put("next attack: first pass", (first & kFirstPassBit) != 0);
    out.put("next attack: the weapon's spell after the hit's targets", (first & kOtherTargetsBit) == 0);
    out.put("next attack: can be critical", (first & kCannotBeCriticalBit) == 0);
    out.put("next attack: name shown", (first & kNameNotShownBit) == 0);
    out.put("next attack: Vanish applies", (second & kVanishAppliesBit) != 0);
    out.put("next attack: two-handed", (second & kNotTwoHanded) == 0);
    out.put("next attack: row ignored", (second & kRowIgnoredBit) != 0);
    out.put("next attack: ignore-defense stops the multiplier", (second & kIgnoreDefenseStopsMultiplier) != 0);
    out.put("next attack: no targets aims again", (second & kEmptyTargetsReaimBit) != 0);
    out.put("next attack: automatic critical", (second & kAutomaticCriticalClearBit) == 0);
    out.put("next attack: hits no one", (second & kHitsSomeoneBit) == 0);
}

AttackResult portAttackCells(const battle::BattleState& state, const battle::AttackInFlight& attack,
                             const std::vector<battle::BattleScriptCommand>& commands,
                             const std::deque<battle::AttackAnimationParams>* params, bool awayUnreachable,
                             bool attackCells) {
    EffectCells out;
    battle::BattleScriptBuilder none;
    putApplied(out, portApplied(state, attack, none));
    putStatusChanges(out, statusChangesOf(state));
    DamageScenario scenario{.state = state, .attack = attack};
    TargetEffectRun run{.after = std::move(scenario), .missed = !attack.scratch.missedByEffect.empty()};
    putEffectCells(out, targetEffectResult(run).cells);
    // The sketch cell holds the sketcher while it makes its attack, and the monster a Sketch caught.
    std::optional<battle::BattlerSlot> sketched = attack.scratch.sketcher;
    if (attack.scratch.sketch) {
        sketched = attack.scratch.sketch->monster;
    }
    out.put("sketched", slotText(sketched));
    out.put("flipped", std::to_string(maskOf(state.flippedByControl)));
    // A message's item and amount stay in their cells for the script to show; the port's message
    // carries them.
    for (const battle::BattleScriptCommand& command : commands) {
        if (const auto* message = std::get_if<battle::BattleMessage>(&command)) {
            if (message->firstNumber) {
                out.put("message item", static_cast<std::uint8_t>(*message->firstNumber));
            }
            if (message->secondNumber) {
                out.put("message amount", static_cast<int>(*message->secondNumber & 0xFFFF));
            }
        }
    }
    out.put("script", portScriptText(commands, params));
    putNextAttack(out, firstFlagsByte(attack), secondFlagsByte(attack));
    out.put("attacks left", static_cast<std::uint8_t>(attack.scratch.attacksLeft));
    out.put("damage modification", attack.scratch.damageModification);
    out.put("re-aimed at random", attack.scratch.randomizeTargets);
    out.put("quiet misses", attack.scratch.quietMisses);
    out.put("away from the field unreachable", awayUnreachable);
    out.put("Interceptor guarding", slotText(state.scratch.interceptorGuarding));
    if (!attackCells) {
        std::erase_if(out.cells, [](const auto& cell) { return attackOwnCell(cell.first); });
    }
    return AttackResult{.cells = std::move(out.cells)};
}
}  // namespace detail

namespace {

AttackRun runPortAttack(DamageScenario scenario, bool self) {
    AttackRun run{.after = std::move(scenario)};
    battle::BattleHooks hooks;
    if (self) {
        battle::runSelfAttack(run.after.state, hooks, run.script, run.after.attack, run.after.attacker);
    } else {
        battle::runAttack(run.after.state, hooks, run.script, run.after.attack, run.after.attacker);
    }
    return run;
}
}  // namespace

AttackRun portAttack(DamageScenario scenario) {
    return runPortAttack(std::move(scenario), false);
}

AttackRun portSelfAttack(DamageScenario scenario) {
    return runPortAttack(std::move(scenario), true);
}

AttackResult attackResult(const AttackRun& run) {
    return portAttackCells(run.after.state, run.after.attack, run.script.commands(), &run.script.params(),
                           run.after.attack.scratch.awayFromTheFieldUnreachable, true);
}

std::string differences(const AttackResult& port, const AttackResult& guest) {
    return cellDifferences(port.cells, guest.cells);
}

DamageScenario drawAttackScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetEffectScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    battle::Battler& attacker = state.at(s.attacker);

    // The attack as a command's body hands it to the frame: Fight three times in eight, Capture once,
    // otherwise any other command with an aiming byte.
    const unsigned pick = rng() % 8;
    BattleCommandId command = pick < 3 ? BattleCommandId::FIGHT : BattleCommandId::CAPTURE;
    while (pick > 3 && (command == BattleCommandId::FIGHT || command == BattleCommandId::CAPTURE)) {
        command = static_cast<BattleCommandId>(rng() % kCommandTargetingInit.size());
    }
    attack.command = command;
    attack.queuedCommand = command;
    attack.attack = static_cast<AttackId>(byte(rng));
    attack.queuedAttack = static_cast<AttackId>(byte(rng));
    attack.targeting = Targeting{byte(rng)};
    attack.targets = oneIn(rng, 4) ? battle::BattlerSet{} : anyBattlers(rng);
    attack.rules = battle::AimingRules{.noCharacterTargets = oneIn(rng, 4),
                                       .randomTarget = oneIn(rng, 4),
                                       .fallBackToEarlierTargets = oneIn(rng, 4),
                                       .mayTargetTheFallen = oneIn(rng, 4),
                                       .keepTargetsThatAreGone = oneIn(rng, 4),
                                       .opposingSideOnly = oneIn(rng, 4),
                                       .excludeAttacker = oneIn(rng, 4)};
    constexpr std::array<battle::AttackNameKind, 8> kinds{
        battle::AttackNameKind::Attack,  battle::AttackNameKind::Item,    battle::AttackNameKind::Esper,
        battle::AttackNameKind::Bushido, battle::AttackNameKind::Command, battle::AttackNameKind::MonsterSpecialAttack,
        battle::AttackNameKind::JokerDoom, battle::AttackNameKind::Blitz};
    const unsigned kind = rng() % 12;
    attack.scratch.nameKind.reset();
    if (kind < kinds.size() && (kinds[kind] != battle::AttackNameKind::MonsterSpecialAttack || s.attacker.isMonster())) {
        attack.scratch.nameKind = kinds[kind];
    }
    attacker.specialAttack.packed = byte(rng);
    attacker.specialAttackAnimation = static_cast<MonsterAttackAnimation>(byte(rng));
    // A monster's special attack is named for it only as the Special command's attack ($EF) loads it
    // (InitAttacker), with that attack's own record.
    if (attack.scratch.nameKind == battle::AttackNameKind::MonsterSpecialAttack) {
        attack.attack = AttackId::SPECIAL;
        battle::loadAttackRecord(attack, AttackId::SPECIAL);
    }
    attack.record.mpCost = oneIn(rng, 2) ? 0 : byte(rng);
    attack.scratch.mpCost = 0;
    if (oneIn(rng, 2)) {
        attack.scratch.mpCost = byte(rng);
        if (oneIn(rng, 4) && attacker.mp < 0xFF) {
            attack.scratch.mpCost = static_cast<std::uint8_t>(attacker.mp + 1 + upTo(rng, 0xFE - attacker.mp));
        }
    }
    attack.scratch.quietMisses = oneIn(rng, 4);
    attack.scratch.aimExactly = oneIn(rng, 8);
    attack.scratch.randomizeTargets = oneIn(rng, 4);
    attack.scratch.currentTargets = anyBattlers(rng);
    attack.scratch.weaponSpellcast = {};
    if (oneIn(rng, 4)) {
        attack.scratch.weaponSpellcast.raw =
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(SpellCastMode::RANDOM_ON_ATTACK) | (byte(rng) & 0x3F));
    }
    if (oneIn(rng, 16)) {
        attack.scratch.sketcher = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    attack.firstPass = !oneIn(rng, 4);
    attack.extraMultiplier = upTo(rng, 3);
    attack.scratch.attacksLeft = upTo(rng, 2);
    attack.canBeCritical = oneIn(rng, 2);
    if (attack.canBeCritical) {
        attack.rules.opposingSideOnly = true;
    }
    attack.automaticCritical = oneIn(rng, 16);
    attack.emptyTargetsReaim = !oneIn(rng, 8);
    attack.rowIgnored = !oneIn(rng, 4);
    attack.hitsNoOne = oneIn(rng, 16);
    attack.nameShown = oneIn(rng, 8);

    // The attacker's statuses that change its attack, and Air Anchor on it.
    for (const StatusId status : {StatusId::MORPH, StatusId::BERSERK, StatusId::IMP, StatusId::SILENCE}) {
        if (oneIn(rng, 8)) {
            attacker.status.set(status);
        } else {
            attacker.status.clear(status);
        }
    }
    attacker.airAnchored = oneIn(rng, 8);
    if (oneIn(rng, 4)) {
        attack.record.flags1.set(AttackFlag1::ENABLE_RUNIC);
    }
    if (oneIn(rng, 12)) {
        state.extraHits = 1 + upTo(rng, 7);
    } else {
        state.extraHits.reset();
    }
    state.facingRight = anyBattlers(rng);
    constexpr std::array<battle::BattleType, 3> layouts{battle::BattleType::Side, battle::BattleType::Pincer,
                                                         battle::BattleType::Front};
    state.battleType = layouts[rng() % layouts.size()];
    if (oneIn(rng, 4)) {
        battle::Battler& strago = state.at(battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1)));
        strago.actor = CharacterPropId::STRAGO;
        // Half the time in none of the statuses that keep him from learning (LearnLore :8800-8802).
        if (oneIn(rng, 2)) {
            for (const StatusId status : {StatusId::DEAD, StatusId::PETRIFY, StatusId::BLIND, StatusId::ZOMBIE,
                                          StatusId::SLEEP, StatusId::CONFUSE, StatusId::BERSERK, StatusId::STOP,
                                          StatusId::FROZEN, StatusId::RAGE, StatusId::HIDE}) {
                strago.status.clear(status);
            }
        }
        if (oneIn(rng, 2)) {
            attack.record.flags1.set(AttackFlag1::LEARNABLE_LORE);
            // One of the lores, for Strago to see.
            if (oneIn(rng, 2)) {
                attack.attack = static_cast<AttackId>(static_cast<std::uint8_t>(AttackId::CONDEMNED) +
                                                      upTo(rng, static_cast<int>(game::kLores) - 1));
            }
        }
    }
    for (std::size_t lore = 0; lore < game::kLores; ++lore) {
        state.party.loresKnown.set(lore, oneIn(rng, 2));
        state.loresLearned.set(lore, oneIn(rng, 4));
    }
    for (battle::Battler& battler : state.battlers) {
        battler.vigor = byte(rng);
        battler.magicPower = byte(rng);
        battler.triggers.runic = oneIn(rng, 8);
        battler.triggers.monsterRunic = oneIn(rng, 8);
    }
    // No attack removes and toggles its statuses at once; on both the cartridge runs unrelated code.
    if (attack.record.flags2.has(AttackFlag2::REMOVE_STATUS)) {
        attack.record.flags2.clear(AttackFlag2::TOGGLE_STATUS);
    }
    // An effect comes on a record it can come on: a weapon's and a command's ride the plain hit, which
    // ignores Reflect, and an attack's own rides an attack that can bounce off Reflect only if one
    // that can carries it. Sketch's command makes one attack with no spell to cast after it.
    static const std::set<std::uint8_t> kBouncingEffects = [] {
        std::set<std::uint8_t> effects;
        for (int id = 0; id <= 0xFF; ++id) {
            const AttackProperties& record = getAttackProperties(static_cast<AttackId>(id));
            if (!record.flags1.has(AttackFlag1::IGNORE_REFLECT)) {
                effects.insert(battle::SpecialEffect::of(record.specialEffect).index());
            }
        }
        return effects;
    }();
    if (!kBouncingEffects.contains(attack.effect.index())) {
        attack.record.flags1.set(AttackFlag1::IGNORE_REFLECT);
    }
    if (attack.effect == battle::SpecialEffect::of(battle::CommandEffect::Sketch)) {
        attack.scratch.attacksLeft = 0;
        attack.scratch.followUpSpell.reset();
        attack.scratch.weaponSpellcast = {};
    }

    // Fight and Capture as their body hands them on (fightAttack): each attack loads a hand, two
    // swings or an Offering's eight, the plain hit (a monster's special attack one time in four),
    // able to be critical and named by nothing but a monster's special attack. A party member's hands
    // are drawn whole, the shield hand empty one time in two and the weapon hand without power one
    // time in eight; a monster's as loading it leaves them, a weapon hand with its attack's power,
    // graphic and hit rate and nothing else (only a party member's equipment fills the rest).
    if (command == BattleCommandId::FIGHT || command == BattleCommandId::CAPTURE) {
        attack.scratch.fightCommand = command;
        attack.attack = s.attacker.isMonster() && oneIn(rng, 4) ? AttackId::SPECIAL : AttackId::BATTLE;
        attack.queuedAttack = attack.attack;
        const bool offering = oneIn(rng, 4);
        if (offering) {
            attacker.relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
        } else {
            attacker.relicEffects4.clear(RelicEffect4::FIGHT_TO_X_FIGHT);
        }
        attack.scratch.attacksLeft = offering ? 7 : 1;
        attack.scratch.dragonHorn = false;
        attack.canBeCritical = true;
        attack.scratch.nameKind.reset();
        const auto drawHand = [&](battle::Hand& hand, bool empty, bool monster) {
            hand = battle::Hand{};
            if (empty) {
                return;
            }
            hand.item = static_cast<ItemId>(byte(rng));
            hand.battlePower = oneIn(rng, 8) ? 0 : byte(rng);
            hand.hitRate = byte(rng);
            if (monster) {
                return;
            }
            hand.element.bits = sparse(rng);
            hand.weaponFlags.bits = byte(rng);
            hand.spellCast.raw = oneIn(rng, 4) ? byte(rng) : 0;
            hand.blockEffect.packed = byte(rng);
        };
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
            battle::Battler& battler = state.at(slot);
            drawHand(battler.weaponHand, false, slot.isMonster());
            drawHand(battler.shieldHand, slot.isMonster() || oneIn(rng, 2), slot.isMonster());
        }
    }

    // The links as the battle makes them, each with both its ends: a party member controlling a
    // monster, a monster holding a party member in Seize, a battler charming another, and a Love
    // Token's bodyguard. A battler is at one end of each kind of link at most.
    for (battle::Battler& battler : state.battlers) {
        battler.controlling.reset();
        battler.controlledBy.reset();
        battler.controlEnding = false;
        battler.seizing.reset();
        battler.seizedBy.reset();
        battler.charming.reset();
        battler.charmedBy.reset();
        battler.bodyguard.reset();
        battler.guarding.reset();
    }
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        const battle::BattlerSlot monster = battle::BattlerSlot::monster(position);
        const battle::BattlerSlot member = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
        if (oneIn(rng, 6) && !state.at(member).controlling) {
            const bool ending = oneIn(rng, 4);
            state.at(member).controlling = monster;
            state.at(member).controlEnding = ending;
            state.at(monster).controlledBy = member;
            state.at(monster).controlEnding = ending;
        }
        if (oneIn(rng, 8) && !state.at(member).seizedBy && !state.at(monster).seizing) {
            state.at(monster).seizing = member;
            state.at(member).seizedBy = monster;
        }
    }
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        const battle::BattlerSlot other = anySlot(rng);
        if (other == slot) {
            continue;
        }
        if (oneIn(rng, 10) && !state.at(slot).charmedBy && !state.at(other).charming) {
            state.at(slot).charmedBy = other;
            state.at(other).charming = slot;
        }
        if (oneIn(rng, 10) && !state.at(slot).bodyguard && !state.at(other).guarding) {
            state.at(slot).bodyguard = other;
            state.at(other).guarding = slot;
        }
    }
    return s;
}

DamageScenario drawSelfAttackScenario(std::mt19937& rng) {
    DamageScenario s = drawAttackScenario(rng);
    battle::AttackInFlight& attack = s.attack;
    attack.command = oneIn(rng, 4) ? BattleCommandId::MAGIC : battle::kNothingShown;
    // The battle's own actions load no hand; the attacks left are drawn as for the other commands.
    if (std::exchange(attack.scratch.fightCommand, std::nullopt)) {
        attack.scratch.attacksLeft = upTo(rng, 2);
    }
    for (std::uint8_t& status : attack.record.statuses.bytes) {
        status = sparse(rng);
    }
    if (oneIn(rng, 2)) {
        attack.record.flags2.set(AttackFlag2::REMOVE_STATUS);
        attack.record.flags2.clear(AttackFlag2::TOGGLE_STATUS);
    } else {
        attack.record.flags2.clear(AttackFlag2::REMOVE_STATUS);
    }
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    attack.attackerLevel = oneIn(rng, 2) ? 0 : byte(rng);
    attack.attackerStat = oneIn(rng, 2) ? 0 : byte(rng);
    attack.castingSwirlSuppressed = oneIn(rng, 2);
    const auto drawn = [&](auto& flags, auto flag) {
        if (oneIn(rng, 4)) {
            flags.set(flag);
        } else {
            flags.clear(flag);
        }
    };
    drawn(attack.record.flags2, AttackFlag2::DRAIN);
    drawn(attack.record.flags2, AttackFlag2::RESTORE_HP_MP);
    drawn(attack.record.traits, AttackTrait::IGNORE_DEFENSE);
    drawn(attack.record.traits, AttackTrait::NO_DAMAGE_SPLIT);
    drawn(attack.record.traits, AttackTrait::INVERT_ON_UNDEAD);
    return s;
}

}  // namespace ostinato::trace
