// The damage oracle: the cartridge's own damage routines, isolated and run on the hosted cartridge
// over a battle the port can also work out, so the port's numbers are checked against the game's.
//
// The cartridge is hosted on the engine's SNES machine and started from a tick of the engine's run
// loop. An escape on the game's reset code, at the first instruction after its switch out of
// emulation mode, ends the start: the processor is in the mode the game's code is written for and
// nothing of the game has run yet. Each damage routine is bound where the cartridge holds it and
// called in that context. A caller builds one DamageScenario from the port's own types; the oracle
// writes it into the machine's work RAM at the cells the routine reads, calls the routine, and gives
// back what it wrote — read back from the cells, with watches on them recording each write as the
// routine makes it.
//
//     trace::runOnLoop([&] {
//         trace::DamageOracle oracle{image};
//         oracle.start();
//         trace::DamageScenario scenario;
//         scenario.attack.record.power = 150;
//         ...
//         const std::uint16_t theirs = oracle.calcDamage(scenario);
//         const std::uint16_t ours = battle::physicalDamage(scenario.state, scenario.attack, scenario.attacker);
//     });
//
// runOnLoop() is a run loop on the console's cadence with no window, stepped as fast as the host
// allows, whose one tick runs the work and whose run ends through the loop's exit; a tool with a
// loop of its own starts and calls the oracle from its own tick instead.
//
// Every cell the oracle writes is filled from a field of the port's state, so the guest never sees a
// value the port has no name for; cells a routine does not read are left as the machine has them. The
// routines are at the addresses both US cartridges hold them; the oracle checks each one's opening
// bytes and refuses an image that does not have them. `ostinato-damage-trace --check` runs it from
// the command line; tests/test_damage_formulas.cpp runs it against the port's formulas.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "retropp/clock.h"
#include "retropp/run_loop.h"
#include "retropp/vm.h"

#include "battle/attack.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"

namespace ostinato::trace {

// One battle as far as the damage routines read it: the state, the attack being worked out, and
// whoever it is worked out for.
struct DamageScenario {
    battle::BattleState state;
    battle::AttackInFlight attack;
    battle::BattlerSlot attacker = battle::BattlerSlot::character(0);
};

// What the multiplier routine leaves: the damage it returns and the steps left in the multiplier.
struct MultiplierResult {
    std::uint16_t damage = 0;
    int stepsLeft = 0;
};

// One write a routine made to a watched cell, as the watch saw it.
struct WatchedWrite {
    std::uint32_t at = 0;  // the cell's work-RAM address, $7E:xxxx
    std::uint8_t value = 0;
};

// A clock that moves one tick per reading, so a loop run with it steps as fast as the host allows
// instead of waiting for the wall clock.
class TickClock final : public retropp::Clock {
public:
    explicit TickClock(std::chrono::nanoseconds period) noexcept : period_(period) {}
    [[nodiscard]] std::chrono::nanoseconds now() const noexcept override {
        now_ += period_;
        return now_;
    }

private:
    std::chrono::nanoseconds period_;
    mutable std::chrono::nanoseconds now_{};
};

class DamageOracle {
public:
    // Host `image` (a cartridge image without a copier header), declare the escape and the watches,
    // and bind the routines. Throws std::runtime_error when a routine is not where the US cartridges
    // hold it, and whatever the engine throws for an image it cannot host.
    explicit DamageOracle(std::span<const std::uint8_t> image);
    ~DamageOracle();

    // Start the cartridge as far as its reset code's switch to native mode and park it there. Called
    // from a tick of the run loop, once, before any routine is called. Throws std::runtime_error when
    // the reset code is not reached.
    void start();

    DamageOracle(const DamageOracle&) = delete;
    DamageOracle& operator=(const DamageOracle&) = delete;

    // The spell formula over a power, a magic stat and a level (CalcMagicDmg).
    [[nodiscard]] std::uint16_t magicDamage(std::uint8_t power, std::uint8_t magicStat, std::uint8_t level);

