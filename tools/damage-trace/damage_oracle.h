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
// the port's formulas, tests/test_status_changes.cpp and tests/test_status_effect.cpp against its
// status rules, tests/test_attacker_effects.cpp, tests/test_target_effects.cpp and
// tests/test_control_menu.cpp against its special effects, tests/test_cover_and_params.cpp
// against its cover and its setting up of the attack's animation, and tests/test_espers_oracle.cpp
// against an esper's spells learned and a level-up at a battle's end.
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
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/status_set.h"

namespace ostinato::trace {

// --- the trace ---------------------------------------------------------------------------------

// A routine the trace follows: where it starts, where its main caller's call to it returns (for an
// attack's routines, the attack frame's), where the call to it from the reflected attack returns (0:
// it is not called from there), where the calls from anywhere else return (0: no other), and
// whether it works on the party's records — the records, the spells known and the battle's-end
// cells beside them are logged only inside its calls.
struct TracedRoutine {
    const char* name;
    std::uint32_t entry;
    std::uint32_t frameReturn;
    std::uint32_t bounceReturn;
    std::array<std::uint32_t, 2> otherReturns{};
    bool partyRecords = false;
};

// The base damage, the damage to one target, the damage taken and shown, the statuses taking effect,
// an attack's statuses on one target, its special effect on its attacker and on one target, and at a
// won battle's end an esper's spells learned and a level-up (battle/battle_main.asm: 7217 and 8096;
// 1809, 8362 and 8440; 16193, 8365 and 8455; 11032, 8109, and the battle's start 6128 and a formation
// change 13231; 11094, 8356 and 8434; 10118 and 8275; 9087, 8357 and 8435; 15661 and 15404; 15761
// and 15748 — the program sits at $C2:0000).
inline constexpr std::array<TracedRoutine, 9> kTracedRoutines{{
    {"CalcDmg", 0xC22B9D, 0xC2322E, 0},
    {"CalcTargetDmg", 0xC20B83, 0xC2346C, 0xC23509},
    {"ApplyAndShowDamage", 0xC262EF, 0xC23473, 0xC23528},
    {"UpdateStatus", 0xC24391, 0xC2324D, 0, {0xC224A7, 0xC250AA}},
    {"MagicStatusEffect", 0xC24406, 0xC2345C, 0xC234F9},
    {"DoAttackerEffect", 0xC23E7D, 0xC233A9, 0},
    {"DoTargetEffect", 0xC2387E, 0xC2345F, 0xC234FC},
    {"LearnGenjuMagic", 0xC2602A, 0xC25E72, 0, {}, true},
    {"DoLevelUp", 0xC260C2, 0xC260B4, 0, {}, true},
}};

// The cells the traced routines read and write, as ranges of work RAM: the console's frame count; the
// direct-page params block, attack flags and multiplier; the party's gil and steps; the attack's record copy and
// damage; the party members' control menus; the battle's copy of the party's hands; the numeral
// buffer; the script queue; the monsters standing, the message's item and amount, the formation's
// flags, the battlers leaving, a party member's equipment to work out and the monsters drawn flipped;
// each battler's bit and its actions waiting; what is to be worked out again for each battler; the
// links between battlers, Seize, Love Token, Charm and Zinger, and the items to steal; the statuses
// each battler can be given; the damage ledger; the per-command cells; the battle's list of actions;
// the battle's own cells (the missed sets, the blocks, the Golem's HP, the monsters fallen, the
// battlers that died this action and whose statuses changed, the script queue's place and end); each
// battler's flags, stop timer, stats, monster battle flags, metamorph byte, monster flags, sleep timer,
// the commands it was last hit with, gold, AI variable, pending statuses, triggers, statuses for a
// counterattack, Forcefield's elements and the target count, statuses, Reflect and freeze timers, and
// who is away from the field.
struct TracedRange {
    const char* key;
    std::uint32_t at;
    std::uint32_t size;
};
inline constexpr std::array<TracedRange, 33> kTracedRanges{{
    {"frame count", 0x7E021E, 1},       {"direct page", 0x7E00A0, 32},       {"gil and steps", 0x7E1860, 9},
    {"attack record", 0x7E11A0, 18},    {"monster indexes", 0x7E1FF9, 20},   {"control menus", 0x7E205E, 48},
    {"party's hands", 0x7E2B86, 40},    {"numeral buffer", 0x7E2BCE, 160},   {"script queue", 0x7E2D6E, 256},
    {"monster cells", 0x7E2F2F, 38},    {"battler bits", 0x7E3018, 20},      {"action list", 0x7E3184, 128},
    {"to work out", 0x7E3204, 20},      {"links", 0x7E327C, 388},            {"command cells", 0x7E3400, 32},
    {"actions", 0x7E3420, 768},         {"battle cells", 0x7E3A30, 96},      {"flags", 0x7E3AA0, 20},
    {"stop timers", 0x7E3AF0, 20},      {"stats", 0x7E3B18, 340},            {"monster battle flags", 0x7E3C80, 21},
    {"monster flags", 0x7E3C95, 20},    {"sleep timers", 0x7E3CF8, 20},      {"last hits", 0x7E3D48, 40},
    {"gold", 0x7E3D98, 20},             {"AI variables", 0x7E3DAC, 20},      {"pending statuses", 0x7E3DD4, 80},
    {"triggers", 0x7E3E4C, 20},         {"statuses for a counter", 0x7E3E60, 40}, {"forcefield", 0x7E3EC8, 2},
    {"statuses", 0x7E3EE4, 40},         {"timers", 0x7E3F0C, 20},            {"away from the field", 0x7E3F2C, 2},
}};

// The cells the routines on the party's records read and write, logged only inside their calls: the
// battle's-end cells in direct page (the flags the win shows its messages by, the magic points and
// the place of the spells being learned, the level-up's gains), the sixteen character records (37
// bytes each) and the spells each of the first twelve characters knows (54 bytes each).
inline constexpr std::array<TracedRange, 3> kPartyRecordRanges{{
    {"battle's-end cells", 0x7E00EE, 18},
    {"character records", 0x7E1600, 592},
    {"spells known", 0x7E1A6E, 648},
}};

// The trace on one machine: escapes on each traced routine's entry and return sites and watches over
// kTracedRanges and kPartyRecordRanges, the second logged only inside the calls of the routines on
// the party's records. At each entry it logs the routine and the attack's context — the params block
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
// The context is what the cells hold at the routine's entry, on a running machine as on a parked one:
// the trace keeps the last byte the guest wrote to each of them and lays those over what the machine
// answers. Declares on the machine when constructed: host the cartridge first, and construct it before
// the machine runs. `access`, when given, sees every access to a watched cell whether a call is open
// or not.
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

