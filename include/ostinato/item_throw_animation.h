// The packed jump/throw animation byte from ItemJumpThrowAnim (one per item,
// plus an unarmed row 0). The upstream lays it out as `djjjtttt`
// (btlgfx_main.asm:27469-27498): bit 7 (d) forces the normal "fight" animation
// when the item is thrown, bits 4-6 (jjj) select the jump animation, and bits
// 0-3 (tttt) select the throw animation. The throw command reads the low nibble
// (:27514 "and #$0f") and the jump command the middle bits (:27582-27584,
// "and #$7f" then lsr4). The two class enums below name every value from the
// upstream comment block; the two "???" throw rows keep UNKNOWN_nn names.
#pragma once

#include <cstdint>

namespace ostinato {

// The jump-animation class (bits 4-6). Names from btlgfx_main.asm:27474-27481.
enum class JumpAnimationClass : std::uint8_t {
    UNARMED         = 0,
    THICK_KNIFE     = 1,
    THIN_KNIFE      = 2,
    SWORD           = 3,
    KATANA          = 4,
    ROD             = 5,
    SPEAR           = 6,
    HAWK_EYE_SNIPER = 7,
};

// The throw-animation class (bits 0-3). Names from btlgfx_main.asm:27483-27498;
// the two rows the source marks "???" keep UNKNOWN_nn names.
enum class ThrowAnimationClass : std::uint8_t {
    THICK_KNIFE                       = 0x0,
    THIN_KNIFE                        = 0x1,
    SWORD                             = 0x2,
    KATANA                            = 0x3,
    ROD                               = 0x4,
    SPEAR                             = 0x5,
    HAWK_EYE_SNIPER                   = 0x6,
    UNKNOWN_07                        = 0x7,
    FIRE_SKEAN                        = 0x8,
    WATER_EDGE                        = 0x9,
    BOLT_EDGE                         = 0xA,
    INVIZ_EDGE                        = 0xB,
    SHADOW_EDGE                       = 0xC,
    FULL_MOON_MORNING_STAR_RISING_SUN = 0xD,
    BOOMERANG                         = 0xE,
    UNKNOWN_0F                        = 0xF,
};

struct ItemThrowAnimation {
    std::uint8_t bits = 0;

    // Bit 7: use the normal "fight" animation when thrown (ignore the throw
    // class).
    constexpr bool usesFightAnimation() const { return (bits & 0x80) != 0; }
    // Bits 4-6: the jump animation class.
    constexpr JumpAnimationClass jumpAnimation() const {
        return static_cast<JumpAnimationClass>((bits >> 4) & 0x07);
    }
    // Bits 0-3: the throw animation class.
    constexpr ThrowAnimationClass throwAnimation() const {
        return static_cast<ThrowAnimationClass>(bits & 0x0F);
    }

    static constexpr std::uint8_t pack(bool fight, JumpAnimationClass jump,
                                       ThrowAnimationClass thrown) {
        return static_cast<std::uint8_t>(
            (fight ? 0x80 : 0) |
            ((static_cast<std::uint8_t>(jump) & 0x07) << 4) |
            (static_cast<std::uint8_t>(thrown) & 0x0F));
    }
    // Builders so every row names its jump/throw classes — never a raw byte.
    static constexpr ItemThrowAnimation of(JumpAnimationClass jump,
                                           ThrowAnimationClass thrown) {
        return ItemThrowAnimation{pack(false, jump, thrown)};
    }
    static constexpr ItemThrowAnimation fightAnimation(JumpAnimationClass jump,
                                                       ThrowAnimationClass thrown) {
        return ItemThrowAnimation{pack(true, jump, thrown)};
    }
};

static_assert(sizeof(ItemThrowAnimation) == 1,
              "ItemThrowAnimation must be byte-identical to the ROM byte");
static_assert(
    ItemThrowAnimation::of(JumpAnimationClass::UNARMED,
                           ThrowAnimationClass::THICK_KNIFE).bits == 0x00 &&
        ItemThrowAnimation::of(JumpAnimationClass::SPEAR,
                               ThrowAnimationClass::BOOMERANG).bits == 0x6E &&
        ItemThrowAnimation::of(JumpAnimationClass::SPEAR,
                               ThrowAnimationClass::BOOMERANG)
                .jumpAnimation() == JumpAnimationClass::SPEAR &&
        ItemThrowAnimation::of(JumpAnimationClass::SPEAR,
                               ThrowAnimationClass::BOOMERANG)
                .throwAnimation() == ThrowAnimationClass::BOOMERANG &&
        ItemThrowAnimation::fightAnimation(JumpAnimationClass::UNARMED,
                                           ThrowAnimationClass::THICK_KNIFE)
                .bits == 0x80 &&
        ItemThrowAnimation::fightAnimation(JumpAnimationClass::UNARMED,
                                           ThrowAnimationClass::THICK_KNIFE)
            .usesFightAnimation(),
    "ItemThrowAnimation builders must round-trip the flag and both classes");

}  // namespace ostinato
