// Formations: the 576-record battle_monsters table (who is in a battle and
// where) and its parallel battle_prop aux table (how the battle begins), plus
// the 16-entry conditional-battle substitution table. The row data is
// generated (src/data/generated/formation_data.inc, formation_aux_data.inc,
// cond_battle_data.inc); this header owns the record types, their builders,
// the entry types, and the accessors.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "ostinato/battle_song.h"
#include "ostinato/formation_id.h"
#include "ostinato/formation_ref.h"
#include "ostinato/monster_entrance_type.h"
#include "ostinato/monster_id.h"

namespace ostinato {

// One 15-byte formation record (battle_monsters.dat, ROM CF/6200). The battle
// graphics loader mon_data_get (btlgfx_main.asm:1990-2028) and InitMonsters
// (battle_main.asm:7672) name every field; the object representation is
// byte-identical to a ROM record (sizeof/offsetof asserts below + the
// full-corpus memcmp test). A slot's monster id is split across a low byte
// (bytes 2-7) and one bit of the high byte (byte 14); the empty-slot sentinel
// is $1FF. Read a formation through the accessors; write generated rows through
// of() so each slot names its monster instead of a split byte pair.
struct Formation {
    // byte 0: high nibble = VRAM map index (0-12); low nibble = low 4 bits of
    // the 6-bit "bg1 monsters" mask (zero corpus-wide — "not set for any
    // monsters" upstream).
    std::uint8_t vramMapAndBg1;
    // byte 1: bits 0-5 = present mask (slot i present iff bit i); bits 6-7 =
    // high 2 bits of the bg1 mask.
    std::uint8_t presentAndBg1;
    // bytes 2-7: low byte of each slot's monster id.
    std::array<std::uint8_t, 6> monsterIdLow;
    // bytes 8-13: packed position, high nibble X and low nibble Y, each in
    // 8-pixel units.
    std::array<std::uint8_t, 6> position;
    // byte 14: bit i = bit 8 of slot i's monster id; an empty slot reads $1FF.
    std::uint8_t monsterIdHigh;

    // The $1FF value a slot's reassembled id takes when the slot is unused.
    static constexpr std::uint16_t kEmptySlot = 0x1FF;

    constexpr std::uint8_t vramMap() const {
        return static_cast<std::uint8_t>(vramMapAndBg1 >> 4);
    }
    // The 6-bit bg1 mask, reassembled from byte 0 (low 4 bits) and byte 1
    // (high 2 bits). Zero corpus-wide; exposed for completeness.
    constexpr std::uint8_t bg1Mask() const {
        return static_cast<std::uint8_t>((vramMapAndBg1 & 0x0F) |
                                         ((presentAndBg1 & 0xC0) >> 2));
    }
    // Whether slot i is in the initial present mask. A slot can hold a monster
    // (id != $1FF) yet be absent here: those are reinforcements that arrive
    // mid-battle.
    constexpr bool isPresent(std::size_t slot) const {
        return ((presentAndBg1 >> slot) & 1) != 0;
    }
    // The reassembled 9-bit slot id ($1FF when empty).
    constexpr std::uint16_t rawMonsterId(std::size_t slot) const {
        return static_cast<std::uint16_t>(
            (((monsterIdHigh >> slot) & 1) << 8) | monsterIdLow[slot]);
    }
    constexpr bool slotEmpty(std::size_t slot) const {
        return rawMonsterId(slot) == kEmptySlot;
    }
    // The monster in a slot; only meaningful when !slotEmpty(slot).
    constexpr MonsterId monsterId(std::size_t slot) const {
        return static_cast<MonsterId>(rawMonsterId(slot));
    }
    // Slot position in pixels (the packed nibble times 8).
    constexpr std::uint8_t positionX(std::size_t slot) const {
        return static_cast<std::uint8_t>((position[slot] >> 4) * 8);
    }
    constexpr std::uint8_t positionY(std::size_t slot) const {
        return static_cast<std::uint8_t>((position[slot] & 0x0F) * 8);
    }

