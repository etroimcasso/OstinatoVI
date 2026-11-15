// The cartridge a test asks beside the port, and how much of the cartridge a run of the tests asks.
//
// Test code. Many tests work a case out by hand, run the port on it, and also run the cartridge's
// own routine on it through the damage oracle (tools/damage-trace/src/oracle/damage_oracle.h), the
// port matching both. Others ask only the cartridge: drawn sweeps, replays of traces and recordings,
// the oracle's own checks. A run of the tests asks the cartridge one of two ways, chosen by the
// environment variable OSTINATO_CARTRIDGE_CHECKS:
//
//   unset or empty   every test runs, and every hand check asks the cartridge beside the port
//   brief            the hand checks run on the port alone, and every test that asks only the
//                    cartridge is skipped, giving kBriefSkipReason; nothing hosts the machine
//   anything else    the first test that asks fails, naming the variable and its value
//
// A developer's run leaves the variable unset. CI sets it to brief.
//
// Three entry points. withCartridge runs a hand check: its body takes a Cartridge, which holds a
// started oracle where the run asks everything and nothing where it is brief. A helper that runs the
// port and compares returns the port's answer either way:
//
//   AttackerEffectRun checked(const DamageScenario& s) {
//       AttackerEffectRun run = trace::runAttackerEffect(s);
//       withCartridge(image(), [&](const Cartridge& cartridge) {
//           if (!cartridge) {
//               return;
//           }
//           EXPECT_EQ(trace::differences(trace::attackerEffectResult(run),
//                                        cartridge.oracle().attackerEffect(s)), "");
//       });
//       return run;
//   }
//
// A test body that asks the cartridge itself takes the Cartridge and asks through a file-local
// function that returns std::optional, empty where there is nothing to ask; the hand check is made
// on the port always and on the cartridge's answer where there is one:
//
//   std::optional<int> cartridgeMagicDamage(const Cartridge& cartridge, int power, int stat, int level) {
//       return cartridge ? std::optional<int>{cartridge.oracle().magicDamage(power, stat, level)}
//                        : std::nullopt;
//   }
//
//   TEST_F(DamageFormulaTest, MagicDamage_FourTimesThePowerAtLevelOneWithNoStat) {
//       withCartridge(image(), [&](const Cartridge& cartridge) {
//           EXPECT_THREE_WAYS(battle::magicDamage(4, 0, 1), 16, cartridgeMagicDamage(cartridge, 4, 0, 1));
//       });
//   }
//
// A test that asks only the cartridge opens with OSTINATO_SKIP_WHEN_BRIEF() and runs its body with
// withStartedOracle, which hands it the oracle itself:
//
//   TEST_F(TargetEffectTest, EveryDrawnScenarioLeavesTheCellsAsTheCartridgeDoes) {
//       OSTINATO_SKIP_WHEN_BRIEF();
//       withStartedOracle(image(), [&](trace::DamageOracle& oracle) { ... });
//   }
//
// Gotchas. In a brief run withCartridge calls its body straight away, not from a tick of the run
// loop, and makes no machine: the port's half needs neither. withStartedOracle throws in a brief run,
// so the test that calls it must open with the skip. cartridgeChecks() reads the variable each time
// it is called; a misspelled value fails every test that asks rather than running everything.
#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "oracle/damage_oracle.h"

namespace ostinato::test {

// How much of the cartridge a run of the tests asks.
enum class CartridgeChecks { All, Brief };

// The variable that chooses it.
inline constexpr const char* kCartridgeChecksVariable = "OSTINATO_CARTRIDGE_CHECKS";

// Brief where OSTINATO_CARTRIDGE_CHECKS is "brief", All where it is unset or empty. Any other value
// throws std::runtime_error naming the variable and the value.
inline CartridgeChecks cartridgeChecks() {
    const char* value = std::getenv(kCartridgeChecksVariable);
    if (value == nullptr || *value == '\0') {
        return CartridgeChecks::All;
    }
    if (std::string_view{value} == "brief") {
        return CartridgeChecks::Brief;
    }
    throw std::runtime_error(std::string{kCartridgeChecksVariable} + " is \"" + value +
                             "\"; it is \"brief\" or unset");
}

// The reason every test left out of a brief run gives.
inline constexpr std::string_view kBriefSkipReason =
    "asks the cartridge on the engine's machine; a brief run leaves it out (OSTINATO_CARTRIDGE_CHECKS=brief)";

// The cartridge a test asks beside the port: a started oracle where the run asks everything, nothing
// where it is brief.
class Cartridge {
public:
    // Nothing to ask.
    Cartridge() = default;

    // `oracle`, started; it outlives the Cartridge.
    explicit Cartridge(trace::DamageOracle& oracle) : oracle_(&oracle) {}

    // Whether there is a cartridge to ask.
    explicit operator bool() const { return oracle_ != nullptr; }

    // The started oracle. Throws std::logic_error where there is none.
    trace::DamageOracle& oracle() const {
        if (oracle_ == nullptr) {
            throw std::logic_error("there is no cartridge to ask in a brief run");
        }
        return *oracle_;
    }

private:
    trace::DamageOracle* oracle_ = nullptr;
};

// `body(cartridge)`. Where the run asks everything: from a tick of the run loop, with an oracle
// started on `image`. Where it is brief: straight away, with nothing to ask and no machine made.
template <typename Body>
void withCartridge(std::span<const std::uint8_t> image, Body body) {
    if (cartridgeChecks() == CartridgeChecks::Brief) {
        body(Cartridge{});
        return;
    }
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image};
        oracle.start();
        body(Cartridge{oracle});
    });
}

// `body(oracle)` from a tick of the run loop, with an oracle started on `image`. For a test with
// nothing but the cartridge to check; throws std::logic_error in a brief run, where such a test has
// already been skipped.
template <typename Body>
void withStartedOracle(std::span<const std::uint8_t> image, Body body) {
    if (cartridgeChecks() == CartridgeChecks::Brief) {
        throw std::logic_error("a test that asks only the cartridge ran in a brief run without its skip");
    }
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image};
        oracle.start();
        body(oracle);
    });
}

}  // namespace ostinato::test

// The first statement of a test a brief run leaves out: skips it there, giving kBriefSkipReason.
#define OSTINATO_SKIP_WHEN_BRIEF()                                                                  \
    do {                                                                                            \
        if (::ostinato::test::cartridgeChecks() == ::ostinato::test::CartridgeChecks::Brief) {     \
            GTEST_SKIP() << ::ostinato::test::kBriefSkipReason;                                     \
        }                                                                                           \
    } while (0)
