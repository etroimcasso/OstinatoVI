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
// the command line; tests/test_damage_formulas.cpp and tests/test_target_damage.cpp run it against
// the port's formulas.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "retropp/clock.h"
#include "retropp/run_loop.h"
#include "retropp/vm.h"

#include "battle/attack.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "ostinato/status_set.h"

namespace ostinato::trace {

// --- the trace ---------------------------------------------------------------------------------

// A routine the trace follows: where it starts, where the attack frame's call to it returns, and
// where the call to it from the reflected attack returns (0: it is not called from there).
struct TracedRoutine {
    const char* name;
    std::uint32_t entry;
    std::uint32_t frameReturn;
    std::uint32_t bounceReturn;
};

// The base damage, the damage to one target, and the damage taken and shown (battle/battle_main.asm:
// 7217 and 8096; 1809, 8362 and 8440; 16193, 8365 and 8455 — the program sits at $C2:0000).
inline constexpr std::array<TracedRoutine, 3> kTracedRoutines{{
    {"CalcDmg", 0xC22B9D, 0xC2322E, 0},
    {"CalcTargetDmg", 0xC20B83, 0xC2346C, 0xC23509},
    {"ApplyAndShowDamage", 0xC262EF, 0xC23473, 0xC23528},
}};

// The cells the traced routines read and write, as ranges of work RAM: the direct-page attack flags
// and multiplier; the attack's record copy and damage; each battler's bit; the numeral buffer; the
// script queue; what is to be worked out again for each battler; the links between battlers; the
// damage ledger; the per-command cells; the battle's own cells (the missed set, the blocks, the
// Golem's HP, the script queue's place and end); each battler's flags, stats, monster flags, pending
// statuses, Forcefield's elements and statuses.
struct TracedRange {
    const char* key;
    std::uint32_t at;
    std::uint32_t size;
};
inline constexpr std::array<TracedRange, 16> kTracedRanges{{
    {"direct page", 0x7E00B0, 16},     {"attack record", 0x7E11A0, 18}, {"battler bits", 0x7E3018, 20},
    {"numeral buffer", 0x7E2BCE, 160}, {"script queue", 0x7E2D6E, 256}, {"to work out", 0x7E3204, 20},
    {"links", 0x7E327C, 120},          {"ledger", 0x7E33D0, 40},        {"command cells", 0x7E3400, 32},
    {"battle cells", 0x7E3A30, 96},    {"flags", 0x7E3AA0, 20},         {"stats", 0x7E3B18, 340},
    {"monster flags", 0x7E3C95, 20},   {"pending statuses", 0x7E3DD4, 61}, {"forcefield", 0x7E3EC8, 1},
    {"statuses", 0x7E3EE4, 40},
}};

// The trace on one machine: escapes on each traced routine's entry and return sites and watches over
// kTracedRanges. At each entry it logs the routine and the attack's context — the params block
// ($7E:00A0-00AF), the Launcher's or Super Ball's hits to come ($7E:3405), the aiming rules
// ($7E:00BA) and the attacker of the params block before this one in the script's buffer — and while
// a call is open every access to a watched cell, in order. Every event is one line handed to `line`:
//
//   f<frame> enter <routine> $<bank>:<address>
//   f<frame> params <16 bytes> hits <byte> rules <byte> previous <byte, or -- with no block before>
//   f<frame> r $7E:<cell> <byte>              a read the routine made
//   f<frame> w $7E:<cell> <byte>              a write it made
//   f<frame> return <routine> $<bank>:<address>
//
// Declares on the machine when constructed: host the cartridge first, and construct it before the
// machine runs. `access`, when given, sees every access to a watched cell whether a call is open or
// not.
class DamageTrace {
public:
    using Line = std::function<void(const std::string&)>;
    using Frame = std::function<std::uint64_t()>;
    using Access = std::function<void(std::uint32_t at, std::uint8_t value, bool write)>;

