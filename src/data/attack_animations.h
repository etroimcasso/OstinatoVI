// Attack / weapon / item animation property tables from the btlgfx data layer.
// The row data is generated (src/data/generated/*.inc); this header owns the
// record types, the entry types, and the accessors. Every consumer that
// interprets these rows at runtime — the animation player, the script
// interpreter — lives in the battle-graphics code above this layer; this
// layer is the typed, byte-identical data those consumers read.
//
// Sources (all original-src @ 1ea47b5):
//   * AttackAnimProp            btlgfx_main.asm:48939 (d0/7fb2), 406 x 14 B
//   * AttackGfxProp             btlgfx_main.asm:48889 (d4/d000), 650 x 6 B
//   * WeaponAnimProp            btlgfx_main.asm:48897 (ec/e400), 93 x 8 B
//   * MonsterAttackAnimProp     btlgfx_main.asm:48905 (ec/e6e8), 35 x 8 B
//   * ItemJumpThrowAnim         btlgfx_main.asm:49015 (d1/0040), 257 x 1 B
//   * ItemAnimPtrs              btlgfx_main.asm:48980 (d1/0000), 32 words
//   * MonsterOverlap            monster_overlap.asm:6 (cf/3600), 384 x 1 B
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/animation_init_function.h"
#include "ostinato/animation_ref.h"
#include "ostinato/animation_tile_offset.h"
#include "ostinato/attack_animation_index.h"
#include "ostinato/item_id.h"
#include "ostinato/item_throw_animation.h"
#include "ostinato/monster_attack_animation.h"
#include "ostinato/monster_id.h"
#include "ostinato/thrown_animation_flags.h"

