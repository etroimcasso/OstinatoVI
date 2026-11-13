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
// against its cover and its setting up of the attack's animation, tests/test_espers_oracle.cpp
// against an esper's spells learned and a level-up at a battle's end,
// tests/test_attack_oracle.cpp against the whole attack frame, and tests/test_own_actions_oracle.cpp
// against the battle's own actions.
#pragma once

#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "retropp/clock.h"
#include "retropp/run_loop.h"
#include "retropp/vm.h"

#include "battle/ai_targets.h"
#include "battle/attack.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "game/character_record.h"
#include "game/battle_switches.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/monster_id.h"
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
// and 15748 — the program sits at $C2:0000). Then a battler's AI: its script readied (7442, called
// as a monster is loaded, 7313, and as a scripted battle seats a party member, 7840), its turn (447,
// called from a turn, 223, and reached from a scripted party member's gauge filling, 512, which
// returns to 5742's caller), its counterattack (12468 and 12424), and the three commands only a
// script queues (5049, 5071 and 5132, each returning to the command dispatch, 3111). Then the steps of
// a won battle on the party's records: the experience added (16043, called at 15390 and again for an
// Exp. Egg at 15394), the spells equipment teaches (15623, 15401), the abilities a level teaches
// (15958, 15753) and the rages the fallen monsters teach (12185, 12035).
inline constexpr std::array<TracedRoutine, 19> kTracedRoutines{{
    {"CalcDmg", 0xC22B9D, 0xC2322E, 0},
    {"CalcTargetDmg", 0xC20B83, 0xC2346C, 0xC23509},
    {"ApplyAndShowDamage", 0xC262EF, 0xC23473, 0xC23528},
    {"UpdateStatus", 0xC24391, 0xC2324D, 0, {0xC224A7, 0xC250AA}},
    {"MagicStatusEffect", 0xC24406, 0xC2345C, 0xC234F9},
    {"DoAttackerEffect", 0xC23E7D, 0xC233A9, 0},
    {"DoTargetEffect", 0xC2387E, 0xC2345F, 0xC234FC},
    {"LearnGenjuMagic", 0xC2602A, 0xC25E72, 0, {}, true},
    {"DoLevelUp", 0xC260C2, 0xC260B4, 0, {}, true},
    {"InitAI", 0xC22D71, 0xC22C3D, 0, {0xC23041}},
    {"ExecMonsterAction", 0xC202DC, 0xC2013C, 0, {0xC221A1}},
    {"ExecAIRetal", 0xC24BF4, 0xC24B9A, 0},
    {"Cmd_2e", 0xC21DFA, 0xC21401, 0},
    {"Cmd_2f", 0xC21E1A, 0xC21401, 0},
    {"Cmd_30", 0xC21E5E, 0xC21401, 0},
    {"AddExp", 0xC26235, 0xC25E4F, 0, {0xC25E59}, true},
    {"LearnItemMagic", 0xC25FEF, 0xC25E6A, 0, {}, true},
    {"LearnAbilities", 0xC261B6, 0xC260BF, 0, {}, true},
    {"LearnRage", 0xC24A07, 0xC248EE, 0, {}, true},
}};

// The cells the traced routines read and write, as ranges of work RAM: the console's frame count; the
// direct-page params block, attack flags and multiplier; the party's gil and steps; the attack's record copy and
// damage; the party members' control menus; the battle's copy of the party's hands; the numeral
// buffer; the script's params blocks; the script queue; the monsters standing, the message's item and amount, the formation's
// flags, the battlers leaving, a party member's equipment to work out and the monsters drawn flipped;
// each battler's bit and its actions waiting; what is to be worked out again for each battler; the
// links between battlers, Seize, Love Token, Charm and Zinger, and the items to steal; the statuses
// each battler can be given; the damage ledger; the per-command cells; the battle's list of actions;
// the battle's own cells (the missed sets, the blocks, the Golem's HP, the monsters fallen, the
// battlers that died this action and whose statuses changed, the script queue's place and end); each
// battler's flags, stop timer, stats, monster battle flags, metamorph byte, monster flags, sleep timer,
// the commands it was last hit with, gold, AI variable, pending statuses, triggers, statuses for a
// counterattack, Forcefield's elements and the target count, statuses, Reflect and freeze timers, and
// who is away from the field. Then what a battler's AI reads and writes besides: the script reader's
// direct-page cells, the formation, the character records' party bytes and the party played, the
// lores known, the story's flags, the battle type and a pincer's two sides, Strago's slot, the
// party's spell lists, each battler's gauge and advance wait, the scripts' places and counts, the
// counterattack line, the step being read, the restriction a counterattack starts with, the ATB
// rates, the block graphics, the scripts' wait places, the own timers, the battle's variables and
// switches, the names listed, and the party slots' members and records.
struct TracedRange {
    const char* key;
    std::uint32_t at;
    std::uint32_t size;
};
inline constexpr std::array<TracedRange, 56> kTracedRanges{{
    {"frame count", 0x7E021E, 1},       {"direct page", 0x7E00A0, 32},       {"gil and steps", 0x7E1860, 9},
    {"attack record", 0x7E11A0, 18},    {"monster indexes", 0x7E1FF9, 20},   {"control menus", 0x7E205E, 48},
    {"party's hands", 0x7E2B86, 40},    {"numeral buffer", 0x7E2BCE, 160},   {"params blocks", 0x7E2C6E, 256},
    {"script queue", 0x7E2D6E, 256},
    {"monster cells", 0x7E2F2F, 38},    {"battler bits", 0x7E3018, 20},      {"action list", 0x7E3184, 128},
    {"to work out", 0x7E3204, 20},      {"links", 0x7E327C, 388},            {"command cells", 0x7E3400, 32},
    {"actions", 0x7E3420, 768},         {"battle cells", 0x7E3A30, 96},      {"flags", 0x7E3AA0, 20},
    {"stop timers", 0x7E3AF0, 20},      {"stats", 0x7E3B18, 340},            {"monster battle flags", 0x7E3C80, 21},
    {"monster flags", 0x7E3C95, 20},    {"sleep timers", 0x7E3CF8, 20},      {"last hits", 0x7E3D48, 40},
    {"gold", 0x7E3D98, 20},             {"AI variables", 0x7E3DAC, 20},      {"pending statuses", 0x7E3DD4, 80},
    {"triggers", 0x7E3E4C, 20},         {"statuses for a counter", 0x7E3E60, 40}, {"forcefield", 0x7E3EC8, 2},
    {"statuses", 0x7E3EE4, 40},         {"timers", 0x7E3F0C, 20},            {"away from the field", 0x7E3F2C, 2},
    {"script reader", 0x7E00F0, 16},    {"formation", 0x7E11E0, 2},          {"party bytes", 0x7E1850, 16},
    {"party played", 0x7E1A6D, 1},      {"lores known", 0x7E1D29, 3},        {"story flags", 0x7E1EDF, 1},
    {"battle type", 0x7E201F, 1},       {"pincer sides", 0x7E2EAC, 2},       {"Strago's slot", 0x7E3007, 1},
    {"spell lists", 0x7E3084, 256},     {"gauges and waits", 0x7E3218, 40},  {"script places", 0x7E3240, 60},
    {"counterattack line", 0x7E3920, 256}, {"script step", 0x7E3A2C, 4},     {"restricted on entry", 0x7E3A98, 2},
    {"ATB rates", 0x7E3AC8, 20},        {"block graphics", 0x7E3CE4, 20},    {"wait places", 0x7E3D0C, 40},
    {"own timers", 0x7E3DC0, 20},       {"battle variables", 0x7E3EB0, 24},  {"names listed", 0x7E3ECA, 1},
    {"party slots", 0x7E3ED8, 8},
}};