    // The base damage over the scenario, before the relic bonus (CalcDmg).
    [[nodiscard]] std::uint16_t calcDamage(const DamageScenario& scenario);

    // The relic bonus over the scenario, starting from `damage` (RelicDmgEffect).
    [[nodiscard]] std::uint16_t relicBonus(const DamageScenario& scenario, std::uint16_t damage);

    // The multiplier over the scenario's damage and multiplier steps (ApplyDmgMult).
    [[nodiscard]] MultiplierResult applyMultiplier(const DamageScenario& scenario);

    // The writes the last call made to the watched cells, in order: the damage word at
    // $7E:11B0-11B1 and the multiplier's steps at $7E:00BC.
    [[nodiscard]] const std::vector<WatchedWrite>& writes() const { return writes_; }

    // Where the machine is parked: the first instructions of the game's reset code, in native mode.
    [[nodiscard]] bool atReset() const { return reset_; }

    // The machine, parked.
    [[nodiscard]] retropp::Vm& machine() { return machine_; }

    // The cells, for callers that read them back.
    static constexpr std::uint32_t kWorkRam = 0x7E0000;
    static constexpr std::uint32_t kRecord = kWorkRam + 0x11A0;  // the attack's 14-byte record
    static constexpr std::uint32_t kPower = kWorkRam + 0x11A6;   // the record's power
    static constexpr std::uint32_t kAttackerStat = kWorkRam + 0x11AE;
    static constexpr std::uint32_t kAttackerLevel = kWorkRam + 0x11AF;
    static constexpr std::uint32_t kDamage = kWorkRam + 0x11B0;
    static constexpr std::uint32_t kAttackFlags = kWorkRam + 0x00B3;  // the second attack-flags byte
    static constexpr std::uint32_t kMultiplier = kWorkRam + 0x00BC;
    // Per battler, a word at twice the battler's index.
    static constexpr std::uint32_t kRelicEffects1And3 = kWorkRam + 0x3C44;
    static constexpr std::uint32_t kRelicEffects4And5 = kWorkRam + 0x3C58;

private:
    using Call = void(std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    using Multiply = std::uint16_t(std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);

    // Run `routine` with the watches recording.
    void call(const std::function<void()>& routine);

    void writeScenario(const DamageScenario& scenario);
    void writeByte(std::uint32_t at, std::uint8_t value);
    void writeWord(std::uint32_t at, std::uint16_t value);
    [[nodiscard]] std::uint8_t readByte(std::uint32_t at);
    [[nodiscard]] std::uint16_t readWord(std::uint32_t at);

    retropp::Vm::SNES machine_;
    bool reset_ = false;
    bool recording_ = false;
    std::vector<WatchedWrite> writes_;
    retropp::Routine<Call> calcMagicDmg_;
    retropp::Routine<Call> calcDmg_;
    retropp::Routine<Call> relicDmgEffect_;
    retropp::Routine<Multiply> applyDmgMult_;
};

// Run `work` from the one tick of a run loop on the console's cadence, with no window, stepped as
// fast as the host allows (TickClock). The run ends through the loop's exit: the tick asks for it
// and the exit action lets it proceed. Whatever `work` throws comes out of here.
void runOnLoop(const std::function<void()>& work);

// The seed the sweep draws from, and the scenarios it draws: every cell the three routines read
// drawn at random within the game's own ranges — the record's bytes, the attacker's stat and level
// (a party member's 1 to 99), which battler attacks, the hand, the multiplier's steps, the damage
// and the attacker's relic effects. The same draws are what
// `ostinato-damage-trace --check` runs and what the formula tests sweep.
inline constexpr std::uint32_t kSweepSeed = 0x0D4A2B9D;
[[nodiscard]] DamageScenario drawScenario(std::mt19937& rng);

// One line naming a drawn scenario's inputs, for a report.
[[nodiscard]] std::string describe(const DamageScenario& scenario);

}  // namespace ostinato::trace