namespace ostinato {

// --- AttackAnimProp: one 14-byte attack-animation-properties record ----------
// Field order and widths mirror InitAnimProp's copy loop + reads
// (btlgfx_main.asm:23567-23652): three animation words (sprite / bg1 / bg3),
// three palettes, a default sound effect, the init/special-function byte, the
// special-graphics word, and the multi-target delay. AnimationRef is an
// alignment-1 byte pair (its own header explains why), so the record needs no
// endianness assumption and stays sizeof-locked at 14.
struct AttackAnimationProperties {
    AnimationRef spriteAnimation;
    AnimationRef bg1Animation;
    AnimationRef bg3Animation;
    std::uint8_t spritePalette;
    std::uint8_t bg1Palette;
    std::uint8_t bg3Palette;
    std::uint8_t defaultSoundEffect;
    AnimationInitFunction initFunction;
    AnimationRef specialGraphics;
    std::uint8_t multiTargetDelay;
};

static_assert(sizeof(AttackAnimationProperties) == 14,
              "AttackAnimationProperties must be byte-identical to a 14-byte "
              "AttackAnimProp record");
// The byte offsets ARE the contract (InitAnimProp's w7e6273+N stores/reads).
static_assert(offsetof(AttackAnimationProperties, spriteAnimation) == 0);
static_assert(offsetof(AttackAnimationProperties, bg1Animation) == 2);
static_assert(offsetof(AttackAnimationProperties, bg3Animation) == 4);
static_assert(offsetof(AttackAnimationProperties, spritePalette) == 6);
static_assert(offsetof(AttackAnimationProperties, bg1Palette) == 7);
static_assert(offsetof(AttackAnimationProperties, bg3Palette) == 8);
static_assert(offsetof(AttackAnimationProperties, defaultSoundEffect) == 9);
static_assert(offsetof(AttackAnimationProperties, initFunction) == 10);
static_assert(offsetof(AttackAnimationProperties, specialGraphics) == 11);
static_assert(offsetof(AttackAnimationProperties, multiTargetDelay) == 13);

// One table entry: the row's identity as a decimal index (the 406-row space has
// no symbolic names — it is segmented by command only at runtime) alongside the
// packed record. A compile-time assert verifies index == array position.
struct AttackAnimationPropertiesEntry {
    std::uint16_t index;
    AttackAnimationProperties record;
};

// --- AttackGfxProp: one 6-byte animation-graphics-properties record ----------
// Field order per LoadAnimGfxProp (btlgfx_main.asm:24224-24242): the tile/2bpp
// word, the frame-data index (a row index into the AttackAnimFrames table), and
// the frame width/height. The u16 fields hold ROM little-endian values.
struct AnimationGraphicsProperties {
    AnimationTileOffset tileOffset;
    std::uint16_t frameDataIndex;
    std::uint8_t frameWidth;
    std::uint8_t frameHeight;
};

static_assert(sizeof(AnimationGraphicsProperties) == 6,
              "AnimationGraphicsProperties must be byte-identical to a 6-byte "
              "AttackGfxProp record");
static_assert(std::endian::native == std::endian::little,
              "AnimationGraphicsProperties u16 fields assume a little-endian "
              "platform");
static_assert(offsetof(AnimationGraphicsProperties, tileOffset) == 0);
static_assert(offsetof(AnimationGraphicsProperties, frameDataIndex) == 2);
static_assert(offsetof(AnimationGraphicsProperties, frameWidth) == 4);
static_assert(offsetof(AnimationGraphicsProperties, frameHeight) == 5);

struct AnimationGraphicsPropertiesEntry {
    std::uint16_t index;
    AnimationGraphicsProperties record;
};

// --- WeaponAnimProp / MonsterAttackAnimProp: one shared 8-byte record --------
// The two tables share a record shape and the same copy loop
// (InitWeaponAnim, btlgfx_main.asm:23661-23735): two per-hand animation bytes,
// the weapon palette, the hit-animation script, the hit palette, the
// thrown/flags byte, the sound effect, and a trailing pad byte that is 0 on
// every row and read by no consumer.
struct WeaponAnimationProperties {
    std::array<std::uint8_t, 2> handAnimations;
    std::uint8_t weaponPalette;
    std::uint8_t hitAnimation;
    std::uint8_t hitPalette;
    ThrownAnimationFlags thrown;
    std::uint8_t soundEffect;
    std::uint8_t pad7;
};

static_assert(sizeof(WeaponAnimationProperties) == 8,
              "WeaponAnimationProperties must be byte-identical to an 8-byte "
              "weapon/monster attack-animation record");
static_assert(offsetof(WeaponAnimationProperties, handAnimations) == 0);
static_assert(offsetof(WeaponAnimationProperties, weaponPalette) == 2);
static_assert(offsetof(WeaponAnimationProperties, hitAnimation) == 3);
static_assert(offsetof(WeaponAnimationProperties, hitPalette) == 4);
static_assert(offsetof(WeaponAnimationProperties, thrown) == 5);
static_assert(offsetof(WeaponAnimationProperties, soundEffect) == 6);
static_assert(offsetof(WeaponAnimationProperties, pad7) == 7);

// A weapon row: the weapon item (rows 0-92 are ITEM ids) alongside its record.
struct WeaponAnimationPropertiesEntry {
    ItemId item;
    WeaponAnimationProperties record;
};

// A monster-attack row: the animation (the 35-row MonsterAttackAnimation enum
// IS this table's index space) alongside its record.
struct MonsterAttackAnimationPropertiesEntry {
    MonsterAttackAnimation animation;
    WeaponAnimationProperties record;
};

// --- ItemJumpThrowAnim: the packed jump/throw byte per item ------------------
// Row 0 is the unarmed slot; rows 1-256 are the 256 items (the consumers `inc`
// the item id before indexing, btlgfx_main.asm:27504/27577-27581).
struct ItemThrowAnimationEntry {
    ItemId item;
    ItemThrowAnimation animation;
};

// The unarmed row (index 0) and the 256 item rows (keyed by ItemId).
#include "data/generated/item_jump_throw_anim_data.inc"

// --- ItemAnimPtrs: the usable-item attack-animation references ---------------
// 32 entries for the usable items ($e0-$ff); each names an AttackAnimProp row
// or AttackAnimationIndex::NONE.
struct UsableItemAnimationEntry {
    ItemId item;
    AttackAnimationIndex animation;
};

// --- MonsterOverlap: the per-monster sprite-priority y shift ------------------
struct MonsterOverlapEntry {
    MonsterId monster;
    std::uint8_t yShift;
};

// --- accessors ---------------------------------------------------------------

// The attack-animation properties for a row (0-405). index must be in range.
const AttackAnimationProperties& attackAnimationProperties(std::uint16_t index);
std::span<const AttackAnimationPropertiesEntry> attackAnimationProperties();

// The animation-graphics properties for a row (0-649). index must be in range.
const AnimationGraphicsProperties& animationGraphicsProperties(
    std::uint16_t index);
std::span<const AnimationGraphicsPropertiesEntry> animationGraphicsProperties();

// The weapon animation properties for a weapon item (ids 0-92).
const WeaponAnimationProperties& weaponAnimationProperties(ItemId item);
std::span<const WeaponAnimationPropertiesEntry> weaponAnimationProperties();

// The monster attack-animation properties for an animation row.
const WeaponAnimationProperties& monsterAttackAnimationProperties(
    MonsterAttackAnimation animation);
std::span<const MonsterAttackAnimationPropertiesEntry>
monsterAttackAnimationProperties();

// The jump/throw animation for an item.
ItemThrowAnimation itemThrowAnimation(ItemId item);
std::span<const ItemThrowAnimationEntry> itemThrowAnimations();

// The attack-animation reference for a usable item ($e0-$ff); other items have
// no entry (their runtime path lives in the battle-graphics code).
std::span<const UsableItemAnimationEntry> usableItemAnimations();

// The sprite-priority y shift for a monster (0-383).
std::uint8_t monsterOverlap(MonsterId monster);
std::span<const MonsterOverlapEntry> monsterOverlaps();

}  // namespace ostinato