// The cells the routines on the party's records read and write, logged only inside their calls: the
// first two battle's-end cells in direct page (the rest of them, the flags the win shows its messages
// by, the magic points and the place of the spells being learned and the level-up's gains, are the
// script reader's cells in kTracedRanges and logged inside every call), the sixteen character records
// (37 bytes each), the spells each of the first twelve characters knows (54 bytes each), the Morph
// supply with the SwdTechs and Blitzes known, and the rages and dances known (the lores between them
// are in kTracedRanges).
inline constexpr std::array<TracedRange, 5> kPartyRecordRanges{{
    {"battle's-end cells", 0x7E00EE, 2},
    {"character records", 0x7E1600, 592},
    {"spells known", 0x7E1A6E, 648},
    {"Morph supply, SwdTechs and Blitzes", 0x7E1CF6, 51},
    {"rages and dances known", 0x7E1D2C, 33},
}};

// The routines the trace follows whole — a battle's ending, which calls traced routines of its own —
// logged as what their cells hold on entry and on return rather than access by access: the win
// (15282, called at 11960), the loss (15588, called at 11921 and 12044) and the wrap-up (12062, called
// at 11984 and 12206 and inside the win, 15296, and the loss, 15592). Each names its return sites.
struct SnapshotRoutine {
    const char* name;
    std::uint32_t entry;
    std::array<std::uint32_t, 4> returns{};
};
inline constexpr std::array<SnapshotRoutine, 3> kSnapshotRoutines{{
    {"WinBattle", 0xC25D57, {0xC2488F}},
    {"LoseBattle", 0xC25FCA, {0xC24831, 0xC248F5}},
    {"_c24903", 0xC24903, {0xC248A4, 0xC24A28, 0xC25D74, 0xC25FD3}},
}};

// The cells an ending's snapshot holds: everything a battle's end reads or leaves — the ending's
// flags and the random-number position, the colosseum's wager and prize, the formation, background
// and field flags, the party (records, party bytes, gil, spells, skills, lores, rages and dances
// known), the monsters standing, the inventory, the equipment-changed flags with the message
// variables and the formation's bytes, each actor's battler with the records' offsets and the
// battlers' bits, the party's hands, Doom Gaze's battler, the party members' equipment bits with the
// battle's own cells, the Morph meter shown, each battler's HP, MP, maxima and relic effects, the
// monsters' experience and gold, the battle switches with the battle's number, the party slots,
// the morphed party member and the statuses, Morph's meter, and in the graphics program's half of
// work RAM the items obtained with their two counts.
inline constexpr std::array<TracedRange, 22> kEndingRanges{{
    {"ending's direct page", 0x7E00B0, 16},     {"wager and prize", 0x7E0205, 3},
    {"formation and field", 0x7E11E0, 6},       {"character records", 0x7E1600, 592},
    {"party bytes", 0x7E1850, 16},              {"gil", 0x7E1860, 3},
    {"spells known", 0x7E1A6E, 648},            {"skills known", 0x7E1CF6, 87},
    {"monster indexes", 0x7E2001, 12},          {"inventory", 0x7E2686, 1280},
    {"message variables", 0x7E2F30, 28},        {"battlers", 0x7E3000, 44},
    {"party's hands", 0x7E32F4, 8},             {"Doom Gaze", 0x7E33FA, 1},
    {"battle cells", 0x7E3A20, 128},            {"Morph meter shown", 0x7E3B04, 8},
    {"stats", 0x7E3BF4, 120},                   {"monster experience and gold", 0x7E3D8C, 32},
    {"switches to statuses", 0x7E3EB4, 88},     {"Morph's meter", 0x7E3F30, 2},
    {"items obtained", 0x7E602D, 80},           {"items obtained counts", 0x7E64DA, 2},
}};

// ShowMsg's call of the graphics program (battle/battle_main.asm:15613), where the message it shows
// is in its cell and the message variables are set: the trace logs each message here.
inline constexpr std::uint32_t kShowMessageCall = 0xC25FEA;

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
// The abilities a level teaches are worked out for the actor and level the caller read from the
// record just before the call (battle/battle_main.asm:15750-15752); at that routine's entry the trace
// logs the last actor byte and level byte read from any record:
//
//   f<frame> abilities actor <byte> level <byte>     or "abilities --" when none has been read
//
// The routines of kSnapshotRoutines log every range of kEndingRanges as it stands on entry and on
// return, and inside them, as inside any open call, each battle message as ShowMsg hands it to the
// graphics program:
//
//   f<frame> snapshot <routine> enter $<bank>:<address>
//   f<frame> snapshot LoseBattle handed <message, or -- for none>   only when its caller said
//   f<frame> snapshot <routine> $7E:<first cell> <the range's bytes, two hex digits each>
//   f<frame> snapshot <routine> return $<bank>:<address>
//   f<frame> message <message> <first> <second> <third> <fourth>   the four variables, six hex digits each
//
// A return is logged only while a call of its routine is open, so a site the game also passes on
// other paths — the command dispatch's, after every command — logs only the returns of the calls
// the trace follows.
//
// The context, a snapshot and a message are what the cells hold at that moment, on a running machine
// as on a parked one: the trace keeps the last byte the guest wrote to every cell it watches and lays
// those over what the machine answers. It watches kEndingRanges' cells too, beyond kTracedRanges and
// kPartyRecordRanges, logging none of their accesses. Declares on the machine when constructed: host
// the cartridge first, and construct it before the machine runs. `access`, when given, sees every
// access to a watched cell whether a call is open or not.
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

    // Close the open snapshot of `routine` at its first return site: for a caller that called it itself.
    void returnFromSnapshot(const SnapshotRoutine& routine);

    // The actor and level the next abilities line names: for a caller that hands them to the routine
    // itself rather than reading them from a record first.
    void readForAbilities(std::uint8_t actor, std::uint8_t level);

    // The message the next loss is handed, logged at its entry: for a caller that calls the loss
    // itself rather than from one of the cartridge's two calls of it (none for no message).
    void handOverLossMessage(std::optional<std::uint8_t> message);

    // Drop the bytes kept from the guest's writes and reads: for a caller that writes cells itself
    // through the machine's own write verb, which the watches do not see, on a parked machine.
    void forgetContextWrites();

    // A byte a caller wrote through the machine's own write verb on a running machine, which the
    // watches do not see: kept as the guest's own writes are, so a later read shows it.
    void noteWrite(std::uint32_t at, std::uint8_t value);

    // Every range of work RAM the trace watches, in address order: kTracedRanges,
    // kPartyRecordRanges and the parts of kEndingRanges neither covers. Another watch on the same
    // machine stays clear of them, and hears of their accesses through `access`.
    [[nodiscard]] static std::vector<std::pair<std::uint32_t, std::uint32_t>> watchedRanges();

private:
    struct Places;
    std::unique_ptr<Places> places_;
    retropp::Vm* vm_ = nullptr;
    Line line_;
    Frame frame_;
    int open_ = 0;
    // The calls of each routine entered and not yet returned from: a return site logs the return of
    // a routine it serves only while one of its calls is open.
    std::map<std::string, int, std::less<>> openCalls_;
    bool partyRecordsOpen_ = false;
    int openSnapshots_ = 0;
    std::map<std::string, int, std::less<>> openSnapshotCalls_;
    std::optional<std::optional<std::uint8_t>> lossMessage_;

    void logSnapshot(retropp::Vm& vm, const std::string& routine, const char* how, std::uint32_t at);
};

// A battle message as the trace logged it: the message, and its four variables' twelve bytes.
struct LoggedMessage {
    std::uint8_t message = 0;
    std::array<std::uint8_t, 12> variables{};
};

