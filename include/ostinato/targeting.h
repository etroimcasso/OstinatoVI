// The targeting byte of an attack, an item or a battle command: who it may be aimed at and how
// the cursor starts.
//
// The byte is TargetFlags bits plus a two-bit initial-selection field (single, all, group or
// half). Two whole-byte values mean something on their own: SELF ($02) aims at the user and
// nothing else, and MENU ($FF) on a command means "use the targeting of the attack it casts".
//
//     Targeting t = Targeting::of(TargetFlags::MANUAL, TargetFlags::ENEMY, TargetFlags::INIT_ALL);
//     t.enemyByDefault();      // true
//     t.initialSelection();    // InitialSelection::All
//     t.keepsEveryTarget();    // true: an initial selection other than one target keeps them all
//
// sizeof == 1 keeps it byte-identical to the targeting byte of the records that carry it.
#pragma once

#include <concepts>
#include <cstdint>

#include "ostinato/target_flags.h"

namespace ostinato {

// Whom the cursor starts on.
enum class InitialSelection : std::uint8_t {
    Single,  // one target
    All,     // every target on the side
    Group,
    Half,
};

struct Targeting {
    std::uint8_t bits = 0;

    constexpr Targeting() = default;
    explicit constexpr Targeting(std::uint8_t raw) : bits(raw) {}

    // OR-together builder over TargetFlags. INIT_HALF is INIT_ALL | INIT_GROUP, and MENU is the
    // whole byte $FF.
    static constexpr Targeting of(std::same_as<TargetFlags> auto... flags) {
        Targeting result{};
        ((result.bits |= static_cast<std::uint8_t>(flags)), ...);
        return result;
    }

    // Aims at the user and nothing else: the byte is SELF and no other bit.
    constexpr bool isSelfOnly() const { return bits == static_cast<std::uint8_t>(TargetFlags::SELF); }

    // A command's byte that defers to the targeting of the attack it casts.
    constexpr bool usesAttackDefault() const { return bits == static_cast<std::uint8_t>(TargetFlags::MENU); }

    constexpr InitialSelection initialSelection() const {
        return static_cast<InitialSelection>((bits & static_cast<std::uint8_t>(TargetFlags::INIT_MASK)) >> 2);
    }

    // The player may move the cursor.
    constexpr bool manual() const { return has(TargetFlags::MANUAL); }

    // The cursor stays on one side.
    constexpr bool oneSideOnly() const { return has(TargetFlags::ONE_SIDE); }

    // The choice is made without the player confirming it.
    constexpr bool autoConfirm() const { return has(TargetFlags::AUTO_CONFIRM); }

    // The player may widen the cursor to the whole side.
    constexpr bool multiTarget() const { return has(TargetFlags::MULTI_TARGET); }

    // The cursor starts on the other side.
    constexpr bool enemyByDefault() const { return has(TargetFlags::ENEMY); }

    // The target is chosen by the roulette cursor.
    constexpr bool roulette() const { return has(TargetFlags::ROULETTE); }

    // Every chosen target stays chosen: the attack may hit more than one, or starts on more
    // than one.
    constexpr bool keepsEveryTarget() const {
        return multiTarget() || initialSelection() != InitialSelection::Single;
    }

private:
    constexpr bool has(TargetFlags flag) const { return (bits & static_cast<std::uint8_t>(flag)) != 0; }
};

static_assert(sizeof(Targeting) == 1, "Targeting must be byte-identical to the ROM targeting byte");

}  // namespace ostinato
