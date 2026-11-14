// A battle as the damage routines read it (DamageScenario), what the base damage, the per-target
// pass, the damage taken and shown and the reflected attack leave, the port's answer to each, the
// comparison of two answers, and the sweeps' draws of each scenario.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "ostinato/status_set.h"

namespace ostinato::trace {

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

}  // namespace ostinato::trace