// One routine call as the trace logged it: the routine, the frame it ran on and the address it
// returned to; each cell's value when the routine first read it, for every cell it read before it
// wrote it; each cell's last value written; the attack's context at its entry; the attacker of the
// last attacker effect before it; for an attack's special effect on one target, the target, where
// the trace shows it; the actor and level the abilities a level teaches were worked out for; and each
// battle message shown inside it, in order.
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
    // The attacker of the params block at the entry of the last attacker effect before this call.
    std::optional<std::uint8_t> attackerEffectMaker;
    std::optional<battle::BattlerSlot> target;
    std::optional<std::uint8_t> abilitiesActor;
    std::optional<std::uint8_t> abilitiesLevel;
    std::vector<LoggedMessage> messages;
};

// One ending as the trace logged it: the routine, the frame it began on and the address it returned
// to, the message a loss was handed where its caller said, every cell of kEndingRanges as it stood on
// entry and as it was left, and each battle message shown between, in order.
struct TracedEnding {
    std::string routine;
    std::uint64_t frame = 0;
    std::uint32_t returnedTo = 0;
    std::optional<std::optional<std::uint8_t>> handedMessage;
    std::map<std::uint32_t, std::uint8_t> entry;
    std::map<std::uint32_t, std::uint8_t> left;
    std::vector<LoggedMessage> messages;
};

// Every ending in a trace that returned, in the order they returned — an ending inside another (the
// wrap-up inside the win) before the one around it. Throws std::runtime_error naming the line for a
// snapshot or message line that is not well formed.
[[nodiscard]] std::vector<TracedEnding> parseEndings(std::istream& log);

// Every call in a trace, in order. A call that never returns to one of its routine's return sites —
// one none of the callers kTracedRoutines names made — is left out, and a return with no call open
// is skipped. A call inside which another traced call opens — a script's SetStatus or ClearStatus,
// whose attack runs the damage routines — is left out too, and the calls inside it are kept. The
// attack frame and the bounce work out an attack's statuses on a target and then its special effect
// on the same target, so a DoTargetEffect call that follows a MagicStatusEffect call takes that
// call's target: the one battler whose cells the status call touched. Throws std::runtime_error
// naming the line for a line that starts like an event and is not one.
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
// statuses to set and to clear, whether a controller still holds it (a link that is ending holds no one)
// and its last-attacker mark, the
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

// --- a whole attack --------------------------------------------------------------------------------

// What a whole attack leaves (the attack frame, every attack of a command from loading the hand to
// the messages after it), as the same cells on both sides, each named and given as text: every cell
// the damage taken and shown, the statuses taking effect and a target's special effect are read back
// by; the battle script whole, one line per command, each animation with the params block it shows
// with; what the command's next attack starts from (its two flag bytes, field by field); the attacks
// left, the command cells the frame sets again after each attack, the reach flag and Interceptor's
// guard; and the random-number position.
struct AttackResult {
    std::map<std::string, std::string> cells;
};

// The scenario after the port's whole attack has run over it, and the script it wrote.
struct AttackRun {
    DamageScenario after;
    battle::BattleScriptBuilder script;
};

// The port's whole attack over the scenario by the scenario's attacker (battle::runAttack), worked
// out by the battle's own hooks (BattleHooks).
[[nodiscard]] AttackRun portAttack(DamageScenario scenario);

// The same aimed at the attacker itself (battle::runSelfAttack).
[[nodiscard]] AttackRun portSelfAttack(DamageScenario scenario);

// A port run read back as an AttackResult.
[[nodiscard]] AttackResult attackResult(const AttackRun& run);

[[nodiscard]] std::string differences(const AttackResult& port, const AttackResult& guest);

// The port's action `action` for the scenario's attacker (CommandExecutor::execute(), with the battle's
// own hooks): DamageOverTime, SpellWithoutCaster, StatusTimersExpired, SeizeDrain or an
// AiEffectAction. Read back as ownAction() reads the cartridge's.
[[nodiscard]] AttackResult portOwnAction(DamageScenario scenario, const battle::Action& action);

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

// --- what a won battle gives ---------------------------------------------------------------------

// One call the battle program made to the graphics program (ExecBtlGfx, $C2:6411), as the call
// found the cells the graphics program reads: the command, the party slot it is for ($7E:0010), the
// first five bytes of the battle script ($7E:2D6E — a message is script command 2, the message, and
// the script's end in the fifth), the item record an obtained item is handed over in ($7E:2E72) and
// the four message variables, three bytes each ($7E:2F35).
struct GraphicsCall {
    std::uint8_t command = 0;
    std::uint8_t slot = 0;
    std::array<std::uint8_t, 5> script{};
    std::array<std::uint8_t, 5> item{};
    std::array<std::uint8_t, 12> messageVariables{};
};

// The graphics command that plays the battle script, and the script command that shows a message.
inline constexpr std::uint8_t kPlayBattleScript = 0x04;
inline constexpr std::uint8_t kShowMessageScriptCommand = 0x02;

// A battle message as the player reads it: the message, and the numbers its text shows — its first
// from the first variable and its second from the second, each only where the text reads it, and
// as much of it as the text reads (a name or an item from the low byte, an amount from all three).
struct MessageShown {
    std::uint8_t message = 0;
    std::optional<std::uint32_t> first;
    std::optional<std::uint32_t> second;
    friend bool operator==(const MessageShown&, const MessageShown&) = default;
};

// The message `call` shows, read as its text reads its variables; nothing for a call that is not a
// message. Throws std::logic_error for a message whose variables this table does not name.
[[nodiscard]] std::optional<MessageShown> messageShown(const GraphicsCall& call);

// The message the trace logged, read the same way. Throws std::logic_error as messageShown() does.
[[nodiscard]] MessageShown messageShownBy(const LoggedMessage& logged);

// The party as the cartridge keeps it in its cells: game::Party, and the bits of those cells the
// port keeps no field for — each record's two-bit HP and MP boosts (the top of its maximum HP and MP
// words).
struct PartyCells {
    game::Party party;
    std::array<std::uint8_t, game::kCharacterRecords> hpBoostBits{};
    std::array<std::uint8_t, game::kCharacterRecords> mpBoostBits{};
};

// The win's flags, as the cells the win keeps them in: the curse dispelled, magic points earned, a
// Cat Hood counted, a technique and a blitz told of, and whether the party has magicite (battle
// switch 67, which the win copies before the first party member).
struct WinFlags {
    bool curseDispelled = false;
    bool magicPointsEarned = false;
    bool catHoodCounted = false;
    bool techniqueMessageShown = false;
    bool blitzMessageShown = false;
    bool partyHasMagicite = false;
    friend bool operator==(const WinFlags&, const WinFlags&) = default;
};

// One step of a win's rewards as the cartridge's routine for it reads them: the party and the battle
// switches; the record whose turn it is and the party slot it sits in (what "gained a level" names);
// the battle's magic points and the win's flags; the experience share; whether "gained a level" is
// still to be told for this member; the actor and level an ability check is made for; and the
// monster in each of the six monster slots, none for an empty one.
struct RewardScenario {
    PartyCells cells;
    game::BattleSwitches switches;
    std::size_t recordIndex = 0;
    std::uint8_t partySlot = 0;
    std::uint8_t magicPoints = 0;
    WinFlags flags;
    std::uint32_t share = 0;
    bool levelMessageToTell = true;
    CharacterPropId actor = CharacterPropId::TERRA;
    std::uint8_t level = 1;
    std::array<std::optional<MonsterId>, battle::kMonsterSlots> monsters{};
};

// What a step leaves, as the same fields on both sides: the party and switches read back, the win's
// flags, whether "gained a level" is still to be told, and the messages shown in order — with every
// other graphics call the cartridge made, which the port never makes.
struct RewardResult {
    PartyCells cells;
    game::BattleSwitches switches;
    WinFlags flags;
    bool levelMessageToTell = false;
    std::vector<MessageShown> messages;
    std::vector<GraphicsCall> otherGraphicsCalls;
};