    // One slot for the builder. An empty slot is a default-constructed Slot
    // (monster == nullopt); a filled slot names its monster and 8-pixel-unit
    // position, and sets present unless it is a reinforcement.
    struct Slot {
        std::optional<MonsterId> monster;
        std::uint8_t x = 0;       // 0-15, in 8-pixel units
        std::uint8_t y = 0;       // 0-15, in 8-pixel units
        bool present = false;
    };
    struct Fields {
        std::uint8_t vramMap = 0;         // 0-12
        std::array<Slot, 6> slots{};
    };
    // Build a record from named slots. Packs each slot's id back into the
    // split low/high bytes, the position nibbles, and the present mask; the
    // bg1 mask is always zero (its corpus-wide value). Byte-identical to ROM.
    static constexpr Formation of(const Fields& f) {
        Formation r{};
        r.vramMapAndBg1 = static_cast<std::uint8_t>((f.vramMap & 0x0F) << 4);
        r.presentAndBg1 = 0;
        r.monsterIdLow = {};
        r.position = {};
        r.monsterIdHigh = 0;
        for (std::size_t i = 0; i < 6; ++i) {
            const Slot& s = f.slots[i];
            const std::uint16_t id =
                s.monster ? static_cast<std::uint16_t>(*s.monster) : kEmptySlot;
            r.monsterIdLow[i] = static_cast<std::uint8_t>(id & 0xFF);
            r.monsterIdHigh = static_cast<std::uint8_t>(
                r.monsterIdHigh | (((id >> 8) & 1) << i));
            r.position[i] =
                static_cast<std::uint8_t>(((s.x & 0x0F) << 4) | (s.y & 0x0F));
            if (s.present) {
                r.presentAndBg1 =
                    static_cast<std::uint8_t>(r.presentAndBg1 | (1 << i));
            }
        }
        return r;
    }
};

static_assert(sizeof(Formation) == 15,
              "Formation must be byte-identical to a 15-byte battle_monsters "
              "record");
static_assert(offsetof(Formation, vramMapAndBg1) == 0);
static_assert(offsetof(Formation, presentAndBg1) == 1);
static_assert(offsetof(Formation, monsterIdLow) == 2);
static_assert(offsetof(Formation, position) == 8);
static_assert(offsetof(Formation, monsterIdHigh) == 14);

// One 4-byte formation aux record (battle_prop.dat, ROM CF/5900): how a battle
// begins. LoadBattleProp (battle_main.asm:7940) reads it. The four bytes are
// stored raw (byte-identical to ROM, including the unknown $40 bit of byte 3);
// the accessors decode the packed fields, and of() names them when building
// generated rows. Note byte 0's high nibble stores a battle-type DISABLE mask
// that the loader inverts (eor #$00f0) — the accessors expose the possible
// types, and of() applies the inversion when packing.
struct FormationAux {
    std::uint8_t entranceAndTypes;  // byte 0: lo nibble entrance, hi nibble inverted battle-type mask
    std::uint8_t flags;             // byte 1
    std::uint8_t characterAi;       // byte 2
    std::uint8_t audioFlags;        // byte 3

    constexpr MonsterEntranceType entrance() const {
        return static_cast<MonsterEntranceType>(entranceAndTypes & 0x0F);
    }
    // The possible battle types after the loader's XOR: $10 front, $20 back,
    // $40 pincer, $80 side.
    constexpr std::uint8_t possibleBattleTypes() const {
        return static_cast<std::uint8_t>((entranceAndTypes & 0xF0) ^ 0xF0);
    }
    constexpr bool frontPossible() const {
        return (possibleBattleTypes() & 0x10) != 0;
    }
    constexpr bool backPossible() const {
        return (possibleBattleTypes() & 0x20) != 0;
    }
    constexpr bool pincerPossible() const {
        return (possibleBattleTypes() & 0x40) != 0;
    }
    constexpr bool sidePossible() const {
        return (possibleBattleTypes() & 0x80) != 0;
    }
    constexpr bool fanfareDisabled() const { return (flags & 0x02) != 0; }
    constexpr bool jokerDoomDisabled() const { return (flags & 0x04) != 0; }
    constexpr bool leapDisabled() const { return (flags & 0x08) != 0; }
    constexpr bool characterAiEnabled() const { return (flags & 0x80) != 0; }
    constexpr std::uint8_t characterAiIndex() const { return characterAi; }
    constexpr bool runningDisabled() const { return (audioFlags & 0x01) != 0; }
    constexpr bool veldtDisabled() const { return (audioFlags & 0x02) != 0; }
    constexpr bool preemptiveDisabled() const {
        return (audioFlags & 0x04) != 0;
    }
    constexpr BattleSong song() const {
        return static_cast<BattleSong>((audioFlags >> 3) & 0x07);
    }
    constexpr bool continueCurrentMusic() const {
        return (audioFlags & 0x80) != 0;
    }