    // Drop the context bytes kept from the guest's writes: for a caller that writes those cells itself
    // through the machine's own write verb, which the watches do not see, on a parked machine.
    void forgetContextWrites();

private:
    struct Places;
    std::unique_ptr<Places> places_;
    Line line_;
    Frame frame_;
    int open_ = 0;
    bool partyRecordsOpen_ = false;
};

// One routine call as the trace logged it: the routine, the frame it ran on and the address it
// returned to; each cell's value when the routine first read it, for every cell it read before it
// wrote it; each cell's last value written; the attack's context at its entry; and, for an attack's
// special effect on one target, the target, where the trace shows it.
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
    std::optional<battle::BattlerSlot> target;
};

// Every call in a trace, in order. A call that never returns to one of its routine's return sites —
// one none of the callers kTracedRoutines names made — is left out, and a return with no call open
// is skipped. The attack frame and the bounce work out an attack's statuses on a target and then its
// special effect on the same target, so a DoTargetEffect call that follows a MagicStatusEffect call
// takes that call's target: the one battler whose cells the status call touched. Throws
// std::runtime_error naming the line for a line that starts like an event and is not one.
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

// What every battler's waiting statuses taking effect leaves, as the same fields on both sides:
// each battler's statuses, its statuses to set and to clear, the statuses it is to have for a
// counterattack, what is to be worked out again for it (as the cartridge's byte holds it: bit 7
// castable spells, 6 drop its actions, 5 start a condemned count, 4 stop it, 3 its commands, 2 its
// speed, 1 its menu after Morph, 0 Palidor), whether its menu may open, whether its gauge empties
// after the action, Phantasm's drain, whether a party member's equipment is to be worked out again,
// its four status timers; and the battlers that died this action, the party members whose statuses
// changed, and the monsters standing and fallen.
struct StatusChangesResult {
    std::array<StatusSet, battle::kBattlerSlots> status{};
    std::array<StatusSet, battle::kBattlerSlots> statusesToSet{};
    std::array<StatusSet, battle::kBattlerSlots> statusesToClear{};
    std::array<StatusSet, battle::kBattlerSlots> statusForCounter{};
    std::array<std::uint8_t, battle::kBattlerSlots> pendingUpdates{};
    std::array<bool, battle::kBattlerSlots> menuMayOpen{};
    std::array<bool, battle::kBattlerSlots> emptyGaugeAfterAction{};
    std::array<bool, battle::kBattlerSlots> phantasm{};
    std::array<bool, battle::kBattlerSlots> equipmentChanged{};
    std::array<std::uint8_t, battle::kBattlerSlots> sleepTimer{};
    std::array<std::uint8_t, battle::kBattlerSlots> stopTimer{};
    std::array<std::uint8_t, battle::kBattlerSlots> reflectTimer{};
    std::array<std::uint8_t, battle::kBattlerSlots> freezeTimer{};
    battle::BattlerSet diedAwaitingCounter;
    battle::BattlerSet statusChanged;
    battle::BattlerSet monstersStanding;
    battle::BattlerSet monstersFallen;
};

