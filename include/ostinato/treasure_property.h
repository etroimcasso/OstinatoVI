// One treasure chest/pot/point on a field map (TreasureProp, player.asm:779-843).
// A map's treasures are located through the per-map offset table in
// src/data/map_triggers.h; each 5-byte record places the chest, marks it with an
// obtained-bit, and carries a type-dependent payload. sizeof == 5 keeps the
// record byte-identical to the ROM.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ostinato/item_id.h"

namespace ostinato {

// The treasure switch word (TreasureProp +2..+3): the event-bit index that marks
// this chest obtained, plus the content-type discriminator in the high bits.
// player.asm:781-850 reads it as a 16-bit value (and #$01ff for the bit index,
// then the high-byte flags select gil / item / monster / empty). Stored as an
// alignment-1 byte pair so the enclosing record stays sizeof-locked.
struct TreasureSwitch {
    std::array<std::uint8_t, 2> bytes = {0, 0};

    // The two bytes as a 16-bit little-endian value.
    constexpr std::uint16_t raw() const {
        return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
    }

    // Bits 0-8: index into the $1e40 obtained-treasure bit array (and #$01ff;
    // the low 3 bits pick the bit, bits 3-8 the byte — player.asm:783-795).
    constexpr std::uint16_t eventBit() const { return raw() & 0x01FF; }
    // Bit 15: the contents are gil (player.asm:796-797, bpl = not gil).
    constexpr bool isGil() const { return (raw() & 0x8000) != 0; }
    // Bit 14: the contents are an item (player.asm:829-831).
    constexpr bool isItem() const { return (raw() & 0x4000) != 0; }
    // Bit 13: the contents are a monster-in-a-box (player.asm:839-841).
    constexpr bool isMonsterInABox() const { return (raw() & 0x2000) != 0; }
    // Bit 12: the chest is empty. The consumer's test branches to the next
    // instruction either way (player.asm:848-851, "this has no effect"); the bit
    // is preserved regardless.
    constexpr bool isEmpty() const { return (raw() & 0x1000) != 0; }
};

static_assert(sizeof(TreasureSwitch) == 2,
              "TreasureSwitch must be byte-identical to the 2-byte ROM word");
static_assert(alignof(TreasureSwitch) == 1,
              "TreasureSwitch must be alignment-1 to sit inside the packed "
              "TreasureProperty record at offset +2");

// One 5-byte treasure record. posX/posY place the chest on the map; the switch
// word gives the obtained-bit and content type; content is the type-dependent
// payload (interpret it with the matching accessor once the switch type is known).
struct TreasureProperty {
    std::uint8_t posX;       // +0
    std::uint8_t posY;       // +1
    TreasureSwitch trigger;  // +2..+3
    std::uint8_t content;    // +4

    // gil contents = content x 100 (player.asm:800-803).
    constexpr std::uint32_t gilAmount() const { return content * 100u; }
    // item contents = the item id passed to GiveItem (player.asm:832-834).
    constexpr ItemId item() const { return static_cast<ItemId>(content); }
    // monster-in-a-box contents = a formation LOW byte written to $0789
    // (player.asm:842-843). The formation id space is 16-bit (FormationId); a
    // chest carries only the low byte, so this is a raw byte, not a FormationId.
    constexpr std::uint8_t formationLowByte() const { return content; }
};

static_assert(sizeof(TreasureProperty) == 5,
              "TreasureProperty must be byte-identical to a 5-byte ROM record");
static_assert(alignof(TreasureProperty) == 1,
              "TreasureProperty must be alignment-1 to stay packed in the array");
static_assert(offsetof(TreasureProperty, posX) == 0);
static_assert(offsetof(TreasureProperty, posY) == 1);
static_assert(offsetof(TreasureProperty, trigger) == 2);
static_assert(offsetof(TreasureProperty, content) == 4);

}  // namespace ostinato