    DamageTrace(retropp::Vm& machine, Line line, Frame frame, Access access = {});
    ~DamageTrace();
    DamageTrace(const DamageTrace&) = delete;
    DamageTrace& operator=(const DamageTrace&) = delete;

    // Close the open call of `routine` as the attack frame's: for a caller that called the routine
    // itself rather than through the frame.
    void returnToFrame(const TracedRoutine& routine);

private:
    struct Places;
    std::unique_ptr<Places> places_;
    Line line_;
    Frame frame_;
    int open_ = 0;
};

// One routine call as the trace logged it: the routine, the frame it ran on and the address it
// returned to; each cell's value when the routine first read it, for every cell it read before it
// wrote it; each cell's last value written; and the attack's context at its entry.
struct TracedCall {
    std::string routine;
    std::uint64_t frame = 0;
    std::uint32_t returnedTo = 0;
    std::map<std::uint32_t, std::uint8_t> inputs;
    std::map<std::uint32_t, std::uint8_t> writes;
    std::array<std::uint8_t, 16> params{};
    std::uint8_t hitsToCome = 0xFF;
    std::uint8_t aimingRules = 0;
    std::optional<std::uint8_t> previousAttacker;
};

// Every call in a trace, in order. A call that never returns to one of its routine's return sites —
// one neither the frame nor the reflected attack made — is left out, and a return with no call open
// is skipped. Throws std::runtime_error naming the line for a line that starts like an event and is
// not one.
[[nodiscard]] std::vector<TracedCall> parseTrace(std::istream& log);

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

// What the per-target pass leaves, as the same fields on both sides: the ledger, each battler's
// statuses to set and to clear, whether it still has a controller and its last-attacker mark, the
// multiplier's steps left, the animation's second value, the second value the attack's animation
// was rewritten with (none when it was not), and the random-number position.
struct TargetDamageResult {
    battle::DamageLedger ledger;
    std::array<StatusSet, battle::kBattlerSlots> statusesToSet{};
    std::array<StatusSet, battle::kBattlerSlots> statusesToClear{};
    std::array<bool, battle::kBattlerSlots> controlled{};
    // The last attacker's slot index, with the top bit set while it is waiting to be answered; 0
    // with no last attacker.
    std::array<std::uint8_t, battle::kBattlerSlots> lastAttackerMark{};
    int stepsLeft = 0;
    std::uint8_t secondary = 0;
    std::optional<std::uint8_t> animationRewrittenWith;
    std::uint8_t rngPosition = 0;
};

// The port's per-target pass over `scenario` against `target`, read back as a TargetDamageResult.
[[nodiscard]] TargetDamageResult portTargetDamage(DamageScenario scenario, battle::BattlerSlot target);

// Every field where `port` and `guest` differ, one per line naming the cell and both values; empty
// when they agree.
[[nodiscard]] std::string differences(const TargetDamageResult& port, const TargetDamageResult& guest);

// What taking and showing the damage leaves, as the same fields on both sides: every battler's HP,
// MP, statuses to set, last-attacker mark, last damager and whether its castable spells are to be
// worked out again; the party members a Black Belt may counter for, the battlers still shown
// "Miss", the Golem's HP, the weapon's spell, the ledger, and the numerals shown in the order the
// script shows them.
struct ApplyDamageResult {
    std::array<std::uint16_t, battle::kBattlerSlots> hp{};
    std::array<std::uint16_t, battle::kBattlerSlots> mp{};
    std::array<StatusSet, battle::kBattlerSlots> statusesToSet{};
    std::array<std::uint8_t, battle::kBattlerSlots> lastAttackerMark{};
    std::array<std::optional<battle::BattlerSlot>, battle::kBattlerSlots> lastDamagedBy{};
    std::array<bool, battle::kBattlerSlots> enabledMagic{};
    battle::BattlerSet blackBeltEligible;
    battle::BattlerSet missed;
    std::uint16_t golemHp = 0;
    std::uint8_t weaponSpellcast = 0;  // the weapon's spellcast byte; 0 once a kill cancels it
    battle::DamageLedger ledger;
    std::vector<battle::DamageNumbersShown> shown;
};

// The port's taking and showing of the damage over `scenario`, read back as an ApplyDamageResult.
[[nodiscard]] ApplyDamageResult portApplyDamage(DamageScenario scenario);

[[nodiscard]] std::string differences(const ApplyDamageResult& port, const ApplyDamageResult& guest);

// What the reflected attack leaves, as the same fields on both sides: everything its damage taken
// and shown leaves, every battler's statuses to clear and the battler that last hit it, the bounce
// as the script shows it, the attack's damage and whether it can still be critical, the Launcher's
// or Super Ball's hits still to come, the Golem's and Interceptor's blocks, and the random-number
// position.
struct ReflectedAttackResult {
    ApplyDamageResult applied;
    std::array<StatusSet, battle::kBattlerSlots> statusesToClear{};
    std::array<std::optional<battle::BattlerSlot>, battle::kBattlerSlots> lastHitBy{};
    std::string shownAs;  // the ReflectedAttackShown or ScatteredHitsShown, as one line
    int damage = 0;
    bool canBeCritical = false;
    std::optional<int> extraHits;
    std::optional<battle::BattlerSlot> golemBlocked;
    std::optional<battle::BattlerSlot> interceptorBlocked;
    std::uint8_t rngPosition = 0;
};

// The port's reflected attack over `scenario`, made by the scenario's attacker and bounced off the
// battlers in the scenario's attack.params.reflectedOff, read back as a ReflectedAttackResult.
[[nodiscard]] ReflectedAttackResult portReflectedAttack(DamageScenario scenario);

[[nodiscard]] std::string differences(const ReflectedAttackResult& port, const ReflectedAttackResult& guest);

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

