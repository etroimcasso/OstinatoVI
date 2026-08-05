// The 16-bit equippable-characters mask of an item-properties record (record
// bytes +1..+2, little-endian). Bits 0-13 are the fourteen playable character
// slots in CHAR order — the equip menus test the selected actor's
// CharEquipMaskTbl entry (1 << actor) against this word
// (src/menu/equip.asm:2287-2317), and the item menu walks the bits with an lsr
// loop in the same order (src/menu/item.asm:1358-1369). The top two bits carry
// special roles: bit 14 marks imp gear (battle_main.asm:2535-2541 reads it via
// the asl2 carry to keep the item's power while the wearer is an imp), and
// bit 15 marks heavy gear — GetCharEquipMask ORs $8000 into the actor's mask
// when the Merit Award relic bit ($11D8 bit 5) is set, so heavy items are
// equippable only through that relic. Because CharEquipMaskTbl has all sixteen
// entries, guest actors $0E/$0F test bits 14/15 as their own slots; canEquip
// reproduces that faithfully by testing 1 << id for any CharacterId.
//
// Storage is the ROM's little-endian byte pair (not a std::uint16_t member):
// the mask sits at record offset +1, and a 2-byte-aligned member there would
// force layout padding that breaks the 30-byte record. bits() composes the
// word.
#pragma once

#include <concepts>
#include <cstdint>

#include "ostinato/character_id.h"

namespace ostinato {

// The two special roles of the mask's top bits.
enum class EquipSpecial : std::uint16_t {
    IMP   = 0x4000,  // bit 14: imp gear — works while the wearer is an imp
    HEAVY = 0x8000,  // bit 15: heavy gear — equippable only via Merit Award
                     //         ($11D8 bit 5 ORs this bit into the actor mask)
};

struct EquipPermissions {
    std::uint8_t lo = 0;  // record byte +1 — character slots 0-7
    std::uint8_t hi = 0;  // record byte +2 — slots 8-13 + IMP/HEAVY bits

    constexpr std::uint16_t bits() const {
        return static_cast<std::uint16_t>(lo | (hi << 8));
    }

    // The equip-menu test: 1 << actor against the mask, matching
    // CharEquipMaskTbl's 1-bit-per-actor entries.
    constexpr bool canEquip(CharacterId id) const {
        return (bits() & static_cast<std::uint16_t>(
                             1u << static_cast<std::uint8_t>(id))) != 0;
    }

    constexpr bool impGear() const {
        return (bits() & static_cast<std::uint16_t>(EquipSpecial::IMP)) != 0;
    }

    constexpr bool heavyGear() const {
        return (bits() & static_cast<std::uint16_t>(EquipSpecial::HEAVY)) != 0;
    }

    // OR-together builder over character slots and the special top bits:
    // EquipPermissions::of(CharacterId::EDGAR, CharacterId::MOG,
    // EquipSpecial::HEAVY). Zero arguments yields the empty mask (an item no
    // character can equip).
    template <typename... Bits>
        requires((std::same_as<Bits, CharacterId> ||
                  std::same_as<Bits, EquipSpecial>) &&
                 ...)
    static constexpr EquipPermissions of(Bits... parts) {
        std::uint16_t mask = 0;
        (
            [&mask](auto part) {
                if constexpr (std::same_as<decltype(part), CharacterId>) {
                    mask |= static_cast<std::uint16_t>(
                        1u << static_cast<std::uint8_t>(part));
                } else {
                    mask |= static_cast<std::uint16_t>(part);
                }
            }(parts),
            ...);
        return EquipPermissions{static_cast<std::uint8_t>(mask & 0xFF),
                                static_cast<std::uint8_t>(mask >> 8)};
    }
};

static_assert(sizeof(EquipPermissions) == 2,
              "EquipPermissions must be byte-identical to the ROM's 16-bit "
              "equippable-characters mask");
static_assert(alignof(EquipPermissions) == 1,
              "EquipPermissions must stay 1-byte-aligned so the record layout "
              "carries it at offset +1 without padding");
static_assert(EquipPermissions::of(CharacterId::TERRA,
                                   EquipSpecial::HEAVY).bits() == 0x8001,
              "EquipPermissions::of must compose slots and special bits");

}  // namespace ostinato
