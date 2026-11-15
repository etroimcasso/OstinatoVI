// What the effect tests need of the machine they run on: a machine that has not been given a
// cartridge fails, naming what it lacks. The attacker-effect routine on the oracle, its sweep and the
// trace's replay of both effect routines are in tests/oracle/test_effects_oracle.cpp.

#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

// A machine that has not been given a cartridge fails, naming the variable that names one.
TEST(EffectsCartridge, AMissingCartridgeFailsNamingTheVariable) {
    constexpr const char* kVariable = "FF6_VANILLA_ROM";
    const char* was = std::getenv(kVariable);
    const std::optional<std::string> saved = was != nullptr ? std::optional<std::string>{was} : std::nullopt;
#ifdef _WIN32
    _putenv_s(kVariable, "");
#else
    unsetenv(kVariable);
#endif
    std::string message;
    try {
        (void)vanillaRom();
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
#ifdef _WIN32
    _putenv_s(kVariable, saved ? saved->c_str() : "");
#else
    if (saved) {
        setenv(kVariable, saved->c_str(), 1);
    }
#endif
    EXPECT_NE(message.find(kVariable), std::string::npos) << message;
}

}  // namespace
}  // namespace ostinato::test
