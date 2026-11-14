// A traced AI call rebuilt as a battle from the cells it read, and replayed against the port.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "ai_script_bytes.h"
#include "battle/ai.h"
#include "battle/ai_targets.h"
#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/char_ai.h"
#include "fixtures/ai_scripts_expected.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

#include "cells/ai/ai.h"
#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"
#include "oracle/routines.h"
#include "scenarios/ai/ai_run_shared.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

// The step at `place` in the cartridge's script block; none for 0, the place of a part that has
// never stopped at a wait (no step after a wait is the block's first).
const AiStep* stepAt(std::uint16_t place) {
    if (place == 0) {
        return nullptr;
    }
    for (const auto& [step, at] : stepPlaces()) {
        if (at == place) {
            return step;
        }
    }
    throw std::runtime_error("a script place, " + std::to_string(place) + ", that is no step of the cartridge's scripts");
}

// The script whose turn part starts at `place`, if one does.
std::optional<std::uint16_t> scriptWithTurnAt(std::uint16_t place) {
    for (const test::ExpectedAiScript& row : test::kExpectedAiScripts) {
        if (row.turnOffset == place) {
            return row.id;
        }
    }
    return std::nullopt;
}

// The script part that starts at `place`: a script's turn part, or its counterattack part; none for
// $FFFF. Mag Roader's counterattack part starts where Wild Cat's turn part does, and is the same steps.
std::span<const AiStep> partAt(std::uint16_t place) {
    if (place == kNoPlace) {
        return {};
    }
    for (const test::ExpectedAiScript& row : test::kExpectedAiScripts) {
        const AiScript& script = aiScript(row.id);
        if (row.turnOffset == place) {
            return script.turn;
        }
        if (row.counterOffset && *row.counterOffset == place) {
            return script.counter;
        }
    }
    throw std::runtime_error("a script place, " + std::to_string(place) + ", where no script part starts");
}
}  // namespace