// The port's statuses taking effect over `scenario`, read back as a StatusChangesResult.
[[nodiscard]] StatusChangesResult portStatusChanges(DamageScenario scenario);

[[nodiscard]] std::string differences(const StatusChangesResult& port, const StatusChangesResult& guest);

// What an attack's statuses on one target leave, as the same fields on both sides: the target's
// statuses to set and to clear, the battlers the effect missed and the battlers shown "Miss".
struct StatusEffectResult {
    StatusSet statusesToSet;
    StatusSet statusesToClear;
    battle::BattlerSet missedByEffect;
    battle::BattlerSet missed;
};

// The port's statuses of the scenario's attack on `target`, read back as a StatusEffectResult. The
// effect's misses start empty, as the attack frame leaves them for each target.
[[nodiscard]] StatusEffectResult portStatusEffect(DamageScenario scenario, battle::BattlerSlot target);

[[nodiscard]] std::string differences(const StatusEffectResult& port, const StatusEffectResult& guest);

// What an attack's special effect on its attacker leaves, as the same cells on both sides, each named
// and given as text: the attack's record, damage and animation values, its flags, params and aim,
// what the command carries to its next attack, the battle's cells the effects write, the attacker's
// own cells and the party's gil, the params blocks and animations added to the script, and the
// random-number position.
struct AttackerEffectResult {
    std::map<std::string, std::string> cells;
};

// The scenario after the port's attacker effect has run over it, and the script it wrote.
struct AttackerEffectRun {
    DamageScenario after;
    battle::BattleScriptBuilder script;
};

// The port's special effect of the scenario's attack on the scenario's attacker.
[[nodiscard]] AttackerEffectRun runAttackerEffect(DamageScenario scenario);

// A port run read back as an AttackerEffectResult.
[[nodiscard]] AttackerEffectResult attackerEffectResult(const AttackerEffectRun& run);

[[nodiscard]] std::string differences(const AttackerEffectResult& port, const AttackerEffectResult& guest);

// What an attack's special effect on one target leaves, as the same cells on both sides, each named
// and given as text: whether the effect missed the target; the attack's record, damage, multiplier,
// animation values, params and aim; the command's message and what it names, its spell or sketch to
// make next, its earlier targets, the battlers shown "Miss", whether counterattacks are on and whether
// an attack with no one left is hidden; the battle's battlers leaving, gone, fallen, swallowed, taken
// from the party and holding an item, the monsters drawn flipped, Zinger's two, the end-of-battle
// event and the party's gil; for every battler its HP, MP, level, statuses and statuses waiting, what
// is to be worked out again, Air Anchor, its row, gauge and menu flags, Overcast, Phantasm and the
// control menu flag, its elements, every link, its item in hand, AI variable, items to steal, gold,
// the command it was last hit with, its last-attacker mark, its actions waiting and, for a party
// member, its control menu; the commands, params blocks and interrupting actions the effect added;
// and the random-number position.
struct TargetEffectResult {
    std::map<std::string, std::string> cells;
};

// The scenario after the port's target effect has run over it: the target, whether the effect missed
// it, and the script it wrote. The script starts with the attack's own animation, put there by the
// attack frame before any target's effect.
struct TargetEffectRun {
    DamageScenario after;
    battle::BattlerSlot target = battle::BattlerSlot::monster(0);
    bool missed = false;
    battle::BattleScriptBuilder script;
};

// The port's special effect of the scenario's attack on `target`.
[[nodiscard]] TargetEffectRun runTargetEffect(DamageScenario scenario, battle::BattlerSlot target);

