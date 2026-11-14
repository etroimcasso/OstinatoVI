// The trace of the cartridge's battle routines as the game runs them: the routines it follows, the
// ranges of work RAM it watches, and DamageTrace, which hangs escapes and watches on a machine and
// hands every event to a line callback. trace_parse.h reads the lines back.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace retropp {
class Vm;
}  // namespace retropp

namespace ostinato::trace {

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

// The cells the traced routines read and write, as ranges of work RAM: the play clock's frames; the
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
    {"play clock frames", 0x7E021E, 1}, {"direct page", 0x7E00A0, 32},       {"gil and steps", 0x7E1860, 9},
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

}  // namespace ostinato::trace