AiScenario DamageOracle::readAiScenario(battle::BattlerSlot actor) {
    AiScenario s;
    s.actor = actor;
    battle::BattleState& state = s.state;
    const battle::BattlerSet enemies = setOf(readWord(kEnemyCharacters));
    const std::uint8_t notSeized = readByte(kReachCharacters);
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        battle::Battler& battler = state.at(slot);
        const auto at = static_cast<std::uint32_t>(index * 2);
        if (slot.isCharacter()) {
            // A party slot's member and record, none negative.
            const std::uint8_t member = readByte(kActor + at);
            battler.actor = (member & kTopBit) != 0 ? std::nullopt
                                                    : std::optional<CharacterPropId>(static_cast<CharacterPropId>(member));
            const std::uint8_t record = readByte(kRecordIndex + at);
            battler.record = (record & kTopBit) != 0 ? std::nullopt : std::optional<std::size_t>(record);
            // A party member held in Seize is out of reach; who holds it the reach check does not read.
            if ((notSeized & 1u << slot.position()) == 0) {
                battler.seizedBy = battle::BattlerSlot::monster(0);
            }
            battler.actsAsEnemy = enemies.has(slot);
        } else {
            const std::uint16_t monster = readWord(kMonsterIndex + at);
            battler.monster =
                (monster & 0x8000) != 0 ? std::nullopt : std::optional<MonsterId>(static_cast<MonsterId>(monster));
        }
        const std::uint8_t field = readByte(kOnTheField + at);
        battler.present = (field & kPresentBit) != 0;
        battler.inBattleMenu = (field & kInBattleMenuBit) != 0;
        battler.menuMayOpen = (field & kMenuMayOpenBit) != 0;
        const auto statuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{
                {readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at), readByte(first34 + at + 1)}};
        };
        battler.status = statuses(kStatus12, kStatus34);
        battler.statusesToSet = statuses(kToSet12, kToSet34);
        battler.statusesToClear = statuses(kToClear12, kToClear34);
        battler.statusForCounter = statuses(kCounterStatus12, kCounterStatus34);
        battler.hp = readWord(kHp + at);
        battler.mp = readWord(kMp + at);
        battler.level = readByte(kLevel + at);
        battler.elementsWeak.bits = readByte(kWeakHalved + at);
        const std::uint8_t mark = readByte(kLastAttacker + at);
        const int lastAttacker = mark & ~kWaitingBit;
        battler.lastAttacker = lastAttacker < battle::kBattlerSlots
                                   ? std::optional<battle::BattlerSlot>(battle::BattlerSlot::fromIndex(lastAttacker))
                                   : std::nullopt;
        battler.retaliationPending = (mark & kWaitingBit) != 0;
        battler.targetingTarget = slotOrNone(readByte(kTargetingTarget + at));
        const auto hit = [&](std::uint32_t byCell, std::uint32_t withCell, auto with) {
            using With = decltype(with);
            const std::optional<battle::BattlerSlot> who = slotOrNone(readByte(byCell + at));
            return who ? std::optional<battle::Hit<With>>(battle::Hit<With>{.by = *who, .with = With{readByte(withCell + at)}})
                       : std::nullopt;
        };
        battler.lastHit.command = hit(kLastHitCommandBy, kLastHitCommand, BattleCommandId{});
        battler.lastHit.spell = hit(kLastHitSpellBy, kLastHitSpell, AttackId{});
        battler.lastHit.item = hit(kLastHitItemBy, kLastHitItem, ItemId{});
        if (const std::optional<battle::BattlerSlot> who = slotOrNone(readByte(kLastHitElementBy + at))) {
            battler.lastHit.element = battle::Hit<ElementSet>{.by = *who, .with = ElementSet{readByte(kLastHitElement + at)}};
        }
        battler.lastDamagedBy = slotTimesTwo(readByte(kLastDamagedBy + at));
        battler.aiVariable = readByte(kAiVariable + at);
        battler.turnTimer = readWord(kOwnTimer + at);
        battler.atbRate = readWord(kAtbRate + at);
        battler.charmedBy = slotOrNone(readByte(kCharmedBy + at));
        battler.stamina = readByte(kStamina + at);
        battler.evadeFactor = readByte(kEvadeAndMagicBlock + at);
        battler.magicBlockFactor = readByte(kEvadeAndMagicBlock + at + 1);
        battler.physicalBlockGraphics.bits = readByte(kBlockGraphics + at);
        battler.magicBlockGraphics.bits = readByte(kBlockGraphics + at + 1);
        battler.triggers.retort = (readByte(kRetort + at) & 1) != 0;
        const std::uint8_t wait = readByte(kAdvanceWait + at);
        battler.advanceWait = wait == kUnsetWait ? std::nullopt : std::optional<std::uint8_t>(wait);
        battler.turnScript = partAt(readWord(kTurnPlace + at));
        battler.counterScript = partAt(readWord(kCounterPlace + at));
        battler.hasCounterScript = !battler.counterScript.empty();
        battler.turnResume = battle::AiResume{.blocksSkipped = readByte(kTurnSkipped + at),
                                              .at = stepAt(readWord(kTurnWaitPlace + at))};
        battler.counterResume = battle::AiResume{.blocksSkipped = readByte(kCounterSkipped + at),
                                                 .at = stepAt(readWord(kCounterWaitPlace + at))};
    }

    // The reach check's cells as the game keeps them: on the field or about to be, the second cell
    // (counted in the battle), the monsters the AI has not hidden and the party members not seized
    // (above).
    state.targetableOrPresent = setOf(readWord(kReachFirst));
    state.countedInBattle = setOf(readWord(kReachSecond));
    state.aiUntargetable =
        battle::BattlerSet{BattleSlotMask{}, BattleSlotMask(static_cast<std::uint8_t>(~readByte(kAiTargetable) & kMonsterSlotBits))};
    state.scratch.countersEnabled = (readByte(kCountersEnabled) & kCountersOn) != 0;
    state.awayFromTheFieldUnreachable = (readByte(kReachKeepsAway) & kTopBit) != 0;
    state.awayFromTheField = setOf(readWord(kAwayFromTheField));
    state.battleType = static_cast<battle::BattleType>(readByte(kBattleType));
    state.pincerSides[0] = BattleSlotMask(readByte(kPincerFirstSide));
    state.pincerSides[1] = BattleSlotMask(readByte(kPincerSecondSide));

    state.alive = setOf(readWord(kAlive));
    state.allyCount = readByte(kAllyCount);
    state.enemyCount = readByte(kEnemyCount);
    state.battleCounter = readWord(kBattleTimer);
    for (std::size_t i = 0; i < state.battleVariables.size(); ++i) {
        state.battleVariables[i] = readByte(kBattleVariables + static_cast<std::uint32_t>(i));
    }
    for (std::size_t i = 0; i < state.switches.variables.size(); ++i) {
        state.switches.variables[i] = readByte(kBattleSwitches + static_cast<std::uint32_t>(i));
    }
    state.monsterNamesListed = readByte(kNamesListed);
    state.gauRecruited = (readByte(kStoryFlags) & kGauRecruitedBit) != 0;
    state.formation = static_cast<FormationId>(readWord(kFormation));
    state.rng = battle::BattleRng{readByte(kRngPosition)};
    state.invincible = setOf(readWord(kInvincible));
    const std::uint8_t event = readByte(kEndOfBattleEvent);
    state.endOfBattleEvent =
        event == 0 ? std::nullopt : std::optional<battle::EndOfBattleEvent>(static_cast<battle::EndOfBattleEvent>(event / 2));
    state.party.currentParty = readByte(kPartyPlayed);
    for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
        const std::uint8_t value = readByte(kPartyBytes + static_cast<std::uint32_t>(record));
        state.party.membership[record] =
            game::PartyMembership{.party = static_cast<std::uint8_t>(value & kPartyNumberBits),
                                  .slot = static_cast<std::uint8_t>(value >> kPartySlotShift & kPartySlotBits),
                                  .backRow = (value & kPartyRowBit) != 0,
                                  .enabled = (value & kRecordEnabledBit) != 0,
                                  .visible = (value & kRecordShownBit) != 0};
    }

    state.counterattackRunning = (readByte(kBattleFlags) & kCounterattackRunningBit) != 0;
    state.counterAiRanThisStep = setOf(static_cast<std::uint16_t>(~readWord(kCounterAiNotRun)));
    state.diedAwaitingCounter = setOf(readWord(kDiedAwaitingCounter));
    state.attackedThisStep = setOf(static_cast<std::uint16_t>(~readWord(kNotAttackedThisStep)));
    state.quickTarget = slotOrNone(readByte(kQuickTarget));
    const std::uint8_t quickTurns = readByte(kQuickTurns);
    state.quickTurnsLeft = quickTurns == kNoQuick ? std::nullopt : std::optional<std::uint8_t>(quickTurns);
    const std::uint8_t end = readByte(kCounterQueueEnd);
    for (std::uint8_t i = readByte(kCounterQueueStart); i < end; ++i) {
        state.counterQueue.push(battle::BattlerSlot::fromIndex((readByte(kCounterQueue + i) / 2) % battle::kBattlerSlots));
    }

    s.run = battle::AiRun{.targets = setOf(readWord(kAiTargets)),
                          .startingTargets = setOf(readWord(kAiStartingTargets)),
                          .restricted = readByte(kAiRestricted) != 0};
    s.answering = setOf(readWord(kTargets));
    return s;
}
namespace {

// The battler whose per-battler cell, one word at twice its index from `first`, the call read; none
// when it read none, and an error when it read more than one battler's.
std::optional<battle::BattlerSlot> battlerRead(const TracedCall& call, std::uint32_t first, const char* what) {
    std::optional<battle::BattlerSlot> found;
    for (const auto& [cell, value] : call.inputs) {
        if (cell < first || cell >= first + 2 * battle::kBattlerSlots) {
            continue;
        }
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>((cell - first) / 2));
        if (found && *found != slot) {
            throw std::runtime_error(call.routine + " read more than one battler's " + what);
        }
        found = slot;
    }
    return found;
}