    // The damage to `target` over the scenario (CalcTargetDmg): every battler's cells the pass reads
    // written from the scenario, then read back.
    [[nodiscard]] TargetDamageResult targetDamage(const DamageScenario& scenario, battle::BattlerSlot target);

    // The damage in the scenario's ledger taken and shown, with the scenario's attacker as the one
    // who made the attack (the cartridge's apply-and-show routine, which takes each battler's
    // entries off its HP or MP and writes the numeral commands into the battle script).
    [[nodiscard]] ApplyDamageResult applyDamage(const DamageScenario& scenario);

    // The spell in the scenario bounced off the battlers in its attack.params.reflectedOff, or a
    // Launcher's or Super Ball's hits made (CalcReflectDmg). The routine runs the cartridge's own
    // status and special-effect passes, which the port's hooks leave out, so its scenarios are spells
    // those passes change nothing for: no statuses or special effect, no element that melts Frozen on
    // a Frozen target, no Vanished target, no miss for a status the target is immune to.
    [[nodiscard]] ReflectedAttackResult reflectedAttack(const DamageScenario& scenario);

    // The writes the last call made to the watched cells, in order: the damage word at
    // $7E:11B0-11B1 and the multiplier's steps at $7E:00BC.
    [[nodiscard]] const std::vector<WatchedWrite>& writes() const { return writes_; }

    // Replay one call the trace logged against the port: the battle is rebuilt from the cells the
    // routine read (every other cell as the oracle writes a default scenario), the attacker and target
    // from the attack's context and the cells the routine touched, and the port's answer is compared
    // with the cells the routine wrote. Returns every cell where they part, one per line; empty when
    // they agree. Throws std::runtime_error for a call it cannot rebuild, naming why.
    [[nodiscard]] std::string replay(const TracedCall& call);

    // Log every call the oracle's routines make to the traced routines in the trace's own lines; a
    // call the oracle makes itself is closed as the attack frame's.
    void trace(bool on) { tracing_ = on; }
    [[nodiscard]] std::vector<std::string> takeTrace() { return std::exchange(traced_, {}); }

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
    using TargetCall = void(std::uint16_t, std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);

