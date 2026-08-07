// Attack properties: the 256-record magic_prop table. The row data is
// generated (src/data/generated/magic_prop_en_data.inc); this header owns the
// record type, the entry type, and the accessors.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/attack_effects.h"
#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/element_set.h"
#include "ostinato/status_set.h"
#include "ostinato/targeting.h"

namespace ostinato {

// One 14-byte attack-properties record (magic_prop_en.dat, ROM C4/6AC0).
// Member order and widths mirror the record layout documented in
// original-src/notes/battle-ram.txt:208-249 (the $11A0 spell-mode block
// LoadMagicProp copies records into), so the object representation is
// byte-identical to a ROM record — pinned by the static_asserts below and the
// full-corpus byte-equivalence test. The table spans the full unified ATTACK
// index space (spells, esper attacks, skills, monster specials), hence
// "Attack", not "Magic".
struct AttackProperties {
    Targeting targeting;
    ElementSet element;
    AttackTraitSet traits;
    AttackFlags1 flags1;
    AttackFlags2 flags2;
    std::uint8_t mpCost;
    std::uint8_t power;
    AttackMiscFlags misc;
    std::uint8_t hitRate;
    AttackSpecialEffect specialEffect;
    StatusSet statuses;
};

static_assert(sizeof(AttackProperties) == 14,
              "AttackProperties must be byte-identical to a 14-byte magic_prop record");
// The byte offsets ARE the contract (battle-ram.txt:210-249).
static_assert(offsetof(AttackProperties, targeting) == 0);
static_assert(offsetof(AttackProperties, element) == 1);
static_assert(offsetof(AttackProperties, traits) == 2);
static_assert(offsetof(AttackProperties, flags1) == 3);
static_assert(offsetof(AttackProperties, flags2) == 4);
static_assert(offsetof(AttackProperties, mpCost) == 5);
static_assert(offsetof(AttackProperties, power) == 6);
static_assert(offsetof(AttackProperties, misc) == 7);
static_assert(offsetof(AttackProperties, hitRate) == 8);
static_assert(offsetof(AttackProperties, specialEffect) == 9);
static_assert(offsetof(AttackProperties, statuses) == 10);

// One table entry: the record's identity as a typed field (the AttackId
// enumerator — identity is a field, never a comment) alongside the packed
// record, which stays sizeof-locked to the ROM bytes. Every generated row
// reads { .id = AttackId::NAME, .record = { ... } }; a compile-time assert
// verifies id == array position for every entry.
struct AttackPropertiesEntry {
    AttackId id;
    AttackProperties record;
};

// The record for an attack, from the English-language table. The table is
// language-variant upstream (magic_prop_en.dat / magic_prop_jp.dat rip as
// separate files); a Language dispatch axis is added when the JP table becomes
// rippable — until then the EN table is the sole backing store.
const AttackProperties& getAttackProperties(AttackId id);

// The full 256-entry EN table (ATTACK index order), for iteration and
// full-corpus tests.
std::span<const AttackPropertiesEntry> attackPropertiesEn();

}  // namespace ostinato