// A port run read back as a TargetEffectResult.
[[nodiscard]] TargetEffectResult targetEffectResult(const TargetEffectRun& run);

[[nodiscard]] std::string differences(const TargetEffectResult& port, const TargetEffectResult& guest);

// What setting up the attack's animation leaves (InitGfxParams), as the same cells on both sides,
// each named and given as text: the params block, the three aiming rules it can add, and whether the
// casting swirl is still suppressed.
struct ParamsInitResult {
    std::map<std::string, std::string> cells;
};

// The port's setting up of the scenario's animation (battle::initParams), read back as a
// ParamsInitResult.
[[nodiscard]] ParamsInitResult portInitParams(DamageScenario scenario);

[[nodiscard]] std::string differences(const ParamsInitResult& port, const ParamsInitResult& guest);

// What a True Knight's or a Love Token's cover leaves (CoverEffect), as the same cells on both sides,
// each named and given as text: the targets, the ally covered and the battlers stepping in.
struct CoverResult {
    std::map<std::string, std::string> cells;
};

// The port's cover over the scenario (battle::coverTarget), read back as a CoverResult.
[[nodiscard]] CoverResult portCoverTarget(DamageScenario scenario);

[[nodiscard]] std::string differences(const CoverResult& port, const CoverResult& guest);

// --- the party's records at a battle's end -------------------------------------------------------

// One character's record and spells as a won battle's end reads them: the record and which of the
// sixteen it is, whose spells are being learned (one of the first twelve characters), the battle's
// magic points, and the two-bit HP and MP boosts the cartridge keeps in the top of the record's
// maximum HP and MP words (0-3 each), which the port keeps no field for.
struct RecordScenario {
    game::CharacterRecord record;
    std::size_t recordIndex = 0;
    CharacterPropId actor = CharacterPropId::TERRA;
    game::SpellProgress spells;
    std::uint8_t magicPoints = 0;
    std::uint8_t hpBoostBits = 0;
    std::uint8_t mpBoostBits = 0;
};

// What a level-up leaves of the record, as the same fields on both sides.
struct LevelUpResult {
    std::uint8_t level = 0;
    std::uint16_t maxHp = 0;
    std::uint16_t maxMp = 0;
    std::uint8_t hpBoostBits = 0;
    std::uint8_t mpBoostBits = 0;
    std::uint8_t vigor = 0;
    std::uint8_t speed = 0;
    std::uint8_t stamina = 0;
    std::uint8_t magicPower = 0;
};

// The port's level-up of the scenario's record (battle::levelUp), on the US cartridges' MP curve,
// with the boosts carried through as the scenario has them.
[[nodiscard]] LevelUpResult portLevelUp(const RecordScenario& scenario);

[[nodiscard]] std::string differences(const LevelUpResult& port, const LevelUpResult& guest);

// What the esper's spells learned leave, as the same fields on both sides: the learned percentage of
// each of the 54 spells, and whether any of them was stored.
struct SpellLearningResult {
    std::array<std::uint8_t, game::kLearnableSpells> progress{};
    bool progressed = false;
};

// The port's spells learned from the scenario record's esper (battle::learnFromEsper).
[[nodiscard]] SpellLearningResult portLearnFromEsper(const RecordScenario& scenario);

