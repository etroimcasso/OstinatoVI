// parseTrace(), parseEndings() and messageShownBy(): a trace's lines read back as calls, endings and
// the messages shown.

#include "trace/trace_parse.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <istream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "scenarios/common/draws.h"
#include "scenarios/ending/rewards.h"
#include "scenarios/status/status_shared.h"
#include "trace/cells.h"
#include "trace/trace_log.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

std::vector<TracedCall> parseTrace(std::istream& log) {
    std::vector<TracedCall> calls;
    std::optional<TracedCall> open;
    std::set<std::uint32_t> written;
    // The attacker of the params block at the last attacker effect's entry.
    std::optional<std::uint8_t> attackerEffectMaker;
    std::string text;
    int number = 0;
    const auto fail = [&](const std::string& why) {
        throw std::runtime_error("trace line " + std::to_string(number) + ": " + why + ": " + text);
    };
    const auto hex = [&](const std::string& word, std::uint32_t most) {
        std::size_t used = 0;
        unsigned long value = 0;
        try {
            value = std::stoul(word, &used, 16);
        } catch (const std::exception&) {
            fail("not a hex number");
        }
        if (used != word.size() || value > most) {
            fail("not a hex number");
        }
        return static_cast<std::uint32_t>(value);
    };
    // "$7E:3BF4" or "$C2:0B83" as one address.
    const auto address = [&](const std::string& word) {
        if (word.size() != 8 || word[0] != '$' || word[3] != ':') {
            fail("not an address");
        }
        return hex(word.substr(1, 2), 0xFF) << 16 | hex(word.substr(4), 0xFFFF);
    };
    while (std::getline(log, text)) {
        ++number;
        if (text.size() < 2 || text[0] != 'f' || !std::isdigit(static_cast<unsigned char>(text[1]))) {
            continue;
        }
        std::istringstream in{text};
        std::string frameWord;
        std::string kind;
        in >> frameWord >> kind;
        std::uint64_t frame = 0;
        try {
            frame = std::stoull(frameWord.substr(1));
        } catch (const std::exception&) {
            fail("not a frame");
        }
        if (kind == "enter") {
            std::string name;
            std::string at;
            in >> name >> at;
            address(at);
            open = TracedCall{.routine = name, .frame = frame};
            open->attackerEffectMaker = attackerEffectMaker;
            if (name == kTracedRoutines[6].name && !calls.empty() && calls.back().routine == kTracedRoutines[4].name) {
                open->target = statusCallTarget(calls.back());
            }
            written.clear();
        } else if (kind == "params") {
            std::array<std::uint8_t, 16> params{};
            for (std::uint8_t& byte : params) {
                std::string word;
                in >> word;
                byte = static_cast<std::uint8_t>(hex(word, 0xFF));
            }
            std::string hitsWord, hits, rulesWord, rules, previousWord, previous;
            in >> hitsWord >> hits >> rulesWord >> rules >> previousWord >> previous;
            if (hitsWord != "hits" || rulesWord != "rules" || previousWord != "previous") {
                fail("not a params line");
            }
            if (open) {
                open->params = params;
                open->hitsToCome = static_cast<std::uint8_t>(hex(hits, 0xFF));
                open->aimingRules = static_cast<std::uint8_t>(hex(rules, 0xFF));
                if (previous != "--") {
                    open->previousAttacker = static_cast<std::uint8_t>(hex(previous, 0xFF));
                }
            }
        } else if (kind == "r" || kind == "w") {
            std::string at;
            std::string value;
            in >> at >> value;
            const std::uint32_t cell = address(at);
            const auto byte = static_cast<std::uint8_t>(hex(value, 0xFF));
            if (!open) {
                continue;
            }
            if (kind == "r") {
                if (!written.contains(cell) && !open->inputs.contains(cell)) {
                    open->inputs[cell] = byte;
                }
            } else {
                open->writes[cell] = byte;
                written.insert(cell);
            }
        } else if (kind == "return") {
            std::string name;
            std::string at;
            in >> name >> at;
            const std::uint32_t site = address(at);
            if (open && open->routine == name) {
                open->returnedTo = site;
                if (name == kTracedRoutines[5].name) {
                    attackerEffectMaker = open->params[kBlockAttacker];
                }
                calls.push_back(std::move(*open));
                open.reset();
            }
        } else if (kind == "abilities") {
            std::string actorWord;
            in >> actorWord;
            if (actorWord == "--") {
                continue;
            }
            std::string actor, levelWord, level;
            in >> actor >> levelWord >> level;
            if (actorWord != "actor" || levelWord != "level") {
                fail("not an abilities line");
            }
            if (open) {
                open->abilitiesActor = static_cast<std::uint8_t>(hex(actor, 0xFF));
                open->abilitiesLevel = static_cast<std::uint8_t>(hex(level, 0xFF));
            }
        } else if (kind == "message") {
            std::string message;
            in >> message;
            LoggedMessage logged{.message = static_cast<std::uint8_t>(hex(message, 0xFF))};
            for (std::size_t variable = 0; variable < 4; ++variable) {
                std::string word;
                in >> word;
                if (word.size() != 6) {
                    fail("not a message variable");
                }
                const std::uint32_t value = hex(word, 0xFFFFFF);
                for (std::size_t i = 0; i < 3; ++i) {
                    logged.variables[variable * 3 + i] = static_cast<std::uint8_t>(value >> (8 * i) & 0xFF);
                }
            }
            if (open) {
                open->messages.push_back(logged);
            }
        } else if (kind == "snapshot") {
            continue;
        } else {
            fail("not a trace event");
        }
    }
    return calls;
}

