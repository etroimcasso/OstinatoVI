// Hand-written port-design (PLAN phase-1.B D7). Not parser-emitted.
//
// The four flag bytes of a 14-byte attack-properties record. No upstream
// symbol source exists for these bits — the meanings live only in the prose
// RAM map (original-src/notes/battle-ram.txt:212-243), cited per enum below —
// so these are hand-written like StatusSet's packing rule, with the contract
// doc carrying the per-bit citations. Byte offsets refer to the record layout
// in src/data/attack_properties.h.
#pragma once

#include <cstdint>

#include "ostinato/flag_set.h"

namespace ostinato {

// Record byte +2 — upstream-unnamed attack-nature byte ($11A2,
// battle-ram.txt:212-220). Descriptive port names.
enum class AttackTrait : std::uint8_t {
    PHYSICAL            = 0x01,  // bit 0: physical damage
    INSTANT_DEATH       = 0x02,  // bit 1: instant death spell
    RESURRECTION_TARGET = 0x04,  // bit 2: resurrection targetting
    INVERT_ON_UNDEAD    = 0x08,  // bit 3: invert damage to undead
    RANDOM_TARGET       = 0x10,  // bit 4: random target
    IGNORE_DEFENSE      = 0x20,  // bit 5: ignore target's defense
    NO_DAMAGE_SPLIT     = 0x40,  // bit 6: no damage split
    NO_CHARACTER_TARGET = 0x80,  // bit 7: can't target characters (esper
                                 //        attacks, tools, desperation attacks)
};

// Record byte +3 — the RAM map's own "Attack flags 1" ($11A3,
// battle-ram.txt:221-229).
enum class AttackFlag1 : std::uint8_t {
    USABLE_ON_FIELD     = 0x01,  // bit 0: useable on field
    IGNORE_REFLECT      = 0x02,  // bit 1: ignore reflect
    LEARNABLE_LORE      = 0x04,  // bit 2: can learn as lore
    ENABLE_RUNIC        = 0x08,  // bit 3: enable runic
    QUICK_WARP          = 0x10,  // bit 4: quick/warp flag ??? (upstream's own
                                 //        uncertainty note, preserved verbatim)
    RETARGET_IF_INVALID = 0x20,  // bit 5: re-target if target becomes invalid
    KILLS_ATTACKER      = 0x40,  // bit 6: attacker dies after attack
                                 //        (air anchor effect)
    AFFECT_MP           = 0x80,  // bit 7: affect mp
};

// Record byte +4 — the RAM map's own "Attack flags 2" ($11A4,
// battle-ram.txt:230-238).
enum class AttackFlag2 : std::uint8_t {
    RESTORE_HP_MP     = 0x01,  // bit 0: restore hp/mp
    DRAIN             = 0x02,  // bit 1: drain effect
    REMOVE_STATUS     = 0x04,  // bit 2: remove status
    TOGGLE_STATUS     = 0x08,  // bit 3: toggle status
    STAMINA_DEFENSE   = 0x10,  // bit 4: use stamina for defense ??? (upstream's
                               //        own uncertainty note, preserved verbatim)
    UNDODGEABLE       = 0x20,  // bit 5: can't dodge
    LEVEL_DIVISIBLE   = 0x40,  // bit 6: level divisible spell (evasion gives
                               //        the factor)
    FRACTIONAL_DAMAGE = 0x80,  // bit 7: damage is fraction of hp
};

// Record byte +7 — upstream-unnamed 2-bit byte ($11A7, battle-ram.txt:241-243).
// Descriptive port names.
enum class AttackMiscFlag : std::uint8_t {
    MISS_IF_STATUS_IMMUNE = 0x01,  // bit 0: automatically miss if target is
                                   //        immune to status
    SHOW_ATTACK_MESSAGE   = 0x02,  // bit 1: display battle message based on
                                   //        attack index (if attack hits)
};

using AttackTraitSet  = FlagSet<AttackTrait>;
using AttackFlags1    = FlagSet<AttackFlag1>;
using AttackFlags2    = FlagSet<AttackFlag2>;
using AttackMiscFlags = FlagSet<AttackMiscFlag>;

static_assert(sizeof(AttackTraitSet) == 1 && sizeof(AttackFlags1) == 1 &&
              sizeof(AttackFlags2) == 1 && sizeof(AttackMiscFlags) == 1,
              "attack flag sets must stay byte-identical to their ROM bytes");

}  // namespace ostinato