    // Run `routine` with the watches recording.
    void call(const std::function<void()>& routine);

    void writeScenario(const DamageScenario& scenario);
    void writeTargetCells(const DamageScenario& scenario);
    void writeApplyCells(const DamageScenario& scenario);
    void writeAimCells(const DamageScenario& scenario);
    void writeContext(const DamageScenario& scenario);
    [[nodiscard]] DamageScenario readScenario();
    [[nodiscard]] TargetDamageResult readTargetResult();
    [[nodiscard]] ApplyDamageResult readApplyResult(std::size_t firstNumeralCommand = 0,
                                                    std::uint32_t firstNumeralBuffer = 0);
    [[nodiscard]] ReflectedAttackResult readReflectResult();
    [[nodiscard]] std::vector<battle::DamageNumbersShown> readNumerals(std::size_t firstCommand,
                                                                       std::uint32_t firstBuffer);
    void writeByte(std::uint32_t at, std::uint8_t value);
    void writeWord(std::uint32_t at, std::uint16_t value);
    [[nodiscard]] std::uint8_t readByte(std::uint32_t at);
    [[nodiscard]] std::uint16_t readWord(std::uint32_t at);

    retropp::Vm::SNES machine_;
    bool reset_ = false;
    bool recording_ = false;
    std::vector<WatchedWrite> writes_;
    bool tracing_ = false;
    std::vector<std::string> traced_;
    std::unique_ptr<DamageTrace> trace_;
    retropp::Routine<Call> calcMagicDmg_;
    retropp::Routine<Call> calcDmg_;
    retropp::Routine<Call> relicDmgEffect_;
    retropp::Routine<Multiply> applyDmgMult_;
    retropp::Routine<TargetCall> calcTargetDmg_;
    retropp::Routine<Call> applyAndShowDmg_;
    retropp::Routine<TargetCall> calcReflectDmg_;
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

// A scenario for the per-target pass and the target it is worked out against.
struct TargetScenario {
    DamageScenario scenario;
    battle::BattlerSlot target = battle::BattlerSlot::monster(0);
};

// The scenarios the per-target sweep draws, from the same seed: every cell the pass reads drawn
// within the game's own ranges — the record's bytes (a fractional attack's power at most 16, as
// every one in the game has), the damage and the multiplier's steps, the
// command, the effect (the Atma Weapon only for a party member), the flags the frame sets, the
// blocks, Forcefield and the random-number position, and for every battler its level, HP and MP (a
// party member's at most 9,999 and 999, never above the maximum), defenses, elements, statuses,
// stance, links and ledger entries.
[[nodiscard]] TargetScenario drawTargetScenario(std::mt19937& rng);

// One line naming a drawn per-target scenario's inputs, for a report.
[[nodiscard]] std::string describe(const TargetScenario& drawn);

// The scenarios the apply-and-show sweep draws, from the same seed: every battler's HP, MP and
// maximums within the game's own ranges, its statuses (Zombie among them), whether it dies at 0
// MP, its links and ledger entries; an attack on HP or on MP, who made it, the Golem's or
// Interceptor's block and the Golem's HP, the battlers shown "Miss", the invincible monsters, the
// party members a Black Belt may already counter for, and the weapon's spellcast byte.
[[nodiscard]] DamageScenario drawApplyScenario(std::mt19937& rng);

// The scenarios the reflected-attack sweep draws, from the same seed: a spell by any battler with
// the damage, power, elements, hit rate and flags drawn within the ranges reflectedAttack() takes,
// bounced off one to four battlers, or a Launcher's or Super Ball's one to eight hits; every
// battler on the field or not, with its statuses, Reflect, defenses, magic block, stamina and block
// graphics drawn; the battle type and pincer sides, the aiming rules, Charm and the party members
// fighting against the party.
[[nodiscard]] DamageScenario drawReflectScenario(std::mt19937& rng);

}  // namespace ostinato::trace