std::vector<TracedEnding> parseEndings(std::istream& log) {
    std::vector<TracedEnding> endings;
    // The endings entered and not yet returned from, innermost last, and which of their two
    // snapshots each range line fills.
    std::vector<std::pair<TracedEnding, bool>> open;
    std::string text;
    int number = 0;
    const auto fail = [&](const std::string& why) {
        throw std::runtime_error("trace line " + std::to_string(number) + ": " + why + ": " + text);
    };
    const auto hex = [&](const std::string& word, std::uint32_t most) {
        std::size_t used = 0;
        unsigned long value = 0;
        try {
            value = std::stoul(word, &used, 16);
        } catch (const std::exception&) {
            fail("not a hex number");
        }
        if (used != word.size() || value > most) {
            fail("not a hex number");
        }
        return static_cast<std::uint32_t>(value);
    };
    const auto address = [&](const std::string& word) {
        if (word.size() != 8 || word[0] != '$' || word[3] != ':') {
            fail("not an address");
        }
        return hex(word.substr(1, 2), 0xFF) << 16 | hex(word.substr(4), 0xFFFF);
    };
    while (std::getline(log, text)) {
        ++number;
        if (text.size() < 2 || text[0] != 'f' || !std::isdigit(static_cast<unsigned char>(text[1]))) {
            continue;
        }
        std::istringstream in{text};
        std::string frameWord;
        std::string kind;
        in >> frameWord >> kind;
        std::uint64_t frame = 0;
        try {
            frame = std::stoull(frameWord.substr(1));
        } catch (const std::exception&) {
            fail("not a frame");
        }
        if (kind == "message") {
            std::string message;
            in >> message;
            LoggedMessage logged{.message = static_cast<std::uint8_t>(hex(message, 0xFF))};
            for (std::size_t variable = 0; variable < 4; ++variable) {
                std::string word;
                in >> word;
                if (word.size() != 6) {
                    fail("not a message variable");
                }
                const std::uint32_t value = hex(word, 0xFFFFFF);
                for (std::size_t i = 0; i < 3; ++i) {
                    logged.variables[variable * 3 + i] = static_cast<std::uint8_t>(value >> (8 * i) & 0xFF);
                }
            }
            for (auto& [ending, returned] : open) {
                ending.messages.push_back(logged);
            }
            continue;
        }
        if (kind != "snapshot") {
            continue;
        }
        std::string name;
        std::string word;
        in >> name >> word;
        if (word == "enter") {
            std::string at;
            in >> at;
            address(at);
            open.emplace_back(TracedEnding{.routine = name, .frame = frame}, false);
            continue;
        }
        if (open.empty() || open.back().first.routine != name) {
            continue;
        }
        if (word == "return") {
            std::string at;
            in >> at;
            open.back().first.returnedTo = address(at);
            open.back().second = true;
            continue;
        }
        if (word == "handed") {
            std::string message;
            in >> message;
            open.back().first.handedMessage =
                message == "--" ? std::optional<std::uint8_t>{}
                                : std::optional<std::uint8_t>{static_cast<std::uint8_t>(hex(message, 0xFF))};
            continue;
        }
        const std::uint32_t first = address(word);
        std::string bytes;
        in >> bytes;
        if (bytes.size() % 2 != 0) {
            fail("not a range's bytes");
        }
        auto& [ending, returned] = open.back();
        std::map<std::uint32_t, std::uint8_t>& cells = returned ? ending.left : ending.entry;
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            cells[first + static_cast<std::uint32_t>(i / 2)] = static_cast<std::uint8_t>(hex(bytes.substr(i, 2), 0xFF));
        }
        // The return's last range closes the ending.
        const TracedRange& last = kEndingRanges.back();
        if (returned && first == last.at) {
            endings.push_back(std::move(ending));
            open.pop_back();
        }
    }
    return endings;
}

MessageShown messageShownBy(const LoggedMessage& logged) {
    GraphicsCall call{.command = kPlayBattleScript};
    call.script[0] = kShowMessageScriptCommand;
    call.script[1] = logged.message;
    call.messageVariables = logged.variables;
    return *messageShown(call);
}

}  // namespace ostinato::trace
