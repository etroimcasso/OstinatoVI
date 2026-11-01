#include "damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "retropp/guest_escape.h"
#include "retropp/guest_watch.h"
#include "retropp/isa.h"
#include "retropp/memory_region.h"
#include "retropp/snes.h"
#include "retropp/timing.h"

#include "battle/battle_script.h"
#include "battle/damage.h"
#include "data/attack_properties.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"

namespace ostinato::trace {

namespace {

// --- the routines (battle/battle_main.asm; the battle program sits at $C2:0000) -----------------

constexpr std::uint32_t kCalcMagicDmg = 0xC22B69;
constexpr std::uint32_t kCalcDmg = 0xC22B9D;
constexpr std::uint32_t kRelicDmgEffect = 0xC20D4A;
constexpr std::uint32_t kApplyDmgMult = 0xC2370B;
constexpr std::uint32_t kCalcTargetDmg = 0xC20B83;

// `Reset` (field/reset.asm), where the reset vector's `jml` lands after `clc; xce`
// (field/header.asm): the first instruction the game runs in native mode.
constexpr std::uint32_t kReset = 0xC00019;

// The machine is advanced a microsecond at a time until the reset escape fires, so it is parked
// within an instruction or two of it; the whole start takes a few microseconds, and this many steps
// is far more than enough before the start is called a failure.
constexpr std::chrono::nanoseconds kStartStep{1'000};
constexpr int kStartStepsAllowed = 100'000;

constexpr const char* kResetKey = "reset";
constexpr const char* kDamageKey = "damage word";
constexpr const char* kMultiplierKey = "multiplier steps";

struct RoutineOpening {
    const char* name;
    std::uint32_t at;
    std::array<std::uint8_t, 4> bytes;
    std::size_t length;
};
// Each routine's first instruction, as the cartridge holds it.
constexpr std::array<RoutineOpening, 5> kOpenings{{
    {"CalcMagicDmg", kCalcMagicDmg, {0xAD, 0xAF, 0x11, 0x00}, 3},    // lda $11af
    {"CalcDmg", kCalcDmg, {0xAD, 0xA2, 0x11, 0x00}, 3},              // lda $11a2
    {"RelicDmgEffect", kRelicDmgEffect, {0x08, 0xAD, 0xA4, 0x11}, 4},  // php; lda $11a4
    {"ApplyDmgMult", kApplyDmgMult, {0x5A, 0xA4, 0xBC, 0x00}, 3},    // phy; ldy $bc
    {"CalcTargetDmg", kCalcTargetDmg, {0x08, 0xE2, 0x20, 0xAD}, 4},  // php; sep #$20; lda $11a6
}};

// The frame the battle program calls them in: data bank $7E and direct page $0000, so every cell is
// a work-RAM address. CalcDmg and RelicDmgEffect are entered with both register widths narrow,
// ApplyDmgMult and CalcTargetDmg with the accumulator wide and the index registers narrow;
// interrupts are off throughout.
constexpr std::uint8_t kBattleDataBank = 0x7E;
constexpr std::uint16_t kBattleDirectPage = 0x0000;
constexpr std::uint8_t kByteWideStatus = 0x34;
constexpr std::uint8_t kWideAccumulatorStatus = 0x14;

// --- the cells the per-target pass reads and writes ----------------------------------------------

constexpr std::uint32_t kWorkRam = DamageOracle::kWorkRam;
constexpr std::uint32_t kEffect = kWorkRam + 0x11A9;            // the special effect, its index doubled
constexpr std::uint32_t kFirstAttackFlags = kWorkRam + 0x00B2;  // bit 7: a drain is capped
constexpr std::uint32_t kAnimationCommand = kWorkRam + 0x00B4;
constexpr std::uint32_t kCommand = kWorkRam + 0x00B5;
constexpr std::uint32_t kAttackIndex = kWorkRam + 0x00B6;
constexpr std::uint32_t kSecondary = kWorkRam + 0x00B7;
constexpr std::uint32_t kRngPosition = kWorkRam + 0x00BE;
constexpr std::uint32_t kDamageModification = kWorkRam + 0x3414;
constexpr std::uint32_t kGolemBlock = kWorkRam + 0x3A82;
constexpr std::uint32_t kInterceptorBlock = kWorkRam + 0x3A83;
constexpr std::uint32_t kForcefield = kWorkRam + 0x3EC8;
// The battle script's queue: where the attack's animation went, the next free place, the queue.
constexpr std::uint32_t kAnimationPlace = kWorkRam + 0x3A71;
constexpr std::uint32_t kQueueEnd = kWorkRam + 0x3A72;
constexpr std::uint32_t kQueue = kWorkRam + 0x2D6E;
constexpr std::uint8_t kQueueCommandSize = 4;
// Per battler, at twice the battler's index.
constexpr std::uint32_t kLevel = kWorkRam + 0x3B18;
constexpr std::uint32_t kDefenses = kWorkRam + 0x3BB8;   // defense, then magic defense
constexpr std::uint32_t kHp = kWorkRam + 0x3BF4;
constexpr std::uint32_t kMp = kWorkRam + 0x3C08;
constexpr std::uint32_t kMaxHp = kWorkRam + 0x3C1C;
constexpr std::uint32_t kMaxMp = kWorkRam + 0x3C30;
constexpr std::uint32_t kAbsorbedNullified = kWorkRam + 0x3BCC;
constexpr std::uint32_t kWeakHalved = kWorkRam + 0x3BE0;
constexpr std::uint32_t kMonsterFlags = kWorkRam + 0x3C95;
constexpr std::uint32_t kStance = kWorkRam + 0x3AA1;
constexpr std::uint32_t kStatus12 = kWorkRam + 0x3EE4;
constexpr std::uint32_t kStatus34 = kWorkRam + 0x3EF8;
constexpr std::uint32_t kToSet12 = kWorkRam + 0x3DD4;
constexpr std::uint32_t kToSet34 = kWorkRam + 0x3DE8;
constexpr std::uint32_t kToClear12 = kWorkRam + 0x3DFC;
constexpr std::uint32_t kToClear34 = kWorkRam + 0x3E10;
constexpr std::uint32_t kController = kWorkRam + 0x32B9;
constexpr std::uint32_t kLastAttacker = kWorkRam + 0x32E0;
constexpr std::uint32_t kTaken = kWorkRam + 0x33D0;
constexpr std::uint32_t kHealed = kWorkRam + 0x33E4;

// Damage modification on and off; a link or block that names no one; a ledger entry with nothing in it.
constexpr std::uint8_t kOn = 0xFF;
constexpr std::uint8_t kNoOne = 0xFF;
constexpr std::uint16_t kNoDamage = 0xFFFF;
constexpr std::uint8_t kDrainCappedBit = 0x80;
constexpr std::uint8_t kWaitingBit = 0x80;
// The monster flags byte: undead, the Imp critical, human, the name hidden, dying at 0 MP.
constexpr std::uint8_t kUndeadBit = 0x80;
constexpr std::uint8_t kImpCriticalBit = 0x40;
constexpr std::uint8_t kHumanBit = 0x10;
constexpr std::uint8_t kNameHiddenBit = 0x04;
constexpr std::uint8_t kDiesAtZeroMpBit = 0x01;
// The stance byte: Defending, the back row.
constexpr std::uint8_t kDefendingBit = 0x02;
constexpr std::uint8_t kBackRowBit = 0x20;
// The queue's place for the attack's animation is filled with this before a call, and the port's
// with an animation of this second value, so a rewrite on either side shows.
constexpr std::uint8_t kUnwritten = 0xEE;

// The second attack-flags byte as a command starts it, every bit set, with the two bits the damage
// routines read: set while the hand is not held in both hands, set while an attack that ignores
// defense is not multiplied (docs/contracts/battle_commands.md, "A command's start").
constexpr std::uint8_t kAttackFlagsAtStart = 0xFF;
constexpr std::uint8_t kNotTwoHanded = 0x40;
constexpr std::uint8_t kIgnoreDefenseStopsMultiplier = 0x10;

// The first relic word holds relic effects 1 in its low byte, with that byte's top bit taken by
// relic effects 2's top bit, and relic effects 3 in its high byte (InitCharBattle).
constexpr std::uint8_t kLowSevenBits = 0x7F;
constexpr std::uint8_t kTopBit = 0x80;

std::uint16_t slotIndex(battle::BattlerSlot slot) {
    return static_cast<std::uint16_t>(slot.index() * 2);
}

std::uint8_t byte(std::mt19937& rng) {
    return static_cast<std::uint8_t>(rng() & 0xFF);
}

}  // namespace

DamageOracle::DamageOracle(std::span<const std::uint8_t> image) {
    machine_.hostRom(image);
    for (const RoutineOpening& routine : kOpenings) {
        const std::vector<std::uint8_t> bytes =
            machine_.read(retropp::MemoryRegion{.at = routine.at, .size = static_cast<std::uint32_t>(routine.length)});
        if (!std::equal(bytes.begin(), bytes.end(), routine.bytes.begin())) {
            throw std::runtime_error(std::string{"the cartridge does not hold "} + routine.name +
                                     " where the US cartridges do");
        }
    }

    machine_.registerEscapes(retropp::escapes(retropp::GuestEscape{
        .key = kResetKey, .at = kReset, .handler = [this](retropp::Vm&, std::uint32_t) { reset_ = true; }}));
    // A watch names the address the store drove, and a direct-page store drives $00:00xx; the same
    // byte of work RAM is recorded under its $7E address so a reader compares one name.
    const auto record = [this](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
        if (recording_) {
            writes_.push_back(WatchedWrite{.at = kWorkRam | (at & 0xFFFF), .value = value});
        }
        return retropp::AccessVerdict::proceed();
    };
    machine_.registerWatches(retropp::watches(
        retropp::GuestWatch{
            .key = kDamageKey, .at = retropp::MemoryRegion{.at = kDamage, .size = 2}, .onWrite = record},
        retropp::GuestWatch{
            .key = kMultiplierKey, .at = retropp::MemoryRegion{.at = kMultiplier, .size = 1}, .onWrite = record}));

    const retropp::RoutineBinding byteWide{
        .inputs = {retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
        .isa = retropp::Isa::Wdc65816};
    calcMagicDmg_ = machine_.bindRoutine<Call>(kCalcMagicDmg, byteWide);
    calcDmg_ = machine_.bindRoutine<Call>(kCalcDmg, byteWide);
    relicDmgEffect_ = machine_.bindRoutine<Call>(kRelicDmgEffect, byteWide);
    applyDmgMult_ = machine_.bindRoutine<Multiply>(
        kApplyDmgMult, {.inputs = {retropp::snes::C, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                        .output = retropp::snes::C,
                        .isa = retropp::Isa::Wdc65816});
    calcTargetDmg_ = machine_.bindRoutine<TargetCall>(
        kCalcTargetDmg,
        {.inputs = {retropp::snes::X, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
}

DamageOracle::~DamageOracle() = default;

void DamageOracle::start() {
    // The machine runs on the game's own tick, a microsecond at a time, until the reset escape
    // fires, and is parked there.
    machine_.run(retropp::Vm::Advance::OnTick);
    for (int step = 0; step < kStartStepsAllowed && !reset_; ++step) {
        machine_.advanceTick(kStartStep);
    }
    machine_.stop();
    if (!reset_) {
        throw std::runtime_error("the cartridge did not reach its reset code");
    }
}

void DamageOracle::call(const std::function<void()>& routine) {
    if (!reset_) {
        throw std::logic_error("the oracle's cartridge has not been started (DamageOracle::start)");
    }
    writes_.clear();
    recording_ = true;
    routine();
    recording_ = false;
}

void runOnLoop(const std::function<void()>& work) {
    TickClock clock{retropp::TimingProfile::Snes.tickPeriod()};
    retropp::RunLoop loop{clock, retropp::TimingProfile::Snes};
    std::exception_ptr failure;
    bool done = false;
    loop.simTick([&](const retropp::InputState&) {
        if (!done) {
            done = true;
            try {
                work();
            } catch (...) {
                failure = std::current_exception();
            }
            loop.exitRequest();
        }
    });
    loop.renderLoop([] {});
    loop.exitAction([] { return retropp::ExitVerdict::Proceed; });
    loop.run();
    if (failure) {
        std::rethrow_exception(failure);
    }
}

std::uint16_t DamageOracle::magicDamage(std::uint8_t power, std::uint8_t magicStat, std::uint8_t level) {
    writeByte(kPower, power);
    writeByte(kAttackerStat, magicStat);
    writeByte(kAttackerLevel, level);
    writeWord(kDamage, 0);
    call([&] { calcMagicDmg_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    return readWord(kDamage);
}

std::uint16_t DamageOracle::calcDamage(const DamageScenario& scenario) {
    writeScenario(scenario);
    call([&] { calcDmg_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    return readWord(kDamage);
}

std::uint16_t DamageOracle::relicBonus(const DamageScenario& scenario, std::uint16_t damage) {
    writeScenario(scenario);
    writeWord(kDamage, damage);
    call([&] {
        relicDmgEffect_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage);
    });
    return readWord(kDamage);
}

MultiplierResult DamageOracle::applyMultiplier(const DamageScenario& scenario) {
    writeScenario(scenario);
    std::uint16_t damage = 0;
    call([&] {
        damage = applyDmgMult_(static_cast<std::uint16_t>(scenario.attack.damage), kWideAccumulatorStatus,
                               kBattleDataBank, kBattleDirectPage);
    });
    return MultiplierResult{.damage = damage, .stepsLeft = readByte(kMultiplier)};
}

TargetDamageResult DamageOracle::targetDamage(const DamageScenario& scenario, battle::BattlerSlot target) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    call([&] {
        calcTargetDmg_(slotIndex(scenario.attacker), slotIndex(target), kWideAccumulatorStatus, kBattleDataBank,
                       kBattleDirectPage);
    });
    return readTargetResult();
}

void DamageOracle::writeTargetCells(const DamageScenario& scenario) {
    const battle::AttackInFlight& attack = scenario.attack;
    const battle::BattleState& state = scenario.state;
    writeByte(kEffect, static_cast<std::uint8_t>(attack.effect.index() * 2));
    std::uint8_t first = kAttackFlagsAtStart;
    if (!attack.drainCapped) {
        first &= static_cast<std::uint8_t>(~kDrainCappedBit);
    }
    writeByte(kFirstAttackFlags, first);
    writeByte(kAnimationCommand, 0);
    writeByte(kCommand, static_cast<std::uint8_t>(attack.command));
    writeByte(kAttackIndex, static_cast<std::uint8_t>(attack.attack));
    writeByte(kSecondary, attack.secondary);
    writeByte(kRngPosition, state.rng.position());
    writeByte(kDamageModification, attack.scratch.damageModification ? kOn : 0);
    const auto blockByte = [](const std::optional<battle::BattlerSlot>& slot) {
        return slot ? static_cast<std::uint8_t>(slotIndex(*slot)) : kNoOne;
    };
    writeByte(kGolemBlock, blockByte(attack.scratch.golemBlocked));
    writeByte(kInterceptorBlock, blockByte(attack.scratch.interceptorBlocked));
    writeByte(kForcefield, state.elementsNullifiedByForcefield.bits);
    writeByte(kAnimationPlace, 0);
    writeByte(kQueueEnd, kQueueCommandSize);
    const std::array<std::uint8_t, kQueueCommandSize> unwritten{kUnwritten, kUnwritten, kUnwritten, kUnwritten};
    machine_.write(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize}, unwritten);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        writeByte(kLevel + at, battler.level);
        writeByte(kDefenses + at, battler.defense);
        writeByte(kDefenses + at + 1, battler.magicDefense);
        writeWord(kHp + at, battler.hp);
        writeWord(kMp + at, battler.mp);
        writeWord(kMaxHp + at, battler.maxHp);
        writeWord(kMaxMp + at, battler.maxMp);
        writeByte(kAbsorbedNullified + at, battler.elementsAbsorbed.bits);
        writeByte(kAbsorbedNullified + at + 1, battler.elementsNullified.bits);
        writeByte(kWeakHalved + at, battler.elementsWeak.bits);
        writeByte(kWeakHalved + at + 1, battler.elementsHalved.bits);
        std::uint8_t flags = 0;
        flags |= battler.undead ? kUndeadBit : 0;
        flags |= battler.impCritical ? kImpCriticalBit : 0;
        flags |= battler.human ? kHumanBit : 0;
        flags |= battler.nameHidden ? kNameHiddenBit : 0;
        flags |= battler.diesAtZeroMp ? kDiesAtZeroMpBit : 0;
        writeByte(kMonsterFlags + at, flags);
        std::uint8_t stance = 0;
        stance |= battler.defending ? kDefendingBit : 0;
        stance |= battler.backRow ? kBackRowBit : 0;
        writeByte(kStance + at, stance);
        const auto writeStatuses = [&](std::uint32_t first12, std::uint32_t first34, const StatusSet& set) {
            writeByte(first12 + at, set.bytes[0]);
            writeByte(first12 + at + 1, set.bytes[1]);
            writeByte(first34 + at, set.bytes[2]);
            writeByte(first34 + at + 1, set.bytes[3]);
        };
        writeStatuses(kStatus12, kStatus34, battler.status);
        writeStatuses(kToSet12, kToSet34, battler.statusesToSet);
        writeStatuses(kToClear12, kToClear34, battler.statusesToClear);
        writeByte(kController + at, blockByte(battler.controlledBy));
        std::uint8_t mark = battler.lastAttacker ? static_cast<std::uint8_t>(battler.lastAttacker->index()) : 0;
        mark |= battler.retaliationPending ? kWaitingBit : 0;
        writeByte(kLastAttacker + at, mark);
        writeWord(kTaken + at, attack.scratch.ledger.taken[i].value_or(kNoDamage));
        writeWord(kHealed + at, attack.scratch.ledger.healed[i].value_or(kNoDamage));
    }
}

TargetDamageResult DamageOracle::readTargetResult() {
    TargetDamageResult result;
    const auto entry = [](std::uint16_t word) -> std::optional<std::uint16_t> {
        if (word == kNoDamage) {
            return std::nullopt;
        }
        return word;
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.ledger.taken[i] = entry(readWord(kTaken + at));
        result.ledger.healed[i] = entry(readWord(kHealed + at));
        const auto readStatuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        result.statusesToSet[i] = readStatuses(kToSet12, kToSet34);
        result.statusesToClear[i] = readStatuses(kToClear12, kToClear34);
        result.controlled[i] = (readByte(kController + at) & kWaitingBit) == 0;
        result.lastAttackerMark[i] = readByte(kLastAttacker + at);
    }
    result.stepsLeft = readByte(kMultiplier);
    result.secondary = readByte(kSecondary);
    const std::vector<std::uint8_t> place = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize});
    if (std::any_of(place.begin(), place.end(), [](std::uint8_t b) { return b != kUnwritten; })) {
        result.animationRewrittenWith = place[kQueueCommandSize - 1];
    }
    result.rngPosition = readByte(kRngPosition);
    return result;
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
        result.controlled[i] = battler.controlledBy.has_value();
        std::uint8_t mark = battler.lastAttacker ? static_cast<std::uint8_t>(battler.lastAttacker->index()) : 0;
        mark |= battler.retaliationPending ? kWaitingBit : 0;
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

void DamageOracle::writeScenario(const DamageScenario& scenario) {
    const battle::AttackInFlight& attack = scenario.attack;
    std::array<std::uint8_t, sizeof(AttackProperties)> record{};
    std::memcpy(record.data(), &attack.record, record.size());
    machine_.write(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(record.size())}, record);
    writeByte(kAttackerStat, attack.attackerStat);
    writeByte(kAttackerLevel, attack.attackerLevel);
    writeWord(kDamage, static_cast<std::uint16_t>(attack.damage));
    std::uint8_t flags = kAttackFlagsAtStart;
    if (attack.twoHanded) {
        flags &= static_cast<std::uint8_t>(~kNotTwoHanded);
    }
    if (!attack.ignoreDefenseStopsMultiplier) {
        flags &= static_cast<std::uint8_t>(~kIgnoreDefenseStopsMultiplier);
    }
    writeByte(kAttackFlags, flags);
    writeByte(kMultiplier, static_cast<std::uint8_t>(attack.multiplier));

    for (std::size_t i = 0; i < scenario.state.battlers.size(); ++i) {
        const battle::Battler& battler = scenario.state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const auto first = static_cast<std::uint8_t>((battler.relicEffects1.bits & kLowSevenBits) |
                                                     (battler.relicEffects2.bits & kTopBit));
        writeByte(kRelicEffects1And3 + at, first);
        writeByte(kRelicEffects1And3 + at + 1, battler.relicEffects3.bits);
        writeByte(kRelicEffects4And5 + at, battler.relicEffects4.bits);
        writeByte(kRelicEffects4And5 + at + 1, battler.relicEffects5.bits);
    }
}

void DamageOracle::writeByte(std::uint32_t at, std::uint8_t value) {
    const std::array<std::uint8_t, 1> bytes{value};
    machine_.write(retropp::MemoryRegion{.at = at, .size = 1}, bytes);
}

void DamageOracle::writeWord(std::uint32_t at, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value & 0xFF),
                                            static_cast<std::uint8_t>(value >> 8)};
    machine_.write(retropp::MemoryRegion{.at = at, .size = 2}, bytes);
}

std::uint8_t DamageOracle::readByte(std::uint32_t at) {
    return machine_.read(retropp::MemoryRegion{.at = at, .size = 1})[0];
}

std::uint16_t DamageOracle::readWord(std::uint32_t at) {
    const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{.at = at, .size = 2});
    return static_cast<std::uint16_t>(bytes[0] | bytes[1] << 8);
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

// A party member's HP and MP never pass these.
constexpr int kPartyMaxHp = 9999;
constexpr int kPartyMaxMp = 999;
constexpr int kPartyMaxLevel = 99;
constexpr int kLedgerMax = 9999;
// No fractional attack or item in the game has a power above this.
constexpr int kMostFractionalPower = 16;

// True one time in `n`.
bool oneIn(std::mt19937& rng, unsigned n) {
    return rng() % n == 0;
}

// A byte with about a quarter of its bits set, so the flags it stands for come up one at a time.
std::uint8_t sparse(std::mt19937& rng) {
    return static_cast<std::uint8_t>(byte(rng) & byte(rng));
}

int upTo(std::mt19937& rng, int most) {
    return static_cast<int>(rng() % static_cast<unsigned>(most + 1));
}

battle::BattlerSlot anySlot(std::mt19937& rng) {
    return battle::BattlerSlot::fromIndex(static_cast<int>(rng() % battle::kBattlerSlots));
}

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