// The actions of `after` the call added: what follows the ones `before` held.
std::vector<std::string> added(std::vector<std::string> after, const std::vector<std::string>& before) {
    if (after.size() >= before.size() && std::equal(before.begin(), before.end(), after.begin())) {
        after.erase(after.begin(), after.begin() + static_cast<std::ptrdiff_t>(before.size()));
    }
    return after;
}

// One step of the scripts that queues one of the three commands, as the action it queues.
struct CommandStep {
    std::size_t command = 0;  // 0 the variable, 1 the switch, 2 the effect
    std::vector<std::uint8_t> bytes;
    battle::Action action;
};

// Every action of the three commands a step can hold: each variable set, raised or lowered by 0 to
// 63; each bit of a variable and each battle switch toggled, set or cleared; and each effect, with
// each target for the five that read one and each status for SetStatus and ClearStatus. Nothing,
// which no script holds, is among them: the cartridge's routine for it runs all the same.
const std::vector<CommandStep>& commandSteps() {
    static const std::vector<CommandStep> steps = [] {
        constexpr std::uint8_t kLargestAmount = 63;
        constexpr std::uint8_t kLastBit = 7;
        constexpr int kBattleSwitchCount = 160;
        constexpr int kStatusCount = 32;
        std::vector<CommandStep> all;
        std::vector<AiVariable> variables{AiVariable::own()};
        for (std::uint8_t index = 0; index < kAiBattleVariables; ++index) {
            variables.push_back(AiVariable::battle(index));
        }
        for (const AiVariable variable : variables) {
            for (const AiVariableChange change : {AiVariableChange::Set, AiVariableChange::Add, AiVariableChange::Subtract}) {
                for (std::uint8_t amount = 0; amount <= kLargestAmount; ++amount) {
                    const battle::BattleVariableAction a{.variable = variable, .change = change, .amount = amount};
                    all.push_back({0, stepBytesFor(a), a});
                }
            }
        }
        std::vector<AiSwitch> switches;
        for (std::uint8_t bit = 0; bit <= kLastBit; ++bit) {
            switches.push_back(AiSwitch::own(bit));
            for (std::uint8_t index = 0; index < kAiBattleVariables; ++index) {
                switches.push_back(AiSwitch::battleVariable(index, bit));
            }
        }
        for (int number = 0; number < kBattleSwitchCount; ++number) {
            switches.push_back(AiSwitch::battleSwitch(number));
        }
        for (const AiSwitch which : switches) {
            for (const AiSwitchChange change : {AiSwitchChange::Toggle, AiSwitchChange::Set, AiSwitchChange::Clear}) {
                const battle::BattleSwitchAction a{.which = which, .change = change};
                all.push_back({1, stepBytesFor(a), a});
            }
        }
        std::vector<AiTarget> targets;
        for (int character = 0; character < static_cast<int>(AiTargetSelector::MONSTER_SLOT_1); ++character) {
            targets.emplace_back(static_cast<CharacterPropId>(character));
        }
        for (int selector = static_cast<int>(AiTargetSelector::MONSTER_SLOT_1);
             selector <= static_cast<int>(AiTargetSelector::TARGETTING_TARGET); ++selector) {
            targets.emplace_back(static_cast<AiTargetSelector>(selector));
        }
        for (int number = 0; number <= static_cast<int>(AiEffect::HideSelf); ++number) {
            const auto effect = static_cast<AiEffect>(number);
            const bool namesATarget = effect == AiEffect::MakeInvincible || effect == AiEffect::EndInvincible ||
                                      effect == AiEffect::MakeTargetable || effect == AiEffect::MakeUntargetable ||
                                      effect == AiEffect::FillGauge;
            const bool namesAStatus = effect == AiEffect::SetStatus || effect == AiEffect::ClearStatus;
            if (namesATarget) {
                for (const AiTarget target : targets) {
                    const battle::AiEffectAction a{.effect = effect, .target = target};
                    all.push_back({2, stepBytesFor(a), a});
                }
            } else if (namesAStatus) {
                for (int status = 0; status < kStatusCount; ++status) {
                    const battle::AiEffectAction a{.effect = effect, .status = static_cast<StatusId>(status)};
                    all.push_back({2, stepBytesFor(a), a});
                }
            } else {
                const battle::AiEffectAction a{.effect = effect};
                all.push_back({2, stepBytesFor(a), a});
            }
        }
        return all;
    }();
    return steps;
}
}  // namespace