// The port's side of each step over the same scenario: the share added to the record's experience
// (battle::addExperience); the spells the record's equipment teaches (battle::learnFromEquipment); the
// levels the record reaches, each with the abilities it teaches and the messages the win shows for
// them (battle::reachEarnedLevels); the abilities of the scenario's actor at the scenario's level, with
// their messages (battle::learnLevelAbilities); and the rages of the six monster slots
// (battle::learnRages).
[[nodiscard]] RewardResult portAddExperience(const RewardScenario& scenario);
[[nodiscard]] RewardResult portLearnFromEquipment(const RewardScenario& scenario);
[[nodiscard]] RewardResult portLevelUps(const RewardScenario& scenario);
[[nodiscard]] RewardResult portLearnAbilities(const RewardScenario& scenario);
[[nodiscard]] RewardResult portLearnRages(const RewardScenario& scenario);

// Every field where the two parted, one per line; empty when they agree. The party is compared cell
// by cell as the cartridge holds it, each cell named.
[[nodiscard]] std::string differences(const RewardResult& port, const RewardResult& guest);

// --- a battle's end ------------------------------------------------------------------------------

// A battle at its end as an ending reads it: the party (its game::Party is the battle's party) and
// the battle.
struct EndingScenario {
    PartyCells cells;
    battle::BattleState state;
};

// Something an ending shows, as both sides are compared: a party member's battle menu closing, a
// wait of so many frames, an item a party member put away, a battle message, the acting battler
// stepping back, or the party celebrating a win. On the cartridge a wait is that many calls to wait
// for a frame, a merge of the items obtained shows nothing, an item added and merged at once (a drop,
// the colosseum's prize) shows only its message, and the celebration is one call — read with whether
// the fanfare plays and whether battle event 1 does, as the graphics program's command decides them
// from the formation's flags and the field's music. The port's celebration is every menu closing,
// then the fanfare and the event as it asks for them.
struct EndingCall {
    enum class Kind : std::uint8_t { CloseMenu, Wait, ItemObtained, Message, StepBack, Celebration, Other };
    Kind kind = Kind::Other;
    std::uint8_t slot = 0;       // CloseMenu: the party slot
    int frames = 0;              // Wait
    ItemId item = ItemId::EMPTY; // ItemObtained
    MessageShown message;        // Message
    bool fanfare = false;        // Celebration: the fanfare starts
    bool victoryEvent = false;   // Celebration: battle event 1 plays
    std::uint8_t command = 0;    // Other: the graphics command, or 0 for a request of the port's own
    friend bool operator==(const EndingCall&, const EndingCall&) = default;
};

// What an ending leaves, as the same fields on both sides: the party read back, the battle switches,
// the inventory, the items obtained with both counts, what each party member holds in hand and who
// is marked as holding something, the Morph meter shown per party slot, whether battle menus may
// open, the random-number position, and the calls in order.
struct EndingResult {
    PartyCells cells;
    game::BattleSwitches switches;
    std::array<battle::BattleItem, game::kInventorySlots> inventory{};
    battle::ItemsObtained itemsObtained;
    std::array<std::optional<ItemId>, battle::kPartySlots> hands{};
    std::uint8_t holdingItem = 0;
    std::array<std::uint8_t, battle::kPartySlots> morphGauges{};
    bool menusDisabled = false;
    std::uint8_t rngPosition = 0;
    std::vector<EndingCall> calls;
};

// The port's side of each ending over the scenario: the wrap-up (battle::wrapUpBattle), the win
// (battle::playBattleEnd with BattleWon), and the loss with `message` (BattleLost, then the switches
// as battle::writeBack() hands them back).
[[nodiscard]] EndingResult portWrapUp(const EndingScenario& scenario);
[[nodiscard]] EndingResult portWin(const EndingScenario& scenario);
[[nodiscard]] EndingResult portLoss(const EndingScenario& scenario, std::optional<std::uint8_t> message);

// What the game keeps from a battle, as the cartridge writes it to the party's cells: the party
// (each record's statuses, HP and MP, and who is in the party), the inventory, the battle switches,
// and the battles Gau can find on the Veldt (a bit per battle below 512).
struct WriteBackResult {
    PartyCells cells;
    std::array<game::InventorySlot, game::kInventorySlots> inventory{};
    game::BattleSwitches switches;
    std::bitset<512> veldtBattles;
};

// The port's side: battle::writeBack() over the scenario, the Veldt's battles being the scenario's
// with the result's veldtFormation added.
[[nodiscard]] WriteBackResult portWriteBack(const EndingScenario& scenario);
[[nodiscard]] std::string differences(const WriteBackResult& port, const WriteBackResult& guest);

// Every field where the two parted, one per line; empty when they agree. An inventory slot or a place
// of the list whose item is empty on both sides agrees whatever else it holds.
[[nodiscard]] std::string differences(const EndingResult& port, const EndingResult& guest);

// The items obtained and the inventory, as the graphics program's two commands for them read them:
// the list with both counts, the inventory, and the item a command adds and how many — handed over as
// the battle works its record out (battle::battleItem) with no one in the party.
struct ItemsObtainedScenario {
    battle::ItemsObtained list;
    std::array<battle::BattleItem, game::kInventorySlots> inventory{};
    ItemId item = ItemId::EMPTY;
    std::uint8_t quantity = 0;
};

struct ItemsObtainedResult {
    battle::ItemsObtained list;
    std::array<battle::BattleItem, game::kInventorySlots> inventory{};
};

// The port's side: the scenario's item put in the list as battle::obtainItem() puts it, and the list
// merged (battle::mergeItemsObtained).
[[nodiscard]] ItemsObtainedResult portAddItemObtained(const ItemsObtainedScenario& scenario);
[[nodiscard]] ItemsObtainedResult portMergeItemsObtained(const ItemsObtainedScenario& scenario);

[[nodiscard]] std::string differences(const ItemsObtainedResult& port, const ItemsObtainedResult& guest);

// --- a monster's AI --------------------------------------------------------------------------------

// One battle as a monster's AI script reads it: the state, the battler whose script it is, and what
// the script's run holds so far — its targets, the targets it started with, and whether it is
// restricted.
//
// A scenario run as a turn or a counterattack reads every battler's script as the state holds it
// (Battler::turnScript, counterScript and the two resumes, readied with battle::readyAi()), and a
// counterattack answers `answering`.
struct AiScenario {
    battle::BattleState state;
    battle::BattlerSlot actor = battle::BattlerSlot::monster(0);
    battle::AiRun run;
    battle::BattlerSet answering;
};

// What finding a script's targets leaves, as the same fields on both sides: the battlers found,
// whether any were (the cartridge's carry), the battler's Targetting mark and the random-number
// position.
struct AiTargetsResult {
    battle::BattlerSet targets;
    bool any = false;
    std::optional<battle::BattlerSlot> targetingTarget;
    std::uint8_t rngPosition = 0;
};

// The port's targets for `target` over the scenario (battle::aiTargets), read back as an
// AiTargetsResult.
[[nodiscard]] AiTargetsResult portAiTargets(AiScenario scenario, AiTarget target, bool reachableOnly);

[[nodiscard]] std::string differences(const AiTargetsResult& port, const AiTargetsResult& guest);

// What testing a script's condition leaves, as the same fields on both sides: whether it holds, the
// run's targets and restriction, the battler's Targetting mark and the random-number position.
struct AiConditionResult {
    bool holds = false;
    battle::BattlerSet targets;
    bool restricted = false;
    std::optional<battle::BattlerSlot> targetingTarget;
    std::uint8_t rngPosition = 0;
};

