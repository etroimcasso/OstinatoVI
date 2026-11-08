#include "machine_state.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace ostinato::trace {
namespace {

constexpr std::string_view kMark = "OstinatoVI machine state 1\n";
constexpr std::string_view kCartridgeLine = "cartridge ";

struct RevisionName {
    GameVersion version;
    std::string_view name;
};

constexpr RevisionName kRevisionNames[] = {
    {.version = GameVersion::JP_1_0, .name = "JP_1_0"},
    {.version = GameVersion::US_1_0, .name = "US_1_0"},
    {.version = GameVersion::US_1_1, .name = "US_1_1"},
};

struct ButtonName {
    std::string_view name;
    bool retropp::snes::Buttons::*held;
};

constexpr ButtonName kButtonNames[] = {
    {.name = "A", .held = &retropp::snes::Buttons::a},
    {.name = "B", .held = &retropp::snes::Buttons::b},
    {.name = "X", .held = &retropp::snes::Buttons::x},
    {.name = "Y", .held = &retropp::snes::Buttons::y},
    {.name = "L", .held = &retropp::snes::Buttons::l},
    {.name = "R", .held = &retropp::snes::Buttons::r},
    {.name = "Start", .held = &retropp::snes::Buttons::start},
    {.name = "Select", .held = &retropp::snes::Buttons::select},
    {.name = "Up", .held = &retropp::snes::Buttons::up},
    {.name = "Down", .held = &retropp::snes::Buttons::down},
    {.name = "Left", .held = &retropp::snes::Buttons::left},
    {.name = "Right", .held = &retropp::snes::Buttons::right},
};

[[noreturn]] void refuseFile(const std::filesystem::path& file, const std::string& reason) {
    throw std::runtime_error(file.string() + " " + reason);
}

[[noreturn]] void refuseLine(int line, const std::string& reason) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + reason);
}

[[noreturn]] void refuseWord(std::string_view word, const std::string& reason) {
    throw std::runtime_error(std::string{word} + " " + reason);
}

// Work RAM, banks $7E and $7F, one byte to an entry.
constexpr retropp::MemoryRegion kWorkRam{.at = 0x7E0000, .size = 1, .count = 0x20000};
constexpr std::uint32_t kFirstWorkRamBank = 0x7E;
constexpr std::uint32_t kLastWorkRamBank = 0x7F;
constexpr std::size_t kBankDigits = 2;
constexpr std::size_t kAddressDigits = 7;  // BB:AAAA

// The whole word as a number in `base`.
bool parseNumber(std::string_view word, int base, std::integral auto& value) {
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value, base);
    return !word.empty() && error == std::errc{} && end == word.data() + word.size();
}

std::optional<retropp::snes::Buttons> parseButtons(std::string_view names) {
    retropp::snes::Buttons buttons;
    if (names == "-") {
        return buttons;
    }
    while (true) {
        const std::size_t plus = names.find('+');
        const std::string_view name = names.substr(0, plus);
        const auto* known = std::find_if(std::begin(kButtonNames), std::end(kButtonNames),
                                         [&](const ButtonName& button) { return button.name == name; });
        if (known == std::end(kButtonNames)) {
            return std::nullopt;
        }
        buttons.*(known->held) = true;
        if (plus == std::string_view::npos) {
            return buttons;
        }
        names.remove_prefix(plus + 1);
    }
}

}  // namespace

std::string_view cartridgeName(GameVersion cartridge) {
    const auto* known = std::find_if(std::begin(kRevisionNames), std::end(kRevisionNames),
                                     [&](const RevisionName& row) { return row.version == cartridge; });
    return known->name;
}

void writeMachineState(const std::filesystem::path& file, GameVersion cartridge,
                       std::span<const std::uint8_t> bytes) {
    std::ofstream out{file, std::ios::binary | std::ios::trunc};
    out << kMark << kCartridgeLine << cartridgeName(cartridge) << '\n';
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
        refuseFile(file, "could not be written");
    }
}

MachineState readMachineState(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        refuseFile(file, "could not be read");
    }
    const std::string whole{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const std::string_view contents{whole};
    if (!contents.starts_with(kMark)) {
        refuseFile(file, "is not a machine state");
    }
    std::string_view rest = contents.substr(kMark.size());
    const std::size_t lineEnd = rest.find('\n');
    if (!rest.starts_with(kCartridgeLine) || lineEnd == std::string_view::npos) {
        refuseFile(file, "does not say which cartridge it belongs to");
    }
    const std::string_view name = rest.substr(kCartridgeLine.size(), lineEnd - kCartridgeLine.size());
    const auto* known = std::find_if(std::begin(kRevisionNames), std::end(kRevisionNames),
                                     [&](const RevisionName& row) { return row.name == name; });
    if (known == std::end(kRevisionNames)) {
        refuseFile(file, "names a cartridge revision that does not exist: " + std::string{name});
    }
    rest.remove_prefix(lineEnd + 1);
    if (rest.empty()) {
        refuseFile(file, "holds no state after its two lines");
    }
    MachineState state{.cartridge = known->version};
    state.bytes.assign(reinterpret_cast<const std::uint8_t*>(rest.data()),
                       reinterpret_cast<const std::uint8_t*>(rest.data()) + rest.size());
    return state;
}

