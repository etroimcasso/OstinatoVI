// A trace read back: the calls and endings it logged and the messages shown inside them, parsed from
// the lines DamageTrace writes (trace_log.h).
#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "battle/battler_set.h"

#include "scenarios/ending/rewards.h"

namespace ostinato::trace {

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

// The message the trace logged, read the same way. Throws std::logic_error as messageShown() does.
[[nodiscard]] MessageShown messageShownBy(const LoggedMessage& logged);

}  // namespace ostinato::trace
