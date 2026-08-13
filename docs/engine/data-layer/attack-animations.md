# Attack / weapon / item animation data

The battle-graphics data layer: seven tables that say *which* animation, graphics,
palette, and sound each attack, weapon, and thrown item uses, plus how a monster's
sprite is nudged for overlap. These tables are the id-and-property data the battle
animation player reads; the animation playback itself (frame timing, script
interpretation) lives in the battle-graphics code above this layer.

## Public surface

```cpp
#include "data/attack_animations.h"

using ostinato::ItemId;
using ostinato::MonsterId;
using ostinato::MonsterAttackAnimation;

// Attack-animation properties, by row index (0-405):
const auto& a = ostinato::attackAnimationProperties(1);
a.spriteAnimation.index();      // 220
a.bg1Animation.hasHighBit();    // true  (the high flag is set on this row)
a.defaultSoundEffect;           // 23    (a sound id — numbered, like a sound test)

// Animation-graphics properties, by row index (0-649):
const auto& g = ostinato::animationGraphicsProperties(0);
g.tileOffset.is2bpp();          // graphics depth flag
g.frameDataIndex;               // row into the frame-data table

// Weapon and monster attack-animation records (shared 8-byte shape):
const auto& w = ostinato::weaponAnimationProperties(ItemId::DIRK);
const auto& m = ostinato::monsterAttackAnimationProperties(
    MonsterAttackAnimation::HIT);

// The packed jump/throw byte for an item:
auto t = ostinato::itemThrowAnimation(ItemId::FIRE_SKEAN);
t.throwAnimation();             // ThrowAnimationClass::FIRE_SKEAN

// The attack animation a usable item plays ($e0-$ff):
for (const auto& e : ostinato::usableItemAnimations())
    if (!e.animation.isNone()) /* e.item plays AttackAnimProp row e.animation.index() */;

// A monster's sprite-overlap y shift (0-383):
ostinato::monsterOverlap(MonsterId::TENTACLE);   // 72
```

Every table also exposes a full-table span for iteration —
`attackAnimationProperties()`, `animationGraphicsProperties()`,
`weaponAnimationProperties()`, `monsterAttackAnimationProperties()`,
`itemThrowAnimations()`, `usableItemAnimations()`, `monsterOverlaps()`.

## Typed values

Several fields are packed words or bytes rather than plain numbers; each gets a
small wrapper that names its parts. The raw values that are *just* ids — palette
slots, sound effects, frame indices, animation-script indices — stay decimal
numbers (they have no names in the original, the way sound-test entries are
numbered).

```cpp
struct AnimationRef {           // sizeof == 2 — an animation word
    bool isNone() const;        // $ffff: no animation
    std::uint16_t index() const;// low 15 bits
    bool hasHighBit() const;    // bit 15 — a per-row flag
    static AnimationRef of(std::uint16_t index);
    static AnimationRef withHighBit(std::uint16_t index);
    static const AnimationRef NONE;
};

struct AnimationTileOffset {    // sizeof == 2 — AttackGfxProp's tile word
    bool is2bpp() const;        // bit 15 — 2bpp vs 4bpp graphics
    std::uint16_t offset() const;
    static AnimationTileOffset of(std::uint16_t offset);
    static AnimationTileOffset of2bpp(std::uint16_t offset);
};

struct AnimationInitFunction {  // sizeof == 1 — AttackAnimProp's init byte
    std::uint8_t index() const; // low 7 bits — the init/special-function index
    bool hasHighBit() const;    // bit 7 — a flag
    static AnimationInitFunction of(std::uint8_t index);
    static AnimationInitFunction withHighBit(std::uint8_t index);
};

struct AttackAnimationIndex {   // sizeof == 2 — a usable-item animation ref
    bool isNone() const;        // $ffff: no animation
    std::uint16_t index() const;// an AttackAnimProp row
    static AttackAnimationIndex of(std::uint16_t index);
    static const AttackAnimationIndex NONE;
};
```

## AttackAnimProp — 406 records × 14 bytes

```cpp
struct AttackAnimationProperties {
    AnimationRef spriteAnimation;      // +0
    AnimationRef bg1Animation;         // +2
    AnimationRef bg3Animation;         // +4
    std::uint8_t spritePalette;        // +6
    std::uint8_t bg1Palette;           // +7
    std::uint8_t bg3Palette;           // +8
    std::uint8_t defaultSoundEffect;   // +9
    AnimationInitFunction initFunction;// +10
    AnimationRef specialGraphics;      // +11
    std::uint8_t multiTargetDelay;     // +13
};
static_assert(sizeof(AttackAnimationProperties) == 14);
```

One record per attack animation. The three animation words and the special-graphics
word are `AnimationRef`s (with `NONE` for the `$ffff` "unused" rows); palettes, the
sound effect, and the multi-target delay are numbered ids. The 406-row space has no
names — rows are addressed by decimal index, and a compile-time assert pins each
entry to its position.

## AttackGfxProp — 650 records × 6 bytes

```cpp
struct AnimationGraphicsProperties {
    AnimationTileOffset tileOffset;  // +0  (bit 15 = 2bpp)
    std::uint16_t frameDataIndex;    // +2  (a row into the frame-data table)
    std::uint8_t frameWidth;         // +4
    std::uint8_t frameHeight;        // +5
};
static_assert(sizeof(AnimationGraphicsProperties) == 6);
```

The graphics half of an animation: which tiles, which frame-data row, and the frame
size. `frameDataIndex` is validated to stay within the frame-data table's bounds.

## Weapon & monster attack animations — 93 + 35 records × 8 bytes