    struct Fields {
        MonsterEntranceType entrance = MonsterEntranceType::PRE_DRAWN;
        bool frontPossible = false;
        bool backPossible = false;
        bool pincerPossible = false;
        bool sidePossible = false;
        bool fanfareDisabled = false;
        bool jokerDoomDisabled = false;
        bool leapDisabled = false;
        bool characterAiEnabled = false;
        std::uint8_t characterAi = 0;
        bool runningDisabled = false;
        bool veldtDisabled = false;
        bool preemptiveDisabled = false;
        BattleSong song = BattleSong::BATTLE_THEME;
        bool continueCurrentMusic = false;
        bool unknownBit40 = false;  // byte 3 $40 — no consumer found; preserved
    };
    static constexpr FormationAux of(const Fields& f) {
        const std::uint8_t possible = static_cast<std::uint8_t>(
            (f.frontPossible ? 0x10 : 0) | (f.backPossible ? 0x20 : 0) |
            (f.pincerPossible ? 0x40 : 0) | (f.sidePossible ? 0x80 : 0));
        FormationAux r{};
        r.entranceAndTypes = static_cast<std::uint8_t>(
            (possible ^ 0xF0) |
            (static_cast<std::uint8_t>(f.entrance) & 0x0F));
        r.flags = static_cast<std::uint8_t>(
            (f.fanfareDisabled ? 0x02 : 0) | (f.jokerDoomDisabled ? 0x04 : 0) |
            (f.leapDisabled ? 0x08 : 0) | (f.characterAiEnabled ? 0x80 : 0));
        r.characterAi = f.characterAi;
        r.audioFlags = static_cast<std::uint8_t>(
            (f.runningDisabled ? 0x01 : 0) | (f.veldtDisabled ? 0x02 : 0) |
            (f.preemptiveDisabled ? 0x04 : 0) |
            ((static_cast<std::uint8_t>(f.song) & 0x07) << 3) |
            (f.unknownBit40 ? 0x40 : 0) | (f.continueCurrentMusic ? 0x80 : 0));
        return r;
    }
};

static_assert(sizeof(FormationAux) == 4,
              "FormationAux must be byte-identical to a 4-byte battle_prop "
              "record");

// One conditional-battle substitution (cond_battle.dat, ROM CF/3780): when a
// conditional-battle flag is set, the loader replaces the trigger formation
// with the replacement (battle_main.asm:7945-7955). Only entries 0-7 are
// reachable (flag bits 8-15 of $3EB9, entry i <-> bit 8+i); entries 8-15 are
// dead ROM bytes, ported for byte fidelity.
struct ConditionalBattle {
    FormationRef trigger;
    FormationRef replacement;
};

static_assert(sizeof(ConditionalBattle) == 4,
              "ConditionalBattle must be byte-identical to a cond_battle "
              "entry (two formation words)");

// One table entry: the record's identity as the FormationId enumerator
// alongside the packed record. A compile-time assert verifies id == position.
struct FormationEntry {
    FormationId id;
    Formation record;
};
struct FormationAuxEntry {
    FormationId id;
    FormationAux record;
};

// battle_monsters: the monsters and layout of every formation.
const Formation& getFormation(FormationId id);
std::span<const FormationEntry> formations();

// battle_prop: how each formation's battle begins.
const FormationAux& getFormationAux(FormationId id);
std::span<const FormationAuxEntry> formationAux();

// cond_battle: index 0-15 (only 0-7 are reachable in-game).
const ConditionalBattle& getConditionalBattle(std::size_t index);
std::span<const ConditionalBattle> conditionalBattles();

}  // namespace ostinato