[[nodiscard]] std::string differences(const SpellLearningResult& port, const SpellLearningResult& guest);

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
    // status and special-effect passes for each bounce that hits, as the port's hooks do; this
    // comparison is written for spells neither pass changes anything for: no statuses or special
    // effect, no element that melts Frozen on a Frozen target, no Vanished target, no miss for a
    // status the target is immune to.
    [[nodiscard]] ReflectedAttackResult reflectedAttack(const DamageScenario& scenario);

    // Every present battler's waiting statuses taking effect over the scenario (UpdateStatus): every
    // battler's cells the pass reads written from the scenario, then read back.
    [[nodiscard]] StatusChangesResult statusChanges(const DamageScenario& scenario);

    // The statuses of the scenario's attack on `target`, one of the targets it hits
    // (MagicStatusEffect), with the scenario's attacker as the one who made it.
    [[nodiscard]] StatusEffectResult statusEffect(const DamageScenario& scenario, battle::BattlerSlot target);

    // The special effect of the scenario's attack on the scenario's attacker (DoAttackerEffect), called
    // as the attack frame calls it, before the hit check.
    [[nodiscard]] AttackerEffectResult attackerEffect(const DamageScenario& scenario);

    // The special effect of the scenario's attack on `target`, one of the targets it hits
    // (DoTargetEffect), with the scenario's attacker as the one who made it — called as the attack
    // frame calls it, after the attack's statuses on the target and with the frame's animation
    // already in the script. The scenario holds what the frame leaves there: the target out of the
    // targets hit, already marked to answer the attacker, and its last command hit recorded.
    [[nodiscard]] TargetEffectResult targetEffect(const DamageScenario& scenario, battle::BattlerSlot target);

    // The attack's animation set up over the scenario (InitGfxParams), called as the attack frame
    // calls it: after the aim and the cover, with the attacks still to come and the Dragon Horn as
    // the scenario holds them.
    [[nodiscard]] ParamsInitResult initParams(const DamageScenario& scenario);

    // The cover over the scenario (CoverEffect), called as the attack frame calls it: after the aim,
    // with the accumulator wide and the index registers narrow. The scenario's targets are the aim's.
    [[nodiscard]] CoverResult coverEffect(const DamageScenario& scenario);

    // The spells the scenario record's esper teaches learned further (LearnGenjuMagic), called as a
    // won battle's end calls it for a party member with an esper: the record written at its place
    // among the sixteen, the character's spells known at theirs, the battle's magic points, and Morph
    // available so that a spell stored shows in the win's flags. The record must have an esper.
    [[nodiscard]] SpellLearningResult learnFromEsper(const RecordScenario& scenario);

    // One level-up of the scenario's record (DoLevelUp), called as a won battle's end calls it: the
    // record written at its place with the boosts in the top of its maximum HP and MP words. The
    // record's level must be below 99.
    [[nodiscard]] LevelUpResult levelUp(const RecordScenario& scenario);

    // The writes the last call made to the watched cells, in order: the damage word at
    // $7E:11B0-11B1 and the multiplier's steps at $7E:00BC.
    [[nodiscard]] const std::vector<WatchedWrite>& writes() const { return writes_; }

    // Replay one call the trace logged against the port: the battle is rebuilt from the cells the
    // routine read (every other cell as the oracle writes a default scenario), the attacker and target
    // from the attack's context and the cells the routine touched, and the port's answer is compared
    // with the cells the routine wrote. The statuses taking effect replay from any of their callers;
    // an attack's statuses on one target take the target from the battler cells they touched. An
    // attack's special effect takes its attacker from the params block (for a bounce, the battler it
    // bounced off, or for a Launcher's or Super Ball's hits the lowest battler in the bounces that hit)
    // and its effect on one target takes the call's target (TracedCall::target). Returns every cell
    // where they part, one per line; empty when they agree. Throws std::runtime_error for a call it
    // cannot rebuild, naming why.
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
    using PassCall = void(std::uint8_t, std::uint8_t, std::uint16_t);

    // Run `routine` with the watches recording.
    void call(const std::function<void()>& routine);

    void writeScenario(const DamageScenario& scenario);
    void writeTargetCells(const DamageScenario& scenario);
    void writeApplyCells(const DamageScenario& scenario);
    void writeAimCells(const DamageScenario& scenario);
    void writeContext(const DamageScenario& scenario);
    void writeStatusCells(const DamageScenario& scenario);
    void writeEffectCells(const DamageScenario& scenario);
    void writeTargetEffectCells(const DamageScenario& scenario);
    [[nodiscard]] AttackerEffectResult readAttackerEffectResult();
    [[nodiscard]] TargetEffectResult readTargetEffectResult();
    [[nodiscard]] StatusChangesResult readStatusChangesResult();
    [[nodiscard]] StatusEffectResult readStatusEffectResult(battle::BattlerSlot target);
    [[nodiscard]] DamageScenario readStatusScenario();
    [[nodiscard]] std::string replayStatusCall(const TracedCall& call, const TracedRoutine& routine);
    [[nodiscard]] DamageScenario readEffectScenario();
    [[nodiscard]] std::string replayEffectCall(const TracedCall& call, const TracedRoutine& routine, bool bounce);
    void writeRecordScenario(const RecordScenario& scenario);
    [[nodiscard]] LevelUpResult readLevelUpResult(std::size_t recordIndex);
    [[nodiscard]] SpellLearningResult readSpellLearningResult(CharacterPropId actor);
    [[nodiscard]] std::string replayRecordCall(const TracedCall& call, const TracedRoutine& routine);
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
    retropp::Routine<PassCall> updateStatus_;
    retropp::Routine<TargetCall> magicStatusEffect_;
    retropp::Routine<Call> doAttackerEffect_;
    retropp::Routine<TargetCall> doTargetEffect_;
    retropp::Routine<Call> initGfxParams_;
    retropp::Routine<Call> coverEffect_;
    retropp::Routine<TargetCall> learnGenjuMagic_;
    retropp::Routine<Call> doLevelUp_;
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

