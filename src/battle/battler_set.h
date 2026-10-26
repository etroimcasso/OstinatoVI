// The ten places a battle has room for, and sets of them.
//
// A battle holds up to four party members and up to six monsters. Each has a BattlerSlot: party
// positions 0-3 and monster positions 0-5. A BattlerSet names any group of them — the targets of
// an attack, the battlers still alive, the characters who just escaped.
//
//     BattlerSet targets = BattlerSet::of(BattlerSlot::monster(2));
//     targets.add(BattlerSlot::monster(3));
//     if (targets.has(BattlerSlot::monster(2))) { ... }
//     targets.count();                                    // 2
//
// A BattlerSet is the two bytes the original keeps for a target list, the party's byte first, so
// it compares equal to the original's targets byte for byte.
#pragma once

#include <cstdint>

#include "ostinato/battle_slot_mask.h"

namespace ostinato::battle {

inline constexpr int kPartySlots = 4;
inline constexpr int kMonsterSlots = 6;
inline constexpr int kBattlerSlots = kPartySlots + kMonsterSlots;

// One of the ten places in a battle. The party's four come first (index 0-3), then the six
// monsters' (index 4-9).
class BattlerSlot {
public:
    // Party member `position` (0-3).
    static constexpr BattlerSlot character(int position) {
        return BattlerSlot(static_cast<std::uint8_t>(position));
    }

    // Monster `position` (0-5).
    static constexpr BattlerSlot monster(int position) {
        return BattlerSlot(static_cast<std::uint8_t>(kPartySlots + position));
    }

    // The slot with index `index` (0-9).
    static constexpr BattlerSlot fromIndex(int index) {
        return BattlerSlot(static_cast<std::uint8_t>(index));
    }

    // 0-9: the party's slots, then the monsters'.
    constexpr int index() const { return index_; }

    constexpr bool isCharacter() const { return index_ < kPartySlots; }
    constexpr bool isMonster() const { return !isCharacter(); }

    // The position within the slot's own side: 0-3 for the party, 0-5 for monsters.
    constexpr int position() const { return isCharacter() ? index_ : index_ - kPartySlots; }

    friend constexpr bool operator==(BattlerSlot, BattlerSlot) = default;

private:
    explicit constexpr BattlerSlot(std::uint8_t index) : index_(index) {}

    std::uint8_t index_;
};

// A set of battle slots: one bit per party position and one per monster position.
struct BattlerSet {
    BattleSlotMask characters;
    BattleSlotMask monsters;

    static constexpr BattlerSet of(BattlerSlot slot) {
        BattlerSet set;
        set.add(slot);
        return set;
    }

    static constexpr BattlerSet allCharacters() {
        return BattlerSet{BattleSlotMask(0x0F), BattleSlotMask(0x00)};
    }

    static constexpr BattlerSet allMonsters() {
        return BattlerSet{BattleSlotMask(0x00), BattleSlotMask(0x3F)};
    }

    constexpr bool has(BattlerSlot slot) const {
        return side(slot).has(static_cast<std::uint8_t>(slot.position()));
    }

    constexpr void add(BattlerSlot slot) {
        side(slot).bits |= bit(slot);
    }

    constexpr void remove(BattlerSlot slot) {
        side(slot).bits &= static_cast<std::uint8_t>(~bit(slot));
    }

    constexpr bool empty() const { return characters.bits == 0 && monsters.bits == 0; }

    constexpr int count() const { return popcount(characters.bits) + popcount(monsters.bits); }

    // The party's members of the set, or its monsters.
    constexpr BattlerSet charactersOnly() const { return BattlerSet{characters, BattleSlotMask()}; }
    constexpr BattlerSet monstersOnly() const { return BattlerSet{BattleSlotMask(), monsters}; }

    friend constexpr BattlerSet operator|(BattlerSet a, BattlerSet b) {
        return BattlerSet{BattleSlotMask(static_cast<std::uint8_t>(a.characters.bits | b.characters.bits)),
                          BattleSlotMask(static_cast<std::uint8_t>(a.monsters.bits | b.monsters.bits))};
    }

    friend constexpr BattlerSet operator&(BattlerSet a, BattlerSet b) {
        return BattlerSet{BattleSlotMask(static_cast<std::uint8_t>(a.characters.bits & b.characters.bits)),
                          BattleSlotMask(static_cast<std::uint8_t>(a.monsters.bits & b.monsters.bits))};
    }

    // The members of this set that are not in `other`.
    constexpr BattlerSet without(BattlerSet other) const {
        return BattlerSet{BattleSlotMask(static_cast<std::uint8_t>(characters.bits & ~other.characters.bits)),
                          BattleSlotMask(static_cast<std::uint8_t>(monsters.bits & ~other.monsters.bits))};
    }

    friend constexpr bool operator==(BattlerSet a, BattlerSet b) {
        return a.characters.bits == b.characters.bits && a.monsters.bits == b.monsters.bits;
    }

private:
    constexpr BattleSlotMask& side(BattlerSlot slot) { return slot.isCharacter() ? characters : monsters; }
    constexpr const BattleSlotMask& side(BattlerSlot slot) const {
        return slot.isCharacter() ? characters : monsters;
    }

    static constexpr std::uint8_t bit(BattlerSlot slot) {
        return static_cast<std::uint8_t>(1u << slot.position());
    }

    static constexpr int popcount(std::uint8_t bits) {
        int n = 0;
        for (; bits != 0; bits = static_cast<std::uint8_t>(bits & (bits - 1))) {
            ++n;
        }
        return n;
    }
};

static_assert(sizeof(BattlerSet) == 2, "BattlerSet must be the two bytes of a target list");

}  // namespace ostinato::battle
