// The battle switches: the numbered flags and small counters event scripts, monster AI and the
// battle itself share.
//
// Event scripts test and change them by number (`if_b_switch $4B`, `set_b_switch $43`), monster AI
// changes them by number, and a battle starts from the game's copy and hands its own copy back
// when it ends. Switch n is bit n % 8 of variable n / 8. Most switches mean whatever the scripts
// that use them mean; the ones the battle reads or writes are named below. Three of the variables
// hold numbers rather than switches: two keep Doom Gaze's HP between the battles he flees from, and
// one counts the battles fought with the cursed shield.
//
//     game::BattleSwitches switches;
//     switches.set(game::kMagicOnlySwitch);           // Fanatics' Tower: only magic works
//     switches.isSet(game::kPartyDefeatedSwitch);     // after a battle: the party lost
//     switches.doomGazeHp();
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ostinato::game {

inline constexpr std::size_t kBattleVariables = 20;

// The switches the battle reads or writes.
inline constexpr int kConditionalBattleSwitches = 48;  // 48-55: one per conditional battle, in order
inline constexpr int kMagicOnlySwitch = 56;            // only magic works here
inline constexpr int kPermanentMorphSwitch = 58;       // Terra stays morphed for the whole battle
inline constexpr int kClearedAtBattleStartSwitch = 63; // every battle clears it as it starts
inline constexpr int kPartyDefeatedSwitch = 64;        // the battle just fought was lost
inline constexpr int kTerraMagicSceneSwitch = 65;      // Terra's next spell in battle starts a scene
inline constexpr int kMorphAvailableSwitch = 66;       // Terra can Morph
inline constexpr int kRanOutOfTimeSwitch = 68;         // the story's time ran out during the battle; also Warp
inline constexpr int kTimerExpiredSwitch = 69;         // a story countdown reached zero during the battle
inline constexpr int kEatenByZoneEaterSwitch = 71;     // Zone Eater swallowed the party
inline constexpr int kGauObtainedSwitch = 73;          // Gau has joined the party at least once
inline constexpr int kShadowStaysSwitch = 75;          // Shadow will not walk off after a battle

struct BattleSwitches {
    std::array<std::uint8_t, kBattleVariables> variables{};

    constexpr bool isSet(int number) const {
        return (variables[static_cast<std::size_t>(number / 8)] & bit(number)) != 0;
    }

    constexpr void set(int number, bool on = true) {
        std::uint8_t& variable = variables[static_cast<std::size_t>(number / 8)];
        variable = static_cast<std::uint8_t>(on ? variable | bit(number) : variable & ~bit(number));
    }

    constexpr void clear(int number) { set(number, false); }

    // Doom Gaze's HP when he last fled, or 0 before the first time.
    constexpr std::uint16_t doomGazeHp() const {
        return static_cast<std::uint16_t>(variables[kDoomGazeHpLow] | (variables[kDoomGazeHpHigh] << 8));
    }

    constexpr void setDoomGazeHp(std::uint16_t hp) {
        variables[kDoomGazeHpLow] = static_cast<std::uint8_t>(hp & 0xFF);
        variables[kDoomGazeHpHigh] = static_cast<std::uint8_t>(hp >> 8);
    }

    friend constexpr bool operator==(const BattleSwitches&, const BattleSwitches&) = default;

private:
    static constexpr std::size_t kDoomGazeHpLow = 10;
    static constexpr std::size_t kDoomGazeHpHigh = 11;

    static constexpr std::uint8_t bit(int number) { return static_cast<std::uint8_t>(1u << (number % 8)); }
};

}  // namespace ostinato::game
