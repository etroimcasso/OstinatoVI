// A battle script as both sides of a whole battle show it: one line per command of the cartridge's
// queue ($7E:2D6E, four bytes a command), naming the command and every operand its handler in the
// graphics program reads, in the same words for the port's script and for the cartridge's.
//
//     const std::vector<std::string> ours = scriptShown(request.commands, text);    // the port's
//     const std::vector<std::string> theirs = scriptShown(readScript(bytes), text); // the cartridge's
//     // ours == theirs when the two scripts show the same thing.
//
// A damage numeral shown one at a time is one queue command each ($0B), so a port command that shows
// several in turn is a line for each. An animation's line carries the params block it plays with. A
// Jump's ready stance shows no targets: the cartridge's is aimed at the action pool's top slot's, which
// the port has no counterpart for (docs/Bugs.md). Every other stance shows its targets.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "battle/battle_script.h"
#include "data/text_corpus.h"

namespace ostinato::trace {

// One command of the cartridge's queue: its four bytes, and the port's command they read as. A command
// no kind of the port's shows — one the battle program never writes ($00, $04, $05, $07, $08, $0A,
// $16-$1A) or a name kind with no writer (3) — reads as none and is shown by its bytes.
struct ScriptCommandRead {
    std::array<std::uint8_t, 4> bytes{};
    std::optional<battle::BattleScriptCommand> command;
};

// Whether an animation's fourth byte, its `secondary`, is the attack's own. The battle program writes
// the byte ($B7, copied into the command by _c235d4 battle_main.asm:8562-8572) only for these, and
// every other animation carries the one the last of them left (ExecCmd :3096-3109 does not reset it):
// - a weapon's swing — Fight, Capture and Jump, the weapon's item plus one (_c2299f :6958-6960, from
//   FightAttack :3458 and ExecAttack :8032, and Cmd_16 :3869), rewritten inside the swing by the Atma
//   Weapon's length (:2275-2276) and the dice (:10685-10694). A desperation attack is shown as Magic
//   (DesperationAttack :3592-3593) and carries it;
// - a monster's special attack, its special animation (CalcAttackEffect :8189-8194), as its params'
//   monsterAttack;
// - a Dance, the background it changes to or $FF (Cmd_13 :3711-3724), which the port keeps as the
//   animation's newBackground;
// - a Sketch that hit, the monster sketched (TargetEffect_55 :9538-9542); one that missed leaves the
//   monster out of the targets hit (_c23b1b :9512-9518).
[[nodiscard]] bool fourthByteIsTheAttacks(const battle::AttackAnimation& animation);

// The fourth byte an animation shows: a Dance's background, or $FF where it changes none, and every
// other animation's `secondary`.
[[nodiscard]] std::uint8_t fourthByte(const battle::AttackAnimation& animation);

// The lines `commands` show, one per queue command. `text` gives a battle message's record, whose
// control codes say which of its two numbers it shows and how much of each.
[[nodiscard]] std::vector<std::string> scriptShown(std::span<const battle::BattleScriptCommand> commands,
                                                   const TextCorpus& text);
[[nodiscard]] std::vector<std::string> scriptShown(std::span<const ScriptCommandRead> commands,
                                                   const TextCorpus& text);

}  // namespace ostinato::trace
