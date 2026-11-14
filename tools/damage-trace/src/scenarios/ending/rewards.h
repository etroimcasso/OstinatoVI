// What a won battle gives: the graphics program's calls and the messages they show, the party as
// the cartridge keeps it, the win's flags, each reward step's scenario and result, the port's side
// of each step, the comparison and the reward sweeps' draws.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "battle/battler_set.h"
#include "battle/rewards.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/monster_id.h"

namespace ostinato::trace {

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

}  // namespace ostinato::trace