// The port's test of `condition` over the scenario (battle::aiCondition), read back as an
// AiConditionResult.
[[nodiscard]] AiConditionResult portAiCondition(AiScenario scenario, const AiCondition& condition);

[[nodiscard]] std::string differences(const AiConditionResult& port, const AiConditionResult& guest);

// One battler's script as the cartridge keeps it: where its turn part and its counterattack part
// start, each counted in bytes from the start of the cartridge's script block ($FFFF for none), and
// for each part the blocks skipped and the place to go on from when it last stopped at a wait (the
// place 0 when it has never stopped at one).
struct AiScriptPlaces {
    std::uint16_t turn = 0;
    std::uint16_t counter = 0;
    std::uint8_t turnSkipped = 0;
    std::uint16_t turnWaitPlace = 0;
    std::uint8_t counterSkipped = 0;
    std::uint16_t counterWaitPlace = 0;
};

// The port's script for `slot` over the scenario, as the cartridge keeps it.
[[nodiscard]] AiScriptPlaces portScriptPlaces(const battle::BattleState& state, battle::BattlerSlot slot);

[[nodiscard]] std::string differences(const AiScriptPlaces& port, const AiScriptPlaces& guest);

// What a turn or a counterattack's run of a script leaves, as the same fields on both sides: the
// battler's actions and counterattacks as the cartridge's lists hold them (each "command attack
// targets MP", with "--" for the bytes the command never reads), its advance wait, its script's
// places, its Targetting mark, the three sets a counterattack's guard reads and changes, the
// counterattack line by battler and the random-number position.
struct AiRunResult {
    std::vector<std::string> actions;
    std::vector<std::string> counterActions;
    std::optional<std::uint8_t> advanceWait;
    AiScriptPlaces script;
    std::optional<battle::BattlerSlot> targetingTarget;
    battle::BattlerSet counterAiRanThisStep;
    battle::BattlerSet diedAwaitingCounter;
    battle::BattlerSet attackedThisStep;
    std::vector<int> counterQueue;
    std::uint8_t rngPosition = 0;
};

// One action as the cartridge's lists hold it, with "--" for the bytes its command never reads.
[[nodiscard]] std::string listedActionText(const battle::Action& action);

// The port's turn (battle::runAiTurn) and counterattack (battle::runAiCounter, with a counterattack
// running) over the scenario, read back as an AiRunResult. The commands are aimed by CommandExecutor.
[[nodiscard]] AiRunResult portAiTurn(AiScenario scenario);
[[nodiscard]] AiRunResult portAiCounter(AiScenario scenario);

// The port's turn for `actor` on `state`, which keeps what the run leaves — for turns run one after
// another.
[[nodiscard]] AiRunResult portAiTurn(battle::BattleState& state, battle::BattlerSlot actor);

[[nodiscard]] std::string differences(const AiRunResult& port, const AiRunResult& guest);

// What one of the three commands only a script queues leaves, as the same fields on both sides: the
// four battle variables, the twenty switch bytes, every battler's own variable, timer, ATB rate,
// statuses and statuses to set and to clear; the invincible and AI-untargetable sets, the battle's
// timer, the end-of-battle event, Gau recruited and the party membership of every character record;
// and the random-number position.
struct AiCommandResult {
    std::array<std::uint8_t, kAiBattleVariables> battleVariables{};
    std::array<std::uint8_t, game::kBattleVariables> switches{};
    std::array<std::uint8_t, battle::kBattlerSlots> ownVariables{};
    std::array<std::uint16_t, battle::kBattlerSlots> ownTimers{};
    std::array<std::uint16_t, battle::kBattlerSlots> atbRates{};
    std::array<StatusSet, battle::kBattlerSlots> statuses{};
    std::array<StatusSet, battle::kBattlerSlots> statusesToSet{};
    std::array<StatusSet, battle::kBattlerSlots> statusesToClear{};
    battle::BattlerSet invincible;
    battle::BattlerSet aiUntargetable;
    std::uint16_t battleCounter = 0;
    std::optional<battle::EndOfBattleEvent> endOfBattleEvent;
    bool gauRecruited = false;
    std::array<game::PartyMembership, game::kCharacterRecords> membership{};
    std::uint8_t rngPosition = 0;
    // The cells of the battle an attack leaves and the script, as AttackResult names them: unchanged
    // on both sides for a command that makes no attack, and what SetStatus's and ClearStatus's
    // attack leaves for those two. The attack's own cells end with the command and are not read.
    AttackResult attack;
};

// The port's command `action` (a BattleVariableAction, BattleSwitchAction or AiEffectAction) run by
// CommandExecutor::execute() for the scenario's actor, read back as an AiCommandResult.
[[nodiscard]] AiCommandResult portAiCommand(AiScenario scenario, const battle::Action& action);

[[nodiscard]] std::string differences(const AiCommandResult& port, const AiCommandResult& guest);

// One write a routine made to a watched cell, as the watch saw it.
struct WatchedWrite {
    std::uint32_t at = 0;  // the cell's work-RAM address, $7E:xxxx
    std::uint8_t value = 0;
};

// A cell a routine read before anything wrote it, and the byte the read found.
struct UnwrittenRead {
    std::uint32_t at = 0;  // the cell's work-RAM address, $7E:xxxx
    std::uint8_t value = 0;
};

// Cells the attack frame reads that no scenario writes, left as the machine has them: `count` cells
// from `at`, `stride` apart. Each is a cell whose value reaches nothing the game shows or keeps.
struct CellsLeftAsTheMachineHasThem {
    std::uint32_t at = 0;
    std::uint32_t count = 1;
    std::uint32_t stride = 1;
    const char* why = "";
};
inline constexpr std::array<CellsLeftAsTheMachineHasThem, 12> kCellsLeftAsTheMachineHasThem{{
    {.at = 0x7E01FE, .count = 2, .why = "the return landing the engine plants for the call, read by its last rts"},
    {.at = 0x7E01EE, .count = 8,
     .why = "the return landing the engine plants for a graphics command the oracle runs inside a call, read by "
            "the command's rtl"},
    {.at = 0x7E00F9,
     .why = "the byte above the experience a level needs, rotated with it by a 16-bit rol and never compared; the "
            "win zeroes it before it counts a drop there"},
    {.at = 0x7E2F41,
     .why = "the byte after the win's gold total, which the total's 16-bit increment of its third byte reads and puts "
            "back; six monsters' gold never carries out of the third byte"},
    {.at = 0x7E2E74,
     .why = "an empty item's targeting, which the item record leaves as it was and the list copies; a place whose "
            "item is empty is never read past its item"},
    {.at = 0x7E00EB,
     .why = "the top byte of the multiply's scratch, which every shift of it (3 to 6 steps) moves out unread"},
    {.at = 0x7E00F3,
     .why = "the byte after the damage-healed flag, loaded with it where only the flag's lowest bit is tested"},
    {.at = 0x7E2F34,
     .why = "the byte after the four equipment flags, which a 16-bit increment of the last one reads and puts back"},
    {.at = 0x7E2F44, .count = 2,
     .why = "the monsters drawn see-through, read and put back by Vanish's setting and clearing"},
    {.at = 0x7E32DF, .count = 10, .stride = 2,
     .why = "the byte before each last-attacker mark, loaded with it where only the mark's sign is tested"},
    {.at = 0x7E3A29, .count = 3,
     .why = "the last three bytes of a script command, which the commands queued with them do not read"},
    {.at = 0x7E3A81, .why = "the byte before the Golem's block, loaded with it where only the next byte's sign is tested"},
}};