bool stateIsOf(const MachineState& state, GameVersion cartridge) {
    return state.cartridge == cartridge;
}

std::vector<InputStep> readInputScript(std::istream& script) {
    std::vector<InputStep> steps;
    std::string text;
    for (int line = 1; std::getline(script, text); ++line) {
        std::istringstream words{text.substr(0, text.find('#'))};
        std::string frameWord;
        if (!(words >> frameWord)) {
            continue;
        }
        std::string buttonsWord;
        std::string extra;
        if (!(words >> buttonsWord) || (words >> extra)) {
            refuseLine(line, "expected <frame> <buttons>: " + text);
        }
        std::uint64_t frame = 0;
        const auto [end, error] = std::from_chars(frameWord.data(), frameWord.data() + frameWord.size(), frame);
        if (error != std::errc{} || end != frameWord.data() + frameWord.size()) {
            refuseLine(line, "expected <frame> <buttons>: " + text);
        }
        if (!steps.empty() && frame <= steps.back().frame) {
            refuseLine(line, "frame " + frameWord + " does not come after frame " +
                                 std::to_string(steps.back().frame));
        }
        const std::optional<retropp::snes::Buttons> buttons = parseButtons(buttonsWord);
        if (!buttons) {
            refuseLine(line, "not a button: " + buttonsWord);
        }
        steps.push_back(InputStep{.frame = frame, .buttons = *buttons});
    }
    return steps;
}

retropp::snes::Buttons buttonsAt(std::span<const InputStep> script, std::uint64_t frame) {
    const auto past = std::upper_bound(script.begin(), script.end(), frame,
                                       [](std::uint64_t at, const InputStep& step) { return at < step.frame; });
    return past == script.begin() ? retropp::snes::Buttons{} : std::prev(past)->buttons;
}

CellWrite readCellWrite(std::string_view frame, std::string_view address, std::string_view bytes) {
    CellWrite write;
    if (!parseNumber(frame, 10, write.frame)) {
        refuseWord(frame, "is not a frame");
    }

    // `$BB:AAAA`, the bank one of work RAM's two.
    std::string_view rest = address;
    if (rest.starts_with('$')) {
        rest.remove_prefix(1);
    }
    std::uint32_t bank = 0;
    std::uint32_t offset = 0;
    if (rest.size() != kAddressDigits || rest[kBankDigits] != ':' || !parseNumber(rest.substr(0, kBankDigits), 16, bank) ||
        !parseNumber(rest.substr(kBankDigits + 1), 16, offset)) {
        refuseWord(address, "is not an address $BB:AAAA");
    }
    if (bank < kFirstWorkRamBank || bank > kLastWorkRamBank) {
        refuseWord(address, "is not in work RAM, $7E:0000-$7F:FFFF");
    }
    write.at = bank << 16 | offset;

    if (bytes.empty() || bytes.size() % 2 != 0) {
        refuseWord(bytes, "is not bytes: an even number of hex digits");
    }
    for (std::size_t digit = 0; digit < bytes.size(); digit += 2) {
        std::uint32_t value = 0;
        if (!parseNumber(bytes.substr(digit, 2), 16, value)) {
            refuseWord(bytes, "is not bytes: an even number of hex digits");
        }
        write.bytes.push_back(static_cast<std::uint8_t>(value));
    }
    if (write.at - kWorkRam.at + write.bytes.size() > kWorkRam.count) {
        refuseWord(bytes, "runs past $7F:FFFF");
    }
    return write;
}

WorkRamBytes::Map declareWorkRamBytes(retropp::Vm& machine) {
    return machine.registerRegions(retropp::regions(retropp::region(&WorkRamBytes::bytes, kWorkRam, "work RAM bytes")));
}

void writeCells(retropp::Vm& machine, const WorkRamBytes::Map& ram, std::span<const CellWrite> writes,
                std::uint64_t frame) {
    for (const CellWrite& write : writes) {
        if (write.frame != frame) {
            continue;
        }
        for (std::size_t i = 0; i < write.bytes.size(); ++i) {
            const std::array<std::uint8_t, 1> one{write.bytes[i]};
            machine.write(ram, &WorkRamBytes::bytes, one, static_cast<std::uint32_t>(write.at - kWorkRam.at + i));
        }
    }
}

}  // namespace ostinato::trace