std::string DamageOracle::replayAiCall(const TracedCall& call, const TracedRoutine& routine) {
    // The call's reads, and the cells kept with them: a monster slot holds a monster in two cells
    // the battle keeps together, the held set and the high byte of the monster's index, and the
    // one the call did not read follows the one it did.
    const auto overlay = [this](const std::map<std::uint32_t, std::uint8_t>& cells) {
        for (const auto& [cell, value] : cells) {
            writeByte(cell, value);
        }
    };
    const auto overlayReads = [&] {
        overlay(call.inputs);
        const bool heldRead = call.inputs.contains(kMonstersHeld);
        auto held = readByte(kMonstersHeld);
        for (int position = 0; position < battle::kMonsterSlots; ++position) {
            const auto at = static_cast<std::uint32_t>(battle::BattlerSlot::monster(position).index() * 2);
            const bool indexRead = call.inputs.contains(kMonsterIndex + at + 1);
            const auto bit = static_cast<std::uint8_t>(1u << position);
            if (heldRead && !indexRead) {
                writeWord(kMonsterIndex + at, (held & bit) != 0 ? 0 : kNoMonster);
            } else if (indexRead && !heldRead) {
                held = static_cast<std::uint8_t>((readByte(kMonsterIndex + at + 1) & kTopBit) == 0 ? held | bit : held & ~bit);
            }
        }
        writeByte(kMonstersHeld, held);
    };

    if (routine.entry == kInitAi) {
        // The slot is the one whose turn place the call wrote, and the script the one whose turn
        // part starts there.
        std::optional<battle::BattlerSlot> slot;
        for (const auto& [cell, value] : call.writes) {
            if (cell >= kTurnPlace && cell < kTurnPlace + 2 * battle::kBattlerSlots) {
                slot = battle::BattlerSlot::fromIndex(static_cast<int>((cell - kTurnPlace) / 2));
            }
        }
        if (!slot) {
            throw std::runtime_error("InitAI wrote no turn place");
        }
        const auto at = static_cast<std::uint32_t>(slot->index() * 2);
        const auto byteOf = [&](std::uint32_t cell) {
            const auto found = call.writes.find(cell);
            if (found == call.writes.end()) {
                throw std::runtime_error("InitAI wrote half of a turn place");
            }
            return found->second;
        };
        const auto place = static_cast<std::uint16_t>(byteOf(kTurnPlace + at) | byteOf(kTurnPlace + at + 1) << 8);
        const std::optional<std::uint16_t> script = scriptWithTurnAt(place);
        if (!script) {
            throw std::runtime_error("InitAI wrote a turn place, " + std::to_string(place) + ", where no script starts");
        }
        const AiScenario plain;
        writeAiScriptCells(plain.state);
        overlay(call.inputs);
        AiScenario s = readAiScenario(*slot);
        s.state.at(*slot).aiScript = *script;
        battle::readyAi(s.state, *slot);
        const AiScriptPlaces port = portScriptPlaces(s.state, *slot);
        overlay(call.writes);
        return differences(port, readScriptPlaces(*slot));
    }

    if (routine.entry == kExecMonsterAction || routine.entry == kExecAiRetal) {
        const bool counter = routine.entry == kExecAiRetal;
        // The battler whose script place the run read; a counterattack that ran already this step
        // reads only its battler's bit.
        std::optional<battle::BattlerSlot> actor =
            battlerRead(call, counter ? kCounterPlace : kTurnPlace, "script place");
        if (!actor && counter) {
            actor = battlerRead(call, kBattlerBit, "battler bit");
        }
        if (!actor) {
            throw std::runtime_error(call.routine + " read no battler's script place");
        }
        AiScenario plain;
        plain.actor = *actor;
        writeAiRunCells(plain, counter);
        overlayReads();
        const AiRunResult before = readAiRunResult(*actor);
        AiScenario s = readAiScenario(*actor);
        const AiRunResult port = counter ? portAiCounter(s) : portAiTurn(s);
        overlay(call.writes);
        AiRunResult guest = readAiRunResult(*actor);
        guest.actions = added(guest.actions, before.actions);
        guest.counterActions = added(guest.counterActions, before.counterActions);
        return differences(port, guest);
    }

    // One of the three commands. The step is the one a script holds whose bytes are the ones the
    // command read.
    const std::size_t command = static_cast<std::size_t>(
        std::find(kAiCommands.begin(), kAiCommands.end(), routine.entry) - kAiCommands.begin());
    if (!call.inputs.contains(kCommandAttack)) {
        throw std::runtime_error(call.routine + " read no attack byte");
    }
    const CommandStep* step = nullptr;
    for (const CommandStep& candidate : commandSteps()) {
        if (candidate.command != command) {
            continue;
        }
        const auto at = [&](std::size_t i) -> std::uint8_t { return i < candidate.bytes.size() ? candidate.bytes[i] : 0; };
        const auto agrees = [&](std::uint32_t cell, std::uint8_t value) {
            const auto found = call.inputs.find(cell);
            return found == call.inputs.end() || found->second == value;
        };
        if (agrees(kCommandAttack, at(1)) && agrees(kCommandTargets, at(2)) && agrees(kCommandTargets + 1, at(3))) {
            step = &candidate;
            break;
        }
    }
    if (step == nullptr) {
        std::string bytes;
        for (const std::uint32_t cell : {kCommandAttack, kCommandTargets, kCommandTargets + 1}) {
            const auto found = call.inputs.find(cell);
            bytes += found == call.inputs.end() ? " --" : " " + std::to_string(found->second);
        }
        throw std::runtime_error(call.routine + " read bytes no script's step can hold:" + bytes);
    }
    if (const auto* effect = std::get_if<battle::AiEffectAction>(&step->action);
        effect != nullptr && (effect->effect == AiEffect::SetStatus || effect->effect == AiEffect::ClearStatus)) {
        throw std::runtime_error("SetStatus and ClearStatus make an attack, which replays through the attack's own calls");
    }

    // The cells as the oracle writes a default command's, then the call's reads.
    const auto rebuild = [&] {
        const AiScenario plain;
        writeAiCells(plain);
        writeByte(kAiTargetable, 0xFF);
        writeWord(kReachSecondCell, 0xFFFF);
        writeWord(kInvincible, 0);
        writeByte(kEndOfBattleEvent, 0);
        writeByte(kPartyPlayed, 0);
        for (std::size_t record = 0; record < game::kCharacterRecords; ++record) {
            writeByte(kPartyBytes + static_cast<std::uint32_t>(record), 0);
        }
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            const auto at = static_cast<std::uint32_t>(index * 2);
            writeWord(kAtbRate + at, 0);
            if (index < battle::kPartySlots) {
                writeByte(kRecordIndex + at, kNoOne);
            }
            writeWord(kToSet12 + at, 0);
            writeWord(kToSet34 + at, 0);
            writeWord(kToClear12 + at, 0);
            writeWord(kToClear34 + at, 0);
        }
        writeCommandStart();
        overlayReads();
    };
    rebuild();
    overlay(call.writes);
    AiCommandResult recorded = readAiCommandResult();

    // The battler is each one the cartridge's command, given the same reads, makes the same writes for.
    std::string out;
    int matched = 0;
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot actor = battle::BattlerSlot::fromIndex(index);
        rebuild();
        DamageOracle::call(
            [&] { aiCommands_[command](slotIndex(actor), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
        if (!differences(readAiCommandResult(), recorded).empty()) {
            continue;
        }
        ++matched;
        rebuild();
        AiScenario s = readAiScenario(actor);
        AiCommandResult port = portAiCommand(s, step->action);
        // No command here makes an attack, so the attack's cells are not compared.
        port.attack = {};
        recorded.attack = {};
        const std::string parted = differences(port, recorded);
        if (!parted.empty()) {
            out += "as battler " + std::to_string(index) + ":\n" + parted;
        }
    }
    if (matched == 0) {
        throw std::runtime_error(call.routine + " makes the call's writes for no battler, given its reads");
    }
    return out;
}

}  // namespace ostinato::trace
