// The packed special-attack byte (+31) of a monster-properties record
// (loaded to $322D by LoadRageProp, battle_main.asm:7506-7507). The monster
// special-attack setup decodes it (battle_main.asm:8195-8235): bit 7 makes
// the attack undodgeable, bit 6 makes it deal no damage (status-only), and
// the low 6 bits select the effect by band — below $20 the value IS the
// status id the attack inflicts, $20-$2F adds (value - $20) to the damage
// multiplier, $30/$31 drain HP/MP, and $32 upward removes reflect (any bits
// past $32 are dead at dispatch). Construction goes through the per-band
// builders below so every configured byte reads as its meaning. sizeof == 1
// keeps it byte-identical to the ROM byte.
#pragma once

#include <cstdint>

#include "ostinato/status_id.h"

namespace ostinato {

struct MonsterSpecialAttack {
    std::uint8_t packed = 0;

    // Bit 7: the special attack can't be dodged.
    constexpr bool cantDodge() const { return (packed & 0x80) != 0; }

    // Bit 6: the special attack deals no damage (status-only).
    constexpr bool noDamage() const { return (packed & 0x40) != 0; }

    // Low 6 bits: the effect-class byte the battle dispatch decodes by band.
    constexpr std::uint8_t effectClass() const { return packed & 0x3F; }

    // --- per-band builders (battle_main.asm:8225-8235) ---

    // Band $00-$1F: the attack inflicts a status; the class value is the
    // StatusId itself (every StatusId is below $20 by construction).
    static constexpr MonsterSpecialAttack inflictStatus(StatusId status) {
        return MonsterSpecialAttack{static_cast<std::uint8_t>(status)};
    }

    // Band $20-$2F: the attack adds `boost` (0..15, masked to the band) to
    // the damage multiplier; damageBoost(0) is a plain damaging special.
    static constexpr MonsterSpecialAttack damageBoost(std::uint8_t boost) {
        return MonsterSpecialAttack{
            static_cast<std::uint8_t>(0x20 | (boost & 0x0F))};
    }

    // Band $30/$31: the attack drains HP / MP.
    static constexpr MonsterSpecialAttack drainHp() {
        return MonsterSpecialAttack{0x30};
    }
    static constexpr MonsterSpecialAttack drainMp() {
        return MonsterSpecialAttack{0x31};
    }

    // Band $32+: the attack removes reflect. Class bits past $32 are dead at
    // dispatch but still ROM data — deadResidualBits (0..13, masked to the
    // band) carries them so the built value round-trips the exact byte.
    static constexpr MonsterSpecialAttack removeReflect(
            std::uint8_t deadResidualBits = 0) {
        return MonsterSpecialAttack{static_cast<std::uint8_t>(
            0x32 + (deadResidualBits <= 0x0D ? deadResidualBits : 0x0D))};
    }

    // --- modifier bits, chained onto a band builder ---

    constexpr MonsterSpecialAttack withCantDodge() const {
        return MonsterSpecialAttack{static_cast<std::uint8_t>(packed | 0x80)};
    }

    constexpr MonsterSpecialAttack withNoDamage() const {
        return MonsterSpecialAttack{static_cast<std::uint8_t>(packed | 0x40)};
    }
};

static_assert(sizeof(MonsterSpecialAttack) == 1,
              "MonsterSpecialAttack must be byte-identical to the ROM "
              "special-attack byte");
static_assert(
    MonsterSpecialAttack::inflictStatus(StatusId::CONDEMNED).packed == 0x08 &&
    MonsterSpecialAttack::damageBoost(0).packed == 0x20 &&
    MonsterSpecialAttack::damageBoost(13).packed == 0x2D &&
    MonsterSpecialAttack::drainHp().packed == 0x30 &&
    MonsterSpecialAttack::drainMp().packed == 0x31 &&
    MonsterSpecialAttack::removeReflect().packed == 0x32 &&
    MonsterSpecialAttack::removeReflect(13).withCantDodge().withNoDamage()
            .packed == 0xFF &&
    MonsterSpecialAttack::inflictStatus(StatusId::SLEEP).withNoDamage()
            .packed == 0x4F &&
    MonsterSpecialAttack{0xFF}.effectClass() == 0x3F &&
    MonsterSpecialAttack{0xFF}.cantDodge() &&
    MonsterSpecialAttack{0xFF}.noDamage() &&
    !MonsterSpecialAttack{}.cantDodge(),
    "MonsterSpecialAttack must round-trip the consumer's decode");

}  // namespace ostinato