// The scenarios the statuses-taking-effect sweep draws, from the same seed: for every battler,
// whether it is on the field, its statuses, its statuses to set and to clear and the statuses it can
// be given, every bit of each drawn; its HP and maximum HP (a party member's at most 9,999), often
// near an eighth of the maximum; its Overcast and Phantasm triggers, whether it is waiting to
// retaliate, what is to be worked out again for it, its menu and gauge flags, its timers and
// whether its equipment is to be worked out again; and the battlers that died this action, the
// party members whose statuses changed and the monsters standing and fallen.
[[nodiscard]] DamageScenario drawStatusChangesScenario(std::mt19937& rng);

// The scenarios the status-effect sweep draws, from the same seed: an attack with every bit of its
// traits, element, second flags, misc flags and statuses drawn — except the two status modes
// together, which no attack has and on which the cartridge runs unrelated code — whether Vanish
// applies, and a target
// with its statuses, statuses to set and to clear, the statuses it can be given and whether it is
// undead drawn; and the battlers already shown "Miss".
[[nodiscard]] TargetScenario drawStatusEffectScenario(std::mt19937& rng);

// The scenarios the attacker-effect sweep draws, from the same seed: an effect anywhere in the effect
// space, half the time one of the effects that act on the attacker; the attack's record, damage,
// multiplier, command and animation values, its flags, aim and params; the command's attacks to come,
// target count and message; for every battler its level, HP and MP, statuses, flags, hands, gold, Seize
// and whether it is on the field or in its menu; the party's gil and steps, the battle's frame count,
// Forcefield, Quick, the Golem, the allies standing, whether the party can run, whether a counterattack
// is running, and the random-number position.
[[nodiscard]] DamageScenario drawAttackerEffectScenario(std::mt19937& rng);

// The scenarios the target-effect sweep draws, from the same seed: every cell the attacker-effect
// sweep draws, then an effect that acts on the target three times in four, a target and, for the
// command effects that open a party member's menu, a party member attacking; for every battler its
// level, stamina and statuses, whether instant death can reach it, its metamorph byte, items to steal
// and monster, every link (a Control link sometimes ending), its AI variable, item in hand and the
// action it has waiting; the formation, the attack's statuses and hit rate, Mind Blast's picks, the
// battlers gone, fallen, swallowed and taken from the party, Zinger's two and the monsters drawn
// flipped. The target is left as the attack frame leaves it for the effect.
[[nodiscard]] TargetScenario drawTargetEffectScenario(std::mt19937& rng);

// The scenarios the cover sweep draws, from the same seed: every cell the target-effect sweep draws,
// then for every battler its statuses cleared one time in two, Near Fatal and the True Knight's relic
// each one time in two, and one time in six a Control link on it (ending one time in two) or else one
// time in six a Seize hold on it; and targets for the blow three times in four. Nothing has been
// bounced off anyone, no one has stepped in yet and no ally is covered.
[[nodiscard]] DamageScenario drawCoverScenario(std::mt19937& rng);

// The scenarios the level-up sweep draws, from the same seed: one of the sixteen records at a level
// from 1 to 98, its maximum HP up to 9,999 and maximum MP up to 999 (each often within a level's gain
// of its cap), its vigor, speed, stamina and magic power up to 128 (often 126 to 128, and now and then
// 252 to 255, where the bonus comes round in the stat's byte), the two boosts 0-3 each, and any of the
// 27 espers or none.
[[nodiscard]] RecordScenario drawLevelUpScenario(std::mt19937& rng);

// The scenarios the esper-learning sweep draws, from the same seed: one of the first twelve
// characters with any of the 27 espers, 0 to 10 magic points or one time in four any byte (so the
// rate times the magic points passes a byte), and each of the 54 spells at a
// percentage below 100, just learned or learned.
[[nodiscard]] RecordScenario drawLearningScenario(std::mt19937& rng);

// One line naming a drawn record scenario's inputs, for a report.
[[nodiscard]] std::string describe(const RecordScenario& scenario);

}  // namespace ostinato::trace
