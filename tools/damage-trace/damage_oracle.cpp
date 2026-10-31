#include "damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>

#include "retropp/guest_escape.h"
#include "retropp/guest_watch.h"
#include "retropp/isa.h"
#include "retropp/memory_region.h"
#include "retropp/snes.h"
#include "retropp/timing.h"

#include "data/attack_properties.h"

namespace ostinato::trace {

namespace {

// --- the routines (battle/battle_main.asm; the battle program sits at $C2:0000) -----------------

constexpr std::uint32_t kCalcMagicDmg = 0xC22B69;
constexpr std::uint32_t kCalcDmg = 0xC22B9D;
constexpr std::uint32_t kRelicDmgEffect = 0xC20D4A;
constexpr std::uint32_t kApplyDmgMult = 0xC2370B;

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
constexpr std::array<RoutineOpening, 4> kOpenings{{
    {"CalcMagicDmg", kCalcMagicDmg, {0xAD, 0xAF, 0x11, 0x00}, 3},    // lda $11af
    {"CalcDmg", kCalcDmg, {0xAD, 0xA2, 0x11, 0x00}, 3},              // lda $11a2
    {"RelicDmgEffect", kRelicDmgEffect, {0x08, 0xAD, 0xA4, 0x11}, 4},  // php; lda $11a4
    {"ApplyDmgMult", kApplyDmgMult, {0x5A, 0xA4, 0xBC, 0x00}, 3},    // phy; ldy $bc
}};

// The frame the battle program calls them in: data bank $7E and direct page $0000, so every cell is
// a work-RAM address. CalcDmg and RelicDmgEffect are entered with both register widths narrow,
// ApplyDmgMult with the accumulator wide; interrupts are off throughout.
constexpr std::uint8_t kBattleDataBank = 0x7E;
constexpr std::uint16_t kBattleDirectPage = 0x0000;
constexpr std::uint8_t kByteWideStatus = 0x34;
constexpr std::uint8_t kWideAccumulatorStatus = 0x14;

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