// Whether `cell` is one of kCellsLeftAsTheMachineHasThem.
[[nodiscard]] bool leftAsTheMachineHasIt(std::uint32_t cell);

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
    // among the sixteen, the character's spells known at theirs, the battle's magic points, and the
    // party holding magicite so that a spell stored shows in the win's flags. The record must have an
    // esper.
    [[nodiscard]] SpellLearningResult learnFromEsper(const RecordScenario& scenario);

    // One level-up of the scenario's record (DoLevelUp), called as a won battle's end calls it: the
    // record written at its place with the boosts in the top of its maximum HP and MP words. The
    // record's level must be below 99.
    [[nodiscard]] LevelUpResult levelUp(const RecordScenario& scenario);

    // The steps of a win's rewards, each called as the win calls it, the party written into its cells
    // (writeParty()), the switches, the win's flags, the magic points, the scenario record's actor's
    // spells as the ones being learned and the message variables as the win leaves them:
    //
    // The share added to the scenario record's experience (AddExp): the record in X, the accumulator
    // narrow and the index registers wide.
    [[nodiscard]] RewardResult addExperience(const RewardScenario& scenario);

    // The spells the scenario record's equipment teaches (LearnItemMagic), the same way. The record's
    // actor must be one of the first twelve characters.
    [[nodiscard]] RewardResult learnFromEquipment(const RewardScenario& scenario);

    // The levels the scenario record reaches (CheckLevelUp), the same way, with "gained a level" still
    // to be told or not and the scenario's party slot in the second message variable.
    [[nodiscard]] RewardResult levelUps(const RewardScenario& scenario);

    // The abilities of the scenario's actor at the scenario's level (LearnAbilities): the actor in the
    // accumulator's low byte and the level in its high byte.
    [[nodiscard]] RewardResult learnAbilities(const RewardScenario& scenario);

    // The rages of the six monster slots (LearnRage), both register widths narrow.
    [[nodiscard]] RewardResult learnRages(const RewardScenario& scenario);

    // Every call the last of those made to the graphics program, in order. While one of them runs, a
    // call to the graphics program is answered here: the commands that wait on the screen or the sound
    // return at once, and the two that keep the items obtained ($05, which adds one to the list, and
    // $0A, which merges the list into the inventory) run as the cartridge holds them, each found
    // through the cartridge's own table of the commands.
    [[nodiscard]] const std::vector<GraphicsCall>& graphicsCalls() const { return graphicsCalls_; }

    // One call to the graphics program as the battle program makes it (ExecBtlGfx), the command in the
    // accumulator, from the cells as they stand, answered as above. Returns the calls it made.
    const std::vector<GraphicsCall>& graphicsCommand(std::uint8_t command);

    // The battle's end as every ending but two begins it (_c24903), from the scenario's cells, both
    // register widths narrow, with the graphics program answered as above.
    [[nodiscard]] EndingResult wrapUp(const EndingScenario& scenario);

    // The win, whole (WinBattle), the same way, the accumulator narrow.
    [[nodiscard]] EndingResult win(const EndingScenario& scenario);

    // The loss (LoseBattle), the message in the accumulator — $FF for none.
    [[nodiscard]] EndingResult loss(const EndingScenario& scenario, std::optional<std::uint8_t> message);

    // What the game keeps from the battle (UpdateSRAM), from the scenario's cells, both register widths
    // narrow.
    [[nodiscard]] WriteBackResult writeBack(const EndingScenario& scenario);

    // The scenario's item added to the list (graphics command $05), and the list merged into the
    // inventory ($0A), each through the battle program's call to the graphics program.
    [[nodiscard]] ItemsObtainedResult addItemObtained(const ItemsObtainedScenario& scenario);
    [[nodiscard]] ItemsObtainedResult mergeItemsObtained(const ItemsObtainedScenario& scenario);

    // The cartridge's title program laid over work RAM as a console just switched on lays it
    // (DecompCutsceneProg), and the last three places of the items obtained read back.
    [[nodiscard]] battle::ItemsObtainedTail itemsObtainedTailAtPowerOn();

    // The battlers `target` names for the scenario's actor (CheckAITarget), with or without the cut
    // to the battlers an attack can reach, called as a script's target step and its conditions call
    // it: the operand in the accumulator, the actor in Y, the cut asked for by a clear carry.
    [[nodiscard]] AiTargetsResult aiTargets(const AiScenario& scenario, AiTarget target, bool reachableOnly);

    // The scenario's actor's script testing `condition` (the condition's own routine, found through
    // the cartridge's table of them), called as the script's reader calls it: the condition's bytes
    // where the reader leaves them, the actor in Y and the carry clear. The condition must be one a
    // script can hold.
    [[nodiscard]] AiConditionResult aiCondition(const AiScenario& scenario, const AiCondition& condition);

    // `slot`'s script readied (InitAI), called as loading a monster calls it: the script's index in
    // the accumulator and the slot in Y, both wide. The slot's script cells start as the scenario's
    // state holds them, so what the routine leaves alone shows.
    [[nodiscard]] AiScriptPlaces readyAi(const AiScenario& scenario, battle::BattlerSlot slot);

    // The scenario's actor's turn (ExecMonsterAction), called as taking a turn calls it: the actor in
    // X, no targets and no counterattack running.
    [[nodiscard]] AiRunResult aiTurn(const AiScenario& scenario);

    // The scenario's actor's counterattack (ExecAIRetal), called as making a counterattack calls it:
    // the actor in X, the scenario's `answering` as the targets and a counterattack running.
    [[nodiscard]] AiRunResult aiCounter(const AiScenario& scenario);

    // A turn after the last aiTurn()/aiCounter(), on the cartridge's own state from then: only the
    // actor's lists are emptied first, so what the runs carry from one to the next (the places,
    // the counts, the marks, the random numbers) is the cartridge's alone.
    [[nodiscard]] AiRunResult nextAiTurn(battle::BattlerSlot actor);

    // The command `action` (a BattleVariableAction, BattleSwitchAction or AiEffectAction) for the
    // scenario's actor (Cmd_2e, Cmd_2f or Cmd_30), called as running a command calls it: the cells a
    // command starts with, the actor in Y and the action's three bytes where the command reads them.
    // Every cell the attack frame reads is written as attack() writes it, from a scenario whose
    // attack is the one a command starts with, so SetStatus's and ClearStatus's attack runs whole.
    [[nodiscard]] AiCommandResult aiCommand(const AiScenario& scenario, const battle::Action& action);

    // One of the battle's own actions for the scenario's attacker — damage over time (Cmd_22), a spell
    // with no caster (Cmd_26), status timers running out (Cmd_29), Seize's drain (Cmd_2d), or a
    // script's effect (Cmd_30, run as aiCommand() runs it) — called as running a command calls it:
    // every cell the attack frame reads written as attack() writes it, from the attack a command
    // starts with; the cells a command starts with; the enemies left, every battler's damage-over-time
    // action waiting, its hold by a status and its Poison multiplier; the actor in Y, and the action's
    // command, attack byte and targets where the command reads them. Read back as aiCommand() reads an
    // attack's cells, with every battler's damage-over-time action waiting and Poison multiplier.
    [[nodiscard]] AttackResult ownAction(const DamageScenario& scenario, const battle::Action& action);

    // Every attack of the scenario's attack (ExecAttack), made by the scenario's attacker and called
    // as a command's body calls it: every cell the frame reads written from the scenario, the
    // script's queue and params buffer empty, the attacker in X.
    [[nodiscard]] AttackResult attack(const DamageScenario& scenario);

    // The same aimed at the attacker itself (ExecSelfAttack).
    [[nodiscard]] AttackResult selfAttack(const DamageScenario& scenario);

    // Every cell of the battle's work RAM ($7E:0000-3FFF) the last call read before anything wrote
    // it — the oracle's own writes since the call before, or the guest's during the call — with the
    // byte its first such read found, in address order. Empty when every cell the routine read came
    // from the scenario or from the routine itself.
    [[nodiscard]] std::vector<UnwrittenRead> unwrittenReads() const;

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
    //
    // A battler's AI rebuilds as its scenario: the cells as the oracle writes a default one, then
    // every cell the routine read, read back as an AiScenario. A script readied is the slot whose
    // turn place the call wrote, and the script whose turn part starts there. A turn and a
    // counterattack are the battler whose script place the call read (a counterattack that ran
    // already reads only its battler's bit), and a counterattack answers the targets it read. The
    // actions compared are the ones the call added to the battler's lists. A command is the action a
    // script's step can hold whose bytes are the ones the call read, and the battler is each one the
    // cartridge's own command, given the same reads, makes the same writes for. SetStatus and
    // ClearStatus make an attack, which the trace follows through the attack's own calls.
    //
    // A step of a won battle rebuilds as its RewardScenario the same way: the record is the one whose
    // cells the call touched, the spells the ones the call's spell-list place names, the abilities
    // the actor and level logged at its entry, the rages the monsters it read. The messages the call
    // showed are compared with the port's.
    [[nodiscard]] std::string replay(const TracedCall& call);

    // Replay one ending the trace logged against the port: the cells it found are written, read back
    // as an EndingScenario and played out by the port's ending of the same name (a loss handed the
    // message logged where its caller said, otherwise the one its caller hands: Banon fell from his
    // own ending, "Annihilated" from the end check, or none in the colosseum); what the port leaves is compared
    // with the cells the ending left, and the messages it shows with the ones the ending showed, in
    // order. Returns every difference, one per line; empty when they agree. Throws
    // std::runtime_error for an ending it cannot rebuild — one with an equipment change pending —
    // naming why.
    [[nodiscard]] std::string replayEnding(const TracedEnding& ending);

    // The battle at its end as the cells now hold it, read the way writeEndingScenario() writes one.
    [[nodiscard]] EndingScenario readEndingScenario();

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
    void writeAttackCells(const DamageScenario& scenario);
    void writeCommandStart();
    [[nodiscard]] AttackResult readAttackResult(bool attackCells);
    [[nodiscard]] ApplyDamageResult readApplyCells();
    void registerGate();
    void noteGuestAccess(std::uint32_t cell, std::uint8_t value, bool write);
    void writeBytes(const retropp::MemoryRegion& place, std::span<const std::uint8_t> bytes);
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
    void writeParty(const PartyCells& cells);
    [[nodiscard]] PartyCells readParty();
    void writeRewardScenario(const RewardScenario& scenario);
    [[nodiscard]] RewardResult readRewardResult();
    [[nodiscard]] RewardResult callReward(const std::function<void()>& routine);
    [[nodiscard]] RewardScenario readRewardScenario();
    [[nodiscard]] std::string replayRewardCall(const TracedCall& call, const TracedRoutine& routine);
    void answerGraphics(const GraphicsCall& call);
    void writeItemsObtained(const battle::ItemsObtained& list);
    [[nodiscard]] battle::ItemsObtained readItemsObtained();
    void writeInventory(const std::array<battle::BattleItem, game::kInventorySlots>& inventory);
    [[nodiscard]] std::array<battle::BattleItem, game::kInventorySlots> readInventory();
    [[nodiscard]] ItemsObtainedResult callItemsObtained(const ItemsObtainedScenario& scenario, std::uint8_t command);
    void writeEndingScenario(const EndingScenario& scenario);
    [[nodiscard]] EndingResult readEndingResult();
    [[nodiscard]] EndingResult callEnding(const EndingScenario& scenario, const SnapshotRoutine& snapshot,
                                          const std::function<void()>& routine);
    void writeAiCells(const AiScenario& scenario);
    void writeAiScriptCells(const battle::BattleState& state);
    void writeAiRunCells(const AiScenario& scenario, bool counter);
    void emptyActionLists(battle::BattlerSlot actor);
    [[nodiscard]] AiScriptPlaces readScriptPlaces(battle::BattlerSlot slot);
    [[nodiscard]] AiScenario readAiScenario(battle::BattlerSlot actor);
    [[nodiscard]] std::string replayAiCall(const TracedCall& call, const TracedRoutine& routine);
    [[nodiscard]] AiRunResult readAiRunResult(battle::BattlerSlot actor);
    [[nodiscard]] AiCommandResult readAiCommandResult();
    [[nodiscard]] std::optional<battle::BattlerSlot> readTargetingTarget(battle::BattlerSlot actor);
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
    using TargetsCall = std::uint8_t(std::uint8_t, std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    using ConditionCall = std::uint8_t(std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    retropp::Routine<TargetsCall> checkAiTarget_;
    // The condition routines by the game's numbering, each where the cartridge's table points.
    std::vector<retropp::Routine<ConditionCall>> aiConditions_;
    using SetupCall = void(std::uint16_t, std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    retropp::Routine<SetupCall> initAi_;
    retropp::Routine<Call> execMonsterAction_;
    retropp::Routine<Call> execAiRetal_;
    // The three commands, by the command number less 0x2E.
    std::array<retropp::Routine<Call>, 3> aiCommands_;
    // Damage over time, a spell with no caster, status timers running out and Seize's drain.
    std::array<retropp::Routine<Call>, 4> ownActions_;
    retropp::Routine<Call> execSelfAttack_;
    retropp::Routine<Call> execAttack_;
    // A win's rewards, and the two graphics commands that keep the items obtained.
    retropp::Routine<Call> addExp_;
    retropp::Routine<Call> learnItemMagic_;
    retropp::Routine<Call> checkLevelUp_;
    using AbilitiesCall = void(std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    retropp::Routine<AbilitiesCall> learnAbilities_;
    retropp::Routine<Call> learnRage_;
    retropp::Routine<AbilitiesCall> execBtlGfx_;
    retropp::Routine<Call> addItemObtained_;
    retropp::Routine<Call> mergeItemsObtained_;
    // The battle's end, and the title program's laying out.
    retropp::Routine<Call> wrapUp_;
    retropp::Routine<Call> winBattle_;
    retropp::Routine<AbilitiesCall> loseBattle_;
    retropp::Routine<Call> updateSram_;
    retropp::Routine<Call> decompCutsceneProg_;
    std::vector<GraphicsCall> graphicsCalls_;
    // A graphics command the oracle has no answer for was called (the menu queue's position, $0D).
    std::optional<std::uint8_t> unansweredGraphics_;
    // The read-set gate: the cells written since the last call began (by the oracle before it, by the
    // guest during it), and the cells the guest read during the last call before any was written.
    std::set<std::uint32_t> gateWritten_;
    std::map<std::uint32_t, std::uint8_t> unwrittenReads_;
    bool gateCalling_ = false;
    bool gateFresh_ = true;
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

// The scenarios the reward sweeps draw, from the same seed. Each draws the whole party: the sixteen
// records with their two boosts, every character's spells (a percentage below 100, just learned or
// learned), the SwdTechs, Blitzes, lores, dances and rages known, the gil and the Morph supply; the
// battle switches; a record and the party slot it sits in; the battle's magic points; and every one
// of the win's flags. Then:
//
// - drawExperienceScenario: the record's experience anywhere to 15,000,000, often within a share of
//   the cap, and a share anywhere to 16,777,215, often small.
// - drawEquipmentScenario: one of the first twelve characters, each of the six slots empty or any
//   item, the Cursed Shield often, the shield's count often at 254 or 255, any magic points.
// - drawLevelScenario: any of the sixteen actors at a level from 1 to 99, its experience from just
//   under its next level to several levels past it, an esper or none, "gained a level" still to be
//   told or not.
// - drawRageScenario: each of the six monster slots empty, a monster numbered below 256 or one above.
[[nodiscard]] RewardScenario drawExperienceScenario(std::mt19937& rng);
[[nodiscard]] RewardScenario drawEquipmentScenario(std::mt19937& rng);
[[nodiscard]] RewardScenario drawLevelScenario(std::mt19937& rng);
[[nodiscard]] RewardScenario drawRageScenario(std::mt19937& rng);

// One case of the ability walk, which takes every actor 0-15 at every level 1-99 over three spell
// lists: `actor` at `level` with every spell below 100 % (`mix` 0), just learned (1) or learned (2),
// and the rest of the party, the SwdTechs and Blitzes known and the win's flags drawn from `rng`.
[[nodiscard]] RewardScenario abilityCase(CharacterPropId actor, std::uint8_t level, int mix, std::mt19937& rng);

// One line naming a drawn reward scenario's inputs, for a report.
[[nodiscard]] std::string describe(const RewardScenario& scenario);

// A battle at its end, for the wrap-up: up to four party members, each holding an item in hand or
// not and marked as holding one or not, someone morphed or not; an inventory with room, with items
// the hands hold near 99, or full; and the items obtained empty, part-full or near the end of their
// sixteen places, with both counts drawn so a merge often runs past place 12 into the last three.
[[nodiscard]] EndingScenario drawEndingScenario(std::mt19937& rng);

// A battle won or lost, for the win, the loss and the write-back: one to four party members with a mix
// of actors (Terra, Mog, Gau, Gogo, Umaro and the guests often), alive or fallen, Cat Hoods and Exp.
// Eggs, espers, experience near the next levels; one to six monsters fallen, fled or standing; on the
// Veldt or not; the colosseum now and then; lores learned; every background; the fanfare on and off;
// the wrap-up's own draws besides.
[[nodiscard]] EndingScenario drawWinScenario(std::mt19937& rng);
[[nodiscard]] std::string describe(const EndingScenario& scenario);

// The list and the inventory for the graphics program's two commands, drawn the same way, with an
// item and a quantity to add.
[[nodiscard]] ItemsObtainedScenario drawItemsObtainedScenario(std::mt19937& rng);

// A battle a script's targets are found in, the target asked for and whether the sides are cut to
// the reachable.
struct AiTargetsCase {
    AiScenario scenario;
    AiTarget target;
    bool reachableOnly = false;
};

// A battle a script's condition is tested in, and the condition.
struct AiConditionCase {
    AiScenario scenario;
    AiCondition condition;
};

// The battles the AI sweeps draw, from the same seed: an actor in any slot; for every battler
// whether it is on the field, the party member or monster in its slot (or none), its statuses
// (Dead, Reflect, Hide and the statuses from Dance on often among them), HP, MP and level often at
// the thresholds a condition asks about, weaknesses, the battler that last attacked it and whether
// it is waiting to answer, its Targetting mark, what it was last hit with and by whom, the battler
// that last damaged it, its own variable and timer, and whether it is held in Seize; the battle's
// reachable, counted, untargetable, away-from-the-field and alive sets, the reach flag the last
// command left, the party and monster counts, the timer, the four variables and the switches, the
// names listed, Gau, the formation and the random-number position; and the run's targets, starting
// targets and restriction.
[[nodiscard]] AiScenario drawAiScenario(std::mt19937& rng);

// A drawn battle with any of the thirty selectors or a party member, with or without the cut.
[[nodiscard]] AiTargetsCase drawAiTargetsCase(std::mt19937& rng);

// A drawn battle with any condition a script can hold, its operands drawn within the ranges a
// script's own can take and often at the battle's own values.
[[nodiscard]] AiConditionCase drawAiConditionCase(std::mt19937& rng);

// One line naming a drawn AI battle's inputs, for a report.
[[nodiscard]] std::string describe(const AiScenario& scenario);

// The ways a counterattack's guard can find the battler, each a different path through it.
enum class CounterGuard : std::uint8_t {
    AlreadyRan,           // its counterattack part has run this step
    DiedAfterAnAttack,    // it died this action, and another attacked it this step
    DiedUnattacked,       // it died this action with no one else attacking it
    QuickForAnother,      // Quick is running for another battler
    HeldByAStatus,        // it answers with Sleep, Confuse, Berserk, Stop or Frozen
    Charmed,
    Free,
    QuickForItself,
};
inline constexpr int kCounterGuards = 8;

// A drawn battle in which `actor` runs `script` (drawAiScenario()'s battle, the actor on the field,
// the script readied, its advance wait unset or short, no one away from the field unless the last
// command left them out of reach), for a counterattack with the guard on `guard`'s path and the
// battler it answers drawn.
[[nodiscard]] AiScenario drawAiRunScenario(std::mt19937& rng, std::uint16_t script, battle::BattlerSlot actor,
                                           CounterGuard guard = CounterGuard::Free);

// A drawn battle with one of the three commands only a script queues, every operation and every
// effect, its operands drawn within what a script can hold.
struct AiCommandCase {
    AiScenario scenario;
    battle::Action action;
};
[[nodiscard]] AiCommandCase drawAiCommandCase(std::mt19937& rng);

// The scenarios the whole-attack sweep draws, from the same seed: drawTargetEffectScenario()'s
// battle, then the attack as a command's body hands it to the frame — Fight three times in eight and
// Capture one, each with its two swings or an Offering's eight and every battler's hands (a party
// member's whole, its shield hand empty one time in two; a monster's as loading it leaves them), or
// else any other command with an aiming byte, its attack and the attack chosen, the targeting byte,
// the targets (none
// one time in four), the aiming rules, the name it is shown under, the MP it costs (above the
// attacker's one time in four of those it costs), the swirl, quiet misses, the attack hidden without
// targets, an exact aim, damage modification and re-aiming at random, the first pass, the attacks
// left with a Dragon Horn, the command's extra steps, a critical hit's chance and an automatic one,
// the sketcher; the attacker's Morph, Berserk, Imp and Mute, Air Anchor on it, Runic up on the
// battlers, the weapon's spell cast at random, a Launcher's hits to come, the battlers' facing in a
// side or pincer attack, Strago in the party with the lores known and learned (half the time free to
// learn one, and the attack one of the lores half of those), and every battler's vigor and magic power.
[[nodiscard]] DamageScenario drawAttackScenario(std::mt19937& rng);

// A drawn battle and one of the battle's own actions in it.
struct OwnActionCase {
    DamageScenario scenario;
    battle::Action action;
};

// The battles and actions the own-action sweep draws, from the same seed: drawAttackScenario()'s
// battle, then one of five actions with its operands in the game's own ranges — damage over time
// (Poison, Regen or Sap, for a battler holding the status three times in four, its Poison multiplier
// 0 to 14 by twos, held by a status one time in four, no enemies left one time in eight); a spell with
// no caster (Doom one time in three, with no enemies left one time in four of those, otherwise any of
// the 54 spells) at any battler; status timers running out for a battler (one to four of Stop,
// Reflect, Freeze and Sleep, each status held three times in four); Seize's drain by a monster holding
// a party member; and a script's SetStatus or ClearStatus. Every battler is held by a status and has
// a damage-over-time action waiting one time in four each, the battler a beat or a drain is for always
// waiting, and the battler an action acts on is on the field and reachable three times in four.
[[nodiscard]] OwnActionCase drawOwnActionCase(std::mt19937& rng);

// One line naming a drawn battle's inputs and its action, for a report.
[[nodiscard]] std::string describe(const OwnActionCase& drawn);

// drawAttackScenario()'s battle with the attack the battle's own actions make on their attacker:
// nothing shown three times in four, otherwise as Magic; sparse statuses, removed one time in two;
// undodgeable; the level and stat 0 or drawn; the swirl; the drain, restore, ignore-defense, unsplit
// and undead-inverting flags drawn; and no hand loaded.
[[nodiscard]] DamageScenario drawSelfAttackScenario(std::mt19937& rng);

}  // namespace ostinato::trace