Both tables share one record shape. Weapon rows are keyed by the weapon `ItemId`
(ids 0-92); monster rows by `MonsterAttackAnimation` (that 35-value enum is the
table's index space).

```cpp
struct WeaponAnimationProperties {
    std::array<std::uint8_t, 2> handAnimations;  // +0  per-hand animation ids
    std::uint8_t weaponPalette;                  // +2
    std::uint8_t hitAnimation;                   // +3
    std::uint8_t hitPalette;                     // +4
    ThrownAnimationFlags thrown;                 // +5
    std::uint8_t soundEffect;                    // +6
    std::uint8_t pad7;                           // +7  always 0
};
static_assert(sizeof(WeaponAnimationProperties) == 8);
```

The `thrown` byte carries a flag and a small type:

```cpp
enum class WeaponAnimationType : std::uint8_t {
    UNKNOWN_0 = 0, STAR_OR_GAMBLER = 1, UNKNOWN_2, UNKNOWN_3, UNKNOWN_4,
};
struct ThrownAnimationFlags {          // sizeof == 1
    bool isThrown() const;             // bit 7
    WeaponAnimationType animationType() const;  // low 7 bits
    static ThrownAnimationFlags of(WeaponAnimationType type);
    static ThrownAnimationFlags thrown(WeaponAnimationType type);
};
```

Only type 1 has a name in the original (its "star or gambler" special case); the
rest use `UNKNOWN_n`, and the backing data keeps the low bits within 0-4. `pad7` is
a trailing byte that is 0 on every row and read by nothing — it is kept so the
record stays byte-identical to the ROM.

## ItemJumpThrowAnim — one packed byte per item

```cpp
enum class JumpAnimationClass  : std::uint8_t { UNARMED, THICK_KNIFE, /* 8 */ };
enum class ThrowAnimationClass : std::uint8_t { THICK_KNIFE, THIN_KNIFE, /* 16 */ };

struct ItemThrowAnimation {            // sizeof == 1
    bool usesFightAnimation() const;   // bit 7 — use the normal fight animation
    JumpAnimationClass jumpAnimation() const;   // bits 4-6
    ThrowAnimationClass throwAnimation() const; // bits 0-3
    static ItemThrowAnimation of(JumpAnimationClass, ThrowAnimationClass);
    static ItemThrowAnimation fightAnimation(JumpAnimationClass, ThrowAnimationClass);
};

ItemThrowAnimation itemThrowAnimation(ItemId item);
```

`itemThrowAnimations()` yields the 256 item rows keyed by `ItemId`;
`kUnarmedItemThrowAnimation` is the separate row-0 unarmed slot. The class names come
from the original's own comment block; the two throw values it marks "???" keep
`UNKNOWN_nn` names.

## ItemAnimPtrs — usable-item animations

32 entries for the usable items (`ItemId` `$e0`-`$ff`); each names an `AttackAnimProp`
row via `AttackAnimationIndex`, or `NONE`. In the original these are pre-multiplied
byte offsets (row × 14); the port stores the row index and the test re-multiplies to
prove byte fidelity.

## MonsterOverlap — sprite-priority y shift

```cpp
struct MonsterOverlapEntry { MonsterId monster; std::uint8_t yShift; };
std::uint8_t monsterOverlap(MonsterId monster);   // 0-383
```

One decimal y-shift magnitude per monster (all 384); 14 monsters carry a non-zero
shift (e.g. `TENTACLE` = 72), the rest are 0.

## Backing data / where to change

Rows live in `src/data/generated/` — `attack_anim_prop_data.inc`,
`attack_gfx_prop_data.inc`, `weapon_anim_prop_data.inc`,
`monster_attack_anim_prop_data.inc`, `item_jump_throw_anim_data.inc`,
`item_anim_ptrs_data.inc`, `monster_overlap_data.inc` — each included into its
table's array in `src/data/attack_animations.cpp` (the item-jump-throw rows are
included at namespace scope in `src/data/attack_animations.h`). Values are named
where a name exists and decimal ids otherwise, so most edits are one field:

```cpp
    { ItemId::DIRK, WeaponAnimationProperties{
        .handAnimations = { 0, 0 },
        .weaponPalette  = 2,
        .hitAnimation   = 5,
        .hitPalette     = 2,
        .thrown         = ThrownAnimationFlags::of(WeaponAnimationType::UNKNOWN_0),
        .soundEffect    = 12,
        .pad7           = 0,
    } },
```

Keep the structural rules intact when editing: `pad7` stays 0, the thrown type stays
within the five `WeaponAnimationType` values, and each wrapper-entry table's index
field stays equal to its position (compile-time asserts verify this). A deliberate
change must also update the matching row in the table's fixture under
`tests/fixtures/` (`attack_anim_prop_expected.h`, `attack_gfx_prop_expected.h`,
`weapon_anim_prop_expected.h`, `monster_attack_anim_prop_expected.h`,
`item_jump_throw_anim_expected.h`, `item_anim_ptrs_expected.h`,
`monster_overlap_expected.h`), which carry the original ROM bytes.

## What's tested

`tests/test_attack_animations.cpp` — every record of all seven tables compared in
full against its fixture (no subsets): the packed records by a direct memory compare
of the record bytes, the item-jump-throw sequence reassembled and compared, and the
usable-item references re-multiplied back to the original words. Plus the typed-wrapper
round-trips and hand-traced spot checks (a high-bit animation row, a 2bpp graphics
row, the RENAME_CARD usable-item animation, the DIRK jump/throw decode, TENTACLE's
overlap, and a thrown star/gambler weapon). The Japanese-ROM variants of the two
language-specific tables are registered as visible skips until that ROM is
extractable.
